#include "Modules/ModuleManager.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "AnimationGraphSchema.h"
#include "AnimGraphNode_BlendListByBool.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ModifyBone.h"
#include "AnimGraphNode_ObserveBone.h"
#include "AnimGraphNode_PoseSnapshot.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_TwoBoneIK.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace MotionBridgeFootIKGraph
{
constexpr const TCHAR* BlueprintPath =
    TEXT("/Game/MotionBridge/FootIK/ABP_MotionBridgeFootIK.ABP_MotionBridgeFootIK");
constexpr const TCHAR* AnimationPath =
    TEXT("/Game/MotionBridge/RootMotionRetargeted/A_MotionBridgeRootMotion_Manny.A_MotionBridgeRootMotion_Manny");
constexpr const TCHAR* NaturalWalkBlueprintPath =
    TEXT("/Game/MotionBridge/FootIK/ABP_MotionBridgeNaturalWalkFootIK.ABP_MotionBridgeNaturalWalkFootIK");
constexpr const TCHAR* NaturalWalkAnimationPath =
    TEXT("/Game/Characters/Mannequins/Animations/Manny/MM_Walk_Fwd.MM_Walk_Fwd");
constexpr const TCHAR* HundredStyleBlueprintPath =
    TEXT("/Game/MotionBridge/100Style/ABP_MotionBridge100StyleFootIK.ABP_MotionBridge100StyleFootIK");
constexpr const TCHAR* HundredStyleAnimationPath =
    TEXT("/Game/MotionBridge/100StyleRetargeted/A_MotionBridge100Style_Manny.A_MotionBridge100Style_Manny");
constexpr const TCHAR* SFUWalkBlueprintPath =
    TEXT("/Game/MotionBridge/SFUWalk/ABP_MotionBridgeSFUWalkFootIK.ABP_MotionBridgeSFUWalkFootIK");
constexpr const TCHAR* SFUWalkAnimationPath =
    TEXT("/Game/MotionBridge/SFUWalkRetargeted/A_MotionBridgeSFUWalk_Manny.A_MotionBridgeSFUWalk_Manny");

template <typename NodeType>
NodeType* AddNode(UEdGraph& Graph, const int32 X, const int32 Y)
{
    FGraphNodeCreator<NodeType> Creator(Graph);
    NodeType* Node = Creator.CreateNode();
    Node->NodePosX = X;
    Node->NodePosY = Y;
    Creator.Finalize();
    return Node;
}

UEdGraphPin* FindPosePin(UEdGraphNode& Node, const EEdGraphPinDirection Direction)
{
    for (UEdGraphPin* Pin : Node.Pins)
    {
        if (Pin && Pin->Direction == Direction &&
            UAnimationGraphSchema::IsPosePin(Pin->PinType))
        {
            return Pin;
        }
    }
    return nullptr;
}

UEdGraphPin* FindDataPin(
    UEdGraphNode& Node,
    const FName Name,
    const EEdGraphPinDirection Direction)
{
    for (UEdGraphPin* Pin : Node.Pins)
    {
        if (Pin && Pin->Direction == Direction && Pin->PinName == Name)
        {
            return Pin;
        }
    }
    return nullptr;
}

bool Connect(UEdGraph& Graph, UEdGraphPin* Output, UEdGraphPin* Input)
{
    if (!Output || !Input)
    {
        return false;
    }
    return Graph.GetSchema()->TryCreateConnection(Output, Input);
}

UK2Node_VariableGet* AddVariableGet(
    UEdGraph& Graph,
    const FName VariableName,
    const int32 X,
    const int32 Y)
{
    FGraphNodeCreator<UK2Node_VariableGet> Creator(Graph);
    UK2Node_VariableGet* Node = Creator.CreateNode();
    Node->VariableReference.SetSelfMember(VariableName);
    Node->NodePosX = X;
    Node->NodePosY = Y;
    Creator.Finalize();
    return Node;
}

bool ConnectVariable(
    UEdGraph& Graph,
    UK2Node_VariableGet& VariableNode,
    UEdGraphNode& TargetNode,
    const FName TargetPinName)
{
    UEdGraphPin* ValuePin = nullptr;
    for (UEdGraphPin* Pin : VariableNode.Pins)
    {
        if (Pin && Pin->Direction == EGPD_Output &&
            Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Object)
        {
            ValuePin = Pin;
            break;
        }
    }
    return Connect(
        Graph,
        ValuePin,
        FindDataPin(TargetNode, TargetPinName, EGPD_Input));
}

void RequireConnection(const bool Connected, const TCHAR* Description)
{
    if (!Connected)
    {
        UE_LOG(LogTemp, Fatal, TEXT("MOTIONBRIDGE_FOOT_IK_GRAPH=FAIL|connection=%s"), Description);
    }
}

void Build(
    const TCHAR* TargetBlueprintPath = BlueprintPath,
    const TCHAR* TargetAnimationPath = AnimationPath,
    const TCHAR* ResultKey = TEXT("MOTIONBRIDGE_FOOT_IK_GRAPH"),
    const float PlayRate = 1.0f)
{
    UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, TargetBlueprintPath);
    UAnimSequence* Animation = LoadObject<UAnimSequence>(nullptr, TargetAnimationPath);
    if (!Blueprint || !Animation)
    {
        UE_LOG(
            LogTemp,
            Fatal,
            TEXT("%s=FAIL|missing_asset|blueprint=%d|animation=%d"),
            ResultKey,
            Blueprint != nullptr,
            Animation != nullptr);
    }

    UEdGraph* AnimGraph = nullptr;
    for (UEdGraph* Graph : Blueprint->FunctionGraphs)
    {
        if (Graph && Graph->GetFName() == TEXT("AnimGraph"))
        {
            AnimGraph = Graph;
            break;
        }
    }
    if (!AnimGraph)
    {
        UE_LOG(LogTemp, Fatal, TEXT("MOTIONBRIDGE_FOOT_IK_GRAPH=FAIL|missing_anim_graph"));
    }

    AnimGraph->Modify();
    UAnimGraphNode_Root* Root = nullptr;
    for (UEdGraphNode* Node : AnimGraph->Nodes)
    {
        if (UAnimGraphNode_Root* Candidate = Cast<UAnimGraphNode_Root>(Node))
        {
            Root = Candidate;
            break;
        }
    }
    if (!Root)
    {
        UE_LOG(LogTemp, Fatal, TEXT("MOTIONBRIDGE_FOOT_IK_GRAPH=FAIL|missing_root"));
    }

    TArray<UEdGraphNode*> OldNodes = AnimGraph->Nodes;
    for (UEdGraphNode* Node : OldNodes)
    {
        if (Node && Node != Root)
        {
            AnimGraph->RemoveNode(Node);
        }
    }
    Root->BreakAllNodeLinks();
    Root->NodePosX = 3250;
    Root->NodePosY = 0;

    UAnimGraphNode_SequencePlayer* Sequence =
        AddNode<UAnimGraphNode_SequencePlayer>(*AnimGraph, 0, 0);
    Sequence->Node.SetSequence(Animation);
    Sequence->Node.SetPlayRate(PlayRate);
    Sequence->ReconstructNode();

    UAnimGraphNode_PoseSnapshot* RealtimePose =
        AddNode<UAnimGraphNode_PoseSnapshot>(*AnimGraph, 0, 220);
    RealtimePose->Node.Mode = ESnapshotSourceMode::SnapshotPin;
    RealtimePose->ReconstructNode();
    for (int32 PinIndex = 0;
         PinIndex < RealtimePose->ShowPinForProperties.Num();
         ++PinIndex)
    {
        if (RealtimePose->ShowPinForProperties[PinIndex].PropertyName ==
            TEXT("Snapshot"))
        {
            RealtimePose->SetPinVisibility(true, PinIndex);
            break;
        }
    }

    UAnimGraphNode_BlendListByBool* PoseSelector =
        AddNode<UAnimGraphNode_BlendListByBool>(*AnimGraph, 250, 0);
    PoseSelector->ReconstructNode();

    UAnimGraphNode_LocalToComponentSpace* LocalToComponent =
        AddNode<UAnimGraphNode_LocalToComponentSpace>(*AnimGraph, 500, 0);

    UAnimGraphNode_ObserveBone* ObserveLeftFoot =
        AddNode<UAnimGraphNode_ObserveBone>(*AnimGraph, 750, -120);
    ObserveLeftFoot->Node.BoneToObserve.BoneName = TEXT("foot_l");
    ObserveLeftFoot->Node.DisplaySpace = BCS_ComponentSpace;
    ObserveLeftFoot->Node.bRelativeToRefPose = false;
    ObserveLeftFoot->ReconstructNode();

    UAnimGraphNode_ObserveBone* ObserveRightFoot =
        AddNode<UAnimGraphNode_ObserveBone>(*AnimGraph, 900, -120);
    ObserveRightFoot->Node.BoneToObserve.BoneName = TEXT("foot_r");
    ObserveRightFoot->Node.DisplaySpace = BCS_ComponentSpace;
    ObserveRightFoot->Node.bRelativeToRefPose = false;
    ObserveRightFoot->ReconstructNode();

    UAnimGraphNode_ModifyBone* Pelvis =
        AddNode<UAnimGraphNode_ModifyBone>(*AnimGraph, 1150, 0);
    Pelvis->Node.BoneToModify.BoneName = TEXT("pelvis");
    Pelvis->Node.TranslationMode = BMM_Additive;
    Pelvis->Node.TranslationSpace = BCS_ComponentSpace;
    Pelvis->Node.RotationMode = BMM_Ignore;
    Pelvis->Node.ScaleMode = BMM_Ignore;
    Pelvis->ReconstructNode();

    UAnimGraphNode_TwoBoneIK* LeftIK =
        AddNode<UAnimGraphNode_TwoBoneIK>(*AnimGraph, 1400, 0);
    LeftIK->Node.IKBone.BoneName = TEXT("foot_l");
    LeftIK->Node.EffectorLocationSpace = BCS_ComponentSpace;
    LeftIK->Node.JointTargetLocationSpace = BCS_ComponentSpace;
    LeftIK->ReconstructNode();

    UAnimGraphNode_TwoBoneIK* RightIK =
        AddNode<UAnimGraphNode_TwoBoneIK>(*AnimGraph, 1900, 0);
    RightIK->Node.IKBone.BoneName = TEXT("foot_r");
    RightIK->Node.EffectorLocationSpace = BCS_ComponentSpace;
    RightIK->Node.JointTargetLocationSpace = BCS_ComponentSpace;
    RightIK->ReconstructNode();

    UAnimGraphNode_ModifyBone* LeftFootRotation =
        AddNode<UAnimGraphNode_ModifyBone>(*AnimGraph, 1650, 0);
    LeftFootRotation->Node.BoneToModify.BoneName = TEXT("foot_l");
    LeftFootRotation->Node.TranslationMode = BMM_Ignore;
    LeftFootRotation->Node.RotationMode = BMM_Replace;
    LeftFootRotation->Node.RotationSpace = BCS_ComponentSpace;
    LeftFootRotation->Node.ScaleMode = BMM_Ignore;
    LeftFootRotation->ReconstructNode();

    UAnimGraphNode_ModifyBone* RightFootRotation =
        AddNode<UAnimGraphNode_ModifyBone>(*AnimGraph, 2150, 0);
    RightFootRotation->Node.BoneToModify.BoneName = TEXT("foot_r");
    RightFootRotation->Node.TranslationMode = BMM_Ignore;
    RightFootRotation->Node.RotationMode = BMM_Replace;
    RightFootRotation->Node.RotationSpace = BCS_ComponentSpace;
    RightFootRotation->Node.ScaleMode = BMM_Ignore;
    RightFootRotation->ReconstructNode();

    UAnimGraphNode_ComponentToLocalSpace* ComponentToLocal =
        AddNode<UAnimGraphNode_ComponentToLocalSpace>(*AnimGraph, 2500, 0);

    RequireConnection(
        Connect(*AnimGraph, FindPosePin(*RealtimePose, EGPD_Output), FindDataPin(*PoseSelector, TEXT("BlendPose_0"), EGPD_Input)),
        TEXT("realtime_pose_to_true_pose"));
    RequireConnection(
        Connect(*AnimGraph, FindPosePin(*Sequence, EGPD_Output), FindDataPin(*PoseSelector, TEXT("BlendPose_1"), EGPD_Input)),
        TEXT("sequence_to_false_pose"));
    RequireConnection(
        Connect(*AnimGraph, FindPosePin(*PoseSelector, EGPD_Output), FindPosePin(*LocalToComponent, EGPD_Input)),
        TEXT("pose_selector_to_local_to_component"));
    RequireConnection(
        Connect(*AnimGraph, FindPosePin(*LocalToComponent, EGPD_Output), FindPosePin(*ObserveLeftFoot, EGPD_Input)),
        TEXT("local_to_component_to_observe_left"));
    RequireConnection(
        Connect(*AnimGraph, FindPosePin(*ObserveLeftFoot, EGPD_Output), FindPosePin(*ObserveRightFoot, EGPD_Input)),
        TEXT("observe_left_to_observe_right"));
    RequireConnection(
        Connect(*AnimGraph, FindPosePin(*ObserveRightFoot, EGPD_Output), FindPosePin(*Pelvis, EGPD_Input)),
        TEXT("observe_right_to_pelvis"));
    RequireConnection(
        Connect(*AnimGraph, FindPosePin(*Pelvis, EGPD_Output), FindPosePin(*LeftIK, EGPD_Input)),
        TEXT("pelvis_to_left_ik"));
    RequireConnection(
        Connect(*AnimGraph, FindPosePin(*LeftIK, EGPD_Output), FindPosePin(*LeftFootRotation, EGPD_Input)),
        TEXT("left_ik_to_left_rotation"));
    RequireConnection(
        Connect(*AnimGraph, FindPosePin(*LeftFootRotation, EGPD_Output), FindPosePin(*RightIK, EGPD_Input)),
        TEXT("left_rotation_to_right_ik"));
    RequireConnection(
        Connect(*AnimGraph, FindPosePin(*RightIK, EGPD_Output), FindPosePin(*RightFootRotation, EGPD_Input)),
        TEXT("right_ik_to_right_rotation"));
    RequireConnection(
        Connect(*AnimGraph, FindPosePin(*RightFootRotation, EGPD_Output), FindPosePin(*ComponentToLocal, EGPD_Input)),
        TEXT("right_rotation_to_component_to_local"));
    RequireConnection(
        Connect(*AnimGraph, FindPosePin(*ComponentToLocal, EGPD_Output), FindPosePin(*Root, EGPD_Input)),
        TEXT("component_to_local_to_output"));

    UK2Node_VariableGet* PelvisOffset = AddVariableGet(
        *AnimGraph, TEXT("PelvisOffsetCS"), 260, 230);
    UK2Node_VariableGet* LeftEffector = AddVariableGet(
        *AnimGraph, TEXT("LeftFootEffectorCS"), 560, 300);
    UK2Node_VariableGet* LeftKnee = AddVariableGet(
        *AnimGraph, TEXT("LeftKneeTargetCS"), 560, 390);
    UK2Node_VariableGet* LeftAlpha = AddVariableGet(
        *AnimGraph, TEXT("LeftFootAlpha"), 560, 480);
    UK2Node_VariableGet* RightEffector = AddVariableGet(
        *AnimGraph, TEXT("RightFootEffectorCS"), 820, 300);
    UK2Node_VariableGet* RightKnee = AddVariableGet(
        *AnimGraph, TEXT("RightKneeTargetCS"), 820, 390);
    UK2Node_VariableGet* RightAlpha = AddVariableGet(
        *AnimGraph, TEXT("RightFootAlpha"), 820, 480);
    UK2Node_VariableGet* LeftRotation = AddVariableGet(
        *AnimGraph, TEXT("LeftFootRotationCS"), 930, 570);
    UK2Node_VariableGet* RightRotation = AddVariableGet(
        *AnimGraph, TEXT("RightFootRotationCS"), 1430, 570);
    UK2Node_VariableGet* RealtimePoseValid = AddVariableGet(
        *AnimGraph, TEXT("bRealtimePoseValid"), 180, 520);
    UK2Node_VariableGet* RealtimePoseSnapshot = AddVariableGet(
        *AnimGraph, TEXT("RealtimePoseSnapshot"), -80, 610);

    RequireConnection(
        ConnectVariable(*AnimGraph, *RealtimePoseValid, *PoseSelector, TEXT("bActiveValue")),
        TEXT("realtime_pose_valid"));
    RequireConnection(
        ConnectVariable(*AnimGraph, *RealtimePoseSnapshot, *RealtimePose, TEXT("Snapshot")),
        TEXT("realtime_pose_snapshot"));

    RequireConnection(
        ConnectVariable(*AnimGraph, *PelvisOffset, *Pelvis, TEXT("Translation")),
        TEXT("pelvis_offset"));
    RequireConnection(
        ConnectVariable(*AnimGraph, *LeftEffector, *LeftIK, TEXT("EffectorLocation")),
        TEXT("left_effector"));
    RequireConnection(
        ConnectVariable(*AnimGraph, *LeftKnee, *LeftIK, TEXT("JointTargetLocation")),
        TEXT("left_knee"));
    RequireConnection(
        ConnectVariable(*AnimGraph, *LeftAlpha, *LeftIK, TEXT("Alpha")),
        TEXT("left_alpha"));
    RequireConnection(
        ConnectVariable(*AnimGraph, *RightEffector, *RightIK, TEXT("EffectorLocation")),
        TEXT("right_effector"));
    RequireConnection(
        ConnectVariable(*AnimGraph, *RightKnee, *RightIK, TEXT("JointTargetLocation")),
        TEXT("right_knee"));
    RequireConnection(
        ConnectVariable(*AnimGraph, *RightAlpha, *RightIK, TEXT("Alpha")),
        TEXT("right_alpha"));
    RequireConnection(
        ConnectVariable(*AnimGraph, *LeftRotation, *LeftFootRotation, TEXT("Rotation")),
        TEXT("left_rotation"));
    RequireConnection(
        ConnectVariable(*AnimGraph, *LeftAlpha, *LeftFootRotation, TEXT("Alpha")),
        TEXT("left_rotation_alpha"));
    RequireConnection(
        ConnectVariable(*AnimGraph, *RightRotation, *RightFootRotation, TEXT("Rotation")),
        TEXT("right_rotation"));
    RequireConnection(
        ConnectVariable(*AnimGraph, *RightAlpha, *RightFootRotation, TEXT("Alpha")),
        TEXT("right_rotation_alpha"));

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    UPackage* Package = Blueprint->GetOutermost();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    if (!UPackage::SavePackage(Package, Blueprint, *Filename, SaveArgs))
    {
        UE_LOG(LogTemp, Fatal, TEXT("MOTIONBRIDGE_FOOT_IK_GRAPH=FAIL|save"));
    }

    UE_LOG(LogTemp, Display, TEXT("%s=PASS"), ResultKey);
}
} // namespace MotionBridgeFootIKGraph

