// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EnvironmentQuery/EnvQueryContext.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "EnemyMovesetData.h"
#include "BotEQSContexts.generated.h"

class AAIController;
class UEnvQuery;

/**
 * 敌对目标 EQS 上下文 (UBotEQSContext_Target)
 * @Purpose 为 EQS 查询提供当前 Bot 的敌对目标（100% 兼容 BvB 阵营索敌，禁止硬编码 GetPlayerPawn(0)）
 * @Pipeline 优先读取 AAIController::GetFocusActor() -> 若无效或指向自身，则回退调用 CFLF_FindNearestHostileTarget
 */
UCLASS(Blueprintable, meta = (DisplayName = "Bot EQS Context - Target"))
class GAS_API UBotEQSContext_Target : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};

/**
 * 同阵营队友 EQS 上下文 (UBotEQSContext_Allies)
 * @Purpose 为 EQS 查询提供当前世界内与发起者同阵营的存活队友列表，驱动扇形散开与防掩体扎堆打分
 */
UCLASS(Blueprintable, meta = (DisplayName = "Bot EQS Context - Allies"))
class GAS_API UBotEQSContext_Allies : public UEnvQueryContext
{
	GENERATED_BODY()

public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};

/**
 * Bot 战术 EQS 异步调度与掩体探头决策辅助库 (UBotEQSBlueprintLibrary)
 * @Purpose 封装低频异步 EQS 执行、主/副查询自动回退、防攻击/硬直中途走神校验，以及《光环》豺狼人探头决策
 */
UCLASS()
class GAS_API UBotEQSBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 触发带 1.2s 硬冷却锁与防打断校验的低频异步战术 EQS 寻路（CFLF_RunBotTacticalEQS）
	 * @param AIController 发起移动的 AIController
	 * @param PrimaryQuery 首选 EQS 资产（如 EQS_Bot_Cover 或 EQS_Bot_Flank）
	 * @param FallbackQuery 备选兜底 EQS 资产（主查询找不到合法点时自动链式触发，如空旷场地 Cover -> Flank）
	 * @param AcceptanceRadius 到达目标点的接受半径（默认 80cm）
	 * @param CooldownSeconds 每只 Bot 的 EQS 最小触发间隔（默认 1.2s，防状态树 0.25s 重入刷屏）
	 * @param bDrawDebugPoint 是否在选中的战术点绘制瞬态调试球（方便 PIE 调优观察）
	 * @return 本次调用是否通过冷却锁并成功发起异步 EQS（返回 false 表示仍在冷却中或入参无效）
	 */
	UFUNCTION(BlueprintCallable, Category = "FPS|Combat|AI", meta = (DisplayName = "CFLF_RunBotTacticalEQS", Keywords = "Bot EQS Tactical Flank Cover Move Async CFLF", AcceptanceRadius = "80.0", CooldownSeconds = "1.2", bDrawDebugPoint = "false"))
	static bool CFLF_RunBotTacticalEQS(
		AAIController* AIController,
		UEnvQuery* PrimaryQuery,
		UEnvQuery* FallbackQuery = nullptr,
		float AcceptanceRadius = 80.0f,
		float CooldownSeconds = 1.2f,
		bool bDrawDebugPoint = false
	);

	/**
	 * 判定《光环》豺狼人风格 (CoverSkirmish) Bot 此刻是否应探头寻找枪线（CFLF_ShouldCoverBotPeek）
	 * @Purpose 避免“未持牌躲掩体 → 无视线抢不到牌 → 永久躲墙死锁”：
	 *          当至少有 1 招冷却就绪 且 令牌裁判池有空位/可视野抢占时返回 true（探头跑 Flank）；
	 *          刚开完枪在冷却中 或 令牌池已满时返回 false（缩回掩体跑 Cover）。
	 */
	UFUNCTION(BlueprintPure, Category = "FPS|Combat|AI", meta = (DisplayName = "CFLF_ShouldCoverBotPeek", Keywords = "Bot EQS Cover Peek Jackal Token Cooldown CFLF"))
	static bool CFLF_ShouldCoverBotPeek(
		AActor* BotActor,
		const UEnemyMovesetData* MovesetData,
		const TMap<FGameplayTag, float>& LastUsedTimeMap,
		float CurrentWorldTime,
		EEnemyTokenType TokenType = EEnemyTokenType::Ranged
	);
};
