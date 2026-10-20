#include "MotionBridgeReceiverComponent.h"

#include "Algo/BinarySearch.h"
#include "Common/UdpSocketBuilder.h"
#include "Common/UdpSocketReceiver.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "HAL/PlatformTime.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SocketSubsystem.h"


namespace
{
constexpr uint64 MotionBridgeOverlayMessageKey = 0x4D4F54494F4EULL;
constexpr int32 MaxJsonMessageCharacters = 1024 * 1024;
constexpr int32 MaxPendingUdpPackets = 512;
constexpr int32 MaxProtocolBones = 512;
constexpr int32 MaxProtocolChannels = 512;
constexpr int32 MaxProtocolNameCharacters = 128;
constexpr int32 MaxStreamIdCharacters = 128;
constexpr int32 MaxRememberedFrameIndices = 4096;

bool TryReadInt32Field(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* FieldName,
    int32& OutValue,
    const bool bRequireNonNegative)
{
    double Number = 0.0;
    if (!Object->TryGetNumberField(FieldName, Number) ||
        !FMath::IsFinite(Number) ||
        Number < static_cast<double>(MIN_int32) ||
        Number > static_cast<double>(MAX_int32) ||
        FMath::RoundToDouble(Number) != Number ||
        (bRequireNonNegative && Number < 0.0))
    {
        return false;
    }

    OutValue = static_cast<int32>(Number);
    return true;
}

bool IsValidProtocolName(const FString& Name)
{
    return !Name.IsEmpty() && Name.Len() <= MaxProtocolNameCharacters;
}

bool IsBefore(
    const FMotionBridgeFrame& Left,
    const FMotionBridgeFrame& Right)
{
    if (Left.TimestampSeconds != Right.TimestampSeconds)
    {
        return Left.TimestampSeconds < Right.TimestampSeconds;
    }
    return Left.FrameIndex < Right.FrameIndex;
}


FMotionBridgeFrame InterpolateFrame(
    const FMotionBridgeFrame& Lower,
    const FMotionBridgeFrame& Upper,
    const double PlaybackTimestampSeconds,
    const double Alpha)
{
    FMotionBridgeFrame Result;
    Result.FrameIndex = Lower.FrameIndex;
    Result.TimestampSeconds = PlaybackTimestampSeconds;
    Result.SenderUnixSeconds = FMath::Lerp(
        Lower.SenderUnixSeconds,
        Upper.SenderUnixSeconds,
        Alpha);
    Result.RootPosition = FMath::Lerp(
        Lower.RootPosition,
        Upper.RootPosition,
        Alpha);
    Result.RootRotation = FQuat::Slerp(
        Lower.RootRotation,
        Upper.RootRotation,
        Alpha).GetNormalized();

    Result.LocalBoneRotations.Reserve(Lower.LocalBoneRotations.Num());
    for (int32 Index = 0; Index < Lower.LocalBoneRotations.Num(); ++Index)
    {
        Result.LocalBoneRotations.Add(FQuat::Slerp(
            Lower.LocalBoneRotations[Index],
            Upper.LocalBoneRotations[Index],
            Alpha).GetNormalized());
    }

    if (Lower.LocalBoneTranslations.Num() == Upper.LocalBoneTranslations.Num())
    {
        Result.LocalBoneTranslations.Reserve(
            Lower.LocalBoneTranslations.Num());
        for (int32 Index = 0;
             Index < Lower.LocalBoneTranslations.Num();
             ++Index)
        {
            Result.LocalBoneTranslations.Add(FMath::Lerp(
                Lower.LocalBoneTranslations[Index],
                Upper.LocalBoneTranslations[Index],
                Alpha));
        }
    }

    Result.Channels.Reserve(Lower.Channels.Num());
    for (int32 Index = 0; Index < Lower.Channels.Num(); ++Index)
    {
        Result.Channels.Add(FMath::Lerp(
            Lower.Channels[Index],
            Upper.Channels[Index],
            static_cast<float>(Alpha)));
    }
    return Result;
}


bool ReadNumberArray(
    const TSharedPtr<FJsonObject>& Object,
    const TCHAR* FieldName,
    const int32 ExpectedCount,
    TArray<double>& OutValues)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Object->TryGetArrayField(FieldName, Values) ||
        !Values ||
        Values->Num() != ExpectedCount)
    {
        return false;
    }

    OutValues.Reset(ExpectedCount);
    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        double Number = 0.0;
        if (!Value.IsValid() || !Value->TryGetNumber(Number) ||
            !FMath::IsFinite(Number))
        {
            return false;
        }
        OutValues.Add(Number);
    }
    return true;
}


bool ReadQuaternion(
    const TArray<TSharedPtr<FJsonValue>>& Values,
    FQuat& OutQuaternion)
{
    if (Values.Num() != 4)
    {
        return false;
    }

    double Components[4]{};
    for (int32 Index = 0; Index < 4; ++Index)
    {
        if (!Values[Index].IsValid() ||
            !Values[Index]->TryGetNumber(Components[Index]) ||
            !FMath::IsFinite(Components[Index]))
        {
            return false;
        }
    }

    OutQuaternion = FQuat(
        Components[0],
        Components[1],
        Components[2],
        Components[3]);
    if (OutQuaternion.ContainsNaN() || OutQuaternion.SizeSquared() < UE_DOUBLE_SMALL_NUMBER)
    {
        return false;
    }
    OutQuaternion.Normalize();
    return true;
}
} // namespace


UMotionBridgeReceiverComponent::UMotionBridgeReceiverComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}


