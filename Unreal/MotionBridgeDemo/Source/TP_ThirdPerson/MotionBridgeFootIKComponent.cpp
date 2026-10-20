#include "MotionBridgeFootIKComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "MotionBridgeFootIKAnimInstance.h"
#include "UnrealClient.h"

UMotionBridgeFootIKComponent::UMotionBridgeFootIKComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UMotionBridgeFootIKComponent::BeginPlay()
{
    Super::BeginPlay();

    CharacterOwner = Cast<ACharacter>(GetOwner());
    if (!CharacterOwner)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("MOTIONBRIDGE_FOOT_IK=DISABLED|owner_is_not_character"));
        SetComponentTickEnabled(false);
        return;
    }

    CharacterMesh = CharacterOwner->GetMesh();
    if (!CharacterMesh)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("MOTIONBRIDGE_FOOT_IK=DISABLED|missing_skeletal_mesh"));
        SetComponentTickEnabled(false);
        return;
    }

    bTraceRuntimeFrames = FParse::Param(
        FCommandLine::Get(),
        TEXT("MotionBridgeTraceFootIKFrames"));

    // 必须先让 Skeletal Mesh 计算本帧动画姿势，再读取 foot_l / foot_r。
    // 这里只建立“Mesh -> FootIKComponent”的单向依赖，不改 Actor 或移动组件时序。
    PrimaryComponentTick.AddPrerequisite(
        CharacterMesh,
        CharacterMesh->PrimaryComponentTick);

    UE_LOG(LogTemp, Display, TEXT("MOTIONBRIDGE_FOOT_IK=READY"));
}

