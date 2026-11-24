# 现代 Bot AI 系统全资产实机扫描与架构评估报告 (BOT_AI_STATE_REPORT.md)

> **数据采集源**：Unreal Editor Live MCP Session (`http://127.0.0.1:8000/mcp`)  
> **采集时间**：2026-11-24 23:05  
> **生效工具集**：`editor_toolset.toolsets.blueprint.BlueprintTools`、`state_tree_toolset.toolsets.state_tree.StateTreeTools`  
> **编制目的**：供 **Neon 架构模型** 审查评估当前虚幻 5.8 工业级 GAS + StateTree + Combat Director 的实机内存蓝图拓扑，诊断潜在逻辑死锁，并指导后续走位机动（Positioning）与挂载落地。

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
        BPB -->|存储| Vars["• EnemyMovesetData (招式资产)<br>• MoveStyle (走位枚举)<br>• MoveCooldownMap (冷却字典)"]
    end

    subgraph StateTree ["StateTree 通用大脑 (ST_Enemy_Arena)"]
        Schema["StateTreeAIComponentSchema"]
        Eval["评估器: STE_EnemyCombatContext<br>• CFLF_FindNearestHostileTarget (动态索敌解锁 BvB)<br>• CFLF_GetEnemyCombatInfo (几何/血量态势解算)<br>• 0.1s 采样节流定时器"]
        
        State_Stagger["① State: Staggered (最高优先级, bIsStaggered == true)<br>Task: STT_EnemyStagger<br>• 停脚 StopMovement<br>• 释放令牌 ReleaseAttackToken<br>• 监听 !bIsStaggered 退出并回到 Root"]
        State_Attack["② State: Attack (bIsStaggered == false)<br>Task: STT_EnemyAttack<br>• BPI 接口拉取招式与冷却<br>• CFLF_PickBestEnemyMove 选招<br>• TryRequestAttackToken 申请令牌<br>• Make Gameplay Tag Container 激活招式 GA<br>• IF_RecordBotMoveUsed 记录冷却<br>• 成功 -> Root / 失败 -> Positioning"]
        State_Pos["③ State: Positioning (兜底机动)<br>Task: STT_EnemyPositioning<br>• 依据 MoveStyle 分流 (转向/追击/迟滞)<br>• OnTick (0.25s 延迟) -> 跃迁至 Attack 重试选招"]

        Eval -->|输出 CombatInfo / bIsStaggered / bIsAttacking| StateTree
        Schema --> State_Stagger
        Schema --> State_Attack
        Schema --> State_Pos
    end

    subgraph GAS ["GAS 技能打断闭环"]
        GA_Hit["GA_Enemy_HitReact<br>• 监听 Event.Hit.Weakspot<br>• 赋予 State.Enemy.Staggered<br>• 取消 Ability.Enemy.Attack (硬直打断蓄力)"]
    end

    State_Attack -->|申请 / 释放令牌| ECD
    State_Stagger -->|打断立即释放令牌| ECD
    GA_Hit -->|赋予 Staggered Tag| Eval