void UMotionBridgeReceiverComponent::BeginPlay()
{
    Super::BeginPlay();

    // 动画网格必须在本组件更新完播放快照后再求值，避免 AnimInstance 读到上一帧。
    if (AActor* Owner = GetOwner())
    {
        if (USkeletalMeshComponent* Mesh =
                Owner->FindComponentByClass<USkeletalMeshComponent>())
        {
            Mesh->AddTickPrerequisiteComponent(this);
        }
    }

    if (bEnableUdpReceiver ||
        FParse::Param(FCommandLine::Get(), TEXT("MotionBridgeEnableUdp")) ||
        FParse::Param(FCommandLine::Get(), TEXT("MotionBridgeUdpPoseTest")) ||
        FParse::Param(FCommandLine::Get(), TEXT("MotionBridgeUdpAnimationTest")) ||
        FParse::Param(FCommandLine::Get(), TEXT("MotionBridgeUdpFallbackTest")) ||
        FParse::Param(FCommandLine::Get(), TEXT("MotionBridgeUdpReconnectTest")))
    {
        StartUdpReceiver();
    }

    bShowDebugOverlay =
        bShowDebugOverlay ||
        FParse::Param(FCommandLine::Get(), TEXT("MotionBridgeDebugOverlay"));
}


void UMotionBridgeReceiverComponent::EndPlay(
    const EEndPlayReason::Type EndPlayReason)
{
    if (GEngine && bDebugOverlayWasVisible)
    {
        GEngine->RemoveOnScreenDebugMessage(MotionBridgeOverlayMessageKey);
    }
    StopUdpReceiver();
    Super::EndPlay(EndPlayReason);
}


void UMotionBridgeReceiverComponent::TickComponent(
    const float DeltaTime,
    const ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    DrainUdpPackets();
    UpdatePerformanceMetrics(DeltaTime);
    if (bAutoAdvancePlayback)
    {
        FMotionBridgeSampleResult NewSample;
        if (AdvancePlaybackAndSample(DeltaTime, NewSample))
        {
            FScopeLock Lock(&BufferMutex);
            LatestPlaybackSample = MoveTemp(NewSample);
            bHasLatestPlaybackSample = true;
        }
    }
    UpdateDebugOverlay(DeltaTime);
}


bool UMotionBridgeReceiverComponent::StartUdpReceiver()
{
    if (UdpReceiver || UdpSocket)
    {
        return true;
    }

    const uint16 SafePort = static_cast<uint16>(FMath::Clamp(UdpListenPort, 1, 65535));
    UdpSocket = FUdpSocketBuilder(TEXT("MotionBridgeUdpSocket"))
        .AsNonBlocking()
        .AsReusable()
        .BoundToEndpoint(FIPv4Endpoint(FIPv4Address::InternalLoopback, SafePort))
        .WithReceiveBufferSize(2 * 1024 * 1024);
    if (!UdpSocket)
    {
        UE_LOG(LogTemp, Error, TEXT("MOTIONBRIDGE_UDP_START=FAIL|port=%d"), SafePort);
        return false;
    }

    UdpReceiver = new FUdpSocketReceiver(
        UdpSocket,
        FTimespan::FromMilliseconds(10.0),
        TEXT("MotionBridgeUdpReceiver"));
    UdpReceiver->SetMaxReadBufferSize(1024 * 1024);
    UdpReceiver->OnDataReceived().BindLambda(
        [this](const FArrayReaderPtr& Data, const FIPv4Endpoint&)
        {
            if (!Data.IsValid() || Data->Num() <= 0 ||
                Data->Num() > MaxJsonMessageCharacters)
            {
                if (Data.IsValid() && Data->Num() > MaxJsonMessageCharacters)
                {
                    DroppedUdpPacketCount.fetch_add(1);
                }
                return;
            }
            const int32 PreviousCount = PendingUdpPacketCount.fetch_add(1);
            if (PreviousCount >= MaxPendingUdpPackets)
            {
                PendingUdpPacketCount.fetch_sub(1);
                DroppedUdpPacketCount.fetch_add(1);
                return;
            }
            const FUTF8ToTCHAR Converted(
                reinterpret_cast<const ANSICHAR*>(Data->GetData()),
                Data->Num());
            PendingUdpPackets.Enqueue(FString(Converted.Length(), Converted.Get()));
        });
    UdpReceiver->Start();
    UE_LOG(LogTemp, Display, TEXT("MOTIONBRIDGE_UDP_START=PASS|address=127.0.0.1|port=%d"), SafePort);
    return true;
}


void UMotionBridgeReceiverComponent::StopUdpReceiver()
{
    if (UdpReceiver)
    {
        UdpReceiver->Stop();
        delete UdpReceiver;
        UdpReceiver = nullptr;
    }
    if (UdpSocket)
    {
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(UdpSocket);
        UdpSocket = nullptr;
    }

    FString DiscardedPacket;
    while (PendingUdpPackets.Dequeue(DiscardedPacket))
    {
        PendingUdpPacketCount.fetch_sub(1);
    }
}


