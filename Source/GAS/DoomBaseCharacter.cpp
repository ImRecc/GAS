#include "DoomBaseCharacter.h"
#include "AbilitySystemComponent.h"
#include "DoomAttributeSet.h"
#include "AbilitySystemBlueprintLibrary.h" //to implement EventOnLanded

ADoomBaseCharacter::ADoomBaseCharacter()
{
	// 1. Create the Brain (AbilitySystemComponent)
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));

	// 2. Create the Data (Attributes)
	Attributes = CreateDefaultSubobject<UDoomAttributeSet>(TEXT("Attributes"));
}

UAbilitySystemComponent* ADoomBaseCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ADoomBaseCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// 3. Initialize the ASC. 
	// 参数1: OwnerActor (谁拥有这个组件)
	// 参数2: AvatarActor (这个组件在物理世界中的化身是谁)
	// 对于单机FPS，Owner和Avatar都是Character本身。
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);
	}
}

void ADoomBaseCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit); //engine's base landed logic

	//send GAS Event When landed
	if (AbilitySystemComponent)
	{
		FGameplayTag EventTag =
			FGameplayTag::RequestGameplayTag(FName("State.Movement.Landed"));
		FGameplayEventData Payload;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(this, EventTag, Payload);
	}
}