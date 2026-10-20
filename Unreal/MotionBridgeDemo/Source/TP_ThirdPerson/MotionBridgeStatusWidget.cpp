#include "MotionBridgeStatusWidget.h"

#include "MotionBridgeReceiverComponent.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
constexpr float StatusRefreshIntervalSeconds = 0.1f;
}


void UMotionBridgeStatusWidget::SetReceiverComponent(
    UMotionBridgeReceiverComponent* InReceiver)
{
    ReceiverComponent = InReceiver;
    RefreshDisplay();
}


TSharedRef<SWidget> UMotionBridgeStatusWidget::RebuildWidget()
{
    if (WidgetTree && !WidgetTree->RootWidget)
    {
        BuildWidgetTree();
    }
    return Super::RebuildWidget();
}


void UMotionBridgeStatusWidget::NativeConstruct()
{
    Super::NativeConstruct();
    RefreshDisplay();
}


void UMotionBridgeStatusWidget::NativeTick(
    const FGeometry& MyGeometry,
    const float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    RefreshElapsedSeconds += InDeltaTime;
    if (RefreshElapsedSeconds < StatusRefreshIntervalSeconds)
    {
        return;
    }

    RefreshElapsedSeconds = 0.0f;
    RefreshDisplay();
}


void UMotionBridgeStatusWidget::BuildWidgetTree()
{
    UCanvasPanel* RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(
        UCanvasPanel::StaticClass(),
        TEXT("RootCanvas"));
    WidgetTree->RootWidget = RootCanvas;

    PanelBorder = WidgetTree->ConstructWidget<UBorder>(
        UBorder::StaticClass(),
        TEXT("PanelBorder"));
    PanelBorder->SetBrushColor(FLinearColor(0.015f, 0.02f, 0.035f, 0.92f));
    PanelBorder->SetPadding(FMargin(18.0f, 14.0f));

    UCanvasPanelSlot* PanelSlot = RootCanvas->AddChildToCanvas(PanelBorder);
    PanelSlot->SetAnchors(FAnchors(0.0f, 0.0f));
    PanelSlot->SetAlignment(FVector2D(0.0f, 0.0f));
    PanelSlot->SetPosition(FVector2D(24.0f, 24.0f));
    PanelSlot->SetSize(FVector2D(610.0f, 305.0f));

    UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(
        UVerticalBox::StaticClass(),
        TEXT("Content"));
    PanelBorder->SetContent(Content);

    UTextBlock* TitleText = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(),
        TEXT("TitleText"));
    TitleText->SetText(FText::FromString(TEXT("MOTIONBRIDGE / REALTIME STREAM")));
    TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.82f, 1.0f)));
    FSlateFontInfo TitleFont = TitleText->GetFont();
    TitleFont.Size = 20;
    TitleText->SetFont(TitleFont);
    Content->AddChildToVerticalBox(TitleText)->SetPadding(
        FMargin(0.0f, 0.0f, 0.0f, 8.0f));

    StatusText = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(),
        TEXT("StatusText"));
    FSlateFontInfo StatusFont = StatusText->GetFont();
    StatusFont.Size = 18;
    StatusText->SetFont(StatusFont);
    Content->AddChildToVerticalBox(StatusText)->SetPadding(
        FMargin(0.0f, 0.0f, 0.0f, 10.0f));

    StatsText = WidgetTree->ConstructWidget<UTextBlock>(
        UTextBlock::StaticClass(),
        TEXT("StatsText"));
    StatsText->SetAutoWrapText(true);
    StatsText->SetColorAndOpacity(FSlateColor(FLinearColor(0.88f, 0.9f, 0.95f)));
    FSlateFontInfo StatsFont = StatsText->GetFont();
    StatsFont.Size = 15;
    StatsText->SetFont(StatsFont);
    Content->AddChildToVerticalBox(StatsText);
}


