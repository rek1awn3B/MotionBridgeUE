#include "MotionBridgeRootMotionCharacter.h"

#include "MotionBridgeFootIKComponent.h"
#include "MotionBridgeFootIKAnimInstance.h"
#include "MotionBridgeReceiverComponent.h"
#include "MotionBridgeWebSocketClientComponent.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/PoseSnapshot.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/OutputDeviceRedirector.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
FTransform ExtractRootMotionRange(
    const UAnimSequence& Animation,
    const float StartTime,
    const float DeltaTime,
    const bool bAllowLooping)
{
    const FAnimExtractContext Context(
        static_cast<double>(StartTime),
        true,
        FDeltaTimeRecord(DeltaTime),
        bAllowLooping);
    return Animation.ExtractRootMotion(Context);
}
}

AMotionBridgeRootMotionCharacter::AMotionBridgeRootMotionCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    FootIKComponent = CreateDefaultSubobject<UMotionBridgeFootIKComponent>(
        TEXT("FootIKComponent"));
    ReceiverComponent = CreateDefaultSubobject<UMotionBridgeReceiverComponent>(
        TEXT("ReceiverComponent"));
    WebSocketClientComponent =
        CreateDefaultSubobject<UMotionBridgeWebSocketClientComponent>(
            TEXT("WebSocketClientComponent"));
    RuntimeRetargetSourceMesh =
        CreateDefaultSubobject<USkeletalMeshComponent>(
            TEXT("RuntimeRetargetSourceMesh"));
    RuntimeRetargetSourceMesh->SetupAttachment(GetCapsuleComponent());
    RuntimeRetargetSourceMesh->SetCollisionEnabled(
        ECollisionEnabled::NoCollision);
    RuntimeRetargetSourceMesh->SetGenerateOverlapEvents(false);
    RuntimeRetargetSourceMesh->SetCastShadow(false);
    RuntimeRetargetSourceMesh->SetHiddenInGame(true);
    RuntimeRetargetSourceMesh->VisibilityBasedAnimTickOption =
        EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    RuntimeRetargetProbeMesh =
        CreateDefaultSubobject<USkeletalMeshComponent>(
            TEXT("RuntimeRetargetProbeMesh"));
    RuntimeRetargetProbeMesh->SetupAttachment(GetCapsuleComponent());
    RuntimeRetargetProbeMesh->SetCollisionEnabled(
        ECollisionEnabled::NoCollision);
    RuntimeRetargetProbeMesh->SetGenerateOverlapEvents(false);
    RuntimeRetargetProbeMesh->SetCastShadow(false);
    RuntimeRetargetProbeMesh->SetHiddenInGame(true);
    RuntimeRetargetProbeMesh->VisibilityBasedAnimTickOption =
        EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

    static ConstructorHelpers::FObjectFinder<USkeletalMesh> SourceMeshFinder(
        TEXT("/Game/MotionBridge/100StyleSource/SK_MotionBridge100StyleSource.SK_MotionBridge100StyleSource"));
    if (SourceMeshFinder.Succeeded())
    {
        RuntimeRetargetSourceMesh->SetSkeletalMeshAsset(
            SourceMeshFinder.Object);
    }

    static ConstructorHelpers::FClassFinder<UAnimInstance>
        SourceAnimBlueprintFinder(
            TEXT("/Game/MotionBridge/RuntimeRetarget/ABP_MotionBridgeBvhSource"));
    if (SourceAnimBlueprintFinder.Succeeded())
    {
        RuntimeRetargetSourceMesh->SetAnimationMode(
            EAnimationMode::AnimationBlueprint);
        RuntimeRetargetSourceMesh->SetAnimInstanceClass(
            SourceAnimBlueprintFinder.Class);
    }

    GetCapsuleComponent()->InitCapsuleSize(42.0f, 96.0f);
    GetCharacterMovement()->bOrientRotationToMovement = false;

    static ConstructorHelpers::FObjectFinder<USkeletalMesh> MeshFinder(
        TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
    if (MeshFinder.Succeeded())
    {
        GetMesh()->SetSkeletalMeshAsset(MeshFinder.Object);
        RuntimeRetargetProbeMesh->SetSkeletalMeshAsset(MeshFinder.Object);
    }
    // 这段 Root Motion 动画的骨骼轨道已经包含约一个胶囊半高的
    // 垂直根偏移；不能再套用 ThirdPerson 模板的 Mesh -96 cm 下移，
    // 否则整个人（包括双脚射线起点）都会落到地板下方。
    GetMesh()->SetRelativeLocation(FVector::ZeroVector);
    GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
    RuntimeRetargetProbeMesh->SetRelativeLocation(FVector::ZeroVector);
    RuntimeRetargetProbeMesh->SetRelativeRotation(
        FRotator(0.0f, -90.0f, 0.0f));
    GetMesh()->SetDisablePostProcessBlueprint(true);
    // 自动验收会使用 NullRHI；即使 Mesh 没有被摄像机渲染，也必须持续
    // 刷新动画姿势，否则脚骨骼位置会停在参考姿势，射线测试失去意义。
    GetMesh()->VisibilityBasedAnimTickOption =
        EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    RuntimeRetargetProbeMesh->SetDisablePostProcessBlueprint(true);

    static ConstructorHelpers::FObjectFinder<UAnimSequence> AnimationFinder(
        TEXT("/Game/MotionBridge/RootMotionRetargeted/A_MotionBridgeRootMotion_Manny.A_MotionBridgeRootMotion_Manny"));
    if (AnimationFinder.Succeeded())
    {
        RootMotionAnimation = AnimationFinder.Object;
    }

    static ConstructorHelpers::FClassFinder<UAnimInstance> AnimBlueprintFinder(
        TEXT("/Game/MotionBridge/FootIK/ABP_MotionBridgeFootIK"));
    if (AnimBlueprintFinder.Succeeded())
    {
        GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        GetMesh()->SetAnimInstanceClass(AnimBlueprintFinder.Class);
    }
}

