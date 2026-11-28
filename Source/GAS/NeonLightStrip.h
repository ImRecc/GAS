#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineTypes.h"
#include "NeonLightStrip.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class URectLightComponent;
class UAudioComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;

/**
 * 霓虹灯条快捷配色预设（选择预设会自动填充 NeonColor；手动调色后自动切回 Custom）
 */
UENUM(BlueprintType)
enum class ENeonColorPreset : uint8
{
	Custom        UMETA(DisplayName = "自定义 (Custom)"),
	CyberCyan     UMETA(DisplayName = "赛博冷青 (Cyber Cyan)"),
	NeonAmber     UMETA(DisplayName = "警戒暖橙 (Neon Amber)"),
	HotMagenta    UMETA(DisplayName = "霓虹品红 (Hot Magenta)"),
	IceWhite      UMETA(DisplayName = "冰冷白光 (Ice White)"),
	ToxicGreen    UMETA(DisplayName = "毒液绿 (Toxic Green)"),
	WarningRed    UMETA(DisplayName = "警报红 (Warning Red)")
};

/**
 * 运行时呼吸/闪烁动态效果模式（Static 时完全关闭 Tick，零 CPU 开销）
 */
UENUM(BlueprintType)
enum class ENeonPulseMode : uint8
{
	Static             UMETA(DisplayName = "常亮静态 (Static - Zero Tick)"),
	Breathe            UMETA(DisplayName = "平滑呼吸 (Breathe)"),
	FluorescentFlicker UMETA(DisplayName = "雨夜电流闪烁 (Fluorescent Flicker)"),
	Strobe             UMETA(DisplayName = "脉冲频闪 (Strobe)")
};

/**
 * 雨夜赛博霓虹灯条与矩形补光基类
 * - 几何长度沿本地 Y 轴延伸，RectLight 默认沿本地 +X 轴向外照射（无需手动旋转光源即可贴合宽高）
 * - 支持编辑器实时预览、一键配色预设、低开销开关补光及运行时动态变色/闪烁
 */
UCLASS(Blueprintable)
class GAS_API ANeonLightStrip : public AActor
{
	GENERATED_BODY()

public:
	ANeonLightStrip();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** 立即将当前属性同步至网格体缩放、动态材质与矩形补光（蓝图与编辑器脚本通用） */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Neon")
	void ApplyVisualSettings();

	/** 根据预设枚举返回对应的线性颜色 */
	UFUNCTION(BlueprintPure, Category = "Neon|Preset")
	static FLinearColor GetPresetLinearColor(ENeonColorPreset Preset);

	// =========================================================================
	// 核心施工单暴露参数（与施工单命名完全一致）
	// =========================================================================

	/** 霓虹主色（同时驱动材质 EmissiveColor 与 RectLight 光色） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Appearance")
	FLinearColor NeonColor = FLinearColor(0.10f, 0.85f, 1.0f, 1.0f);

	/** 快捷预设色板（选择非 Custom 预设会自动更新 NeonColor） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Appearance")
	ENeonColorPreset ColorPreset = ENeonColorPreset::CyberCyan;

	/** 自发光倍率（驱动 M_NeonSign_Base 的 EmissiveIntensity） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Appearance", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "100.0"))
	float EmissiveIntensity = 15.0f;

	/** 矩形补光强度（bEnableLight 为 true 时生效） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Light", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "20000.0"))
	float LightIntensity = 1500.0f;

	/** 矩形补光衰减半径 (cm) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Light", meta = (ClampMin = "50.0", UIMin = "100.0", UIMax = "5000.0"))
	float LightAttenuationRadius = 1000.0f;

	/** 灯条总长度 (cm)，自动同步 StaticMesh Y 轴缩放与 RectLight SourceWidth */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Geometry", meta = (ClampMin = "20.0", UIMin = "50.0", UIMax = "3000.0"))
	float StripLengthCm = 400.0f;

	// =========================================================================
	// 快捷自定义扩展参数（尺寸、材质贴图、补光朝向与动态闪烁）
	// =========================================================================

	/** 灯条截面宽度/高度 (cm)，同步 StaticMesh Z 轴缩放与 RectLight SourceHeight */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Geometry", meta = (ClampMin = "4.0", UIMin = "4.0", UIMax = "400.0"))
	float StripWidthCm = 16.0f;

	/** 灯条厚度 (cm)，同步 StaticMesh X 轴缩放 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Geometry", meta = (ClampMin = "2.0", UIMin = "2.0", UIMax = "100.0"))
	float StripDepthCm = 10.0f;

	/** 是否启用矩形补光（关闭后仅保留自发光灯条，零光源 GPU 开销） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Light")
	bool bEnableLight = true;

	/** 矩形光是否投射阴影（GTX 1660S 默认关闭以保证高帧率） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Light")
	bool bCastLightShadows = false;

	/** 光源强度单位（默认 Candelas，可切换 Lumens 或 Unitless） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Light")
	ELightUnits LightUnits = ELightUnits::Candelas;

	/** RectLight 相对灯条表面的前移距离 (cm)，避免贴墙自遮挡 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Light", meta = (UIMin = "0.0", UIMax = "200.0"))
	float LightForwardOffsetCm = 16.0f;

	/** RectLight 俯仰微调角度 (度)，例如 -45° 或 -90° 可直接将光线压向湿地面 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Light", meta = (ClampMin = "-90.0", ClampMax = "90.0", UIMin = "-90.0", UIMax = "90.0"))
	float LightPitchOffsetDeg = 0.0f;

	/** RectLight 遮光板张开角度 (度)，收窄可减少不必要溢光 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Light", meta = (ClampMin = "0.0", ClampMax = "90.0", UIMin = "0.0", UIMax = "90.0"))
	float LightBarnDoorAngle = 88.0f;

	/** 基础材质（默认使用 /Game/customMaterials/LevelEnv/M_NeonSign_Base） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Material")
	TObjectPtr<UMaterialInterface> BaseMaterial = nullptr;

	/** 可选招牌贴图（留空使用材质默认白块；指定后会覆写 SignTexture 参数） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Material")
	TObjectPtr<UTexture2D> SignTexture = nullptr;

	/** 运行时动态发光模式（Static 时不启用 Tick） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Pulse")
	ENeonPulseMode PulseMode = ENeonPulseMode::Static;

	/** 动态呼吸/闪烁速度 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Pulse", meta = (ClampMin = "0.1", UIMin = "0.2", UIMax = "20.0", EditCondition = "PulseMode != ENeonPulseMode::Static"))
	float PulseSpeed = 2.5f;

	/** 动态最低亮度比例 (0~1) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Pulse", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", EditCondition = "PulseMode != ENeonPulseMode::Static"))
	float PulseMinFactor = 0.45f;

	/** 错峰相位偏移（秒），避免多根灯条同步闪烁 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Neon|Pulse", meta = (UIMin = "0.0", UIMax = "10.0", EditCondition = "PulseMode != ENeonPulseMode::Static"))
	float PulsePhaseOffset = 0.0f;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> StripMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<URectLightComponent> RectLight;

	/** 预留电流嗡鸣音效插槽（默认空，后续可直接挂载 SoundCue / MetaSound） */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UAudioComponent> HumAudio;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

private:
	void ApplyPulseFactor(float Factor);
	float EvaluatePulseFactor(float TimeSeconds) const;
	float PulseElapsedSeconds = 0.0f;
};
