#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MotionBridgeStatusWidget.generated.h"

class UBorder;
class UMotionBridgeReceiverComponent;
class UTextBlock;

/** 在本地玩家视口中显示实时动作流状态。 */
UCLASS()
class TP_THIRDPERSON_API UMotionBridgeStatusWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    void SetReceiverComponent(UMotionBridgeReceiverComponent* InReceiver);

protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;

    virtual void NativeConstruct() override;

    virtual void NativeTick(
        const FGeometry& MyGeometry,
        float InDeltaTime) override;

private:
    /** Widget 只观察 Receiver，不取得它的生命周期所有权。 */
    UPROPERTY(Transient)
    TWeakObjectPtr<UMotionBridgeReceiverComponent> ReceiverComponent;

    UPROPERTY(Transient)
    TObjectPtr<UBorder> PanelBorder;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> StatusText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> StatsText;

    float RefreshElapsedSeconds = 0.0f;
    FString LastReportedStatus;

    void BuildWidgetTree();
    void RefreshDisplay();
};
