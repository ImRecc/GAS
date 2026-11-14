// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "GameplayEffectTypes.h"
#include "GameplayAbilitySpecHandle.h"
#include "FPSAnimFunctionLibrary.generated.h"

class UGameplayEffect;
class UGameplayAbility;

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

	/**
	 * 动态向目标 Actor 的 ASC 赋予指定的 GAS 技能（卡带热插拔，若已存在则直接返回句柄）
	 */
	UFUNCTION(BlueprintCallable, Category = "FPS|Combat", meta = (DisplayName = "CFLF_GiveAbilityToActor", Keywords = "Give Ability GAS Card Grant CFLF"))
	static FGameplayAbilitySpecHandle CFLF_GiveAbilityToActor(
		AActor* TargetActor,
		TSubclassOf<UGameplayAbility> AbilityClass,
		int32 AbilityLevel = 1
	);

	/**
	 * 向四周等分环形抛洒一组由 Class 数组配置的实体掉落物（花瓣式爆装备 / 数组化多类掉落）
	 */
	UFUNCTION(BlueprintCallable, Category = "FPS|Combat", meta = (WorldContext = "WorldContextObject", DisplayName = "CFLF_SpawnScatteredPickupList", Keywords = "Spawn Drop Pickup Scatter Halo List Fodder CFLF", ScatterRadius = "35.0", UpwardImpulse = "350.0", OutwardImpulse = "200.0", RandomAngularImpulse = "180.0"))
	static void CFLF_SpawnScatteredPickupList(
		const UObject* WorldContextObject,
		const TArray<TSubclassOf<AActor>>& PickupClasses,
		FVector CenterLocation,
		float ScatterRadius = 35.0f,
		float UpwardImpulse = 350.0f,
		float OutwardImpulse = 200.0f,
		float RandomAngularImpulse = 180.0f
	);

	/**
	 * 单节点前方/周围球形范围伤害检测：自动比对 Team.Player 与 Team.Enemy 过滤敌对目标并结算伤害
	 */
	UFUNCTION(BlueprintCallable, Category = "FPS|Combat", meta = (DisplayName = "CFLF_SphereDamageHostiles", Keywords = "Sphere Damage Hostile Faction Hurt CFLF", Radius = "110.0", Damage = "15.0", bDrawDebug = "false"))
	static int32 CFLF_SphereDamageHostiles(
		AActor* InstigatorActor,
		FVector SphereCenter,
		float Radius = 110.0f,
		float Damage = 15.0f,
		bool bDrawDebug = false
	);

	/**
	 * 投射物与环境范围爆炸结算（带距离线性衰减、敌对阵营过滤、物理气浪与死怪刚体冲量炸飞）
	 */
	UFUNCTION(BlueprintCallable, Category = "FPS|Combat", meta = (DisplayName = "CFLF_ExplodeRadialDamage", Keywords = "Explode Radial Splash Damage Bomb CFLF", Radius = "350.0", BaseDamage = "50.0", MinDamageFraction = "0.3", ImpulseStrength = "1500.0", bDrawDebug = "false"))
	static int32 CFLF_ExplodeRadialDamage(
		AActor* DamageCauser,
		AActor* InstigatorActor,
		FVector ExplodeCenter,
		float Radius = 350.0f,
		float BaseDamage = 50.0f,
		float MinDamageFraction = 0.3f,
		float ImpulseStrength = 1500.0f,
		bool bDrawDebug = false
	);

	/**
	 * 单节点检测视线遮挡与水平对准夹角（用于 AI 射程防抖与精准开火判定）
	 */
	UFUNCTION(BlueprintCallable, Category = "FPS|Combat", meta = (DisplayName = "CFLF_CheckAimAndLineOfSight", Keywords = "AI Sight Aim Angle LOS Hysteresis CFLF", MaxAngleDegrees = "15.0"))
	static void CFLF_CheckAimAndLineOfSight(
		AActor* SourceActor,
		AActor* TargetActor,
		float MaxAngleDegrees,
		bool& bHasLineOfSight,
		bool& bIsAimAligned
	);

	/**
	 * 单节点评估敌人战斗姿态迟滞防抖（计算 2D 距离并根据视线与迟滞带判定是保持输出还是迈步追击）
	 */
	UFUNCTION(BlueprintPure, Category = "FPS|Combat|AI", meta = (DisplayName = "CFLF_EvaluateCombatStance", Keywords = "AI Stance Hysteresis Range Distance CFLF"))
	static void CFLF_EvaluateCombatStance(
		const AActor* SourceActor,
		const AActor* TargetActor,
		float EngageRange,
		float DisengageRange,
		bool bCurrentStance,
		bool bHasLineOfSight,
		bool& bNewStance,
		float& OutDistance2D
	);
};