void UMotionBridgeFootIKComponent::TickComponent(
    const float DeltaTime,
    const ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!CharacterOwner || !CharacterMesh)
    {
        return;
    }

    // Mesh 已经先于本组件完成本帧 AnimGraph。先用“上一帧求出的支撑面”
    // 验收当前最终姿势，再更新下一帧要消费的 IK 目标。这样测到的不是意图，
    // 而是 Two Bone IK + Modify Bone 真正输出的鞋跟/鞋尖位置。
    const auto AccumulatePostIKClearance =
        [this](
            const FName BoneName,
            const FVector& HeelContactOffset,
            const FVector& ToeContactOffset,
            const FMotionBridgeFootIKState& State,
            float& InOutMinClearance,
            int32& InOutSampleCount)
        {
            // 支撑期很短且释放经过平滑时，权重峰值可能只有约 0.88。
            // 0.85 已经足以检查最终脚底姿势，同时避免验收误报“没有样本”。
            if (!State.bPositionLocked ||
                State.PositionLockAlpha < 0.85f)
            {
                return;
            }

            const FTransform PostIKBoneTransformWorld =
                CharacterMesh->GetSocketTransform(BoneName, RTS_World);
            if (PostIKBoneTransformWorld.ContainsNaN())
            {
                return;
            }

            const FVector HeelWorld =
                PostIKBoneTransformWorld.TransformPosition(HeelContactOffset);
            const FVector ToeWorld =
                PostIKBoneTransformWorld.TransformPosition(ToeContactOffset);
            const float HeelClearance = FVector::DotProduct(
                HeelWorld - State.SupportPlanePoint,
                State.GroundNormal);
            const float ToeClearance = FVector::DotProduct(
                ToeWorld - State.SupportPlanePoint,
                State.GroundNormal);
            InOutMinClearance = FMath::Min(
                InOutMinClearance,
                FMath::Min(HeelClearance, ToeClearance));
            ++InOutSampleCount;
        };
    AccumulatePostIKClearance(
        LeftFootBone,
        LeftHeelContactOffset,
        LeftToeContactOffset,
        LeftFootState,
        MinLeftPostIKClearance,
        LeftPostIKSampleCount);
    AccumulatePostIKClearance(
        RightFootBone,
        RightHeelContactOffset,
        RightToeContactOffset,
        RightFootState,
        MinRightPostIKClearance,
        RightPostIKSampleCount);

    UpdateFoot(
        LeftFootBone,
        LeftHeelContactOffset,
        LeftToeContactOffset,
        LeftSoleNormalLocal,
        DeltaTime,
        LeftFootState,
        FColor::Red,
        TEXT("L"));
    UpdateFoot(
        RightFootBone,
        RightHeelContactOffset,
        RightToeContactOffset,
        RightSoleNormalLocal,
        DeltaTime,
        RightFootState,
        FColor::Green,
        TEXT("R"));

    // 射线命中只能证明脚下方存在地面，不能证明这只脚正在支撑身体。
    // 没有动画接触曲线时，用两个可观测条件近似支撑相：整只鞋底接近地面，
    // 并且脚骨沿支撑面法线方向近似静止。沿斜坡切线向前移动不等于离地；
    // 脚一旦真正远离表面或沿法线快速升降才解锁。
    const auto DesiredContactWeight =
        [this](const FMotionBridgeFootIKState& State)
        {
            if (!State.bGrounded || !State.bHasPreviousAnimatedLocation)
            {
                return 0.0f;
            }

            const float ClearanceRange = FMath::Max(
                ContactClearanceFadeEnd - ContactClearanceFadeStart,
                KINDA_SMALL_NUMBER);
            const float ClearanceWeight = 1.0f - FMath::Clamp(
                (State.SoleClearance - ContactClearanceFadeStart) /
                    ClearanceRange,
                0.0f,
                1.0f);
            const float SpeedRange = FMath::Max(
                ContactSpeedFadeEnd - ContactSpeedFadeStart,
                KINDA_SMALL_NUMBER);
            const float SpeedWeight = 1.0f - FMath::Clamp(
                (State.AnimatedSpeed - ContactSpeedFadeStart) / SpeedRange,
                0.0f,
                1.0f);
            const float HorizontalSpeedRange = FMath::Max(
                ContactHorizontalSpeedFadeEnd -
                    ContactHorizontalSpeedFadeStart,
                KINDA_SMALL_NUMBER);
            const float HorizontalSpeedWeight = 1.0f - FMath::Clamp(
                (State.AnimatedHorizontalSpeed -
                    ContactHorizontalSpeedFadeStart) /
                    HorizontalSpeedRange,
                0.0f,
                1.0f);
            return ClearanceWeight * SpeedWeight * HorizontalSpeedWeight;
        };

    const float DesiredLeftWeight = DesiredContactWeight(LeftFootState);
    const float DesiredRightWeight = DesiredContactWeight(RightFootState);
    if (DesiredLeftWeight >= FootLockEnterWeight)
    {
        MaxLeftSupportHorizontalSpeed = FMath::Max(
            MaxLeftSupportHorizontalSpeed,
            LeftFootState.AnimatedHorizontalSpeed);
    }
    if (DesiredRightWeight >= FootLockEnterWeight)
    {
        MaxRightSupportHorizontalSpeed = FMath::Max(
            MaxRightSupportHorizontalSpeed,
            RightFootState.AnimatedHorizontalSpeed);
    }
    if (LeftFootState.bGrounded)
    {
        MinLeftAnimatedSpeed = FMath::Min(
            MinLeftAnimatedSpeed,
            LeftFootState.AnimatedSpeed);
        MinLeftSoleClearance = FMath::Min(
            MinLeftSoleClearance,
            LeftFootState.SoleClearance);
    }
    if (RightFootState.bGrounded)
    {
        MinRightAnimatedSpeed = FMath::Min(
            MinRightAnimatedSpeed,
            RightFootState.AnimatedSpeed);
        MinRightSoleClearance = FMath::Min(
            MinRightSoleClearance,
            RightFootState.SoleClearance);
    }

    // “贴地”和“支撑锁定”是两件事：UpdateFoot 先按当前
    // 动画 X/Y 求出地面高度和目标朝向；这里在支撑相锁定支撑面
    // 法线与脚掌朝向，但不锁世界 X/Y/Z。摆动相开始后再平滑释放。
    UpdateFootLock(DesiredLeftWeight, DeltaTime, LeftFootState);
    UpdateFootLock(DesiredRightWeight, DeltaTime, RightFootState);
    const auto ApplyHorizontalFootLock = [this](
        FMotionBridgeFootIKState& State)
        {
            const float Alpha = FMath::Clamp(
                State.PositionLockAlpha * HorizontalFootLockStrength,
                0.0f,
                1.0f);
            if (Alpha <= KINDA_SMALL_NUMBER)
            {
                return;
            }

            FVector Correction =
                State.LockedTargetLocation - State.TargetLocation;
            Correction.Z = 0.0f;
            Correction = Correction.GetClampedToMaxSize(
                MaxHorizontalFootLockCorrection);
            State.TargetLocation += Correction * Alpha;
        };
    ApplyHorizontalFootLock(LeftFootState);
    ApplyHorizontalFootLock(RightFootState);
    if (LeftFootState.bPositionLocked)
    {
        MaxLeftSupportSlipDistance = FMath::Max(
            MaxLeftSupportSlipDistance,
            FVector::Dist2D(
                LeftFootState.TargetLocation,
                LeftFootState.LockedTargetLocation));
    }
    if (RightFootState.bPositionLocked)
    {
        MaxRightSupportSlipDistance = FMath::Max(
            MaxRightSupportSlipDistance,
            FVector::Dist2D(
                RightFootState.TargetLocation,
                RightFootState.LockedTargetLocation));
    }

    // DesiredContactWeight 只负责判断“现在是不是支撑相”。一旦进入锁定，
    // AnimGraph 必须完整执行按支撑平面求出的目标；如果继续把 0.83 之类的
    // 接触置信度直接当作 IK Alpha，最终姿势只是原动画和正确目标的中间态，
    // 斜坡上仍可能让脚跟或脚尖穿过平面。PositionLockAlpha 已经负责离地时
    // 的平滑释放，因此这里直接把它作为真正的解算权重。
    LeftFootState.Weight = LeftFootState.PositionLockAlpha;
    RightFootState.Weight = RightFootState.PositionLockAlpha;
    MaxLeftContactWeight = FMath::Max(
        MaxLeftContactWeight,
        LeftFootState.PositionLockAlpha);
    MaxRightContactWeight = FMath::Max(
        MaxRightContactWeight,
        RightFootState.PositionLockAlpha);
    LeftLockedFrameCount += LeftFootState.bPositionLocked ? 1 : 0;
    RightLockedFrameCount += RightFootState.bPositionLocked ? 1 : 0;

    if (bTraceRuntimeFrames)
    {
        UE_LOG(
            LogTemp,
            Display,
            TEXT("MOTIONBRIDGE_FOOT_IK_FRAME|time=%.4f|actor=%s|")
            TEXT("right_animated=%s|right_target=%s|right_locked_target=%s|")
            TEXT("right_desired=%.3f|right_weight=%.3f|right_grounded=%d|")
            TEXT("right_locked=%d|right_horizontal_speed=%.3f|right_clearance=%.3f"),
            ValidationElapsedSeconds,
            *CharacterOwner->GetActorLocation().ToCompactString(),
            *RightFootState.AnimatedLocation.ToCompactString(),
            *RightFootState.TargetLocation.ToCompactString(),
            *RightFootState.LockedTargetLocation.ToCompactString(),
            DesiredRightWeight,
            RightFootState.Weight,
            RightFootState.bGrounded ? 1 : 0,
            RightFootState.bPositionLocked ? 1 : 0,
            RightFootState.AnimatedHorizontalSpeed,
            RightFootState.SoleClearance);
    }

    // 骨盆只参考当前支撑脚。把摆动脚也纳入 Min 会导致骨盆被抬起的
    // 摆动脚向下拖拽，产生蹲伏和腿部过伸。
    float DesiredPelvisOffset = 0.0f;
    bool bHasSupportingFoot = false;
    if (DesiredLeftWeight > 0.5f)
    {
        DesiredPelvisOffset = LeftFootState.VerticalOffset;
        bHasSupportingFoot = true;
    }
    if (DesiredRightWeight > 0.5f)
    {
        DesiredPelvisOffset = bHasSupportingFoot
            ? FMath::Min(
                DesiredPelvisOffset,
                RightFootState.VerticalOffset)
            : RightFootState.VerticalOffset;
        bHasSupportingFoot = true;
    }
    DesiredPelvisOffset = bHasSupportingFoot
        ? FMath::Min(0.0f, DesiredPelvisOffset)
        : 0.0f;
    DesiredPelvisOffset *= PelvisOffsetScale;
    if (LeftFootState.bGrounded || RightFootState.bGrounded)
    {
        DesiredPelvisOffset += PelvisOffsetBias;
    }
    const float PositionAlpha =
        ExpSmoothingAlpha(PositionResponseSpeed, DeltaTime);
    PelvisOffset = FMath::Lerp(
        PelvisOffset,
        DesiredPelvisOffset,
        PositionAlpha);

    LeftGroundedFrameCount += LeftFootState.bGrounded ? 1 : 0;
    RightGroundedFrameCount += RightFootState.bGrounded ? 1 : 0;
    MaxLeftOffsetMagnitude = FMath::Max(
        MaxLeftOffsetMagnitude,
        FMath::Abs(LeftFootState.VerticalOffset));
    MaxRightOffsetMagnitude = FMath::Max(
        MaxRightOffsetMagnitude,
        FMath::Abs(RightFootState.VerticalOffset));
    MaxPelvisOffsetMagnitude = FMath::Max(
        MaxPelvisOffsetMagnitude,
        FMath::Abs(PelvisOffset));
    const auto GroundTiltDegrees = [](const FVector& Normal)
    {
        const float UpDot = FMath::Clamp(
            FVector::DotProduct(Normal.GetSafeNormal(), FVector::UpVector),
            -1.0f,
            1.0f);
        return FMath::RadiansToDegrees(FMath::Acos(UpDot));
    };
    if (LeftFootState.bGrounded)
    {
        MaxLeftGroundTiltDegrees = FMath::Max(
            MaxLeftGroundTiltDegrees,
            GroundTiltDegrees(LeftFootState.GroundNormal));
    }
    if (RightFootState.bGrounded)
    {
        MaxRightGroundTiltDegrees = FMath::Max(
            MaxRightGroundTiltDegrees,
            GroundTiltDegrees(RightFootState.GroundNormal));
    }
    // 跳过启动阶段的平滑过渡；稳定后记录左右脚目标地面的最大高度差。
    // 平地应接近 0，跨台阶时应接近台阶高度。
    if (ValidationElapsedSeconds >= 0.5f &&
        LeftFootState.bGrounded &&
        RightFootState.bGrounded)
    {
        MaxSettledTargetHeightDifference = FMath::Max(
            MaxSettledTargetHeightDifference,
            FMath::Abs(
                LeftFootState.TargetLocation.Z -
                RightFootState.TargetLocation.Z));
    }

    ValidationElapsedSeconds += DeltaTime;
    if (!bReportedRuntimeValidation && ValidationElapsedSeconds >= 2.0f)
    {
        bReportedRuntimeValidation = true;
        const bool bBothFeetHit =
            LeftGroundedFrameCount > 0 && RightGroundedFrameCount > 0;
        const bool bProducedCorrection =
            MaxLeftOffsetMagnitude > 0.1f ||
            MaxRightOffsetMagnitude > 0.1f ||
            MaxPelvisOffsetMagnitude > 0.1f;
        const bool bDetectedSupportPhases =
            LeftLockedFrameCount > 0 &&
            RightLockedFrameCount > 0 &&
            MaxLeftContactWeight >= FootLockEnterWeight &&
            MaxRightContactWeight >= FootLockEnterWeight;
        const bool bFinalSolesStayAboveGround =
            LeftPostIKSampleCount > 0 &&
            RightPostIKSampleCount > 0 &&
            MinLeftPostIKClearance >= -0.05f &&
            MinRightPostIKClearance >= -0.05f;
        float ExpectedHeightDifference = 0.0f;
        const bool bHasExpectedHeightDifference = FParse::Value(
            FCommandLine::Get(),
            TEXT("MotionBridgeExpectedFootHeightDiff="),
            ExpectedHeightDifference);
        const bool bHeightDifferencePassed =
            !bHasExpectedHeightDifference ||
            FMath::IsNearlyEqual(
                MaxSettledTargetHeightDifference,
                ExpectedHeightDifference,
                0.5f);
        const bool bRuntimePassed =
            bBothFeetHit &&
            bProducedCorrection &&
            bDetectedSupportPhases &&
            bFinalSolesStayAboveGround &&
            bHeightDifferencePassed;
        UE_LOG(
            LogTemp,
            Display,
            TEXT("MOTIONBRIDGE_RUNTIME_FOOT_IK=%s|left_hit_frames=%d|right_hit_frames=%d|left_locked_frames=%d|right_locked_frames=%d|max_left_weight=%.3f|max_right_weight=%.3f|left_post_ik_samples=%d|right_post_ik_samples=%d|min_left_post_ik_clearance=%.3f|min_right_post_ik_clearance=%.3f|max_left_support_horizontal_speed=%.3f|max_right_support_horizontal_speed=%.3f|max_left_support_slip=%.3f|max_right_support_slip=%.3f|min_left_normal_speed=%.3f|min_right_normal_speed=%.3f|min_left_clearance=%.3f|min_right_clearance=%.3f|max_left_offset=%.3f|max_right_offset=%.3f|max_pelvis_offset=%.3f|max_left_tilt_deg=%.3f|max_right_tilt_deg=%.3f|max_target_height_diff=%.3f|expected_height_diff=%s|actor_z=%.3f|left_foot=%s|right_foot=%s|left_target=%s|right_target=%s|trace_below=%.3f"),
            bRuntimePassed ? TEXT("PASS") : TEXT("FAIL"),
            LeftGroundedFrameCount,
            RightGroundedFrameCount,
            LeftLockedFrameCount,
            RightLockedFrameCount,
            MaxLeftContactWeight,
            MaxRightContactWeight,
            LeftPostIKSampleCount,
            RightPostIKSampleCount,
            LeftPostIKSampleCount > 0 ? MinLeftPostIKClearance : BIG_NUMBER,
            RightPostIKSampleCount > 0 ? MinRightPostIKClearance : BIG_NUMBER,
            MaxLeftSupportHorizontalSpeed,
            MaxRightSupportHorizontalSpeed,
            MaxLeftSupportSlipDistance,
            MaxRightSupportSlipDistance,
            MinLeftAnimatedSpeed,
            MinRightAnimatedSpeed,
            MinLeftSoleClearance,
            MinRightSoleClearance,
            MaxLeftOffsetMagnitude,
            MaxRightOffsetMagnitude,
            MaxPelvisOffsetMagnitude,
            MaxLeftGroundTiltDegrees,
            MaxRightGroundTiltDegrees,
            MaxSettledTargetHeightDifference,
            bHasExpectedHeightDifference
                ? *FString::Printf(TEXT("%.3f"), ExpectedHeightDifference)
                : TEXT("none"),
            CharacterOwner->GetActorLocation().Z,
            *LeftFootState.AnimatedLocation.ToCompactString(),
            *RightFootState.AnimatedLocation.ToCompactString(),
            *LeftFootState.TargetLocation.ToCompactString(),
            *RightFootState.TargetLocation.ToCompactString(),
            TraceBelowFoot);

        FString ScreenshotName;
        if (FParse::Value(
                FCommandLine::Get(),
                TEXT("MotionBridgeFootIKScreenshot="),
                ScreenshotName) &&
            !ScreenshotName.IsEmpty())
        {
            const FString ScreenshotDirectory =
                FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Screenshots"));
            IFileManager::Get().MakeDirectory(
                *ScreenshotDirectory,
                true);
            const FString ScreenshotPath = FPaths::Combine(
                ScreenshotDirectory,
                ScreenshotName);
            FScreenshotRequest::RequestScreenshot(
                ScreenshotPath,
                false,
                false);
            UE_LOG(
                LogTemp,
                Display,
                TEXT("MOTIONBRIDGE_FOOT_IK_SCREENSHOT=%s"),
                *ScreenshotPath);
        }
    }
}