void AMotionBridgeRootMotionCharacter::ConfigurePortfolioWalkDemo()
{
    // Python 端从原始 BVH 逐帧执行公开的坐标标定与 Manny 骨骼映射；
    // UE 收到的是运行时计算出的 Manny 局部姿势，不再依赖不稳定的
    // Retarget Pose From Mesh 跨组件双缓冲读取。
    bUseRuntimeRetargetPipeline = false;
    bUseConstantSpeedRootMotion = false;
    bLoopRootMotionPlayback = true;
    ConstantSpeedRootMotionBlend = 0.0f;
    ConstantDirectionRootMotionBlend = 0.0f;

    // 实时流携带的是带根位移的完整姿势。演示时由角色 Actor 消费根运动，
    // AnimInstance 同时从姿势快照中剥离 root 位移，避免网格在胶囊内再次前进；
    // 相机跟随 Actor 后，角色会稳定保持在侧视画面中央。
    if (ReceiverComponent)
    {
        ReceiverComponent->bConsumeRootMotionAsActorMovement = true;
    }

    // Runtime Retarget 已经通过目标骨架根设置恢复了绝大部分角色高度，
    // 这里只补偿 Manny 脚底相对胶囊底面的剩余约 8 cm。必须在 BeginPlay
    // 前设置，让 ACharacter 正确缓存初始 Mesh Offset。
    GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -8.0f));
    RuntimeRetargetProbeMesh->SetRelativeLocation(
        FVector(0.0f, 0.0f, -8.0f));

    RootMotionAnimation = LoadObject<UAnimSequence>(
        nullptr,
        TEXT("/Game/MotionBridge/100StyleRetargeted/A_MotionBridge100Style_Manny.A_MotionBridge100Style_Manny"));
    UClass* AnimClass = LoadClass<UAnimInstance>(
        nullptr,
        TEXT("/Game/MotionBridge/100Style/ABP_MotionBridge100StyleFootIK.ABP_MotionBridge100StyleFootIK_C"));
    UClass* ProbeAnimClass = LoadClass<UAnimInstance>(
        nullptr,
        TEXT("/Game/MotionBridge/RuntimeRetarget/ABP_MotionBridgeMannyRetargetProbe.ABP_MotionBridgeMannyRetargetProbe_C"));
    if (AnimClass)
    {
        GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        GetMesh()->SetAnimInstanceClass(AnimClass);
    }
    if (ProbeAnimClass)
    {
        RuntimeRetargetProbeMesh->SetAnimationMode(
            EAnimationMode::AnimationBlueprint);
        RuntimeRetargetProbeMesh->SetAnimInstanceClass(ProbeAnimClass);
    }
    if (FootIKComponent)
    {
        const bool bDisableFootIK = FParse::Param(
            FCommandLine::Get(),
            TEXT("MotionBridgeDisableFootIK"));
        if (bDisableFootIK)
        {
            FootIKComponent->SetComponentTickEnabled(false);
        }
        else
        {
            FootIKComponent->ConfigureFlatGroundWalkDemo();
        }
    }
}

void AMotionBridgeRootMotionCharacter::BeginPlay()
{
    Super::BeginPlay();

    StartLocation = GetActorLocation();
    if (RuntimeRetargetSourceMesh && ReceiverComponent)
    {
        RuntimeRetargetSourceMesh->AddTickPrerequisiteComponent(
            ReceiverComponent);
        RuntimeRetargetProbeMesh->AddTickPrerequisiteComponent(
            RuntimeRetargetSourceMesh);
        GetMesh()->AddTickPrerequisiteComponent(
            RuntimeRetargetProbeMesh);
    }
    bRunRealtimePoseTest = FParse::Param(
        FCommandLine::Get(),
        TEXT("MotionBridgeRealtimePoseTest"));
    bRunUdpPoseTest = FParse::Param(
        FCommandLine::Get(),
        TEXT("MotionBridgeUdpPoseTest"));
    bRunUdpAnimationTest = FParse::Param(
        FCommandLine::Get(),
        TEXT("MotionBridgeUdpAnimationTest"));
    bRunUdpFallbackTest = FParse::Param(
        FCommandLine::Get(),
        TEXT("MotionBridgeUdpFallbackTest"));
    bRunUdpReconnectTest = FParse::Param(
        FCommandLine::Get(),
        TEXT("MotionBridgeUdpReconnectTest"));
    bRunStreamStatsTest = FParse::Param(
        FCommandLine::Get(),
        TEXT("MotionBridgeStreamStatsTest"));
    if (ReceiverComponent &&
        (bRunUdpAnimationTest || bRunUdpFallbackTest ||
         bRunUdpReconnectTest))
    {
        ReceiverComponent->bConsumeRootMotionAsActorMovement = true;
    }
    if (ReceiverComponent &&
        ReceiverComponent->bConsumeRootMotionAsActorMovement)
    {
        // 角色必须在 Receiver 选出本帧播放样本后再消费根运动。
        AddTickPrerequisiteComponent(ReceiverComponent);
        bApplyRootMotionToActor = false;
    }
    if (bRunRealtimePoseTest || bRunUdpPoseTest ||
        bRunUdpAnimationTest || bRunUdpFallbackTest ||
        bRunUdpReconnectTest ||
        bRunStreamStatsTest)
    {
        bApplyRootMotionToActor = false;
        if (bRunRealtimePoseTest)
        {
            InitializeRealtimePoseTest();
        }
        else
        {
            ExpectedRealtimeSpine = FQuat(
                FVector::ForwardVector,
                FMath::DegreesToRadians(22.0f));
            ExpectedRealtimeNeck = FQuat(
                FVector::UpVector,
                FMath::DegreesToRadians(17.0f));
            ExpectedRealtimeHead = FQuat(
                FVector::RightVector,
                FMath::DegreesToRadians(-13.0f));
        }
    }
    if (bRunStreamStatsTest)
    {
        RunStreamStatsTest();
    }
    if (!RootMotionAnimation)
    {
        UE_LOG(LogTemp, Error, TEXT("MOTIONBRIDGE_RUNTIME_ROOT_MOTION=FAIL|missing_animation"));
        return;
    }

    if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
    {
        // AnimGraph 播放同一段动画并叠加 Foot IK。角色胶囊的位移仍由
        // Tick 中的显式根运动提取负责，所以 AnimInstance 必须忽略自己
        // 累积的 Root Motion，避免同一段位移被消费两次。
        AnimInstance->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
        UE_LOG(
            LogTemp,
            Display,
            TEXT("MOTIONBRIDGE_RUNTIME_FOOT_IK_ANIM_INSTANCE=%s|class=%s"),
            AnimInstance->IsA<UMotionBridgeFootIKAnimInstance>()
                ? TEXT("PASS")
                : TEXT("FAIL"),
            *AnimInstance->GetClass()->GetName());
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("MOTIONBRIDGE_RUNTIME_FOOT_IK_ANIM_INSTANCE=FAIL|missing_instance"));
    }
    UE_LOG(
        LogTemp,
        Display,
        TEXT("MOTIONBRIDGE_RUNTIME_ROOT_MOTION_START=%s"),
        *StartLocation.ToCompactString());
    const FTransform TotalRootMotion = ExtractRootMotionRange(
        *RootMotionAnimation,
        0.0f,
        RootMotionAnimation->GetPlayLength(),
        false);
    TotalRootMotionTranslation = TotalRootMotion.GetTranslation();
    TotalRootMotionRotation = TotalRootMotion.GetRotation();
    UE_LOG(
        LogTemp,
        Display,
        TEXT("MOTIONBRIDGE_RUNTIME_EXTRACTED_TOTAL=%s"),
        *TotalRootMotion.GetTranslation().ToCompactString());
}

