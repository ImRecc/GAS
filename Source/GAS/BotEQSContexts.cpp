// Copyright Epic Games, Inc. All Rights Reserved.

#include "BotEQSContexts.h"
#include "AIController.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "DoomBaseCharacter.h"
#include "DoomAttributeSet.h"
#include "FPSAnimFunctionLibrary.h"
#include "EnemyCombatDirector.h"

namespace BotEQSInternal
{
	/** 判断 Bot 当前是否处于允许执行异步 EQS 走位回调的健康非战斗状态 */
	static bool CanBotAcceptEQSMove(const APawn* BotPawn)
	{
		if (!BotPawn || !IsValid(BotPawn))
		{
			return false;
		}

		// 1. 排除已死亡角色
		if (const ADoomBaseCharacter* BaseChar = Cast<ADoomBaseCharacter>(BotPawn))
		{
			if (BaseChar->bIsDead)
			{
				return false;
			}
		}

		// 2. 检查 ASC 标签：若正在攻击、受击硬直或已死亡，绝对不覆盖其站位/停步指令
		if (UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(const_cast<APawn*>(BotPawn)))
		{
			static const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(FName("State.Dead"), false);
			static const FGameplayTag AttackingTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.Attacking"), false);
			static const FGameplayTag StaggeredTag = FGameplayTag::RequestGameplayTag(FName("State.Enemy.Staggered"), false);

			if ((DeadTag.IsValid() && ASC->HasMatchingGameplayTag(DeadTag)) ||
				(AttackingTag.IsValid() && ASC->HasMatchingGameplayTag(AttackingTag)) ||
				(StaggeredTag.IsValid() && ASC->HasMatchingGameplayTag(StaggeredTag)))
			{
				return false;
			}
		}

		return true;
	}

	/** 内部递归派发器：执行异步 EQS 并在主查询无有效点时自动回退副查询 */
	static void DispatchAsyncEQS(
		TWeakObjectPtr<AAIController> WeakAIC,
		TWeakObjectPtr<APawn> WeakPawn,
		UEnvQuery* QueryToRun,
		TWeakObjectPtr<UEnvQuery> WeakFallbackQuery,
		float AcceptanceRadius,
		bool bDrawDebugPoint)
	{
		APawn* Pawn = WeakPawn.Get();
		if (!Pawn || !QueryToRun)
		{
			return;
		}

		FEnvQueryRequest Request(QueryToRun, Pawn);
		Request.Execute(
			EEnvQueryRunMode::RandomBest25Pct,
			FQueryFinishedSignature::CreateLambda(
				[WeakAIC, WeakPawn, WeakFallbackQuery, AcceptanceRadius, bDrawDebugPoint](TSharedPtr<FEnvQueryResult> Result)
				{
					AAIController* AIC = WeakAIC.Get();
					APawn* BotPawn = WeakPawn.Get();
					if (!AIC || !CanBotAcceptEQSMove(BotPawn))
					{
						return;
					}

					if (Result.IsValid() && Result->IsSuccessful() && Result->Items.Num() > 0)
					{
						const FVector BestLocation = Result->GetItemAsLocation(0);
						AIC->MoveToLocation(
							BestLocation,
							AcceptanceRadius,
							true,  // bStopOnOverlap
							true,  // bUsePathfinding
							false, // bProjectDestinationToNavigation
							true,  // bCanStrafe
							nullptr,
							true   // bAllowPartialPath
						);

						if (bDrawDebugPoint)
						{
							if (UWorld* World = BotPawn->GetWorld())
							{
								DrawDebugSphere(World, BestLocation + FVector(0.f, 0.f, 35.f), 32.f, 8, FColor::Emerald, false, 1.1f, 0, 2.f);
							}
						}
					}
					else if (UEnvQuery* Fallback = WeakFallbackQuery.Get())
					{
						// 主查询无合法点（如空旷场地找不到掩体），自动回退执行副查询且不再二次嵌套回退
						DispatchAsyncEQS(WeakAIC, WeakPawn, Fallback, nullptr, AcceptanceRadius, bDrawDebugPoint);
					}
				})
		);
	}
}

