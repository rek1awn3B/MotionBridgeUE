#pragma once

#include <atomic>

#include "Containers/Queue.h"
#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "MotionBridgeReceiverComponent.generated.h"

class FSocket;
class FUdpSocketReceiver;

USTRUCT(BlueprintType)
struct FMotionBridgeFrame
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge")
    int32 FrameIndex = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge")
    double TimestampSeconds = 0.0;

    /** 发送端写入的 Unix 时间；用于统计端到端延迟，0 表示未提供。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge")
    double SenderUnixSeconds = 0.0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge")
    FVector RootPosition = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge")
    FQuat RootRotation = FQuat::Identity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge")
    TArray<FQuat> LocalBoneRotations;

    /** 可选；为空时播放端使用目标骨架参考姿势中的局部位移。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge")
    TArray<FVector> LocalBoneTranslations;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge")
    TArray<float> Channels;
};


USTRUCT(BlueprintType)
struct FMotionBridgeStreamDefinition
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge")
    int32 ProtocolVersion = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge")
    FString StreamId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge")
    TArray<FName> BoneNames;

    /** 可选的标量通道，例如 Smile、JawOpen 或其他 BlendShape 权重。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge")
    TArray<FName> ChannelNames;
};


UENUM(BlueprintType)
enum class EMotionBridgeTransport : uint8
{
    Udp,
    WebSocket,
};


UENUM(BlueprintType)
enum class EMotionBridgePushResult : uint8
{
    Inserted,
    InsertedOutOfOrder,
    DuplicateFrameIndex,
    InvalidTimestamp,
    BoneCountMismatch,
    ChannelCountMismatch,
};


USTRUCT(BlueprintType)
struct FMotionBridgeSampleResult
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    FMotionBridgeFrame Frame;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int32 LowerFrameIndex = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int32 UpperFrameIndex = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    double Alpha = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    bool bClampedToBoundary = false;
};


USTRUCT(BlueprintType)
struct FMotionBridgeStreamStats
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int64 ReceivedUdpPackets = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int64 RejectedUdpPackets = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int64 ReceivedWebSocketMessages = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int64 RejectedWebSocketMessages = 0;

    /** 在进入游戏线程解析前因入口队列已满而丢弃的 UDP/WebSocket 消息。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int64 DroppedIngressMessages = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int64 WebSocketReconnectAttempts = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int64 SubmittedFrames = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int64 AcceptedFrames = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int64 RejectedFrames = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int64 DuplicateFrames = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int64 OutOfOrderFrames = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int64 EstimatedLostFrames = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int64 HardResyncCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int32 BufferedFrames = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int32 LowestFrameIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int32 HighestFrameIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    double CurrentBufferDelaySeconds = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    double MaxBufferDelaySeconds = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    double IncomingFrameRateHz = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    double GameThreadFps = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    double LatestEndToEndLatencyMs = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    double AverageEndToEndLatencyMs = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    double MaxEndToEndLatencyMs = 0.0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    int64 EndToEndLatencySamples = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    bool bWebSocketConnected = false;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MotionBridge")
    bool bStreamLive = false;
};


UCLASS(
    ClassGroup = (MotionBridge),
    meta = (BlueprintSpawnableComponent)
)
class TP_THIRDPERSON_API UMotionBridgeReceiverComponent
    : public UActorComponent
{
    GENERATED_BODY()

public:
    UMotionBridgeReceiverComponent();

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "MotionBridge",
        meta = (ClampMin = "1")
    )
    int32 BufferCapacity = 120;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "MotionBridge",
        meta = (ClampMin = "0.0")
    )
    double PlaybackDelaySeconds = 0.1;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "MotionBridge",
        meta = (ClampMin = "0.0", ClampMax = "1.0")
    )
    double MaxPlaybackSpeedAdjustment = 0.1;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "MotionBridge",
        meta = (ClampMin = "0.0")
    )
    double HardResyncThresholdSeconds = 0.25;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge")
    bool bAutoAdvancePlayback = true;

    /** 最后一帧到达后多久判定实时流中断；设为 0 可关闭超时判断。 */
    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "MotionBridge|Playback",
        meta = (ClampMin = "0.0")
    )
    double StreamTimeoutSeconds = 0.5;

    /**
     * 开启后，角色类负责把网络帧中的 root 轨迹交给 CharacterMovement；
     * AnimInstance 会把快照里的 root 保持在参考姿势，避免网格和胶囊重复移动。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge|Root Motion")
    bool bConsumeRootMotionAsActorMovement = false;

    /** 默认只监听本机，避免开发期间把动作端口暴露到局域网。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge|UDP")
    bool bEnableUdpReceiver = false;

    /** 在游戏视口左上角显示实时收包、丢帧和缓冲状态。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge|Debug")
    bool bShowDebugOverlay = false;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "MotionBridge|Debug",
        meta = (ClampMin = "0.02")
    )
    float DebugOverlayRefreshIntervalSeconds = 0.1f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "MotionBridge|UDP",
        meta = (ClampMin = "1", ClampMax = "65535")
    )
    int32 UdpListenPort = 7001;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "MotionBridge|UDP",
        meta = (ClampMin = "1", ClampMax = "1024")
    )
    int32 MaxUdpPacketsPerTick = 120;

    UFUNCTION(BlueprintCallable, Category = "MotionBridge")
    void PushDebugFrame(const FMotionBridgeFrame& Frame);

    UFUNCTION(BlueprintCallable, Category = "MotionBridge")
    EMotionBridgePushResult PushFrame(const FMotionBridgeFrame& Frame);

    UFUNCTION(BlueprintCallable, Category = "MotionBridge")
    bool ConfigureStream(const FMotionBridgeStreamDefinition& Definition);

    UFUNCTION(BlueprintCallable, Category = "MotionBridge")
    bool TryGetStreamDefinition(
        FMotionBridgeStreamDefinition& OutDefinition) const;

    UFUNCTION(BlueprintCallable, Category = "MotionBridge")
    bool SampleFrameAtTimestamp(
        double PlaybackTimestampSeconds,
        FMotionBridgeSampleResult& OutSample) const;

    UFUNCTION(BlueprintCallable, Category = "MotionBridge")
    bool SampleLatestFrameWithDelay(
        FMotionBridgeSampleResult& OutSample) const;

    UFUNCTION(BlueprintCallable, Category = "MotionBridge")
    bool AdvancePlaybackAndSample(
        double DeltaSeconds,
        FMotionBridgeSampleResult& OutSample);

    UFUNCTION(BlueprintCallable, Category = "MotionBridge")
    void ResetBuffer();

    UFUNCTION(BlueprintPure, Category = "MotionBridge")
    int32 GetBufferedFrameCount() const;

    UFUNCTION(BlueprintCallable, Category = "MotionBridge")
    bool TryGetLatestPlaybackSample(
        FMotionBridgeSampleResult& OutSample) const;

    /** 仅在最近仍有新帧到达时返回播放样本，供动画和 Root Motion 使用。 */
    UFUNCTION(BlueprintCallable, Category = "MotionBridge")
    bool TryGetFreshPlaybackSample(
        FMotionBridgeSampleResult& OutSample) const;

    UFUNCTION(BlueprintPure, Category = "MotionBridge")
    bool IsPlaybackStreamLive() const;

    UFUNCTION(BlueprintCallable, Category = "MotionBridge")
    void GetStreamStats(FMotionBridgeStreamStats& OutStats) const;

    UFUNCTION(BlueprintPure, Category = "MotionBridge|Debug")
    FString BuildDebugOverlayText() const;

    /** 在游戏线程提交一条完整协议消息；UDP 与 WebSocket 共用同一解析路径。 */
    bool SubmitJsonPacket(
        const FString& Packet,
        EMotionBridgeTransport Transport);

    /** WebSocket 组件通过这两个线程安全入口更新连接状态。 */
    void SetWebSocketConnected(bool bConnected);
    void RecordWebSocketReconnectAttempt();
    void RecordDroppedIngressMessages(int64 DroppedCount);

