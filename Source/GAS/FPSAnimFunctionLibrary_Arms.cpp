// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSAnimFunctionLibrary.h"
#include "Kismet/KismetMathLibrary.h"

// ============================================================================
// 内部常量：用于从 CFLF_calMoveImpulseTarget 向 CFLF_solveDualSpringSway 
// 无损传递单帧落地动量冲击的编码阈值（无需修改蓝图引脚签名）
// ============================================================================
namespace HandSwayInternal
{
	constexpr float LANDING_SIGNAL_OFFSET = -1000.0f;
	constexpr float LANDING_SIGNAL_THRESHOLD = -500.0f;
}

// ----------------------------------------------------------------------------
// 武器后坐力 2.0 独立物理二阶弹簧解算器 (人话物理参数版)
// ----------------------------------------------------------------------------
void UFPSAnimFunctionLibrary::CFLF_SolveRecoilSpringV2(
	bool bTriggerRecoil,
	float KickBackCm,
	float KickUpDeg,
	float KickSideDeg,
	float ReturnTimeSec,
	float Bounciness,
	float DeltaTime,
	const FRotator& CurrentRecoilRot,
	const FVector& CurrentRecoilTrans,
	FVectorSpringState& RecoilRotSpringState,
	FVectorSpringState& RecoilTransSpringState,
	FRotator& OutRecoilRot,
	FVector& OutRecoilTrans)
{
	const float SafeDeltaTime = FMath::Max(DeltaTime, 0.0001f);
	const float SafeReturnTime = FMath::Max(0.05f, ReturnTimeSec);
	const float ClampedBounciness = FMath::Clamp(Bounciness, 0.0f, 1.0f);

	// 1. 弹性 -> 阻尼比 (0.0=1.0 临界阻尼无余震, 1.0=0.45 强欠阻尼)
	const float Zeta = FMath::Lerp(1.0f, 0.45f, ClampedBounciness);

	// 2. 回中时间 -> 自然角频率与刚度
	const float OmegaN = 4.0f / (Zeta * SafeReturnTime);
	const float Stiffness = OmegaN * OmegaN;

	// 3. 击发瞬间初速度脉冲注入 (单帧脉冲)
	if (bTriggerRecoil)
	{
		const float VelocityFactor = OmegaN * (1.0f + 1.718f * Zeta);

		// 枪口上跳 KickUpDeg -> RotState.Velocity.X (Component Space Roll 俯仰)
		// 符号取反 (加负号)：使其开火时枪口向上扬起而非下压
		const float V0_Pitch = -KickUpDeg * VelocityFactor;

		// 左右随机抖动 ±KickSideDeg -> RotState.Velocity.Z (Yaw 横摆) 与 0.4x 侧翻 (Velocity.Y)
		const float RandomSide = FMath::FRandRange(-KickSideDeg, KickSideDeg);
		const float V0_Yaw = RandomSide * VelocityFactor;
		const float V0_Roll = V0_Yaw * 0.4f;

		// 枪身后冲 -KickBackCm -> TransState.Velocity.Y (Component Space 前后冲退)
		const float V0_Back = KickBackCm * VelocityFactor;

		RecoilRotSpringState.Velocity.X += V0_Pitch;
		RecoilRotSpringState.Velocity.Y += V0_Roll;
		RecoilRotSpringState.Velocity.Z += V0_Yaw;

		RecoilTransSpringState.Velocity.Y += -V0_Back;

		// 安全极值限幅 (高射速连发防穿模甩飞)
		RecoilRotSpringState.Velocity = RecoilRotSpringState.Velocity.GetClampedToMaxSize(500.0f);
		RecoilTransSpringState.Velocity = RecoilTransSpringState.Velocity.GetClampedToMaxSize(150.0f);
	}

	// 4. 二阶弹簧向零点物理积分
	const FVector CurrentRotVec(CurrentRecoilRot.Roll, CurrentRecoilRot.Pitch, CurrentRecoilRot.Yaw);
	const FVector NewRotVec = UKismetMathLibrary::VectorSpringInterp(
		CurrentRotVec, FVector::ZeroVector, RecoilRotSpringState, Stiffness, Zeta, SafeDeltaTime, 1.0f
	);
	const FVector NewTransVec = UKismetMathLibrary::VectorSpringInterp(
		CurrentRecoilTrans, FVector::ZeroVector, RecoilTransSpringState, Stiffness, Zeta, SafeDeltaTime, 1.0f
	);

	OutRecoilRot.Roll  = NewRotVec.X;
	OutRecoilRot.Pitch = NewRotVec.Y;
	OutRecoilRot.Yaw   = NewRotVec.Z;

	OutRecoilTrans = NewTransVec;
}

