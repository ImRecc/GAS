// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/DeveloperSettings.h"
#include "EnemyMovesetData.h"
#include "EnemyCombatDirector.generated.h"

class APlayerCameraManager;

/**
 * 进攻令牌视野外被抢占时的优先级策略
 */
UENUM(BlueprintType)
enum class EEnemyPreemptionStrategy : uint8
{
	LongestHeld = 0       UMETA(DisplayName = "持有最久优先 / Longest Held"),
	FurthestDistance = 1  UMETA(DisplayName = "距离最远优先 / Furthest Distance"),
	Random = 2            UMETA(DisplayName = "随机剥夺 / Random")
};

/**
 * 敌人战斗导演编辑器配置面板 (UEnemyCombatSettings)
 * @Purpose 在 UE 编辑器中直接通过【Edit -> Project Settings -> Game -> Enemy Combat Director】直观微调全场节奏参数
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Enemy Combat Director / 敌人战斗导演配置"))
class GAS_API UEnemyCombatSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UEnemyCombatSettings();

	/** 全场同屏近战攻击令牌总上限（控制同时扑向玩家的最大近战怪数量） */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Token Pool", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxMeleeTokens = 2;

	/** 全场同屏远程攻击令牌总上限（控制同时向玩家发射弹幕的最大远程怪数量） */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Token Pool", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxRangedTokens = 2;

	/** 是否启用令牌持有防死锁超时自动回收（防止怪异常卡住霸占令牌导致全场怪发呆） */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Timeout & Deadlock")
	bool bEnableTokenTimeout = true;

	/** 令牌单次持有最大超时时间（秒），超过此时间且未主动释放将强制收回重新分发 */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Timeout & Deadlock", meta = (EditCondition = "bEnableTokenTimeout", ClampMin = "1.0", UIMin = "1.0"))
	float TokenTimeoutDuration = 6.0f;

	/** 当视野内怪申请令牌而池已满时，视野外多只持有者被剥夺的优先级策略 */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Preemption")
	EEnemyPreemptionStrategy PreemptionStrategy = EEnemyPreemptionStrategy::LongestHeld;

	/** 判定敌人是否处于玩家摄像机视野内的前向半角（度，默认 45° 对应 90° 视锥范围） */
	UPROPERTY(config, EditAnywhere, BlueprintReadWrite, Category = "Preemption", meta = (ClampMin = "10.0", ClampMax = "89.0"))
	float PlayerViewHalfAngleDeg = 45.0f;
};

/**
 * 单个活动令牌持有记录
 */
USTRUCT()
struct FEnemyTokenRecord
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<AActor> EnemyActor;

	UPROPERTY()
	EEnemyTokenType TokenType = EEnemyTokenType::None;

	UPROPERTY()
	float AcquireTimeSeconds = 0.0f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyTokenRevoked, AActor*, RevokedEnemy);

/**
 * 全场进攻令牌裁判 (UEnemyCombatDirector)
 * @Purpose 现代敌人战斗底座：继承自 UWorldSubsystem，随关卡生命周期全自动挂载，无需手动摆放 Actor
 * @Pipeline
 * 1. 怪物发动攻击前调用 TryRequestAttackToken 申请资格；
 * 2. 裁判维护近战/远程并发攻击配额；
 * 3. 视野优先抢占机制：视野内怪优先攻击，并可抢占视野外怪的攻击权；
 * 4. 攻击完成或被打断时调用 ReleaseAttackToken 释放；
 * 5. 支持 TWeakObjectPtr 死怪自动清理与超时防死锁回收。
 */
