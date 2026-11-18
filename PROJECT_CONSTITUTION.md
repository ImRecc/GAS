# PROJECT_CONSTITUTION.md — UE5.8 Momentum FPS 最高工程宪章

## 1. 硬件基准与渲染红线 (Hardware & Rendering Red Lines)
- **引擎版本**: Unreal Engine 5.8 (C++ & Blueprint 混合架构，核心战斗基于 GAS)
- **目标硬件**: NVIDIA GTX 1660 Super (6GB VRAM)，目标 **1080p @ 60–120+ FPS**
- **绝对禁区**:
  - 严禁开启多反弹 Lumen GI 与 Nanite 微多边形置换/细分 (Tessellation)。
  - 光照与反射固定采用：`DirectionalLight` (CSM 动态阴影，高光掠射角 Pitch `-25°`) + `SSR` + 单一 `PlanarReflection` (`ScreenPercentage = 50`, `NormalDistortionStrength = 8.0`)。
  - 材质采样纹理若为灰度遮罩 (`Disp` / `Roughness` / `AO`)，导入时必须显式锁定 `sRGB = False` 且 `CompressionSettings = TC_Masks`。

## 2. 代码与蓝图解耦铁律 (Architectural Decoupling Rules)
- **零硬引用 (`Zero Hard Cast To`)**: 严禁在武器蓝图、UI (`WBP_`)、动画蓝图 (`ABP_`) 或敌人 AI 中使用 `Cast To BP_Player` / `Cast To ABP_arms` 硬耦合链条。跨模块通信一律通过 **Blueprint Interface (蓝图接口)**、**`BPAC_propManager` 组件**、**Event Dispatcher (事件分发器)** 或 **GameplayTags** 完成。
- **数学与逻辑下沉 C++ (`CFLF_` 规范)**: 凡涉及弹簧阻尼、后坐力曲线、向量坐标变换、扇区角度计算、战利品散落坐标等算法，严禁在蓝图图表里堆砌超过 10 个算术节点；必须封装为 C++ 函数库 (`FPSAnimFunctionLibrary` / `customFL`) 中以 `CFLF_` 开头的无状态或结构体驱动函数。
- **手臂动画禁区 (`ABP_arms` Protection)**:
  - 严格保护现代 3A 虚拟枪骨架构：`CopyBone(hand_r -> ik_hand_gun)`，武器挂点位于 `ik_hand_gun` 下。
  - 左手握持固定采用 `ik_hand_gun` 骨骼空间下的 `FABRIK` / `TwoBoneIK` (`IF_getPropIK`)。
  - 程序化摇摆与后坐力统一通过 `CFLF_solveDualSpringSway` 在 `ik_hand_gun` 与 `spine_05` 上执行叠加变换 (`BMM_Additive`)，严禁破坏现有 IK 顺序。

## 3. 核心类与资产注册表 (Single Source of Truth Registry)
| 模块域 | 核心类 / 资产名称 | 职责与契约说明 |
| :--- | :--- | :--- |
| **角色基类** | `ADoomBaseCharacter` | C++ 角色基类，承载移动/跳跃 CDO、内置 1.4s 串行 Dash 充能计时器与 `OnCharacterDeath(KillerActor)` 分发器 |
| **GAS 属性集** | `UDoomAttributeSet` | 管理 `Health`, `MaxHealth`, `JumpCount`, `DashCount` |
| **道具/武器中枢** | `BPAC_propManager` | 挂载于角色的组件，负责武器切换、IK 接口转发、UI 轮盘事件响应与后坐力信号路由 |
| **手臂动画蓝图** | `ABP_arms` | 第一人称浮动双臂，由 `CFLF_` 四大函数驱动双弹簧程序化摆动与落地冲击 (`LandShockAlpha`) |
| **敌人 AI 基类** | `AIC_EnemyBase` | 驱动 3 类基础杂兵 (`BP_enemy_Grunt_Melee`, `Ranged`, `Turret`)，通过 `TryActivateAbilitiesByTag` 触发 GAS 技能 |
| **地面主材质** | `/Game/customMaterials/CleanGround/v5/M_CyberCleanAsphalt_v5` | 88% `Asphalt031` 无缝粗粝基底 + 12% `Asphalt02` 稀疏破损裂纹岛 + 三层泥水边缘与黑曜石镜面水洼 |
| **手动水洼贴花** | `/Game/customMaterials/Decals/M_Decal_Puddle` | `DBuffer Translucent Color, Normal, Roughness`，锁定 `Water_Roughness = 0.01`，用于手动补水洼 |
| **沥青破碎坑洞贴花** | `/Game/customMaterials/Decals/M_Decal_Pothole` | 可调坑深、破碎边缘与坑底积水，用于重点区域点缀战损地表 |

## 4. 人机协作与工具链规范 (Agent Workflow Protocol)
- **只读侦察先行**: 涉及老资产移植或跨文件重构时，Phase 1 必须先做只读扫描并输出参数清单 `.md`，经开发者确认后再执行 Phase 2 写入。
- **拒绝顺从与隐患隐瞒**: 发现需求或参数存在性能/架构隐患时，必须明确列出【定义 / 假设 / 不确定性 / 需确认项】。
- **上下文卫生**: 每完成一个独立子系统里程碑，执行 `/compact` 或开启全新会话，严禁将旧材质/旧蓝图节点文本带入新任务。
