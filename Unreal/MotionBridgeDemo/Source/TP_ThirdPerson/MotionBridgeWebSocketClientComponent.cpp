#include "MotionBridgeWebSocketClientComponent.h"

#include "MotionBridgeFootIKAnimInstance.h"
#include "MotionBridgeReceiverComponent.h"

#include "Animation/PoseSnapshot.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "IWebSocket.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "WebSocketsModule.h"

namespace
{
constexpr int32 MaxPendingWebSocketMessages = 512;
constexpr int32 MaxWebSocketMessageCharacters = 1024 * 1024;
}


UMotionBridgeWebSocketClientComponent::UMotionBridgeWebSocketClientComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}


void UMotionBridgeWebSocketClientComponent::BeginPlay()
{
    Super::BeginPlay();

    ReceiverComponent = GetOwner()
        ? GetOwner()->FindComponentByClass<UMotionBridgeReceiverComponent>()
        : nullptr;
    if (ReceiverComponent)
    {
        ReceiverComponent->AddTickPrerequisiteComponent(this);
    }

    FString CommandLineUrl;
    if (FParse::Value(
            FCommandLine::Get(),
            TEXT("MotionBridgeWebSocketUrl="),
            CommandLineUrl) &&
        !CommandLineUrl.IsEmpty())
    {
        WebSocketUrl = MoveTemp(CommandLineUrl);
    }

    bRunWebSocketTest = FParse::Param(
        FCommandLine::Get(),
        TEXT("MotionBridgeWebSocketTest"));
    bRunWebSocketReconnectTest = FParse::Param(
        FCommandLine::Get(),
        TEXT("MotionBridgeWebSocketReconnectTest"));
    bEnabled.store(
        bAutoConnect ||
        FParse::Param(FCommandLine::Get(), TEXT("MotionBridgeEnableWebSocket")) ||
        FParse::Param(FCommandLine::Get(), TEXT("MotionBridgeFacialDemo")) ||
        FParse::Param(FCommandLine::Get(), TEXT("MotionBridgeMorphTargetTest")) ||
        bRunWebSocketTest ||
        bRunWebSocketReconnectTest);
    if (bEnabled.load())
    {
        Connect();
    }
}


void UMotionBridgeWebSocketClientComponent::EndPlay(
    const EEndPlayReason::Type EndPlayReason)
{
    bShuttingDown.store(true);
    Disconnect();
    FString Discarded;
    while (PendingMessages.Dequeue(Discarded))
    {
        PendingMessageCount.fetch_sub(1);
    }
    Super::EndPlay(EndPlayReason);
}


void UMotionBridgeWebSocketClientComponent::TickComponent(
    const float DeltaTime,
    const ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (bConnectionStateDirty.exchange(false))
    {
        if (ReceiverComponent)
        {
            ReceiverComponent->SetWebSocketConnected(
                bCallbackConnected.load());
        }
    }

    DrainMessages();
    UpdateWebSocketTest(DeltaTime);

    if (!bEnabled.load() || bShuttingDown.load() ||
        !bReconnectRequested.load())
    {
        return;
    }

    ReconnectElapsedSeconds += DeltaTime;
    if (ReconnectElapsedSeconds >= FMath::Max(ReconnectDelaySeconds, 0.1f))
    {
        ReconnectElapsedSeconds = 0.0f;
        bReconnectRequested.store(false);
        Connect();
    }
}


void UMotionBridgeWebSocketClientComponent::Connect()
{
    if (bShuttingDown.load() || WebSocketUrl.IsEmpty())
    {
        return;
    }
    bEnabled.store(true);
    if (Socket.IsValid() && Socket->IsConnected())
    {
        return;
    }

    Socket.Reset();
    FWebSocketsModule& Module =
        FModuleManager::LoadModuleChecked<FWebSocketsModule>(TEXT("WebSockets"));
    Socket = Module.CreateWebSocket(WebSocketUrl);
    if (!Socket.IsValid())
    {
        bReconnectRequested.store(true);
        return;
    }

    if (ConnectionAttemptCount > 0 && ReceiverComponent)
    {
        ReceiverComponent->RecordWebSocketReconnectAttempt();
    }
    ++ConnectionAttemptCount;
    BindSocketCallbacks();
    UE_LOG(
        LogTemp,
        Display,
        TEXT("MOTIONBRIDGE_WEBSOCKET_CONNECTING|url=%s|attempt=%d"),
        *WebSocketUrl,
        ConnectionAttemptCount);
    Socket->Connect();
}


void UMotionBridgeWebSocketClientComponent::Disconnect()
{
    bEnabled.store(false);
    bReconnectRequested.store(false);
    if (Socket.IsValid())
    {
        Socket->Close(1000, TEXT("MotionBridge component stopped"));
        Socket.Reset();
    }
    bCallbackConnected.store(false);
    bConnectionStateDirty.store(true);
    if (ReceiverComponent && IsInGameThread())
    {
        ReceiverComponent->SetWebSocketConnected(false);
    }
}


