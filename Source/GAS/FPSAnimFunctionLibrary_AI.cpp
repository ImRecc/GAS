// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSAnimFunctionLibrary.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/HitResult.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "DoomAttributeSet.h"
#include "DoomBaseCharacter.h"
#include "EngineUtils.h"

// ----------------------------------------------------------------------------
// 单节点视线遮挡与前向夹角对准检测 (CFLF_CheckAimAndLineOfSight)
// ----------------------------------------------------------------------------
void UFPSAnimFunctionLibrary::CFLF_CheckAimAndLineOfSight(
	AActor* SourceActor,
	AActor* TargetActor,
	float MaxAngleDegrees,
	bool& bHasLineOfSight,
	bool& bIsAimAligned)
{
	bHasLineOfSight = false;
	bIsAimAligned = false;

	if (!SourceActor || !TargetActor)
	{
		return;
	}

	UWorld* World = SourceActor->GetWorld();
	if (!World)
	{
		return;
	}

	// 1. 计算视线起点（眼睛高度）与终点（目标胸部高度）
	FVector StartLoc = SourceActor->GetActorLocation();
	if (const APawn* SourcePawn = Cast<APawn>(SourceActor))
	{
		const float EyeHeight = SourcePawn->BaseEyeHeight > 0.0f ? SourcePawn->BaseEyeHeight : 50.0f;
		StartLoc.Z += EyeHeight;
	}
	else
	{
		StartLoc.Z += 50.0f;
	}

	FVector TargetLoc = TargetActor->GetActorLocation();
	if (const APawn* TargetPawn = Cast<APawn>(TargetActor))
	{
		const float TargetEye = TargetPawn->BaseEyeHeight > 0.0f ? TargetPawn->BaseEyeHeight * 0.7f : 35.0f;
		TargetLoc.Z += TargetEye;
	}
	else
	{
		TargetLoc.Z += 35.0f;
	}

	// 2. 射线遮挡检测 (ECC_Visibility)
	FHitResult HitResult;
	FCollisionQueryParams TraceParams(TEXT("CheckLineOfSight"), false, SourceActor);
	TraceParams.AddIgnoredActor(SourceActor);

	const bool bHit = World->LineTraceSingleByChannel(
		HitResult,
		StartLoc,
		TargetLoc,
		ECC_Visibility,
		TraceParams
	);

	if (!bHit || HitResult.GetActor() == TargetActor)
	{
		bHasLineOfSight = true;
	}

	// 3. 计算前向对准水平夹角 (仅在水平 XY 平面计算，防止高低差误判朝向)
	const FVector ToTarget2D = (TargetLoc - StartLoc).GetSafeNormal2D();
	const FVector Forward2D = SourceActor->GetActorForwardVector().GetSafeNormal2D();

	const float Dot = FVector::DotProduct(Forward2D, ToTarget2D);
	const float Threshold = FMath::Cos(FMath::DegreesToRadians(MaxAngleDegrees));

	bIsAimAligned = (Dot >= Threshold);
}

// ----------------------------------------------------------------------------
// 战斗姿态两态迟滞防抖计算 (CFLF_EvaluateCombatStance)
// ----------------------------------------------------------------------------
void UFPSAnimFunctionLibrary::CFLF_EvaluateCombatStance(
	const AActor* SourceActor,
	const AActor* TargetActor,
	float EngageRange,
	float DisengageRange,
	bool bCurrentStance,
	bool bHasLineOfSight,
	bool& bNewStance,
	float& OutDistance2D)
{
	bNewStance = bCurrentStance;
	OutDistance2D = 0.0f;

	if (!SourceActor || !TargetActor)
	{
		bNewStance = false;
		return;
	}

	// 1. 计算忽略 Z 轴的纯水平 2D 距离（防止玩家跳跃或高低差误判）
	const FVector SLoc = SourceActor->GetActorLocation();
	const FVector TLoc = TargetActor->GetActorLocation();
	OutDistance2D = FVector2D::Distance(FVector2D(SLoc.X, SLoc.Y), FVector2D(TLoc.X, TLoc.Y));

	// 2. 迟滞缓冲区安全保护（若策划手抖把 Disengage 填得比 Engage 还小，自动取最大值形成防抖带）
	const float SafeDisengageRange = FMath::Max(DisengageRange, EngageRange);

	// 3. 两态迟滞状态机
	if (!bCurrentStance)
	{
		// 当前正在追击 (Approach) -> 只有当进入射程且具有视线时，才切换为站定输出
		if (OutDistance2D <= EngageRange && bHasLineOfSight)
		{
			bNewStance = true;
		}
	}
	else
	{
		// 当前正在站定输出 (Combat Stance) -> 只有当彻底脱离缓冲射程，或者完全丢失视线时，才切回追击
		if (OutDistance2D > SafeDisengageRange || !bHasLineOfSight)
		{
			bNewStance = false;
		}
	}
}