void UMotionBridgeReceiverComponent::DrainUdpPackets()
{
    const int32 SafeLimit = FMath::Clamp(MaxUdpPacketsPerTick, 1, 1024);
    FString Packet;
    for (int32 PacketIndex = 0;
         PacketIndex < SafeLimit && PendingUdpPackets.Dequeue(Packet);
         ++PacketIndex)
    {
        PendingUdpPacketCount.fetch_sub(1);
        const bool bAccepted = SubmitJsonPacket(
            Packet,
            EMotionBridgeTransport::Udp);
        if (!bAccepted)
        {
            UE_LOG(LogTemp, Warning, TEXT("MOTIONBRIDGE_UDP_PACKET=REJECTED"));
        }
    }

    const int64 DroppedCount = DroppedUdpPacketCount.exchange(0);
    if (DroppedCount > 0)
    {
        RecordDroppedIngressMessages(DroppedCount);
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("MOTIONBRIDGE_UDP_INGRESS_DROPPED=%lld"),
            DroppedCount);
    }
}


bool UMotionBridgeReceiverComponent::SubmitJsonPacket(
    const FString& Packet,
    const EMotionBridgeTransport Transport)
{
    check(IsInGameThread());
    const bool bAccepted = ParsePacketOnGameThread(Packet, Transport);
    FScopeLock Lock(&BufferMutex);
    if (Transport == EMotionBridgeTransport::Udp)
    {
        ++StreamStats.ReceivedUdpPackets;
        if (!bAccepted)
        {
            ++StreamStats.RejectedUdpPackets;
        }
    }
    else
    {
        ++StreamStats.ReceivedWebSocketMessages;
        if (!bAccepted)
        {
            ++StreamStats.RejectedWebSocketMessages;
        }
    }
    return bAccepted;
}


