# 现代 Bot AI 系统全资产清理重构后扫描与交付报告 (BOT_AI_STATE_REPORT.md)

> **实机执行方式**：Unreal Editor Live MCP Session (`http://127.0.0.1:8000/mcp`)  
> **扫描时间**：2026-11-25 21:46 (实机运行时直接提取 AST/DSL)  
> **涉及资产**：`/Game/bps/botAI/` 目录全资产、`/Game/bps/BP_enemyBase`、`/Game/bps/AIC_EnemyBase`  
> **全局编译状态**：**8 大核心蓝图 + 1 个 StateTree 全量编译通过 (0 错误 0 警告)**  
> **交付目标**：提交给 **Neon 模型** 评估用户清理后的资产最新状态、结构拓扑与待决策点。

---

## 1. 架构总览与数据流向拓扑 (Architecture Topology)

```mermaid
flowchart TD
    subgraph Director ["全局裁判 (C++ UWorldSubsystem)"]
        ECD["UEnemyCombatDirector<br>• 近战/远程令牌池 (配额 2/2)<br>• 视野优先抢占机制<br>• 6.0s 租约超时防死锁回收"]
    end

    subgraph Body ["Bot 物理底盘 (BP_enemyBase)"]
        BC["ADoomBaseCharacter (C++)"] --> BPB["BP_enemyBase (蓝图)"]
        BPB -->|实现| BPI["BPI_BotConfig (解耦接口)<br>• IF_GetBotMoveset<br>• IF_GetBotMoveStyle<br>• IF_GetBotCooldownMap<br>• IF_RecordBotMoveUsed"]
        BPB -->|BeginPlay| AutoLoad["自动装填招式与受击技能<br>• CFLF_GiveAbilityToActor (Moves)<br>• CFLF_GiveAbilityToActor (GA_Enemy_HitReact)"]
        BPB -->|Death| AutoRelease["死亡立即归还令牌<br>• ReleaseAttackToken (Self)"]
    end

    subgraph Controller ["AI 控制器双脑分流 (AIC_EnemyBase)"]
        AIC["AIC_EnemyBase (蓝图)"]
        Toggle["开关: bUseStateTreeBrain (默认 false)"]
        Legacy["旧大脑: F_EvaluateAI 循环定时器"]
        STC["新大脑: StateTreeAIComponent<br>(指向 ST_Enemy_Arena)"]

        AIC --> Toggle
        Toggle -->|false (向后兼容)| Legacy
        Toggle -->|true (现代双脑)| STC
    end

    subgraph StateTree ["StateTree 通用大脑 (ST_Enemy_Arena)"]
        Schema["StateTreeAIComponentSchema"]
        Eval["评估器: STE_EnemyCombatContext<br>• Context.Actor / Context.AIController<br>• CFLF_FindNearestHostileTarget (索敌与 BvB 兜底)<br>• CFLF_GetEnemyCombatInfo (几何/血量 10Hz 节流解算)"]
        
        State_Stagger["① State: Staggered (bIsStaggered == true)<br>Task: STT_EnemyStagger<br>• StopMovement (Context.AIController)<br>• ReleaseAttackToken (Context.Actor)<br>• 监听 !bIsStaggered 自动退出"]
        State_Attack["② State: Attack (bIsStaggered == false)<br>Task: STT_EnemyAttack<br>• CFLF_PickBestEnemyMove 选招<br>• TryRequestAttackToken (传入 TokenType)<br>• 激活招式 GA 并调用 IF_RecordBotMoveUsed 记录冷却<br>• 0.05s 延时后监听 !State.Enemy.Attacking 退出<br>• ExitState 兜底归还令牌"]
        State_Pos["③ State: Positioning (机动与占位)<br>Task: STT_EnemyPositioning<br>• 【用户清理现状】：骨架就绪，待连入优雅机动路线<br>• OnTick (0.25s) -> 自动跃迁回 Attack 尝试抽签"]

        Eval -->|输出 CombatInfo / bIsStaggered / bIsAttacking| StateTree
        Schema --> State_Stagger
        Schema --> State_Attack
        Schema --> State_Pos
        STC -.->|驱动| StateTree
    end

    subgraph GAS ["GAS 技能打断闭环"]
        GA_Hit["GA_Enemy_HitReact<br>• 监听 Event.Hit.Weakspot<br>• 赋予 State.Enemy.Staggered<br>• 取消 Ability.Enemy.Attack"]
    end

    State_Attack -->|申请 / 释放令牌| ECD
    State_Stagger -->|打断立即释放令牌| ECD
    GA_Hit -->|赋予 Staggered Tag| Eval
```