// ----------------------------------------------------------------------------
// 单节点采集敌人战斗态势聚合快照 (CFLF_GetEnemyCombatInfo)
// ----------------------------------------------------------------------------
FEnemyCombatInfo UFPSAnimFunctionLibrary::CFLF_GetEnemyCombatInfo(
	const AActor* EnemyActor,
	const AActor* TargetActor,
	float PlayerViewHalfAngleDeg)
{
	FEnemyCombatInfo OutInfo;

	if (!EnemyActor || !TargetActor)
	{
		return OutInfo;
	}

	UWorld* World = EnemyActor->GetWorld();
	if (!World)
	{
		return OutInfo;
	}

	const FVector EnemyLoc = EnemyActor->GetActorLocation();
	const FVector TargetLoc = TargetActor->GetActorLocation();

	// 1. 水平 2D 距离（忽略 Z 轴高低差）
	OutInfo.Distance2D = FVector2D::Distance(FVector2D(EnemyLoc.X, EnemyLoc.Y), FVector2D(TargetLoc.X, TargetLoc.Y));

	// 2. 高低差 (目标 Z - 自身 Z)
	OutInfo.HeightDiff = TargetLoc.Z - EnemyLoc.Z;

	// 3. 怪物正前方与目标方向的水平夹角（0° ~ 180°）
	const FVector Forward2D = EnemyActor->GetActorForwardVector().GetSafeNormal2D();
	const FVector ToTarget2D = (TargetLoc - EnemyLoc).GetSafeNormal2D();
	const float Dot = FMath::Clamp(FVector::DotProduct(Forward2D, ToTarget2D), -1.0f, 1.0f);
	OutInfo.AngleToTarget = FMath::RadiansToDegrees(FMath::Acos(Dot));

	// 4. 视线遮挡检测 (ECC_Visibility，眼睛到目标胸部)
	FVector StartLoc = EnemyLoc;
	if (const APawn* EnemyPawn = Cast<APawn>(EnemyActor))
	{
		const float EyeHeight = EnemyPawn->BaseEyeHeight > 0.0f ? EnemyPawn->BaseEyeHeight : 50.0f;
		StartLoc.Z += EyeHeight;
	}
	else
	{
		StartLoc.Z += 50.0f;
	}

	FVector TraceEndLoc = TargetLoc;
	if (const APawn* TargetPawn = Cast<APawn>(TargetActor))
	{
		const float TargetEye = TargetPawn->BaseEyeHeight > 0.0f ? TargetPawn->BaseEyeHeight * 0.7f : 35.0f;
		TraceEndLoc.Z += TargetEye;
	}
	else
	{
		TraceEndLoc.Z += 35.0f;
	}

	FHitResult HitResult;
	FCollisionQueryParams TraceParams(TEXT("CombatInfo_LOS"), false, EnemyActor);
	TraceParams.AddIgnoredActor(EnemyActor);

	const bool bHit = World->LineTraceSingleByChannel(
		HitResult,
		StartLoc,
		TraceEndLoc,
		ECC_Visibility,
		TraceParams
	);

	OutInfo.bHasLineOfSight = (!bHit || HitResult.GetActor() == TargetActor);

	// 5. 自身是否处于玩家摄像机前方半角视野内 (bInPlayerView)
	FVector CamLoc = TargetLoc;
	FVector CamForward = TargetActor->GetActorForwardVector();

	if (const APawn* TargetPawn = Cast<APawn>(TargetActor))
	{
		if (const APlayerController* PC = Cast<APlayerController>(TargetPawn->GetController()))
		{
			if (PC->PlayerCameraManager)
			{
				CamLoc = PC->PlayerCameraManager->GetCameraLocation();
				CamForward = PC->PlayerCameraManager->GetCameraRotation().Vector();
			}
		}
	}

	const FVector DirToEnemy = (EnemyLoc - CamLoc).GetSafeNormal();
	const float CamDot = FVector::DotProduct(CamForward, DirToEnemy);
	const float CamThreshold = FMath::Cos(FMath::DegreesToRadians(PlayerViewHalfAngleDeg));
	OutInfo.bInPlayerView = (CamDot >= CamThreshold);

	// 6. 安全读取自身生命值百分比 (Health / MaxHealth)
	float HealthPercent = 1.0f;
	if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(EnemyActor))
	{
		if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
		{
			if (const UDoomAttributeSet* AttrSet = ASC->GetSet<UDoomAttributeSet>())
			{
				const float MaxHP = AttrSet->GetMaxHealth();
				if (MaxHP > 0.0f)
				{
					HealthPercent = FMath::Clamp(AttrSet->GetHealth() / MaxHP, 0.0f, 1.0f);
				}
			}
		}
	}
	OutInfo.SelfHealthPercent = HealthPercent;

	return OutInfo;
}

