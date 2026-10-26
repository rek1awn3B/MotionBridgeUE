#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/PoseSnapshot.h"
#include "MotionBridgeSourcePoseAnimInstance.generated.h"

class UMotionBridgeReceiverComponent;

/**
 * 实验性 UE 跨组件重定向路径：把网络中的源骨架局部姿势写入隐藏源网格，
 * 再供 IK Retargeter 读取。该路径默认关闭，不属于当前公开演示主链路；
 * 默认路径由 Python 从当前 BVH 帧计算 Manny 姿势并直接生成目标快照。
 */
UCLASS(Transient, Blueprintable)
class TP_THIRDPERSON_API UMotionBridgeSourcePoseAnimInstance
    : public UAnimInstance
{
    GENERATED_BODY()

public:
    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    bool IsSourcePoseValid() const { return bSourcePoseValid; }
    const FPoseSnapshot& GetSourcePoseSnapshot() const
    {
        return SourcePoseSnapshot;
    }

protected:
    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Runtime Retarget")
    bool bSourcePoseValid = false;

    UPROPERTY(BlueprintReadOnly, Category = "MotionBridge|Runtime Retarget")
    FPoseSnapshot SourcePoseSnapshot;

private:
    void CacheReceiver();
    void UpdateSourcePose();

    UPROPERTY(Transient)
    TObjectPtr<UMotionBridgeReceiverComponent> ReceiverComponent;
};