---

## 2. 实机编译验收汇总清单

| 资产路径 | 资产类型 | 用户清理与修复要点 | 编译结果 | 实机状态 |
| :--- | :--- | :--- | :---: | :---: |
| `/Game/bps/botAI/BPI_BotConfig` | Interface | 4 大解耦接口定义 | **0 错误** | 正常就绪 |
| `/Game/bps/botAI/GA_Enemy_HitReact` | GameplayAbility | 受击硬直 0.4s + 结束技能 | **0 错误** | 正常就绪 |
| `/Game/bps/botAI/STE_EnemyCombatContext` | StateTreeEvaluator | Context 规范 + BvB 索敌 + 0.1s 节流 | **0 错误** | 正常就绪 |
| `/Game/bps/botAI/STT_EnemyStagger` | StateTreeTask | Context 规范 + 停脚 + 归还令牌 + 标签监听 | **0 错误** | 正常就绪 |
| `/Game/bps/botAI/STT_EnemyAttack` | StateTreeTask | Context 规范 + TokenType + 冷却记录 + 延时退出 | **0 错误** | 正常就绪 |
| `/Game/bps/botAI/STT_EnemyPositioning` | StateTreeTask | Context 规范 + 节点清理精简（待 Neon 评估布线） | **0 错误** | 正常就绪 |
| `/Game/bps/BP_enemyBase` | Character Blueprint | 自动装填招式卡带与受击技能 + 死亡归还令牌 | **0 错误** | 正常就绪 |
| `/Game/bps/AIC_EnemyBase` | AIController Blueprint | StateTreeAIComponent 挂载 + `bUseStateTreeBrain` 无损双脑开关 | **0 错误** | 正常就绪 |
| `/Game/bps/botAI/ST_Enemy_Arena` | StateTree | Evaluator + 3 大状态机层级与过渡转移 | **0 错误** | 正常就绪 |

---

## 3. 用户清理后各资产真实 DSL 与节点连接现状

以下内容均由 Live Unreal MCP 直接从当前虚幻编辑器内存 AST 反编译导出，完全反映用户在编辑器内整理后的最新状态：

### 3.1 `BPI_BotConfig` (Bot 配置解耦接口)
```lisp
(fn IF_GetBotMoveset () (return 0))
(fn IF_GetBotMoveStyle () (return "Stationary"))
(fn IF_GetBotCooldownMap () (return 0))
(fn IF_RecordBotMoveUsed (MoveTag WorldTime))
```
- **变量**：无变量（纯接口）。
- **作用**：隔离 AI 底盘与 StateTree 大脑，实现完全面向接口编程。

---

### 3.2 `GA_Enemy_HitReact` (受击硬直 GameplayAbility)
```lisp
(event Ability|EventActivateAbility
  (Development|PrintString "\"[Staggered 受击硬直!]\"" true true "(R=0.000000,G=0.660000,B=1.000000,A=1.000000)" 1.000000)
  (Utilities|FlowControl|Delay 0.400000)
  (Ability|EndAbility))

(event Ability|EventOnEndAbility (bWasCancelled))
```
- **变量**：无。
- **作用**：当弱点被击中触发受击时，赋予受击硬直标签，维持 0.4s 表现后结束技能。

---

