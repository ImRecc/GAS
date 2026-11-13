# UE5 GAS Project Development Rules & Invariants

## 1. Architectural Integrity & Reuse
- **Faction & Hostility Checks**: Always reuse `CFLF_isHostileTarget` and `CFLF_getActorFactionName` from `FPSAnimFunctionLibrary`. Never introduce redundant or conflicting faction checking functions.
- **Damage Pipeline**: All damage application must flow through `CFLF_ApplyDamageToTarget` using `GE_Damage` with `Data.Damage` SetByCaller magnitude, ensuring consistent combat feedback and death event triggers.
- **Death Handling**: Death must be dispatched via `ADoomBaseCharacter::OnCharacterDeath` directly from `PostGameplayEffectExecute` in `DoomAttributeSet`.

## 2. Blueprint Function Library (C++) Invariants
- **Parameter Defaults & Fallbacks**: Avoid unwired `const FVector&` parameters. Always use value types with default values (`FVector CenterLocation = FVector::ZeroVector`) and include safe fallback logic in C++ (e.g., falling back to `ContextActor->GetActorLocation()` if nearly zero).
- **GAS Const-Correctness**: When routing GAS event instigators (`const AActor*`) to BlueprintImplementableEvents (`AActor*`), perform a safe `const_cast<AActor*>` with null checks.

## 3. Actor Component & Collision Hierarchies
- **Projectile Blueprints**: In any projectile Blueprint using `ProjectileMovementComponent`, the physical collision primitive (e.g. `USphereComponent`) **MUST be the RootComponent** (replace `DefaultSceneRoot`). Ensure `bSimulationGeneratesHitEvents = true` and collision channels are set to Block intended targets (`Pawn`, `WorldStatic`, etc.).
- **Pickup Overlap Safety**: Any pickup actor (`BPA_pickupBase` derivatives) MUST gate overlap events with `Is Player Controlled` (or explicit faction filtering) before applying GameplayEffects to prevent dying enemies from consuming their own drops.

## 4. Weapon & Projectile Design Patterns
- **Tarkov-Style vs Weapon-Driven Damage**: Projectile properties (e.g., `rawDamage`, projectile velocity) should be marked `BlueprintReadWrite, EditAnywhere, meta = (ExposeOnSpawn = true)`. This allows Gameplay Abilities to either override damage at spawn time or leave it disconnected to fall back to the ammunition's native profile.

## 5. Human-Agent Collaborative Workflow (人机协作与分工铁律)

### 5.1 架构清晰时的日常施工 (When Architecture is Clear)
1. **用户主导蓝图骨干搭建 (User Owns Blueprint Skeletons)**:
   - 由用户在 UE 编辑器中亲手创建资产、配置基础标签（Activation/Blocked Tags），搭好宏观执行流程的主干线与 Branch 分支门锁。
   - **禁止行为**：Agent 严禁在未经用户明确授权的情况下，擅自在后台自动生成成片未经商讨的复杂蓝图图表。
2. **Agent 负责数据流拆解与 C++ 封装 (Agent Owns Data Pipeline & C++ Encapsulation)**:
   - Agent 负责分析骨架中空缺的计算与数据处理环节，与用户共同商讨引脚签名（输入、输出、命名）。
   - Agent 负责将复杂的数学、射线检测、距离比对等算法下沉为纯粹、高内聚的 C++ 静态函数（`CFLF_...`）并完成构建。
3. **引导致力与亲手装配 (Guided Hands-on Wiring)**:
   - Agent 提供清晰的引脚对接指引与数据流图，由用户亲手将 C++ 节点接入其搭建的骨干蓝图中，完成验证闭环。

### 5.2 架构不清晰或高度复杂时的决策流程 (When Architecture is Complex / Ambiguous)
1. **主动暂停与推演 (Pause & Clarify)**:
   - 当遇到深层机制选型（如多目标仇恨系统、复杂 GAS 复制与预判时序、宏观 AI 决策架构选择）或设计边界模糊时，严禁边做边猜。
2. **协同评估与外部专家介入 (Argon Architecture Consultation)**:
   - Agent 必须主动指出当前架构的不确定性与风险，主动建议或协助整理技术上下文，引入 **Argon 模型** 进行架构推演与多方案评估。
   - 待宏观架构、类继承关系、职责边界达成共识并形成技术蓝图后，再切回 **5.1** 的人机分工流程进行落地。

