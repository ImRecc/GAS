#include "DoomAttributeSet.h"
#include "GameplayEffectExtension.h"
#include "GameplayTagsModule.h"
#include "AbilitySystemBlueprintLibrary.h"

void UDoomAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	// Clamp Health so that UI queries never show values outside [0, MaxHealth]
	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	else if (Attribute == GetJumpCountAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxJumpCount());
	}
	else if (Attribute == GetDashCountAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxDashCount());
	}
}

void UDoomAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// --- Health clamping & death ---
	if (Data.EvaluatedData.Attribute == GetHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth(), 0.f, GetMaxHealth()));

		if (GetHealth() <= 0.f)
		{
			// Broadcast a GameplayEvent so Blueprints can react to death
			if (AActor* OwningActor = GetOwningActor())
			{
				FGameplayTag DeathTag = FGameplayTag::RequestGameplayTag(FName("State.Dead"));
				FGameplayEventData Payload;
				Payload.Instigator = Data.EffectSpec.GetEffectContext().GetOriginalInstigator();
				UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(OwningActor, DeathTag, Payload);
			}
		}
	}
	// --- JumpCount clamping ---
	else if (Data.EvaluatedData.Attribute == GetJumpCountAttribute())
	{
		SetJumpCount(FMath::Clamp(GetJumpCount(), 0.f, GetMaxJumpCount()));
	}
	// --- DashCount clamping ---
	else if (Data.EvaluatedData.Attribute == GetDashCountAttribute())
	{
		SetDashCount(FMath::Clamp(GetDashCount(), 0.f, GetMaxDashCount()));
	}
}