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

void UFPSAnimFunctionLibrary::CFLF_calLookDragTarget(
	const FRotator& CurrentAimRot,
	const FRotator& LastAimRot,
	float DeltaTime,
	float LookSwayStrength,
	float MaxAngularSpeed,
	FRotator& OutLookRot,
	FVector& OutLookTrans)
{
	// 步骤 1：计算两帧之间的旋转角度差（自动处理 360 度跨越回绕，保证差值在 [-180, 180] 之间）
	const FRotator DeltaRot = (CurrentAimRot - LastAimRot).GetNormalized();

	// 步骤 2：转化为物理角速度（度/秒，deg/sec）
	// 防除零保护：限制 DeltaTime 最小为 0.0001 秒
	const float SafeDeltaTime = FMath::Max(DeltaTime, 0.0001f);
	const float SafeMaxSpeed = FMath::Max(MaxAngularSpeed, 0.0f);

	// 计算每秒俯仰（Pitch）与偏航（Yaw）角速度，并钳制在最大安全速度内，防止剧烈卡顿或切屏引起的突变
	const float PitchSpeed = FMath::Clamp(DeltaRot.Pitch / SafeDeltaTime, -SafeMaxSpeed, SafeMaxSpeed);
	const float YawSpeed   = FMath::Clamp(DeltaRot.Yaw   / SafeDeltaTime, -SafeMaxSpeed, SafeMaxSpeed);

	// 步骤 3：映射到手部旋转滞后量（OutLookRot）
	// - 系数说明：-0.025 与 -0.012 是将物理速度映射为视觉柔和滞后的基础经验系数
	// - 负号代表“惯性反向”，即视线向正方向转，武器向反方向滞后
	// - 乘以武器配置的 LookSwayStrength（重武器系数大，手枪系数小）
	// - 最后进行角度限幅（Clamp），保证手部绝对不穿模穿屏
	OutLookRot.Pitch = FMath::Clamp(PitchSpeed * (-0.025f * LookSwayStrength), -8.0f, 8.0f);
	OutLookRot.Yaw   = FMath::Clamp(YawSpeed   * (-0.025f * LookSwayStrength), -10.0f, 10.0f);
	OutLookRot.Roll  = FMath::Clamp(YawSpeed   * (-0.012f * LookSwayStrength), -5.0f, 5.0f);

	// 步骤 4：映射到手部微平移量（OutLookTrans）
	// - X 轴（前后）：视线旋转不直接产生明显的前后缩放位移，保持 0.0
	// - Y 轴（左右）：向右转头时（YawSpeed > 0），手臂惯性向左滑移（负系数），限幅 [-2.0, 2.0] cm
	// - Z 轴（上下）：向上甩枪时（PitchSpeed > 0），力臂几何抬升（正系数），限幅 [-1.5, 1.5] cm
	OutLookTrans.X = 0.0f;
	OutLookTrans.Y = FMath::Clamp(YawSpeed   * (-0.008f * LookSwayStrength), -2.0f, 2.0f);
	OutLookTrans.Z = FMath::Clamp(PitchSpeed * (0.006f  * LookSwayStrength), -1.5f, 1.5f);
}