namespace MotionBridgeWalkInspection
{
void Inspect(const TCHAR* AssetPath)
{
    UAnimSequence* Animation = LoadObject<UAnimSequence>(nullptr, AssetPath);
    if (!Animation)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("MOTIONBRIDGE_WALK_ASSET=FAIL|path=%s"),
            AssetPath);
        return;
    }

    const float PlayLength = Animation->GetPlayLength();
    const FTransform RootMotion = Animation->ExtractRootMotion(
        0.0f,
        PlayLength,
        false);
    const FVector Translation = RootMotion.GetTranslation();
    const float HorizontalDistance = FVector(
        Translation.X,
        Translation.Y,
        0.0f).Length();
    const float NominalSpeed = PlayLength > SMALL_NUMBER
        ? HorizontalDistance / PlayLength
        : 0.0f;

    FString CurveNames;
    if (const IAnimationDataModel* DataModel = Animation->GetDataModel())
    {
        for (const FFloatCurve& Curve : DataModel->GetFloatCurves())
        {
            if (!CurveNames.IsEmpty())
            {
                CurveNames += TEXT(",");
            }
            CurveNames += Curve.GetName().ToString();
        }
    }

    UE_LOG(
        LogTemp,
        Display,
        TEXT("MOTIONBRIDGE_WALK_ASSET=PASS|path=%s|length=%.6f|root_motion_enabled=%d|translation=%s|horizontal_distance=%.6f|nominal_speed=%.6f|curves=%s"),
        AssetPath,
        PlayLength,
        Animation->bEnableRootMotion,
        *Translation.ToCompactString(),
        HorizontalDistance,
        NominalSpeed,
        *CurveNames);
}

