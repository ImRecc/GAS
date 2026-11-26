// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSAnimFunctionLibrary.h"
#include "DoomAttributeSet.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "GameplayTagsModule.h"
#include "Abilities/GameplayAbility.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "DrawDebugHelpers.h"
#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"
#include "Components/PrimitiveComponent.h"

// ----------------------------------------------------------------------------
// 对目标施加统一伤害 (自动兼容 GAS ASC 目标与传统 UE 目标，支持触发受击 GameplayEvent)
// ----------------------------------------------------------------------------
bool UFPSAnimFunctionLibrary::CFLF_ApplyDamageToTarget(
	AActor* TargetActor,
	float DamageAmount,
	AActor* InstigatorActor,
	TSubclassOf<UGameplayEffect> DamageGEClass,
	FGameplayTag HitEventTag)
{
	if (!TargetActor || DamageAmount <= 0.0f)
	{
		return false;
	}

	// 1. 尝试通过 GAS ASC 施加伤害
	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	if (TargetASC)
	{
		TSubclassOf<UGameplayEffect> EffectiveGEClass = DamageGEClass;
		if (!EffectiveGEClass)
		{
			EffectiveGEClass = LoadClass<UGameplayEffect>(nullptr, TEXT("/Game/bps/gameplayEffect-Ability/GE_Damage.GE_Damage_C"));
		}

		if (EffectiveGEClass)
		{
			FGameplayEffectContextHandle Context = TargetASC->MakeEffectContext();
			Context.AddInstigator(InstigatorActor, InstigatorActor);

			FGameplayEffectSpecHandle SpecHandle = TargetASC->MakeOutgoingSpec(EffectiveGEClass, 1.0f, Context);
			if (SpecHandle.IsValid())
			{
				FGameplayTag DamageTag = FGameplayTag::RequestGameplayTag(FName("Data.Damage"), false);
				if (DamageTag.IsValid())
				{
					SpecHandle.Data->SetSetByCallerMagnitude(DamageTag, -FMath::Abs(DamageAmount));
				}

				TargetASC->ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());

				// 发送受击打断事件（例如 Event.Hit.Weakspot 触发 GA_Enemy_HitReact 打断蓄力与赋予硬直）
				if (HitEventTag.IsValid())
				{
					FGameplayEventData Payload;
					Payload.EventTag = HitEventTag;
					Payload.Instigator = InstigatorActor;
					Payload.Target = TargetActor;
					Payload.EventMagnitude = DamageAmount;
					UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(TargetActor, HitEventTag, Payload);
				}

				return true;
			}
		}
	}

	// 2. 传统通用 Fallback：对非 GAS 对象调用引擎原生伤害
	AController* InstigatorController = InstigatorActor ? InstigatorActor->GetInstigatorController() : nullptr;
	UGameplayStatics::ApplyDamage(TargetActor, DamageAmount, InstigatorController, InstigatorActor, nullptr);

	return true;
}

// ----------------------------------------------------------------------------
// 动态向目标 Actor 的 ASC 赋予指定的 GAS 技能 (卡带热插拔)
// ----------------------------------------------------------------------------
FGameplayAbilitySpecHandle UFPSAnimFunctionLibrary::CFLF_GiveAbilityToActor(
	AActor* TargetActor,
	TSubclassOf<UGameplayAbility> AbilityClass,
	int32 AbilityLevel)
{
	if (!TargetActor || !AbilityClass)
	{
		return FGameplayAbilitySpecHandle();
	}

	UAbilitySystemComponent* ASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(TargetActor);
	if (!ASC)
	{
		return FGameplayAbilitySpecHandle();
	}

	if (FGameplayAbilitySpec* ExistingSpec = ASC->FindAbilitySpecFromClass(AbilityClass))
	{
		return ExistingSpec->Handle;
	}

	return ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, AbilityLevel, INDEX_NONE, TargetActor));
}

