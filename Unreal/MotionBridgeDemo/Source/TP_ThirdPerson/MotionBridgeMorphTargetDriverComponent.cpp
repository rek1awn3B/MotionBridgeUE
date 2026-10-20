#include "MotionBridgeMorphTargetDriverComponent.h"

#include "MotionBridgeReceiverComponent.h"

#include "Animation/MorphTarget.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"


UMotionBridgeMorphTargetDriverComponent::
    UMotionBridgeMorphTargetDriverComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}


void UMotionBridgeMorphTargetDriverComponent::BeginPlay()
{
    Super::BeginPlay();
    ReceiverComponent = GetOwner()
        ? GetOwner()->FindComponentByClass<UMotionBridgeReceiverComponent>()
        : nullptr;
    SkeletalMeshComponent = GetOwner()
        ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>()
        : nullptr;
    if (ReceiverComponent)
    {
        AddTickPrerequisiteComponent(ReceiverComponent);
    }
    bRunMorphTargetTest = FParse::Param(
        FCommandLine::Get(),
        TEXT("MotionBridgeMorphTargetTest"));
}


void UMotionBridgeMorphTargetDriverComponent::TickComponent(
    const float DeltaTime,
    const ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    ApplySample(DeltaTime);
    UpdateMorphTargetTest(DeltaTime);
}


void UMotionBridgeMorphTargetDriverComponent::ApplySample(
    const float DeltaTime)
{
    if (!ReceiverComponent || !SkeletalMeshComponent)
    {
        return;
    }

    FMotionBridgeStreamDefinition Definition;
    FMotionBridgeSampleResult Sample;
    if (!ReceiverComponent->TryGetStreamDefinition(Definition) ||
        !ReceiverComponent->TryGetFreshPlaybackSample(Sample) ||
        Definition.ChannelNames.Num() != Sample.Frame.Channels.Num())
    {
        return;
    }

    USkeletalMesh* MeshAsset =
        SkeletalMeshComponent->GetSkeletalMeshAsset();
    if (!MeshAsset)
    {
        return;
    }

    for (int32 Index = 0; Index < Definition.ChannelNames.Num(); ++Index)
    {
        const FName ChannelName = Definition.ChannelNames[Index];
        const FName* ExplicitTarget = ChannelToMorphTarget.Find(ChannelName);
        const FName MorphTargetName = ExplicitTarget
            ? *ExplicitTarget
            : ChannelName;
        if (!MeshAsset->FindMorphTarget(MorphTargetName))
        {
            if (!MissingMorphTargets.Contains(MorphTargetName))
            {
                MissingMorphTargets.Add(MorphTargetName);
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("MOTIONBRIDGE_MORPH_MISSING=%s"),
                    *MorphTargetName.ToString());
            }
            continue;
        }

        const float TargetWeight = FMath::Clamp(
            Sample.Frame.Channels[Index],
            0.0f,
            1.0f);
        const float PreviousWeight = CurrentWeights.FindRef(MorphTargetName);
        const float NewWeight = InterpolationSpeed <= 0.0f
            ? TargetWeight
            : FMath::FInterpTo(
                PreviousWeight,
                TargetWeight,
                DeltaTime,
                InterpolationSpeed);
        CurrentWeights.Add(MorphTargetName, NewWeight);
        MaximumObservedWeights.FindOrAdd(MorphTargetName) = FMath::Max(
            MaximumObservedWeights.FindRef(MorphTargetName),
            NewWeight);
        SkeletalMeshComponent->SetMorphTarget(MorphTargetName, NewWeight);
    }
}


void UMotionBridgeMorphTargetDriverComponent::UpdateMorphTargetTest(
    const float DeltaTime)
{
    if (!bRunMorphTargetTest || bMorphTargetTestReported)
    {
        return;
    }

    MorphTargetTestElapsedSeconds += DeltaTime;
    if (MorphTargetTestElapsedSeconds < 3.0f)
    {
        return;
    }

    const float Smile = MaximumObservedWeights.FindRef(TEXT("Smile"));
    const float JawOpen = MaximumObservedWeights.FindRef(TEXT("JawOpen"));
    const float BlinkLeft = MaximumObservedWeights.FindRef(TEXT("BlinkLeft"));
    const float BlinkRight = MaximumObservedWeights.FindRef(TEXT("BlinkRight"));
    const bool bPassed =
        MissingMorphTargets.IsEmpty() &&
        CurrentWeights.Num() >= 4 &&
        Smile >= 0.65f &&
        JawOpen >= 0.20f &&
        BlinkLeft >= 0.45f &&
        BlinkRight >= 0.45f;
    bMorphTargetTestReported = true;
    UE_LOG(
        LogTemp,
        Display,
        TEXT("MOTIONBRIDGE_MORPH_TARGET_TEST=%s|mapped=%d|missing=%d|smile=%.3f|jaw_open=%.3f|blink_left=%.3f|blink_right=%.3f"),
        bPassed ? TEXT("PASS") : TEXT("FAIL"),
        CurrentWeights.Num(),
        MissingMorphTargets.Num(),
        Smile,
        JawOpen,
        BlinkLeft,
        BlinkRight);
    if (bPassed && FParse::Param(
            FCommandLine::Get(),
            TEXT("MotionBridgeCaptureMorph")))
    {
        const FString ScreenshotDirectory = FPaths::Combine(
            FPaths::ProjectSavedDir(),
            TEXT("Screenshots/WindowsEditor"));
        IFileManager::Get().MakeDirectory(*ScreenshotDirectory, true);
        const FString ScreenshotPath = FPaths::Combine(
            ScreenshotDirectory,
            TEXT("MotionBridgeMorphTargetLive.png"));
        FScreenshotRequest::RequestScreenshot(
            ScreenshotPath,
            true,
            false);
        UE_LOG(
            LogTemp,
            Display,
            TEXT("MOTIONBRIDGE_MORPH_SCREENSHOT=REQUESTED|path=%s"),
            *ScreenshotPath);
    }
    GLog->FlushThreadedLogs();
    if (FParse::Param(
            FCommandLine::Get(),
            TEXT("MotionBridgeExitAfterTest")))
    {
        FPlatformMisc::RequestExit(false);
    }
}
