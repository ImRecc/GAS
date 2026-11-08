#include "DoomAttributeSet.h"
#include "DoomBaseCharacter.h"
#include "GameplayEffectExtension.h"
#include "GameplayTagsModule.h"
#include "AbilitySystemBlueprintLibrary.h"

UDoomAttributeSet::UDoomAttributeSet()
{
	// 默认赋予健康的初始属性，确保任何新创建的角色（敌人/玩家）一出生就拥有 100 满血！
	InitHealth(100.0f);
	InitMaxHealth(100.0f);
	InitJumpCount(2.0f);
	InitMaxJumpCount(2.0f);
	InitDashCount(2.0f);
	InitMaxDashCount(2.0f);
	InitDashPower(2000.0f);
	InitJumpPower(600.0f);
}

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

				// 直接调用 Character 的死亡实现事件，省去蓝图监听 GameplayEvent 的繁琐
				if (ADoomBaseCharacter* DoomChar = Cast<ADoomBaseCharacter>(OwningActor))
				{
					AActor* Killer = Payload.Instigator ? const_cast<AActor*>(Payload.Instigator.Get()) : nullptr;
					DoomChar->OnCharacterDeath(Killer);
				}
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