// ----------------------------------------------------------------------------
// 单节点前方/周围球形范围伤害检测：自动比对 Team.Player 与 Team.Enemy 过滤敌对目标并结算伤害
// ----------------------------------------------------------------------------
int32 UFPSAnimFunctionLibrary::CFLF_SphereDamageHostiles(
	AActor* InstigatorActor,
	FVector SphereCenter,
	float Radius,
	float Damage,
	bool bDrawDebug)
{
	if (!InstigatorActor || Radius <= 0.0f || Damage <= 0.0f)
	{
		return 0;
	}

	UWorld* World = InstigatorActor->GetWorld();
	if (!World)
	{
		return 0;
	}

	if (SphereCenter.IsNearlyZero())
	{
		SphereCenter = InstigatorActor->GetActorLocation();
	}

	if (bDrawDebug)
	{
		DrawDebugSphere(World, SphereCenter, Radius, 16, FColor::Orange, false, 0.4f, 0, 1.5f);
	}

	// 1. 获取攻击者所属阵营
	UAbilitySystemComponent* InstigatorASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(InstigatorActor);
	const FGameplayTag PlayerTag = FGameplayTag::RequestGameplayTag(FName("Team.Player"), false);
	const FGameplayTag EnemyTag = FGameplayTag::RequestGameplayTag(FName("Team.Enemy"), false);

	bool bInstigatorIsPlayer = false;
	bool bInstigatorIsEnemy = false;
	if (InstigatorASC)
	{
		bInstigatorIsPlayer = InstigatorASC->HasMatchingGameplayTag(PlayerTag);
		bInstigatorIsEnemy = InstigatorASC->HasMatchingGameplayTag(EnemyTag);
	}
	else
	{
		const APawn* InstPawn = Cast<APawn>(InstigatorActor);
		bInstigatorIsPlayer = InstPawn && InstPawn->IsPlayerControlled();
		bInstigatorIsEnemy = !bInstigatorIsPlayer;
	}

	// 2. 球形检测重叠的目标 (查询 Pawn 碰撞对象)
	TArray<FOverlapResult> Overlaps;
	FCollisionShape SphereShape = FCollisionShape::MakeSphere(Radius);
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams QueryParams(TEXT("SphereDamageHostiles"), false, InstigatorActor);
	QueryParams.AddIgnoredActor(InstigatorActor);

	World->OverlapMultiByObjectType(
		Overlaps,
		SphereCenter,
		FQuat::Identity,
		ObjectQueryParams,
		SphereShape,
		QueryParams
	);

	int32 DamagedCount = 0;
	TSet<AActor*> ProcessedActors;

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* HitActor = Overlap.GetActor();
		if (!HitActor || HitActor == InstigatorActor || ProcessedActors.Contains(HitActor))
		{
			continue;
		}
		ProcessedActors.Add(HitActor);

		UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor);
		bool bTargetIsPlayer = false;
		bool bTargetIsEnemy = false;
		if (TargetASC)
		{
			bTargetIsPlayer = TargetASC->HasMatchingGameplayTag(PlayerTag);
			bTargetIsEnemy = TargetASC->HasMatchingGameplayTag(EnemyTag);
		}
		else
		{
			const APawn* TargetPawn = Cast<APawn>(HitActor);
			bTargetIsPlayer = TargetPawn && TargetPawn->IsPlayerControlled();
			bTargetIsEnemy = !bTargetIsPlayer;
		}

		const bool bIsHostile = (bInstigatorIsPlayer && bTargetIsEnemy) || (bInstigatorIsEnemy && bTargetIsPlayer);
		if (bIsHostile)
		{
			if (CFLF_ApplyDamageToTarget(HitActor, Damage, InstigatorActor))
			{
				DamagedCount++;
			}
		}
	}

	return DamagedCount;
}