// ----------------------------------------------------------------------------
// 手部物理摇晃 2.0 独立解算器 (人话物理参数版)
// ----------------------------------------------------------------------------
void UFPSAnimFunctionLibrary::CFLF_SolveHandSwayV2(
	float LookMaxYawDeg,
	float LookMaxPitchDeg,
	float StrafeTiltDeg,
	float LandingDropCm,
	float LandingReturnTimeSec,
	float BobAmplitudeCm,
	float StepsPerSecond,
	float FollowTimeSec,
	float SwayBounciness,
	const FRotator& CurrentAimRot,
	FRotator& LastAimRot,
	const FVector& CurrentLocalVel,
	FVector& LastLocalVel,
	float InWalkPhase,
	bool bIsFalling,
	float DeltaTime,
	const FRotator& CurrentSwayRot,
	const FVector& CurrentSwayTrans,
	FVectorSpringState& SwayRotSpringState,
	FVectorSpringState& SwayTransSpringState,
	FVectorSpringState& LandingSpringState,
	FVector& CurrentLandOffset,
	float& OutWalkPhase,
	FRotator& OutSwayRot,
	FVector& OutSwayTrans,
	bool& bOutFootstepTrigger)
{
	// 1. Clamp 到 0.033f (30fps 下限步长)，防止掉帧突发大 DeltaTime 引起弹簧数值爆炸飞手
	const float SafeDeltaTime = FMath::Clamp(DeltaTime, 0.0001f, 0.033f);
	const float SafeFollowTime = FMath::Max(0.08f, FollowTimeSec);
	const float ClampedBounciness = FMath::Clamp(SwayBounciness, 0.0f, 1.0f);

	// 2. 使用 CurrentLandOffset 先还原纯常态值（消除正反馈死循环）
	const float PureCurrentRotRoll = CurrentSwayRot.Roll - CurrentLandOffset.X;
	const FVector PureCurrentTrans = CurrentSwayTrans - FVector(0.0f, CurrentLandOffset.Y, CurrentLandOffset.Z);

	// 3. 常态摇晃跟手时间与弹性 -> 刚度与阻尼
	const float Zeta = FMath::Lerp(1.0f, 0.45f, ClampedBounciness);
	const float OmegaN = 4.0f / (Zeta * SafeFollowTime);
	const float Stiffness = OmegaN * OmegaN;

	// 4. 视角转动滞后 (按 200 deg/s 归一化)
	const FRotator DeltaAimRot = (CurrentAimRot - LastAimRot).GetNormalized();
	LastAimRot = CurrentAimRot;

	const float PitchNorm = FMath::Clamp(DeltaAimRot.Pitch / (SafeDeltaTime * 200.0f), -1.0f, 1.0f);
	const float YawNorm   = FMath::Clamp(DeltaAimRot.Yaw   / (SafeDeltaTime * 200.0f), -1.0f, 1.0f);

	const float LookTargetRoll   = -PitchNorm * LookMaxPitchDeg;
	const float LookTargetPitch  = -YawNorm   * (LookMaxYawDeg * 0.4f);
	const float LookTargetYaw    = -YawNorm   * LookMaxYawDeg;
	const float LookTargetTransX = -YawNorm   * 1.5f;
	const float LookTargetTransZ =  PitchNorm * 1.0f;

	// 5. 移动加减速惯性 (按 2000 cm/s² 归一化)
	const FVector2D Accel2D = FVector2D(
		(CurrentLocalVel.X - LastLocalVel.X) / SafeDeltaTime,
		(CurrentLocalVel.Y - LastLocalVel.Y) / SafeDeltaTime
	).GetClampedToMaxSize(4000.0f);

	const float NormAccelForward = Accel2D.X / 2000.0f;
	const float NormAccelRight   = Accel2D.Y / 2000.0f;

	const float MoveTargetPitch  = -NormAccelRight   * StrafeTiltDeg;
	const float MoveTargetYaw    = -NormAccelRight   * (StrafeTiltDeg * 0.35f);
	const float MoveTargetRoll   = -NormAccelForward * 1.0f;
	const float MoveTargetTransX = -NormAccelRight   * 1.5f;
	const float MoveTargetTransY = -NormAccelForward * 1.5f;

	// 6. 跳跃空中抛物线 (走 FollowTimeSec) 与独立落地重锤初速度注入
	float AirRoll = 0.0f;
	float AirTransZ = 0.0f;

	const float DeltaVz = CurrentLocalVel.Z - LastLocalVel.Z;
	const bool bJustLanded = (!bIsFalling) && (LastLocalVel.Z < -120.0f) && (DeltaVz > 120.0f);

	const float Zeta_Land = 1.0f; // 临界阻尼 (零过冲去弹性，消除弹簧手)
	const float SafeLandReturnTime = FMath::Max(0.05f, LandingReturnTimeSec);
	const float OmegaN_Land = 4.0f / (Zeta_Land * SafeLandReturnTime);
	const float Stiffness_Land = OmegaN_Land * OmegaN_Land;

	if (bIsFalling)
	{
		const float AirRatio = FMath::Clamp(CurrentLocalVel.Z / 600.0f, -1.5f, 1.2f);
		AirRoll   = AirRatio * (-0.15f * LookMaxPitchDeg);
		AirTransZ = AirRatio * (-0.2f  * LandingDropCm);
	}
	else if (bJustLanded)
	{
		const float ImpactRatio = FMath::Clamp(-LastLocalVel.Z / 600.0f, 0.25f, 1.0f);
		const float V_Drop = -LandingDropCm * ImpactRatio * OmegaN_Land * (1.0f + 1.718f * Zeta_Land);

		// 注入独立落地短弹簧：
		LandingSpringState.Velocity.X += -V_Drop * 1.2f; // 低头下顿
		LandingSpringState.Velocity.Y += V_Drop * 0.3f;  // 向后微缩
		LandingSpringState.Velocity.Z += V_Drop;         // 向下沉降
	}
	LastLocalVel = CurrentLocalVel;

	// 7. 使用安全的 VectorSpringInterp 独立更新落地短弹簧，写回 CurrentLandOffset 并做 [-8, 8] 硬限幅
	const FVector NewLandPos = UKismetMathLibrary::VectorSpringInterp(
		CurrentLandOffset, FVector::ZeroVector, LandingSpringState, Stiffness_Land, Zeta_Land, SafeDeltaTime, 1.0f
	);

	CurrentLandOffset = FVector(
		FMath::Clamp(NewLandPos.X, -8.0f, 8.0f),
		FMath::Clamp(NewLandPos.Y, -8.0f, 8.0f),
		FMath::Clamp(NewLandPos.Z, -8.0f, 8.0f)
	);

	// 8. 步态 8 字形呼吸起伏 (李萨如曲线)
	const float Speed2D = FVector2D(CurrentLocalVel.X, CurrentLocalVel.Y).Size();
	const float AlphaMove = ((!bIsFalling) && Speed2D > 15.0f)
		? FMath::Clamp(Speed2D / 600.0f, 0.0f, 1.2f)
		: 0.0f;

	const float StepRadPerSec = FMath::Max(0.5f, StepsPerSecond) * 2.0f * PI;
	const float PhaseNew = FMath::Fmod(InWalkPhase + SafeDeltaTime * StepRadPerSec * AlphaMove, 6.2831853f);
	OutWalkPhase = PhaseNew;

	bOutFootstepTrigger = (FMath::Sin(2.0f * InWalkPhase) < 0.0f) && (FMath::Sin(2.0f * PhaseNew) >= 0.0f);

	const float WaveH = FMath::Cos(PhaseNew) * AlphaMove;
	const float WaveV = FMath::Sin(2.0f * PhaseNew) * AlphaMove;

	const float BobRoll   = WaveV * (BobAmplitudeCm * 2.0f);
	const float BobPitch  = WaveH * (-BobAmplitudeCm * 2.5f);
	const float BobYaw    = WaveH * (BobAmplitudeCm * 2.2f);
	const float BobTransX = WaveH * (BobAmplitudeCm * 0.8f);
	const float BobTransZ = WaveV * BobAmplitudeCm;

	// 9. 三路目标求和与极限几何钳制 (常态摇晃)
	const FVector RawTargetRot(
		LookTargetRoll  + MoveTargetRoll  + AirRoll + BobRoll,
		LookTargetPitch + MoveTargetPitch + BobPitch,
		LookTargetYaw   + MoveTargetYaw   + BobYaw
	);
	const FVector ClampedTargetRot(
		FMath::Clamp(RawTargetRot.X, -12.0f, 12.0f),
		FMath::Clamp(RawTargetRot.Y, -12.0f, 12.0f),
		FMath::Clamp(RawTargetRot.Z, -18.0f, 18.0f)
	);

	const FVector RawTargetTrans(
		LookTargetTransX + MoveTargetTransX + BobTransX,
		MoveTargetTransY,
		LookTargetTransZ + AirTransZ + BobTransZ
	);
	const FVector ClampedTargetTrans(
		FMath::Clamp(RawTargetTrans.X, -4.0f, 4.0f),
		FMath::Clamp(RawTargetTrans.Y, -4.0f, 4.0f),
		FMath::Clamp(RawTargetTrans.Z, -7.0f, 6.0f)
	);

	// 10. 二阶弹簧数值积分 (输入纯常态姿态)
	const FVector CurrentRotVec(PureCurrentRotRoll, CurrentSwayRot.Pitch, CurrentSwayRot.Yaw);
	const FVector NewRotVec = UKismetMathLibrary::VectorSpringInterp(
		CurrentRotVec, ClampedTargetRot, SwayRotSpringState, Stiffness, Zeta, SafeDeltaTime, 1.0f
	);
	const FVector NewTransVec = UKismetMathLibrary::VectorSpringInterp(
		PureCurrentTrans, ClampedTargetTrans, SwayTransSpringState, Stiffness, Zeta, SafeDeltaTime, 1.0f
	);

	// 11. 常态摇晃结果与硬限幅独立落地短弹簧结果直接相加输出 (无需额外骨骼节点)
	OutSwayRot.Roll  = NewRotVec.X + CurrentLandOffset.X;
	OutSwayRot.Pitch = NewRotVec.Y;
	OutSwayRot.Yaw   = NewRotVec.Z;

	OutSwayTrans = NewTransVec + FVector(0.0f, CurrentLandOffset.Y, CurrentLandOffset.Z);
}