### 3.3 `STE_EnemyCombatContext` (战斗上下文评估器)
```lisp
(event EventTreeStart
  (bind _actor (Variables|Context|GetActor))
  (bind _returnvalue (FPS|Combat|AI|CFLF_FindNearestHostileTarget _actor))
  (Utilities|IsValid _returnvalue
    (:"Is Valid"
      (Variables|Context|SetTargetActor _returnvalue))))

(event EventTick (DeltaTime)
  (bind _actor (Variables|Context|GetActor))
  (bind _returnvalue (Ability|GetAbilitySystemComponent _actor))
  (bind _timeraccum (Variables|Default|GetTimerAccum))
  (bind _returnvalue_1 (+ DeltaTime _timeraccum))
  (bind _actor_1 (Variables|Context|GetActor))
  (bind _targetactor (Variables|Context|GetTargetActor))
  (|SetbIsStaggered (GameplayTags|HasMatchingGameplayTag _returnvalue "(TagName=\"State.Enemy.Staggered\")"))
  (|SetbIsAttacking (GameplayTags|HasMatchingGameplayTag _returnvalue "(TagName=\"State.Enemy.Attacking\")"))
  (Variables|Default|SetTimerAccum _returnvalue_1)
  (if (> _returnvalue_1 0.1)
    (Variables|Default|SetTimerAccum 0.0)
    (Utilities|IsValid _targetactor
      (:"Is Valid"
        (Variables|Default|SetCombatInfo (FPS|Combat|AI|CFLF_GetEnemyCombatInfo _actor_1 _targetactor)))
      (:"Is Not Valid"
        (Variables|Context|SetTargetActor (FPS|Combat|AI|CFLF_FindNearestHostileTarget _actor_1))))))
```
- **变量列表**：
  - `Context` 分类：`TargetActor` (AActor*), `AIController` (AAIController*), `Actor` (AActor*)
  - `Default` 分类：`CombatInfo` (FEnemyCombatInfo), `bIsStaggered` (bool), `bIsAttacking` (bool), `TimerAccum` (float)
- **状态评估**：
  1. `EventTreeStart` 初始化时利用 `CFLF_FindNearestHostileTarget(Context.Actor)` 索敌；
  2. 每帧提取 ASC，实时更新硬直标签 `bIsStaggered` 与攻击标签 `bIsAttacking`；
  3. `TimerAccum` 节流控制在 0.1s (10Hz)：
     - 若 `TargetActor` 有效，解算几何与战斗信息 `CFLF_GetEnemyCombatInfo`；
     - 若 `TargetActor` 无效（如被击杀），触发 BvB 动态转火，重新索敌最近存活目标。

---

### 3.4 `STT_EnemyStagger` (受击硬直任务)
```lisp
(event EventEnterState (Transition)
  (AI|Navigation|StopMovement (Variables|Context|GetAIController))
  (EnemyCombatDirector|Tokens|ReleaseAttackToken (WorldSubsystems|GetEnemyCombatDirector) (Variables|Context|GetActor)))

(event EventTick (DeltaTime)
  (if (not (|GetbIsStaggered))
    (StateTree|FinishTask true)))
```
- **变量列表**：
  - `Context` 分类：`AIController` (AAIController*), `Actor` (AActor*)
  - `Default` 分类：`bIsStaggered` (bool, 绑定自 Evaluator), `TimerAccum` (float)
- **状态评估**：
  1. `EventEnterState`：立即刹车停步 `StopMovement`，并通过裁判系统释放攻击令牌 `ReleaseAttackToken`，绝不霸占攻击席位；
  2. `EventTick`：实时检测 `!bIsStaggered`，硬直一旦结束立刻 `FinishTask(true)`，交出执行权回到状态树根节点。

---

