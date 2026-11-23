// Copyright Epic Games, Inc. All Rights Reserved.

#include "FPSAnimFunctionLibrary.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Components/PrimitiveComponent.h"
#include "Components/PointLightComponent.h"
#include "TimerManager.h"
#include "Misc/App.h"

// ----------------------------------------------------------------------------
// 径向武器轮盘扇区解析算法 (CFLF_CalculateRadialSector)
// 纯数学无状态，输入为屏幕空间虚拟光标 (+X 右, +Y 下)，输出 12 点钟为 0° 的顺时针角度
// ----------------------------------------------------------------------------
void UFPSAnimFunctionLibrary::CFLF_CalculateRadialSector(
	FVector2D InCursorVector,
	int32 NumSectors,
	float DeadZone,
	float& OutAngleDegrees,
	int32& OutSectorIndex,
	bool& bOutInDeadZone)
{
	const int32 SafeSectors = FMath::Max(1, NumSectors);

	const float SafeDeadZone = FMath::Max(0.0f, DeadZone);
	if (InCursorVector.SizeSquared() < SafeDeadZone * SafeDeadZone)
	{
		OutAngleDegrees = 0.0f;
		OutSectorIndex = -1;
		bOutInDeadZone = true;
		return;
	}

	bOutInDeadZone = false;

	const float AngleDeg = FMath::RadiansToDegrees(FMath::Atan2(InCursorVector.X, -InCursorVector.Y));
	OutAngleDegrees = FMath::Fmod(AngleDeg + 360.0f, 360.0f);

	const float SectorStep = 360.0f / static_cast<float>(SafeSectors);
	const float ShiftedAngle = FMath::Fmod(OutAngleDegrees + (SectorStep * 0.5f), 360.0f);
	OutSectorIndex = FMath::FloorToInt(ShiftedAngle / SectorStep) % SafeSectors;
}

// ----------------------------------------------------------------------------
// 获取未受 TimeDilation 缩放的引擎真实绝对时间步长（秒）
// ----------------------------------------------------------------------------
float UFPSAnimFunctionLibrary::CFLF_GetUnscaledDeltaTime()
{
	return FApp::GetDeltaTime();
}

// ----------------------------------------------------------------------------
// 向四周抛洒带随机物理旋转和初速度的实体掉落物 (Halo 式爆装备 / 防堆叠环形分布)
// ----------------------------------------------------------------------------
void UFPSAnimFunctionLibrary::CFLF_SpawnScatteredPickups(
	const UObject* WorldContextObject,
	TSubclassOf<AActor> PickupClass,
	FVector CenterLocation,
	int32 Count,
	float ScatterRadius,
	float UpwardImpulse,
	float OutwardImpulse,
	float RandomAngularImpulse)
{
	if (!WorldContextObject || !PickupClass || Count <= 0)
	{
		return;
	}

	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull);
	if (!World)
	{
		return;
	}

	FVector ActualCenter = CenterLocation;
	if (ActualCenter.IsNearlyZero(1.0f))
	{
		if (const AActor* ContextActor = Cast<AActor>(WorldContextObject))
		{
			ActualCenter = ContextActor->GetActorLocation();
		}
	}

	const float BaseAngleDeg = FMath::FRandRange(0.0f, 360.0f);
	const float StepAngleDeg = 360.0f / static_cast<float>(Count);

	for (int32 i = 0; i < Count; ++i)
	{
		const float AngleDeg = BaseAngleDeg + StepAngleDeg * static_cast<float>(i) + FMath::FRandRange(-12.0f, 12.0f);
		const float AngleRad = FMath::DegreesToRadians(AngleDeg);

		const FVector HorizontalDir(FMath::Cos(AngleRad), FMath::Sin(AngleRad), 0.0f);
		const float ActualRadius = ScatterRadius + FMath::FRandRange(-5.0f, 15.0f);
		const FVector SpawnLoc = ActualCenter + HorizontalDir * ActualRadius + FVector(0.0f, 0.0f, 25.0f);
		const FRotator SpawnRot(0.0f, AngleDeg, 0.0f);

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		AActor* SpawnedPickup = World->SpawnActor<AActor>(PickupClass, SpawnLoc, SpawnRot, SpawnParams);
		if (!SpawnedPickup)
		{
			continue;
		}

		UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(SpawnedPickup->GetRootComponent());
		if (!PrimComp)
		{
			PrimComp = SpawnedPickup->FindComponentByClass<UPrimitiveComponent>();
		}

		if (PrimComp && PrimComp->IsSimulatingPhysics())
		{
			const FVector Impulse = HorizontalDir * OutwardImpulse + FVector(0.0f, 0.0f, UpwardImpulse + FMath::FRandRange(-25.0f, 25.0f));
			PrimComp->AddImpulse(Impulse, NAME_None, true);

			const FVector RandomSpin(
				FMath::FRandRange(-1.0f, 1.0f),
				FMath::FRandRange(-1.0f, 1.0f),
				FMath::FRandRange(-1.0f, 1.0f)
			);
			PrimComp->AddAngularImpulseInDegrees(RandomSpin.GetSafeNormal() * RandomAngularImpulse, NAME_None, true);
		}
	}
}