bool UMotionBridgeReceiverComponent::ParsePacketOnGameThread(
    const FString& Packet,
    const EMotionBridgeTransport Transport)
{
    if (Packet.IsEmpty() || Packet.Len() > MaxJsonMessageCharacters)
    {
        return false;
    }

    const TCHAR* TransportName = Transport == EMotionBridgeTransport::Udp
        ? TEXT("UDP")
        : TEXT("WEBSOCKET");
    TSharedPtr<FJsonObject> Object;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Packet);
    if (!FJsonSerializer::Deserialize(Reader, Object) || !Object.IsValid())
    {
        return false;
    }

    FString Type;
    if (!Object->TryGetStringField(TEXT("type"), Type))
    {
        return false;
    }

    if (Type == TEXT("stream"))
    {
        int32 ProtocolVersion = 0;
        FString StreamId;
        const TArray<TSharedPtr<FJsonValue>>* BoneValues = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* ChannelValues = nullptr;
        if (!TryReadInt32Field(
                Object,
                TEXT("protocol_version"),
                ProtocolVersion,
                true) ||
            !Object->TryGetStringField(TEXT("stream_id"), StreamId) ||
            StreamId.IsEmpty() ||
            StreamId.Len() > MaxStreamIdCharacters)
        {
            return false;
        }

        Object->TryGetArrayField(TEXT("bones"), BoneValues);
        Object->TryGetArrayField(TEXT("channels"), ChannelValues);
        if ((!BoneValues || BoneValues->IsEmpty()) &&
            (!ChannelValues || ChannelValues->IsEmpty()))
        {
            return false;
        }
        if ((BoneValues && BoneValues->Num() > MaxProtocolBones) ||
            (ChannelValues && ChannelValues->Num() > MaxProtocolChannels))
        {
            return false;
        }

        FMotionBridgeStreamDefinition Definition;
        Definition.ProtocolVersion = ProtocolVersion;
        Definition.StreamId = StreamId;
        if (BoneValues)
        {
            for (const TSharedPtr<FJsonValue>& BoneValue : *BoneValues)
            {
                FString BoneName;
                if (!BoneValue.IsValid() ||
                    !BoneValue->TryGetString(BoneName) ||
                    !IsValidProtocolName(BoneName))
                {
                    return false;
                }
                Definition.BoneNames.Add(FName(*BoneName));
            }
        }
        if (ChannelValues)
        {
            for (const TSharedPtr<FJsonValue>& ChannelValue : *ChannelValues)
            {
                FString ChannelName;
                if (!ChannelValue.IsValid() ||
                    !ChannelValue->TryGetString(ChannelName) ||
                    !IsValidProtocolName(ChannelName))
                {
                    return false;
                }
                Definition.ChannelNames.Add(FName(*ChannelName));
            }
        }

        const bool bConfigured = ConfigureStream(Definition);
        if (bConfigured)
        {
            UE_LOG(
                LogTemp,
                Display,
                TEXT("MOTIONBRIDGE_%s_STREAM=PASS|id=%s|bones=%d|channels=%d"),
                TransportName,
                *StreamId,
                Definition.BoneNames.Num(),
                Definition.ChannelNames.Num());
        }
        else
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("MOTIONBRIDGE_%s_STREAM=FAIL|id=%s|bones=%d|channels=%d"),
                TransportName,
                *StreamId,
                Definition.BoneNames.Num(),
                Definition.ChannelNames.Num());
        }
        return bConfigured;
    }

    if (Type != TEXT("frame"))
    {
        return false;
    }

    FMotionBridgeFrame Frame;
    FString FrameStreamId;
    FMotionBridgeStreamDefinition ActiveDefinition;
    if (!TryReadInt32Field(
            Object,
            TEXT("frame_index"),
            Frame.FrameIndex,
            true) ||
        !Object->TryGetNumberField(TEXT("timestamp"), Frame.TimestampSeconds) ||
        !Object->TryGetStringField(TEXT("stream_id"), FrameStreamId) ||
        !TryGetStreamDefinition(ActiveDefinition) ||
        FrameStreamId != ActiveDefinition.StreamId)
    {
        return false;
    }
    Object->TryGetNumberField(
        TEXT("sent_at_unix_seconds"),
        Frame.SenderUnixSeconds);

    const bool bHasSkeletalStream = !ActiveDefinition.BoneNames.IsEmpty();
    TArray<double> RootPosition;
    if (bHasSkeletalStream || Object->HasField(TEXT("root_position")))
    {
        if (!ReadNumberArray(Object, TEXT("root_position"), 3, RootPosition))
        {
            return false;
        }
        Frame.RootPosition = FVector(
            RootPosition[0],
            RootPosition[1],
            RootPosition[2]);
    }

    const TArray<TSharedPtr<FJsonValue>>* RootRotation = nullptr;
    if (bHasSkeletalStream || Object->HasField(TEXT("root_rotation")))
    {
        if (!Object->TryGetArrayField(TEXT("root_rotation"), RootRotation) ||
            !RootRotation ||
            !ReadQuaternion(*RootRotation, Frame.RootRotation))
        {
            return false;
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* BoneRotations = nullptr;
    if (Object->TryGetArrayField(TEXT("bone_rotations"), BoneRotations))
    {
        if (!BoneRotations ||
            BoneRotations->Num() != ActiveDefinition.BoneNames.Num() ||
            BoneRotations->Num() > MaxProtocolBones)
        {
            return false;
        }
        Frame.LocalBoneRotations.Reserve(BoneRotations->Num());
        for (const TSharedPtr<FJsonValue>& BoneRotationValue : *BoneRotations)
        {
            const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
            FQuat Rotation;
            if (!BoneRotationValue.IsValid() ||
                !BoneRotationValue->TryGetArray(Components) ||
                !Components ||
                !ReadQuaternion(*Components, Rotation))
            {
                return false;
            }
            Frame.LocalBoneRotations.Add(Rotation);
        }
    }
    else if (bHasSkeletalStream)
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* BoneTranslations = nullptr;
    if (Object->TryGetArrayField(TEXT("bone_translations"), BoneTranslations))
    {
        if (!BoneTranslations ||
            BoneTranslations->Num() != Frame.LocalBoneRotations.Num())
        {
            return false;
        }
        Frame.LocalBoneTranslations.Reserve(BoneTranslations->Num());
        for (const TSharedPtr<FJsonValue>& TranslationValue : *BoneTranslations)
        {
            const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
            if (!TranslationValue.IsValid() ||
                !TranslationValue->TryGetArray(Components) ||
                !Components ||
                Components->Num() != 3)
            {
                return false;
            }
            double Values[3]{};
            for (int32 Index = 0; Index < 3; ++Index)
            {
                if (!(*Components)[Index].IsValid() ||
                    !(*Components)[Index]->TryGetNumber(Values[Index]) ||
                    !FMath::IsFinite(Values[Index]))
                {
                    return false;
                }
            }
            Frame.LocalBoneTranslations.Add(FVector(
                Values[0],
                Values[1],
                Values[2]));
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* Channels = nullptr;
    if (Object->TryGetArrayField(TEXT("channels"), Channels))
    {
        if (!Channels ||
            Channels->Num() != ActiveDefinition.ChannelNames.Num() ||
            Channels->Num() > MaxProtocolChannels)
        {
            return false;
        }
        Frame.Channels.Reserve(Channels->Num());
        for (const TSharedPtr<FJsonValue>& ChannelValue : *Channels)
        {
            double Value = 0.0;
            if (!ChannelValue.IsValid() ||
                !ChannelValue->TryGetNumber(Value) ||
                !FMath::IsFinite(Value) ||
                Value < -static_cast<double>(MAX_flt) ||
                Value > static_cast<double>(MAX_flt))
            {
                return false;
            }
            Frame.Channels.Add(static_cast<float>(Value));
        }
    }

    const EMotionBridgePushResult Result = PushFrame(Frame);
    const bool bAccepted =
        Result == EMotionBridgePushResult::Inserted ||
        Result == EMotionBridgePushResult::InsertedOutOfOrder;
    if (!bAccepted)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("MOTIONBRIDGE_%s_FRAME=FAIL|index=%d|timestamp=%.6f|result=%d"),
            TransportName,
            Frame.FrameIndex,
            Frame.TimestampSeconds,
            static_cast<int32>(Result));
    }
    return bAccepted;
}


void UMotionBridgeReceiverComponent::PushDebugFrame(
    const FMotionBridgeFrame& Frame)
{
    PushFrame(Frame);
}


EMotionBridgePushResult UMotionBridgeReceiverComponent::PushFrame(
    const FMotionBridgeFrame& Frame)
{
    FScopeLock Lock(&BufferMutex);
    ++StreamStats.SubmittedFrames;
    const auto RejectFrame = [this](const EMotionBridgePushResult Result)
    {
        ++StreamStats.RejectedFrames;
        if (Result == EMotionBridgePushResult::DuplicateFrameIndex)
        {
            ++StreamStats.DuplicateFrames;
        }
        return Result;
    };

    if (!FMath::IsFinite(Frame.TimestampSeconds))
    {
        return RejectFrame(EMotionBridgePushResult::InvalidTimestamp);
    }

    if (ExpectedBoneCount != INDEX_NONE &&
        Frame.LocalBoneRotations.Num() != ExpectedBoneCount)
    {
        return RejectFrame(EMotionBridgePushResult::BoneCountMismatch);
    }
    if (!Frame.LocalBoneTranslations.IsEmpty() &&
        Frame.LocalBoneTranslations.Num() != Frame.LocalBoneRotations.Num())
    {
        return RejectFrame(EMotionBridgePushResult::BoneCountMismatch);
    }
    if (ExpectedChannelCount != INDEX_NONE &&
        Frame.Channels.Num() != ExpectedChannelCount)
    {
        return RejectFrame(EMotionBridgePushResult::ChannelCountMismatch);
    }

    if (ObservedFrameIndices.Contains(Frame.FrameIndex))
    {
        return RejectFrame(EMotionBridgePushResult::DuplicateFrameIndex);
    }

    if (ExpectedBoneCount == INDEX_NONE)
    {
        ExpectedBoneCount = Frame.LocalBoneRotations.Num();
    }
    if (ExpectedChannelCount == INDEX_NONE)
    {
        ExpectedChannelCount = Frame.Channels.Num();
    }

    const int32 InsertionIndex = Algo::LowerBound(
        FrameBuffer,
        Frame,
        IsBefore);
    const bool bOutOfOrder = InsertionIndex != FrameBuffer.Num();
    FrameBuffer.Insert(Frame, InsertionIndex);
    ObservedFrameIndices.Add(Frame.FrameIndex);
    ObservedFrameIndexOrder.Enqueue(Frame.FrameIndex);
    while (ObservedFrameIndices.Num() > MaxRememberedFrameIndices)
    {
        int32 OldestFrameIndex = INDEX_NONE;
        if (!ObservedFrameIndexOrder.Dequeue(OldestFrameIndex))
        {
            break;
        }
        ObservedFrameIndices.Remove(OldestFrameIndex);
    }
    ++StreamStats.AcceptedFrames;
    if (bOutOfOrder)
    {
        ++StreamStats.OutOfOrderFrames;
    }
    if (StreamStats.LowestFrameIndex == INDEX_NONE)
    {
        StreamStats.LowestFrameIndex = Frame.FrameIndex;
        StreamStats.HighestFrameIndex = Frame.FrameIndex;
    }
    else
    {
        StreamStats.LowestFrameIndex = FMath::Min(
            StreamStats.LowestFrameIndex,
            Frame.FrameIndex);
        StreamStats.HighestFrameIndex = FMath::Max(
            StreamStats.HighestFrameIndex,
            Frame.FrameIndex);
    }
    const int64 ObservedIndexRange =
        static_cast<int64>(StreamStats.HighestFrameIndex) -
        static_cast<int64>(StreamStats.LowestFrameIndex) + 1;
    StreamStats.EstimatedLostFrames = FMath::Max<int64>(
        ObservedIndexRange - StreamStats.AcceptedFrames,
        0);
    LastAcceptedFramePlatformSeconds = FPlatformTime::Seconds();
    bHasAcceptedFrameTime = true;

    if (Frame.SenderUnixSeconds > 0.0 &&
        FMath::IsFinite(Frame.SenderUnixSeconds))
    {
        const double NowUnixSeconds =
            (FDateTime::UtcNow() - FDateTime(1970, 1, 1)).GetTotalSeconds();
        const double LatencyMs = FMath::Max(
            (NowUnixSeconds - Frame.SenderUnixSeconds) * 1000.0,
            0.0);
        if (FMath::IsFinite(LatencyMs))
        {
            StreamStats.LatestEndToEndLatencyMs = LatencyMs;
            StreamStats.MaxEndToEndLatencyMs = FMath::Max(
                StreamStats.MaxEndToEndLatencyMs,
                LatencyMs);
            const double PreviousTotal =
                StreamStats.AverageEndToEndLatencyMs *
                static_cast<double>(StreamStats.EndToEndLatencySamples);
            ++StreamStats.EndToEndLatencySamples;
            StreamStats.AverageEndToEndLatencyMs =
                (PreviousTotal + LatencyMs) /
                static_cast<double>(StreamStats.EndToEndLatencySamples);
        }
    }

    const int32 SafeCapacity = FMath::Max(BufferCapacity, 1);
    const int32 Overflow = FrameBuffer.Num() - SafeCapacity;
    if (Overflow > 0)
    {
        FrameBuffer.RemoveAt(0, Overflow, EAllowShrinking::No);
    }

    return bOutOfOrder
        ? EMotionBridgePushResult::InsertedOutOfOrder
        : EMotionBridgePushResult::Inserted;
}


bool UMotionBridgeReceiverComponent::ConfigureStream(
    const FMotionBridgeStreamDefinition& Definition)
{
    if (Definition.ProtocolVersion != 1 ||
        Definition.StreamId.IsEmpty() ||
        (Definition.BoneNames.IsEmpty() &&
         Definition.ChannelNames.IsEmpty()))
    {
        return false;
    }

    TSet<FName> UniqueBoneNames;
    UniqueBoneNames.Reserve(Definition.BoneNames.Num());
    for (const FName BoneName : Definition.BoneNames)
    {
        if (BoneName.IsNone() || UniqueBoneNames.Contains(BoneName))
        {
            return false;
        }
        UniqueBoneNames.Add(BoneName);
    }


    TSet<FName> UniqueChannelNames;
    UniqueChannelNames.Reserve(Definition.ChannelNames.Num());
    for (const FName ChannelName : Definition.ChannelNames)
    {
        if (ChannelName.IsNone() ||
            UniqueChannelNames.Contains(ChannelName))
        {
            return false;
        }
        UniqueChannelNames.Add(ChannelName);
    }

    FScopeLock Lock(&BufferMutex);
    StreamDefinition = Definition;
    bHasStreamDefinition = true;
    ResetStateLocked(false);
    return true;
}


bool UMotionBridgeReceiverComponent::TryGetStreamDefinition(
    FMotionBridgeStreamDefinition& OutDefinition) const
{
    FScopeLock Lock(&BufferMutex);
    if (!bHasStreamDefinition)
    {
        return false;
    }

    OutDefinition = StreamDefinition;
    return true;
}


bool UMotionBridgeReceiverComponent::SampleFrameAtTimestamp(
    const double PlaybackTimestampSeconds,
    FMotionBridgeSampleResult& OutSample) const
{
    FScopeLock Lock(&BufferMutex);
    return SampleFrameAtTimestampLocked(
        PlaybackTimestampSeconds,
        OutSample);
}


bool UMotionBridgeReceiverComponent::SampleLatestFrameWithDelay(
    FMotionBridgeSampleResult& OutSample) const
{
    FScopeLock Lock(&BufferMutex);
    if (FrameBuffer.IsEmpty())
    {
        return false;
    }

    const double SafeDelay = FMath::Max(PlaybackDelaySeconds, 0.0);
    const double PlaybackTimestampSeconds =
        FrameBuffer.Last().TimestampSeconds - SafeDelay;
    return SampleFrameAtTimestampLocked(
        PlaybackTimestampSeconds,
        OutSample);
}


bool UMotionBridgeReceiverComponent::AdvancePlaybackAndSample(
    const double DeltaSeconds,
    FMotionBridgeSampleResult& OutSample)
{
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds < 0.0)
    {
        return false;
    }

    FScopeLock Lock(&BufferMutex);
    if (FrameBuffer.IsEmpty())
    {
        return false;
    }

    const double OldestTimestamp = FrameBuffer[0].TimestampSeconds;
    const double NewestTimestamp = FrameBuffer.Last().TimestampSeconds;
    const double SafeDelay = FMath::Max(PlaybackDelaySeconds, 0.0);
    const double TargetTimestamp = FMath::Max(
        NewestTimestamp - SafeDelay,
        OldestTimestamp);

    if (!bPlaybackClockInitialized)
    {
        CurrentPlaybackTimestampSeconds = TargetTimestamp;
        bPlaybackClockInitialized = true;
    }
    else
    {
        const double ClockError =
            TargetTimestamp - CurrentPlaybackTimestampSeconds;
        const double SafeHardResyncThreshold =
            FMath::Max(HardResyncThresholdSeconds, 0.0);

        if (FMath::Abs(ClockError) > SafeHardResyncThreshold)
        {
            // 重连或长时间卡顿后误差过大，直接回到理想缓冲位置。
            CurrentPlaybackTimestampSeconds = TargetTimestamp;
            ++StreamStats.HardResyncCount;
        }
        else
        {
            // 正常情况下以接近 1 倍速前进，只用少量变速逐渐消除时钟误差。
            const double SafeSpeedAdjustment = FMath::Clamp(
                MaxPlaybackSpeedAdjustment,
                0.0,
                1.0);
            const double MaxCorrectionThisTick =
                DeltaSeconds * SafeSpeedAdjustment;
            const double Correction = FMath::Clamp(
                ClockError,
                -MaxCorrectionThisTick,
                MaxCorrectionThisTick);
            CurrentPlaybackTimestampSeconds += DeltaSeconds + Correction;

            // 网络停止时不能播放到尚未收到的未来。
            CurrentPlaybackTimestampSeconds = FMath::Min(
                CurrentPlaybackTimestampSeconds,
                NewestTimestamp);
        }
    }

    StreamStats.CurrentBufferDelaySeconds = FMath::Max(
        NewestTimestamp - CurrentPlaybackTimestampSeconds,
        0.0);
    StreamStats.MaxBufferDelaySeconds = FMath::Max(
        StreamStats.MaxBufferDelaySeconds,
        StreamStats.CurrentBufferDelaySeconds);

    return SampleFrameAtTimestampLocked(
        CurrentPlaybackTimestampSeconds,
        OutSample);
}


