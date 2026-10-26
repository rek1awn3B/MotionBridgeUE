#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MotionBridgeFootIKComponent.generated.h"

class ACharacter;
class USkeletalMeshComponent;

USTRUCT(BlueprintType)
struct FMotionBridgeFootIKState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    bool bGrounded = false;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    FVector AnimatedLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    FVector TargetLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    FVector GroundNormal = FVector::UpVector;

    /** IK 前的原动画脚骨骼世界旋转。 */
    FQuat AnimatedRotationWorld = FQuat::Identity;

    /** 经过地面对齐和支撑期锁定后的脚骨骼世界旋转。 */
    FQuat TargetRotationWorld = FQuat::Identity;

    /** 本帧射线重建出的支撑平面上的一点，供最终姿势穿透验收使用。 */
    FVector SupportPlanePoint = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    float VerticalOffset = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    float Weight = 0.0f;

    /** IK 前原动画脚骨骼的世界空间速度，单位 cm/s。 */
    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    /** 原动画脚骨沿支撑面法线的速度绝对值，单位 cm/s。 */
    float AnimatedSpeed = 0.0f;

    /** 原动画脚骨的世界水平速度，单位 cm/s；用于检测支撑期滑步。 */
    float AnimatedHorizontalSpeed = 0.0f;

    /** 鞋跟/前掌中较高者到地面的距离；整只鞋底接近地面时才可能锁脚。 */
    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    float SoleClearance = BIG_NUMBER;

    /** 当前支撑脚是否已经捕获了一个固定的世界空间落脚点。 */
    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    bool bPositionLocked = false;

    /** 着地瞬间记录的世界空间脚底目标；支撑阶段保持不变。 */
    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    FVector LockedTargetLocation = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    FVector LockedGroundNormal = FVector::UpVector;

    /** 着地时捕获的脚骨骼世界旋转；支撑期间保持不变。 */
    FQuat LockedTargetRotationWorld = FQuat::Identity;

    /** 解锁时从固定落脚点平滑退回原动画轨迹，避免脚突然跳走。 */
    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    float PositionLockAlpha = 0.0f;

    FVector PreviousAnimatedLocation = FVector::ZeroVector;
    bool bHasPreviousAnimatedLocation = false;
};

/**
 * 在游戏线程检测左右脚下方的地面，并产生 AnimGraph 可消费的 IK 参数。
 *
 * 组件只负责地面查询、目标构造和平滑，不直接修改骨骼。真正的腿部解算
 * 由 AnimGraph / Control Rig 的 Two Bone IK 完成。
 */