bool UMotionBridgeWebSocketClientComponent::IsConnected() const
{
    return Socket.IsValid() && Socket->IsConnected();
}


void UMotionBridgeWebSocketClientComponent::BindSocketCallbacks()
{
    Socket->OnConnected().AddUObject(
        this,
        &UMotionBridgeWebSocketClientComponent::HandleConnected);
    Socket->OnConnectionError().AddUObject(
        this,
        &UMotionBridgeWebSocketClientComponent::HandleConnectionError);
    Socket->OnClosed().AddUObject(
        this,
        &UMotionBridgeWebSocketClientComponent::HandleClosed);
    Socket->OnMessage().AddUObject(
        this,
        &UMotionBridgeWebSocketClientComponent::HandleMessage);
}


void UMotionBridgeWebSocketClientComponent::DrainMessages()
{
    if (!ReceiverComponent)
    {
        return;
    }

    FString Message;
    const int32 SafeLimit = FMath::Clamp(MaxMessagesPerTick, 1, 1024);
    for (int32 Index = 0;
         Index < SafeLimit && PendingMessages.Dequeue(Message);
         ++Index)
    {
        PendingMessageCount.fetch_sub(1);
        if (!ReceiverComponent->SubmitJsonPacket(
                Message,
                EMotionBridgeTransport::WebSocket))
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("MOTIONBRIDGE_WEBSOCKET_MESSAGE=REJECTED"));
        }
    }

    const int64 DroppedCount = DroppedMessageCount.exchange(0);
    if (DroppedCount > 0)
    {
        ReceiverComponent->RecordDroppedIngressMessages(DroppedCount);
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("MOTIONBRIDGE_WEBSOCKET_INGRESS_DROPPED=%lld"),
            DroppedCount);
    }
}


void UMotionBridgeWebSocketClientComponent::HandleConnected()
{
    bCallbackConnected.store(true);
    bConnectionStateDirty.store(true);
    bReconnectRequested.store(false);
    UE_LOG(LogTemp, Display, TEXT("MOTIONBRIDGE_WEBSOCKET_CONNECTED=PASS"));
}


void UMotionBridgeWebSocketClientComponent::HandleConnectionError(
    const FString& Error)
{
    bCallbackConnected.store(false);
    bConnectionStateDirty.store(true);
    if (!bShuttingDown.load() && bEnabled.load())
    {
        bReconnectRequested.store(true);
    }
    UE_LOG(
        LogTemp,
        Warning,
        TEXT("MOTIONBRIDGE_WEBSOCKET_ERROR=%s"),
        *Error);
}


void UMotionBridgeWebSocketClientComponent::HandleClosed(
    const int32 StatusCode,
    const FString& Reason,
    const bool bWasClean)
{
    bCallbackConnected.store(false);
    bConnectionStateDirty.store(true);
    if (!bShuttingDown.load() && bEnabled.load())
    {
        bReconnectRequested.store(true);
    }
    UE_LOG(
        LogTemp,
        Display,
        TEXT("MOTIONBRIDGE_WEBSOCKET_CLOSED|code=%d|clean=%d|reason=%s"),
        StatusCode,
        bWasClean,
        *Reason);
}


void UMotionBridgeWebSocketClientComponent::HandleMessage(
    const FString& Message)
{
    if (Message.Len() > MaxWebSocketMessageCharacters)
    {
        DroppedMessageCount.fetch_add(1);
        return;
    }

    const int32 PreviousCount = PendingMessageCount.fetch_add(1);
    if (PreviousCount >= MaxPendingWebSocketMessages)
    {
        PendingMessageCount.fetch_sub(1);
        DroppedMessageCount.fetch_add(1);
        return;
    }
    PendingMessages.Enqueue(Message);
}


