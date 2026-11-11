// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSAnimFunctionLibrary.h"
#include "DoomAttributeSet.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameplayTagsModule.h"
#include "Kismet/GameplayStatics.h"

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
// 1. 视角转动滞后目标 (已对齐 BCS_ComponentSpace: Roll=上下俯仰, Pitch=侧翻, Yaw=左右横摆)
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

	// Component Space 旋转映射：
	// - 鼠标上下看 (PitchSpeed) -> 驱动绕 X 轴旋转 (Roll: 枪口上下滞后俯仰)
	// - 鼠标左右看 (YawSpeed)   -> 驱动绕 Z 轴旋转 (Yaw: 枪口左右滞后横摆，露枪身侧面) 
	//                              及绕 Y 轴旋转 (Pitch: 伴随枪身侧倾翻滚)
	OutLookRot.Roll  = FMath::Clamp(PitchSpeed * (-0.022f * LookSwayStrength), -8.0f,  8.0f);
	OutLookRot.Pitch = FMath::Clamp(YawSpeed   * (-0.015f * LookSwayStrength), -6.0f,  6.0f);
	OutLookRot.Yaw   = FMath::Clamp(YawSpeed   * (-0.028f * LookSwayStrength), -12.0f, 12.0f);

	// Component Space 位移映射 (X=左右, Y=前后, Z=上下)
	OutLookTrans.X = FMath::Clamp(YawSpeed   * (-0.008f * LookSwayStrength), -2.5f, 2.5f);
	OutLookTrans.Y = 0.0f;
	OutLookTrans.Z = FMath::Clamp(PitchSpeed * ( 0.006f * LookSwayStrength), -2.0f, 2.0f);
}

// ----------------------------------------------------------------------------
// 2. 移动加减速冲量与跳跃/坠地感知 (已对齐 BCS_ComponentSpace + 内置坠地动量信号)
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

	// 注意：仅对水平加速度 (X, Y) 做限幅，避免落地瞬间巨大的 Z 轴速度差把水平加速度模长稀释归零！
	const FVector2D RawAccel2D(
		(CurrentLocalVel.X - LastLocalVel.X) / SafeDeltaTime,
		(CurrentLocalVel.Y - LastLocalVel.Y) / SafeDeltaTime
	);
	const FVector2D ClampedAccel2D = RawAccel2D.GetClampedToMaxSize(4000.0f);

	// Actor Space 输入：X 为前后 (W/S)，Y 为左右 (A/D)
	const float NormAccelForward = ClampedAccel2D.X / 2000.0f; // W/S 起停
	const float NormAccelRight   = ClampedAccel2D.Y / 2000.0f; // A/D 横移折返

	// --- A/D 左右横移 (NormAccelRight)：高占比展现枪身两侧 (Yaw 横摆 + Pitch 侧翻)，绝不上下摆 ---
	const float StrafeYaw    = NormAccelRight * (-0.2f * SideSwayStrength); // Z轴：水平转出枪身侧面
	const float StrafePitch  = NormAccelRight * (-1.0f * SideSwayStrength); // Y轴：枪身向移动反方向侧翻
	const float StrafeTransX = NormAccelRight * (-0.8f * SideSwayStrength); // X轴：左右平移缓冲

	// --- W/S 前后起停 (NormAccelForward)：仅产生前后推拉 (Trans.Y) 与轻微枪口上下俯仰 (Roll) ---
	const float ForwardRoll   = NormAccelForward * (-0.6f * SideSwayStrength); // X轴：起跑微低头、急停微抬头
	const float ForwardTransY = NormAccelForward * (-0.6f * SideSwayStrength); // Y轴：起跑向后缩、急停向前冲

	// --- 空中抛物线 (起跳下压 -> 下落失重上浮) 与 触地瞬间重锤检测 ---
	float VerticalRoll   = 0.0f;
	float VerticalPitch  = 0.0f;
	float VerticalTransZ = 0.0f;

	const float DeltaVz = CurrentLocalVel.Z - LastLocalVel.Z;
	const bool bJustLanded = (!bIsFalling) && (LastLocalVel.Z < -120.0f) && (DeltaVz > 120.0f);

	if (bIsFalling)
	{
		// 在空中：
		// 1. 刚起跳时 (CurrentLocalVel.Z > 0) -> 枪往下沉 (-Z)、枪口微低头 (-Roll)
		// 2. 空中下坠时 (CurrentLocalVel.Z < 0) -> 失重感让枪往上飘起 (+Z)、枪口微抬蓄力 (+Roll)
		const float AirRatio = FMath::Clamp(CurrentLocalVel.Z / 600.0f, -1.5f, 1.2f);
		VerticalRoll   = AirRatio * (-0.1f); // 起跳低头，下落抬头蓄力
		VerticalPitch  = AirRatio * (-0.1f); // 空中伴随轻微侧倾感
		VerticalTransZ = AirRatio * (-0.1f); // 起跳下沉，下落上浮最高约 +3.6cm
	}
	else if (bJustLanded)
	{
		// 触地瞬间：计算落地冲击强度 Ratio (范围 0.2 ~ 2.0)，通过负向特征码传递给弹簧解算器直接注入动量！
		const float ImpactRatio = FMath::Clamp(-LastLocalVel.Z / 600.0f, 0.25f, 1.0f);
		VerticalTransZ = HandSwayInternal::LANDING_SIGNAL_OFFSET - (ImpactRatio * 100.0f);
	}

	// 组装输出 (严格按字段名赋值，杜绝 FRotator(Pitch,Yaw,Roll) 构造函数顺序混淆)
	OutImpulseRot.Roll  = ForwardRoll + VerticalRoll;
	OutImpulseRot.Pitch = StrafePitch + VerticalPitch;
	OutImpulseRot.Yaw   = StrafeYaw;

	OutImpulseTrans.X = StrafeTransX;
	OutImpulseTrans.Y = ForwardTransY;
	OutImpulseTrans.Z = VerticalTransZ;
}

