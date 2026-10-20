#include "MotionBridgeThirdPersonSmokeTestGameMode.h"

#include "TP_ThirdPersonCharacter.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/PlatformMisc.h"
#include "UObject/UnrealType.h"

AMotionBridgeThirdPersonSmokeTestGameMode::AMotionBridgeThirdPersonSmokeTestGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AMotionBridgeThirdPersonSmokeTestGameMode::BeginPlay()
{
	Super::BeginPlay();
	UE_LOG(LogTemp, Display, TEXT("MOTIONBRIDGE_THIRD_PERSON=START"));
}

void AMotionBridgeThirdPersonSmokeTestGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TotalElapsed += DeltaSeconds;

	if (!bTestStarted)
	{
		if (!TryInitializeTest())
		{
			if (TotalElapsed >= 2.0f)
			{
				FinishTest(false, TEXT("controller_character_or_ground_not_ready"));
			}
			return;
		}
	}

	UpdateTest(DeltaSeconds);
}

bool AMotionBridgeThirdPersonSmokeTestGameMode::TryInitializeTest()
{
	if (!TestCharacter.IsValid())
	{
		const APlayerController* PlayerController =
			GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
		TestCharacter = PlayerController != nullptr
			? Cast<ATP_ThirdPersonCharacter>(PlayerController->GetPawn())
			: nullptr;
	}

	ATP_ThirdPersonCharacter* Character = TestCharacter.Get();
	if (Character == nullptr)
	{
		return false;
	}

	if (!bGroundCreated)
	{
		bGroundCreated = CreateTestGround();
	}

	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (!bGroundCreated || Movement == nullptr || !Movement->IsMovingOnGround())
	{
		return false;
	}

	const USkeletalMeshComponent* MeshComponent = Character->GetMesh();
	const bool bInputAssetsValid =
		HasAssignedObjectProperty(Character, TEXT("DefaultMappingContext")) &&
		HasAssignedObjectProperty(Character, TEXT("JumpAction")) &&
		HasAssignedObjectProperty(Character, TEXT("MoveAction")) &&
		HasAssignedObjectProperty(Character, TEXT("LookAction"));
	const bool bEnhancedInputValid =
		Character->FindComponentByClass<UEnhancedInputComponent>() != nullptr;
	const bool bCameraValid =
		Character->GetCameraBoom() != nullptr &&
		Character->GetFollowCamera() != nullptr &&
		Character->GetCameraBoom()->bUsePawnControlRotation &&
		!Character->GetFollowCamera()->bUsePawnControlRotation;
	const bool bMovementConfigValid =
		Movement->bOrientRotationToMovement &&
		!Character->bUseControllerRotationPitch &&
		!Character->bUseControllerRotationYaw &&
		!Character->bUseControllerRotationRoll;
	const bool bAnimationValid =
		MeshComponent != nullptr &&
		MeshComponent->GetSkeletalMeshAsset() != nullptr &&
		MeshComponent->GetAnimInstance() != nullptr;

	bConfigValid =
		bInputAssetsValid &&
		bEnhancedInputValid &&
		bCameraValid &&
		bMovementConfigValid &&
		bAnimationValid;

	UE_LOG(
		LogTemp,
		Display,
		TEXT("MOTIONBRIDGE_THIRD_PERSON_CONFIG=%s|class=%s|controller=%s|test_ground=%d|input_assets=%d|enhanced_input=%d|camera=%d|movement=%d|mesh_and_anim=%d|anim_class=%s"),
		bConfigValid ? TEXT("PASS") : TEXT("FAIL"),
		*Character->GetClass()->GetName(),
		*GetNameSafe(Character->GetController()),
		bGroundCreated ? 1 : 0,
		bInputAssetsValid ? 1 : 0,
		bEnhancedInputValid ? 1 : 0,
		bCameraValid ? 1 : 0,
		bMovementConfigValid ? 1 : 0,
		bAnimationValid ? 1 : 0,
		MeshComponent != nullptr && MeshComponent->GetAnimInstance() != nullptr
			? *MeshComponent->GetAnimInstance()->GetClass()->GetName()
			: TEXT("None"));

	bTestStarted = true;
	PhaseElapsed = 0.0f;
	StartLocation = Character->GetActorLocation();
	return true;
}