bool UMotionBridgeReceiverComponent::SampleFrameAtTimestampLocked(
    const double PlaybackTimestampSeconds,
    FMotionBridgeSampleResult& OutSample) const
{
    if (FrameBuffer.IsEmpty() ||
        !FMath::IsFinite(PlaybackTimestampSeconds))
    {
        return false;
    }

    const FMotionBridgeFrame& Oldest = FrameBuffer[0];
    if (PlaybackTimestampSeconds <= Oldest.TimestampSeconds)
    {
        OutSample.Frame = Oldest;
        OutSample.LowerFrameIndex = Oldest.FrameIndex;
        OutSample.UpperFrameIndex = Oldest.FrameIndex;
        OutSample.Alpha = 0.0;
        OutSample.bClampedToBoundary = true;
        return true;
    }

    const FMotionBridgeFrame& Newest = FrameBuffer.Last();
    if (PlaybackTimestampSeconds >= Newest.TimestampSeconds)
    {
        OutSample.Frame = Newest;
        OutSample.LowerFrameIndex = Newest.FrameIndex;
        OutSample.UpperFrameIndex = Newest.FrameIndex;
        OutSample.Alpha = 0.0;
        OutSample.bClampedToBoundary = true;
        return true;
    }

    // 找到第一个时间戳严格晚于播放时间的帧。
    int32 First = 0;
    int32 Last = FrameBuffer.Num();
    while (First < Last)
    {
        const int32 Middle = First + (Last - First) / 2;
        if (FrameBuffer[Middle].TimestampSeconds <=
            PlaybackTimestampSeconds)
        {
            First = Middle + 1;
        }
        else
        {
            Last = Middle;
        }
    }

    const int32 UpperIndex = First;
    const int32 LowerIndex = UpperIndex - 1;
    const FMotionBridgeFrame& Lower = FrameBuffer[LowerIndex];
    const FMotionBridgeFrame& Upper = FrameBuffer[UpperIndex];
    const double Duration = Upper.TimestampSeconds - Lower.TimestampSeconds;
    if (Duration <= UE_DOUBLE_SMALL_NUMBER)
    {
        return false;
    }

    const double Alpha = FMath::Clamp(
        (PlaybackTimestampSeconds - Lower.TimestampSeconds) / Duration,
        0.0,
        1.0);
    OutSample.Frame = InterpolateFrame(
        Lower,
        Upper,
        PlaybackTimestampSeconds,
        Alpha);
    OutSample.LowerFrameIndex = Lower.FrameIndex;
    OutSample.UpperFrameIndex = Upper.FrameIndex;
    OutSample.Alpha = Alpha;
    OutSample.bClampedToBoundary = false;
    return true;
}


