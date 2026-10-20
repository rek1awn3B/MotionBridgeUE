#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MotionBridgeStatusHUD.generated.h"

class UMotionBridgeReceiverComponent;
class UMotionBridgeStatusWidget;

/** 本地玩家界面入口；负责创建状态 Widget 并连接到场景中的 Receiver。 */
UCLASS()
class TP_THIRDPERSON_API AMotionBridgeStatusHUD : public AHUD
{
    GENERATED_BODY()

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UPROPERTY(Transient)
    TObjectPtr<UMotionBridgeStatusWidget> StatusWidget;

    UMotionBridgeReceiverComponent* FindReceiverComponent() const;
};
