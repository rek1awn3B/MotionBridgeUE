#include "MotionBridgeSourcePoseAnimInstance.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "MotionBridgeReceiverComponent.h"

void UMotionBridgeSourcePoseAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    CacheReceiver();
}

void UMotionBridgeSourcePoseAnimInstance::NativeUpdateAnimation(
    const float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);
    if (!ReceiverComponent)
    {
        CacheReceiver();
    }
    UpdateSourcePose();
}

void UMotionBridgeSourcePoseAnimInstance::CacheReceiver()
{
    const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
    const AActor* Owner = Mesh ? Mesh->GetOwner() : nullptr;
    ReceiverComponent = Owner
        ? Owner->FindComponentByClass<UMotionBridgeReceiverComponent>()
        : nullptr;
}

void UMotionBridgeSourcePoseAnimInstance::UpdateSourcePose()
{
    bSourcePoseValid = false;
    SourcePoseSnapshot.Reset();
    if (!ReceiverComponent)
    {
        return;
    }

    FMotionBridgeStreamDefinition Definition;
    FMotionBridgeSampleResult Sample;
    if (!ReceiverComponent->TryGetStreamDefinition(Definition) ||
        !ReceiverComponent->TryGetFreshPlaybackSample(Sample) ||
        Definition.BoneNames.Num() != Sample.Frame.LocalBoneRotations.Num())
    {
        return;
    }

    USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
    USkeletalMesh* MeshAsset = Mesh ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!MeshAsset)
    {
        return;
    }

    const FReferenceSkeleton& ReferenceSkeleton = MeshAsset->GetRefSkeleton();
    const TArray<FTransform>& ReferencePose =
        ReferenceSkeleton.GetRefBonePose();

    FPoseSnapshot& Snapshot = SourcePoseSnapshot;
    Snapshot.SnapshotName = TEXT("MotionBridgeBvhSourcePose");
    Snapshot.BoneNames = Definition.BoneNames;
    Snapshot.LocalTransforms.Reserve(Definition.BoneNames.Num());

    uint32 BoneListHash = 0;
    for (int32 Index = 0; Index < Definition.BoneNames.Num(); ++Index)
    {
        const FName BoneName = Definition.BoneNames[Index];
        const int32 BoneIndex = ReferenceSkeleton.FindBoneIndex(BoneName);
        const FQuat& Rotation = Sample.Frame.LocalBoneRotations[Index];
        if (!ReferencePose.IsValidIndex(BoneIndex) ||
            Rotation.ContainsNaN() ||
            Rotation.SizeSquared() < UE_DOUBLE_SMALL_NUMBER)
        {
            SourcePoseSnapshot.Reset();
            return;
        }

        FTransform LocalTransform = ReferencePose[BoneIndex];
        // MotionRoot 是 IK Retargeter 配置中的源重定向根；最外层
        // MotionBridge_BVH_Source 只是 FBX 导入生成的骨架包装节点。
        const bool bIsMotionRoot = BoneName == TEXT("MotionRoot");
        const bool bStripRootFromPose =
            bIsMotionRoot &&
            ReceiverComponent->bConsumeRootMotionAsActorMovement;

        if (!bStripRootFromPose &&
            Sample.Frame.LocalBoneTranslations.IsValidIndex(Index))
        {
            const FVector& Translation =
                Sample.Frame.LocalBoneTranslations[Index];
            if (Translation.ContainsNaN())
            {
                SourcePoseSnapshot.Reset();
                return;
            }
            LocalTransform.SetTranslation(Translation);
        }

        if (!bStripRootFromPose)
        {
            LocalTransform.SetRotation(Rotation.GetNormalized());
        }

        Snapshot.LocalTransforms.Add(LocalTransform);
        BoneListHash = HashCombine(BoneListHash, GetTypeHash(BoneName));
    }

    Snapshot.SkeletalMeshName = FName(*FString::Printf(
        TEXT("MotionBridgeBvhSource_%08X"),
        BoneListHash));
    Snapshot.bIsValid = true;
    bSourcePoseValid = true;
}
