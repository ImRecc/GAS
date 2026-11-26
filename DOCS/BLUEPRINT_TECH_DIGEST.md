# UE5 GAS 项目蓝图与底座技术摸底摘要 (`BLUEPRINT_TECH_DIGEST`)

> **生成方式**：基于磁盘源码/配置只读扫描 + Live Unreal MCP 只读采样（零猜测、零修改）。
> **引擎版本**：Unreal Engine 5.8（`GAS.uproject` 启用 `GameplayAbilities`、`StateTree`、`GameplayStateTree`、`ModelContextProtocol`）。

---

## 一、磁盘底座与配置摸底

### 1.1 文档基准与规范现状
- **`PROJECT_MAP.md`**：记录了 1080p @ 60–120 FPS（目标 GTX 1660 Super 6GB）纯腰射 FPS 架构、C++ `UFPSAnimFunctionLibrary` 索引与跨模块契约。**注意**：文档中部分描述已滞后于实装（例如 `PROJECT_MAP.md` 写轮盘主控件为 `W_weaponWheel_v2`，而 `bp_playerController` 实际加载的是 `W_WeaponWheel_4Slot`）。
- **`PROJECT_CONSTITUTION.md`**：首行已被人工批注“目前看来毫无意义，且副作用大过期望太多，不要再遵循它”，实际人机协作应以 `GEMINI.md` 为准。

### 1.2 C++ 源码与模块依赖（`Source/GAS/`）
- **`GAS.Build.cs`**：`PublicDependencyModuleNames` 包含 `Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput`, `GameplayAbilities`, `GameplayTags`, `GameplayTasks`, `DeveloperSettings`, `AIModule`；`PrivateDependencyModuleNames` 为空。
- **`DoomBaseCharacter` (`.h/.cpp`)**：继承 `ACharacter` + `IAbilitySystemInterface`，内置 `AbilitySystemComponent` 与 `UDoomAttributeSet`。`PossessedBy` / `BeginPlay` 绑定 `DashCount` 属性变化监听（`DashRechargeInterval = 1.4f` 串行单次 `FTimerHandle` 充能，非循环 Tick），并自动 `GiveAbility(DefaultAttackAbility)`；`Landed()` 发送 `State.Movement.Landed` GameplayEvent；暴露 `bIsDead`、`LootDrops` 与 `OnCharacterDeath(AActor* KillerActor)`。
- **`DoomAttributeSet` (`.h/.cpp`)**：管理 6 项核心属性（`Health=100`, `MaxHealth=100`, `JumpCount=2`, `MaxJumpCount=2`, `DashCount=2`, `MaxDashCount=2`）。`PostGameplayEffectExecute` 钳制 `Health <= 0` 时发送 `State.Dead` GameplayEvent、置 `bIsDead = true`、调用 `OnCharacterDeath` 并挂载松散标签 `State.Dead`。
- **`EnemyMovesetData` (`.h/.cpp`)**：定义 `EEnemyTokenType`（None/Melee/Ranged）、`EEnemyMoveStyle`（Stationary/Chase/Skirmish/CoverSkirmish）、`FEnemyMoveRule`、`FEnemyCombatInfo`、`UEnemyMovesetData`。
- **`EnemyCombatDirector` (`.h/.cpp`)**：`UWorldSubsystem` + `UEnemyCombatSettings`（`UDeveloperSettings`）。维护近战/远程攻击令牌池（默认各 2）、6s 超时防死锁回收、玩家视锥半角（45°）视野优先抢占、`EQSLastQueryTimeMap`（`TWeakObjectPtr<AActor>` 防泄漏）跨状态 1.2s EQS 冷却锁。
- **`BotEQSContexts` (`.h/.cpp`)**：`UBotEQSContext_Target`（优先 `GetFocusActor()`，兜底 `CFLF_FindNearestHostileTarget`）、`UBotEQSContext_Allies`（遍历活体 `ADoomBaseCharacter` 过滤同阵营）、`UBotEQSBlueprintLibrary`（`CFLF_RunBotTacticalEQS` 异步执行 + 主副查询回退 + `CanBotAcceptEQSMove` 防攻击/硬直打断；`CFLF_ShouldCoverBotPeek` 探头判定）。
- **`FPSAnimFunctionLibrary`（`.h` + `_Arms/_Combat/_AI/_UI.cpp`）**：
  - **Arms**：`CFLF_SolveHandSwayV2`（30fps 下限步长钳制 + 独立落地临界阻尼短弹簧）、`CFLF_SolveRecoilSpringV2`（闭式控制论二阶弹簧），另有 4 个已标记 `DeprecatedFunction` 的 1.0 老函数。
  - **Combat**：`CFLF_ApplyDamageToTarget`（**硬编码 Fallback 加载 `/Game/bps/gameplayEffect-Ability/GE_Damage.GE_Damage_C`**，`Data.Damage` 负值扣血 + 可选 `HitEventTag`）、`CFLF_GiveAbilityToActor`、`CFLF_SphereDamageHostiles`、`CFLF_ExplodeRadialDamage`、`CFLF_StepSpringRecoil`。
  - **AI**：`CFLF_CheckAimAndLineOfSight`、`CFLF_EvaluateCombatStance`、`CFLF_GetEnemyCombatInfo`、`CFLF_PickBestEnemyMove`、`CFLF_FindNearestHostileTarget`（全图 `TActorIterator<APawn>` 遍历）。
  - **UI/VFX**：`CFLF_CalculateRadialSector`、`CFLF_GetUnscaledDeltaTime`、`CFLF_SpawnScatteredPickups`、`CFLF_SpawnScatteredPickupList`、`CFLF_SpawnImpactFlash`（`NewObject<UPointLightComponent>` 挂在 `WorldSettings` 并用单次 Timer 销毁）。