void InspectAll()
{
    Inspect(TEXT("/Game/Characters/Mannequins/Animations/Manny/MM_Walk_InPlace.MM_Walk_InPlace"));
    Inspect(TEXT("/Game/Characters/Mannequins/Animations/Manny/MM_Walk_Fwd.MM_Walk_Fwd"));
    UE_LOG(LogTemp, Display, TEXT("MOTIONBRIDGE_WALK_ASSET_INSPECTION=PASS"));
}
} // namespace MotionBridgeWalkInspection

namespace MotionBridgeAnimationCompression
{
void Rebuild(const TCHAR* AssetPath, const TCHAR* ResultKey)
{
    UAnimSequence* Animation = LoadObject<UAnimSequence>(nullptr, AssetPath);
    if (!Animation)
    {
        UE_LOG(LogTemp, Fatal, TEXT("%s=FAIL|missing_asset"), ResultKey);
    }

    Animation->Modify();
    const FName RootBoneName = TEXT("root");
    const IAnimationDataModel* DataModel = Animation->GetDataModel();
    if (!DataModel || !DataModel->IsValidBoneTrackName(RootBoneName))
    {
        UE_LOG(LogTemp, Fatal, TEXT("%s=FAIL|missing_raw_root_track"), ResultKey);
    }

    struct FTrackSnapshot
    {
        FName Name;
        TArray<FVector3f> Positions;
        TArray<FQuat4f> Rotations;
        TArray<FVector3f> Scales;
    };

    TArray<FName> TrackNames;
    DataModel->GetBoneTrackNames(TrackNames);
    TrackNames.Remove(RootBoneName);
    TrackNames.Insert(RootBoneName, 0);

    TArray<FTrackSnapshot> TrackSnapshots;
    TrackSnapshots.Reserve(TrackNames.Num());
    for (const FName TrackName : TrackNames)
    {
        TArray<FTransform> Transforms;
        DataModel->GetBoneTrackTransforms(TrackName, Transforms);
        if (Transforms.IsEmpty())
        {
            UE_LOG(
                LogTemp,
                Fatal,
                TEXT("%s=FAIL|empty_raw_track|track=%s"),
                ResultKey,
                *TrackName.ToString());
        }

        FTrackSnapshot& Snapshot = TrackSnapshots.AddDefaulted_GetRef();
        Snapshot.Name = TrackName;
        Snapshot.Positions.Reserve(Transforms.Num());
        Snapshot.Rotations.Reserve(Transforms.Num());
        Snapshot.Scales.Reserve(Transforms.Num());
        for (const FTransform& Transform : Transforms)
        {
            Snapshot.Positions.Add(FVector3f(Transform.GetTranslation()));
            Snapshot.Rotations.Add(FQuat4f(Transform.GetRotation()));
            Snapshot.Scales.Add(FVector3f(Transform.GetScale3D()));
        }
    }

    IAnimationDataController& Controller = Animation->GetController();
    {
        IAnimationDataController::FScopedBracket Bracket(
            Controller,
            FText::FromString(TEXT("Reorder animation tracks root first")),
            false);
        Controller.RemoveAllBoneTracks(false);
        for (const FTrackSnapshot& Snapshot : TrackSnapshots)
        {
            if (!Controller.AddBoneCurve(Snapshot.Name, false) ||
                !Controller.SetBoneTrackKeys(
                    Snapshot.Name,
                    Snapshot.Positions,
                    Snapshot.Rotations,
                    Snapshot.Scales,
                    false))
            {
                UE_LOG(
                    LogTemp,
                    Fatal,
                    TEXT("%s=FAIL|restore_track|track=%s"),
                    ResultKey,
                    *Snapshot.Name.ToString());
            }
        }
    }
    // 重定向批处理可能先生成压缩缓存、后写 Root Motion 标志。主动发送
    // Populated 通知使压缩数据失效，再同步重建当前平台数据，确保运行时
    // 的 ExtractRootMotion 读取到 root 轨迹，而不只是编辑器 RAW 轨道。
    Controller.NotifyPopulated();
    Animation->CacheDerivedDataForCurrentPlatform();

    UPackage* Package = Animation->GetOutermost();
    Package->MarkPackageDirty();
    const FString Filename = FPackageName::LongPackageNameToFilename(
        Package->GetName(), FPackageName::GetAssetPackageExtension());
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    if (!UPackage::SavePackage(Package, Animation, *Filename, SaveArgs))
    {
        UE_LOG(LogTemp, Fatal, TEXT("%s=FAIL|save"), ResultKey);
    }

    const FTransform RootMotion = Animation->ExtractRootMotion(
        0.0f,
        Animation->GetPlayLength(),
        false);
    const USkeleton* Skeleton = Animation->GetSkeleton();
    const FName SkeletonRoot =
        Skeleton && Skeleton->GetReferenceSkeleton().GetNum() > 0
            ? Skeleton->GetReferenceSkeleton().GetBoneName(0)
            : NAME_None;
    const UAnimSequence::FScopedCompressedAnimSequence CompressedData =
        Animation->GetCompressedData();
    const TArray<FTrackToSkeletonMap>& CompressedMap =
        CompressedData.Get().CompressedTrackToSkeletonMapTable;
    const int32 FirstCompressedBone =
        CompressedMap.IsEmpty() ? INDEX_NONE : CompressedMap[0].BoneTreeIndex;
    UE_LOG(
        LogTemp,
        Display,
        TEXT("%s=PASS|translation=%s|skeleton_root=%s|compressed_tracks=%d|first_compressed_bone=%d"),
        ResultKey,
        *RootMotion.GetTranslation().ToCompactString(),
        *SkeletonRoot.ToString(),
        CompressedMap.Num(),
        FirstCompressedBone);
}
} // namespace MotionBridgeAnimationCompression

class FMotionBridgeEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        if (FParse::Param(FCommandLine::Get(), TEXT("MotionBridgeBuildFootIKGraph")))
        {
            MotionBridgeFootIKGraph::Build();
        }
        if (FParse::Param(
                FCommandLine::Get(),
                TEXT("MotionBridgeBuildNaturalWalkFootIKGraph")))
        {
            MotionBridgeFootIKGraph::Build(
                MotionBridgeFootIKGraph::NaturalWalkBlueprintPath,
                MotionBridgeFootIKGraph::NaturalWalkAnimationPath,
                TEXT("MOTIONBRIDGE_NATURAL_WALK_FOOT_IK_GRAPH"),
                0.75f);
        }
        if (FParse::Param(
                FCommandLine::Get(),
                TEXT("MotionBridgeBuild100StyleFootIKGraph")))
        {
            MotionBridgeFootIKGraph::Build(
                MotionBridgeFootIKGraph::HundredStyleBlueprintPath,
                MotionBridgeFootIKGraph::HundredStyleAnimationPath,
                TEXT("MOTIONBRIDGE_100STYLE_FOOT_IK_GRAPH"));
        }
        if (FParse::Param(
                FCommandLine::Get(),
                TEXT("MotionBridgeBuildSFUWalkFootIKGraph")))
        {
            MotionBridgeFootIKGraph::Build(
                MotionBridgeFootIKGraph::SFUWalkBlueprintPath,
                MotionBridgeFootIKGraph::SFUWalkAnimationPath,
                TEXT("MOTIONBRIDGE_SFU_WALK_FOOT_IK_GRAPH"));
        }
        if (FParse::Param(
                FCommandLine::Get(),
                TEXT("MotionBridgeInspectWalkAssets")))
        {
            MotionBridgeWalkInspection::InspectAll();
        }
        if (FParse::Param(
                FCommandLine::Get(),
                TEXT("MotionBridgeRecompress100Style")))
        {
            MotionBridgeAnimationCompression::Rebuild(
                MotionBridgeFootIKGraph::HundredStyleAnimationPath,
                TEXT("MOTIONBRIDGE_100STYLE_RECOMPRESS"));
        }
        if (FParse::Param(
                FCommandLine::Get(),
                TEXT("MotionBridgeRecompressSFUWalk")))
        {
            MotionBridgeAnimationCompression::Rebuild(
                MotionBridgeFootIKGraph::SFUWalkAnimationPath,
                TEXT("MOTIONBRIDGE_SFU_WALK_RECOMPRESS"));
        }
    }
};

IMPLEMENT_MODULE(FMotionBridgeEditorModule, MotionBridgeEditor)