void UMotionBridgeWebSocketClientComponent::UpdateWebSocketTest(
    const float DeltaTime)
{
    if ((!bRunWebSocketTest && !bRunWebSocketReconnectTest) ||
        bWebSocketTestReported ||
        !ReceiverComponent)
    {
        return;
    }

    WebSocketTestElapsedSeconds += DeltaTime;
    const float RequiredDuration = bRunWebSocketReconnectTest ? 6.0f : 2.5f;
    if (WebSocketTestElapsedSeconds < RequiredDuration)
    {
        return;
    }

    FMotionBridgeStreamStats Stats;
    ReceiverComponent->GetStreamStats(Stats);
    FMotionBridgeStreamDefinition StreamDefinition;
    ReceiverComponent->TryGetStreamDefinition(StreamDefinition);

    const ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner());
    const USkeletalMeshComponent* Mesh = CharacterOwner
        ? CharacterOwner->GetMesh()
        : nullptr;
    const UMotionBridgeFootIKAnimInstance* MotionAnimInstance = Mesh
        ? Cast<UMotionBridgeFootIKAnimInstance>(Mesh->GetAnimInstance())
        : nullptr;
    const FPoseSnapshot* Snapshot = MotionAnimInstance
        ? &MotionAnimInstance->GetRealtimePoseSnapshot()
        : nullptr;
    const int32 SnapshotBoneCount = Snapshot
        ? Snapshot->BoneNames.Num()
        : 0;
    float SpineErrorDegrees = BIG_NUMBER;
    if (Mesh && Snapshot && Snapshot->bIsValid &&
        Snapshot->BoneNames.Num() == Snapshot->LocalTransforms.Num())
    {
        const int32 SpineIndex = Snapshot->BoneNames.IndexOfByKey(
            FName(TEXT("spine_01")));
        if (Snapshot->LocalTransforms.IsValidIndex(SpineIndex))
        {
            const FQuat ActualSpine = Mesh->GetBoneTransform(
                FName(TEXT("spine_01")),
                RTS_ParentBoneSpace).GetRotation().GetNormalized();
            const FQuat ExpectedSpine =
                Snapshot->LocalTransforms[SpineIndex].GetRotation().GetNormalized();
            SpineErrorDegrees = FMath::RadiansToDegrees(
                ActualSpine.AngularDistance(ExpectedSpine));
        }
    }
    const bool bDirectMannyPoseApplied =
        MotionAnimInstance &&
        MotionAnimInstance->IsRealtimePoseValid() &&
        Snapshot &&
        Snapshot->bIsValid &&
        SnapshotBoneCount >= 80 &&
        SpineErrorDegrees <= 0.5f;
    const bool bRuntimeRetargetApplied =
        MotionAnimInstance &&
        MotionAnimInstance->IsRuntimeRetargetValid() &&
        StreamDefinition.BoneNames.Num() >= 20 &&
        StreamDefinition.BoneNames.Contains(TEXT("MotionRoot")) &&
        StreamDefinition.BoneNames.Contains(TEXT("Hips")) &&
        !StreamDefinition.BoneNames.Contains(TEXT("pelvis"));
    const bool bRealtimePoseApplied =
        bDirectMannyPoseApplied || bRuntimeRetargetApplied;
    if (bRunWebSocketReconnectTest &&
        (Stats.WebSocketReconnectAttempts < 2 ||
         !Stats.bWebSocketConnected ||
         Stats.AcceptedFrames < 15))
    {
        return;
    }
    const bool bBasePassed =
        Stats.bWebSocketConnected &&
        Stats.ReceivedWebSocketMessages >= 31 &&
        Stats.RejectedWebSocketMessages == 0 &&
        Stats.AcceptedFrames >= 30 &&
        Stats.EndToEndLatencySamples >= 30 &&
        Stats.IncomingFrameRateHz >= 20.0 &&
        Stats.IncomingFrameRateHz <= 75.0 &&
        Stats.GameThreadFps > 1.0 &&
        Stats.AverageEndToEndLatencyMs < 500.0 &&
        bRealtimePoseApplied;
    const bool bReconnectPassed =
        Stats.bWebSocketConnected &&
        Stats.ReceivedWebSocketMessages >= 100 &&
        Stats.RejectedWebSocketMessages == 0 &&
        Stats.AcceptedFrames >= 15 &&
        Stats.EndToEndLatencySamples >= 15 &&
        Stats.AverageEndToEndLatencyMs < 500.0 &&
        Stats.WebSocketReconnectAttempts >= 2 &&
        bRealtimePoseApplied;
    const bool bPassed = bRunWebSocketReconnectTest
        ? bReconnectPassed
        : bBasePassed;
    bWebSocketTestReported = true;
    UE_LOG(
        LogTemp,
        Display,
        TEXT("MOTIONBRIDGE_%s=%s|messages=%lld|rejected=%lld|accepted=%lld|input_hz=%.2f|game_fps=%.2f|latency_latest_ms=%.2f|latency_average_ms=%.2f|latency_max_ms=%.2f|latency_samples=%lld|reconnects=%lld|connected=%d|pose_applied=%d|direct_manny_pose=%d|runtime_retarget=%d|stream_bones=%d|snapshot_bones=%d|spine_error_deg=%.3f"),
        bRunWebSocketReconnectTest
            ? TEXT("WEBSOCKET_RECONNECT_TEST")
            : TEXT("WEBSOCKET_TEST"),
        bPassed ? TEXT("PASS") : TEXT("FAIL"),
        Stats.ReceivedWebSocketMessages,
        Stats.RejectedWebSocketMessages,
        Stats.AcceptedFrames,
        Stats.IncomingFrameRateHz,
        Stats.GameThreadFps,
        Stats.LatestEndToEndLatencyMs,
        Stats.AverageEndToEndLatencyMs,
        Stats.MaxEndToEndLatencyMs,
        Stats.EndToEndLatencySamples,
        Stats.WebSocketReconnectAttempts,
        Stats.bWebSocketConnected,
        bRealtimePoseApplied,
        bDirectMannyPoseApplied,
        bRuntimeRetargetApplied,
        StreamDefinition.BoneNames.Num(),
        SnapshotBoneCount,
        SpineErrorDegrees);
    GLog->FlushThreadedLogs();
    if (FParse::Param(
            FCommandLine::Get(),
            TEXT("MotionBridgeExitAfterTest")))
    {
        FPlatformMisc::RequestExit(false);
    }
}