// ----------------------------------------------------------------------------
// 投射物与环境范围爆炸结算 (带距离线性衰减、敌对过滤、物理冲击波与死怪炸飞推力)
// ----------------------------------------------------------------------------
int32 UFPSAnimFunctionLibrary::CFLF_ExplodeRadialDamage(
	AActor* DamageCauser,
	AActor* InstigatorActor,
	FVector ExplodeCenter,
	float Radius,
	float BaseDamage,
	float MinDamageFraction,
	float ImpulseStrength,
	bool bDrawDebug)
{
	if (Radius <= 0.0f || BaseDamage <= 0.0f)
	{
		return 0;
	}

	UWorld* World = nullptr;
	if (DamageCauser)
	{
		World = DamageCauser->GetWorld();
	}
	else if (InstigatorActor)
	{
		World = InstigatorActor->GetWorld();
	}

	if (!World)
	{
		return 0;
	}

	if (ExplodeCenter.IsNearlyZero() && DamageCauser)
	{
		ExplodeCenter = DamageCauser->GetActorLocation();
	}

	if (bDrawDebug)
	{
		DrawDebugSphere(World, ExplodeCenter, Radius, 24, FColor::Red, false, 1.0f, 0, 2.0f);
	}

	// 1. 获取攻击者所属阵营
	AActor* ActualInstigator = InstigatorActor;
	if (!ActualInstigator && DamageCauser)
	{
		ActualInstigator = DamageCauser->GetInstigator();
	}

	const FGameplayTag PlayerTag = FGameplayTag::RequestGameplayTag(FName("Team.Player"), false);
	const FGameplayTag EnemyTag = FGameplayTag::RequestGameplayTag(FName("Team.Enemy"), false);

	bool bInstigatorIsPlayer = false;
	bool bInstigatorIsEnemy = false;
	if (ActualInstigator)
	{
		UAbilitySystemComponent* InstigatorASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(ActualInstigator);
		if (InstigatorASC)
		{
			bInstigatorIsPlayer = InstigatorASC->HasMatchingGameplayTag(PlayerTag);
			bInstigatorIsEnemy = InstigatorASC->HasMatchingGameplayTag(EnemyTag);
		}
		else
		{
			const APawn* InstPawn = Cast<APawn>(ActualInstigator);
			bInstigatorIsPlayer = InstPawn && InstPawn->IsPlayerControlled();
			bInstigatorIsEnemy = !bInstigatorIsPlayer;
		}
	}

	// 2. 空间球形重叠查询 (查询 Pawn 与 PhysicsBody)
	TArray<FOverlapResult> Overlaps;
	FCollisionShape SphereShape = FCollisionShape::MakeSphere(Radius);
	FCollisionObjectQueryParams ObjectQueryParams;
	ObjectQueryParams.AddObjectTypesToQuery(ECC_Pawn);
	ObjectQueryParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FCollisionQueryParams QueryParams(TEXT("ExplodeRadialDamage"), false, DamageCauser);
	if (DamageCauser)
	{
		QueryParams.AddIgnoredActor(DamageCauser);
	}

	World->OverlapMultiByObjectType(
		Overlaps,
		ExplodeCenter,
		FQuat::Identity,
		ObjectQueryParams,
		SphereShape,
		QueryParams
	);

	int32 DamagedCount = 0;
	TSet<AActor*> ProcessedActors;
	const float SafeMinFraction = FMath::Clamp(MinDamageFraction, 0.0f, 1.0f);

	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* HitActor = Overlap.GetActor();
		if (!HitActor || HitActor == DamageCauser || ProcessedActors.Contains(HitActor))
		{
			continue;
		}
		ProcessedActors.Add(HitActor);

		bool bShouldDamage = true;
		if (ActualInstigator)
		{
			if (HitActor == ActualInstigator)
			{
				bShouldDamage = false;
			}
			else
			{
				UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor);
				bool bTargetIsPlayer = false;
				bool bTargetIsEnemy = false;
				if (TargetASC)
				{
					bTargetIsPlayer = TargetASC->HasMatchingGameplayTag(PlayerTag);
					bTargetIsEnemy = TargetASC->HasMatchingGameplayTag(EnemyTag);
				}
				else
				{
					const APawn* TargetPawn = Cast<APawn>(HitActor);
					bTargetIsPlayer = TargetPawn && TargetPawn->IsPlayerControlled();
					bTargetIsEnemy = !bTargetIsPlayer;
				}

				if (bInstigatorIsPlayer || bInstigatorIsEnemy)
				{
					bShouldDamage = (bInstigatorIsPlayer && bTargetIsEnemy) || (bInstigatorIsEnemy && bTargetIsPlayer);
				}
			}
		}

		if (bShouldDamage)
		{
			const float Dist = FVector::Dist(ExplodeCenter, HitActor->GetActorLocation());
			const float DistAlpha = FMath::Clamp(Dist / Radius, 0.0f, 1.0f);
			const float ScaledDamage = FMath::Lerp(BaseDamage, BaseDamage * SafeMinFraction, DistAlpha);

			if (CFLF_ApplyDamageToTarget(HitActor, ScaledDamage, ActualInstigator))
			{
				DamagedCount++;
			}
		}

		// 3. 物理冲击波推力
		if (ImpulseStrength > 0.0f)
		{
			TArray<UPrimitiveComponent*> PrimComponents;
			HitActor->GetComponents<UPrimitiveComponent>(PrimComponents);
			for (UPrimitiveComponent* PrimComp : PrimComponents)
			{
				if (PrimComp && PrimComp->IsSimulatingPhysics())
				{
					PrimComp->AddRadialImpulse(
						ExplodeCenter,
						Radius,
						ImpulseStrength,
						ERadialImpulseFalloff::RIF_Linear,
						true
					);
				}
			}
		}
	}

	return DamagedCount;
}