bool AMotionBridgeThirdPersonSmokeTestGameMode::CreateTestGround()
{
	ATP_ThirdPersonCharacter* Character = TestCharacter.Get();
	UWorld* World = GetWorld();
	UStaticMesh* FloorMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Character == nullptr || World == nullptr || FloorMesh == nullptr)
	{
		return false;
	}

	const float CapsuleHalfHeight =
		Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FVector FloorLocation =
		Character->GetActorLocation() -
		FVector(0.0, 0.0, CapsuleHalfHeight + 50.0);
	AStaticMeshActor* FloorActor = World->SpawnActor<AStaticMeshActor>(
		FloorLocation,
		FRotator::ZeroRotator);
	if (FloorActor == nullptr)
	{
		return false;
	}

	UStaticMeshComponent* FloorComponent = FloorActor->GetStaticMeshComponent();
	FloorComponent->SetMobility(EComponentMobility::Movable);
	FloorComponent->SetStaticMesh(FloorMesh);
	FloorComponent->SetCollisionProfileName(TEXT("BlockAll"));
	FloorActor->SetActorScale3D(FVector(20.0, 20.0, 1.0));
	return true;
}

void AMotionBridgeThirdPersonSmokeTestGameMode::UpdateTest(float DeltaSeconds)
{
	ATP_ThirdPersonCharacter* Character = TestCharacter.Get();
	if (Character == nullptr)
	{
		FinishTest(false, TEXT("character_destroyed"));
		return;
	}

	PhaseElapsed += DeltaSeconds;
	if (PhaseElapsed < 1.5f)
	{
		const FRotator ControllerRotation = Character->GetControlRotation();
		const FRotator YawRotation(0.0, ControllerRotation.Yaw, 0.0);
		const FVector ForwardDirection =
			FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		Character->AddMovementInput(ForwardDirection, 1.0f);
	}
	else if (!bJumpIssued)
	{
		Character->Jump();
		bJumpIssued = true;
	}

	MaxDistance = FMath::Max(
		MaxDistance,
		FVector::Dist2D(StartLocation, Character->GetActorLocation()));

	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	if (Movement != nullptr && Movement->IsFalling())
	{
		bObservedFalling = true;
	}
	else if (bObservedFalling)
	{
		bObservedLanding = true;
	}

	if (bObservedLanding)
	{
		const bool bPassed =
			bConfigValid &&
			MaxDistance >= 100.0f &&
			bObservedFalling;
		FinishTest(
			bPassed,
			bPassed ? TEXT("completed") : TEXT("validation_failed"));
	}
	else if (PhaseElapsed >= 6.0f)
	{
		FinishTest(false, TEXT("movement_or_jump_timeout"));
	}
}

void AMotionBridgeThirdPersonSmokeTestGameMode::FinishTest(
	bool bPassed,
	const TCHAR* Reason)
{
	if (bPassed)
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT("MOTIONBRIDGE_THIRD_PERSON=PASS|reason=%s|max_distance=%.3f|jump_issued=%d|observed_falling=%d|observed_landing=%d"),
			Reason,
			MaxDistance,
			bJumpIssued ? 1 : 0,
			bObservedFalling ? 1 : 0,
			bObservedLanding ? 1 : 0);
	}
	else
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("MOTIONBRIDGE_THIRD_PERSON=FAIL|reason=%s|max_distance=%.3f|jump_issued=%d|observed_falling=%d|observed_landing=%d"),
			Reason,
			MaxDistance,
			bJumpIssued ? 1 : 0,
			bObservedFalling ? 1 : 0,
			bObservedLanding ? 1 : 0);
	}

	SetActorTickEnabled(false);
	FGenericPlatformMisc::RequestExit(false);
}

bool AMotionBridgeThirdPersonSmokeTestGameMode::HasAssignedObjectProperty(
	const UObject* Object,
	const FName PropertyName)
{
	if (Object == nullptr)
	{
		return false;
	}

	const FObjectPropertyBase* Property =
		FindFProperty<FObjectPropertyBase>(Object->GetClass(), PropertyName);
	return Property != nullptr &&
		Property->GetObjectPropertyValue_InContainer(Object) != nullptr;
}