void UMotionBridgeReceiverComponent::ResetBuffer()
{
    FScopeLock Lock(&BufferMutex);
    ResetStateLocked(true);
}


void UMotionBridgeReceiverComponent::ResetStateLocked(
    const bool bClearStreamDefinition)
{
    const int64 PreservedUdpPackets = StreamStats.ReceivedUdpPackets;
    const int64 PreservedRejectedUdpPackets = StreamStats.RejectedUdpPackets;
    const int64 PreservedWebSocketMessages =
        StreamStats.ReceivedWebSocketMessages;
    const int64 PreservedRejectedWebSocketMessages =
        StreamStats.RejectedWebSocketMessages;
    const int64 PreservedDroppedIngressMessages =
        StreamStats.DroppedIngressMessages;
    const int64 PreservedReconnectAttempts =
        StreamStats.WebSocketReconnectAttempts;
    FrameBuffer.Reset();
    ExpectedChannelCount = INDEX_NONE;
    CurrentPlaybackTimestampSeconds = 0.0;
    bPlaybackClockInitialized = false;
    LatestPlaybackSample = FMotionBridgeSampleResult{};
    bHasLatestPlaybackSample = false;
    LastAcceptedFramePlatformSeconds = 0.0;
    bHasAcceptedFrameTime = false;
    StreamStats = FMotionBridgeStreamStats{};
    if (!bClearStreamDefinition)
    {
        StreamStats.ReceivedUdpPackets = PreservedUdpPackets;
        StreamStats.RejectedUdpPackets = PreservedRejectedUdpPackets;
        StreamStats.ReceivedWebSocketMessages =
            PreservedWebSocketMessages;
        StreamStats.RejectedWebSocketMessages =
            PreservedRejectedWebSocketMessages;
        StreamStats.DroppedIngressMessages =
            PreservedDroppedIngressMessages;
        StreamStats.WebSocketReconnectAttempts =
            PreservedReconnectAttempts;
    }
    ObservedFrameIndices.Reset();
    int32 DiscardedFrameIndex = INDEX_NONE;
    while (ObservedFrameIndexOrder.Dequeue(DiscardedFrameIndex))
    {
    }
    MetricsElapsedSeconds = 0.0;
    MetricsAcceptedFrameBaseline = 0;
    MetricsTickCount = 0;

    if (bClearStreamDefinition)
    {
        StreamDefinition = FMotionBridgeStreamDefinition{};
        bHasStreamDefinition = false;
        ExpectedBoneCount = INDEX_NONE;
    }
    else
    {
        ExpectedBoneCount = bHasStreamDefinition
            ? StreamDefinition.BoneNames.Num()
            : INDEX_NONE;
        ExpectedChannelCount = bHasStreamDefinition
            ? StreamDefinition.ChannelNames.Num()
            : INDEX_NONE;
    }
}


