// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSAnimFunctionLibrary.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Engine/HitResult.h"

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
