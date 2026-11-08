// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "GameplayEffectTypes.h"
#include "FPSAnimFunctionLibrary.generated.h"

class UGameplayEffect;

/**
 * FPS 第一人称动画、物理计算与战斗辅助函数库
 */
UCLASS()
class GAS_API UFPSAnimFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 计算视角转动拖拽手部滞后目标（CFLF_calLookDragTarget）
	 */
	UFUNCTION(BlueprintPure, Category = "FPS|HandSway", meta = (DisplayName = "CFLF_calLookDragTarget", Keywords = "HandSway LookDrag Target CFLF", LookSwayStrength = "1.0", MaxAngularSpeed = "720.0"))
	static void CFLF_calLookDragTarget(
		const FRotator& CurrentAimRot,
		const FRotator& LastAimRot,
		float DeltaTime,
		float LookSwayStrength,
		float MaxAngularSpeed,
		FRotator& OutLookRot,
		FVector& OutLookTrans
	);

	/**
	 * 计算角色移动与起伏冲量目标（CFLF_calMoveImpulseTarget）
	 */
	UFUNCTION(BlueprintPure, Category = "FPS|HandSway", meta = (DisplayName = "CFLF_calMoveImpulseTarget", Keywords = "HandSway MoveImpulse Target CFLF", SideSwayStrength = "1.0"))
	static void CFLF_calMoveImpulseTarget(
		const FVector& CurrentLocalVel,
		const FVector& LastLocalVel,
		float DeltaTime,
		float SideSwayStrength,
		bool bIsFalling,
		FRotator& OutImpulseRot,
		FVector& OutImpulseTrans
	);

	/**
	 * 计算跑动步态呼吸与8字形起伏目标（CFLF_calWalkBobbingTarget）
	 */
	UFUNCTION(BlueprintPure, Category = "FPS|HandSway", meta = (DisplayName = "CFLF_calWalkBobbingTarget", Keywords = "HandSway WalkBobbing Target CFLF", MaxWalkSpeed = "600.0", StepFrequency = "9.5", BobStrength = "1.0"))
	static void CFLF_calWalkBobbingTarget(
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
		bool& bOutFootstepTrigger
	);

	/**
	 * 二阶欠阻尼弹簧解算器（CFLF_solveDualSpringSway）
	 */
	UFUNCTION(BlueprintCallable, Category = "FPS|HandSway", meta = (DisplayName = "CFLF_solveDualSpringSway", Keywords = "HandSway Solve Spring Dual CFLF", ADSAlpha = "0.0", Stiffness = "165.0", DampingRatio = "0.6"))
	static void CFLF_solveDualSpringSway(
		const FRotator& LookRot,
		const FVector& LookTrans,
		const FRotator& ImpulseRot,
		const FVector& ImpulseTrans,
		const FRotator& BobRot,
		const FVector& BobTrans,
		float ADSAlpha,
		const FRotator& CurrentRot,
		const FVector& CurrentTrans,
		UPARAM(ref) FVectorSpringState& RotSpringState,
		UPARAM(ref) FVectorSpringState& TransSpringState,
		float Stiffness,
		float DampingRatio,
		float DeltaTime,
		FRotator& OutTotalRot,
		FVector& OutTotalTrans
	);

	/**
	 * 向四周抛洒带随机物理旋转和初速度的实体掉落物（Halo 式爆装备 / 防堆叠环形分布）
	 * CenterLocation 改为值传递并提供智能兜底，杜绝世界原点 (0,0,0) 丢失！
	 */
	UFUNCTION(BlueprintCallable, Category = "FPS|Combat", meta = (WorldContext = "WorldContextObject", DisplayName = "CFLF_SpawnScatteredPickups", Keywords = "Spawn Drop Pickup Scatter Halo Fodder CFLF", Count = "2", ScatterRadius = "35.0", UpwardImpulse = "350.0", OutwardImpulse = "200.0", RandomAngularImpulse = "180.0"))
	static void CFLF_SpawnScatteredPickups(
		const UObject* WorldContextObject,
		TSubclassOf<AActor> PickupClass,
		FVector CenterLocation,
		int32 Count = 2,
		float ScatterRadius = 35.0f,
		float UpwardImpulse = 350.0f,
		float OutwardImpulse = 200.0f,
		float RandomAngularImpulse = 180.0f
	);

	/**
	 * 对目标施加统一伤害（自动兼容 GAS ASC 目标与传统 UE 目标）
	 */
	UFUNCTION(BlueprintCallable, Category = "FPS|Combat", meta = (DisplayName = "CFLF_ApplyDamageToTarget", Keywords = "Damage Apply GAS Hurt Hit CFLF", DamageAmount = "25.0"))
	static bool CFLF_ApplyDamageToTarget(
		AActor* TargetActor,
		float DamageAmount,
		AActor* InstigatorActor,
		TSubclassOf<UGameplayEffect> DamageGEClass = nullptr
	);
};
