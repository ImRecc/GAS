# (UE5 + GAS)

---

## 1. Core Architecture

```
[ Enhanced Input (IMC_player) ]
               │
               ▼
   [ BP_PlayerCharacter ] ◄── (BPI_comWithProps) ──┐
         │              ▲                          │
         │ (Owns)       │ (Implements)             │
         ▼              │                          │
  [ AbilitySystemComp ] ┘                          │
         │                                         │
         ├─────────────────────────────────────────┼──────────────────────┐
         ▼                                         ▼                      ▼
 [ Movement Abilities (GA) ]            [ BPAC_propManager ]      [ Animation (ABP_arms) ]
  - GA_dash / GA_jumping                 - Prop Inventory          - 2nd-Order Spring Sway
  - GA_crouchSlide                       - Equip / Stance Sync     - Procedural Recoil
  - GA_wallSliding / GA_vault            - Ability Grant/Revoke    - IK Hand Placement
                                                   │
                                                   ▼
                                         [ BPA_propBase ]
                                                   │
                                                   ▼
                                        [ BPCA_weaponBase ]
                                                   │
                                         ┌─────────┴─────────┐
                                         ▼                   ▼
                                 [ BPCA_gatlingGun ]  [ Other Weapons ]
```

---

## 2. 装备与道具继承分层 (Equipment Hierarchy)

| 层级 | 类名 | 核心职责 | 关键数据 / 接口 |
| :--- | :--- | :--- | :--- |
| **L1: 道具根基类** | `BPA_propBase` | 通用手持物基础，负责第一人称绑定与动画姿态识别 | `propMesh`, `GrantedAbility`, `propStanceTag`, `lookSwayStrength` |
| **L2: 武器根基类** | `BPCA_weaponBase` | 武器核心战斗参数，射击原型共享数据 | `baseDamage`, `baseFireDelay`, `targetFireDelay`, `recoilContext`, `projectileClass`, `sprayAngle` |
| **L3: 武器具体实现** | `BPCA_gatlingGun` 等 | 差异化视觉表现与特异性机制（网格体、枪口骨骼、特效） | 旋转枪管骨骼、特定音效/材质实例 |

---

## 3. 武器射击原型与 GA 分类 (Weapon Archetypes)

摒弃“一把枪对应一个专属 GA”的强耦合模式**：

1. **`GA_Weapon_SemiAuto` (半自动原型)**
   - 适配：手枪、超级霰弹枪、高斯单发狙。
   - 特性：单击触发、输入缓冲队列（Input Buffer 0.1s）、基于武器属性的动态冷却。
2. **`GA_Weapon_FullAuto` (全自动/可变频原型)**
   - 适配：突击步枪、等离子枪、加特林机枪。
   - 特性：按住射击循环、支持线性/曲线 RPM Ramp-Up（普通步枪加速度为 0，加特林机枪具有升频与降频阻尼）。
3. **`GA_Weapon_Charged` (蓄力原型)**
   - 适配：BFG 蓄能、电磁炮过载。

---

## 4. GAS 标签与状态互斥矩阵 (Ability & Tag Relationship Matrix)

系统基于 GAS 原生的 Tag 机制解决状态冲突与技能打断：

| 触发能力 (Ability Tag) | 授予状态 (Activation Owned Tag) | 阻塞能力 (Block Abilities) | 打断能力 (Cancel Abilities) | 备注 |
| :--- | :--- | :--- | :--- | :--- |
| `Ability.Dash` | `State.Dashing` | `Ability.Vaulting` | `Ability.Crouch` | DOOM 风格：冲刺中允许武器瞬发射击 |
| `Ability.CrouchSlide` | `State.Movement.CrouchSlide`| 无 | 无 | 滑铲中可全向瞄准与开火 |
| `Ability.WallSliding` | `State.Movement.WallSliding`| `Ability.Crouch` | 无 | 蹬墙滑行允许单手持枪射击 |
| `Ability.Vaulting` | `State.Vaulting` | `Ability.Fire`, `Ability.Dash` | `Ability.Fire` | 翻越掩体双手受限，强制打断开火 |
| `Ability.Fire` | `State.Firing` | 无 | 无 | 高频武器循环，退出时施加 Cooldown |

---

## 5. 核心蓝图通信契约 (Blueprint Interfaces)

- **`BPI_comWithProps`** (由 `BP_PlayerCharacter` 实现)：
  - `IF_getEquippedWeapon`: 获取当前激活的 `BPCA_weaponBase`。
  - `IF_getAimDirection`: 获取摄像机前向瞄准射线及射线起止点。
  - `IF_applyRecoil`: 将武器定义的 `recoilContext` 施加给第一人称手臂与相机。
- **`BPAC_propManager`** (挂载于角色)：
  - 监听武器切换输入，调用 `F_equipProp`。
  - 动态执行 `GiveAbility` / `ClearAbility`，保持角色 ASC 技能池轻量纯净。
