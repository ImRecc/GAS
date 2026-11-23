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
 * 道具/武器在 4 格轮盘中的分类枚举 (0:上, 1:右, 2:下, 3:左)
 */
UENUM(BlueprintType)
enum class E_PropCategory : uint8
{
	Weapon = 0       UMETA(DisplayName = "武器 / Weapon"),
	Special = 1      UMETA(DisplayName = "特殊装备 / Special"),
	Gadget = 2       UMETA(DisplayName = "道具 / Gadget"),
	MeleeSidearm = 3 UMETA(DisplayName = "近战副武器 / Melee & Sidearm")
};

/**
 * FPS 第一人称动画、物理计算与战斗辅助函数库
 */
UCLASS()
class GAS_API UFPSAnimFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
#pragma region Arms & Recoil
	// ============================================================================
	// 1. 手臂与后坐力物理系统 (Arms & Recoil)
	// ============================================================================

	/**
	 * 武器后坐力 2.0 独立物理二阶弹簧解算器 (人话物理参数版)
	 * 采用闭式控制论公式由 ReturnTimeSec 与 Bounciness 自动反算刚度与初速度
	 */
	UFUNCTION(BlueprintCallable, Category = "FPS|Recoil|V2", meta = (DisplayName = "CFLF_SolveRecoilSpringV2", Keywords = "Recoil Spring V2 Human CFLF", ReturnTimeSec = "0.18", Bounciness = "0.25"))
	static void CFLF_SolveRecoilSpringV2(
		bool bTriggerRecoil,
		float KickBackCm,
		float KickUpDeg,
		float KickSideDeg,
		float ReturnTimeSec,
		float Bounciness,
		float DeltaTime,
		const FRotator& CurrentRecoilRot,
		const FVector& CurrentRecoilTrans,
		UPARAM(ref) FVectorSpringState& RecoilRotSpringState,
		UPARAM(ref) FVectorSpringState& RecoilTransSpringState,
		FRotator& OutRecoilRot,
		FVector& OutRecoilTrans
	);

	/**
	 * 手部物理摇晃 2.0 独立解算器 (人话物理参数版)
	 * 整合转头滞后、移动惯性、跳跃下落、落地重锤与 8 字形步态，由 FollowTimeSec 与 SwayBounciness 驱动
	 * 落地冲击采用独立的 LandingSpringState 按 LandingReturnTimeSec 解算，最后叠加输出
	 */
	UFUNCTION(BlueprintCallable, Category = "FPS|HandSway|V2", meta = (DisplayName = "CFLF_SolveHandSwayV2", Keywords = "HandSway Spring V2 Human CFLF", LookMaxYawDeg = "5.6", LookMaxPitchDeg = "4.4", StrafeTiltDeg = "2.0", LandingDropCm = "4.5", LandingReturnTimeSec = "0.25", BobAmplitudeCm = "0.66", StepsPerSecond = "1.5", FollowTimeSec = "0.85", SwayBounciness = "0.0"))
	static void CFLF_SolveHandSwayV2(
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
		UPARAM(ref) FRotator& LastAimRot,
		const FVector& CurrentLocalVel,
		UPARAM(ref) FVector& LastLocalVel,
		float InWalkPhase,
		bool bIsFalling,
		float DeltaTime,
		const FRotator& CurrentSwayRot,
		const FVector& CurrentSwayTrans,
		UPARAM(ref) FVectorSpringState& SwayRotSpringState,
		UPARAM(ref) FVectorSpringState& SwayTransSpringState,
		UPARAM(ref) FVectorSpringState& LandingSpringState,
		UPARAM(ref) FVector& CurrentLandOffset,
		float& OutWalkPhase,
		FRotator& OutSwayRot,
		FVector& OutSwayTrans,
		bool& bOutFootstepTrigger
	);

	// --- 1.0 废弃函数保留兼容（供老资产过渡使用，新逻辑切勿调用） ---

	/**
	 * 计算视角转动拖拽手部滞后目标（CFLF_calLookDragTarget）
	 */
	UFUNCTION(BlueprintPure, Category = "FPS|HandSway|Legacy", meta = (DisplayName = "CFLF_calLookDragTarget", Keywords = "HandSway LookDrag Target CFLF", LookSwayStrength = "1.0", MaxAngularSpeed = "720.0", DeprecatedFunction, DeprecationMessage = "1.0 已废弃，请迁移至 2.0 聚合函数 CFLF_SolveHandSwayV2"))
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
	UFUNCTION(BlueprintPure, Category = "FPS|HandSway|Legacy", meta = (DisplayName = "CFLF_calMoveImpulseTarget", Keywords = "HandSway MoveImpulse Target CFLF", SideSwayStrength = "1.0", DeprecatedFunction, DeprecationMessage = "1.0 已废弃，请迁移至 2.0 聚合函数 CFLF_SolveHandSwayV2"))
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
	UFUNCTION(BlueprintPure, Category = "FPS|HandSway|Legacy", meta = (DisplayName = "CFLF_calWalkBobbingTarget", Keywords = "HandSway WalkBobbing Target CFLF", MaxWalkSpeed = "600.0", StepFrequency = "9.5", BobStrength = "1.0", DeprecatedFunction, DeprecationMessage = "1.0 已废弃，请迁移至 2.0 聚合函数 CFLF_SolveHandSwayV2"))
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
	UFUNCTION(BlueprintCallable, Category = "FPS|HandSway|Legacy", meta = (DisplayName = "CFLF_solveDualSpringSway", Keywords = "HandSway Solve Spring Dual CFLF", ADSAlpha = "0.0", Stiffness = "165.0", DampingRatio = "0.6", DeprecatedFunction, DeprecationMessage = "1.0 已废弃，请迁移至 2.0 聚合函数 CFLF_SolveHandSwayV2"))
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
#pragma endregion