protected:
    virtual void BeginPlay() override;

private:
    UPROPERTY(VisibleAnywhere, Category = "MotionBridge")
    TArray<FMotionBridgeFrame> FrameBuffer;

    mutable FCriticalSection BufferMutex;
    int32 ExpectedBoneCount = INDEX_NONE;
    int32 ExpectedChannelCount = INDEX_NONE;
    double CurrentPlaybackTimestampSeconds = 0.0;
    bool bPlaybackClockInitialized = false;
    FMotionBridgeSampleResult LatestPlaybackSample;
    bool bHasLatestPlaybackSample = false;
    double LastAcceptedFramePlatformSeconds = 0.0;
    bool bHasAcceptedFrameTime = false;
    FMotionBridgeStreamDefinition StreamDefinition;
    bool bHasStreamDefinition = false;
    FMotionBridgeStreamStats StreamStats;
    TSet<int32> ObservedFrameIndices;
    TQueue<int32> ObservedFrameIndexOrder;

    FSocket* UdpSocket = nullptr;
    FUdpSocketReceiver* UdpReceiver = nullptr;
    TQueue<FString, EQueueMode::Mpsc> PendingUdpPackets;
    std::atomic<int32> PendingUdpPacketCount{0};
    std::atomic<int64> DroppedUdpPacketCount{0};
    float DebugOverlayElapsedSeconds = 0.0f;
    bool bDebugOverlayWasVisible = false;
    double MetricsElapsedSeconds = 0.0;
    int64 MetricsAcceptedFrameBaseline = 0;
    int64 MetricsTickCount = 0;
    bool bWebSocketConnected = false;

    bool SampleFrameAtTimestampLocked(
        double PlaybackTimestampSeconds,
        FMotionBridgeSampleResult& OutSample) const;
    bool IsPlaybackStreamLiveLocked(double NowPlatformSeconds) const;

    void ResetStateLocked(bool bClearStreamDefinition);
    bool StartUdpReceiver();
    void StopUdpReceiver();
    void DrainUdpPackets();
    bool ParsePacketOnGameThread(
        const FString& Packet,
        EMotionBridgeTransport Transport);
    void UpdatePerformanceMetrics(double DeltaTime);
    void UpdateDebugOverlay(float DeltaTime);
};