### 1.3 `Config/DefaultEngine.ini` 与 `Config/DefaultGameplayTags.ini`
- **渲染与抗锯齿（`DefaultEngine.ini`）**：
  - RHI 锁定 `DefaultGraphicsRHI_DX12`，目标着色器 `+D3D12TargetedShaderFormats=PCD3D_SM6`（`+D3D11TargetedShaderFormats=PCD3D_SM5`）。
  - `r.AllowStaticLighting=False`、`r.DynamicGlobalIlluminationMethod=1`（Lumen）、`r.ReflectionMethod=2`（HitLighting/Lumen 反射配置）、`r.RayTracing=True`、`r.RayTracing.RayTracingProxies.ProjectEnabled=True`、`r.Substrate=True`（Substrate 材质开启，GBufferFormat=0）、`r.Shadow.Virtual.Enable=1`（VSM）、`r.SSR.Quality=4` / `r.SSR.Temporal=1` / `r.SSR.MaxRoughness=0.8`、`r.AllowGlobalClipPlane=True`。
  - **抗锯齿**：`DefaultEngine.ini` 未显式覆盖 `r.AntiAliasingMethod`，沿用 UE 5.8 默认设置（TSR）。
  - **注意**：对标 GTX 1660 Super (6GB) 1080p 60–120 FPS 目标而言，当前 `DefaultEngine.ini` 同时开启了 Lumen + RayTracing + Substrate + VSM + SSR Quality 4，默认画质开销极高。
- **碰撞通道（`DefaultEngine.ini`）**：无 `[/Script/Engine.CollisionProfile]` 自定义通道配置，全部使用引擎默认通道（如 `ECC_Visibility`、`ECC_Pawn`、`ECC_PhysicsBody`）。
- **GameplayTags（`DefaultGameplayTags.ini`）**：共注册 42 个标签，分为 `Ability.*`（14 个，含移动、开火、姿态与 4 个精英怪招式）、`Cooldown.Enemy.Attack`、`Data.*`（6 个 SetByCaller 数值标签）、`Event.Hit.Weakspot`、`Input.PrimaryAction(.stop)`、`State.*`（11 个状态标签）、`Team.Enemy` / `Team.Player`。**注意**：`PROJECT_MAP.md` 中提到的 `State.Blocked.Fire` 当前并未在 `DefaultGameplayTags.ini` 中注册。

---

## 二、核心蓝图 Live MCP 只读审查

### 1. `BP_PlayerCharacter`（`/Game/bps/BP_PlayerCharacter`，父类：`ADoomBaseCharacter`）
1. **Tick 与高频定时器**
   - `ReceiveTick` 未实现（`bIsImplemented: false`）。
   - `EventBeginPlay` 注册了一个 **0.5s 循环定时器** `SetTimerbyFunctionName("F_PrintPlayerHP", 0.5, true)`，每 0.5 秒向屏幕打印玩家血量调试字串。