void UMotionBridgeFootIKComponent::UpdateFootLock(
    const float DesiredContactWeight,
    const float DeltaTime,
    FMotionBridgeFootIKState& State)
{
    const bool bCanEnterLock =
        State.bGrounded && DesiredContactWeight >= FootLockEnterWeight;
    if (!State.bPositionLocked && bCanEnterLock)
    {
        // 记录进入支撑相时的目标。不要在这里把 Alpha 直接写成 1：
        // Two Bone IK 若在一帧内完全接管腿链，膝盖会出现可见的抽动。
        // 下方统一从当前 Alpha 平滑渐入。
        State.bPositionLocked = true;
        State.LockedTargetLocation = State.TargetLocation;
        State.LockedGroundNormal = State.GroundNormal;
        State.LockedTargetRotationWorld = State.TargetRotationWorld;
    }
    else if (
        State.bPositionLocked &&
        (!State.bGrounded || DesiredContactWeight <= FootLockExitWeight))
    {
        State.bPositionLocked = false;
    }

    const float TransitionResponseSpeed = State.bPositionLocked
        ? WeightResponseSpeed
        : FootLockReleaseResponseSpeed;
    const float TransitionAlpha =
        ExpSmoothingAlpha(TransitionResponseSpeed, DeltaTime);
    State.PositionLockAlpha = FMath::Lerp(
        State.PositionLockAlpha,
        State.bPositionLocked ? 1.0f : 0.0f,
        TransitionAlpha);

    if (State.PositionLockAlpha <= KINDA_SMALL_NUMBER)
    {
        State.PositionLockAlpha = 0.0f;
        return;
    }

    // 支撑面法线和脚掌朝向使用相同的渐入/渐出 Alpha，避免位置已经平滑、
    // 旋转却在接触帧突然切换。
    State.GroundNormal = FMath::Lerp(
        State.GroundNormal,
        State.LockedGroundNormal,
        State.PositionLockAlpha).GetSafeNormal();
    State.TargetRotationWorld = FQuat::Slerp(
        State.TargetRotationWorld,
        State.LockedTargetRotationWorld,
        State.PositionLockAlpha).GetNormalized();
}

