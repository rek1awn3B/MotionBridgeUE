#pragma once

#include "CoreMinimal.h"
#include "MotionBridgeRootMotionCharacter.h"
#include "MotionBridgeNaturalWalkCharacter.generated.h"

class UAnimSequence;

/**
 * Foot IK 的自然步态演示角色。
 *
 * 它播放 Manny 原地直线走路，并让 CharacterMovement 推动胶囊前进；这样角色
 * 会真实走上台阶，而不是把台阶棱边永久放在两只脚之间做极端压力测试。
 */
UCLASS()
class TP_THIRDPERSON_API AMotionBridgeNaturalWalkCharacter
    : public AMotionBridgeRootMotionCharacter
{
    GENERATED_BODY()

public:
    AMotionBridgeNaturalWalkCharacter();

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    /** AnimGraph 直接播放的 Manny 向前走 Root Motion 动画。 */
    UPROPERTY()
    TObjectPtr<UAnimSequence> NaturalWalkMotionSource;

    FVector NaturalWalkStartLocation = FVector::ZeroVector;
    float NaturalWalkElapsedSeconds = 0.0f;
    bool bReportedNaturalWalkResult = false;
};
