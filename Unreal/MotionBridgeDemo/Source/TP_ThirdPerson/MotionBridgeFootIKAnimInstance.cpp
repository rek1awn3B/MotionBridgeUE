#include "MotionBridgeFootIKAnimInstance.h"

#include "BoneControllers/AnimNode_ObserveBone.h"
#include "Animation/PoseSnapshot.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Character.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "MotionBridgeFootIKComponent.h"
#include "MotionBridgeReceiverComponent.h"
#include "MotionBridgeRootMotionCharacter.h"
#include "MotionBridgeSourcePoseAnimInstance.h"
#include "UObject/UnrealType.h"

bool UMotionBridgeFootIKAnimInstance::TryGetPreIKBoneTransformWorld(
    const FName BoneName,
    FTransform& OutTransform) const
{
    const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
    if (!Mesh)
    {
        return false;
    }

    // AnimBlueprint 编译后，每个 Observe Bone 都会成为 AnimInstance 上的
    // FAnimNode_ObserveBone 属性。节点位于 IK 之前，所以 Translation 是
    // 原动画姿势中的组件空间位置，而不是已经被 IK 修改过的位置。
    for (TFieldIterator<FStructProperty> PropertyIt(GetClass()); PropertyIt; ++PropertyIt)
    {
        const FStructProperty* StructProperty = *PropertyIt;
        if (StructProperty->Struct != FAnimNode_ObserveBone::StaticStruct())
        {
            continue;
        }

        const FAnimNode_ObserveBone* ObserveNode =
            StructProperty->ContainerPtrToValuePtr<FAnimNode_ObserveBone>(this);
        if (ObserveNode &&
            ObserveNode->BoneToObserve.BoneName == BoneName &&
            ObserveNode->DisplaySpace == BCS_ComponentSpace &&
            !ObserveNode->Translation.ContainsNaN() &&
            !ObserveNode->Rotation.ContainsNaN())
        {
            const FTransform BoneTransformCS(
                ObserveNode->Rotation,
                ObserveNode->Translation,
                ObserveNode->Scale);
            OutTransform = BoneTransformCS * Mesh->GetComponentTransform();
            return true;
        }
    }
    return false;
}

void UMotionBridgeFootIKAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();
    CacheOwnerAndComponent();
}

void UMotionBridgeFootIKAnimInstance::NativeUpdateAnimation(
    const float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    if (!CharacterOwner || !FootIKComponent)
    {
        CacheOwnerAndComponent();
    }
    UpdateRealtimePose();
    UpdateRuntimeRetargetState();

    if (!CharacterOwner || !FootIKComponent)
    {
        LeftFootAlpha = 0.0f;
        RightFootAlpha = 0.0f;
        return;
    }

    const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
    if (!Mesh)
    {
        return;
    }
    const AMotionBridgeRootMotionCharacter* MotionCharacter =
        Cast<AMotionBridgeRootMotionCharacter>(CharacterOwner);
    const bool bIsRetargetProbe =
        MotionCharacter &&
        Mesh == MotionCharacter->GetRuntimeRetargetProbeMesh();

    const FTransform& ComponentToWorld = Mesh->GetComponentTransform();
    const FMotionBridgeFootIKState& LeftState =
        FootIKComponent->GetLeftFootState();
    const FMotionBridgeFootIKState& RightState =
        FootIKComponent->GetRightFootState();

    // 地面检测组件输出世界坐标；AnimGraph 的 Skeletal Control 节点使用组件空间。
    LeftFootEffectorCS = ComponentToWorld.InverseTransformPosition(
        LeftState.TargetLocation);
    RightFootEffectorCS = ComponentToWorld.InverseTransformPosition(
        RightState.TargetLocation);
    LeftGroundNormalCS = ComponentToWorld.InverseTransformVectorNoScale(
        LeftState.GroundNormal).GetSafeNormal();
    RightGroundNormalCS = ComponentToWorld.InverseTransformVectorNoScale(
        RightState.GroundNormal).GetSafeNormal();

    // Modify Bone 使用组件空间 Replace，直接写入最终目标朝向。
    // 不能用“上一帧原动画 -> 目标”的叠加增量：AnimGraph 消费组件数据时
    // 已经隔了一帧，而且 Two Bone IK 先修改了腿链，增量的参考姿势并不可靠。
    const auto BuildFootTargetRotationCS =
        [&ComponentToWorld](const FMotionBridgeFootIKState& State)
        {
            const FQuat ComponentRotation =
                ComponentToWorld.GetRotation().GetNormalized();
            const FQuat TargetComponent =
                ComponentRotation.Inverse() *
                State.TargetRotationWorld;
            return TargetComponent.GetNormalized().Rotator();
        };

    LeftFootRotationCS = BuildFootTargetRotationCS(LeftState);
    RightFootRotationCS = BuildFootTargetRotationCS(RightState);

    const FVector KneeDirectionWorld = CharacterOwner->GetActorForwardVector();
    const FVector LeftKneeWorld = Mesh->GetSocketLocation(LeftKneeBone);
    const FVector RightKneeWorld = Mesh->GetSocketLocation(RightKneeBone);
    LeftKneeTargetCS = ComponentToWorld.InverseTransformPosition(
        LeftKneeWorld + KneeDirectionWorld * KneeTargetDistance);
    RightKneeTargetCS = ComponentToWorld.InverseTransformPosition(
        RightKneeWorld + KneeDirectionWorld * KneeTargetDistance);

    const FVector PelvisOffsetWorld =
        FVector::UpVector * FootIKComponent->GetPelvisOffset();
    PelvisOffsetCS = ComponentToWorld.InverseTransformVectorNoScale(
        PelvisOffsetWorld);

    // 自动验收时可用同一动画、同一相机关闭 Foot IK，生成严格的 A/B 对照图。
    // 该开关只在显式命令行参数存在时生效，不改变正常运行配置。
    if (bIsRetargetProbe ||
        FParse::Param(FCommandLine::Get(), TEXT("MotionBridgeDisableFootIK")))
    {
        PelvisOffsetCS = FVector::ZeroVector;
        LeftFootAlpha = 0.0f;
        RightFootAlpha = 0.0f;
    }
    else
    {
        LeftFootAlpha = LeftState.Weight;
        RightFootAlpha = RightState.Weight;
    }
}