// ----------------------------------------------------------------------------
// 3. 步态 8 字形呼吸起伏 (已对齐 BCS_ComponentSpace: Roll=上下颠簸, Pitch/Yaw=左右摆动)
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

	// 李萨如 8 字波：横向 1x 频率，纵向 2x 频率
	const float WaveH = FMath::Cos(PhaseNew) * AlphaMove * BobStrength;
	const float WaveV = FMath::Sin(2.0f * PhaseNew) * AlphaMove * BobStrength;

	// 对齐 Component Space：
	// - 纵向步态起伏 (WaveV) -> 驱动绕 X 轴俯仰 (Roll) 与 Z 轴上下位移 (Trans.Z)
	// - 横向步态迈步 (WaveH) -> 驱动绕 Y 轴侧翻 (Pitch)、绕 Z 轴横摆 (Yaw) 与 X 轴左右位移 (Trans.X)
	OutBobRot.Roll  = WaveV * 1.3f;
	OutBobRot.Pitch = WaveH * -1.8f;
	OutBobRot.Yaw   = WaveH * 1.6f;

	OutBobTrans.X = WaveH * 0.85f;
	OutBobTrans.Y = 0.0f;
	OutBobTrans.Z = WaveV * 0.95f;

	bOutFootstepTrigger = (FMath::Sin(2.0f * InWalkPhase) < 0.0f) && (FMath::Sin(2.0f * PhaseNew) >= 0.0f);
}