// ============================================================================
// 1. UBotEQSContext_Target：提供敌对目标（优先 AIController Focus，兜底 BvB 索敌）
// ============================================================================
void UBotEQSContext_Target::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* QuerierActor = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QuerierActor)
	{
		return;
	}

	AActor* TargetActor = nullptr;

	// 1. 优先从 AIController 的 FocusActor 读取已锁定目标
	AAIController* AIC = Cast<AAIController>(QuerierActor);
	if (!AIC)
	{
		if (APawn* QuerierPawn = Cast<APawn>(QuerierActor))
		{
			AIC = Cast<AAIController>(QuerierPawn->GetController());
		}
	}

	if (AIC)
	{
		AActor* FocusActor = AIC->GetFocusActor();
		// 防自指保护：确保 FocusActor 有效且不是发起者自身
		if (IsValid(FocusActor) && FocusActor != QuerierActor)
		{
			TargetActor = FocusActor;
		}
	}

	// 2. 若 Focus 为空，回退调用 BvB 通用敌对索敌纯函数
	if (!IsValid(TargetActor))
	{
		AActor* SearchOrigin = (AIC && AIC->GetPawn()) ? static_cast<AActor*>(AIC->GetPawn()) : QuerierActor;
		TargetActor = UFPSAnimFunctionLibrary::CFLF_FindNearestHostileTarget(SearchOrigin, 5000.f, false);
	}

	// 3. 注入有效目标供 EQS Generator / Tests 使用
	if (IsValid(TargetActor))
	{
		UEnvQueryItemType_Actor::SetContextHelper(ContextData, TargetActor);
	}
}

// ============================================================================
// 2. UBotEQSContext_Allies：提供同阵营存活队友列表（防止扎堆与抢同一掩体）
// ============================================================================
void UBotEQSContext_Allies::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	AActor* QuerierActor = Cast<AActor>(QueryInstance.Owner.Get());
	if (!QuerierActor)
	{
		return;
	}

	// 若 Owner 是 AIController，转换为其控制的 Pawn 以便正确比对阵营与排除自身
	if (AAIController* AIC = Cast<AAIController>(QuerierActor))
	{
		if (APawn* ControlledPawn = AIC->GetPawn())
		{
			QuerierActor = ControlledPawn;
		}
	}

	UWorld* World = QuerierActor->GetWorld();
	if (!World)
	{
		return;
	}

	// 1. 识别发起者自身阵营
	const FGameplayTag PlayerTag = FGameplayTag::RequestGameplayTag(FName("Team.Player"), false);
	const FGameplayTag EnemyTag = FGameplayTag::RequestGameplayTag(FName("Team.Enemy"), false);
	const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(FName("State.Dead"), false);

	bool bQuerierIsPlayer = false;
	bool bQuerierIsEnemy = false;
	if (UAbilitySystemComponent* SelfASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(QuerierActor))
	{
		bQuerierIsPlayer = PlayerTag.IsValid() && SelfASC->HasMatchingGameplayTag(PlayerTag);
		bQuerierIsEnemy = EnemyTag.IsValid() && SelfASC->HasMatchingGameplayTag(EnemyTag);
	}
	else if (const APawn* SelfPawn = Cast<APawn>(QuerierActor))
	{
		bQuerierIsPlayer = SelfPawn->IsPlayerControlled();
		bQuerierIsEnemy = !bQuerierIsPlayer;
	}

	TArray<AActor*> Allies;

	// 2. 遍历世界中活着的 ADoomBaseCharacter，收集同阵营存活队友
	for (TActorIterator<ADoomBaseCharacter> It(World); It; ++It)
	{
		ADoomBaseCharacter* Candidate = *It;
		if (!Candidate || Candidate == QuerierActor || Candidate->bIsDead)
		{
			continue;
		}

		UAbilitySystemComponent* CandASC = Candidate->GetAbilitySystemComponent();
		if (CandASC)
		{
			if (DeadTag.IsValid() && CandASC->HasMatchingGameplayTag(DeadTag))
			{
				continue;
			}
			if (const UDoomAttributeSet* AttrSet = CandASC->GetSet<UDoomAttributeSet>())
			{
				if (AttrSet->GetHealth() <= 0.0f)
				{
					continue;
				}
			}
		}

		bool bCandIsPlayer = false;
		bool bCandIsEnemy = false;
		if (CandASC)
		{
			bCandIsPlayer = PlayerTag.IsValid() && CandASC->HasMatchingGameplayTag(PlayerTag);
			bCandIsEnemy = EnemyTag.IsValid() && CandASC->HasMatchingGameplayTag(EnemyTag);
		}
		else
		{
			bCandIsPlayer = Candidate->IsPlayerControlled();
			bCandIsEnemy = !bCandIsPlayer;
		}

		// 仅收录同阵营队友（敌对双方互不计入 Allies）
		const bool bIsSameFaction = (bQuerierIsEnemy && bCandIsEnemy) || (bQuerierIsPlayer && bCandIsPlayer);
		if (bIsSameFaction)
		{
			Allies.Add(Candidate);
		}
	}

	if (Allies.Num() > 0)
	{
		UEnvQueryItemType_Actor::SetContextHelper(ContextData, Allies);
	}
}

