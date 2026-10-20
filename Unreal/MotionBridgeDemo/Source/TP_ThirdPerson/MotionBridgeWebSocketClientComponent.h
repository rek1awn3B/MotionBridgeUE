#pragma once

#include <atomic>

#include "Components/ActorComponent.h"
#include "Containers/Queue.h"
#include "CoreMinimal.h"
#include "MotionBridgeWebSocketClientComponent.generated.h"

class IWebSocket;
class UMotionBridgeReceiverComponent;

/**
 * WebSocket 只负责连接、重连和跨线程收消息；协议解析仍由 Receiver
 * 在游戏线程统一完成。
 */
UCLASS(ClassGroup = (MotionBridge), meta = (BlueprintSpawnableComponent))
class TP_THIRDPERSON_API UMotionBridgeWebSocketClientComponent
    : public UActorComponent
{
    GENERATED_BODY()

public:
    UMotionBridgeWebSocketClientComponent();

    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge|WebSocket")
    bool bAutoConnect = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MotionBridge|WebSocket")
    FString WebSocketUrl = TEXT("ws://127.0.0.1:8765/");

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "MotionBridge|WebSocket",
        meta = (ClampMin = "0.1"))
    float ReconnectDelaySeconds = 1.0f;

    UPROPERTY(
        EditAnywhere,
        BlueprintReadWrite,
        Category = "MotionBridge|WebSocket",
        meta = (ClampMin = "1", ClampMax = "1024"))
    int32 MaxMessagesPerTick = 120;

    UFUNCTION(BlueprintCallable, Category = "MotionBridge|WebSocket")
    void Connect();

    UFUNCTION(BlueprintCallable, Category = "MotionBridge|WebSocket")
    void Disconnect();

    UFUNCTION(BlueprintPure, Category = "MotionBridge|WebSocket")
    bool IsConnected() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UMotionBridgeReceiverComponent> ReceiverComponent;

    TSharedPtr<IWebSocket> Socket;
    TQueue<FString, EQueueMode::Mpsc> PendingMessages;
    std::atomic<int32> PendingMessageCount{0};
    std::atomic<int64> DroppedMessageCount{0};
    std::atomic_bool bConnectionStateDirty{false};
    std::atomic_bool bCallbackConnected{false};
    std::atomic_bool bReconnectRequested{false};
    std::atomic_bool bEnabled{false};
    std::atomic_bool bShuttingDown{false};
    float ReconnectElapsedSeconds = 0.0f;
    int32 ConnectionAttemptCount = 0;
    bool bRunWebSocketTest = false;
    bool bRunWebSocketReconnectTest = false;
    bool bWebSocketTestReported = false;
    float WebSocketTestElapsedSeconds = 0.0f;

    void BindSocketCallbacks();
    void DrainMessages();
    void HandleConnected();
    void HandleConnectionError(const FString& Error);
    void HandleClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
    void HandleMessage(const FString& Message);
    void UpdateWebSocketTest(float DeltaTime);
};