void AMotionBridgeRootMotionCharacter::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (ReceiverComponent &&
        ReceiverComponent->bConsumeRootMotionAsActorMovement)
    {
        UpdateNetworkRootMotion(DeltaSeconds);
    }

    if (bRunUdpAnimationTest && !bUdpAnimationReported)
    {
        UpdateUdpAnimationTest(DeltaSeconds);
    }

    if (bRunUdpFallbackTest && !bUdpFallbackReported)
    {
        UpdateUdpFallbackTest(DeltaSeconds);
    }

    if (bRunUdpReconnectTest && !bReconnectReported)
    {
        UpdateUdpReconnectTest(DeltaSeconds);
    }

    if ((bRunRealtimePoseTest || bRunUdpPoseTest) && !bRealtimePoseReported)
    {
        RealtimePoseTestElapsedSeconds += DeltaSeconds;
        FMotionBridgeSampleResult Sample;
        const float RequiredWarmupSeconds = bRunUdpPoseTest ? 1.5f : 0.5f;
        if (RealtimePoseTestElapsedSeconds >= RequiredWarmupSeconds &&
            ReceiverComponent &&
            ReceiverComponent->TryGetLatestPlaybackSample(Sample))
        {
            ReportRealtimePoseTest();
        }
        else if (RealtimePoseTestElapsedSeconds >= 5.0f)
        {
            bRealtimePoseReported = true;
            UE_LOG(LogTemp, Error, TEXT("MOTIONBRIDGE_REALTIME_POSE=FAIL|network_sample_timeout"));
        }
    }

    if (!RootMotionAnimation)
    {
        return;
    }

    const float PlayLength = RootMotionAnimation->GetPlayLength();
    float RemainingRootMotionTime = DeltaSeconds;
    int32 RootMotionStepCount = 0;
    while (
        RemainingRootMotionTime > KINDA_SMALL_NUMBER &&
        RootMotionStepCount++ < 4)
    {
        if (RootMotionPlaybackTime >= PlayLength - KINDA_SMALL_NUMBER)
        {
            if (!bLoopRootMotionPlayback)
            {
                break;
            }
            RootMotionPlaybackTime = 0.0f;
        }

        const float Step = FMath::Min(
            RemainingRootMotionTime,
            PlayLength - RootMotionPlaybackTime);
        if (bApplyRootMotionToActor)
        {
            FTransform LocalRootMotion = ExtractRootMotionRange(
                *RootMotionAnimation,
                RootMotionPlaybackTime,
                Step,
                false);
            if (bUseConstantSpeedRootMotion && PlayLength > KINDA_SMALL_NUMBER)
            {
                // 不再强制 100% 匀速：完全抹掉步态速度变化会让角色胶囊与
                // 支撑脚的落地节奏失配。这里在原始增量与匀速增量之间混合，
                // 总位移不变，但每一步的速度脉冲会被压低。
                const FVector ConstantTranslation =
                    TotalRootMotionTranslation * (Step / PlayLength);
                LocalRootMotion.SetTranslation(
                    FMath::Lerp(
                        LocalRootMotion.GetTranslation(),
                        ConstantTranslation,
                        FMath::Clamp(
                            ConstantSpeedRootMotionBlend,
                            0.0f,
                            1.0f)));

                const float StartAlpha = RootMotionPlaybackTime / PlayLength;
                const float EndAlpha =
                    (RootMotionPlaybackTime + Step) / PlayLength;
                const FQuat ConstantStartRotation = FQuat::Slerp(
                    FQuat::Identity,
                    TotalRootMotionRotation,
                    StartAlpha);
                const FQuat ConstantEndRotation = FQuat::Slerp(
                    FQuat::Identity,
                    TotalRootMotionRotation,
                    EndAlpha);
                const FQuat ConstantDeltaRotation =
                    ConstantEndRotation * ConstantStartRotation.Inverse();
                LocalRootMotion.SetRotation(FQuat::Slerp(
                    LocalRootMotion.GetRotation(),
                    ConstantDeltaRotation,
                    FMath::Clamp(
                        ConstantDirectionRootMotionBlend,
                        0.0f,
                        1.0f)).GetNormalized());
            }
            UCharacterMovementComponent* Movement = GetCharacterMovement();
            const FTransform WorldRootMotion = Movement->ConvertLocalRootMotionToWorld(
                LocalRootMotion,
                Step);
            FHitResult Hit;
            const FQuat NewRotation =
                WorldRootMotion.GetRotation() * GetActorQuat();
            Movement->SafeMoveUpdatedComponent(
                WorldRootMotion.GetTranslation(),
                NewRotation,
                true,
                Hit);
        }
        RootMotionPlaybackTime += Step;
        RemainingRootMotionTime -= Step;
    }

    // 运行时验收记录播放期间离起点的最大水平距离。循环演示会继续前进，
    // 但第一周期结束后仍只报告一次结果。
    const FVector CurrentDelta = GetActorLocation() - StartLocation;
    MaxHorizontalDistance = FMath::Max(
        MaxHorizontalDistance,
        FVector(CurrentDelta.X, CurrentDelta.Y, 0.0f).Length());

    if (bReportedResult)
    {
        return;
    }

    ElapsedSeconds += DeltaSeconds;
    if (ElapsedSeconds < PlayLength + 0.1f)
    {
        return;
    }

    bReportedResult = true;
    if (!bApplyRootMotionToActor)
    {
        UE_LOG(
            LogTemp,
            Display,
            TEXT("MOTIONBRIDGE_RUNTIME_ROOT_MOTION=IN_PLACE_FOR_FOOT_IK"));
        return;
    }

    const FVector Delta = GetActorLocation() - StartLocation;
    const float HorizontalDistance = FVector(Delta.X, Delta.Y, 0.0f).Length();
    UE_LOG(
        LogTemp,
        Display,
        TEXT("MOTIONBRIDGE_RUNTIME_ROOT_MOTION=%s|peak_distance=%.3f|final_distance=%.3f|delta=%s"),
        MaxHorizontalDistance >= 50.0f ? TEXT("PASS") : TEXT("FAIL"),
        MaxHorizontalDistance,
        HorizontalDistance,
        *Delta.ToCompactString());
}