// ----------------------------------------------------------------------------
// 4. 二阶欠阻尼弹簧解算器 (内置触地瞬间 Velocity Kick 动量直注逻辑)
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

	// 检查是否收到来自 CFLF_calMoveImpulseTarget 的触地瞬间动量注入信号
	FVector CleanImpulseTrans = ImpulseTrans;
	if (CleanImpulseTrans.Z < HandSwayInternal::LANDING_SIGNAL_THRESHOLD)
	{
		// 解码落地冲击强度 Ratio (0.25 ~ 2.0)
		const float ImpactRatio = FMath::Clamp(
			-(CleanImpulseTrans.Z - HandSwayInternal::LANDING_SIGNAL_OFFSET) / 100.0f,
			0.0f, 2.5f
		);
		CleanImpulseTrans.Z = 0.0f; // 还原稳态位移目标为 0

		// 直接向二阶弹簧内部速度向量注入落地重锤冲击 (Spring Velocity Kick)！
		// 约定 RotVec 分量顺序：X = Roll (上下俯仰), Y = Pitch (枪身侧翻), Z = Yaw (左右横摆)
		RotSpringState.Velocity.X   += ImpactRatio * (-185.0f) * SwayScale; // 枪口猛烈下栽低头 (~ -8度峰值)
		RotSpringState.Velocity.Y   += ImpactRatio * (-75.0f)  * SwayScale; // 伴随机械侧倾震动 (~ -3度峰值)
		TransSpringState.Velocity.Z += ImpactRatio * (-140.0f) * SwayScale; // 手臂整体向下沉降 (~ -5.8cm峰值)
		TransSpringState.Velocity.Y += ImpactRatio * (-45.0f)  * SwayScale; // 落地时微向后缩缓冲
	}

	// 统一按 (X = Roll, Y = Pitch, Z = Yaw) 顺序打包旋转向量并限幅
	const FVector RawTargetRotVec(
		(LookRot.Roll  + ImpulseRot.Roll  + BobRot.Roll)  * SwayScale,
		(LookRot.Pitch + ImpulseRot.Pitch + BobRot.Pitch) * SwayScale,
		(LookRot.Yaw   + ImpulseRot.Yaw   + BobRot.Yaw)   * SwayScale
	);
	const FVector TargetRotVec(
		FMath::Clamp(RawTargetRotVec.X, -10.0f, 10.0f), // Roll: 上下俯仰限幅
		FMath::Clamp(RawTargetRotVec.Y, -10.0f, 10.0f), // Pitch: 左右侧翻限幅
		FMath::Clamp(RawTargetRotVec.Z, -16.0f, 16.0f)  // Yaw: 左右横摆放宽至 16度，充分展示枪身侧面
	);

	const FVector RawTargetTransVec = (LookTrans + CleanImpulseTrans + BobTrans) * SwayScale;
	const FVector TargetTransVec(
		FMath::Clamp(RawTargetTransVec.X, -3.5f, 3.5f), // X: 左右横移限幅
		FMath::Clamp(RawTargetTransVec.Y, -3.5f, 3.5f), // Y: 前后推拉限幅
		FMath::Clamp(RawTargetTransVec.Z, -6.0f, 5.0f)  // Z: 上下浮沉限幅
	);

	const FVector CurrentRotVec(CurrentRot.Roll, CurrentRot.Pitch, CurrentRot.Yaw);

	// 二阶欠阻尼弹簧积分
	const FVector NewRotVec = UKismetMathLibrary::VectorSpringInterp(
		CurrentRotVec, TargetRotVec, RotSpringState, Stiffness, DampingRatio, SafeDeltaTime, 1.0f
	);
	const FVector NewTransVec = UKismetMathLibrary::VectorSpringInterp(
		CurrentTrans, TargetTransVec, TransSpringState, Stiffness, DampingRatio, SafeDeltaTime, 1.0f
	);

	// 还原为 FRotator (严格按字段名赋值)
	OutTotalRot.Roll  = NewRotVec.X;
	OutTotalRot.Pitch = NewRotVec.Y;
	OutTotalRot.Yaw   = NewRotVec.Z;

	OutTotalTrans = NewTransVec;
}

void UFPSAnimFunctionLibrary::CFLF_SpawnScatteredPickups(
	const UObject* WorldContextObject,
	TSubclassOf<AActor> PickupClass,
	FVector CenterLocation,
	int32 Count,
	float ScatterRadius,
	float UpwardImpulse,
	float OutwardImpulse,
	float RandomAngularImpulse)
{
	if (!WorldContextObject || !PickupClass || Count <= 0)
	{
		return;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!World)
	{
		return;
	}

	// 智能位置兜底：若传入的坐标为 (0,0,0)（即蓝图未连线），自动使用调用者 Actor 自身坐标，杜绝飞到地图原点！
	FVector ActualCenter = CenterLocation;
	if (ActualCenter.IsNearlyZero(1.0f))
	{
		if (const AActor* ContextActor = Cast<AActor>(WorldContextObject))
		{
			ActualCenter = ContextActor->GetActorLocation();
		}
	}

	// 随机初始朝向，避免多波次掉落角度完全一致
	const float BaseAngleDeg = FMath::FRandRange(0.0f, 360.0f);
	const float StepAngleDeg = 360.0f / static_cast<float>(Count);

	for (int32 i = 0; i < Count; ++i)
	{
		// 环形等角度错位分布 + 微随机扰动（防堆叠）
		const float AngleDeg = BaseAngleDeg + StepAngleDeg * static_cast<float>(i) + FMath::FRandRange(-12.0f, 12.0f);
		const float AngleRad = FMath::DegreesToRadians(AngleDeg);

		const FVector HorizontalDir(FMath::Cos(AngleRad), FMath::Sin(AngleRad), 0.0f);
		const float ActualRadius = ScatterRadius + FMath::FRandRange(-5.0f, 15.0f);
		const FVector SpawnLoc = ActualCenter + HorizontalDir * ActualRadius + FVector(0.0f, 0.0f, 25.0f);
		const FRotator SpawnRot(0.0f, AngleDeg, 0.0f);

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* SpawnedPickup = World->SpawnActor<AActor>(PickupClass, SpawnLoc, SpawnRot, SpawnParams);
		if (!SpawnedPickup)
		{
			continue;
		}

		// 寻找物理碰撞组件施加动量喷射
		UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(SpawnedPickup->GetRootComponent());
		if (!PrimComp)
		{
			PrimComp = SpawnedPickup->FindComponentByClass<UPrimitiveComponent>();
		}

		if (PrimComp && PrimComp->IsSimulatingPhysics())
		{
			// 径向向外冲量 + 向上抛射冲量（bVelChange=true 保证不受 mass 差异影响）
			const FVector Impulse = HorizontalDir * OutwardImpulse + FVector(0.0f, 0.0f, UpwardImpulse + FMath::FRandRange(-25.0f, 25.0f));
			PrimComp->AddImpulse(Impulse, NAME_None, true);

			// 随机三轴角冲量翻滚
			const FVector RandomSpin(
				FMath::FRandRange(-1.0f, 1.0f),
				FMath::FRandRange(-1.0f, 1.0f),
				FMath::FRandRange(-1.0f, 1.0f)
			);
			PrimComp->AddAngularImpulseInDegrees(RandomSpin.GetSafeNormal() * RandomAngularImpulse, NAME_None, true);
		}
	}
}

