#include "MotionBridgeDemoGameMode.h"

#include "MotionBridgeRootMotionCharacter.h"
#include "MotionBridgeStatusHUD.h"
#include "MotionBridgeFacialDemoActor.h"

#include "Camera/CameraActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AMotionBridgeDemoGameMode::AMotionBridgeDemoGameMode()
{
    // 这是动画展示关卡，不生成第三人称模板的可操控角色。
    DefaultPawnClass = nullptr;
    HUDClass = AMotionBridgeStatusHUD::StaticClass();
}

void AMotionBridgeDemoGameMode::BeginPlay()
{
    Super::BeginPlay();

    const bool bRunFacialDemo =
        FParse::Param(FCommandLine::Get(), TEXT("MotionBridgeFacialDemo")) ||
        FParse::Param(FCommandLine::Get(), TEXT("MotionBridgeMorphTargetTest"));
    if (bRunFacialDemo)
    {
        UWorld* World = GetWorld();
        if (!World)
        {
            return;
        }

        AMotionBridgeFacialDemoActor* FacialActor =
            World->SpawnActor<AMotionBridgeFacialDemoActor>(
            FVector(0.0f, 0.0f, 110.0f),
            FRotator::ZeroRotator);
        if (FacialActor)
        {
            // Blender 的测试资产以米为建模单位；FBX 导入后换算成 UE 的厘米尺度。
            // 必须在 SpawnActor 后设置 Actor 尺度，否则生成变换会覆盖根组件的尺度。
            FacialActor->SetActorScale3D(FVector(100.0f));
        }
        ADirectionalLight* FacialLight = World->SpawnActor<ADirectionalLight>(
            FVector(200.0f, 100.0f, 300.0f),
            FRotator(-25.0f, 160.0f, 0.0f));
        if (FacialLight)
        {
            FacialLight->GetLightComponent()->SetIntensity(5.0f);
        }
        World->SpawnActor<ASkyLight>(
            FVector(0.0f, 0.0f, 250.0f),
            FRotator::ZeroRotator);

        FVector FaceCenter(0.0f, 0.0f, 110.0f);
        float FaceRadius = 90.0f;
        if (FacialActor)
        {
            if (USkeletalMeshComponent* FaceMesh =
                    FacialActor->FindComponentByClass<USkeletalMeshComponent>())
            {
                FaceMesh->UpdateBounds();
                FaceCenter = FaceMesh->Bounds.Origin;
                FaceRadius = FMath::Max(FaceMesh->Bounds.SphereRadius, 1.0f);
                UE_LOG(
                    LogTemp,
                    Display,
                    TEXT("MOTIONBRIDGE_FACE_BOUNDS|center=%s|radius=%.2f|scale=%s"),
                    *FaceCenter.ToCompactString(),
                    FaceRadius,
                    *FacialActor->GetActorScale3D().ToCompactString());
            }
        }

        // FBX 坐标轴转换后，原创头像的面部朝向为 +Y。
        const FVector CameraLocation =
            FaceCenter + FVector(0.0f, FMath::Max(FaceRadius * 2.8f, 200.0f), 0.0f);
        ACameraActor* FacialCamera = World->SpawnActor<ACameraActor>(
            CameraLocation,
            (FaceCenter - CameraLocation).Rotation());
        if (FacialCamera)
        {
            if (APlayerController* PlayerController =
                    World->GetFirstPlayerController())
            {
                PlayerController->SetViewTarget(FacialCamera);
            }
        }
        UE_LOG(LogTemp, Display, TEXT("MOTIONBRIDGE_FACIAL_DEMO=READY"));
        return;
    }

    if (!FParse::Param(
            FCommandLine::Get(),
            TEXT("MotionBridgePortfolioWalkRuntimeDemo")))
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return;
    }

    const FTransform CharacterTransform(
        FRotator::ZeroRotator,
        FVector(0.0f, 0.0f, 91.0f));
    AMotionBridgeRootMotionCharacter* Character =
        World->SpawnActorDeferred<AMotionBridgeRootMotionCharacter>(
            AMotionBridgeRootMotionCharacter::StaticClass(),
            CharacterTransform,
            nullptr,
            nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (Character)
    {
        Character->ConfigurePortfolioWalkDemo();
        UGameplayStatics::FinishSpawningActor(Character, CharacterTransform);
    }

    AStaticMeshActor* Floor = World->SpawnActor<AStaticMeshActor>(
        FVector(0.0f, -78.5f, -10.0f),
        FRotator::ZeroRotator);
    if (Floor)
    {
        Floor->SetActorScale3D(FVector(3.0f, 8.0f, 0.1f));
        UStaticMesh* Cube = LoadObject<UStaticMesh>(
            nullptr,
            TEXT("/Engine/BasicShapes/Cube.Cube"));
        Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
    }

    ADirectionalLight* KeyLight = World->SpawnActor<ADirectionalLight>(
        FVector(0.0f, 0.0f, 300.0f),
        FRotator(-45.0f, -35.0f, 0.0f));
    if (KeyLight)
    {
        KeyLight->GetLightComponent()->SetIntensity(4.0f);
    }
    World->SpawnActor<ASkyLight>(
        FVector(0.0f, 0.0f, 200.0f),
        FRotator::ZeroRotator);

    ACameraActor* Camera = World->SpawnActor<ACameraActor>(
        FVector::ZeroVector,
        FRotator::ZeroRotator);
    if (Camera)
    {
        if (Character)
        {
            // 跟随角色根节点，只抵消整体前进，不干预骨骼动画。侧视角能让
            // 支撑脚是否滑动、脚掌是否穿地和膝关节是否抽动更容易观察。
            Camera->AttachToActor(
                Character,
                FAttachmentTransformRules::SnapToTargetNotIncludingScale);
            Camera->SetActorRelativeLocation(FVector(450.0f, 0.0f, 80.0f));
            Camera->SetActorRelativeRotation(FRotator(-10.0f, 180.0f, 0.0f));
        }
        if (APlayerController* PlayerController =
                World->GetFirstPlayerController())
        {
            PlayerController->SetViewTarget(Camera);
        }
    }

    UE_LOG(LogTemp, Display, TEXT("MOTIONBRIDGE_PORTFOLIO_WALK_DEMO=PASS"));
}
