# PROJECT CONSTITUTION (项目架构宪法与不可妥协红线)
> **文档属性**：本文件为跨会话的绝对永久技术锚点。任何新开的 Agent 对话在启动任务前必须首先研读本文件，严格遵守以下 5 条铁律。

---

## 一、 硬件与渲染基准（Hardware & Rendering Baseline）
1. **目标性能基准**：NVIDIA GeForce GTX 1660 Super (6GB VRAM)，目标运行在 1080p @ 60~120 FPS。
2. **光照与反射管线**：严禁开启重度实时光线追踪（Hardware Ray Tracing）或高开销 Lumen 硬件追踪；主地面与水坑必须走 **屏幕空间反射 (SSR) + 平面反射 (Planar Reflection) + 级联阴影 (CSM)** 高性能闭环。

---

## 二、 地面与环境材质定稿规范（Ground & Environment Standard）
1. **定稿版本**：主地面与墙体材质定稿为 **`v5`**（路径：`/Game/customMaterials/CleanGround/v5/`），严禁回退改动 `v1 ~ v4`（作为安全备份保留）。
2. **分层原则**：主地面必须由 **`Asphalt031`（无裂痕粗粝玄武岩）承载 88% 主地表**，仅由宏观噪波（`Damage_Island_Threshold`）在 12% 离散低洼凹陷区切入 `Asphalt02` 深裂缝。墙面与斜坡（`MI_Wall_CleanConcrete_v5`）必须保持 `Crack_Island_Enable = 0.0`，确保 100% 纯净无缝。
3. **水体物理原则**：水坑必须遵循三段式物理表现（渗透湿圈 -> 坑沿暗褐泥沙环 -> 坑心 0.01 粗糙度黑曜石霓虹镜面）。全局贴花资产位于 `/Game/customMaterials/Decals/`。

---

## 三、 C++ 与算法架构规范（C++ & Code Architecture）
1. **纯数学逻辑下沉 C++**：所有涉及向量几何、弹簧振子、阻尼回正、后坐力偏转、视差计算的纯数学逻辑，必须封装入 `FPSAnimFunctionLibrary`，函数名统一以 **`CFLF_`**（Custom Function Library Function）开头，禁止在蓝图中连出成百上千个杂乱数学节点。
2. **严禁破坏武器手臂动画图**：严禁私自改动或重构 `ABP_arms` 现有的 **双弹簧视角摆动（Hand Sway）与 FABRIK 虚拟枪骨（Virtual Gun Bone）IK 链**。

---

## 四、 玩法与技能系统规范（GAS & Gameplay Framework）
1. **GAS 统一调度**：角色技能（Dash、开火、近战、跳跃技能等）与属性必须统一走 Gameplay Ability System，依赖 `UDoomAttributeSet` 与 `TryActivateAbilitiesByTag`。
2. **严禁硬类型强转换（No Hard Cast To）**：严禁在蓝图中使用大量的 `Cast To BP_PlayerCharacter` 或具体武器子类强转；交互、换枪、伤害等统一通过 `GameplayTag`、接口（Interface）或 `BPAC_propManager` 组件派发事件。

---

## 五、 老 Demo 移植守则（Legacy Harvesting Protocol）
1. **读写分离与只读侦察**：严禁将老 Demo 的庞大蓝图节点连线全盘拷入当前上下文；
2. **去 Cast 洗白流程**：先通过 Python 脚本提取纯数值（CDO 参数、CameraShake 振幅等）填表，再提炼数学公式下沉为 `CFLF_` C++ 库，最后将 UI 交互事件重定向至 GameplayTag。