// ----------------------------------------------------------------------------
// 招式配置表加权筛选与决策 (CFLF_PickBestEnemyMove)
// ----------------------------------------------------------------------------
bool UFPSAnimFunctionLibrary::CFLF_PickBestEnemyMove(
	const UEnemyMovesetData* MovesetData,
	const FEnemyCombatInfo& CombatInfo,
	const UAbilitySystemComponent* EnemyASC,
	const TMap<FGameplayTag, float>& LastUsedTimeMap,
	float CurrentWorldTime,
	FEnemyMoveRule& OutSelectedMove)
{
	OutSelectedMove = FEnemyMoveRule();

	if (!MovesetData || MovesetData->Moves.Num() == 0)
	{
		return false;
	}

	struct FMoveCandidate
	{
		int32 Index;
		float Weight;
	};

	TArray<FMoveCandidate> Candidates;
	float TotalWeight = 0.0f;

	for (int32 i = 0; i < MovesetData->Moves.Num(); ++i)
	{
		const FEnemyMoveRule& Rule = MovesetData->Moves[i];

		// 1. 2D 距离区间校验
		if (CombatInfo.Distance2D < Rule.MinDistance || CombatInfo.Distance2D > Rule.MaxDistance)
		{
			continue;
		}

		// 2. 水平夹角区间校验 (0°~180°)
		if (CombatInfo.AngleToTarget < Rule.MinFrontAngle || CombatInfo.AngleToTarget > Rule.MaxFrontAngle)
		{
			continue;
		}

		// 3. 自身血量比例阶段校验
		if (CombatInfo.SelfHealthPercent < Rule.MinHealthPercent || CombatInfo.SelfHealthPercent > Rule.MaxHealthPercent)
		{
			continue;
		}

		// 4. 单招独立冷却校验 (CurrentWorldTime - LastUsedTime >= CooldownSeconds)
		if (Rule.CooldownSeconds > 0.0f && Rule.MoveTag.IsValid())
		{
			if (const float* LastUsedTime = LastUsedTimeMap.Find(Rule.MoveTag))
			{
				if (CurrentWorldTime - *LastUsedTime < Rule.CooldownSeconds)
				{
					continue; // 尚在冷却中
				}
			}
		}

		// 5. 白名单标签匹配 (RequiredTags: 怪物 ASC 必须全包含)
		if (Rule.RequiredTags.Num() > 0)
		{
			if (!EnemyASC || !EnemyASC->HasAllMatchingGameplayTags(Rule.RequiredTags))
			{
				continue;
			}
		}

		// 6. 黑名单标签匹配 (BlockedTags: 怪物 ASC 拥有任一则禁用)
		if (Rule.BlockedTags.Num() > 0)
		{
			if (EnemyASC && EnemyASC->HasAnyMatchingGameplayTags(Rule.BlockedTags))
			{
				continue;
			}
		}

		// 7. 合法候选入池
		const float MoveWeight = FMath::Max(0.001f, Rule.Weight);
		Candidates.Add({ i, MoveWeight });
		TotalWeight += MoveWeight;
	}

	if (Candidates.Num() == 0)
	{
		return false;
	}

	// 加权随机抽选
	const float RandomVal = FMath::FRandRange(0.0f, TotalWeight);
	float CurrentWeightSum = 0.0f;

	for (const FMoveCandidate& Candidate : Candidates)
	{
		CurrentWeightSum += Candidate.Weight;
		if (RandomVal <= CurrentWeightSum)
		{
			OutSelectedMove = MovesetData->Moves[Candidate.Index];
			return true;
		}
	}

	// 浮点累加兜底：选择最后一个合法候选
	OutSelectedMove = MovesetData->Moves[Candidates.Last().Index];
	return true;
}