### 3.5 `STT_EnemyAttack` (招式决策与攻击任务)
```lisp
(event EventEnterState (Transition)
  (bind _movetag (Utilities|Struct|BreakEnemyMoveRule (FPS|Combat|AI|CFLF_PickBestEnemyMove (|IF_GetBotMoveset (Variables|Context|GetActor)) (Variables|Default|GetCombatInfo) (Ability|GetAbilitySystemComponent (Variables|Context|GetActor)) (|IF_GetBotCooldownMap (Variables|Context|GetActor)) (Utilities|Time|GetGameTimeInSeconds))))
  (bind _actor (Variables|Context|GetActor))
  (Variables|Default|SetAttackElapsed 0.0)
  (bind _moveset (|IF_GetBotMoveset _actor))
  (bind _cooldownmap (|IF_GetBotCooldownMap _actor))
  (bind (_outselectedmove _returnvalue) (FPS|Combat|AI|CFLF_PickBestEnemyMove _moveset (Variables|Default|GetCombatInfo) (Ability|GetAbilitySystemComponent (Variables|Context|GetActor)) _cooldownmap (Utilities|Time|GetGameTimeInSeconds)))
  (if _returnvalue
    (bind _returnvalue_1 (EnemyCombatDirector|Tokens|TryRequestAttackToken (WorldSubsystems|GetEnemyCombatDirector) (Variables|Context|GetActor) (Utilities|Struct|BreakEnemyMoveRule _outselectedmove)))
    (if _returnvalue_1
      (AI|Navigation|StopMovement (Variables|Context|GetAIController))
      (bind _returnvalue_2 (Abilities|TryActivateAbilitiesByTag (Ability|GetAbilitySystemComponent (Variables|Context|GetActor)) (GameplayTags|MakeGameplayTagContainerFromTag _movetag)))
      (if _returnvalue_2
        (|IF_RecordBotMoveUsed (Variables|Context|GetActor) _movetag (Utilities|Time|GetGameTimeInSeconds))
        (else
          (EnemyCombatDirector|Tokens|ReleaseAttackToken (WorldSubsystems|GetEnemyCombatDirector) (Variables|Context|GetActor))
          (StateTree|FinishTask false)))
      (else
        (StateTree|FinishTask false)))
    (else
      (StateTree|FinishTask false))))

(event EventTick (DeltaTime)
  (bind _attackelapsed (Variables|Default|GetAttackElapsed))
  (bind _returnvalue (+ DeltaTime _attackelapsed))
  (Variables|Default|SetAttackElapsed _returnvalue)
  (if (>= _returnvalue 0.05)
    (if (not (GameplayTags|HasMatchingGameplayTag (Ability|GetAbilitySystemComponent (Variables|Context|GetActor)) "(TagName=\"State.Enemy.Attacking\")"))
      (StateTree|FinishTask true))))

(event EventExitState (Transition)
  (EnemyCombatDirector|Tokens|ReleaseAttackToken (WorldSubsystems|GetEnemyCombatDirector) (Variables|Context|GetActor)))
```
- **变量列表**：
  - `Context` 分类：`AIController` (AAIController*), `Actor` (AActor*)
  - `Default` 分类：`CombatInfo` (FEnemyCombatInfo, 绑定自 Evaluator), `AttackElapsed` (float)
- **状态评估**：
  1. `EventEnterState`：
     - 调用底盘接口获取 Moveset 与 CooldownMap；
     - `CFLF_PickBestEnemyMove` 纯函数选招；
     - 解构获得 `TokenType` 并调用 `TryRequestAttackToken` 抢牌；
     - 抢牌成功则 `StopMovement` 站定，并通过 GAS 激活招式 Tag；
     - 激活成功调用 `IF_RecordBotMoveUsed` 记录冷却戳，失败则立即归还令牌并 `FinishTask(false)`；
  2. `EventTick`：
     - 累加 `AttackElapsed`，0.05s 延时后实时检测 `State.Enemy.Attacking` 标签；
     - 当技能后摇结束移除标签后，立即 `FinishTask(true)`；
  3. `EventExitState`：
     - 退出状态时兜底调用 `ReleaseAttackToken`，杜绝任何外部强制打断引起的令牌泄漏。

---

### 3.6 `STT_EnemyPositioning` (机动走位任务 —— 用户清理重点关注项)
```lisp
(event EventTick (DeltaTime)
  (bind _movestyle (|IF_GetBotMoveStyle (Variables|Context|GetActor)))
  (switch Utilities|FlowControl|Switch|SwitchonEEnemyMoveStyle _movestyle
    (:Skirmish
      (Utilities|IsValid (Variables|Default|GetTargetActor)))))

(event EventEnterState (Transition)
  (Utilities|IsValid (Variables|Default|GetTargetActor)))

(event EventExitState (Transition)
  (AI|ClearFocus (Variables|Context|GetAIController)))
```
- **变量列表**：
  - `Context` 分类：`AIController` (AAIController*), `Actor` (AActor*)
  - `Default` 分类：`targetActor` (AActor*), `bLastShouldChase` (bool), `bNeedInitMove` (bool)
