#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MotionBridgeDemoGameMode.generated.h"


UCLASS()
class TP_THIRDPERSON_API AMotionBridgeDemoGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AMotionBridgeDemoGameMode();

protected:
    virtual void BeginPlay() override;
};