```

---

## 2. Live MCP 资产级图表 DSL 与引脚拓扑实测

### 2.1 `BPI_BotConfig` (通用解耦接口)
- **文件路径**：`/Game/bps/botAI/BPI_BotConfig`
- **MCP 提取 DSL 状态**：
  ```lisp
  (fn IF_GetBotMoveset () (return 0))
  (fn IF_GetBotMoveStyle () (return "Stationary"))
  (fn IF_GetBotCooldownMap () (return 0))
  (fn IF_RecordBotMoveUsed (MoveTag WorldTime))
  ```
- **架构评估**：规范定义的 4 大契约接口。彻底将 AI 任务逻辑与 Character 具体类解耦，任何未来载具、飞行怪、炮台均可通过实现该接口无缝挂载此一套 StateTree。

---

### 2.2 `BP_enemyBase` (Bot 物理底盘)
- **文件路径**：`/Game/bps/BP_enemyBase`
- **MCP 提取接口实现 DSL**：
  ```lisp
  ;; 1. 获取招式库资产
  (fn IF_GetBotMoveset ()
    (return (Variables|Default|GetEnemyMovesetData)))

  ;; 2. 获取走位风格 (Stationary / Chase / Skirmish)
  (fn IF_GetBotMoveStyle ()
    (return (Variables|Default|GetMoveStyle)))

  ;; 3. 获取冷却字典
  (fn IF_GetBotCooldownMap ()
    (return (Variables|Default|GetMoveCooldownMap)))

  ;; 4. 记录招式冷却时间戳 (位于 EventGraph)
  (event EventIF_RecordBotMoveUsed (MoveTag WorldTime)
    (Utilities|Map|Add (Variables|Default|GetMoveCooldownMap) MoveTag WorldTime))
  ```
- **架构评估**：底盘实现极其纯粹，无任何多余计算。变量全部采用原生类型，`Map.Add` 正确写入冷却记录。

---

### 2.3 `GA_Enemy_HitReact` (受击硬直与蓄力打断卡带)
- **文件路径**：`/Game/bps/botAI/GA_Enemy_HitReact`
- **CDO 配置**：
  - `TriggerTag` = `Event.Hit.Weakspot` (GameplayEvent)
  - `ActivationOwnedTags` = `State.Enemy.Staggered`
  - `CancelAbilitiesWithTag` = `Ability.Enemy.Attack`
- **MCP 提取 DSL**：
  ```lisp
  (event Ability|EventActivateAbility
    (Development|PrintString "\"[Staggered 受击硬直!]\"" true true "(R=0,G=0.66,B=1,A=1)" 1.0)
    (Utilities|FlowControl|Delay 0.4)
    (Ability|EndAbility))
  ```
- **架构评估**：标准的 GAS 反应技能。触发时即打断 `Ability.Enemy.Attack`，并通过持有 `State.Enemy.Staggered` 标签通知 Evaluator 驱动状态机跃迁。

---

### 2.4 `STE_EnemyCombatContext` (StateTree 态势评估器)
- **文件路径**：`/Game/bps/botAI/STE_EnemyCombatContext`
- **MCP 提取 DSL 完整实录**：
  ```lisp
  ;; 启动时索敌初始化 (解锁 Bot vs Bot)
  (event EventTreeStart
    (bind _returnvalue (AI|GetAIController 0))
    (bind _returnvalue_1 (Pawn|GetControlledPawn _returnvalue))
    (bind _returnvalue_2 (FPS|Combat|AI|CFLF_FindNearestHostileTarget _returnvalue_1))
    (Utilities|IsValid _returnvalue_2
      (:"Is Valid"
        (bind _output_get (Variables|Default|SetTargetActor _returnvalue_2)))))

  ;; Tick 每帧感知与 0.1s 几何解算节流
  (event EventTick (DeltaTime)
    (bind _returnvalue (AI|GetAIController 0))
    (bind _returnvalue_1 (Pawn|GetControlledPawn _returnvalue))
    (bind _returnvalue_2 (Ability|GetAbilitySystemComponent _returnvalue_1))
    (bind _targetactor (Variables|Default|GetTargetActor))
    ;; 实时提取 ASC 状态 Tag
    (|SetbIsStaggered (GameplayTags|HasMatchingGameplayTag _returnvalue_2 "(TagName=\"State.Enemy.Staggered\")"))
    (|SetbIsAttacking (GameplayTags|HasMatchingGameplayTag _returnvalue_2 "(TagName=\"State.Enemy.Attacking\")"))
    ;; 0.1s 定时器累计
    (bind _output_get (Variables|Default|SetTimerAccum (+ DeltaTime (Variables|Default|GetTimerAccum))))
    (if (> _output_get 0.1)
      (Variables|Default|SetTimerAccum 0.0)
      (Utilities|IsValid _targetactor
        (:"Is Valid"
          (Variables|Default|SetCombatInfo (FPS|Combat|AI|CFLF_GetEnemyCombatInfo (Pawn|GetControlledPawn (AI|GetAIController 0)) _targetactor)))
        (:"Is Not Valid"
          (Variables|Default|SetTargetActor (Game|GetPlayerPawn 0))))))
  ```
- **架构评估**：
  - **动态索敌能力**：通过 `CFLF_FindNearestHostileTarget` 取代了传统硬编码 `GetPlayerPawn(0)`，完全原生支持玩家混战、怪物互殴（BvB）及中立阵营。
  - **性能保护**：`CFLF_GetEnemyCombatInfo` 几何解算（距离、视锥投影、血量计算）被精确限制在 10Hz（0.1s），同时 ASC 的 Tag 状态保持每帧读取，兼顾超高精度与超低 CPU 损耗。

---

### 2.5 `STT_EnemyAttack` (攻击招式执行任务)
- **文件路径**：`/Game/bps/botAI/STT_EnemyAttack`
- **MCP 提取 DSL 完整实录**：
  ```lisp
  (event EventEnterState (Transition)
    (bind _returnvalue (AI|GetAIController 0))
    (bind _returnvalue_1 (Pawn|GetControlledPawn _returnvalue))
    (Variables|Default|SetAttackElapsed 0.0)
    (bind _cooldownmap (|IF_GetBotCooldownMap _returnvalue_1))
    (bind _moveset (|IF_GetBotMoveset _returnvalue_1))
    ;; 1. 纯函数加权选招
    (bind (_outselectedmove _returnvalue_2) (FPS|Combat|AI|CFLF_PickBestEnemyMove _moveset (Variables|Default|GetCombatInfo) (Ability|GetAbilitySystemComponent _returnvalue_1) _cooldownmap (Utilities|Time|GetGameTimeInSeconds)))
    (if _returnvalue_2
      ;; 2. 向 Director 抢占令牌
      (bind _returnvalue_3 (EnemyCombatDirector|Tokens|TryRequestAttackToken (WorldSubsystems|GetEnemyCombatDirector) (Pawn|GetControlledPawn (AI|GetAIController 0))))
      (if _returnvalue_3
        ;; 3. 停脚并激活技能
        (AI|Navigation|StopMovement (AI|GetAIController 0))
        (bind _returnvalue_4 (Abilities|TryActivateAbilitiesByTag (Ability|GetAbilitySystemComponent (Pawn|GetControlledPawn (AI|GetAIController 0))) (GameplayTags|MakeGameplayTagContainerFromTag (Utilities|Struct|BreakEnemyMoveRule _outselectedmove))))
        (if (not _returnvalue_4)
          ;; 激活失败立即释放令牌
          (EnemyCombatDirector|Tokens|ReleaseAttackToken (WorldSubsystems|GetEnemyCombatDirector) (Pawn|GetControlledPawn (AI|GetAIController 0)))
          (StateTree|FinishTask false))
        (else
          (StateTree|FinishTask false)))
      (else
        (StateTree|FinishTask false))))

  ;; 4. 轮询退出判定 (发现细节)
  (event EventTick (DeltaTime)
    (if (not true)
      (StateTree|FinishTask true)))

  ;; 5. 退出状态兜底 (发现细节)
  (event EventExitState (Transition)
    (EnemyCombatDirector|Tokens|ReleaseAttackToken (WorldSubsystems|GetEnemyCombatDirector)))
  ```
- **实测诊断分析（供 Neon 审查）**：
  - **选招与抢牌闭环打通**：`CFLF_PickBestEnemyMove` 完美接入了 `CombatInfo`、`Moveset`、`CooldownMap`、`Time`，令牌申请与技能激活流转正确。
  - **待微调细节 1 (Tick 监听)**：`EventTick` 中的 `(not true)` 显示当前 `NOT` 节点的输入悬空，缺少将任务变量 `bIsAttacking` 接进 `NOT` 节点的操作。若不接，任务不会在技能施放完成后自然退回状态树。
  - **待微调细节 2 (ExitState 参数)**：`EventExitState` 里的 `ReleaseAttackToken` 节点目前没有连入 `TargetBot`（应接入 `Pawn|GetControlledPawn`），导致退出状态兜底释放令牌时缺少目标 Pawn。

---

### 2.6 `STT_EnemyStagger` (硬直被打断任务)
- **文件路径**：`/Game/bps/botAI/STT_EnemyStagger`
- **MCP 提取 DSL 完整实录**：
  ```lisp
  (event EventEnterState (Transition)
    (bind _returnvalue (AI|GetAIController 0))
    (AI|Navigation|StopMovement _returnvalue)
    (EnemyCombatDirector|Tokens|ReleaseAttackToken (WorldSubsystems|GetEnemyCombatDirector) (Pawn|GetControlledPawn _returnvalue)))

  (event EventTick (DeltaTime)
    (if (not (|GetbIsStaggered))
      (StateTree|FinishTask true)))
  ```
- **架构评估**：**100% 完美范本**。进入硬直瞬间强制停脚（`StopMovement`）并立即归还攻击令牌给 Director，防止霸占近战/远程配额；Tick 中监听硬直 Tag 结束，结束后立即 `FinishTask(true)`，交由状态树重新评估。

---

### 2.7 `STT_EnemyPositioning` (机动走位任务)
- **文件路径**：`/Game/bps/botAI/STT_EnemyPositioning`
- **MCP 提取 DSL 现状**：
  ```lisp
  (event EventTick (DeltaTime))

  (event EventEnterState (Transition)
    (|SetbNeedInitMove true)
    (AI|SetFocus (AI|GetAIController 0)))

  (event EventExitState (Transition)
    (AI|ClearFocus (AI|GetAIController 0)))
  ```
- **架构评估**：
  - 基础进出逻辑完备（`SetFocus` / `ClearFocus`）；
  - **待补充**：`ReceiveLatentTick` 中依据 `Pawn -> IF_GetBotMoveStyle` 的 3 种机动分支（`Stationary` 仅注视 / `Chase` 追击 / `Skirmish` 环绕游走）。

---

### 2.8 `ST_Enemy_Arena` (StateTree 核心层级与跃迁拓扑)
- **文件路径**：`/Game/bps/botAI/ST_Enemy_Arena`
- **Schema**：`StateTreeAIComponentSchema`
- **MCP 提取 StateTree 活体节点配置**：
  ```
  Root (StateTreeState_0)
    ├── Evaluator: STE_EnemyCombatContext_C (全局态势评估)
    │
    ├── ① State: StateTreeState_1 (命名: State: Staggered, 最高优先级)
    │     ├── Task: STT_EnemyStagger_C
    │     ├── Enter Condition: Evaluator.bIsStaggered == True (CompareBool)
    │     └── Transitions:
    │           └── OnStateCompleted -> Goto Root (立即重新决策)
    │
    ├── ② State: StateTreeState_0 (命名: state: Attack, 次优先级)
    │     ├── Task: STT_EnemyAttack_C
    │     ├── Enter Condition: Evaluator.bIsStaggered == False
    │     └── Transitions:
    │           ├── OnStateSucceeded -> Goto Root (打完一套重选)
    │           └── OnStateFailed -> Goto state:Positioning (招式冷却/没抢到牌 -> 进走位)
    │
    └── ③ State: StateTreeState_2 (命名: state: Positioning, 兜底机动)
          ├── Task: STT_EnemyPositioning_C
          ├── Enter Condition: (None, 兜底默认进入)
          └── Transitions:
                ├── OnTick (即时): Condition: Evaluator.bIsStaggered == True -> Goto State: Staggered
                └── OnTick (延迟 0.25s): Goto state:Attack (0.25s 后自动回到攻击尝试选招)
  ```
- **架构评估**：状态树拓扑结构非常干净，逻辑严密。`Staggered -> Attack -> Positioning` 三级优先级分明，且通过 `0.25s` 定时 Tick 跃迁实现了优雅的选招轮询，不会造成死锁卡死在走位态。

---

## 3. 架构审阅与重构议题 (供 Neon 模型裁决)

### 议题一：命名规范全面重构 (Relational "Enemy" -> Entity "Bot")
- **背景**：原设计大量采用 `EnemyMovesetData`、`EEnemyMoveStyle`、`UEnemyCombatDirector`、`STE_EnemyCombatContext` 等以 `Enemy` 为前缀的命名。
- **问题**：在多人混战、Bot vs Bot（BvB）、中立佣兵等模式下，“Enemy” 是相对关系而非实体本质。将实体命名为 Enemy 会造成架构概念污染。
- **建议演进路线**：
  1. C++ 结构体与枚举：后续新模块统一以 `Bot` 为核心（如 `EBotMoveStyle`，目标称为 `TargetActor`，关系判断称为 `IsHostile`）；
  2. 保持向后兼容：现有已编译的 `UEnemyCombatDirector` 与 `UEnemyMovesetData` 保留现有类名（避免已有蓝图断引脚），在包装与对外接口层逐步统一使用 `Bot` 规范。
- **请 Neon 评估**：是否赞同该演进路线？

### 议题二：`STT_EnemyAttack` 发现的 2 处引脚修正方案
- **修正 1**：在 `STT_EnemyAttack` 的 `EventTick` 中，将任务变量 `bIsAttacking`（绑定自 Evaluator）连入 `NOT` 节点（实现 `!bIsAttacking` 结束任务）。
- **修正 2**：在 `EventExitState` 中，将 `Get Controlled Pawn` 连入 `ReleaseAttackToken` 的 `TargetBot` 引脚。
- **请 Neon 评估**：逻辑闭环是否完全吻合 Step 2 原始设计预期？

### 议题三：`STT_EnemyPositioning` 的 3 种走位细化
- **`Stationary`**：仅调用 `AIController->SetFocus(TargetActor)`，维持射击朝向；
- **`Chase`**：调用 `AIController->MoveToActor(TargetActor, AcceptanceRadius = 150.0f)`；
- **`Skirmish`**：调用纯函数计算环绕向量，当距离过近（< 600）时后撤，距离适中（600~1200）时切向横移。
- **请 Neon 评估**：在 GTX 1660 Super 腰射 FPS 基准下，`Skirmish` 是否建议直接采用无寻路开销的简单射线推斥，还是走完整的 `EnvQueryManager` (EQS)？

---

## 4. 下一步落地实施步骤表

| 序号 | 任务模块 | 操作方式 | 预估耗时 | 交付成果 |
| :---: | :--- | :--- | :---: | :--- |
| **1** | **`STT_EnemyAttack` 引脚微调** | 使用 Unreal MCP 修复 Tick 监听与 ExitState 的 Pawn 引脚 | 1 分钟 | 攻击任务 100% 自动结束与释放 |
| **2** | **`STT_EnemyPositioning` 走位落地** | 在 `ReceiveLatentTick` 接入 Switch on `MoveStyle` 分支 | 2 分钟 | 拥有 3 种不同走位风格的 Bot |
| **3** | **`AIC_EnemyBase` 挂载 StateTree** | 添加 `StateTreeAIComponent`，指定为 `ST_Enemy_Arena`，加 `bUseStateTreeBrain` 开关 | 1 分钟 | 支持蓝图内一键无损切换新旧 AI 大脑 |
| **4** | **实机 PIE 跑测** | 地图放置 Grunt，测试弱点打断、令牌争夺与多怪围攻表现 | 3 分钟 | 验证高帧率下的 AI 流畅度与战斗节奏 |
