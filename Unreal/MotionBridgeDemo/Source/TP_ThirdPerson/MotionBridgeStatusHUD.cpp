#include "MotionBridgeStatusHUD.h"

#include "MotionBridgeReceiverComponent.h"
#include "MotionBridgeStatusWidget.h"

#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"


void AMotionBridgeStatusHUD::BeginPlay()
{
    Super::BeginPlay();

    APlayerController* PlayerController = GetOwningPlayerController();
    if (!PlayerController || !PlayerController->IsLocalController())
    {
        return;
    }

    UMotionBridgeReceiverComponent* Receiver = FindReceiverComponent();
    if (!Receiver)
    {
        UE_LOG(LogTemp, Warning, TEXT("MOTIONBRIDGE_UMG=SKIPPED|receiver_not_found"));
        return;
    }

    StatusWidget = CreateWidget<UMotionBridgeStatusWidget>(
        PlayerController,
        UMotionBridgeStatusWidget::StaticClass());
    if (!StatusWidget)
    {
        UE_LOG(LogTemp, Error, TEXT("MOTIONBRIDGE_UMG=FAIL|widget_creation"));
        return;
    }

    StatusWidget->SetReceiverComponent(Receiver);
    StatusWidget->AddToViewport(10);
    UE_LOG(
        LogTemp,
        Display,
        TEXT("MOTIONBRIDGE_UMG=CREATED|receiver=%s|owner=%s"),
        *GetNameSafe(Receiver),
        *GetNameSafe(Receiver->GetOwner()));
}


void AMotionBridgeStatusHUD::EndPlay(
    const EEndPlayReason::Type EndPlayReason)
{
    if (StatusWidget)
    {
        StatusWidget->RemoveFromParent();
        StatusWidget = nullptr;
    }
    Super::EndPlay(EndPlayReason);
}


UMotionBridgeReceiverComponent*
AMotionBridgeStatusHUD::FindReceiverComponent() const
{
    if (const APlayerController* PlayerController = GetOwningPlayerController())
    {
        if (const APawn* Pawn = PlayerController->GetPawn())
        {
            if (UMotionBridgeReceiverComponent* Receiver =
                    Pawn->FindComponentByClass<UMotionBridgeReceiverComponent>())
            {
                return Receiver;
            }
        }
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    for (TActorIterator<AActor> ActorIterator(World);
         ActorIterator;
         ++ActorIterator)
    {
        if (UMotionBridgeReceiverComponent* Receiver =
                ActorIterator->FindComponentByClass<UMotionBridgeReceiverComponent>())
        {
            return Receiver;
        }
    }
    return nullptr;
}