- **【核心差异与待决策分析】**：
  - **现状**：用户在虚幻编辑器中对该蓝图进行了清线简化，目前仅保留了接口获取 `MoveStyle`、`Switch on EEnemyMoveStyle` 的结构骨架，以及退出时的 `ClearFocus`。
  - **编译状态**：0 错误，完全正常。
  - **运行表现**：进入该状态后，由于具体的 `MoveToActor` 与 `CFLF_EvaluateCombatStance` 未连接执行线，Bot 在机动状态下将保持当前位移惯性或站立，随后由 StateTree 的 0.25s 轮询过渡回 `Attack` 状态。
  - **需 Neon 评估决策**：请 Neon 模型评估是否将 3 种机动风格（`Stationary` -> 停脚，`Chase` -> 直奔目标，`Skirmish` -> 迟滞环绕）以最清爽、无面条引脚的方式重新接入，或是否需要新增辅助纯函数以保持蓝图极简。

---

### 3.7 `BP_enemyBase` (Bot 物理底盘)
```lisp
--- Graph: BP_enemyBase:EventGraph ---
(event EventBeginPlay
  (bind _self self)
  (bind _enemymovesetdata (Variables|Default|GetEnemyMovesetData))
  (Ability|GameplayEffect|AddLooseGameplayTags _self (GameplayTags|MakeGameplayTagContainerFromTag "(TagName=\"Team.Enemy\")"))
  (GameplayAbilities|GiveAbility (Variables|GAS|GetAbilitySystemComponent) (|GetGA_class))
  (Utilities|IsValid _enemymovesetdata
    (:"Is Valid"
      (for _array_element (Class|EnemyMovesetData|GetMoves _enemymovesetdata)
        (FPS|Combat|CFLF_GiveAbilityToActor _self (Utilities|Struct|BreakEnemyMoveRule _array_element)))
      (FPS|Combat|CFLF_GiveAbilityToActor _self "/Game/bps/botAI/GA_Enemy_HitReact.GA_Enemy_HitReact_C"))))

(event GAS|Combat|EventOnCharacterDeath (KillerActor)
  (bind _sm_core (|GetSM_core))
  (bind _returnvalue (Transformation|GetWorldLocation _sm_core))
  (EnemyCombatDirector|Tokens|ReleaseAttackToken (WorldSubsystems|GetEnemyCombatDirector) self)
  (Collision|SetCollisionEnabled (Variables|Character|GetCapsuleComponent))
  (Transformation|DetachFromComponent _sm_core "KeepWorld" "KeepWorld" "KeepWorld")
  (Collision|SetCollisionProfileName _sm_core "PhysicsActor")
  (Physics|SetSimulatePhysics _sm_core true)
  (Physics|AddImpulse _sm_core (Math|Vector|vector+vector (Math|Vector|vector*vector (Math|Vector|Normalize (Math|Vector|vector-vector _returnvalue (Transformation|GetActorLocation KillerActor))) 350.0) (Math|Vector|MakeVector 0.0 0.0 500.0)) "None" true)
  (FPS|Combat|CFLF_SpawnScatteredPickupList (Variables|Combat|Grunt|GetLootDrops) _returnvalue)
  (Utilities|FlowControl|Delay 3.0)
  (Actor|DestroyActor))

(event EventIF_RecordBotMoveUsed (MoveTag WorldTime)
  (Utilities|Map|Add (Variables|Default|GetMoveCooldownMap) MoveTag WorldTime))

--- Graph: BP_enemyBase:IF_GetBotMoveset ---
(fn IF_GetBotMoveset () (return (Variables|Default|GetEnemyMovesetData)))

--- Graph: BP_enemyBase:IF_GetBotMoveStyle ---
(fn IF_GetBotMoveStyle () (return (Variables|Default|GetMoveStyle)))

--- Graph: BP_enemyBase:IF_GetBotCooldownMap ---
(fn IF_GetBotCooldownMap () (return (Variables|Default|GetMoveCooldownMap)))
```
- **变量列表**：
  - `AIType` 分类：`EngageRange`, `DisengageRange`, `bIsStationary`, `GA_class`
  - `Default` 分类：`EnemyMovesetData`, `MoveStyle`, `MoveCooldownMap`