int32 UMotionBridgeReceiverComponent::GetBufferedFrameCount() const
{
    FScopeLock Lock(&BufferMutex);
    return FrameBuffer.Num();
}


bool UMotionBridgeReceiverComponent::TryGetLatestPlaybackSample(
    FMotionBridgeSampleResult& OutSample) const
{
    FScopeLock Lock(&BufferMutex);
    if (!bHasLatestPlaybackSample)
    {
        return false;
    }

    OutSample = LatestPlaybackSample;
    return true;
}


bool UMotionBridgeReceiverComponent::TryGetFreshPlaybackSample(
    FMotionBridgeSampleResult& OutSample) const
{
    FScopeLock Lock(&BufferMutex);
    if (!bHasLatestPlaybackSample ||
        !IsPlaybackStreamLiveLocked(FPlatformTime::Seconds()))
    {
        return false;
    }

    OutSample = LatestPlaybackSample;
    return true;
}


bool UMotionBridgeReceiverComponent::IsPlaybackStreamLive() const
{
    FScopeLock Lock(&BufferMutex);
    return IsPlaybackStreamLiveLocked(FPlatformTime::Seconds());
}


void UMotionBridgeReceiverComponent::GetStreamStats(
    FMotionBridgeStreamStats& OutStats) const
{
    FScopeLock Lock(&BufferMutex);
    OutStats = StreamStats;
    OutStats.BufferedFrames = FrameBuffer.Num();
    OutStats.bWebSocketConnected = bWebSocketConnected;
    OutStats.bStreamLive =
        IsPlaybackStreamLiveLocked(FPlatformTime::Seconds());
}