UCLASS()
class GAS_API UEnemyCombatDirector : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/**
	 * 申请进攻令牌（BlueprintCallable）
	 * @param EnemyActor 申请攻击的怪
	 * @param TokenType 申请的令牌类型（None / Melee / Ranged）
	 * @return 是否成功获得令牌（若未满或成功抢占返回 true，否则返回 false）
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy Combat Director|Tokens")
	bool TryRequestAttackToken(AActor* EnemyActor, EEnemyTokenType TokenType);

	/**
	 * 释放该怪占用的所有进攻令牌（BlueprintCallable，攻击结束/打断时调用）
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy Combat Director|Tokens")
	void ReleaseAttackToken(AActor* EnemyActor);

	/**
	 * 查询该怪当前是否持有任意有效攻击令牌（BlueprintCallable / BlueprintPure）
	 */
	UFUNCTION(BlueprintPure, Category = "Enemy Combat Director|Tokens")
	bool HasAttackToken(AActor* EnemyActor) const;

	/**
	 * 只读探测该怪此刻是否有机会拿到指定类型的攻击令牌（不实际占用令牌）
	 * @Purpose 供《光环》豺狼人风格 (CoverSkirmish) 判定是探头找枪线还是缩回掩体，避免“未持牌躲掩体→无视线抢不到牌”的死锁
	 */
	UFUNCTION(BlueprintPure, Category = "Enemy Combat Director|Tokens")
	bool CanRequestAttackToken(AActor* EnemyActor, EEnemyTokenType TokenType) const;

	/**
	 * 检查并消耗该 Bot 的低频 EQS 冷却锁（跨 StateTree 状态重入持久化，防高频查询卡帧）
	 * @param BotActor 发起 EQS 的 Bot
	 * @param CooldownSeconds 冷却时长（默认 1.2 秒）
	 * @return 若已过冷却则记录本次时间戳并返回 true；仍在冷却中返回 false
	 */
	UFUNCTION(BlueprintCallable, Category = "Enemy Combat Director|EQS")
	bool TryConsumeEQSCooldown(AActor* BotActor, float CooldownSeconds = 1.2f);

	/**
	 * 获取当前令牌分配调试字符串（如 "Melee: 1/2 | Ranged: 2/2"）
	 */
	UFUNCTION(BlueprintPure, Category = "Enemy Combat Director|Debug")
	FString GetActiveTokenDebugString() const;

	/**
	 * 当怪物的令牌因超时或被视野内怪抢占而强制剥夺时的广播委托
	 */
	UPROPERTY(BlueprintAssignable, Category = "Enemy Combat Director|Events")
	FOnEnemyTokenRevoked OnTokenRevoked;

	// ========================================================================
	// 运行时动态调参接口 (Blueprint & C++ 均可随时覆盖配置)
	// ========================================================================

	UFUNCTION(BlueprintCallable, Category = "Enemy Combat Director|Config")
	void SetMaxMeleeTokens(int32 NewMax);

	UFUNCTION(BlueprintPure, Category = "Enemy Combat Director|Config")
	int32 GetMaxMeleeTokens() const { return MaxMeleeTokens; }

	UFUNCTION(BlueprintCallable, Category = "Enemy Combat Director|Config")
	void SetMaxRangedTokens(int32 NewMax);

	UFUNCTION(BlueprintPure, Category = "Enemy Combat Director|Config")
	int32 GetMaxRangedTokens() const { return MaxRangedTokens; }

	UFUNCTION(BlueprintCallable, Category = "Enemy Combat Director|Config")
	void SetTokenTimeoutEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Enemy Combat Director|Config")
	bool IsTokenTimeoutEnabled() const { return bEnableTokenTimeout; }

	UFUNCTION(BlueprintCallable, Category = "Enemy Combat Director|Config")
	void SetTokenTimeoutDuration(float NewDuration);

	UFUNCTION(BlueprintPure, Category = "Enemy Combat Director|Config")
	float GetTokenTimeoutDuration() const { return TokenTimeoutDuration; }

	UFUNCTION(BlueprintCallable, Category = "Enemy Combat Director|Config")
	void SetPreemptionStrategy(EEnemyPreemptionStrategy NewStrategy);

	UFUNCTION(BlueprintPure, Category = "Enemy Combat Director|Config")
	EEnemyPreemptionStrategy GetPreemptionStrategy() const { return PreemptionStrategy; }

	UFUNCTION(BlueprintCallable, Category = "Enemy Combat Director|Config")
	void SetPlayerViewHalfAngle(float NewAngleDeg);

	UFUNCTION(BlueprintPure, Category = "Enemy Combat Director|Config")
	float GetPlayerViewHalfAngle() const { return PlayerViewHalfAngleDeg; }

protected:
	/** 清理已销毁的 Actor 或持有超时的活动令牌 */
	void CleanStaleTokens();

	/** 判断指定 Actor 是否处于玩家主摄像机视野内 */
	bool IsActorInPlayerView(const AActor* TargetActor, const APlayerCameraManager* CamMgr) const;

	/** 统计指定类型的当前有效持有数 */
	int32 CountActiveTokensOfType(EEnemyTokenType TokenType) const;

private:
	UPROPERTY()
	TArray<FEnemyTokenRecord> ActiveTokens;

	/** 记录每只 Bot 上一次触发低频 EQS 战术查询的世界时间戳（使用弱指针自动防死怪泄漏） */
	TMap<TWeakObjectPtr<AActor>, float> EQSLastQueryTimeMap;

	// 运行时配置缓存（初始化时从 UEnemyCombatSettings 读取）
	int32 MaxMeleeTokens = 2;
	int32 MaxRangedTokens = 2;
	bool bEnableTokenTimeout = true;
	float TokenTimeoutDuration = 6.0f;
	EEnemyPreemptionStrategy PreemptionStrategy = EEnemyPreemptionStrategy::LongestHeld;
	float PlayerViewHalfAngleDeg = 45.0f;
};