void AMotionBridgeRootMotionCharacter::InitializeRealtimePoseTest()
{
    if (!ReceiverComponent || !GetMesh() || !GetMesh()->GetSkeletalMeshAsset())
    {
        UE_LOG(LogTemp, Error, TEXT("MOTIONBRIDGE_REALTIME_POSE=FAIL|missing_component_or_mesh"));
        bRealtimePoseReported = true;
        return;
    }

    const FReferenceSkeleton& ReferenceSkeleton =
        GetMesh()->GetSkeletalMeshAsset()->GetRefSkeleton();
    const TArray<FTransform>& ReferencePose = ReferenceSkeleton.GetRefBonePose();
    const auto BuildTarget = [&ReferenceSkeleton, &ReferencePose](
        const FName BoneName,
        const FVector& LocalAxis,
        const float AngleDegrees,
        FQuat& OutTarget)
    {
        const int32 BoneIndex = ReferenceSkeleton.FindBoneIndex(BoneName);
        if (!ReferencePose.IsValidIndex(BoneIndex))
        {
            return false;
        }
        const FQuat Delta(LocalAxis.GetSafeNormal(), FMath::DegreesToRadians(AngleDegrees));
        OutTarget = (ReferencePose[BoneIndex].GetRotation() * Delta).GetNormalized();
        return true;
    };

    if (!BuildTarget(TEXT("spine_01"), FVector::ForwardVector, 8.0f, ExpectedRealtimeSpine) ||
        !BuildTarget(TEXT("neck_01"), FVector::UpVector, 12.0f, ExpectedRealtimeNeck) ||
        !BuildTarget(TEXT("head"), FVector::UpVector, -18.0f, ExpectedRealtimeHead))
    {
        UE_LOG(LogTemp, Error, TEXT("MOTIONBRIDGE_REALTIME_POSE=FAIL|missing_test_bone"));
        bRealtimePoseReported = true;
        return;
    }

    FMotionBridgeStreamDefinition Definition;
    Definition.ProtocolVersion = 1;
    Definition.StreamId = TEXT("realtime-pose-runtime-test");
    Definition.BoneNames = {TEXT("spine_01"), TEXT("neck_01"), TEXT("head")};
    if (!ReceiverComponent->ConfigureStream(Definition))
    {
        UE_LOG(LogTemp, Error, TEXT("MOTIONBRIDGE_REALTIME_POSE=FAIL|stream_configuration"));
        bRealtimePoseReported = true;
        return;
    }

    ReceiverComponent->PlaybackDelaySeconds = 0.0;
    // 该测试只注入两帧固定姿势，目的是验证 Pose Snapshot 映射；
    // 不应让实时流超时机制干扰这个独立测试。
    ReceiverComponent->StreamTimeoutSeconds = 0.0;
    FMotionBridgeFrame Frame;
    Frame.FrameIndex = 1;
    Frame.TimestampSeconds = 0.0;
    Frame.LocalBoneRotations = {
        ExpectedRealtimeSpine,
        ExpectedRealtimeNeck,
        ExpectedRealtimeHead};
    const EMotionBridgePushResult FirstResult = ReceiverComponent->PushFrame(Frame);
    Frame.FrameIndex = 2;
    Frame.TimestampSeconds = 1.0;
    const EMotionBridgePushResult SecondResult = ReceiverComponent->PushFrame(Frame);
    if (FirstResult != EMotionBridgePushResult::Inserted ||
        SecondResult != EMotionBridgePushResult::Inserted)
    {
        UE_LOG(LogTemp, Error, TEXT("MOTIONBRIDGE_REALTIME_POSE=FAIL|frame_injection"));
        bRealtimePoseReported = true;
    }
}