// ============================================================================
// 1.0 废弃函数保留实现（供老资产过渡兼容，保证工程零断链）
// ============================================================================

// ----------------------------------------------------------------------------
// 视角转动滞后目标 (已对齐 BCS_ComponentSpace: Roll=上下俯仰, Pitch=侧翻, Yaw=左右横摆)
// ----------------------------------------------------------------------------
void UFPSAnimFunctionLibrary::CFLF_calLookDragTarget(
	const FRotator& CurrentAimRot,
	const FRotator& LastAimRot,
	float DeltaTime,
	float LookSwayStrength,
	float MaxAngularSpeed,
	FRotator& OutLookRot,
	FVector& OutLookTrans)
{
	const float SafeDeltaTime = FMath::Max(DeltaTime, 0.0001f);
	const FRotator DeltaRot = (CurrentAimRot - LastAimRot).GetNormalized();

	const float PitchSpeed = FMath::Clamp(DeltaRot.Pitch / SafeDeltaTime, -MaxAngularSpeed, MaxAngularSpeed);
	const float YawSpeed   = FMath::Clamp(DeltaRot.Yaw   / SafeDeltaTime, -MaxAngularSpeed, MaxAngularSpeed);

	OutLookRot.Roll  = FMath::Clamp(PitchSpeed * (-0.022f * LookSwayStrength), -8.0f,  8.0f);
	OutLookRot.Pitch = FMath::Clamp(YawSpeed   * (-0.015f * LookSwayStrength), -6.0f,  6.0f);
	OutLookRot.Yaw   = FMath::Clamp(YawSpeed   * (-0.028f * LookSwayStrength), -12.0f, 12.0f);

	OutLookTrans.X = FMath::Clamp(YawSpeed   * (-0.008f * LookSwayStrength), -2.5f, 2.5f);
	OutLookTrans.Y = 0.0f;
	OutLookTrans.Z = FMath::Clamp(PitchSpeed * ( 0.006f * LookSwayStrength), -2.0f, 2.0f);
}