2. **耦合与引用**
   - **Cast To**：`EventBeginPlay` 中 `CastTobp_playerController`（用于获取 `EnhancedInputLocalPlayerSubsystem` 挂载 `IMC_player`）；`IF_getEquippedWeapon` 中 `CastToBPCA_weaponBase`。
   - **直接硬引用**：依赖表包含 8 个具体 GA/GE 类（`GE_initStatus`、`GA_dash`、`GA_passive_dashRecharge`、`GA_jumping`、`GA_crouch`、`GA_crouchSlide`、`GA_vault`、`GA_wallJump`、`GA_wallSliding`）、具体骨架与动画蓝图（`SK_Mannequin_Arms`、`ABP_arms`）、具体震屏（`CS_LandImpact`）、`customFL`、`BPAC_propManager`、`BPA_propBase`、`BPCA_weaponBase`、`bp_playerController` 及 10 个 `IA_*` / `IMC_player`。
3. **通信方式**
   - **BPI**：实现 `BPI_comWithProps`（`IF_getEquippedProp`、`IF_getAimDirection`、`IF_getFirstpersonMesh`、`IF_getPropIK`、`IF_getEquippedWeapon`、`IF_getRecoilContx`）；通过 `BPI_playerUI` 调用 Controller（`IF_playCameraShake`、`IF_toggleWeaponWheel`、`IF_UpdateWheelInput`、`IF_ScrollWheel`）。
   - **Event Dispatcher**：无自定义分发器。
   - **GameplayTag 读写**：`BeginPlay` 写入松散标签 `Team.Player`；`EventOnLanded` 发送 `State.Movement.Landed`；通过 `TryActivateAbilitiesByTag` / `SendGameplayEventToActor` 驱动跳跃、冲刺、滑铲、蹲伏、翻墙、开火等技能；`BeginPlay` 使用 `Data.MaxHealth`（硬编码 `114.0`）、`Data.JumpCount`、`Data.DashCount`。
4. **生命周期与清理**
   - `OnCharacterDeath` 仅检查 `bWheelActive`：若为真则置 `false` 并调用 `IF_toggleWeaponWheel(false)` 兜底关闭轮盘。
   - **未清理项**：未在死亡或 `EndPlay` 时清除 `F_PrintPlayerHP` 循环定时器，未移除 `IMC_player` 输入映射或屏蔽输入，未显式重置全局时间膨胀或清理已生成的武器 Actor。
5. **可疑坏味道**
   - **调试残余**：`BeginPlay` 永久开启 0.5s 循环 `PrintString("Player HP: ...")`。
   - **硬编码数值**：`GE_initStatus` 的 `Data.MaxHealth` 在蓝图里硬编码为 `114.0`。
   - **冗余与空实现风险**：`IF_getPropIK` 中 `TransformToBoneSpace` 与 `GetSocketTransform` 节点存在重复计算连线；`OnLanded` 在 C++ `ADoomBaseCharacter::Landed` 已发送一次 `State.Movement.Landed`，蓝图 `EventOnLanded` 又发送了一次（**重复发送事件**）。
   - **硬引用扩展瓶颈**：直接引用 `SK_Mannequin_Arms` 与 `ABP_arms`，且依赖 `BPCA_weaponBase` 具体子类，未来替换正式第一人称美术资产时需同步修改角色蓝图。
6. **数据流**
   - **输入流入**：`IMC_player` 经 Enhanced Input 触发各 `IA_*` 事件；`IA_Move` 驱动 `AddMovementInput`；`IA_Look` 经 `bWheelActive` 分支，False 时直接驱动 Controller Yaw/Pitch，True 时乘以 `wheelSelectorSens` 经 `IF_UpdateWheelInput` 推入轮盘；`IA_selector` / `IA_WheelScroll` 控制轮盘显隐与滚动；`IA_equipPropSlots` 直接调用 `BPAC_propManager.F_equipProp(Truncate(Value) - 1)`；`IA_fire` / 机动输入转为 `TryActivateAbilitiesByTag` / `SendGameplayEventToActor` 进入 ASC；`IF_getRecoilContx` 将后坐力上下文转发给 `SKM_arms` 的 `ABP_arms`（`IF_applyRecoil`）。