void AMotionBridgeRootMotionCharacter::ReportRealtimePoseTest()
{
    bRealtimePoseReported = true;
    if (!GetMesh())
    {
        UE_LOG(LogTemp, Error, TEXT("MOTIONBRIDGE_REALTIME_POSE=FAIL|missing_mesh"));
        return;
    }

    const auto AngularErrorDegrees = [this](
        const FName BoneName,
        const FQuat& Expected)
    {
        const FQuat Actual = GetMesh()->GetBoneTransform(
            BoneName,
            RTS_ParentBoneSpace).GetRotation().GetNormalized();
        return FMath::RadiansToDegrees(Actual.AngularDistance(Expected));
    };

    const float SpineError = AngularErrorDegrees(TEXT("spine_01"), ExpectedRealtimeSpine);
    const float NeckError = AngularErrorDegrees(TEXT("neck_01"), ExpectedRealtimeNeck);
    const float HeadError = AngularErrorDegrees(TEXT("head"), ExpectedRealtimeHead);
    const float ThreeBoneMaxError = FMath::Max3(SpineError, NeckError, HeadError);
    const UMotionBridgeFootIKAnimInstance* MotionAnimInstance =
        Cast<UMotionBridgeFootIKAnimInstance>(GetMesh()->GetAnimInstance());
    const FPoseSnapshot* Snapshot = MotionAnimInstance
        ? &MotionAnimInstance->GetRealtimePoseSnapshot()
        : nullptr;
    float StreamMaxError = 0.0f;
    int32 StreamBoneCount = 0;
    if (Snapshot &&
        Snapshot->BoneNames.Num() == Snapshot->LocalTransforms.Num())
    {
        StreamBoneCount = Snapshot->BoneNames.Num();
        for (int32 Index = 0; Index < StreamBoneCount; ++Index)
        {
            StreamMaxError = FMath::Max(
                StreamMaxError,
                AngularErrorDegrees(
                    Snapshot->BoneNames[Index],
                    Snapshot->LocalTransforms[Index].GetRotation()));
        }
    }
    const bool bEnoughBones = !bRunUdpPoseTest || StreamBoneCount >= 20;
    const bool bPassed =
        ThreeBoneMaxError <= 0.5f &&
        StreamMaxError <= 0.5f &&
        bEnoughBones;
    UE_LOG(
        LogTemp,
        Display,
        TEXT("MOTIONBRIDGE_REALTIME_POSE=%s|spine_error_deg=%.3f|neck_error_deg=%.3f|head_error_deg=%.3f|stream_max_error_deg=%.3f|anim_valid=%d|snapshot_valid=%d|snapshot_bones=%d"),
        bPassed ? TEXT("PASS") : TEXT("FAIL"),
        SpineError,
        NeckError,
        HeadError,
        StreamMaxError,
        MotionAnimInstance && MotionAnimInstance->IsRealtimePoseValid(),
        Snapshot && Snapshot->bIsValid,
        StreamBoneCount);
    GLog->FlushThreadedLogs();
}

void AMotionBridgeRootMotionCharacter::UpdateUdpAnimationTest(
    const float DeltaSeconds)
{
    UdpAnimationElapsedSeconds += DeltaSeconds;

    FMotionBridgeSampleResult Sample;
    if (ReceiverComponent &&
        ReceiverComponent->TryGetLatestPlaybackSample(Sample) &&
        Sample.Frame.FrameIndex != LastUdpObservedFrameIndex)
    {
        LastUdpObservedFrameIndex = Sample.Frame.FrameIndex;
        ++UdpObservedFrameCount;

        if (!bHasUdpRootSample)
        {
            UdpRootMin = Sample.Frame.RootPosition;
            UdpRootMax = Sample.Frame.RootPosition;
            FirstUdpBoneRotations = Sample.Frame.LocalBoneRotations;
            bHasUdpRootSample = true;
        }
        else
        {
            UdpRootMin.X = FMath::Min(UdpRootMin.X, Sample.Frame.RootPosition.X);
            UdpRootMin.Y = FMath::Min(UdpRootMin.Y, Sample.Frame.RootPosition.Y);
            UdpRootMin.Z = FMath::Min(UdpRootMin.Z, Sample.Frame.RootPosition.Z);
            UdpRootMax.X = FMath::Max(UdpRootMax.X, Sample.Frame.RootPosition.X);
            UdpRootMax.Y = FMath::Max(UdpRootMax.Y, Sample.Frame.RootPosition.Y);
            UdpRootMax.Z = FMath::Max(UdpRootMax.Z, Sample.Frame.RootPosition.Z);

            const int32 ComparableBoneCount = FMath::Min(
                FirstUdpBoneRotations.Num(),
                Sample.Frame.LocalBoneRotations.Num());
            for (int32 Index = 0; Index < ComparableBoneCount; ++Index)
            {
                MaxUdpBoneMotionDegrees = FMath::Max(
                    MaxUdpBoneMotionDegrees,
                    FMath::RadiansToDegrees(
                        FirstUdpBoneRotations[Index].AngularDistance(
                            Sample.Frame.LocalBoneRotations[Index])));
            }
        }
    }

    if (UdpObservedFrameCount >= 69)
    {
        // Actor Tick 先于 Mesh/AnimInstance；多等一帧，让骨骼实际姿势追上
        // Receiver 已经选出的最后一个快照，再做逐骨骼误差比较。
        ++UdpTicksAfterFinalFrame;
    }
    if (UdpTicksAfterFinalFrame >= 2 ||
        UdpAnimationElapsedSeconds >= 5.0f)
    {
        ReportUdpAnimationTest();
    }
}