// ----------------------------------------------------------------------------
// 移动加减速冲量与跳跃/坠地感知 (已对齐 BCS_ComponentSpace + 内置坠地动量信号)
// ----------------------------------------------------------------------------
void UFPSAnimFunctionLibrary::CFLF_calMoveImpulseTarget(
	const FVector& CurrentLocalVel,
	const FVector& LastLocalVel,
	float DeltaTime,
	float SideSwayStrength,
	bool bIsFalling,
	FRotator& OutImpulseRot,
	FVector& OutImpulseTrans)
{
	const float SafeDeltaTime = FMath::Max(DeltaTime, 0.0001f);

	const FVector2D RawAccel2D(
		(CurrentLocalVel.X - LastLocalVel.X) / SafeDeltaTime,
		(CurrentLocalVel.Y - LastLocalVel.Y) / SafeDeltaTime
	);
	const FVector2D ClampedAccel2D = RawAccel2D.GetClampedToMaxSize(4000.0f);

	const float NormAccelForward = ClampedAccel2D.X / 2000.0f;
	const float NormAccelRight   = ClampedAccel2D.Y / 2000.0f;

	const float StrafeYaw    = NormAccelRight * (-0.2f * SideSwayStrength);
	const float StrafePitch  = NormAccelRight * (-1.0f * SideSwayStrength);
	const float StrafeTransX = NormAccelRight * (-0.8f * SideSwayStrength);

	const float ForwardRoll   = NormAccelForward * (-0.6f * SideSwayStrength);
	const float ForwardTransY = NormAccelForward * (-0.6f * SideSwayStrength);

	float VerticalRoll   = 0.0f;
	float VerticalPitch  = 0.0f;
	float VerticalTransZ = 0.0f;

	const float DeltaVz = CurrentLocalVel.Z - LastLocalVel.Z;
	const bool bJustLanded = (!bIsFalling) && (LastLocalVel.Z < -120.0f) && (DeltaVz > 120.0f);

	if (bIsFalling)
	{
		const float AirRatio = FMath::Clamp(CurrentLocalVel.Z / 600.0f, -1.5f, 1.2f);
		VerticalRoll   = AirRatio * (-0.1f);
		VerticalPitch  = AirRatio * (-0.1f);
		VerticalTransZ = AirRatio * (-0.1f);
	}
	else if (bJustLanded)
	{
		const float ImpactRatio = FMath::Clamp(-LastLocalVel.Z / 600.0f, 0.25f, 1.0f);
		VerticalTransZ = HandSwayInternal::LANDING_SIGNAL_OFFSET - (ImpactRatio * 100.0f);
	}

	OutImpulseRot.Roll  = ForwardRoll + VerticalRoll;
	OutImpulseRot.Pitch = StrafePitch + VerticalPitch;
	OutImpulseRot.Yaw   = StrafeYaw;

	OutImpulseTrans.X = StrafeTransX;
	OutImpulseTrans.Y = ForwardTransY;
	OutImpulseTrans.Z = VerticalTransZ;
}