---

### 2. `bp_playerController`（`/Game/inputs/bp_playerController`，父类：`APlayerController`）
1. **Tick 与高频定时器**
   - `EventGraph` 中存在空 `EventTick` 节点（`ReceiveTick` 为 `true`，但无后续连线）。无定时器。
2. **耦合与引用**
   - **Cast To**：无 `Cast To` 节点。
   - **直接硬引用**：`BeginPlay` 通过 `CreateWidget` 直接硬引用具体 UMG 类 `W_WeaponWheel_4Slot` 与 `W_ScreenFeedback`；实现接口 `BPI_playerUI`。
3. **通信方式**
   - **BPI**：实现 `BPI_playerUI` 全部 6 个事件：`IF_toggleWeaponWheel`、`IF_UpdateWheelInput`、`IF_ScrollWheel`、`IF_TriggerDamageFlash`、`IF_playCameraShake`、`IF_TriggerSpeedLines`。
   - **Event Dispatcher**：无。
   - **GameplayTag 读写**：无直接读写。
4. **生命周期与清理**
   - `ReceiveEndPlay`、`ReceiveUnPossess`、`ReceiveDestroyed` 均未实现。切图或销毁时未调用 `RemoveFromParent` 清理 `weaponWheelWidget` 与 `screenFeedback`。
5. **可疑坏味道**
   - **实锤接线 Bug**：`EventIF_TriggerDamageFlash` 错误地调用了 `WScreenFeedback::TriggerSpeedLines` 而非 `TriggerDamageFlash`，导致受击红光暗角实际上会触发冲刺风线。
   - **遗留调试输出**：`IF_playCameraShake` 每次震屏都会执行 `PrintString(GetClassDisplayName(shakeClass))`。
   - **版本与命名漂移**：实际创建的是 4 槽轮盘 `W_WeaponWheel_4Slot`（与 `PROJECT_MAP.md` 描述的 8 扇区 `W_weaponWheel_v2` 不一致），未来加到 8 把武器时此处硬编码的类引用与函数调用（`OpenWheel` / `CloseWheel` / `UpdateWheelInput`）必须整体替换或抽象为接口。
   - **空 Tick 节点**：保留未连接的 `EventTick`。
6. **数据流**
   - **初始化**：`BeginPlay` 创建 `W_WeaponWheel_4Slot`（ZOrder=10，初始 Collapsed）与 `W_ScreenFeedback`（ZOrder=-1）并加入视口。
   - **运行时**：接收来自角色/技能通过 `BPI_playerUI` 发来的指令，直接路由给持有的两个常驻 Widget 实例或乘以 `globalCameraShakeMultiplier` 后调用 `ClientStartCameraShake`。

---

### 3. `BPAC_propManager`（`/Game/bps/gameplayEffect-Ability/equipmentSys/props/utilities/BPAC_propManager`，父类：`UActorComponent`）
> **注**：Content 中存在两个同名 `ObjectRedirector`（位于 `equipmentSys/` 与 `equipmentSys/props/`），真实资产位于 `props/utilities/`。
1. **Tick 与高频定时器**
   - `ReceiveTick` 标志为 `true`，`EventGraph` 中存在空的 `EventTick` 节点；无定时器。
2. **耦合与引用**
   - **Cast To**：`EventBeginPlay` 中对 `GetOwner()` 执行 `CastToCharacter` 并缓存为 `As Character`。
   - **直接硬引用**：依赖包含 `BPA_propBase`、`BPI_comWithProps`，以及 **2 个具体武器子类** `BPCA_gatlingGun` 与 `BPCA_testWeapon`（来自 `StartingLoadout` 默认配置）。
3. **通信方式**
   - **BPI**：通过 `BPI_comWithProps` 向 `As Character` 调用 `IF_getFirstpersonMesh` 获取手部网格体以挂载武器。
   - **Event Dispatcher**：无（`PROJECT_MAP.md` 中提及的 `OnWeaponFired` 广播并不在此组件上）。
   - **GameplayTag 读写**：在 `F_equipProp` 中读写旧/新武器 `BPA_propBase::PropStanceTag`，通过 `RemoveLooseGameplayTags` / `AddLooseGameplayTags` 同步给角色 ASC。