void AMotionBridgeRootMotionCharacter::UpdateNetworkRootMotion(
    const float DeltaSeconds)
{
    FMotionBridgeSampleResult Sample;
    if (!ReceiverComponent ||
        !ReceiverComponent->TryGetFreshPlaybackSample(Sample))
    {
        bNetworkRootInitialized = false;
        return;
    }

    const FVector CurrentPosition = Sample.Frame.RootPosition;
    const FQuat CurrentRotation = Sample.Frame.RootRotation.GetNormalized();
    if (CurrentPosition.ContainsNaN() ||
        CurrentRotation.ContainsNaN() ||
        CurrentRotation.SizeSquared() < UE_DOUBLE_SMALL_NUMBER)
    {
        return;
    }

    // 第一帧只建立基准；流重启或时间戳倒退时也重新建立基准，避免瞬移。
    if (!bNetworkRootInitialized ||
        Sample.Frame.TimestampSeconds <= LastNetworkRootTimestampSeconds)
    {
        bNetworkRootInitialized = true;
        LastNetworkRootTimestampSeconds = Sample.Frame.TimestampSeconds;
        PreviousNetworkRootPosition = CurrentPosition;
        PreviousNetworkRootRotation = CurrentRotation;
        return;
    }

    const FTransform PreviousRoot(
        PreviousNetworkRootRotation,
        PreviousNetworkRootPosition);
    const FTransform CurrentRoot(CurrentRotation, CurrentPosition);
    const FTransform LocalRootDelta =
        CurrentRoot.GetRelativeTransform(PreviousRoot);

    UCharacterMovementComponent* Movement = GetCharacterMovement();
    if (Movement)
    {
        const FTransform WorldRootDelta =
            Movement->ConvertLocalRootMotionToWorld(
                LocalRootDelta,
                DeltaSeconds);
        FHitResult Hit;
        const FQuat NewRotation =
            WorldRootDelta.GetRotation() * GetActorQuat();
        Movement->SafeMoveUpdatedComponent(
            WorldRootDelta.GetTranslation(),
            NewRotation,
            true,
            Hit);
    }

    LastNetworkRootTimestampSeconds = Sample.Frame.TimestampSeconds;
    PreviousNetworkRootPosition = CurrentPosition;
    PreviousNetworkRootRotation = CurrentRotation;

    const FVector ActorDelta = GetActorLocation() - StartLocation;
    MaxNetworkActorHorizontalDistance = FMath::Max(
        MaxNetworkActorHorizontalDistance,
        FVector(ActorDelta.X, ActorDelta.Y, 0.0f).Length());
}

void AMotionBridgeRootMotionCharacter::ReportUdpAnimationTest()
{
    bUdpAnimationReported = true;
    const UMotionBridgeFootIKAnimInstance* MotionAnimInstance =
        GetMesh()
            ? Cast<UMotionBridgeFootIKAnimInstance>(GetMesh()->GetAnimInstance())
            : nullptr;
    const FPoseSnapshot* Snapshot = MotionAnimInstance
        ? &MotionAnimInstance->GetRealtimePoseSnapshot()
        : nullptr;

    float MaxRotationErrorDegrees = 0.0f;
    float MaxTranslationError = 0.0f;
    int32 SnapshotBoneCount = 0;
    if (GetMesh() && Snapshot &&
        Snapshot->BoneNames.Num() == Snapshot->LocalTransforms.Num())
    {
        SnapshotBoneCount = Snapshot->BoneNames.Num();
        for (int32 Index = 0; Index < SnapshotBoneCount; ++Index)
        {
            const FTransform Actual = GetMesh()->GetBoneTransform(
                Snapshot->BoneNames[Index],
                RTS_ParentBoneSpace);
            const FTransform& Expected = Snapshot->LocalTransforms[Index];
            MaxRotationErrorDegrees = FMath::Max(
                MaxRotationErrorDegrees,
                FMath::RadiansToDegrees(
                    Actual.GetRotation().GetNormalized().AngularDistance(
                        Expected.GetRotation().GetNormalized())));
            MaxTranslationError = FMath::Max(
                MaxTranslationError,
                FVector::Distance(
                    Actual.GetTranslation(),
                    Expected.GetTranslation()));
        }
    }

    const FVector RootExtent = UdpRootMax - UdpRootMin;
    const float RootHorizontalExtent = FVector(
        RootExtent.X,
        RootExtent.Y,
        0.0f).Length();
    const FVector ActorDelta = GetActorLocation() - StartLocation;
    const float ActorFinalHorizontalDistance = FVector(
        ActorDelta.X,
        ActorDelta.Y,
        0.0f).Length();
    FMotionBridgeStreamStats StreamStats;
    if (ReceiverComponent)
    {
        ReceiverComponent->GetStreamStats(StreamStats);
    }
    const bool bPassed =
        UdpObservedFrameCount >= 20 &&
        RootHorizontalExtent >= 50.0f &&
        MaxNetworkActorHorizontalDistance >= 50.0f &&
        MaxUdpBoneMotionDegrees >= 5.0f &&
        SnapshotBoneCount >= 80 &&
        MotionAnimInstance &&
        MotionAnimInstance->IsRealtimePoseValid() &&
        MaxRotationErrorDegrees <= 0.5f &&
        MaxTranslationError <= 0.05f &&
        StreamStats.ReceivedUdpPackets >= 70 &&
        StreamStats.AcceptedFrames == 69 &&
        StreamStats.RejectedUdpPackets == 0 &&
        StreamStats.EstimatedLostFrames == 0;
    UE_LOG(
        LogTemp,
        Display,
        TEXT("MOTIONBRIDGE_UDP_ANIMATION=%s|observed_frames=%d|snapshot_bones=%d|root_horizontal_extent=%.3f|actor_peak_distance=%.3f|actor_final_distance=%.3f|max_bone_motion_deg=%.3f|max_rotation_error_deg=%.3f|max_translation_error=%.3f|udp_packets=%lld|accepted_frames=%lld|rejected_udp=%lld|estimated_lost=%lld|out_of_order=%lld|hard_resyncs=%lld|buffer_delay=%.3f"),
        bPassed ? TEXT("PASS") : TEXT("FAIL"),
        UdpObservedFrameCount,
        SnapshotBoneCount,
        RootHorizontalExtent,
        MaxNetworkActorHorizontalDistance,
        ActorFinalHorizontalDistance,
        MaxUdpBoneMotionDegrees,
        MaxRotationErrorDegrees,
        MaxTranslationError,
        StreamStats.ReceivedUdpPackets,
        StreamStats.AcceptedFrames,
        StreamStats.RejectedUdpPackets,
        StreamStats.EstimatedLostFrames,
        StreamStats.OutOfOrderFrames,
        StreamStats.HardResyncCount,
        StreamStats.CurrentBufferDelaySeconds);
    GLog->FlushThreadedLogs();
}