// ----------------------------------------------------------------------------
// 环形花瓣状抛洒掉落物数组 (CFLF_SpawnScatteredPickupList)
// ----------------------------------------------------------------------------
void UFPSAnimFunctionLibrary::CFLF_SpawnScatteredPickupList(
	const UObject* WorldContextObject,
	const TArray<TSubclassOf<AActor>>& PickupClasses,
	FVector CenterLocation,
	float ScatterRadius,
	float UpwardImpulse,
	float OutwardImpulse,
	float RandomAngularImpulse)
{
	if (!WorldContextObject || PickupClasses.IsEmpty())
	{
		return;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return;
	}

	if (CenterLocation.IsNearlyZero())
	{
		if (const AActor* ContextActor = Cast<AActor>(WorldContextObject))
		{
			CenterLocation = ContextActor->GetActorLocation();
		}
	}

	const int32 Count = PickupClasses.Num();
	const float AngleStep = 360.0f / static_cast<float>(Count);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	for (int32 i = 0; i < Count; ++i)
	{
		TSubclassOf<AActor> PickupClass = PickupClasses[i];
		if (!PickupClass)
		{
			continue;
		}

		const float BaseAngle = i * AngleStep;
		const float Jitter = FMath::FRandRange(-12.0f, 12.0f);
		const float Radians = FMath::DegreesToRadians(BaseAngle + Jitter);

		const FVector Offset(FMath::Cos(Radians) * ScatterRadius, FMath::Sin(Radians) * ScatterRadius, 10.0f);
		const FVector SpawnLoc = CenterLocation + Offset;
		const FRotator SpawnRot(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f);

		AActor* SpawnedPickup = World->SpawnActor<AActor>(PickupClass, SpawnLoc, SpawnRot, SpawnParams);
		if (!SpawnedPickup)
		{
			continue;
		}

		UPrimitiveComponent* PrimComp = Cast<UPrimitiveComponent>(SpawnedPickup->GetRootComponent());
		if (!PrimComp)
		{
			PrimComp = SpawnedPickup->FindComponentByClass<UPrimitiveComponent>();
		}

		if (PrimComp)
		{
			if (!PrimComp->IsSimulatingPhysics())
			{
				PrimComp->SetSimulatePhysics(true);
			}

			FVector LaunchVel = FVector(FMath::Cos(Radians), FMath::Sin(Radians), 0.0f) * OutwardImpulse;
			LaunchVel.Z = UpwardImpulse + FMath::FRandRange(-30.0f, 30.0f);

			PrimComp->AddImpulse(LaunchVel, NAME_None, true);

			FVector RandomSpin(
				FMath::FRandRange(-1.0f, 1.0f),
				FMath::FRandRange(-1.0f, 1.0f),
				FMath::FRandRange(-1.0f, 1.0f)
			);
			PrimComp->AddAngularImpulseInDegrees(RandomSpin.GetSafeNormal() * RandomAngularImpulse, NAME_None, true);
		}
	}
}

// ----------------------------------------------------------------------------
// 在命中点生成瞬态无阴影爆闪点光源 (CFLF_SpawnImpactFlash)
// ----------------------------------------------------------------------------
void UFPSAnimFunctionLibrary::CFLF_SpawnImpactFlash(
	const UObject* WorldContextObject,
	FVector HitLocation,
	FVector HitNormal,
	FLinearColor LightColor,
	float Intensity,
	float Radius,
	float LifeTime)
{
	if (!WorldContextObject)
	{
		return;
	}

	UWorld* World = nullptr;
	if (WorldContextObject)
	{
		World = WorldContextObject->GetWorld();
	}
	if (!World && GEngine)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
			{
				World = Context.World();
				if (World)
				{
					break;
				}
			}
		}
	}
	if (!World)
	{
		return;
	}

	const FVector SpawnLoc = HitLocation + (HitNormal.GetSafeNormal() * 12.0f);

	UPointLightComponent* LightComp = NewObject<UPointLightComponent>(World->GetWorldSettings());
	if (!LightComp)
	{
		return;
	}

	LightComp->RegisterComponentWithWorld(World);
	LightComp->SetWorldLocation(SpawnLoc);
	LightComp->SetLightColor(LightColor);
	LightComp->SetIntensity(Intensity);
	LightComp->SetAttenuationRadius(Radius);
	LightComp->SetCastShadows(false);
	LightComp->bUseInverseSquaredFalloff = false;

	FTimerHandle Handle;
	const float SafeLife = FMath::Max(0.02f, LifeTime);
	World->GetTimerManager().SetTimer(Handle, [LightComp]()
	{
		if (IsValid(LightComp))
		{
			LightComp->DestroyComponent();
		}
	}, SafeLife, false);
}
