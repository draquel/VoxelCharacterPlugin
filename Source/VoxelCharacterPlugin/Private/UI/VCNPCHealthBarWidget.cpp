// Copyright Daniel Raquel. All Rights Reserved.

#include "UI/VCNPCHealthBarWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"

void UVCNPCHealthBarWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UVCNPCHealthBarWidget::BuildWidgetTree()
{
	if (!WidgetTree || Bar)
	{
		return;
	}
	USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("HealthBarSize"));
	Size->SetWidthOverride(BarWidth);
	Size->SetHeightOverride(BarHeight);
	WidgetTree->RootWidget = Size;

	Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("HealthBar"));
	Bar->SetFillColorAndOpacity(FLinearColor(0.8f, 0.12f, 0.12f));
	Bar->SetPercent(1.f);
	Size->AddChild(Bar);
}

void UVCNPCHealthBarWidget::SetHealth(float Health, float MaxHealth)
{
	BuildWidgetTree();
	if (Bar)
	{
		Bar->SetPercent(MaxHealth > 0.f ? FMath::Clamp(Health / MaxHealth, 0.f, 1.f) : 0.f);
	}
}
