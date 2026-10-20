#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MotionBridgeFacialDemoActor.generated.h"

class USkeletalMeshComponent;
class UMotionBridgeMorphTargetDriverComponent;
class UMotionBridgeReceiverComponent;
class UMotionBridgeWebSocketClientComponent;

/** 原创简化角色，用于演示网络标量通道驱动 Morph Target。 */
UCLASS()
class TP_THIRDPERSON_API AMotionBridgeFacialDemoActor : public AActor
{
    GENERATED_BODY()

public:
    AMotionBridgeFacialDemoActor();

private:
    UPROPERTY(VisibleAnywhere, Category = "MotionBridge|Facial")
    TObjectPtr<USkeletalMeshComponent> FaceMeshComponent;

    UPROPERTY(VisibleAnywhere, Category = "MotionBridge|Facial")
    TObjectPtr<UMotionBridgeReceiverComponent> ReceiverComponent;

    UPROPERTY(VisibleAnywhere, Category = "MotionBridge|Facial")
    TObjectPtr<UMotionBridgeWebSocketClientComponent> WebSocketClientComponent;

    UPROPERTY(VisibleAnywhere, Category = "MotionBridge|Facial")
    TObjectPtr<UMotionBridgeMorphTargetDriverComponent> MorphTargetDriverComponent;
};