void UFPSAnimFunctionLibrary::CFLF_calMoveImpulseTarget(
	const FVector& CurrentLocalVel,
	const FVector& LastLocalVel,
	float DeltaTime,
	float SideSwayStrength,
	bool bIsFalling,
	FRotator& OutImpulseRot,
	FVector& OutImpulseTrans)
{
	// 步骤 1：防除零保护与局部线性加速度计算（单位：cm/s²）
	// 【核心设计】：由于使用的是局部加速度 (Current - Last) / dt；
	// 当玩家按住 W 达到最大速度匀速奔跑时，加速度为 0，手部自然回中，彻底根治“持续按 W 手臂卡死偏角”的问题。
	const float SafeDeltaTime = FMath::Max(DeltaTime, 0.0001f);
	const FVector RawAccel = (CurrentLocalVel - LastLocalVel) / SafeDeltaTime;

	// 步骤 2：对局部加速度矢量模长限幅（4000 cm/s²），防止传送、瞬移或掉帧导致数值尖峰
	const FVector ClampedAccel = RawAccel.GetClampedToMaxSize(4000.0f);

	// 步骤 3：水平加减速惯性映射（以 2000 cm/s² 标准满速起步加速度为基准归一化）
	const float NormalizedAccelX = ClampedAccel.X / 2000.0f;
	const float NormalizedAccelY = ClampedAccel.Y / 2000.0f;

	// - X 轴（前进/后退）：起跑（+X 加速）手部后缩沉降；急停（-X 减速）手部受惯性前冲
	const float AccelPitch  = NormalizedAccelX * (-3.5f * SideSwayStrength);
	const float AccelTransX = NormalizedAccelX * (-1.8f * SideSwayStrength);

	// - Y 轴（左右折返）：变向（A/D 键加速度）带来明显的横滚侧倾（Roll）与偏航转向（Yaw）
	const float AccelRoll   = NormalizedAccelY * (-4.5f * SideSwayStrength);
	const float AccelYaw    = NormalizedAccelY * (2.0f  * SideSwayStrength);
	const float AccelTransY = NormalizedAccelY * (-1.5f * SideSwayStrength);

	// 步骤 4：空中滞后与着陆冲击
	// - 空中滞后（AirPitch）：起跳上升阶段（Vz > 0）枪口微低；下落阶段（Vz < 0）风阻使枪口微抬，限幅 [-6°, 6°]
	const float AirPitch = bIsFalling
		? FMath::Clamp((CurrentLocalVel.Z / 600.0f) * -4.0f, -6.0f, 6.0f)
		: 0.0f;

	// - 落地沉降冲击（LandTransZ）：着陆瞬间垂直产生巨大向上加速度，手部受惯性向下沉降顿挫，限幅 [-3.5, 2.0] cm
	const float LandTransZ = FMath::Clamp((ClampedAccel.Z / 3000.0f) * -2.5f, -3.5f, 2.0f);

	// 步骤 5：合成输出
	OutImpulseRot.Pitch = AccelPitch + AirPitch;
	OutImpulseRot.Yaw   = AccelYaw;
	OutImpulseRot.Roll  = AccelRoll;

	OutImpulseTrans.X   = AccelTransX;
	OutImpulseTrans.Y   = AccelTransY;
	OutImpulseTrans.Z   = LandTransZ;
}

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
	// 步骤 1：计算有效水平地面移动速度比率
	// 移动速度低于 15 cm/s 视为静止，防止极微小输入导致的步态蠕动抖动
	const float Speed2D = FVector2D(LocalVelocity.X, LocalVelocity.Y).Size();
	const float SafeMaxSpeed = FMath::Max(MaxWalkSpeed, 1.0f);
	const float AlphaMove = (bIsMovingOnGround && Speed2D > 15.0f)
		? FMath::Clamp(Speed2D / SafeMaxSpeed, 0.0f, 1.2f)
		: 0.0f;

	// 步骤 2：步态相位角累加（模 2π 回绕，保证相位在 [0, 2π) 之间无限平滑推进）
	constexpr float TwoPi = 6.28318530718f;
	const float PhaseNew = FMath::Fmod(InWalkPhase + DeltaTime * StepFrequency * AlphaMove, TwoPi);
	OutWalkPhase = PhaseNew;

	// 步骤 3：李萨如 8 字形波形（水平 1x 步频，垂直 2x 步频）
	// 人类奔跑时，左右摇摆为 1 个周期（Cos(φ)），双脚交替触地导致上下颠簸为 2 个周期（Sin(2φ)）
	const float WaveH = FMath::Cos(PhaseNew) * AlphaMove * BobStrength;
	const float WaveV = FMath::Sin(2.0f * PhaseNew) * AlphaMove * BobStrength;

	// 步骤 4：映射为步态呼吸旋转与位移
	OutBobRot.Pitch = WaveV * 1.2f;
	OutBobRot.Yaw   = WaveH * 1.5f;
	OutBobRot.Roll  = WaveH * -1.8f;

	OutBobTrans.X   = 0.0f;
	OutBobTrans.Y   = WaveH * 0.8f;
	OutBobTrans.Z   = WaveV * 0.9f;

	// 步骤 5：检测脚步触地波谷触发点（当垂直正弦波从负穿越到非负时为脚掌着地瞬间）
	// 预留给未来无缝触发脚步音效或地面微震
	bOutFootstepTrigger = (FMath::Sin(2.0f * InWalkPhase) < 0.0f) && (FMath::Sin(2.0f * PhaseNew) >= 0.0f);
}

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
	// 步骤 1：机瞄（ADS）衰减系数
	// 腰射（ADSAlpha = 0）全额晃动；机瞄（ADSAlpha = 1）平滑衰减至 15%（0.15），保证开镜视野稳定
	const float ClampedADSAlpha = FMath::Clamp(ADSAlpha, 0.0f, 1.0f);
	const float ADSScale = FMath::Lerp(1.0f, 0.15f, ClampedADSAlpha);

	// 步骤 2：合成总目标旋转（小角度下直接叠加三通道：视线滞后 + 移动加减速 + 步态起伏）
	const float TotalPitch = (LookRot.Pitch + ImpulseRot.Pitch + BobRot.Pitch) * ADSScale;
	const float TotalYaw   = (LookRot.Yaw   + ImpulseRot.Yaw   + BobRot.Yaw)   * ADSScale;
	const float TotalRoll  = (LookRot.Roll  + ImpulseRot.Roll  + BobRot.Roll)  * ADSScale;

	// 对目标旋转进行最终安全限幅
	const FVector TargetRotVec(
		FMath::Clamp(TotalPitch, -8.0f, 8.0f),
		FMath::Clamp(TotalYaw,   -10.0f, 10.0f),
		FMath::Clamp(TotalRoll,  -12.0f, 12.0f)
	);

	// 步骤 3：合成总目标平移
	const float TotalTransX = (LookTrans.X + ImpulseTrans.X + BobTrans.X) * ADSScale;
	const float TotalTransY = (LookTrans.Y + ImpulseTrans.Y + BobTrans.Y) * ADSScale;
	const float TotalTransZ = (LookTrans.Z + ImpulseTrans.Z + BobTrans.Z) * ADSScale;

	// 对目标平移进行最终安全限幅
	const FVector TargetTransVec(
		FMath::Clamp(TotalTransX, -3.0f, 3.0f),
		FMath::Clamp(TotalTransY, -3.0f, 3.0f),
		FMath::Clamp(TotalTransZ, -4.0f, 4.0f)
	);

	// 步骤 4：当前手部旋转状态解构成 FVector (X=Pitch, Y=Yaw, Z=Roll) 供弹簧解算
	const FVector CurrentRotVec(CurrentRot.Pitch, CurrentRot.Yaw, CurrentRot.Roll);

	// 步骤 5：调用二阶欠阻尼物理弹簧解算器（Stiffness 刚度, DampingRatio 阻尼比）
	const FVector NewRotVec = UKismetMathLibrary::VectorSpringInterp(
		CurrentRotVec,
		TargetRotVec,
		RotSpringState,
		Stiffness,
		DampingRatio,
		DeltaTime,
		1.0f // Mass
	);

	const FVector NewTransVec = UKismetMathLibrary::VectorSpringInterp(
		CurrentTrans,
		TargetTransVec,
		TransSpringState,
		Stiffness,
		DampingRatio,
		DeltaTime,
		1.0f // Mass
	);

	// 步骤 6：100% 作用于 spine_05（单骨骼刚体化驱动）
	// 根据实测确认与当前架构，直接将全额旋转与位移赋予 spine_05；
	// 不再拆分手腕与躯干变量，保证右手护木 Socket 与左手 IK 绝对死锁不脱手！
	OutTotalRot = FRotator(NewRotVec.X, NewRotVec.Y, NewRotVec.Z);
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
