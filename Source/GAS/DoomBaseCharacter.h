#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GameplayEffectTypes.h"
#include "DoomBaseCharacter.generated.h"

class UAbilitySystemComponent;
class UDoomAttributeSet;
class UGameplayAbility;

/**
 * 具有 GAS 架构与 Doom 风格机动核心的角色基类
 */
UCLASS()
class GAS_API ADoomBaseCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ADoomBaseCharacter();

	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	/** 默认攻击卡带技能（进入游戏时自动赋予 ASC） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Grunt")
	TSubclassOf<UGameplayAbility> DefaultAttackAbility;


	/** 死亡花瓣状散落掉落物配置列表 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Grunt")
	TArray<TSubclassOf<AActor>> LootDrops;

	/** 角色是否已经处于死亡状态（用于死怪核心保龄球碰撞连杀与逻辑门锁） */
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Combat|Grunt")
	bool bIsDead = false;

	/** 获取当前冲刺充能进度百分比（0.0 ~ 1.0），冲刺槽满时返回 1.0 */
	UFUNCTION(BlueprintPure, Category = "GAS|Dash")
	float GetDashRechargePercent() const;

	/** 手动重置/恢复冲刺充能计时器（例如吃电池时调用，可选） */
	UFUNCTION(BlueprintCallable, Category = "GAS|Dash")
	void ResetDashRechargeTimer();

	/** 单格冲刺充能所需时长（秒），默认 1.4s（对齐 Doom Eternal 节奏） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GAS|Dash", meta = (ClampMin = "0.1", UIMin = "0.1"))
	float DashRechargeInterval = 1.4f;

	/** 冲刺充能完成一格时的蓝图回调事件（可用于播放音效或轻微 UI 脉冲） */
	UFUNCTION(BlueprintImplementableEvent, Category = "GAS|Dash")
	void OnDashChargeRecharged(float NewDashCount);

	/**
	 * 当角色生命值归零死亡时的统一广播事件（带杀手 Instigator 引用）
	 * 任何子蓝图（玩家或敌人）直接在事件图表中搜索 "OnCharacterDeath" 即可使用！
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "GAS|Combat", meta = (DisplayName = "OnCharacterDeath"))
	void OnCharacterDeath(AActor* KillerActor);

protected:
	virtual void PossessedBy(AController* NewController) override;
	virtual void BeginPlay() override;
	virtual void Landed(const FHitResult& Hit) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS")
	UAbilitySystemComponent* AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS")
	UDoomAttributeSet* Attributes;

	/** 冲刺序列充能定时器句柄 */
	FTimerHandle DashRechargeTimerHandle;

	/** 监听 DashCount 属性变化的内部回调 */
	void OnDashCountChanged(const FOnAttributeChangeData& Data);

	/** 充能周期到达的处理函数 */
	void HandleDashRechargeTick();
};