// ============================================================================
// 3. UBotEQSBlueprintLibrary：低频异步 EQS 执行与掩体探头判定
// ============================================================================
bool UBotEQSBlueprintLibrary::CFLF_RunBotTacticalEQS(
	AAIController* AIController,
	UEnvQuery* PrimaryQuery,
	UEnvQuery* FallbackQuery,
	float AcceptanceRadius,
	float CooldownSeconds,
	bool bDrawDebugPoint)
{
	if (!AIController || !PrimaryQuery)
	{
		return false;
	}

	APawn* BotPawn = AIController->GetPawn();
	if (!BotEQSInternal::CanBotAcceptEQSMove(BotPawn))
	{
		return false;
	}

	UWorld* World = BotPawn->GetWorld();
	if (!World)
	{
		return false;
	}

	// 1. 通过全局战斗导演校验并消耗 1.2s 硬冷却锁（跨状态重入持久化）
	if (UEnemyCombatDirector* Director = World->GetSubsystem<UEnemyCombatDirector>())
	{
		if (!Director->TryConsumeEQSCooldown(BotPawn, FMath::Max(0.1f, CooldownSeconds)))
		{
			return false;
		}
	}

	// 2. 发起异步 EQS 查询（带主/副回退与防开火中途打断保护）
	BotEQSInternal::DispatchAsyncEQS(
		AIController,
		BotPawn,
		PrimaryQuery,
		FallbackQuery,
		AcceptanceRadius,
		bDrawDebugPoint
	);

	return true;
}

bool UBotEQSBlueprintLibrary::CFLF_ShouldCoverBotPeek(
	AActor* BotActor,
	const UEnemyMovesetData* MovesetData,
	const TMap<FGameplayTag, float>& LastUsedTimeMap,
	float CurrentWorldTime,
	EEnemyTokenType TokenType)
{
	if (!BotActor || !MovesetData || MovesetData->Moves.Num() == 0)
	{
		return false;
	}

	UWorld* World = BotActor->GetWorld();
	if (!World)
	{
		return false;
	}

	// 1. 探测令牌裁判池是否有机会获得攻击权（有空位、已持牌或可视野抢占）
	if (const UEnemyCombatDirector* Director = World->GetSubsystem<UEnemyCombatDirector>())
	{
		if (!Director->CanRequestAttackToken(BotActor, TokenType))
		{
			return false; // 令牌池满且无法抢占 -> 缩回掩体
		}
	}

	// 2. 检查招式表中是否至少有 1 招冷却已转好（刚打完进入冷却时主动缩回掩体）
	for (const FEnemyMoveRule& Rule : MovesetData->Moves)
	{
		if (Rule.CooldownSeconds <= 0.0f || !Rule.MoveTag.IsValid())
		{
			return true;
		}

		const float* LastUsed = LastUsedTimeMap.Find(Rule.MoveTag);
		if (!LastUsed || (CurrentWorldTime - *LastUsed >= Rule.CooldownSeconds))
		{
			return true; // 招式冷却就绪且有令牌机会 -> 探头找枪线！
		}
	}

	return false;
}