4. **生命周期与清理**
   - `ReceiveEndPlay` 未实现。当宿主角色死亡或销毁时，**不会销毁 `PropInventory` 中已 `SpawnActor` 出来的武器实例**，也不会清理 `ActivateAbilityHandle` 授予的技能或移除残留的 `PropStanceTag`。
5. **可疑坏味道**
   - **严重逻辑漏洞（`F_equipProp`）**：在 `IsValid(GetGameplayAbilityFromSpecHandle(ActivateAbilityHandle))` 的 `Is Not Valid` 分支完全留空！这意味着首次装备（`BeginPlay` 调用 `F_equipProp(0)` 时 `ActivateAbilityHandle` 尚无效）或当前武器未授予技能时，会直接跳过显隐切换、技能授予与姿态标签挂载。
   - **重复求值与调试残余**：`F_equipProp` 中两次对 `CurrentPropIndex` 赋值且先取数组元素再判空；残留 `PrintString("clearOld")`；在 `AddLooseGameplayTags` 传参用 `GetOwner()` 而 `RemoveLooseGameplayTags` 却用 `As Character`（混用引用）。
   - **武器扩展瓶颈**：`StartingLoadout` 直接硬引用具体武器类（`BPCA_gatlingGun`、`BPCA_testWeapon`），且未做空槽/越界拦截。加 8 把武器时，需将 `StartingLoadout` 迁移为 DataAsset/PrimaryAsset 或软引用，并修复 `F_equipProp` 初次装备与边界校验。
6. **数据流**
   - `BeginPlay` 遍历 `StartingLoadout` 生成武器 Actor -> 存入 `PropInventory` -> 调用角色 `IF_getFirstpersonMesh` 将武器 Attach 到 `def_attachSocketName` 并全部隐藏 -> 调用 `F_equipProp(0)`（当前逻辑存在上述分支缺失）。切枪时由外部传入 `newIndex`，清理旧技能与姿态标签、隐藏旧武器、显示新武器并 `GiveAbility(GrantedAbility)` + 挂载新 `PropStanceTag`。

---

### 4. `BP_enemyBase`（`/Game/bps/BP_enemyBase`，父类：`ADoomBaseCharacter`）
1. **Tick 与高频定时器**
   - `ReceiveTick` 为 `true`，`EventGraph` 中存在空 `EventTick`、`EventHit`、`EventActorBeginOverlap` 占位节点；无定时器。
2. **耦合与引用**
   - **Cast To**：无显式 `Cast To`。
   - **直接硬引用**：`GA_Enemy_HitReact`、`AIC_EnemyBase`、`BPI_BotConfig`、`SM_Sequencer_Key`（引擎 Gizmo 占位网格体？）、`UEnemyMovesetData` 成员变量（`EnemyMovesetData`）。
3. **通信方式**
   - **BPI**：实现 `BPI_BotConfig` 全部 4 个接口：`IF_GetBotMoveset`、`IF_GetBotMoveStyle`、`IF_GetBotCooldownMap`、`IF_RecordBotMoveUsed`。
   - **Event Dispatcher**：无。
   - **GameplayTag 读写**：`BeginPlay` 挂载 `Team.Enemy`；`MoveCooldownMap` 以 `FGameplayTag` 为 Key 记录招式时间戳。
4. **生命周期与清理**
   - `OnCharacterDeath` 实现了完整且清晰的清理链路：
     1. 调用 `UEnemyCombatDirector::ReleaseAttackToken(self)` 归还进攻令牌；
     2. 关闭胶囊体碰撞（`SetCollisionEnabled`）；
     3. 将核心网格 `SM_core` 分离并切换为 `PhysicsActor` 开启物理模拟，沿杀手反方向+向上（350 / Z+500）施加保龄球击飞冲量；
     4. 调用 `CFLF_SpawnScatteredPickupList(LootDrops, ...)` 抛洒掉落物；
     5. `Delay(3.0)` 后 `DestroyActor`。
5. **可疑坏味道**
   - **新旧并存与占位资产**：保留了老版本遗留变量 `EngageRange`、`DisengageRange`、`bIsStationary`、`GA_class`（`BeginPlay` 仍会赋予 `GA_class`），同时又走新表 `EnemyMovesetData`；依赖引擎内部路径 `/Engine/VREditor/TransformGizmo/SM_Sequencer_Key` 作为占位模型（换正式美术时需替换）。
   - **死亡击飞空指针风险**：`OnCharacterDeath` 中直接用 `GetActorLocation(KillerActor)` 计算击飞方向，若 `KillerActor` 为空（如环境伤害/Dot/自杀）或已被销毁，结果指向世界原点 `(0,0,0)`。
   - **空事件节点**：保留未使用的 `EventTick`、`EventHit`、`EventActorBeginOverlap`。