UCLASS(ClassGroup = (MotionBridge), meta = (BlueprintSpawnableComponent))
class TP_THIRDPERSON_API UMotionBridgeFootIKComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UMotionBridgeFootIKComponent();

    /** 配置平地行走成片所需的脚部 IK 参数。 */
    void ConfigureFlatGroundWalkDemo()
    {
        bDrawDebug = false;
        PelvisOffsetScale = 0.0f;
        PelvisOffsetBias = -2.0f;
        HorizontalFootLockStrength = 1.0f;
        WeightResponseSpeed = 16.0f;
        FootLockReleaseResponseSpeed = 8.0f;
        MaxHorizontalFootLockCorrection = 6.0f;
        ContactSafetyMargin = 8.5f;
    }

    UFUNCTION(BlueprintPure, Category = "MotionBridge|Foot IK")
    const FMotionBridgeFootIKState& GetLeftFootState() const
    {
        return LeftFootState;
    }

    UFUNCTION(BlueprintPure, Category = "MotionBridge|Foot IK")
    const FMotionBridgeFootIKState& GetRightFootState() const
    {
        return RightFootState;
    }

    UFUNCTION(BlueprintPure, Category = "MotionBridge|Foot IK")
    float GetPelvisOffset() const
    {
        return PelvisOffset;
    }

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    void UpdateFoot(
        FName BoneName,
        const FVector& HeelContactOffset,
        const FVector& ToeContactOffset,
        const FVector& SoleNormalLocal,
        float DeltaTime,
        FMotionBridgeFootIKState& State,
        const FColor& DebugColor,
        const TCHAR* DebugLabel);

    void UpdateFootLock(
        float DesiredContactWeight,
        float DeltaTime,
        FMotionBridgeFootIKState& State);

    static float ExpSmoothingAlpha(float ResponseSpeed, float DeltaTime);

    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK")
    FName LeftFootBone = TEXT("foot_l");

    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK")
    FName RightFootBone = TEXT("foot_r");

    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0"))
    float TraceAboveFoot = 35.0f;

    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0"))
    float TraceBelowFoot = 55.0f;

    /** Manny 左脚鞋底后跟点，foot_l 骨骼局部空间，单位厘米。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK")
    FVector LeftHeelContactOffset = FVector(-8.36f, -3.74f, -0.14f);

    /** Manny 左脚鞋底鞋尖点，foot_l 骨骼局部空间，单位厘米。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK")
    FVector LeftToeContactOffset = FVector(-7.51f, 23.25f, 0.83f);

    /** Manny 右脚鞋底后跟点，foot_r 骨骼局部空间，单位厘米。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK")
    FVector RightHeelContactOffset = FVector(8.36f, 3.74f, 0.14f);

    /** Manny 右脚鞋底鞋尖点，foot_r 骨骼局部空间，单位厘米。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK")
    FVector RightToeContactOffset = FVector(7.51f, -23.25f, -0.83f);

    /** Manny 左脚鞋底朝上的法线，foot_l 骨骼局部空间。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK")
    FVector LeftSoleNormalLocal = FVector(1.0f, 0.0f, 0.0f);

    /** Manny 右脚骨骼是镜像坐标系，因此鞋底朝上方向为局部 -X。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK")
    FVector RightSoleNormalLocal = FVector(-1.0f, 0.0f, 0.0f);

    /**
     * 脚跟和前掌命中点偏离同一平面的最大距离（厘米）。
     * 超过该值说明鞋底正跨在台阶棱边上，不应强迫刚性脚掌同时贴合两个平面。
     */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0"))
    float SolePlaneTolerance = 2.0f;

    /** 抵消动画脚掌倾斜和网格外形误差的小幅离地余量（厘米）。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0"))
    float ContactSafetyMargin = 0.20f;

    /** 限制脚掌跟随地面法线的最大倾角，避免台阶边缘法线导致脚踝猛转。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0", ClampMax = "60.0"))
    float MaxGroundTiltDegrees = 25.0f;

    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0"))
    float PositionResponseSpeed = 18.0f;

    /**
     * 骨盆高度补偿强度。平地直线行走可设为 0，只保留双脚贴地；
     * 台阶和斜坡场景维持默认 1，继续使用完整的高差适配。
     */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float PelvisOffsetScale = 1.0f;

    /**
     * 额外的固定骨盆高度偏移（厘米）。可用于纯平地演示补偿鞋底/腿长误差，
     * 它不会像逐脚计算的高度修正那样随步伐周期起伏。
     */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK")
    float PelvisOffsetBias = 0.0f;

    /** 脚进入支撑锁定时 IK 权重的渐入速度，避免一帧内从 0 跳到 1。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0"))
    float WeightResponseSpeed = 12.0f;

    /** 支撑脚进入锁定所需的接触权重。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float FootLockEnterWeight = 0.8f;

    /** 接触权重下降到该值后才解锁，和进入阈值形成迟滞，避免反复抖动。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float FootLockExitWeight = 0.2f;

    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0"))
    float FootLockReleaseResponseSpeed = 20.0f;

    /** 支撑期把脚掌水平位置拉回着地点的强度；默认关闭以保持旧场景行为。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float HorizontalFootLockStrength = 0.0f;

    /** 单帧允许支撑脚修正的最大水平距离，防止错误接触造成腿部拉伸。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0"))
    float MaxHorizontalFootLockCorrection = 6.0f;

    /** 鞋底低于该高度时视为完整接触，单位厘米。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0"))
    float ContactClearanceFadeStart = 5.0f;

    /** 鞋底离地超过该值后完全退出支撑相，单位厘米。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.1"))
    float ContactClearanceFadeEnd = 10.0f;

    /** 脚骨沿地面法线速度低于该值时可视为稳定支撑，单位 cm/s。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0"))
    float ContactSpeedFadeStart = 10.0f;

    /** 脚骨沿地面法线速度超过该值后完全视为摆动脚，单位 cm/s。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.1"))
    float ContactSpeedFadeEnd = 60.0f;

    /** 水平速度低于该值时，脚才可能被视为稳定支撑，单位 cm/s。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0"))
    float ContactHorizontalSpeedFadeStart = 15.0f;

    /** 水平速度超过该值时必定是摆动脚，禁止进入支撑锁定，单位 cm/s。 */
    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.1"))
    float ContactHorizontalSpeedFadeEnd = 50.0f;

    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK")
    TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

    UPROPERTY(EditAnywhere, Category = "MotionBridge|Foot IK")
    bool bDrawDebug = true;

    UPROPERTY(Transient)
    TObjectPtr<ACharacter> CharacterOwner;

    UPROPERTY(Transient)
    TObjectPtr<USkeletalMeshComponent> CharacterMesh;

    UPROPERTY(VisibleAnywhere, Category = "MotionBridge|Foot IK")
    FMotionBridgeFootIKState LeftFootState;

    UPROPERTY(VisibleAnywhere, Category = "MotionBridge|Foot IK")
    FMotionBridgeFootIKState RightFootState;

    UPROPERTY(VisibleAnywhere, Category = "MotionBridge|Foot IK")
    float PelvisOffset = 0.0f;

    // 仅用于自动验收：确认运行时左右脚都实际命中过地面，且产生过修正量。
    int32 LeftGroundedFrameCount = 0;
    int32 RightGroundedFrameCount = 0;
    float MaxLeftOffsetMagnitude = 0.0f;
    float MaxRightOffsetMagnitude = 0.0f;
    float MaxPelvisOffsetMagnitude = 0.0f;
    float MaxLeftGroundTiltDegrees = 0.0f;
    float MaxRightGroundTiltDegrees = 0.0f;
    float MaxSettledTargetHeightDifference = 0.0f;
    float MaxLeftContactWeight = 0.0f;
    float MaxRightContactWeight = 0.0f;
    float MinLeftAnimatedSpeed = BIG_NUMBER;
    float MinRightAnimatedSpeed = BIG_NUMBER;
    float MinLeftSoleClearance = BIG_NUMBER;
    float MinRightSoleClearance = BIG_NUMBER;
    float MaxLeftSupportHorizontalSpeed = 0.0f;
    float MaxRightSupportHorizontalSpeed = 0.0f;
    float MaxLeftSupportSlipDistance = 0.0f;
    float MaxRightSupportSlipDistance = 0.0f;
    float MinLeftPostIKClearance = BIG_NUMBER;
    float MinRightPostIKClearance = BIG_NUMBER;
    int32 LeftPostIKSampleCount = 0;
    int32 RightPostIKSampleCount = 0;
    int32 LeftLockedFrameCount = 0;
    int32 RightLockedFrameCount = 0;
    float ValidationElapsedSeconds = 0.0f;
    bool bReportedRuntimeValidation = false;
    bool bTraceRuntimeFrames = false;
};
