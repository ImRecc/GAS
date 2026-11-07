// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "FPSAnimFunctionLibrary.generated.h"

/**
 * FPS 第一人称动画与物理计算函数库
 */
UCLASS()
class GAS_API UFPSAnimFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 计算视角转动拖拽手部滞后目标（CFLF_calLookDragTarget）
	 *
	 * 【算法核心原理解析】：
	 * 1. 物理现象模拟：
	 *    当玩家快速转动视角（甩枪）时，手臂与枪身因惯性无法瞬间跟上视线转速，
	 *    会产生反向的旋转滞后（Rotation Lag）与微小的质心侧向平移（Translation Lag）。
	 *
	 * 2. 帧率无关性（Frame-rate Independence）：
	 *    直接使用两帧角度差在不同帧率下会导致幅度失真（高帧率极小、低帧率极大）。
	 *    因此通过除以 DeltaTime 换算为标准物理角速度 ω（度/秒，deg/sec）。
	 *
	 * 3. 防尖峰与卡顿保护（Numerical Safeguard）：
	 *    - DeltaTime 进行防除零保护（>= 0.0001s）。
	 *    - 角速度通过 MaxAngularSpeed（默认 720 deg/s）进行限幅（Clamp），防止切屏或鼠标瞬移导致画面拉扯爆炸。
	 *
	 * 4. 空间与惯性映射：
	 *    - Pitch（上下）：向上看时手臂向下滞后（负系数），限幅 [-8°, 8°]
	 *    - Yaw（左右）：向右转时手臂向左滞后（负系数），限幅 [-10°, 10°]
	 *    - Roll（倾斜）：向右转时枪身受力臂影响产生逆时针侧倾（负系数），限幅 [-5°, 5°]
	 *    - Trans Y（左右平移）：向右转时枪身向左轻微侧滑，限幅 [-2.0, 2.0] cm
	 *    - Trans Z（上下平移）：向上看时受杠杆几何影响枪身微抬，限幅 [-1.5, 1.5] cm
	 *
	 * @param CurrentAimRot 当前帧瞄准旋转（GetBaseAimRotation）
	 * @param LastAimRot 上一帧保存的瞄准旋转（LastControlRotation）
	 * @param DeltaTime 帧时间（GetWorldDeltaSeconds）
	 * @param LookSwayStrength 武器晃动系数乘数（默认 1.0）
	 * @param MaxAngularSpeed 最大角速度限幅（默认 720.0 度/秒）
	 * @param OutLookRot [输出] 滞后旋转量
	 * @param OutLookTrans [输出] 滞后平移量
	 */
	UFUNCTION(BlueprintPure, Category = "FPS|HandSway", meta = (DisplayName = "CFLF_calLookDragTarget", Keywords = "HandSway LookDrag Target CFLF", LookSwayStrength = "1.0", MaxAngularSpeed = "720.0"))
	static void CFLF_calLookDragTarget(
		const FRotator& CurrentAimRot,
		const FRotator& LastAimRot,
		float DeltaTime,
		float LookSwayStrength,
		float MaxAngularSpeed,
		FRotator& OutLookRot,
		FVector& OutLookTrans
	);

	/**
	 * 计算角色移动与起伏冲量目标（CFLF_calMoveImpulseTarget）
	 *
	 * 【算法核心原理解析】：
	 * 1. 局部线性加速度惯性（而非绝对速度）：
	 *    按住 W 匀速奔跑时局部加速度为 0，只有起步、急停、折返变向时产生加减速惯性冲量。
	 *    彻底根治持续按 W 导致手臂常驻偏角的缺陷。
	 * 2. 空间与惯性映射：
	 *    - 起步加速按 W：手部后缩沉降（Pitch 负角，X 位移后缩）
	 *    - 急停松 W：手部受惯性向前冲
	 *    - 左右急变向按 A/D：产生侧倾（Roll）与轻微偏航（Yaw）
	 * 3. 垂直与下落冲击：
	 *    空中跳跃/下落时根据垂直速度产生俯仰滞后（AirPitch），落地瞬间根据 Z 轴冲击加速度产生下沉顿挫（LandTransZ）。
	 *
	 * @param CurrentLocalVel 当帧局部速度（例如将 Velocity 变换到 Actor 本地旋转空间）
	 * @param LastLocalVel 上一帧缓存的局部速度（LastLocalVelocity）
	 * @param DeltaTime 帧时间（GetWorldDeltaSeconds）
	 * @param SideSwayStrength 武器侧向晃动灵敏度倍率（默认 1.0）
	 * @param bIsFalling 角色是否处于空中（CharacterMovement -> IsFalling）
	 * @param OutImpulseRot [输出] 移动冲击产生的旋转量
	 * @param OutImpulseTrans [输出] 移动冲击产生的平移量
	 */
	UFUNCTION(BlueprintPure, Category = "FPS|HandSway", meta = (DisplayName = "CFLF_calMoveImpulseTarget", Keywords = "HandSway MoveImpulse Target CFLF", SideSwayStrength = "1.0"))
	static void CFLF_calMoveImpulseTarget(
		const FVector& CurrentLocalVel,
		const FVector& LastLocalVel,
		float DeltaTime,
		float SideSwayStrength,
		bool bIsFalling,
		FRotator& OutImpulseRot,
		FVector& OutImpulseTrans
	);

	/**
	 * 计算跑动步态呼吸与8字形起伏目标（CFLF_calWalkBobbingTarget）
	 *
	 * 【算法核心原理解析】：
	 * 1. 李萨如 8 字形波形（Figure-8）：
	 *    水平方向（左右微晃）为 1 倍步频（Cos），垂直方向（换脚起伏）为 2 倍步频（Sin(2φ)）。
	 * 2. 相位角驱动（Phase Accumulator）：
	 *    根据地面移动速度推进相位角（模 2π 回绕），即使帧率浮动也能保证步伐起伏平滑连续。
	 * 3. 脚步触地触发点（Footstep Trigger）：
	 *    检测垂直正弦波谷触发点，预留给未来直接绑定脚步音效或地面震动。
	 *
	 * @param InWalkPhase 当前步态相位变量（WalkBobPhase，范围 [0, 2π)）
	 * @param LocalVelocity 角色局部速度
	 * @param DeltaTime 帧时间（GetWorldDeltaSeconds）
	 * @param bIsMovingOnGround 角色是否在地面有效移动（!IsFalling）
	 * @param MaxWalkSpeed 标准满速奔跑速度（默认 600.0）
	 * @param StepFrequency 步频角速度基准（弧度/秒，默认 9.5）
	 * @param BobStrength 步态呼吸总幅度乘数（默认 1.0）
	 * @param OutWalkPhase [输出] 更新后的步态相位角（存回 WalkBobPhase）
	 * @param OutBobRot [输出] 步态呼吸旋转偏移
	 * @param OutBobTrans [输出] 步态呼吸平移偏移
	 * @param bOutFootstepTrigger [输出] 脚步触地瞬间触发信号（可直接驱动音效）
	 */
	UFUNCTION(BlueprintPure, Category = "FPS|HandSway", meta = (DisplayName = "CFLF_calWalkBobbingTarget", Keywords = "HandSway WalkBobbing Target CFLF", MaxWalkSpeed = "600.0", StepFrequency = "9.5", BobStrength = "1.0"))
	static void CFLF_calWalkBobbingTarget(
		float InWalkPhase,
		const FVector& LocalVelocity,
		float DeltaTime,
		bool bIsMovingOnGround,
		float MaxWalkSpeed,
		float StepFrequency,
		float BobStrength,
		float& OutWalkPhase,
		FRotator& OutBobRot,
		FVector& OutBobTrans,
		bool& bOutFootstepTrigger
	);

	/**
	 * 二阶欠阻尼弹簧解算器（CFLF_solveDualSpringSway）
	 *
	 * 【算法核心原理解析】：
	 * 1. 目标合成与 ADS 机瞄衰减：
	 *    汇总视线拖拽（Look）、移动惯性（Impulse）、步态呼吸（Bob）三通道目标，
	 *    并通过 ADSAlpha（0=腰射, 1=机瞄）平滑衰减手部晃动幅度（完全机瞄时收敛至 15%）。
	 * 2. 二阶物理弹簧阻尼：
	 *    调用 FVectorSpringInterp 执行动力学解算（Stiffness 刚度 + DampingRatio 欠阻尼比）。
	 *    欠阻尼带来类似 Doom / 现代 3A 射击游戏的干脆过冲回弹感。
	 * 3. 100% 作用于 spine_05（极简且稳定的单骨骼驱动）：
	 *    直接输出总旋转与总平移，驱动 spine_05，双手和枪身作为刚体一体运动，
	 *    彻底避免手腕脱手或左手 IK 撕裂。
	 *
	 * @param LookRot 视角拖拽旋转目标
	 * @param LookTrans 视角拖拽平移目标
	 * @param ImpulseRot 移动加减速旋转目标
	 * @param ImpulseTrans 移动加减速平移目标
	 * @param BobRot 步态呼吸旋转目标
	 * @param BobTrans 步态呼吸平移目标
	 * @param ADSAlpha 机瞄权重（0.0=腰射, 1.0=完全机瞄）
	 * @param CurrentRot 上一帧输出的手部旋转状态
	 * @param CurrentTrans 上一帧输出的手部平移状态
	 * @param RotSpringState 旋转弹簧持久化状态（勾选 Pass-by-Reference）
	 * @param TransSpringState 平移弹簧持久化状态（勾选 Pass-by-Reference）
	 * @param Stiffness 弹簧刚度（默认 165.0）
	 * @param DampingRatio 弹簧阻尼比（默认 0.6，产生轻微过冲回弹）
	 * @param DeltaTime 帧时间（GetWorldDeltaSeconds）
	 * @param OutTotalRot [输出] 最终用于驱动 spine_05 的总旋转（存入手部Sway变量）
	 * @param OutTotalTrans [输出] 最终用于驱动 spine_05 的总平移（存入手部Sway变量）
	 */
	UFUNCTION(BlueprintCallable, Category = "FPS|HandSway", meta = (DisplayName = "CFLF_solveDualSpringSway", Keywords = "HandSway Solve Spring Dual CFLF", ADSAlpha = "0.0", Stiffness = "165.0", DampingRatio = "0.6"))
	static void CFLF_solveDualSpringSway(
		const FRotator& LookRot,
		const FVector& LookTrans,
		const FRotator& ImpulseRot,
		const FVector& ImpulseTrans,
		const FRotator& BobRot,
		const FVector& BobTrans,
		float ADSAlpha,
		const FRotator& CurrentRot,
		const FVector& CurrentTrans,
		UPARAM(ref) FVectorSpringState& RotSpringState,
		UPARAM(ref) FVectorSpringState& TransSpringState,
		float Stiffness,
		float DampingRatio,
		float DeltaTime,
		FRotator& OutTotalRot,
		FVector& OutTotalTrans
	);
};
