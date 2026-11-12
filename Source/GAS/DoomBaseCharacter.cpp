#include "DoomBaseCharacter.h"
#include "AbilitySystemComponent.h"
#include "DoomAttributeSet.h"
#include "AbilitySystemBlueprintLibrary.h"

ADoomBaseCharacter::ADoomBaseCharacter()
{
	// 1. 创建 AbilitySystemComponent (大脑)
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));

	// 2. 创建 Attributes (属性集)
	Attributes = CreateDefaultSubobject<UDoomAttributeSet>(TEXT("Attributes"));

	// 3. 默认 Doom Eternal 充能节奏：每格 1.4 秒
	DashRechargeInterval = 1.4f;
}

UAbilitySystemComponent* ADoomBaseCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void ADoomBaseCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// 初始化 ASC ActorInfo (OwnerActor = this, AvatarActor = this)
	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);

		// 绑定 DashCount 属性变化监听（仅单次绑定）
		if (Attributes)
		{
			auto& Delegate = AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(Attributes->GetDashCountAttribute());
			if (!Delegate.IsBoundToObject(this))
			{
				Delegate.AddUObject(this, &ADoomBaseCharacter::OnDashCountChanged);
			}
		}

		// 若配置了默认攻击技能卡带，自动赋予 ASC
		if (DefaultAttackAbility && !AbilitySystemComponent->FindAbilitySpecFromClass(DefaultAttackAbility))
		{
			AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(DefaultAttackAbility, 1, INDEX_NONE, this));
		}
	}
}

void ADoomBaseCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 容错安全检查：确保在非 Controller 附身或客户端情况下也能监听到属性
	if (AbilitySystemComponent && Attributes)
	{
		auto& Delegate = AbilitySystemComponent->GetGameplayAttributeValueChangeDelegate(Attributes->GetDashCountAttribute());
		if (!Delegate.IsBoundToObject(this))
		{
			Delegate.AddUObject(this, &ADoomBaseCharacter::OnDashCountChanged);
		}
	}

	// 确保在 BeginPlay 时也尝试赋予攻击技能卡带（若尚未赋予）
	if (DefaultAttackAbility && AbilitySystemComponent && !AbilitySystemComponent->FindAbilitySpecFromClass(DefaultAttackAbility))
	{
		AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(DefaultAttackAbility, 1, INDEX_NONE, this));
	}
}

void ADoomBaseCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);

	// 着地时广播 GAS 状态事件
	if (AbilitySystemComponent)
	{
		FGameplayTag EventTag = FGameplayTag::RequestGameplayTag(FName("State.Movement.Landed"));
		FGameplayEventData Payload;
		UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(this, EventTag, Payload);
	}
}

void ADoomBaseCharacter::OnDashCountChanged(const FOnAttributeChangeData& Data)
{
	if (!Attributes || !GetWorld())
	{
		return;
	}

	float CurrentDash = Data.NewValue;
	float MaxDash = Attributes->GetMaxDashCount();

	// 充能已满（如吃电池拾取物直接回满），立即清除定时器
	if (CurrentDash >= MaxDash)
	{
		GetWorld()->GetTimerManager().ClearTimer(DashRechargeTimerHandle);
		return;
	}

	// 冲刺次数不足，且当前没有定时器在跑，启动单格 1.4s 序列充能
	if (!GetWorld()->GetTimerManager().IsTimerActive(DashRechargeTimerHandle))
	{
		GetWorld()->GetTimerManager().SetTimer(
			DashRechargeTimerHandle,
			this,
			&ADoomBaseCharacter::HandleDashRechargeTick,
			FMath::Max(DashRechargeInterval, 0.05f),
			false
		);
	}
}

void ADoomBaseCharacter::HandleDashRechargeTick()
{
	if (!Attributes || !GetWorld())
	{
		return;
	}

	float CurrentDash = Attributes->GetDashCount();
	float MaxDash = Attributes->GetMaxDashCount();

	if (CurrentDash < MaxDash)
	{
		float NewDash = FMath::Min(CurrentDash + 1.0f, MaxDash);
		Attributes->SetDashCount(NewDash);
		OnDashChargeRecharged(NewDash);
	}

	// 序列充能核心：若充满一格后依然小于上限（例如连续按了两次冲刺），无缝接着充下一格
	if (Attributes->GetDashCount() < Attributes->GetMaxDashCount())
	{
		GetWorld()->GetTimerManager().SetTimer(
			DashRechargeTimerHandle,
			this,
			&ADoomBaseCharacter::HandleDashRechargeTick,
			FMath::Max(DashRechargeInterval, 0.05f),
			false
		);
	}
	else
	{
		GetWorld()->GetTimerManager().ClearTimer(DashRechargeTimerHandle);
	}
}

float ADoomBaseCharacter::GetDashRechargePercent() const
{
	if (!Attributes)
	{
		return 1.0f;
	}

	if (Attributes->GetDashCount() >= Attributes->GetMaxDashCount())
	{
		return 1.0f;
	}

	if (GetWorld() && GetWorld()->GetTimerManager().IsTimerActive(DashRechargeTimerHandle))
	{
		float Elapsed = GetWorld()->GetTimerManager().GetTimerElapsed(DashRechargeTimerHandle);
		return FMath::Clamp(Elapsed / FMath::Max(DashRechargeInterval, 0.001f), 0.0f, 1.0f);
	}

	return 0.0f;
}

void ADoomBaseCharacter::ResetDashRechargeTimer()
{
	if (!Attributes || !GetWorld())
	{
		return;
	}

	if (Attributes->GetDashCount() >= Attributes->GetMaxDashCount())
	{
		GetWorld()->GetTimerManager().ClearTimer(DashRechargeTimerHandle);
	}
	else
	{
		GetWorld()->GetTimerManager().SetTimer(
			DashRechargeTimerHandle,
			this,
			&ADoomBaseCharacter::HandleDashRechargeTick,
			FMath::Max(DashRechargeInterval, 0.05f),
			false
		);
	}
}