// ----------------------------------------------------------------------------
// 步态 8 字形呼吸起伏 (已对齐 BCS_ComponentSpace: Roll=上下颠簸, Pitch/Yaw=左右摆动)
// ----------------------------------------------------------------------------
void UFPSAnimFunctionLibrary::CFLF_calWalkBobbingTarget(
	float InWalkPhase,
	const FVector& LocalVelocity,
	float DeltaTime,
	bool bIsMovingOnGround,
	float MaxWalkSpeed,
	float StepFrequency,
	float BobStrength,
	float& OutWalkPhase,
	FRotator& OutBobRot,
	FVector& OutBobTrans,
	bool& bOutFootstepTrigger)
{
	const float SafeMaxSpeed = FMath::Max(MaxWalkSpeed, 1.0f);
	const float Speed2D = FVector2D(LocalVelocity.X, LocalVelocity.Y).Size();
	const float AlphaMove = (bIsMovingOnGround && Speed2D > 15.0f)
		? FMath::Clamp(Speed2D / SafeMaxSpeed, 0.0f, 1.2f)
		: 0.0f;

	const float PhaseNew = FMath::Fmod(InWalkPhase + DeltaTime * StepFrequency * AlphaMove, 6.2831853f);
	OutWalkPhase = PhaseNew;

	const float WaveH = FMath::Cos(PhaseNew) * AlphaMove * BobStrength;
	const float WaveV = FMath::Sin(2.0f * PhaseNew) * AlphaMove * BobStrength;

	OutBobRot.Roll  = WaveV * 1.3f;
	OutBobRot.Pitch = WaveH * -1.8f;
	OutBobRot.Yaw   = WaveH * 1.6f;

	OutBobTrans.X = WaveH * 0.85f;
	OutBobTrans.Y = 0.0f;
	OutBobTrans.Z = WaveV * 0.95f;

	bOutFootstepTrigger = (FMath::Sin(2.0f * InWalkPhase) < 0.0f) && (FMath::Sin(2.0f * PhaseNew) >= 0.0f);
}

