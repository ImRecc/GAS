#include "NeonLightStrip.h"

#include "Components/AudioComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace NeonStripConstants
{
	const FName ParamEmissiveColor(TEXT("EmissiveColor"));
	const FName ParamEmissiveIntensity(TEXT("EmissiveIntensity"));
	const FName ParamSignTexture(TEXT("SignTexture"));
	constexpr float BasicCubeExtentCm = 100.0f;
}

ANeonLightStrip::ANeonLightStrip()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	StripMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StripMesh"));
	StripMesh->SetupAttachment(SceneRoot);
	StripMesh->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	StripMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StripMesh->SetCastShadow(false);
	StripMesh->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshFinder.Succeeded())
	{
		StripMesh->SetStaticMesh(CubeMeshFinder.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultNeonMatFinder(TEXT("/Game/customMaterials/LevelEnv/M_NeonSign_Base.M_NeonSign_Base"));
	if (DefaultNeonMatFinder.Succeeded())
	{
		BaseMaterial = DefaultNeonMatFinder.Object;
		StripMesh->SetMaterial(0, BaseMaterial);
	}

	RectLight = CreateDefaultSubobject<URectLightComponent>(TEXT("RectLight"));
	RectLight->SetupAttachment(SceneRoot);
	RectLight->SetCastShadows(false);
	RectLight->SetIntensityUnits(LightUnits);
	RectLight->SetIntensity(LightIntensity);
	RectLight->SetAttenuationRadius(LightAttenuationRadius);
	RectLight->SetLightColor(NeonColor, true);
	RectLight->SetSourceWidth(StripLengthCm);
	RectLight->SetSourceHeight(StripWidthCm);
	RectLight->SetBarnDoorAngle(LightBarnDoorAngle);

	HumAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("HumAudio"));
	HumAudio->SetupAttachment(SceneRoot);
	HumAudio->bAutoActivate = false;
}

void ANeonLightStrip::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyVisualSettings();
}

void ANeonLightStrip::BeginPlay()
{
	Super::BeginPlay();
	ApplyVisualSettings();

	const bool bShouldTick = (PulseMode != ENeonPulseMode::Static);
	SetActorTickEnabled(bShouldTick);
	PulseElapsedSeconds = PulsePhaseOffset;
}

void ANeonLightStrip::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (PulseMode == ENeonPulseMode::Static)
	{
		SetActorTickEnabled(false);
		return;
	}

	PulseElapsedSeconds += DeltaTime;
	ApplyPulseFactor(EvaluatePulseFactor(PulseElapsedSeconds));
}

#if WITH_EDITOR
void ANeonLightStrip::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropName = PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropName == GET_MEMBER_NAME_CHECKED(ANeonLightStrip, ColorPreset))
	{
		if (ColorPreset != ENeonColorPreset::Custom)
		{
			NeonColor = GetPresetLinearColor(ColorPreset);
		}
	}
	else if (PropName == GET_MEMBER_NAME_CHECKED(ANeonLightStrip, NeonColor)
		|| PropertyChangedEvent.GetMemberPropertyName() == GET_MEMBER_NAME_CHECKED(ANeonLightStrip, NeonColor))
	{
		ColorPreset = ENeonColorPreset::Custom;
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
	ApplyVisualSettings();
}
#endif

FLinearColor ANeonLightStrip::GetPresetLinearColor(ENeonColorPreset Preset)
{
	switch (Preset)
	{
	case ENeonColorPreset::CyberCyan:
		return FLinearColor(0.10f, 0.85f, 1.0f, 1.0f);
	case ENeonColorPreset::NeonAmber:
		return FLinearColor(1.0f, 0.45f, 0.08f, 1.0f);
	case ENeonColorPreset::HotMagenta:
		return FLinearColor(1.0f, 0.12f, 0.58f, 1.0f);
	case ENeonColorPreset::IceWhite:
		return FLinearColor(0.72f, 0.93f, 1.0f, 1.0f);
	case ENeonColorPreset::ToxicGreen:
		return FLinearColor(0.14f, 1.0f, 0.42f, 1.0f);
	case ENeonColorPreset::WarningRed:
		return FLinearColor(1.0f, 0.10f, 0.16f, 1.0f);
	case ENeonColorPreset::Custom:
	default:
		return FLinearColor(0.10f, 0.85f, 1.0f, 1.0f);
	}
}

