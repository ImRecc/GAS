// Copyright Epic Games, Inc. All Rights Reserved.

#include "EnemyCombatDirector.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

// ----------------------------------------------------------------------------
// UEnemyCombatSettings 默认构造与分类
// ----------------------------------------------------------------------------
UEnemyCombatSettings::UEnemyCombatSettings()
{
	CategoryName = TEXT("Game");
	SectionName = TEXT("Enemy Combat Director");
}

// ----------------------------------------------------------------------------
// UEnemyCombatDirector 生命周期初始化
// ----------------------------------------------------------------------------
void UEnemyCombatDirector::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 从项目设置 (DefaultGame.ini) 载入初始配置
	if (const UEnemyCombatSettings* Settings = GetDefault<UEnemyCombatSettings>())
	{
		MaxMeleeTokens = Settings->MaxMeleeTokens;
		MaxRangedTokens = Settings->MaxRangedTokens;
		bEnableTokenTimeout = Settings->bEnableTokenTimeout;
		TokenTimeoutDuration = Settings->TokenTimeoutDuration;
		PreemptionStrategy = Settings->PreemptionStrategy;
		PlayerViewHalfAngleDeg = Settings->PlayerViewHalfAngleDeg;
	}

	ActiveTokens.Empty();
}

void UEnemyCombatDirector::Deinitialize()
{
	ActiveTokens.Empty();
	Super::Deinitialize();
}

// ----------------------------------------------------------------------------
// 清理失效与超时的令牌
// ----------------------------------------------------------------------------
void UEnemyCombatDirector::CleanStaleTokens()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		ActiveTokens.Empty();
		return;
	}

	const float CurrentTime = World->GetTimeSeconds();

	for (int32 i = ActiveTokens.Num() - 1; i >= 0; --i)
	{
		const FEnemyTokenRecord& Record = ActiveTokens[i];

		// 1. Actor 已被销毁或未有效挂载
		if (!Record.EnemyActor.IsValid())
		{
			ActiveTokens.RemoveAt(i);
			continue;
		}

		// 2. 超时防死锁强制回收
		if (bEnableTokenTimeout && TokenTimeoutDuration > 0.0f)
		{
			if (CurrentTime - Record.AcquireTimeSeconds > TokenTimeoutDuration)
			{
				AActor* ExpiredActor = Record.EnemyActor.Get();
				ActiveTokens.RemoveAt(i);

				// 广播剥夺通知，告知蓝图该怪已被超时收回
				if (ExpiredActor && OnTokenRevoked.IsBound())
				{
					OnTokenRevoked.Broadcast(ExpiredActor);
				}
			}
		}
	}
}

// ----------------------------------------------------------------------------
// 判断 Actor 是否在玩家视野内
// ----------------------------------------------------------------------------
bool UEnemyCombatDirector::IsActorInPlayerView(const AActor* TargetActor, const APlayerCameraManager* CamMgr) const
{
	if (!TargetActor || !CamMgr)
	{
		return false;
	}

	const FVector CamLoc = CamMgr->GetCameraLocation();
	const FVector CamForward = CamMgr->GetCameraRotation().Vector();
	const FVector DirToEnemy = (TargetActor->GetActorLocation() - CamLoc).GetSafeNormal();

	const float Dot = FVector::DotProduct(CamForward, DirToEnemy);
	const float CosThreshold = FMath::Cos(FMath::DegreesToRadians(PlayerViewHalfAngleDeg));

	return Dot >= CosThreshold;
}

// ----------------------------------------------------------------------------
// 统计指定类型的当前有效持有数
// ----------------------------------------------------------------------------
int32 UEnemyCombatDirector::CountActiveTokensOfType(EEnemyTokenType TokenType) const
{
	int32 Count = 0;
	for (const FEnemyTokenRecord& Record : ActiveTokens)
	{
		if (Record.TokenType == TokenType && Record.EnemyActor.IsValid())
		{
			++Count;
		}
	}
	return Count;
}

