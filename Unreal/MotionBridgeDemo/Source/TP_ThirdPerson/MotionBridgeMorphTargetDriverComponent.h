#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "MotionBridgeMorphTargetDriverComponent.generated.h"

class USkeletalMeshComponent;
class UMotionBridgeReceiverComponent;

/** 把 MotionBridge 标量通道按名称映射为 Skeletal Mesh Morph Target。 */
UCLASS(ClassGroup = (MotionBridge), meta = (BlueprintSpawnableComponent))
class TP_THIRDPERSON_API UMotionBridgeMorphTargetDriverComponent
    : public UActorComponent
{
    GENERATED_BODY()

public:
    UMotionBridgeMorphTargetDriverComponent();

    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    /** 留空时同名映射；可用于 ARKit 名称到自定义角色名称的重映射。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge|Morph")
    TMap<FName, FName> ChannelToMorphTarget;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "MotionBridge|Morph",
        meta = (ClampMin = "0.0"))
    float InterpolationSpeed = 14.0f;

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UMotionBridgeReceiverComponent> ReceiverComponent;

    UPROPERTY(Transient)
    TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent;

    TMap<FName, float> CurrentWeights;
    TMap<FName, float> MaximumObservedWeights;
    TSet<FName> MissingMorphTargets;
    bool bRunMorphTargetTest = false;
    bool bMorphTargetTestReported = false;
    float MorphTargetTestElapsedSeconds = 0.0f;

    void ApplySample(float DeltaTime);
    void UpdateMorphTargetTest(float DeltaTime);
};