void UMotionBridgeStatusWidget::RefreshDisplay()
{
    if (!StatusText || !StatsText)
    {
        return;
    }

    UMotionBridgeReceiverComponent* Receiver = ReceiverComponent.Get();
    if (!Receiver)
    {
        StatusText->SetText(FText::FromString(TEXT("RECEIVER NOT FOUND")));
        StatusText->SetColorAndOpacity(
            FSlateColor(FLinearColor(1.0f, 0.35f, 0.28f)));
        StatsText->SetText(FText::GetEmpty());
        return;
    }

    FMotionBridgeStreamStats Stats;
    Receiver->GetStreamStats(Stats);
    const bool bHasAcceptedFrame = Stats.AcceptedFrames > 0;
    const TCHAR* Status = Stats.bStreamLive
        ? TEXT("LIVE")
        : (bHasAcceptedFrame ? TEXT("TIMED OUT") : TEXT("WAITING"));
    const FLinearColor StatusColor = Stats.bStreamLive
        ? FLinearColor(0.25f, 1.0f, 0.45f)
        : (bHasAcceptedFrame
            ? FLinearColor(1.0f, 0.3f, 0.22f)
            : FLinearColor(1.0f, 0.72f, 0.2f));

    StatusText->SetText(FText::Format(
        NSLOCTEXT("MotionBridge", "StreamStatus", "STATUS  {0}"),
        FText::FromString(Status)));
    StatusText->SetColorAndOpacity(FSlateColor(StatusColor));

    StatsText->SetText(FText::FromString(FString::Printf(
        TEXT("UDP        received %lld    rejected %lld\n")
        TEXT("WEBSOCKET  received %lld    rejected %lld    %s\n")
        TEXT("FRAMES     accepted %lld    rejected %lld\n")
        TEXT("REALTIME   input %.1f Hz    game %.1f FPS\n")
        TEXT("LATENCY    latest %.1f ms    average %.1f ms    max %.1f ms\n")
        TEXT("QUALITY    lost %lld    duplicate %lld    out-of-order %lld\n")
        TEXT("BUFFER     %d/%d    delay %.1f ms    max %.1f ms\n")
        TEXT("RECOVERY   hard resync %lld    reconnect %lld"),
        Stats.ReceivedUdpPackets,
        Stats.RejectedUdpPackets,
        Stats.ReceivedWebSocketMessages,
        Stats.RejectedWebSocketMessages,
        Stats.bWebSocketConnected ? TEXT("CONNECTED") : TEXT("DISCONNECTED"),
        Stats.AcceptedFrames,
        Stats.RejectedFrames,
        Stats.IncomingFrameRateHz,
        Stats.GameThreadFps,
        Stats.LatestEndToEndLatencyMs,
        Stats.AverageEndToEndLatencyMs,
        Stats.MaxEndToEndLatencyMs,
        Stats.EstimatedLostFrames,
        Stats.DuplicateFrames,
        Stats.OutOfOrderFrames,
        Stats.BufferedFrames,
        FMath::Max(Receiver->BufferCapacity, 1),
        Stats.CurrentBufferDelaySeconds * 1000.0,
        Stats.MaxBufferDelaySeconds * 1000.0,
        Stats.HardResyncCount,
        Stats.WebSocketReconnectAttempts)));

    const FString StatusKey = Stats.bStreamLive
        ? TEXT("LIVE")
        : (bHasAcceptedFrame ? TEXT("TIMED_OUT") : TEXT("WAITING"));
    if (LastReportedStatus != StatusKey)
    {
        LastReportedStatus = StatusKey;
        UE_LOG(
            LogTemp,
            Display,
            TEXT("MOTIONBRIDGE_UMG_STATE=%s|udp=%lld|ws=%lld|accepted=%lld|buffered=%d|delay_ms=%.1f|latency_ms=%.1f|input_hz=%.1f|game_fps=%.1f"),
            *StatusKey,
            Stats.ReceivedUdpPackets,
            Stats.ReceivedWebSocketMessages,
            Stats.AcceptedFrames,
            Stats.BufferedFrames,
            Stats.CurrentBufferDelaySeconds * 1000.0,
            Stats.AverageEndToEndLatencyMs,
            Stats.IncomingFrameRateHz,
            Stats.GameThreadFps);

        if (FParse::Param(
                FCommandLine::Get(),
                TEXT("MotionBridgeCaptureUMG")))
        {
            const FString ScreenshotDirectory = FPaths::Combine(
                FPaths::ProjectSavedDir(),
                TEXT("Screenshots/WindowsEditor"));
            IFileManager::Get().MakeDirectory(
                *ScreenshotDirectory,
                true);
            const FString ScreenshotPath = FPaths::Combine(
                ScreenshotDirectory,
                FString::Printf(
                    TEXT("MotionBridgeUMG_%s.png"),
                    *StatusKey));
            FScreenshotRequest::RequestScreenshot(
                ScreenshotPath,
                true,
                false);
            UE_LOG(
                LogTemp,
                Display,
                TEXT("MOTIONBRIDGE_UMG_SCREENSHOT=REQUESTED|state=%s|path=%s"),
                *StatusKey,
                *ScreenshotPath);
        }
    }
}
