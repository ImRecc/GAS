#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h" // <- The Passport
#include "DoomBaseCharacter.generated.h"

class UAbilitySystemComponent;
class UDoomAttributeSet;

UCLASS()
class ADoomBaseCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ADoomBaseCharacter();

	// 必须实现这个接口，让外部系统能找到这个Actor的ASC
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

protected:
	// 引擎生命周期函数，用于初始化ASC
	virtual void PossessedBy(AController* NewController) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS")
	UAbilitySystemComponent* AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS")
	UDoomAttributeSet* Attributes;

	virtual void Landed(const FHitResult& Hit) override;


};