#pragma region Combat & Abilities
	// ============================================================================
	// 2. 战斗与 GAS 技能 (Combat & Abilities)
	// ============================================================================

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
	 * 控制器后坐力单帧步进解算（带自动回正与玩家压枪防对抗保护）
	 * - 开火时：外部向 TargetRecoilOffset 累加 (RandomYaw, KickPitch)，再由本函数每帧平滑施加上抬与回正。
	 * - 防抢鼠标：当玩家主动下拉鼠标（PlayerLookPitchInput > 0）时，自动抵消等量待回正角。
	 */
	UFUNCTION(BlueprintCallable, Category = "FPS|Recoil", meta = (DisplayName = "CFLF_StepSpringRecoil", Keywords = "Recoil Controller Spring Recovery AntiFight CFLF"))
	static void CFLF_StepSpringRecoil(
		UPARAM(ref) FVector2D& CurrentRecoilOffset,
		UPARAM(ref) FVector2D& TargetRecoilOffset,
		float PlayerLookPitchInput,
		float DeltaTime,
		float KickInterpSpeed,
		float RecoverySpeed,
		float& OutDeltaYawInput,
		float& OutDeltaPitchInput
	);
#pragma endregion

#pragma region AI Perception & Stance
	// ============================================================================
	// 3. AI 感知与交战姿态 (AI Perception & Stance)
	// ============================================================================

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
#pragma endregion

#pragma region UI, VFX & Pickups
	// ============================================================================
	// 4. UI 轮盘、掉落物与视觉特效 (UI, VFX & Pickups)
	// ============================================================================

	/**
	 * 径向武器轮盘扇区解析算法（纯数学无状态，不依赖 DeltaTime / TimeDilation）
	 * 坐标系约定：输入为屏幕空间虚拟光标向量（+X 向右，+Y 向下）。
	 * 角度约定：12 点钟正上方为 0°，顺时针递增至 360°（与 UMG SetRenderTransformAngle 一致）。
	 *
	 * @param InCursorVector  累加并钳制后的虚拟光标 2D 向量（像素/单位）
	 * @param NumSectors      轮盘扇区总数（默认 8）
	 * @param DeadZone        中心死区半径（向量长度小于此值时不判定扇区，防止微抖误触）
	 * @param OutAngleDegrees 输出规范化顺时针角度 [0, 360)，死区内保持 0.0
	 * @param OutSectorIndex  输出扇区索引 [0, NumSectors - 1]（0 = 12点钟顺时针递增），死区内返回 -1
	 * @param bOutInDeadZone  是否处于中心死区
	 */
	UFUNCTION(BlueprintPure, Category = "FPS|UI", meta = (DisplayName = "CFLF_CalculateRadialSector", Keywords = "Wheel Radial Sector Calculate Angle CFLF", NumSectors = "8", DeadZone = "35.0"))
	static void CFLF_CalculateRadialSector(
		FVector2D InCursorVector,
		int32 NumSectors,
		float DeadZone,
		float& OutAngleDegrees,
		int32& OutSectorIndex,
		bool& bOutInDeadZone
	);

	/**
	 * 获取未受 TimeDilation 缩放的引擎真实绝对时间步长（秒）
	 * 供 UI 在 0.15x 子弹时间下执行平滑插值（InterpTo）
	 */
	UFUNCTION(BlueprintPure, Category = "FPS|Math", meta = (DisplayName = "CFLF_GetUnscaledDeltaTime", Keywords = "Unscaled DeltaTime RealTime App CFLF"))
	static float CFLF_GetUnscaledDeltaTime();

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
	 * 在命中点生成瞬态无阴影爆闪点光源（用于非 Lumen 场景下击中瞬间环境闪光反馈）
	 * 自动在指定寿命内自毁，不产生额外 Actor，性能零负担。
	 */
	UFUNCTION(BlueprintCallable, Category = "FPS|Combat", meta = (WorldContext = "WorldContextObject", DisplayName = "CFLF_SpawnImpactFlash", Keywords = "Impact Flash Light Bullet Hit CFLF"))
	static void CFLF_SpawnImpactFlash(
		const UObject* WorldContextObject,
		FVector HitLocation,
		FVector HitNormal,
		FLinearColor LightColor = FLinearColor(0.08f, 0.88f, 1.0f),
		float Intensity = 4500.0f,
		float Radius = 380.0f,
		float LifeTime = 0.08f
	);
#pragma endregion
};
