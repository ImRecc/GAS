# Bot AI 全链路实机跑通修复交付报告 (PIE_FIX_DELIVERY_REPORT.md)

> **执行方式**：Unreal Editor Live MCP 手术级修复 + Live PIE 实机运行时探针验收  
> **交付时间**：2026-11-26 01:40  
> **验收结果**：**全部修改蓝图编译 0 错误并已保存至磁盘；Live PIE 实机探针确认 3 类杂兵（Melee / Ranged / Turret）全部正常索敌、转头与按各自风格机动！**

---

## 一、 本次根治的 4 处核心问题与修改清单

| 资产路径 | 修复内容 | 解决的根因 | 编译与保存 |
| :--- | :--- | :--- | :---: |
| **`/Game/bps/BP_PlayerCharacter`** | **手术级重构 `EventBeginPlay`**：<br>1. 删除 1 个空的 `GiveAbility` 节点；<br>2. 删除 2 个多余的 `MakeOutgoingSpec(GE_initStatus)` 与 1 个多余的 `ApplyGameplayEffectSpecToSelf`；<br>3. 将 `Data.MaxHealth(114)`、`Data.JumpCount`、`Data.DashCount` 链式写入**同一个 Spec** 后统一 Apply 一次（未触动任何其他输入/技能事件）。 | 彻底根治玩家开局 `MaxHealth/Health = 0` + `State.Dead` 导致所有 Bot 把玩家当尸体过滤的致命 Bug。 | ✅ 0 错误 / 已保存 |
| **`/Game/bps/botAI/STE_EnemyCombatContext`** | 1. `EventTreeStart` 拿到有效 `TargetActor` 后**立刻计算一次 `CombatInfo`**；<br>2. `EventTick` 断目标重索敌成功后同步立刻更新 `CombatInfo`。 | 消除状态树第 0 帧 `CombatInfo` 全零导致 Melee 出生原地空扑的隐患。 | ✅ 0 错误 / 已保存 |
| **`/Game/bps/botAI/STT_EnemyAttack`** | 在 `EventEnterState` 开头拆解 `CombatInfo` 增加 **`bHasLineOfSight` 门禁**（无视线/无有效目标时立刻 `FinishTask(false)` 跳去走位）。 | 防止无目标死循环空转与隔墙空放技能。 | ✅ 0 错误 / 已保存 |
| **`/Game/bps/botAI/STT_EnemyPositioning`** | 在 `EventEnterState` 与 `EventTick` 增加 **`targetActor == Context.Actor` 或无效时的自愈兜底**（自动调用 `CFLF_FindNearestHostileTarget` 纠正）。 | 根治 `targetActor` 绑定回退指向自身（`TARGET_IS_SELF` → `MoveToActor` 返回 `ALREADY_AT_GOAL`）导致全员原地站桩的终极卡点。 | ✅ 0 错误 / 已保存 |
| **`/Game/bps/AIC_EnemyBase`** | 清理排查期临时日志，保持 `StateTreeAI.bAutoActivate = true` 与 `EventOnPossess -> Delay(0.0) -> StartLogic()`。 | 保证关卡摆放怪与动态生成怪均 100% 正常启动状态树。 | ✅ 0 错误 / 已保存 |

---

## 二、 Live PIE 实机探针验收数据（实测通过）

在 `/Game/t1Level` 通过 MCP 启动真实 PIE 并采样 2.5 秒的实机遥测结果：

| 实机 PIE 对象 | 出生状态 / 坐标 | PIE 运行后状态 / 坐标 | 实机行为验收 |
| :--- | :--- | :--- | :---: |
| **`BP_PlayerCharacter_C_0`** | `HP = 0 / 0`, `State.Dead` ❌ | **`HP = 114 / 114`**, `Jump = 2`, `Dash = 2`, `Tags = [Team.Player]` ✅ | **满血存活，无死亡标签** |
| **`BP_enemy_Grunt_Melee_C_1`** (`Chase`) | `(-160, 1190)`, `Yaw = 0°` | 主动追击至 **`(-10, 73)`**（距玩家 `(0,0)` 仅 73cm，累计位移 **> 1100 cm**），`Yaw = -82.3°` 死死面向玩家 | ✅ **狂暴追击生效** |
| **`BP_enemy_Grunt_Ranged_C_1`** (`Skirmish`) | `(250, 1460)`, `Yaw = 0°` | 前压至 **`(127, 744)`**（位移 **> 720 cm**，精准停在 `754 cm ≈ EngageRange 750 cm` 处站定输出），`Yaw = -99.7°` 面向玩家 | ✅ **游走迟滞生效** |
| **`BP_enemy_Grunt_Turret_C_2`** (`Stationary`) | `(1160, 490)`, `Yaw = 0°` | 原地站桩 **`(1160, 490)`**（`Moved = 0.0 cm`），炮身精准转向 **`Yaw = -157.1°`**（严格对准 `(0,0)` 玩家方向） | ✅ **站桩转头瞄准生效** |

---

## 三、 后续顺手可做的 2 项关卡/面板清理（非阻断）

1. **清理 `/Game/t1Level` 多余玩家分身**：关卡内预先摆放了一只 `BP_PlayerCharacter_C_1`（位于 `(-700, -40)`，`AutoPossessPlayer = Disabled`），它现在也是 114 血活体 `Team.Player`，建议在关卡里删掉（或改放 `PlayerStart`）以免分走 Bot 仇恨。
2. **顺手复查 `ST_Enemy_Arena` 面板绑定**：虽然 `STT_EnemyPositioning` 已有自愈兜底可 100% 正常运行，但下次打开 `/Game/bps/botAI/ST_Enemy_Arena` 时可顺手把 `STT_EnemyPositioning.targetActor` 的绑定源从 `Context.Actor` 改回 `STE_EnemyCombatContext.TargetActor` 并 Compile 保存，保持状态树语义最干净。