// ----------------------------------------------------------------------------
// 控制器后坐力单帧步进解算（带自动回正与玩家压枪防对抗保护）
// ----------------------------------------------------------------------------
void UFPSAnimFunctionLibrary::CFLF_StepSpringRecoil(
	FVector2D& CurrentRecoilOffset,
	FVector2D& TargetRecoilOffset,
	float PlayerLookPitchInput,
	float DeltaTime,
	float KickInterpSpeed,
	float RecoverySpeed,
	float& OutDeltaYawInput,
	float& OutDeltaPitchInput)
{
	if (DeltaTime <= KINDA_SMALL_NUMBER)
	{
		OutDeltaYawInput = 0.0f;
		OutDeltaPitchInput = 0.0f;
		return;
	}

	// 1. 防抢鼠标保护：玩家主动下拉压枪时抵消待回正的 Pitch 偏移
	if (PlayerLookPitchInput > 0.0f && CurrentRecoilOffset.Y > 0.0f)
	{
		const float Cancelled = FMath::Min(CurrentRecoilOffset.Y, PlayerLookPitchInput);
		CurrentRecoilOffset.Y -= Cancelled;
		TargetRecoilOffset.Y = FMath::Max(0.0f, TargetRecoilOffset.Y - Cancelled);
	}

	const FVector2D PrevOffset = CurrentRecoilOffset;

	// 2. 目标偏移平滑衰减回 0（触发自动回正）
	const float SafeRecoverySpeed = FMath::Max(0.1f, RecoverySpeed);
	TargetRecoilOffset = FMath::Vector2DInterpTo(TargetRecoilOffset, FVector2D::ZeroVector, DeltaTime, SafeRecoverySpeed);

	// 3. 当前偏移高速追赶目标
	const float SafeKickSpeed = FMath::Max(0.1f, KickInterpSpeed);
	const float ActiveSpeed = (TargetRecoilOffset.SizeSquared() > CurrentRecoilOffset.SizeSquared())
		? SafeKickSpeed
		: SafeRecoverySpeed;
	CurrentRecoilOffset = FMath::Vector2DInterpTo(CurrentRecoilOffset, TargetRecoilOffset, DeltaTime, ActiveSpeed);

	// 4. 输出本帧相对增量（UE5 中负 Pitch = 抬头，因此取反）
	const FVector2D Delta = CurrentRecoilOffset - PrevOffset;
	OutDeltaYawInput = Delta.X;
	OutDeltaPitchInput = -Delta.Y;
}