bool UFPSAnimFunctionLibrary::CFLF_ApplyDamageToTarget(
	AActor* TargetActor,
	float DamageAmount,
	AActor* InstigatorActor,
	TSubclassOf<UGameplayEffect> DamageGEClass)
{
	if (!TargetActor || DamageAmount <= 0.0f)
	{
		return false;
	}

	// 1. 尝试通过 GAS ASC 施加伤害
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	if (TargetASC)
	{
		TSubclassOf<UGameplayEffect> EffectiveGEClass = DamageGEClass;
		if (!EffectiveGEClass)
		{
			// 自动加载项目中已配置好的标准 GE_Damage
			EffectiveGEClass = LoadClass<UGameplayEffect>(nullptr, TEXT("/Game/bps/gameplayEffect-Ability/GE_Damage.GE_Damage_C"));
		}

		if (EffectiveGEClass)
		{
			FGameplayEffectContextHandle Context = TargetASC->MakeEffectContext();
			Context.AddInstigator(InstigatorActor, InstigatorActor);

			FGameplayEffectSpecHandle SpecHandle = TargetASC->MakeOutgoingSpec(EffectiveGEClass, 1.0f, Context);
			if (SpecHandle.IsValid())
			{
				FGameplayTag DamageTag = FGameplayTag::RequestGameplayTag(FName("Data.Damage"), false);
				if (DamageTag.IsValid())
				{
					// 注入负数伤害（GE_Damage 配置为扣除生命）
					SpecHandle.Data->SetSetByCallerMagnitude(DamageTag, -FMath::Abs(DamageAmount));
				}

				TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());

				// 屏幕打印调试信息，让命中与扣血一目了然
				if (GEngine)
				{
					float CurrentHp = 0.0f;
					float MaxHp = 0.0f;
					if (const UDoomAttributeSet* DoomAttr = Cast<UDoomAttributeSet>(TargetASC->GetAttributeSet(UDoomAttributeSet::StaticClass())))
					{
						CurrentHp = DoomAttr->GetHealth();
						MaxHp = DoomAttr->GetMaxHealth();
					}

					if (CurrentHp <= 0.0f)
					{
						FString KillMsg = FString::Printf(TEXT("☠️ [KILL] %s 被彻底消灭！生命归零 (0.0 / %.1f)"),
							*TargetActor->GetName(), MaxHp);
						GEngine->AddOnScreenDebugMessage(-1, 3.5f, FColor::Red, KillMsg);
					}
					else
					{
						FString HitMsg = FString::Printf(TEXT("🎯 [HIT] %s 受到 %.1f 伤害 | 剩余生命: %.1f / %.1f"),
							*TargetActor->GetName(), DamageAmount, CurrentHp, MaxHp);
						GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green, HitMsg);
					}
				}
				return true;
			}
		}
	}

	// 2. 传统通用 Fallback：对非 GAS 对象（普通假人、可破坏物等）调用引擎原生伤害
	AController* InstigatorController = InstigatorActor ? InstigatorActor->GetInstigatorController() : nullptr;
	UGameplayStatics::ApplyDamage(TargetActor, DamageAmount, InstigatorController, InstigatorActor, nullptr);

	if (GEngine)
	{
		FString Msg = FString::Printf(TEXT("🎯 [HIT-Generic] %s 受到 %.1f 原生伤害"), *TargetActor->GetName(), DamageAmount);
		GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Yellow, Msg);
	}
	return true;
}
