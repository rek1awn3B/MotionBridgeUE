#pragma once

#include "CoreMinimal.h"
#include "TP_ThirdPersonGameMode.h"
#include "MotionBridgeThirdPersonSmokeTestGameMode.generated.h"

class ATP_ThirdPersonCharacter;

/**
 * 仅用于自动验收第三人称角色的测试 GameMode。
 * 正式角色类不承担搭建测试场景、驱动测试流程或退出程序的职责。
 */
UCLASS()
class TP_THIRDPERSON_API AMotionBridgeThirdPersonSmokeTestGameMode
	: public ATP_ThirdPersonGameMode
{
	GENERATED_BODY()

public:
	AMotionBridgeThirdPersonSmokeTestGameMode();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	bool TryInitializeTest();
	bool CreateTestGround();
	void UpdateTest(float DeltaSeconds);
	void FinishTest(bool bPassed, const TCHAR* Reason);
	static bool HasAssignedObjectProperty(
		const UObject* Object,
		const FName PropertyName);

	TWeakObjectPtr<ATP_ThirdPersonCharacter> TestCharacter;
	bool bTestStarted = false;
	bool bGroundCreated = false;
	bool bConfigValid = false;
	bool bJumpIssued = false;
	bool bObservedFalling = false;
	bool bObservedLanding = false;
	float TotalElapsed = 0.0f;
	float PhaseElapsed = 0.0f;
	float MaxDistance = 0.0f;
	FVector StartLocation = FVector::ZeroVector;
};
