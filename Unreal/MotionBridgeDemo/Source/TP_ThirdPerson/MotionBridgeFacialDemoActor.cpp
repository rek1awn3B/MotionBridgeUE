#include "MotionBridgeFacialDemoActor.h"

#include "MotionBridgeMorphTargetDriverComponent.h"
#include "MotionBridgeReceiverComponent.h"
#include "MotionBridgeWebSocketClientComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/ConstructorHelpers.h"


AMotionBridgeFacialDemoActor::AMotionBridgeFacialDemoActor()
{
    PrimaryActorTick.bCanEverTick = false;

    FaceMeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(
        TEXT("FaceMeshComponent"));
    SetRootComponent(FaceMeshComponent);
    FaceMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    FaceMeshComponent->SetEnableGravity(false);
    FaceMeshComponent->VisibilityBasedAnimTickOption =
        EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

    ReceiverComponent = CreateDefaultSubobject<UMotionBridgeReceiverComponent>(
        TEXT("ReceiverComponent"));
    WebSocketClientComponent =
        CreateDefaultSubobject<UMotionBridgeWebSocketClientComponent>(
            TEXT("WebSocketClientComponent"));
    MorphTargetDriverComponent =
        CreateDefaultSubobject<UMotionBridgeMorphTargetDriverComponent>(
            TEXT("MorphTargetDriverComponent"));

    static ConstructorHelpers::FObjectFinder<USkeletalMesh> FaceMeshFinder(
        TEXT("/Game/MotionBridge/Facial/SK_MotionBridgeFace.SK_MotionBridgeFace"));
    if (FaceMeshFinder.Succeeded())
    {
        FaceMeshComponent->SetSkeletalMeshAsset(FaceMeshFinder.Object);
    }
}