// ----------------------------------------------------------------------------
// 单节点查找指定范围内的最近活体敌对目标 (CFLF_FindNearestHostileTarget)
// ----------------------------------------------------------------------------
AActor* UFPSAnimFunctionLibrary::CFLF_FindNearestHostileTarget(
	AActor* SelfBot,
	float MaxSearchRadius,
	bool bRequireLineOfSight)
{
	if (!SelfBot)
	{
		return nullptr;
	}

	UWorld* World = SelfBot->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const FVector SelfLoc = SelfBot->GetActorLocation();

	// 1. 获取搜索发起者自身的阵营信息
	UAbilitySystemComponent* SelfASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(SelfBot);
	const FGameplayTag PlayerTag = FGameplayTag::RequestGameplayTag(FName("Team.Player"), false);
	const FGameplayTag EnemyTag = FGameplayTag::RequestGameplayTag(FName("Team.Enemy"), false);

	bool bSelfIsPlayer = false;
	bool bSelfIsEnemy = false;
	if (SelfASC)
	{
		bSelfIsPlayer = SelfASC->HasMatchingGameplayTag(PlayerTag);
		bSelfIsEnemy = SelfASC->HasMatchingGameplayTag(EnemyTag);
	}
	else if (const APawn* SelfPawn = Cast<APawn>(SelfBot))
	{
		bSelfIsPlayer = SelfPawn->IsPlayerControlled();
		bSelfIsEnemy = !bSelfIsPlayer;
	}

	AActor* BestTarget = nullptr;
	float BestDistSq = MaxSearchRadius > 0.0f ? FMath::Square(MaxSearchRadius) : FLT_MAX;

	const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(FName("State.Dead"), false);

	// 2. 遍历世界中所有活体 Pawn
	for (TActorIterator<APawn> It(World); It; ++It)
	{
		APawn* Candidate = *It;
		if (!Candidate || Candidate == SelfBot)
		{
			continue;
		}

		// 3. 过滤已死亡目标 (bIsDead 标记 / State.Dead 标签 / Health <= 0)
		if (const ADoomBaseCharacter* BaseChar = Cast<ADoomBaseCharacter>(Candidate))
		{
			if (BaseChar->bIsDead)
			{
				continue;
			}
		}

		UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(Candidate);
		if (TargetASC)
		{
			if (DeadTag.IsValid() && TargetASC->HasMatchingGameplayTag(DeadTag))
			{
				continue;
			}

			if (const UDoomAttributeSet* AttrSet = TargetASC->GetSet<UDoomAttributeSet>())
			{
				if (AttrSet->GetHealth() <= 0.0f)
				{
					continue;
				}
			}
		}

		// 4. 判定是否为敌对目标 (Team.Player vs Team.Enemy)
		bool bCandidateIsPlayer = false;
		bool bCandidateIsEnemy = false;
		if (TargetASC)
		{
			bCandidateIsPlayer = TargetASC->HasMatchingGameplayTag(PlayerTag);
			bCandidateIsEnemy = TargetASC->HasMatchingGameplayTag(EnemyTag);
		}
		else
		{
			bCandidateIsPlayer = Candidate->IsPlayerControlled();
			bCandidateIsEnemy = !bCandidateIsPlayer;
		}

		// 敌对关系判定：双方阵营互斥
		bool bIsHostile = false;
		if (bSelfIsPlayer && bCandidateIsEnemy)
		{
			bIsHostile = true;
		}
		else if (bSelfIsEnemy && bCandidateIsPlayer)
		{
			bIsHostile = true;
		}

		if (!bIsHostile)
		{
			continue;
		}

		// 5. 纯水平 2D 距离过滤
		const FVector CandLoc = Candidate->GetActorLocation();
		const float DistSq2D = FVector2D::DistSquared(FVector2D(SelfLoc.X, SelfLoc.Y), FVector2D(CandLoc.X, CandLoc.Y));
		if (DistSq2D > BestDistSq)
		{
			continue;
		}

		// 6. 可选的视线检测 (ECC_Visibility，眼睛到胸部)
		if (bRequireLineOfSight)
		{
			FVector StartLoc = SelfLoc;
			StartLoc.Z += (Cast<APawn>(SelfBot) && Cast<APawn>(SelfBot)->BaseEyeHeight > 0.0f) ? Cast<APawn>(SelfBot)->BaseEyeHeight : 50.0f;

			FVector EndLoc = CandLoc;
			EndLoc.Z += (Candidate->BaseEyeHeight > 0.0f) ? Candidate->BaseEyeHeight * 0.7f : 35.0f;

			FHitResult HitResult;
			FCollisionQueryParams TraceParams(TEXT("FindHostile_LOS"), false, SelfBot);
			TraceParams.AddIgnoredActor(SelfBot);

			const bool bHit = World->LineTraceSingleByChannel(HitResult, StartLoc, EndLoc, ECC_Visibility, TraceParams);
			if (bHit && HitResult.GetActor() != Candidate)
			{
				continue; // 视线被阻挡
			}
		}

		BestDistSq = DistSq2D;
		BestTarget = Candidate;
	}

	return BestTarget;
}