// ----------------------------------------------------------------------------
// 申请进攻令牌 (TryRequestAttackToken)
// ----------------------------------------------------------------------------
bool UEnemyCombatDirector::TryRequestAttackToken(AActor* EnemyActor, EEnemyTokenType TokenType)
{
	// 1. 无令牌限制招式直接放行
	if (TokenType == EEnemyTokenType::None)
	{
		return true;
	}

	if (!EnemyActor)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// 2. 预先清理死怪和超时令牌
	CleanStaleTokens();

	const float CurrentTime = World->GetTimeSeconds();

	// 3. 若当前怪已经持有同类型令牌，允许续期并直接放行
	for (FEnemyTokenRecord& Record : ActiveTokens)
	{
		if (Record.EnemyActor.Get() == EnemyActor && Record.TokenType == TokenType)
		{
			Record.AcquireTimeSeconds = CurrentTime; // 续期租约
			return true;
		}
	}

	// 4. 检查当前配额是否未满
	const int32 MaxCapacity = (TokenType == EEnemyTokenType::Melee) ? MaxMeleeTokens : MaxRangedTokens;
	const int32 CurrentCount = CountActiveTokensOfType(TokenType);

	if (CurrentCount < MaxCapacity)
	{
		// 池未满，直接分发
		FEnemyTokenRecord NewRecord;
		NewRecord.EnemyActor = EnemyActor;
		NewRecord.TokenType = TokenType;
		NewRecord.AcquireTimeSeconds = CurrentTime;
		ActiveTokens.Add(NewRecord);
		return true;
	}

	// 5. 池已满：触发【视野优先抢占机制】
	// 若申请者自身在玩家主摄像机视野内，检查持牌者中是否有“身处视野外”的怪
	APlayerCameraManager* CamMgr = UGameplayStatics::GetPlayerCameraManager(World, 0);
	if (!CamMgr)
	{
		return false; // 无法获取玩家相机，保守拒绝
	}

	const bool bApplicantInView = IsActorInPlayerView(EnemyActor, CamMgr);
	if (!bApplicantInView)
	{
		// 申请者在视野外且池已满，不予发放
		return false;
	}

	// 检索池中身处视野外的持有者候选列表
	TArray<int32> OutOfViewIndices;
	for (int32 i = 0; i < ActiveTokens.Num(); ++i)
	{
		const FEnemyTokenRecord& Record = ActiveTokens[i];
		if (Record.TokenType == TokenType && Record.EnemyActor.IsValid())
		{
			if (!IsActorInPlayerView(Record.EnemyActor.Get(), CamMgr))
			{
				OutOfViewIndices.Add(i);
			}
		}
	}

	if (OutOfViewIndices.Num() == 0)
	{
		// 视野外无可用被抢占怪（所有持牌者皆在视野内与玩家交火）
		return false;
	}

	// 根据抢占策略挑选牺牲者
	int32 VictimCandidateIndex = 0;
	if (PreemptionStrategy == EEnemyPreemptionStrategy::LongestHeld)
	{
		// 优先剥夺持有时间最久（AcquireTimeSeconds 最小）的怪
		float OldestTime = FLT_MAX;
		for (int32 k = 0; k < OutOfViewIndices.Num(); ++k)
		{
			const int32 Index = OutOfViewIndices[k];
			if (ActiveTokens[Index].AcquireTimeSeconds < OldestTime)
			{
				OldestTime = ActiveTokens[Index].AcquireTimeSeconds;
				VictimCandidateIndex = k;
			}
		}
	}
	else if (PreemptionStrategy == EEnemyPreemptionStrategy::FurthestDistance)
	{
		// 优先剥夺离摄像机距离最远的怪
		const FVector CamLoc = CamMgr->GetCameraLocation();
		float MaxDistSq = -1.0f;
		for (int32 k = 0; k < OutOfViewIndices.Num(); ++k)
		{
			const int32 Index = OutOfViewIndices[k];
			if (const AActor* HolderActor = ActiveTokens[Index].EnemyActor.Get())
			{
				const float DistSq = FVector::DistSquared(CamLoc, HolderActor->GetActorLocation());
				if (DistSq > MaxDistSq)
				{
					MaxDistSq = DistSq;
					VictimCandidateIndex = k;
				}
			}
		}
	}
	else // Random
	{
		VictimCandidateIndex = FMath::RandRange(0, OutOfViewIndices.Num() - 1);
	}

	const int32 VictimIndexInActive = OutOfViewIndices[VictimCandidateIndex];
	AActor* VictimActor = ActiveTokens[VictimIndexInActive].EnemyActor.Get();

	// 剥夺牺牲者的令牌
	ActiveTokens.RemoveAt(VictimIndexInActive);

	// 广播通知蓝图被剥夺事件
	if (VictimActor && OnTokenRevoked.IsBound())
	{
		OnTokenRevoked.Broadcast(VictimActor);
	}

	// 将令牌转交授予申请者
	FEnemyTokenRecord NewRecord;
	NewRecord.EnemyActor = EnemyActor;
	NewRecord.TokenType = TokenType;
	NewRecord.AcquireTimeSeconds = CurrentTime;
	ActiveTokens.Add(NewRecord);

	return true;
}

