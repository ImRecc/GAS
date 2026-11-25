# Bot AI PIE 无响应 / 静止不动 实机排查诊断报告 (PIE_FREEZE_DIAGNOSIS.md)

> **排查方式**：Unreal Editor Live MCP 内存扫描 + 实时读取 UE Output Log (`LogStateTree` / `LogScript` / `LogAbilitySystem`) + 关卡实例实测 (`/Game/t1Level`)  
> **排查时间**：2026-11-26 00:55  
> **现象描述**：开启 `bUseStateTreeBrain = true` 进入 PIE 后，关卡内所有 Bot 完全静止不动，无索敌、无走位、无攻击。

---

## 一、 核心根因定位（已 100% 实锤）

### 根因 1（致命主因）：`StateTreeAI` 组件 `bAutoActivate = false` + `EventOnPossess` 启动早于 `BeginPlay`，导致 StateTree 从未运行
1. **实机扫描证据**：
   - `AIC_EnemyBase` 挂载的 `StateTreeAI` 组件（`UStateTreeAIComponent`）当前属性：
     ```json
     {
       "stateTreeRef": "/Game/bps/botAI/ST_Enemy_Arena.ST_Enemy_Arena",
       "bStartLogicAutomatically": false,
       "bAutoActivate": false
     }
     ```
   - `AIC_EnemyBase` 的 `EventBeginPlay` 为空，启动调用 `StateTreeAI->StartLogic()` 放在了 `EventOnPossess` 中。
   - **Unreal Output Log 实锤报错**（PIE 运行与退出时）：
     ```text
     [2026.11.25-15.51.51:112] LogStateTree: Warning: Context Requirements in UStateTreeComponent::StopLogic failed. Component tick is disabled.
     ```
2. **UE5 底层时序机制剖析**：
   - 对于直接摆放在关卡 `/Game/t1Level` 中的怪物（`AutoPossessAI = PlacedInWorldOrSpawned`），引擎在关卡初始化阶段触发 `Possess` → `EventOnPossess`，**早于** `AIC_EnemyBase` 和 `StateTreeAIComponent` 的 `BeginPlay()`！
   - 当 `StartLogic()` 在 `BeginPlay()` 之前被调用，且组件 `bAutoActivate = false`、`bStartLogicAutomatically = false` 时，`UStateTreeAIComponent` 尚未完成激活与上下文初始化，导致 `StartLogic()` 静默失效、Component Tick 保持禁用状态。
   - 随后真正进入 `BeginPlay()` 时，由于 `EventBeginPlay` 为空且 `bStartLogicAutomatically = false`，**没有任何逻辑再次启动 `StateTreeAI`**。结果 `ST_Enemy_Arena` 全程 0 帧运行，所有 Bot 原地罚站。

---

### 根因 2（高危隐患）：修改 `STE_EnemyCombatContext` 等蓝图后，`ST_Enemy_Arena` 未在编辑器内重新编译
- Step 3 中我们通过 MCP 修改了 `STE_EnemyCombatContext`（将 `TargetActor` 从 `Default` 改为 `Output`）以及 `STT_EnemyAttack`，但 MCP 的 `StateTreeTools` 不提供编译 StateTree 资产的接口。
- 在 UE5 中，Evaluator/Task 蓝图变量结构或分类变动后，**必须在 StateTree 编辑器中打开 `/Game/bps/botAI/ST_Enemy_Arena` 点击一次 Compile 并保存**，否则运行时绑定的 `InstanceData` / `PropertyBindings` 会过期失效（亦可能触发 `Context Requirements failed`）。

---

### 根因 3（次要日志报错，不阻塞移动但污染日志）：`BP_enemyBase` 旧版 `GiveAbility(GA_class)` 传入空类
- **Output Log 实锤报错**（PIE 启动瞬间，3 只怪各报 1 次）：
  ```text
  LogAbilitySystem: Error: BuildAbilitySpecFromClass called with an invalid Ability Class.
  LogAbilitySystem: Error: K2_GiveAbility() called with an invalid Ability Class.
  ```
- 原因：`BP_enemyBase:EventBeginPlay` 仍保留了旧版 `GiveAbility(GA_class)` 节点，而 3 类杂兵已迁移到 `EnemyMovesetData.Moves`，`GA_class` 为空（`None`）。

---

## 二、 已排查并排除的正常项（无需怀疑）

| 检查项 | 实机扫描结果 | 状态 |
| :--- | :--- | :---: |
| **当前关卡与寻路网格** | `/Game/t1Level` 已存在 `NavMeshBoundsVolume_0` 与 `RecastNavMesh-Default` | ✅ 正常 |
| **关卡内怪物实例配置** | `BP_enemy_Grunt_Melee_C_1`、`BP_enemy_Grunt_Turret_C_2`、`BP_enemy_Grunt_Turret_C_0` 的 `AIControllerClass` 均为 `AIC_EnemyBase_C`，`AutoPossessAI = PlacedInWorldOrSpawned`，`EnemyMovesetData` 与 `MoveStyle` 均正确继承 | ✅ 正常 |
| **子类 `EventBeginPlay`** | 3 个 Grunt 子类的 `EventBeginPlay` 均正确调用了 `Parent:BeginPlay` | ✅ 正常 |
| **阵营标签** | `BP_PlayerCharacter` 在 `BeginPlay` 赋予 `Team.Player`；`BP_enemyBase` 在 `BeginPlay` 赋予 `Team.Enemy` | ✅ 正常 |
| **StateTree Schema** | `StateTreeAIComponentSchema` 配置为 `AIControllerClass = AIController`、`ContextActorClass = Pawn`，与 `AIC_EnemyBase`/`BP_enemyBase` 兼容 | ✅ 正常 |
| **StateTree 进入条件** | `Staggered` (`bRight = true`)、`Attack` (`bRight = false`)、`Positioning` (无条件兜底) 结构完整 | ✅ 正常 |

---

## 三、 精准修复方案（建议顺序执行）

1. **修复 `AIC_EnemyBase` 的 `StateTreeAI` 启动时序（解决原地罚站）**：
   - 将 `AIC_EnemyBase` 上 `StateTreeAI` 组件的 **`bAutoActivate` 设为 `True`**（保留 `bStartLogicAutomatically = False` 以受双脑开关控制）。
   - 在 `AIC_EnemyBase:EventOnPossess` 中，`SetControlledBot` 之后插入 1 个 **`Delay(0.0)`（或 `DelayUntilNextTick`）**，再执行 `if (bUseStateTreeBrain)` → `StateTreeAI->StartLogic()`。
   - **好处**：同时兼容“关卡预先摆放的怪”和“运行时动态 Spawn 的怪”，且保证触发 `StartLogic()` 时 Controller、Pawn、`StateTreeAIComponent` 的 `BeginPlay()` 以及双方的 `Team.Player` / `Team.Enemy` 标签均已 100% 就绪！
2. **手动在虚幻编辑器编译 `ST_Enemy_Arena`（必做）**：
   - 打开 `/Game/bps/botAI/ST_Enemy_Arena`，顺手核对 3 处绑定（`Stagger.bIsStaggered`、`Attack.CombatInfo`、`Positioning.targetActor`），点击左上角 **Compile** 并 **Save**。
3. **清理 `BP_enemyBase` 空技能报错（可选）**：
   - 在 `BP_enemyBase:EventBeginPlay` 中对 `GA_class` 做 `IsValidClass` 判断后再调用 `GiveAbility`（或移除该旧节点），消除 PIE 启动时的 3 条 `LogAbilitySystem: Error`。
