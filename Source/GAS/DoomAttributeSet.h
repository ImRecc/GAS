#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "DoomAttributeSet.generated.h"

// Hacker-style macro to auto-generate Getters/Setters for Attributes
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

UCLASS()
class UDoomAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UDoomAttributeSet, Health)

		UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UDoomAttributeSet, MaxHealth)

		UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData JumpCount;
	ATTRIBUTE_ACCESSORS(UDoomAttributeSet, JumpCount)
	
		UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData MaxJumpCount;
	ATTRIBUTE_ACCESSORS(UDoomAttributeSet, MaxJumpCount)

		UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData JumpPower; // <- ¶¯Ì¬ÌøÔ¾Á¦¶È
	ATTRIBUTE_ACCESSORS(UDoomAttributeSet, JumpPower)

		UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData DashPower; 
	ATTRIBUTE_ACCESSORS(UDoomAttributeSet, DashPower)

		UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData DashCount; 
	ATTRIBUTE_ACCESSORS(UDoomAttributeSet, DashCount)

		UPROPERTY(BlueprintReadOnly, Category = "Attributes")
	FGameplayAttributeData MaxDashCount;
	ATTRIBUTE_ACCESSORS(UDoomAttributeSet, MaxDashCount)
};