6. **数据流**
   - `BeginPlay` 挂载 `Team.Enemy` -> 赋予老 `GA_class` -> 遍历 `EnemyMovesetData->Moves` 批量调用 `CFLF_GiveAbilityToActor` 赋予招式技能 + 赋予 `GA_Enemy_HitReact`。战斗中通过 `BPI_BotConfig` 向 `ST_Enemy_Arena` 提供招式表、走位风格与 `MoveCooldownMap` 读写。死亡时由 C++ `DoomAttributeSet` 触发 `OnCharacterDeath` 完成令牌释放、物理抛飞与掉落物生成。

---

### 5. `AIC_EnemyBase`（`/Game/bps/AIC_EnemyBase`，父类：`AAIController`）
1. **Tick 与高频定时器**
   - `ReceiveTick` 为 `true`，`EventGraph` 中存在空 `EventTick`。
   - 当 `bUseStateTreeBrain == false` 时，`EventOnPossess` 启动 **0.1s 循环定时器** `SetTimerbyFunctionName("F_EvaluateAI", 0.1, true)`；当 `bUseStateTreeBrain == true` 时不启动该定时器，改由 StateTree 组件驱动。
2. **耦合与引用**
   - **Cast To**：`EventOnPossess` 中 `CastToBP_enemyBase(PossessedPawn)` 并缓存为 `ControlledBot`（若未来有非 `BP_enemyBase` 的敌对 Pawn 会导致 `ControlledBot` 为空）。
   - **直接硬引用**：硬引用 `BP_enemyBase` 与 `ST_Enemy_Arena`。
3. **通信方式**
   - **BPI**：未实现任何 BPI。
   - **Event Dispatcher**：无。
   - **GameplayTag 读写**：老分支 `F_EvaluateAI` 中读取 `ControlledBot` ASC 的 `State.Enemy.Attacking` 标签，并尝试激活 `Ability.Enemy.Attack`。
4. **生命周期与清理**
   - `ReceiveUnPossess`、`ReceiveEndPlay` 均未实现。若切换至老大脑模式，UnPossess 或销毁时不会主动清理 `F_EvaluateAI` 定时器（依赖 Actor 销毁自动回收，但 UnPossess 换体时会继续空跑并触发 `Is Not Valid -> StopMovement`）。
5. **可疑坏味道**
   - **老大脑残留与硬编码**：`F_EvaluateAI` 及其内部复合节点 `老黄历`（空图）仍保留在控制器中，且老分支 `EventOnPossess` 硬编码 `GetPlayerPawn(0)` 作为目标；`F_EvaluateAI` 中 `SetActorRotation` 无 Target 连线（默认作用于 Controller 自身而非 Pawn），且把 `SetActorRotation` 的 bool 返回值（实际不存在，DSL 显示为 `SetIsAimAligned`）错误回写给 `IsAimAligned`。
   - **Posssess 时序隐患**：`EventOnPossess` 在 `Delay(0.0)` 下一帧才启动 StateTree 或定时器，若 Pawn 在同帧内死亡或被 UnPossess，缺乏有效性二次校验。
6. **数据流**
   - `EventOnPossess` -> `CastToBP_enemyBase` -> `Delay(0.0)` -> 按 `bUseStateTreeBrain` 分流：新模式调用 `StateTreeAI->StartLogic()`（加载 `ST_Enemy_Arena`）；老模式每 0.1s 跑 `F_EvaluateAI`（`CFLF_CheckAimAndLineOfSight` + `CFLF_EvaluateCombatStance` + `MoveToActor` / `TryActivateAbilitiesByTag`）。

---

### 6. `ST_Enemy_Arena`（`/Game/bps/botAI/ST_Enemy_Arena`，类型：`UStateTree`）
1. **Tick 与高频定时器**
   - 全局 Evaluator：`STE_EnemyCombatContext`（随 StateTree Tick 持续采集态势）。
   - `state:Positioning` 挂载两条 `OnTick` Transition：一条按条件检测 `Is Staggered == true`；另一条无条件 **0.25s 延迟跳转**（`bDelayTransition=true, delayDuration=0.25`）至 `state:Attack`。