void UMotionBridgeFootIKAnimInstance::CacheOwnerAndComponent()
{
    CharacterOwner = Cast<ACharacter>(TryGetPawnOwner());
    FootIKComponent = CharacterOwner
        ? CharacterOwner->FindComponentByClass<UMotionBridgeFootIKComponent>()
        : nullptr;
    ReceiverComponent = CharacterOwner
        ? CharacterOwner->FindComponentByClass<UMotionBridgeReceiverComponent>()
        : nullptr;
    const AMotionBridgeRootMotionCharacter* MotionCharacter =
        Cast<AMotionBridgeRootMotionCharacter>(CharacterOwner);
    RetargetSourceMeshComponent = MotionCharacter
        ? MotionCharacter->GetRuntimeRetargetSourceMesh()
        : nullptr;
    RetargetProbeMeshComponent = MotionCharacter
        ? MotionCharacter->GetRuntimeRetargetProbeMesh()
        : nullptr;
}

void UMotionBridgeFootIKAnimInstance::UpdateRuntimeRetargetState()
{
    bRuntimeRetargetValid = false;
    if (!RetargetSourceMeshComponent || !RetargetProbeMeshComponent)
    {
        return;
    }

    const UMotionBridgeSourcePoseAnimInstance* SourceAnimInstance =
        Cast<UMotionBridgeSourcePoseAnimInstance>(
            RetargetSourceMeshComponent->GetAnimInstance());
    const AMotionBridgeRootMotionCharacter* MotionCharacter =
        Cast<AMotionBridgeRootMotionCharacter>(CharacterOwner);
    bRuntimeRetargetValid =
        SourceAnimInstance &&
        MotionCharacter &&
        MotionCharacter->ShouldUseRuntimeRetargetPipeline() &&
        SourceAnimInstance->IsSourcePoseValid();
}


void UMotionBridgeFootIKAnimInstance::UpdateRealtimePose()
{
    bRealtimePoseValid = false;
    RealtimePoseSnapshot.Reset();
    if (!ReceiverComponent)
    {
        return;
    }

    FMotionBridgeStreamDefinition Definition;
    FMotionBridgeSampleResult Sample;
    if (!ReceiverComponent->TryGetStreamDefinition(Definition) ||
        !ReceiverComponent->TryGetFreshPlaybackSample(Sample) ||
        Definition.BoneNames.Num() !=
            Sample.Frame.LocalBoneRotations.Num())
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

    FPoseSnapshot& Snapshot = RealtimePoseSnapshot;
    Snapshot.SnapshotName = TEXT("MotionBridgeRealtimePose");
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
            RealtimePoseSnapshot.Reset();
            return;
        }

        FTransform LocalTransform = ReferencePose[BoneIndex];
        const bool bIsRootBone = BoneName == TEXT("root");
        const bool bStripRootFromPose =
            bIsRootBone &&
            ReceiverComponent->bConsumeRootMotionAsActorMovement;
        if (!bStripRootFromPose &&
            Sample.Frame.LocalBoneTranslations.IsValidIndex(Index))
        {
            const FVector& Translation =
                Sample.Frame.LocalBoneTranslations[Index];
            if (Translation.ContainsNaN())
            {
                RealtimePoseSnapshot.Reset();
                return;
            }
            LocalTransform.SetTranslation(Translation);
        }
        if (bIsRootBone && !bStripRootFromPose)
        {
            if (Sample.Frame.RootPosition.ContainsNaN() ||
                Sample.Frame.RootRotation.ContainsNaN() ||
                Sample.Frame.RootRotation.SizeSquared() < UE_DOUBLE_SMALL_NUMBER)
            {
                RealtimePoseSnapshot.Reset();
                return;
            }
            LocalTransform.SetTranslation(Sample.Frame.RootPosition);
            LocalTransform.SetRotation(
                Sample.Frame.RootRotation.GetNormalized());
        }
        else if (!bStripRootFromPose)
        {
            LocalTransform.SetRotation(Rotation.GetNormalized());
        }
        Snapshot.LocalTransforms.Add(LocalTransform);
        BoneListHash = HashCombine(BoneListHash, GetTypeHash(BoneName));
    }

    // 故意不填目标网格名：Pose Snapshot 会按 BoneNames 建立映射，
    // 因而既支持完整 Manny 骨架，也支持只发送部分骨骼的调试流。
    Snapshot.SkeletalMeshName = FName(*FString::Printf(
        TEXT("MotionBridgeStream_%08X"),
        BoneListHash));
    Snapshot.bIsValid = true;
    bRealtimePoseValid = true;
}
