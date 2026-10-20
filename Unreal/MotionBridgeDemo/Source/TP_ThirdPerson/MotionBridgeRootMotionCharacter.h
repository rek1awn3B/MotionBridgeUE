#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MotionBridgeRootMotionCharacter.generated.h"

class UAnimSequence;
class UMotionBridgeFootIKComponent;
class UMotionBridgeReceiverComponent;
class UMotionBridgeWebSocketClientComponent;

/**
 * Root Motion 验证角色。
 *
 * 动画里的 root 轨迹由 CharacterMovement 消费，所以移动的是 Actor/胶囊，
 * 而不是只有网格在胶囊内部滑出去。
 */
UCLASS()
class TP_THIRDPERSON_API AMotionBridgeRootMotionCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    AMotionBridgeRootMotionCharacter();

    /** 在 FinishSpawningActor 前切换为可公开分发的 100STYLE 前进走路演示。 */
    void ConfigurePortfolioWalkDemo();

    /**
     * Day 9 的 Root Motion 关卡保持开启；Day 10 的 Foot IK 关卡关闭，
     * 让同一段重定向动作原地播放，避免 180 度掉头轨迹干扰脚部观察。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge|Root Motion")
    bool bApplyRootMotionToActor = true;

    /**
     * 直线行走演示可把整段根位移按时长均匀分配，避免动捕髋部每一步的
     * 速度脉冲被放大成角色胶囊“一蹿一蹿”。默认关闭，不改变既有回归测试。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge|Root Motion")
    bool bUseConstantSpeedRootMotion = false;

    /** 与循环播放的 AnimGraph 同步重复消费根运动；默认关闭以保持旧验收语义。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge|Root Motion")
    bool bLoopRootMotionPlayback = false;

    /** 0 保留原始逐步速度，1 完全匀速；中间值兼顾平顺与落脚节奏。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge|Root Motion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ConstantSpeedRootMotionBlend = 1.0f;

    /** 0 保留原始根朝向，1 把整段净转角均匀分配，消除逐步左右摆动。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge|Root Motion", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ConstantDirectionRootMotionBlend = 0.0f;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    /**
     * 默认仍指向 Day 9 的回归动画；独立演示关卡可以在 Actor 实例上
     * 换成新的公开 BVH 重定向结果，而不影响既有 UDP 自动验收。
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge|Root Motion")
    TObjectPtr<UAnimSequence> RootMotionAnimation;

private:
    UPROPERTY(VisibleAnywhere, Category = "MotionBridge|Foot IK")
    TObjectPtr<UMotionBridgeFootIKComponent> FootIKComponent;

    UPROPERTY(VisibleAnywhere, Category = "MotionBridge|Realtime")
    TObjectPtr<UMotionBridgeReceiverComponent> ReceiverComponent;

    UPROPERTY(VisibleAnywhere, Category = "MotionBridge|Realtime")
    TObjectPtr<UMotionBridgeWebSocketClientComponent> WebSocketClientComponent;

    FVector StartLocation = FVector::ZeroVector;
    FVector TotalRootMotionTranslation = FVector::ZeroVector;
    FQuat TotalRootMotionRotation = FQuat::Identity;
    float ElapsedSeconds = 0.0f;
    float RootMotionPlaybackTime = 0.0f;
    float MaxHorizontalDistance = 0.0f;
    bool bReportedResult = false;

    bool bRunRealtimePoseTest = false;
    bool bRunUdpPoseTest = false;
    bool bRunUdpAnimationTest = false;
    bool bRunUdpFallbackTest = false;
    bool bRunUdpReconnectTest = false;
    bool bRunStreamStatsTest = false;
    bool bRealtimePoseReported = false;
    float RealtimePoseTestElapsedSeconds = 0.0f;
    FQuat ExpectedRealtimeSpine = FQuat::Identity;
    FQuat ExpectedRealtimeNeck = FQuat::Identity;
    FQuat ExpectedRealtimeHead = FQuat::Identity;

    bool bUdpAnimationReported = false;
    bool bHasUdpRootSample = false;
    float UdpAnimationElapsedSeconds = 0.0f;
    int32 UdpObservedFrameCount = 0;
    int32 LastUdpObservedFrameIndex = INDEX_NONE;
    int32 UdpTicksAfterFinalFrame = 0;
    FVector UdpRootMin = FVector::ZeroVector;
    FVector UdpRootMax = FVector::ZeroVector;
    TArray<FQuat> FirstUdpBoneRotations;
    float MaxUdpBoneMotionDegrees = 0.0f;

    bool bNetworkRootInitialized = false;
    double LastNetworkRootTimestampSeconds = 0.0;
    FVector PreviousNetworkRootPosition = FVector::ZeroVector;
    FQuat PreviousNetworkRootRotation = FQuat::Identity;
    float MaxNetworkActorHorizontalDistance = 0.0f;

    bool bUdpFallbackReported = false;
    bool bSawFreshUdpPose = false;
    bool bCapturedFallbackPose = false;
    float UdpFallbackElapsedSeconds = 0.0f;
    float UdpFallbackStaleSeconds = 0.0f;
    float MaxFallbackBoneMotionDegrees = 0.0f;
    TArray<FName> FallbackBoneNames;
    TArray<FQuat> FallbackStartRotations;

    bool bReconnectSawFirstLiveSession = false;
    bool bReconnectSawTimeout = false;
    bool bReconnectSawSecondLiveSession = false;
    bool bReconnectReported = false;
    float UdpReconnectElapsedSeconds = 0.0f;
    FVector ReconnectDisconnectLocation = FVector::ZeroVector;
    FVector ReconnectStartLocation = FVector::ZeroVector;
    float ReconnectFirstFrameJumpDistance = 0.0f;
    float ReconnectMaxResumedDistance = 0.0f;

    void InitializeRealtimePoseTest();
    void ReportRealtimePoseTest();
    void UpdateNetworkRootMotion(float DeltaSeconds);
    void UpdateUdpAnimationTest(float DeltaSeconds);
    void ReportUdpAnimationTest();
    void UpdateUdpFallbackTest(float DeltaSeconds);
    void ReportUdpFallbackTest(const TCHAR* FailureReason = nullptr);
    void UpdateUdpReconnectTest(float DeltaSeconds);
    void ReportUdpReconnectTest(const TCHAR* FailureReason = nullptr);
    void RunStreamStatsTest();
};
