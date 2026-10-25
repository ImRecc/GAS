#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "DoomAttributeSet.generated.h"

// Macro to auto-generate Getters/Setters for Attributes
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

struct FGameplayEffectModCallbackData;

UCLASS()
class UDoomAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:

	// --- Health ---
	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UDoomAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UDoomAttributeSet, MaxHealth)

	// --- Jump ---
	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData JumpCount;
	ATTRIBUTE_ACCESSORS(UDoomAttributeSet, JumpCount)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData MaxJumpCount;
	ATTRIBUTE_ACCESSORS(UDoomAttributeSet, MaxJumpCount)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData JumpPower;
	ATTRIBUTE_ACCESSORS(UDoomAttributeSet, JumpPower)

	// --- Dash ---
	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData DashPower;
	ATTRIBUTE_ACCESSORS(UDoomAttributeSet, DashPower)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData DashCount;
	ATTRIBUTE_ACCESSORS(UDoomAttributeSet, DashCount)

	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData MaxDashCount;
	ATTRIBUTE_ACCESSORS(UDoomAttributeSet, MaxDashCount)

protected:

	// Clamp attribute values before they are returned by queries (UI, etc.)
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	// React after a GameplayEffect modifies an attribute - do real clamping & death check here
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
};