2. **耦合与引用**
   - 依赖 4 个 StateTree 蓝图节点资产：`STE_EnemyCombatContext`、`STT_EnemyAttack`、`STT_EnemyPositioning`、`STT_EnemyStagger`（通过 `BPI_BotConfig` 与 `UEnemyCombatDirector` 解耦具体怪物蓝图）。
3. **通信方式**
   - 状态树内部通过 `STE_EnemyCombatContext` 暴露的上下文数据（如 `Is Staggered`）驱动 `StateTreeCompareBoolCondition` 进行状态进入与切换。
4. **生命周期与清理**
   - 自身为只读资产，生命周期由 `AIC_EnemyBase` 的 `StateTreeAIComponent` 托管；状态退出/打断清理职责下沉在各个 `STT_*` Task 内部。
5. **可疑坏味道**
   - **硬直打断非对称**：`state:Positioning` 配有 `OnTick -> State: Staggered` 的即时抢占转换，但 `state:Attack` 下 **没有配置 `OnTick -> State: Staggered` 的 Transition**（仅在 `Root` 下的 `State: Staggered` 依靠 `OnStateSucceeded/Failed` 回 Root 后才能进入硬直，意味着攻击期间若被打出硬直，需依赖 `STT_EnemyAttack` 内部主动结束/失败才能切入硬直态）。
   - **固定 0.25s 轮询节拍**：`state:Positioning -> state:Attack` 采用固定 0.25s `OnTick` 延迟重入，若大量远程/近战怪同时在场上走位，每 0.25s 都会尝试进入 `state:Attack` 触发一次抽签与令牌探测（目前依赖 C++ 1.2s EQS 冷却锁兜底才未卡帧）。
6. **数据流**
   - `STE_EnemyCombatContext` 持续更新战斗态势与 `Is Staggered` -> `Root` 依次评估子状态：若 `Is Staggered == true` 进入 `State: Staggered`（`STT_EnemyStagger`，完成后回 `Root`）；否则进入 `state:Attack`（`STT_EnemyAttack`，成功回 `Root`，失败转 `state:Positioning`）；在 `state:Positioning`（`STT_EnemyPositioning`）期间每 0.25s 尝试切回 `state:Attack` 或在硬直时切向 `State: Staggered`。

---

### 7. `ABP_arms`（`/Game/FirstPersonArms/customAnim/ABP_arms`，父类：`UAnimInstance`）
1. **Tick 与高频定时器**
   - `EventBlueprintUpdateAnimation` 每帧执行：校验 `TryGetPawnOwner()` -> `gasWay2Stance` -> `propIKTransform` -> `F_calHandSwayV2`（每帧调用 `CFLF_SolveHandSwayV2` 与 `CFLF_SolveRecoilSpringV2`）。
2. **耦合与引用**
   - **Cast To**：无显式 `Cast To`，但依赖表仍硬引用了 `BP_PlayerCharacter`、`BPA_propBase`、`BPCA_weaponBase`（存在旧变量 `As BP Player Character`）。
   - **直接硬引用**：硬引用具体骨骼 `SK_Mannequin_Arms_Skeleton` 与具体动画序列 `fp_AAMG_idle`、`fp_hg_idle1`、`FP_Falling`（来自 `weaponAnim_AAMG`、`weaponAnim_HHG` 等原型目录）。
3. **通信方式**
   - **BPI**：实现 `BPI_comWithProps::IF_applyRecoil`（写入 `Recoil Context` 并置位 `bTriggerRecoilPulse = true`）；内部实现其余 `BPI_comWithProps` 函数（返回空值占位）并通过 `TryGetPawnOwner()` 调用角色身上的 `IF_getEquippedProp` 与 `IF_getPropIK`。
   - **GameplayTag 读写**：`gasWay2Stance` 复合节点读取 Pawn 的 `AbilitySystemComponent`，通过两次 `HasMatchingGameplayTag` 分支切换 `En_animState`（`En_animStanceState` 枚举，如步枪/手枪姿态）。