// ----------------------------------------------------------------------------
// 释放进攻令牌 (ReleaseAttackToken)
// ----------------------------------------------------------------------------
void UEnemyCombatDirector::ReleaseAttackToken(AActor* EnemyActor)
{
	if (!EnemyActor)
	{
		return;
	}

	for (int32 i = ActiveTokens.Num() - 1; i >= 0; --i)
	{
		if (ActiveTokens[i].EnemyActor.Get() == EnemyActor)
		{
			ActiveTokens.RemoveAt(i);
		}
	}
}

// ----------------------------------------------------------------------------
// 查询是否持有令牌 (HasAttackToken)
// ----------------------------------------------------------------------------
bool UEnemyCombatDirector::HasAttackToken(AActor* EnemyActor) const
{
	if (!EnemyActor)
	{
		return false;
	}

	for (const FEnemyTokenRecord& Record : ActiveTokens)
	{
		if (Record.EnemyActor.Get() == EnemyActor)
		{
			return true;
		}
	}
	return false;
}

// ----------------------------------------------------------------------------
// 调试信息输出 (GetActiveTokenDebugString)
// ----------------------------------------------------------------------------
FString UEnemyCombatDirector::GetActiveTokenDebugString() const
{
	const int32 MeleeCount = CountActiveTokensOfType(EEnemyTokenType::Melee);
	const int32 RangedCount = CountActiveTokensOfType(EEnemyTokenType::Ranged);

	return FString::Printf(TEXT("Melee: %d/%d | Ranged: %d/%d (Total Active: %d)"),
		MeleeCount, MaxMeleeTokens,
		RangedCount, MaxRangedTokens,
		ActiveTokens.Num());
}

// ----------------------------------------------------------------------------
// 运行时配置 Setter / Getter 实现
// ----------------------------------------------------------------------------
void UEnemyCombatDirector::SetMaxMeleeTokens(int32 NewMax)
{
	MaxMeleeTokens = FMath::Max(1, NewMax);
}

void UEnemyCombatDirector::SetMaxRangedTokens(int32 NewMax)
{
	MaxRangedTokens = FMath::Max(1, NewMax);
}

void UEnemyCombatDirector::SetTokenTimeoutEnabled(bool bEnabled)
{
	bEnableTokenTimeout = bEnabled;
}

void UEnemyCombatDirector::SetTokenTimeoutDuration(float NewDuration)
{
	TokenTimeoutDuration = FMath::Max(0.5f, NewDuration);
}

void UEnemyCombatDirector::SetPreemptionStrategy(EEnemyPreemptionStrategy NewStrategy)
{
	PreemptionStrategy = NewStrategy;
}

void UEnemyCombatDirector::SetPlayerViewHalfAngle(float NewAngleDeg)
{
	PlayerViewHalfAngleDeg = FMath::Clamp(NewAngleDeg, 10.0f, 89.0f);
}
