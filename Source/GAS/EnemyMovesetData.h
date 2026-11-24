// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "EnemyMovesetData.generated.h"

class UGameplayAbility;

/**
 * 敌人攻击占位令牌类型
 * @Purpose 用于导演系统（EnemyCombatDirector）控制同屏并发攻击的杂兵数量，防止玩家被围殴无死角暴毙
 */
UENUM(BlueprintType)
enum class EEnemyTokenType : uint8
{
	None = 0    UMETA(DisplayName = "无限制 / None"),
	Melee = 1   UMETA(DisplayName = "近战扑杀令牌 / Melee"),
	Ranged = 2  UMETA(DisplayName = "远程弹幕令牌 / Ranged")
};

/**
 * 敌人基础走位风格枚举
 * @Purpose 供 StateTree 走位任务（STT_EnemyPositioning）或移动状态机区分怪物的机动行为模式
 */
UENUM(BlueprintType)
enum class EEnemyMoveStyle : uint8
{
	Stationary = 0  UMETA(DisplayName = "站桩不动 / Stationary"),
	Chase = 1       UMETA(DisplayName = "近战追击 / Chase"),
	Skirmish = 2    UMETA(DisplayName = "游走迟滞 / Skirmish")
};

/**
 * 怪物单招式决策判定规则
 * @Purpose 定义招式的施放门槛（几何位置、血量阶段、冷却、ASC标签状态）与加权抽签权重
 */
USTRUCT(BlueprintType)
struct GAS_API FEnemyMoveRule
{
	GENERATED_BODY()

	/** 该招式的 GAS 激活标签（用于 TryActivateAbilitiesByTag 或记录冷却） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Identity")
	FGameplayTag MoveTag;

	/** 对应的技能卡带蓝图类（供怪物出生/切换阶段时自动向 ASC 注册授予） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Identity")
	TSubclassOf<UGameplayAbility> MoveAbilityClass = nullptr;

	/** 进攻占位令牌类型（None / Melee / Ranged） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Identity")
	EEnemyTokenType TokenType = EEnemyTokenType::None;

	/** 最小 2D 距离（厘米，仅在水平 XY 平面计算，低于该距离不可施放） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Conditions|Distance", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MinDistance = 0.0f;

	/** 最大 2D 距离（厘米，仅在水平 XY 平面计算，高于该距离不可施放） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Conditions|Distance", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxDistance = 1000.0f;

	/** 相对怪物正前方的最小水平夹角（度，0°~180°，0°为正前方） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Conditions|Angle", meta = (ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0", UIMax = "180.0"))
	float MinFrontAngle = 0.0f;

	/** 相对怪物正前方的最大水平夹角（度，0°~180°，可用于配置绕背反击招式） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Conditions|Angle", meta = (ClampMin = "0.0", ClampMax = "180.0", UIMin = "0.0", UIMax = "180.0"))
	float MaxFrontAngle = 180.0f;

	/** 施放该招式所需的自身最低血量比例（0.0 ~ 1.0） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Conditions|Health", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MinHealthPercent = 0.0f;

	/** 施放该招式允许的自身最高血量比例（0.0 ~ 1.0，低于此值进入残血阶段才可施放） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Conditions|Health", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0"))
	float MaxHealthPercent = 1.0f;

	/** 单招独立冷却时间（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Conditions|Cooldown", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CooldownSeconds = 1.5f;

	/** 满足所有门槛条件后的加权随机抽签权重（越高抽中概率越大） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Weight", meta = (ClampMin = "0.001", UIMin = "0.1"))
	float Weight = 1.0f;

	/** 白名单标签：怪物 ASC 必须同时拥有其中所有标签才可施放（支持二阶段狂暴/断肢触发） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Tags")
	FGameplayTagContainer RequiredTags;

	/** 黑名单标签：怪物 ASC 拥有其中任一标签则立刻禁用（支持破霸体/断臂封招） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Tags")
	FGameplayTagContainer BlockedTags;
};

/**
 * 敌人单帧战斗态势聚合快照
 * @Purpose 将高频几何计算与 ASC 状态只读汇聚为紧凑结构体，供 CFLF_PickBestEnemyMove 进行无状态纯函数匹配
 */
USTRUCT(BlueprintType)
struct GAS_API FEnemyCombatInfo
{
	GENERATED_BODY()

	/** 怪物与目标的忽略 Z 轴水平 2D 距离（厘米） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Info")
	float Distance2D = 0.0f;

	/** 目标与自身的高度差（目标 Z - 自身 Z，厘米） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Info")
	float HeightDiff = 0.0f;

	/** 怪物正前方与目标方向的水平夹角（度，范围 0° ~ 180°） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Info")
	float AngleToTarget = 0.0f;

	/** 视线检测是否畅通（ECC_Visibility 单线检测） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Info")
	bool bHasLineOfSight = false;

	/** 自身是否处于玩家摄像机前方半角视野内 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Info")
	bool bInPlayerView = false;

	/** 自身当前生命值百分比（0.0 ~ 1.0，无 ASC 安全兜底 1.0） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Info")
	float SelfHealthPercent = 1.0f;
};

/**
 * 敌人招式配置表数据资产 (UEnemyMovesetData)
 * @Purpose 现代敌人战斗底座：策划无需修改任何蓝图图表，通过创建资产即可配置完整的怪物攻击技能书
 */
UCLASS(BlueprintType)
class GAS_API UEnemyMovesetData : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** 招式规则列表（按从上到下配置，由 CFLF_PickBestEnemyMove 遍历筛选并加权随机） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Moveset")
	TArray<FEnemyMoveRule> Moves;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override
	{
		return FPrimaryAssetId(TEXT("EnemyMoveset"), GetFName());
	}
};
