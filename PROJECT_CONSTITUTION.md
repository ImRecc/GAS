# PROJECT_CONSTITUTION.md — UE5.8 Momentum FPS 最高工程宪章

> **版本**：v2.0 (全要素融合版：硬件渲染 + 玩法铁律 + 资产规范 + 移植协议)  
> **地位**：新会话冷启动最高法（AI 必读）。控制在 80 行以内，消耗 <1500 Tokens，杜绝逻辑幻觉与工程断链。

---

## 1. 核心玩法与硬件基准红线 (Gameplay & Hardware Red Lines)
- **项目定位**：UE5.8 高机动、纯腰射第一人称射击（FPS）Demo。目标结项：2026 年底。
- **目标硬件**：NVIDIA GTX 1660 Super (6GB VRAM)，目标 **1080p @ 60–120+ FPS**。
- **玩法铁律**：
  - **严禁引入 ADS（开镜瞄准）与掩体系统**（纯腰射移动射击）。
  - **弹药采用 DOOM 式直接弹药池机制**（由 AttributeSet 扣除，无繁琐换弹动画断阻）。
- **渲染绝对禁区**：
  - 严禁开启多反弹 Lumen GI 与 Nanite 微多边形置换/细分。
  - 光照与反射固定采用：`DirectionalLight` (CSM 动态阴影，掠射角 Pitch `-25°`) + `SSR` + 单一 `PlanarReflection` (`ScreenPercentage = 50`)。
  - 灰度遮罩贴图 (`Disp`/`Roughness`/`AO`) 导入必须锁定 `sRGB = False` 且 `CompressionSettings = TC_Masks`。

---

## 2. 代码解耦与架构铁律 (Architectural Decoupling Rules)
- **零硬引用 (`Zero Hard Cast To`)**：严禁在武器、UI、动画蓝图或敌人 AI 中使用 `Cast To BP_Player` / `Cast To ABP_arms`。跨模块通信一律通过 **BPI 接口**、**`BPAC_propManager` 组件**、**Event Dispatcher** 或 **GameplayTags**。
- **数学与逻辑下沉 C++ (`CFLF_` 规范)**：弹簧阻尼、后坐力插值、向量投影、扇区检测、掉落物散落等算法，严禁在蓝图堆砌超过 10 个节点，必须封装为 C++ 函数库 `FPSAnimFunctionLibrary` 中以 `CFLF_` 开头的纯函数。
- **绝对工程禁令**：严禁重命名现有 C++ 函数库类名/函数名，严禁擅自移动已有核心资产路径（防止 Redirectors 与蓝图断链）。
- **手臂动画禁区 (`ABP_arms`)**：
  - 虚拟枪骨架构：`CopyBone(hand_r -> ik_hand_gun)`，武器挂载于 `ik_hand_gun`。
  - 左手握持采用 `ik_hand_gun` 骨骼空间下的 `FABRIK` / `TwoBoneIK`。
  - 程序化摆动与后坐力通过 `CFLF_solveDualSpringSway` 在 `ik_hand_gun` 与 `spine_05` 执行 `BMM_Additive` 变换，严禁打乱 IK 顺序。

---

## 3. 新建资产目录规范 (New Assets Directory Standard)
旧资产保持原位不动，此后所有新建资产严格归入以下目录：
- `/Game/Core/UI/` — 准星、弹药、血量、受击反馈 UI
- `/Game/Core/Feedback/` — Camera Shake、控制器后坐力曲线、打击音效与粒子
- `/Game/Core/Enemies/` — 敌人类蓝图、行为树与 GAS 技能
- `/Game/Core/Maps/` — 白盒测试场与关卡资产
- `/Game/customMaterials/` — 材质库、贴花、3D 碎石网格体

---

## 4. 老项目定向移植协议 (Porting Protocol)
- **老项目只读白名单（仅限以下 3 项用于打磨手感，其余代码一律不扫）**：
  1. **UI**（HUD / 准星 / 状态与受击反馈）
  2. **控制器后坐力**（Controller Recoil Pitch & Yaw 抬枪曲线，与手部骨骼 Additive 互补）
  3. **镜头晃动**（Camera Shake 资产）
- **移植三步铁律**：
  1. 严禁全盘扫描老项目。每次仅针对指定的 **1 个功能** 进行只读检索。
  2. 严禁照搬旧项目的非 GAS 依赖或旧角色基类。
  3. 动手前必须输出《移植适配清单》，经开发者确认后再执行写入。

---

## 5. 人机协作协议 (Agent Workflow Protocol)
- **骨架归人类，黑盒归 AI**：主事件图、状态机主干由开发者掌控；AI 负责纯数学 C++ 函数与独立参数配置。
- **只读侦察先行 & 拒绝盲从**：发现需求存在性能/架构隐患时，必须列出【定义 / 假设 / 不确定性 / 确认项】。
- **上下文卫生**：完成独立子系统里程碑后，执行 `/compact` 或新开会话。新会话直接读取 `PROJECT_CONSTITUTION.md` 与 `PROJECT_MAP.md` 即可完全同步。
对接点实读铁律：PROJECT_MAP.md 仅作路径索引；凡涉及对接新项目现有模块（如 BPAC_propManager、BPI_*、BP_PlayerCharacter）的方案，严禁盲猜变量与函数签名，必须先只读扫描对应资产并在方案中显式列出【定义 / 假设 / 不确定性 / 确认项】。