- **状态评估**：
  1. `EventBeginPlay` 遍历卡带装填全部招式，额外装填 `GA_Enemy_HitReact`；
  2. `EventOnCharacterDeath` 开头立即调用 `ReleaseAttackToken(self)` 归还令牌，其后原有物理布娃娃冲量、战利品散落、延迟销毁全部保留；
  3. `BPI_BotConfig` 接口 4 大函数完全实现，冷却映射表通过 `EventIF_RecordBotMoveUsed` 无缝更新。

---

### 3.8 `AIC_EnemyBase` (AI 控制器双脑分流)
```lisp
--- Graph: AIC_EnemyBase:EventGraph ---
(event EventOnPossess (PossessedPawn)
  (bind _asbp_enemy_base (Utilities|Casting|CastToBP_enemyBase PossessedPawn))
  (Variables|Default|SetControlledBot _asbp_enemy_base))

(event EventBeginPlay
  (if (|GetbUseStateTreeBrain)
    (AI|Logic|StartLogic (Variables|Default|GetStateTreeAI))
    (else
      (Utilities|Time|SetTimerbyFunctionName 0 "F_EvaluateAI" 0.1 true false 0.1 0.3)
      (Variables|Default|SetCurrentTarget (Game|GetPlayerPawn 0)))))

--- Graph: AIC_EnemyBase:F_EvaluateAI ---
;; 原有旧版大脑逻辑，完整保留无损
```
- **变量列表**：
  - `AI Brain` 分类：`bUseStateTreeBrain` (bool, Instance Editable, 默认 `false`)
  - `Default` 分类：`ControlledBot`, `currentTarget`, `bInCombatStance`, `Is Aim Aligned` 等
- **组件挂载**：
  - `StateTreeAIComponent` (Name: `StateTreeAI`) 已挂载，`stateTreeRef: "/Game/bps/botAI/ST_Enemy_Arena.ST_Enemy_Arena"`, `bStartLogicAutomatically: false`
- **状态评估**：
  - 双脑开关完全生效：勾选 `bUseStateTreeBrain` 启动现代 StateTree；不勾选保持原有 `F_EvaluateAI`，保证项目绝对向后兼容。

---

### 3.9 `ST_Enemy_Arena` (StateTree 资产结构)
- **Evaluator**: `STE_EnemyCombatContext`
- **Root State**:
  - `StateTreeState_1 (Staggered)`:
    - 绑定 Task: `STT_EnemyStagger`
    - EnterCondition: `StateTreeCompareBoolCondition` (监听 Staggered 状态)
    - Transition: `OnStateCompleted` -> `Root`
  - `StateTreeState_0 (Attack)`:
    - 绑定 Task: `STT_EnemyAttack`
    - EnterCondition: `StateTreeCompareBoolCondition` (监听非 Staggered 状态)
    - Transition: `OnStateSucceeded` -> `Root`
    - Transition: `OnStateFailed` -> `State: Positioning`
  - `StateTreeState_2 (Positioning)`:
    - 绑定 Task: `STT_EnemyPositioning`
    - Transition: `OnTick (Staggered == True)` -> `State: Staggered`
    - Transition: `OnTick (延时 0.25s)` -> `State: Attack`

---

## 4. 提交 Neon 模型评估的核心决策点 (Neon Sync Checklist)

1. **`STT_EnemyPositioning` 的引脚布线决策**：
   - 目前处于干净骨架状态。请 Neon 给出对于 `Switch on EEnemyMoveStyle` 下 3 种模式的最优连线方案：
     - **Stationary**：是否直接调用 `StopMovement`？
     - **Chase**：是否直接调用 `MoveToActor(TargetActor, 150.0)`？
     - **Skirmish**：是否仍采用 `CFLF_EvaluateCombatStance` 结合 `bLastShouldChase` 迟滞防抖，或将此逻辑封装为更精炼的纯函数？
2. **`targetActor` 变量命名与来源统一**：
   - 评估器中使用的是 `TargetActor` (Category `Context`)，而任务蓝图中使用的是 `targetActor` (Category `Default`)。是否统一提升为 `Context` 变量绑定，由 StateTree 运行时自动传递？
3. **双脑切换验证方案**：
   - 目前默认 `bUseStateTreeBrain = false`。请 Neon 确认验证步骤（单怪勾选测试 vs 关卡批量验证），以便安全推进实战手感调优。