void AMotionBridgeRootMotionCharacter::UpdateUdpFallbackTest(
    const float DeltaSeconds)
{
    UdpFallbackElapsedSeconds += DeltaSeconds;
    const bool bStreamLive =
        ReceiverComponent && ReceiverComponent->IsPlaybackStreamLive();
    if (bStreamLive)
    {
        bSawFreshUdpPose = true;
        UdpFallbackStaleSeconds = 0.0f;
        return;
    }

    if (!bSawFreshUdpPose)
    {
        if (UdpFallbackElapsedSeconds >= 5.0f)
        {
            ReportUdpFallbackTest(TEXT("never_received_fresh_frame"));
        }
        return;
    }

    UdpFallbackStaleSeconds += DeltaSeconds;
    if (UdpFallbackStaleSeconds >= 0.2f && GetMesh())
    {
        if (!bCapturedFallbackPose)
        {
            FMotionBridgeStreamDefinition Definition;
            if (!ReceiverComponent->TryGetStreamDefinition(Definition))
            {
                ReportUdpFallbackTest(TEXT("missing_stream_definition"));
                return;
            }

            FallbackBoneNames = MoveTemp(Definition.BoneNames);
            FallbackStartRotations.Reset(FallbackBoneNames.Num());
            for (const FName BoneName : FallbackBoneNames)
            {
                FallbackStartRotations.Add(
                    GetMesh()->GetBoneTransform(
                        BoneName,
                        RTS_ParentBoneSpace).GetRotation().GetNormalized());
            }
            bCapturedFallbackPose = true;
        }
        else
        {
            for (int32 Index = 0;
                 Index < FallbackBoneNames.Num();
                 ++Index)
            {
                const FQuat Current = GetMesh()->GetBoneTransform(
                    FallbackBoneNames[Index],
                    RTS_ParentBoneSpace).GetRotation().GetNormalized();
                MaxFallbackBoneMotionDegrees = FMath::Max(
                    MaxFallbackBoneMotionDegrees,
                    FMath::RadiansToDegrees(
                        Current.AngularDistance(
                            FallbackStartRotations[Index])));
            }
        }
    }

    if (UdpFallbackStaleSeconds >= 0.8f)
    {
        ReportUdpFallbackTest();
    }
}


void AMotionBridgeRootMotionCharacter::ReportUdpFallbackTest(
    const TCHAR* FailureReason)
{
    bUdpFallbackReported = true;
    const UMotionBridgeFootIKAnimInstance* MotionAnimInstance =
        GetMesh()
            ? Cast<UMotionBridgeFootIKAnimInstance>(GetMesh()->GetAnimInstance())
            : nullptr;
    const bool bStreamLive =
        ReceiverComponent && ReceiverComponent->IsPlaybackStreamLive();
    const bool bAnimUsingRealtime =
        MotionAnimInstance && MotionAnimInstance->IsRealtimePoseValid();
    const bool bPassed =
        FailureReason == nullptr &&
        bSawFreshUdpPose &&
        !bStreamLive &&
        !bAnimUsingRealtime &&
        !bNetworkRootInitialized &&
        MaxFallbackBoneMotionDegrees >= 1.0f;
    UE_LOG(
        LogTemp,
        Display,
        TEXT("MOTIONBRIDGE_UDP_FALLBACK=%s|reason=%s|stream_live=%d|anim_realtime=%d|root_initialized=%d|fallback_motion_deg=%.3f"),
        bPassed ? TEXT("PASS") : TEXT("FAIL"),
        FailureReason ? FailureReason : TEXT("none"),
        bStreamLive,
        bAnimUsingRealtime,
        bNetworkRootInitialized,
        MaxFallbackBoneMotionDegrees);
    GLog->FlushThreadedLogs();
}


void AMotionBridgeRootMotionCharacter::UpdateUdpReconnectTest(
    const float DeltaSeconds)
{
    UdpReconnectElapsedSeconds += DeltaSeconds;
    const bool bStreamLive =
        ReceiverComponent && ReceiverComponent->IsPlaybackStreamLive();

    if (!bReconnectSawFirstLiveSession)
    {
        if (bStreamLive)
        {
            bReconnectSawFirstLiveSession = true;
        }
    }
    else if (!bReconnectSawTimeout)
    {
        if (!bStreamLive)
        {
            bReconnectSawTimeout = true;
            ReconnectDisconnectLocation = GetActorLocation();
        }
    }
    else if (!bReconnectSawSecondLiveSession)
    {
        if (bStreamLive)
        {
            bReconnectSawSecondLiveSession = true;
            ReconnectStartLocation = GetActorLocation();
            ReconnectFirstFrameJumpDistance = FVector::Dist2D(
                ReconnectStartLocation,
                ReconnectDisconnectLocation);
        }
    }
    else
    {
        ReconnectMaxResumedDistance = FMath::Max(
            ReconnectMaxResumedDistance,
            FVector::Dist2D(GetActorLocation(), ReconnectStartLocation));

        FMotionBridgeStreamStats Stats;
        ReceiverComponent->GetStreamStats(Stats);
        if (Stats.AcceptedFrames >= 20 &&
            ReconnectMaxResumedDistance >= 5.0f)
        {
            ReportUdpReconnectTest();
            return;
        }
    }

    if (UdpReconnectElapsedSeconds >= 12.0f)
    {
        ReportUdpReconnectTest(TEXT("timeout"));
    }
}