4. **生命周期与清理**
   - 当 `IF_getEquippedProp` 返回无效时（空手或武器销毁），`F_calHandSwayV2` 会重置 `HandRSwayRot/Trans` 并调用 `ResetVectorSpringState` 复位 3 个弹簧状态，防止切枪/死亡瞬间弹簧残留。
5. **可疑坏味道**
   - **旧资产与废代码残留**：仍保留 1.0 时代变量（`RotSpringState` 等共用外，还有 `oldHandSway` 分类变量 `HandRSwayRot/Trans`）、空的 `F_calHandSway` 函数、`EventGraph` 中未删除的空复合节点 `smoothRecoil`、以及 `if (NeedIK?)` 后悬空未连线的分支。
   - **扩展 8 把武器/换美术瓶颈**：
     - `AnimGraph` 中通过 `BlendPoses(En_animStanceState)` 硬编码了 `SM_GPMG_main`（机枪）与 `SM_HHG_main`（手枪）两个独立状态机，并直接绑定了 `fp_AAMG_idle` / `fp_hg_idle1`。每增加一种新握姿武器就得改 `En_animStanceState` 枚举并新增整套 StateMachine，无法通过数据驱动（如 Linked Anim Layer 或单状态机+序列资产变量替换）扩展 8 把武器。
     - `F_calHandSwayV2` 中大量物理参数通过 `ToolMenus|Get` 节点（Property Access/结构体拆包）从 `currentprop` 与 `Recoil Context` 读取，若未在 `BPA_propBase` 统一默认值，新武器容易出现零值无摇晃。
6. **数据流**
   - **姿态与 IK**：`TryGetPawnOwner` -> `gasWay2Stance` 查 ASC 标签写入 `En_animState` -> `AnimGraph` 驱动 `BlendPoses(En_animStanceState)` 混合 `SM_GPMG_main` / `SM_HHG_main` 与 `FP_Falling`；`propIKTransform` 调用 `IF_getPropIK` 写入 `Weapon Socket IK`。
   - **程序化惯性与后坐力**：`F_calHandSwayV2` 采集 `UnrotateVector(Velocity, ActorRot)`、`BaseAimRotation`、`IsFalling` 及当前武器参数 -> `CFLF_SolveHandSwayV2` 解算 `handSwayRotator` / `handSwayTranslation`；开火时 `IF_applyRecoil` 注入 `Recoil Context` 与脉冲标志 -> `CFLF_SolveRecoilSpringV2` 解算 `CurrentRecoilRot` / `CurrentRecoilTrans` -> `AnimGraph` 通过 `Transform(Modify)Bone`（`spine_05`、`spine_04`、`hand_r`）与 `CopyBone`（`hand_r -> ik_hand_gun`）最终输出姿态。

---

## 三、跨蓝图架构风险优先处理建议（只读结论）

1. **立即修复项（功能性 Bug）**：
   - `bp_playerController::IF_TriggerDamageFlash` 错连到 `TriggerSpeedLines`；
   - `BPAC_propManager::F_equipProp` `Is Not Valid` 分支留空导致初次装备失效；
   - `BP_PlayerCharacter` 与 C++ `ADoomBaseCharacter::Landed` 重复发送 `State.Movement.Landed`。
2. **生命周期与性能卫生**：
   - 清理 `BP_PlayerCharacter` 的 0.5s `F_PrintPlayerHP` 调试定时器、`BPAC_propManager` 的 `"clearOld"` 打印与 `bp_playerController` 的震屏类名打印；
   - 移除 `bp_playerController`、`BPAC_propManager`、`BP_enemyBase`、`AIC_EnemyBase` 中的空 `EventTick` 节点，并在 `EndPlay`/`UnPossess`/`OnCharacterDeath` 补齐 UI、武器 Actor 与定时器清理。
3. **扩展 8 把武器与正式美术前置重构**：
   - 将 `bp_playerController` 的 `W_WeaponWheel_4Slot` 替换为数据驱动的 8 扇区轮盘（并注册缺失的 `State.Blocked.Fire` 标签）；
   - 解耦 `BPAC_propManager::StartingLoadout` 对具体武器蓝图的硬引用；
   - 将 `ABP_arms` 的多状态机硬编码（`SM_GPMG_main` / `SM_HHG_main`）重构为 Linked Anim Layer 或资产驱动的通用持枪模板。