void UMotionBridgeFootIKComponent::UpdateFoot(
    const FName BoneName,
    const FVector& HeelContactOffset,
    const FVector& ToeContactOffset,
    const FVector& SoleNormalLocal,
    const float DeltaTime,
    FMotionBridgeFootIKState& State,
    const FColor& DebugColor,
    const TCHAR* DebugLabel)
{
    // 绝不能从 GetSocketLocation 读取这里的脚位置：Mesh 已经完成本帧 IK，
    // 那会让上一帧输出重新成为下一帧输入，并把脚锁死在旧的世界坐标。
    // Observe Bone 位于 AnimGraph 的 IK 之前，提供真正的原动画脚位置。
    FTransform AnimatedTransform = FTransform::Identity;
    const UMotionBridgeFootIKAnimInstance* FootIKAnimInstance =
        Cast<UMotionBridgeFootIKAnimInstance>(CharacterMesh->GetAnimInstance());
    const bool bHasPreIKLocation =
        FootIKAnimInstance &&
        FootIKAnimInstance->TryGetPreIKBoneTransformWorld(
            BoneName,
            AnimatedTransform);
    if (!bHasPreIKLocation)
    {
        // 第一帧 Observe Bone 还没有执行时暂不施加 IK；下一帧即可拿到缓存。
        State.bGrounded = false;
        State.AnimatedLocation = CharacterMesh->GetSocketLocation(BoneName);
        State.TargetLocation = State.AnimatedLocation;
        State.AnimatedRotationWorld = FQuat::Identity;
        State.TargetRotationWorld = FQuat::Identity;
        State.GroundNormal = FVector::UpVector;
        State.VerticalOffset = 0.0f;
        State.Weight = 0.0f;
        State.AnimatedSpeed = 0.0f;
        State.AnimatedHorizontalSpeed = 0.0f;
        State.SoleClearance = BIG_NUMBER;
        State.bPositionLocked = false;
        State.PositionLockAlpha = 0.0f;
        State.bHasPreviousAnimatedLocation = false;
        return;
    }
    const FVector AnimatedLocation = AnimatedTransform.GetLocation();
    const FQuat AnimatedRotationWorld =
        AnimatedTransform.GetRotation().GetNormalized();
    const bool bHadPreviousAnimatedLocation =
        State.bHasPreviousAnimatedLocation && DeltaTime > SMALL_NUMBER;
    FVector AnimatedVelocityWorld = FVector::ZeroVector;
    if (bHadPreviousAnimatedLocation)
    {
        const FVector AnimatedDelta =
            AnimatedLocation - State.PreviousAnimatedLocation;
        AnimatedVelocityWorld = AnimatedDelta / DeltaTime;
        State.AnimatedHorizontalSpeed = AnimatedVelocityWorld.Size2D();
    }
    else
    {
        // 第一帧没有速度差分，先按摆动脚处理，避免在任意初始相位误锁。
        State.AnimatedSpeed = ContactSpeedFadeEnd;
        State.AnimatedHorizontalSpeed = 0.0f;
    }
    State.PreviousAnimatedLocation = AnimatedLocation;
    State.bHasPreviousAnimatedLocation = true;

    // 接触判定必须始终查看原动画姿势，不能使用已锁定的输出旋转；否则
    // 脚踝开始抬起时，旧旋转会掩盖离地信号，导致支撑锁永远不释放。
    const FVector HeelSampleLocation =
        AnimatedLocation + AnimatedRotationWorld.RotateVector(HeelContactOffset);
    const FVector ToeSampleLocation =
        AnimatedLocation + AnimatedRotationWorld.RotateVector(ToeContactOffset);

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(MotionBridgeFootIK), false);
    QueryParams.AddIgnoredActor(CharacterOwner);

    const auto TraceSample =
        [this, &QueryParams](const FVector& SampleLocation, FHitResult& OutHit)
        {
            const FVector Start =
                SampleLocation + FVector::UpVector * TraceAboveFoot;
            const FVector End =
                SampleLocation - FVector::UpVector * TraceBelowFoot;
            return GetWorld()->LineTraceSingleByChannel(
                OutHit,
                Start,
                End,
                TraceChannel,
                QueryParams);
        };

    FHitResult HeelHit;
    FHitResult ToeHit;
    const bool bHeelHit = TraceSample(HeelSampleLocation, HeelHit);
    const bool bToeHit = TraceSample(ToeSampleLocation, ToeHit);

    // 两个采样点都必须落在同一个可支撑平面上。若脚跟在台阶下、前掌在
    // 台阶上，刚性鞋底不可能同时贴住两面；此前取两者最大修正量会把整只
    // 脚抬起来，造成“前掌安全但后跟悬空”的假接触。
    bool bSameSupportPlane = false;
    FVector SupportPlaneNormal = FVector::UpVector;
    FVector SupportPlanePoint = FVector::ZeroVector;
    if (bHeelHit && bToeHit)
    {
        SupportPlaneNormal =
            (HeelHit.ImpactNormal + ToeHit.ImpactNormal).GetSafeNormal();
        SupportPlanePoint =
            0.5f * (HeelHit.ImpactPoint + ToeHit.ImpactPoint);
        const float PlaneSeparation = FMath::Abs(FVector::DotProduct(
            ToeHit.ImpactPoint - HeelHit.ImpactPoint,
            SupportPlaneNormal));
        bSameSupportPlane =
            PlaneSeparation <= SolePlaneTolerance &&
            FVector::DotProduct(
                HeelHit.ImpactNormal.GetSafeNormal(),
                ToeHit.ImpactNormal.GetSafeNormal()) >= 0.95f;
    }
    const bool bHit = bSameSupportPlane;

    if (bHit)
    {
        const float HeelClearance =
            HeelSampleLocation.Z - HeelHit.ImpactPoint.Z - ContactSafetyMargin;
        const float ToeClearance =
            ToeSampleLocation.Z - ToeHit.ImpactPoint.Z - ContactSafetyMargin;
        State.SoleClearance = FMath::Max(
            0.0f,
            FMath::Max(HeelClearance, ToeClearance));
    }
    else
    {
        State.SoleClearance = BIG_NUMBER;
    }

    const FVector DesiredNormal = bHit
        ? SupportPlaneNormal
        : FVector::UpVector;
    State.AnimatedSpeed = bHadPreviousAnimatedLocation
        ? FMath::Abs(FVector::DotProduct(
            AnimatedVelocityWorld,
            DesiredNormal))
        : ContactSpeedFadeEnd;
    const float PositionAlpha =
        ExpSmoothingAlpha(PositionResponseSpeed, DeltaTime);

    // 第一次命中时从动画脚位置开始平滑，避免默认零向量导致脚飞向原点。
    if (State.TargetLocation.IsNearlyZero() && State.AnimatedLocation.IsNearlyZero())
    {
        State.TargetLocation = AnimatedLocation;
        State.TargetRotationWorld = AnimatedRotationWorld;
    }

    State.bGrounded = bHit;
    State.SupportPlanePoint = SupportPlanePoint;
    State.AnimatedLocation = AnimatedLocation;
    State.AnimatedRotationWorld = AnimatedRotationWorld;
    State.GroundNormal = FMath::Lerp(
        State.GroundNormal,
        DesiredNormal,
        PositionAlpha).GetSafeNormal();

    // 动画里的脚踝在支撑期仍可能保持脚尖上翘。真正要对齐的不是世界 Up，
    // 而是“当前鞋底法线 -> 地面法线”；这样平地上也会把支撑脚放平。
    const FVector AnimatedSoleNormalWorld =
        AnimatedRotationWorld.RotateVector(SoleNormalLocal).GetSafeNormal();
    FQuat GroundTiltWorld = FQuat::FindBetweenNormals(
        AnimatedSoleNormalWorld,
        State.GroundNormal);
    FVector GroundTiltAxis = FVector::ForwardVector;
    float GroundTiltRadians = 0.0f;
    GroundTiltWorld.ToAxisAndAngle(GroundTiltAxis, GroundTiltRadians);
    GroundTiltRadians = FMath::Min(
        GroundTiltRadians,
        FMath::DegreesToRadians(MaxGroundTiltDegrees));
    const FQuat DesiredTargetRotationWorld = bHit
        ? FQuat(GroundTiltAxis.GetSafeNormal(), GroundTiltRadians) *
            AnimatedRotationWorld
        : AnimatedRotationWorld;
    State.TargetRotationWorld = FQuat::Slerp(
        State.TargetRotationWorld,
        DesiredTargetRotationWorld,
        PositionAlpha).GetNormalized();

    // foot 骨骼枢轴不在鞋底上。用 Manny 鞋底的后跟和鞋尖两个局部接触点，
    // 先按目标脚旋转预测它们的世界高度，再取两个采样点
    // 所需垂直修正的较大值，保证脚跟和脚尖都不穿过地面。
    const FQuat ContactRotationWorld = State.bPositionLocked
        ? State.LockedTargetRotationWorld
        : State.TargetRotationWorld;
    const FVector PredictedHeelLocation =
        AnimatedLocation + ContactRotationWorld.RotateVector(HeelContactOffset);
    const FVector PredictedToeLocation =
        AnimatedLocation + ContactRotationWorld.RotateVector(ToeContactOffset);

    // 旋转脚掌会同时改变接触点的 X/Y。斜坡的地面高度随 X/Y 变化，因此不能继续
    // 使用旋转前射线命中点的 Z；必须把旋转后的点沿世界 Up 投影到支撑平面。
    // 求 d 使 dot((P + d*Up) - PlanePoint, Normal) = SafetyMargin。
    const float UpDotPlaneNormal =
        FVector::DotProduct(FVector::UpVector, SupportPlaneNormal);
    const auto RequiredVerticalOffset =
        [bHit,
         UpDotPlaneNormal,
         &SupportPlaneNormal,
         &SupportPlanePoint,
         this](const FVector& PredictedContactLocation)
        {
            if (!bHit || FMath::Abs(UpDotPlaneNormal) <= KINDA_SMALL_NUMBER)
            {
                return -BIG_NUMBER;
            }
            const float SignedDistance = FVector::DotProduct(
                PredictedContactLocation - SupportPlanePoint,
                SupportPlaneNormal);
            return (ContactSafetyMargin - SignedDistance) /
                UpDotPlaneNormal;
        };
    const float HeelRequiredOffset =
        RequiredVerticalOffset(PredictedHeelLocation);
    const float ToeRequiredOffset =
        RequiredVerticalOffset(PredictedToeLocation);
    const float DesiredVerticalOffset = bHit
        ? FMath::Max(HeelRequiredOffset, ToeRequiredOffset)
        : 0.0f;
    // 平滑“相对动画的垂直修正量”，不能平滑绝对世界 Z。角色沿斜坡持续
    // 升高时，绝对 Z 的低通滤波会永远落后于地面，形成稳定的下陷误差。
    const float SmoothedVerticalOffset = FMath::Lerp(
        State.VerticalOffset,
        DesiredVerticalOffset,
        PositionAlpha);
    // “不能穿地”是硬约束：地面要求脚抬得更高时立即跟上；只有目标下降时
    // 才保留平滑，避免脚掌在噪声射线下抖动。
    State.VerticalOffset = bHit
        ? FMath::Max(SmoothedVerticalOffset, DesiredVerticalOffset)
        : SmoothedVerticalOffset;

    // 基础目标 X/Y 永远来自当前原动画姿势，只在 Z 方向叠加贴地修正。
    State.TargetLocation.X = AnimatedLocation.X;
    State.TargetLocation.Y = AnimatedLocation.Y;
    State.TargetLocation.Z =
        AnimatedLocation.Z + State.VerticalOffset;

    if (bDrawDebug)
    {
        const FColor TraceColor = bHit ? DebugColor : FColor::Yellow;
        const auto DrawSampleTrace =
            [this, &TraceColor](const FVector& SampleLocation)
            {
                DrawDebugLine(
                    GetWorld(),
                    SampleLocation + FVector::UpVector * TraceAboveFoot,
                    SampleLocation - FVector::UpVector * TraceBelowFoot,
                    TraceColor,
                    false,
                    0.0f,
                    0,
                    1.5f);
            };
        DrawSampleTrace(HeelSampleLocation);
        DrawSampleTrace(ToeSampleLocation);
        DrawDebugSphere(
            GetWorld(),
            PredictedHeelLocation + FVector::UpVector * DesiredVerticalOffset,
            2.5f,
            8,
            DebugColor,
            false,
            0.0f);
        DrawDebugSphere(
            GetWorld(),
            PredictedToeLocation + FVector::UpVector * DesiredVerticalOffset,
            2.5f,
            8,
            DebugColor,
            false,
            0.0f);
        DrawDebugSphere(
            GetWorld(),
            State.TargetLocation,
            4.0f,
            8,
            DebugColor,
            false,
            0.0f);
        DrawDebugString(
            GetWorld(),
            State.TargetLocation + FVector::UpVector * 8.0f,
            DebugLabel,
            nullptr,
            DebugColor,
            0.0f,
            true);
        if (bHit)
        {
            DrawDebugDirectionalArrow(
                GetWorld(),
                SupportPlanePoint,
                SupportPlanePoint + State.GroundNormal * 25.0f,
                6.0f,
                DebugColor,
                false,
                0.0f,
                0,
                1.5f);
        }
    }
}

float UMotionBridgeFootIKComponent::ExpSmoothingAlpha(
    const float ResponseSpeed,
    const float DeltaTime)
{
    return 1.0f - FMath::Exp(-ResponseSpeed * DeltaTime);
}