void UMotionBridgeReceiverComponent::SetWebSocketConnected(
    const bool bConnected)
{
    FScopeLock Lock(&BufferMutex);
    bWebSocketConnected = bConnected;
}


void UMotionBridgeReceiverComponent::RecordWebSocketReconnectAttempt()
{
    FScopeLock Lock(&BufferMutex);
    ++StreamStats.WebSocketReconnectAttempts;
}


void UMotionBridgeReceiverComponent::RecordDroppedIngressMessages(
    const int64 DroppedCount)
{
    if (DroppedCount <= 0)
    {
        return;
    }
    FScopeLock Lock(&BufferMutex);
    StreamStats.DroppedIngressMessages += DroppedCount;
}


void UMotionBridgeReceiverComponent::UpdatePerformanceMetrics(
    const double DeltaTime)
{
    if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0)
    {
        return;
    }

    FScopeLock Lock(&BufferMutex);
    MetricsElapsedSeconds += DeltaTime;
    ++MetricsTickCount;
    if (MetricsElapsedSeconds < 1.0)
    {
        return;
    }

    const int64 AcceptedSinceLastWindow = FMath::Max<int64>(
        StreamStats.AcceptedFrames - MetricsAcceptedFrameBaseline,
        0);
    StreamStats.IncomingFrameRateHz =
        static_cast<double>(AcceptedSinceLastWindow) /
        MetricsElapsedSeconds;
    StreamStats.GameThreadFps =
        static_cast<double>(MetricsTickCount) /
        MetricsElapsedSeconds;
    MetricsAcceptedFrameBaseline = StreamStats.AcceptedFrames;
    MetricsElapsedSeconds = 0.0;
    MetricsTickCount = 0;
}


FString UMotionBridgeReceiverComponent::BuildDebugOverlayText() const
{
    FMotionBridgeStreamStats Stats;
    GetStreamStats(Stats);
    const TCHAR* Status = Stats.bStreamLive
        ? TEXT("LIVE")
        : (Stats.AcceptedFrames > 0 ? TEXT("TIMED OUT") : TEXT("WAITING"));
    return FString::Printf(
        TEXT("MotionBridge / %s\n")
        TEXT("UDP       packets %lld   rejected %lld\n")
        TEXT("WebSocket messages %lld   rejected %lld   connected %d\n")
        TEXT("Frames    accepted %lld   rejected %lld\n")
        TEXT("Realtime  input %.1f Hz   game %.1f FPS   latency %.1f/%.1f ms\n")
        TEXT("Quality   lost %lld   duplicate %lld   out-of-order %lld\n")
        TEXT("Buffer    %d/%d frames   delay %.1f ms   max %.1f ms\n")
        TEXT("Recovery  hard resync %lld"),
        Status,
        Stats.ReceivedUdpPackets,
        Stats.RejectedUdpPackets,
        Stats.ReceivedWebSocketMessages,
        Stats.RejectedWebSocketMessages,
        Stats.bWebSocketConnected,
        Stats.AcceptedFrames,
        Stats.RejectedFrames,
        Stats.IncomingFrameRateHz,
        Stats.GameThreadFps,
        Stats.LatestEndToEndLatencyMs,
        Stats.AverageEndToEndLatencyMs,
        Stats.EstimatedLostFrames,
        Stats.DuplicateFrames,
        Stats.OutOfOrderFrames,
        Stats.BufferedFrames,
        FMath::Max(BufferCapacity, 1),
        Stats.CurrentBufferDelaySeconds * 1000.0,
        Stats.MaxBufferDelaySeconds * 1000.0,
        Stats.HardResyncCount);
}


void UMotionBridgeReceiverComponent::UpdateDebugOverlay(const float DeltaTime)
{
    if (!bShowDebugOverlay)
    {
        if (GEngine && bDebugOverlayWasVisible)
        {
            GEngine->RemoveOnScreenDebugMessage(
                MotionBridgeOverlayMessageKey);
        }
        bDebugOverlayWasVisible = false;
        DebugOverlayElapsedSeconds = 0.0f;
        return;
    }

    DebugOverlayElapsedSeconds += DeltaTime;
    const float SafeInterval = FMath::Max(
        DebugOverlayRefreshIntervalSeconds,
        0.02f);
    if (!GEngine || DebugOverlayElapsedSeconds < SafeInterval)
    {
        return;
    }
    DebugOverlayElapsedSeconds = 0.0f;

    FMotionBridgeStreamStats Stats;
    GetStreamStats(Stats);
    const FColor StatusColor = Stats.bStreamLive
        ? FColor(80, 220, 120)
        : (Stats.AcceptedFrames > 0
            ? FColor(255, 100, 80)
            : FColor(255, 200, 80));
    GEngine->AddOnScreenDebugMessage(
        MotionBridgeOverlayMessageKey,
        FMath::Max(SafeInterval * 1.5f, 0.2f),
        StatusColor,
        BuildDebugOverlayText(),
        true,
        FVector2D(1.05f, 1.05f));
    bDebugOverlayWasVisible = true;
}


bool UMotionBridgeReceiverComponent::IsPlaybackStreamLiveLocked(
    const double NowPlatformSeconds) const
{
    if (!bHasAcceptedFrameTime)
    {
        return false;
    }
    if (StreamTimeoutSeconds <= 0.0)
    {
        return true;
    }
    return NowPlatformSeconds - LastAcceptedFramePlatformSeconds <=
        StreamTimeoutSeconds;
}