// ----------------------------------------------------------------------------
// 二阶欠阻尼弹簧解算器 (内置触地瞬间 Velocity Kick 动量直注逻辑)
// ----------------------------------------------------------------------------
void UFPSAnimFunctionLibrary::CFLF_solveDualSpringSway(
	const FRotator& LookRot,
	const FVector& LookTrans,
	const FRotator& ImpulseRot,
	const FVector& ImpulseTrans,
	const FRotator& BobRot,
	const FVector& BobTrans,
	float ADSAlpha,
	const FRotator& CurrentRot,
	const FVector& CurrentTrans,
	FVectorSpringState& RotSpringState,
	FVectorSpringState& TransSpringState,
	float Stiffness,
	float DampingRatio,
	float DeltaTime,
	FRotator& OutTotalRot,
	FVector& OutTotalTrans)
{
	const float SafeDeltaTime = FMath::Max(DeltaTime, 0.0001f);
	const float SwayScale = 1.0f - FMath::Clamp(ADSAlpha, 0.0f, 1.0f) * 0.85f;

	FVector CleanImpulseTrans = ImpulseTrans;
	if (CleanImpulseTrans.Z < HandSwayInternal::LANDING_SIGNAL_THRESHOLD)
	{
		const float ImpactRatio = FMath::Clamp(
			-(CleanImpulseTrans.Z - HandSwayInternal::LANDING_SIGNAL_OFFSET) / 100.0f,
			0.0f, 2.5f
		);
		CleanImpulseTrans.Z = 0.0f;

		RotSpringState.Velocity.X   += ImpactRatio * (-185.0f) * SwayScale;
		RotSpringState.Velocity.Y   += ImpactRatio * (-75.0f)  * SwayScale;
		TransSpringState.Velocity.Z += ImpactRatio * (-140.0f) * SwayScale;
		TransSpringState.Velocity.Y += ImpactRatio * (-45.0f)  * SwayScale;
	}

	const FVector RawTargetRotVec(
		(LookRot.Roll  + ImpulseRot.Roll  + BobRot.Roll)  * SwayScale,
		(LookRot.Pitch + ImpulseRot.Pitch + BobRot.Pitch) * SwayScale,
		(LookRot.Yaw   + ImpulseRot.Yaw   + BobRot.Yaw)   * SwayScale
	);
	const FVector TargetRotVec(
		FMath::Clamp(RawTargetRotVec.X, -10.0f, 10.0f),
		FMath::Clamp(RawTargetRotVec.Y, -10.0f, 10.0f),
		FMath::Clamp(RawTargetRotVec.Z, -16.0f, 16.0f)
	);

	const FVector RawTargetTransVec = (LookTrans + CleanImpulseTrans + BobTrans) * SwayScale;
	const FVector TargetTransVec(
		FMath::Clamp(RawTargetTransVec.X, -3.5f, 3.5f),
		FMath::Clamp(RawTargetTransVec.Y, -3.5f, 3.5f),
		FMath::Clamp(RawTargetTransVec.Z, -6.0f, 5.0f)
	);

	const FVector CurrentRotVec(CurrentRot.Roll, CurrentRot.Pitch, CurrentRot.Yaw);

	const FVector NewRotVec = UKismetMathLibrary::VectorSpringInterp(
		CurrentRotVec, TargetRotVec, RotSpringState, Stiffness, DampingRatio, SafeDeltaTime, 1.0f
	);
	const FVector NewTransVec = UKismetMathLibrary::VectorSpringInterp(
		CurrentTrans, TargetTransVec, TransSpringState, Stiffness, DampingRatio, SafeDeltaTime, 1.0f
	);

	OutTotalRot.Roll  = NewRotVec.X;
	OutTotalRot.Pitch = NewRotVec.Y;
	OutTotalRot.Yaw   = NewRotVec.Z;

	OutTotalTrans = NewTransVec;
}