void AMotionBridgeRootMotionCharacter::ReportUdpReconnectTest(
    const TCHAR* FailureReason)
{
    bReconnectReported = true;
    FMotionBridgeStreamStats Stats;
    if (ReceiverComponent)
    {
        ReceiverComponent->GetStreamStats(Stats);
    }

    const bool bPassed =
        FailureReason == nullptr &&
        bReconnectSawFirstLiveSession &&
        bReconnectSawTimeout &&
        bReconnectSawSecondLiveSession &&
        ReconnectFirstFrameJumpDistance <= 1.0f &&
        ReconnectMaxResumedDistance >= 5.0f &&
        Stats.AcceptedFrames >= 20;
    UE_LOG(
        LogTemp,
        Display,
        TEXT("MOTIONBRIDGE_UDP_RECONNECT=%s|reason=%s|first_live=%d|timed_out=%d|second_live=%d|first_frame_jump=%.3f|resumed_distance=%.3f|accepted_after_reconnect=%lld|hard_resyncs=%lld"),
        bPassed ? TEXT("PASS") : TEXT("FAIL"),
        FailureReason ? FailureReason : TEXT("none"),
        bReconnectSawFirstLiveSession,
        bReconnectSawTimeout,
        bReconnectSawSecondLiveSession,
        ReconnectFirstFrameJumpDistance,
        ReconnectMaxResumedDistance,
        Stats.AcceptedFrames,
        Stats.HardResyncCount);
    GLog->FlushThreadedLogs();
}


void AMotionBridgeRootMotionCharacter::RunStreamStatsTest()
{
    if (!ReceiverComponent)
    {
        UE_LOG(LogTemp, Error, TEXT("MOTIONBRIDGE_STREAM_STATS=FAIL|missing_receiver"));
        return;
    }

    ReceiverComponent->bAutoAdvancePlayback = false;
    ReceiverComponent->PlaybackDelaySeconds = 0.1;
    ReceiverComponent->HardResyncThresholdSeconds = 0.25;

    FMotionBridgeStreamDefinition Definition;
    Definition.ProtocolVersion = 1;
    Definition.StreamId = TEXT("stream-stats-runtime-test");
    Definition.BoneNames = {TEXT("root")};
    if (!ReceiverComponent->ConfigureStream(Definition))
    {
        UE_LOG(LogTemp, Error, TEXT("MOTIONBRIDGE_STREAM_STATS=FAIL|configure_stream"));
        return;
    }

    const auto Push = [this](const int32 FrameIndex, const double Timestamp)
    {
        FMotionBridgeFrame Frame;
        Frame.FrameIndex = FrameIndex;
        Frame.TimestampSeconds = Timestamp;
        Frame.LocalBoneRotations = {FQuat::Identity};
        return ReceiverComponent->PushFrame(Frame);
    };

    Push(0, 0.0);
    Push(2, 0.066);
    Push(1, 0.033); // 晚到：填回先前估算出的缺口。
    Push(2, 0.066); // 重复：应拒绝。
    Push(4, 1.0);   // 永久缺少 frame 3。

    FMotionBridgeSampleResult Sample;
    ReceiverComponent->AdvancePlaybackAndSample(0.0, Sample);
    Push(5, 2.0);
    ReceiverComponent->AdvancePlaybackAndSample(0.016, Sample);

    FMotionBridgeStreamStats Stats;
    ReceiverComponent->GetStreamStats(Stats);
    const FString OverlayText = ReceiverComponent->BuildDebugOverlayText();
    const bool bOverlayTextValid =
        OverlayText.Contains(TEXT("accepted 5")) &&
        OverlayText.Contains(TEXT("lost 1")) &&
        OverlayText.Contains(TEXT("duplicate 1")) &&
        OverlayText.Contains(TEXT("out-of-order 1")) &&
        OverlayText.Contains(TEXT("hard resync 1"));
    const bool bPassed =
        Stats.SubmittedFrames == 6 &&
        Stats.AcceptedFrames == 5 &&
        Stats.RejectedFrames == 1 &&
        Stats.DuplicateFrames == 1 &&
        Stats.OutOfOrderFrames == 1 &&
        Stats.EstimatedLostFrames == 1 &&
        Stats.HardResyncCount == 1 &&
        Stats.BufferedFrames == 5 &&
        bOverlayTextValid &&
        FMath::IsNearlyEqual(
            Stats.CurrentBufferDelaySeconds,
            0.1,
            0.001);
    UE_LOG(
        LogTemp,
        Display,
        TEXT("MOTIONBRIDGE_STREAM_STATS=%s|submitted=%lld|accepted=%lld|rejected=%lld|duplicates=%lld|out_of_order=%lld|estimated_lost=%lld|hard_resyncs=%lld|buffered=%d|buffer_delay=%.3f|max_buffer_delay=%.3f|overlay_text_valid=%d"),
        bPassed ? TEXT("PASS") : TEXT("FAIL"),
        Stats.SubmittedFrames,
        Stats.AcceptedFrames,
        Stats.RejectedFrames,
        Stats.DuplicateFrames,
        Stats.OutOfOrderFrames,
        Stats.EstimatedLostFrames,
        Stats.HardResyncCount,
        Stats.BufferedFrames,
        Stats.CurrentBufferDelaySeconds,
        Stats.MaxBufferDelaySeconds,
        bOverlayTextValid);
    GLog->FlushThreadedLogs();
}