void ANeonLightStrip::ApplyVisualSettings()
{
	const float SafeLength = FMath::Max(20.0f, StripLengthCm);
	const float SafeWidth = FMath::Max(4.0f, StripWidthCm);
	const float SafeDepth = FMath::Max(2.0f, StripDepthCm);

	if (StripMesh)
	{
		// 原生 Cube 边长为 100cm；灯条沿本地 Y 轴拉长以与 RectLight SourceWidth 天然对齐
		StripMesh->SetRelativeScale3D(FVector(
			SafeDepth / NeonStripConstants::BasicCubeExtentCm,
			SafeLength / NeonStripConstants::BasicCubeExtentCm,
			SafeWidth / NeonStripConstants::BasicCubeExtentCm
		));

		UMaterialInterface* TargetMat = BaseMaterial.Get();
		if (!TargetMat)
		{
			TargetMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/customMaterials/LevelEnv/M_NeonSign_Base.M_NeonSign_Base"));
			BaseMaterial = TargetMat;
		}

		if (TargetMat)
		{
			if (!DynamicMaterial || DynamicMaterial->Parent != TargetMat)
			{
				DynamicMaterial = UMaterialInstanceDynamic::Create(TargetMat, this);
			}

			if (DynamicMaterial)
			{
				if (StripMesh->GetMaterial(0) != DynamicMaterial)
				{
					StripMesh->SetMaterial(0, DynamicMaterial);
				}

				DynamicMaterial->SetVectorParameterValue(NeonStripConstants::ParamEmissiveColor, NeonColor);
				DynamicMaterial->SetScalarParameterValue(NeonStripConstants::ParamEmissiveIntensity, EmissiveIntensity);

				if (SignTexture)
				{
					DynamicMaterial->SetTextureParameterValue(NeonStripConstants::ParamSignTexture, SignTexture);
				}
				else
				{
					UTexture* DefaultSignTex = nullptr;
					if (TargetMat->GetTextureParameterValue(FMaterialParameterInfo(NeonStripConstants::ParamSignTexture), DefaultSignTex) && DefaultSignTex)
					{
						DynamicMaterial->SetTextureParameterValue(NeonStripConstants::ParamSignTexture, DefaultSignTex);
					}
				}
			}
		}
	}

	if (RectLight)
	{
		RectLight->SetRelativeLocation(FVector(SafeDepth * 0.5f + LightForwardOffsetCm, 0.0f, 0.0f));
		RectLight->SetRelativeRotation(FRotator(LightPitchOffsetDeg, 0.0f, 0.0f));
		RectLight->SetVisibility(bEnableLight);
		RectLight->SetHiddenInGame(!bEnableLight);
		RectLight->SetCastShadows(bCastLightShadows);
		RectLight->SetIntensityUnits(LightUnits);
		RectLight->SetLightColor(NeonColor, true);
		RectLight->SetIntensity(bEnableLight ? LightIntensity : 0.0f);
		RectLight->SetAttenuationRadius(LightAttenuationRadius);
		RectLight->SetSourceWidth(SafeLength);
		RectLight->SetSourceHeight(SafeWidth);
		RectLight->SetBarnDoorAngle(LightBarnDoorAngle);
	}
}

void ANeonLightStrip::ApplyPulseFactor(float Factor)
{
	const float ClampedFactor = FMath::Max(0.0f, Factor);
	if (DynamicMaterial)
	{
		DynamicMaterial->SetScalarParameterValue(NeonStripConstants::ParamEmissiveIntensity, EmissiveIntensity * ClampedFactor);
	}
	if (RectLight && bEnableLight)
	{
		RectLight->SetIntensity(LightIntensity * ClampedFactor);
	}
}

float ANeonLightStrip::EvaluatePulseFactor(float TimeSeconds) const
{
	const float MinF = FMath::Clamp(PulseMinFactor, 0.0f, 1.0f);
	const float Phase = TimeSeconds * FMath::Max(0.1f, PulseSpeed);

	switch (PulseMode)
	{
	case ENeonPulseMode::Breathe:
	{
		const float Wave = 0.5f + 0.5f * FMath::Sin(Phase * PI);
		return FMath::Lerp(MinF, 1.0f, Wave);
	}
	case ENeonPulseMode::FluorescentFlicker:
	{
		// 确定性复合谐波叠加短促跌落，无需 RNG 即可产生自然冷雨夜电流抖动感
		const float BaseWave = FMath::Sin(Phase * 7.3f) * 0.5f + FMath::Sin(Phase * 17.9f) * 0.35f + FMath::Sin(Phase * 31.1f) * 0.15f;
		const float Dip = (BaseWave < -0.58f) ? MinF : 1.0f;
		const float SubtleJitter = FMath::Lerp(0.88f, 1.0f, 0.5f + 0.5f * FMath::Sin(Phase * 11.0f));
		return Dip * SubtleJitter;
	}
	case ENeonPulseMode::Strobe:
	{
		const float Frac = FMath::Frac(Phase * 0.5f);
		return (Frac < 0.35f) ? 1.0f : MinF;
	}
	case ENeonPulseMode::Static:
	default:
		return 1.0f;
	}
}
