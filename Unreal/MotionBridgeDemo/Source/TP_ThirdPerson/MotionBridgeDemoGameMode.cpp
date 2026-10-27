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

    // 演示关卡本身可能保存过一个预览角色。运行时验收需要唯一的数据接收者，
    // 否则两个 WebSocket 客户端会各自回放一份流，统计与画面都会重复。
    TArray<AActor*> ExistingCharacters;
    UGameplayStatics::GetAllActorsOfClass(
        World,
        AMotionBridgeRootMotionCharacter::StaticClass(),
        ExistingCharacters);
    for (AActor* ExistingCharacter : ExistingCharacters)
    {
        if (IsValid(ExistingCharacter))
        {
            ExistingCharacter->Destroy();
        }
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
        // 公开演示使用带 Root Motion 的直走片段。保留足够大的平地，方便
        // 从侧面和正面检查完整轨迹与脚底接触。
        Floor->SetActorScale3D(FVector(12.0f, 12.0f, 0.1f));
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
            // 跟随角色根节点，只抵消整体前进，不干预骨骼动画。
            Camera->AttachToActor(
                Character,
                FAttachmentTransformRules::SnapToTargetNotIncludingScale);
            // Manny 的骨盆在该重定向流中相对 root 沿局部 Y 偏移约 259 cm；
            // 网格再绕 Actor 旋转 -90° 后，这个偏移落到世界 X。相机同步
            // 补偿该偏移，才能让可见身体（而不是胶囊原点）处在画面中央。
            const bool bFrontView = FParse::Param(
                FCommandLine::Get(),
                TEXT("MotionBridgeFrontView"));
            if (bFrontView)
            {
                // 从角色前进方向迎面观察，用来检查左右腿语义、髋部内收
                // 和双脚是否跨过身体中线。
                Camera->SetActorRelativeLocation(
                    FVector(190.0f, 0.0f, 80.0f));
                Camera->SetActorRelativeRotation(
                    FRotator(-10.0f, 180.0f, 0.0f));
            }
            else
            {
                // 侧视角更容易观察支撑脚滑动、脚掌穿地和膝关节抽动。
                Camera->SetActorRelativeLocation(
                    FVector(-260.0f, -450.0f, 80.0f));
                Camera->SetActorRelativeRotation(
                    FRotator(-10.0f, 90.0f, 0.0f));
            }
        }
        if (APlayerController* PlayerController =
                World->GetFirstPlayerController())
        {
            PlayerController->SetViewTarget(Camera);
        }
    }

    UE_LOG(LogTemp, Display, TEXT("MOTIONBRIDGE_PORTFOLIO_WALK_DEMO=PASS"));
}
