#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/PoseSnapshot.h"
#include "MotionBridgeFootIKAnimInstance.generated.h"

class ACharacter;
class UMotionBridgeFootIKComponent;
class UMotionBridgeReceiverComponent;

/**
 * 把游戏线程中的地面检测结果转换为 AnimGraph 使用的组件空间参数。
 */
UCLASS(Transient, Blueprintable)
class TP_THIRDPERSON_API UMotionBridgeFootIKAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    bool IsRealtimePoseValid() const { return bRealtimePoseValid; }
    const FPoseSnapshot& GetRealtimePoseSnapshot() const
    {
        return RealtimePoseSnapshot;
    }

    /**
     * 读取 AnimGraph 中 Observe Bone 在 IK 之前缓存的世界空间变换。
     * 组件用它做地面检测和支撑脚旋转锁定，避免把上一帧
     * IK 的输出重新当作下一帧输入。
     */
    bool TryGetPreIKBoneTransformWorld(
        FName BoneName,
        FTransform& OutTransform) const;

protected:
    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    FVector LeftFootEffectorCS = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    FVector RightFootEffectorCS = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    FVector LeftKneeTargetCS = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    FVector RightKneeTargetCS = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    FVector LeftGroundNormalCS = FVector::UpVector;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    FVector RightGroundNormalCS = FVector::UpVector;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    FRotator LeftFootRotationCS = FRotator::ZeroRotator;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    FRotator RightFootRotationCS = FRotator::ZeroRotator;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    FVector PelvisOffsetCS = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    float LeftFootAlpha = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Foot IK")
    float RightFootAlpha = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Realtime")
    bool bRealtimePoseValid = false;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Realtime")
    FPoseSnapshot RealtimePoseSnapshot;

private:
    void CacheOwnerAndComponent();
    void UpdateRealtimePose();

    UPROPERTY(Transient)
    TObjectPtr<ACharacter> CharacterOwner;

    UPROPERTY(Transient)
    TObjectPtr<UMotionBridgeFootIKComponent> FootIKComponent;

    UPROPERTY(Transient)
    TObjectPtr<UMotionBridgeReceiverComponent> ReceiverComponent;

    UPROPERTY(EditDefaultsOnly, Category = "MotionBridge|Foot IK")
    FName LeftKneeBone = TEXT("calf_l");

    UPROPERTY(EditDefaultsOnly, Category = "MotionBridge|Foot IK")
    FName RightKneeBone = TEXT("calf_r");

    UPROPERTY(EditDefaultsOnly, Category = "MotionBridge|Foot IK", meta = (ClampMin = "0.0"))
    float KneeTargetDistance = 50.0f;
};
