#include "MotionBridgeNaturalWalkCharacter.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
constexpr float NaturalWalkPlayRate = 0.75f;
}

AMotionBridgeNaturalWalkCharacter::AMotionBridgeNaturalWalkCharacter()
{
    bApplyRootMotionToActor = false;

    // Manny 原生动画的 root 位于脚底，因此网格要从胶囊中心下移一个半高。
    // 旧的 BVH 重定向动画自带垂直根偏移，仍由父类场景保持零偏移。
    GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -96.0f));
    GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));

    GetCharacterMovement()->bOrientRotationToMovement = false;

    static ConstructorHelpers::FClassFinder<UAnimInstance> NaturalWalkAnimBlueprint(
        TEXT("/Game/MotionBridge/FootIK/ABP_MotionBridgeNaturalWalkFootIK"));
    if (NaturalWalkAnimBlueprint.Succeeded())
    {
        GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);
        GetMesh()->SetAnimInstanceClass(NaturalWalkAnimBlueprint.Class);
    }

    static ConstructorHelpers::FObjectFinder<UAnimSequence> MotionSource(
        TEXT("/Game/Characters/Mannequins/Animations/Manny/MM_Walk_Fwd.MM_Walk_Fwd"));
    if (MotionSource.Succeeded())
    {
        NaturalWalkMotionSource = MotionSource.Object;
        // AnimGraph 直接播放这同一份动画；开启提取后，姿势与根位移共享同一个
        // 播放时钟，不再把 InPlace 姿势和另一份动画的位移手工拼接。
        NaturalWalkMotionSource->bEnableRootMotion = true;
        NaturalWalkMotionSource->RootMotionRootLock =
            ERootMotionRootLock::AnimFirstFrame;
    }
}

void AMotionBridgeNaturalWalkCharacter::BeginPlay()
{
    Super::BeginPlay();
    NaturalWalkStartLocation = GetActorLocation();

    if (UAnimInstance* AnimInstance = GetMesh()->GetAnimInstance())
    {
        // 基础 Root Motion 路径采用显式提取；自然步态改回 UE 标准流程，让
        // CharacterMovement 消费 AnimGraph 当前帧产生的 Root Motion。
        AnimInstance->SetRootMotionMode(ERootMotionMode::RootMotionFromEverything);
    }
}

void AMotionBridgeNaturalWalkCharacter::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!NaturalWalkMotionSource || bReportedNaturalWalkResult)
    {
        return;
    }

    NaturalWalkElapsedSeconds += DeltaSeconds;
    const float PlaybackDuration =
        NaturalWalkMotionSource->GetPlayLength() / NaturalWalkPlayRate;
    if (NaturalWalkElapsedSeconds < PlaybackDuration)
    {
        return;
    }

    bReportedNaturalWalkResult = true;
    const FVector Delta = GetActorLocation() - NaturalWalkStartLocation;
    UE_LOG(
        LogTemp,
        Display,
        TEXT("MOTIONBRIDGE_NATURAL_WALK_ROOT_MOTION=%s|distance=%.3f|delta=%s"),
        Delta.Size2D() > 100.0f ? TEXT("PASS") : TEXT("FAIL"),
        Delta.Size2D(),
        *Delta.ToCompactString());
}
