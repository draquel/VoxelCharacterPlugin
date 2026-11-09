// Copyright Daniel Raquel. All Rights Reserved.

#include "UI/VCReticleWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Core/VCCharacterBase.h"
#include "Engine/World.h"
#include "TimerManager.h"

namespace
{
	constexpr float DotSize = 5.f;
	constexpr float TickLength = 12.f;
	constexpr float TickThickness = 3.f;
	constexpr float HostileCheckInterval = 0.1f; // a sweep ten times a second is plenty for a colour
	constexpr float UpdateInterval = 1.f / 30.f;   // the timer that stands in for a tick
}

float UVCReticleWidget::ComputeSpread(float DrawPower, float InOpenSpread, float InClosedSpread)
{
	return FMath::Lerp(InOpenSpread, InClosedSpread, FMath::Clamp(DrawPower, 0.f, 1.f));
}

void UVCReticleWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(UpdateTimer, this, &UVCReticleWidget::UpdateReticle, UpdateInterval, true);
	}
}

void UVCReticleWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UpdateTimer);
	}
	Super::NativeDestruct();
}

void UVCReticleWidget::BuildWidgetTree()
{
	if (!WidgetTree || Dot)
	{
		return;
	}
	Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ReticleCanvas"));
	WidgetTree->RootWidget = Canvas;

	const auto MakeBlock = [this](const TCHAR* Name, const FVector2D& Size) -> UBorder*
	{
		UBorder* Block = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), Name);
		// A solid brush: a border's default brush has no resource and painted nothing in the first passes.
		Block->SetBrush(FSlateColorBrush(FLinearColor::White));
		Block->SetBrushColor(Color);
		Block->SetPadding(FMargin(0.f));
		if (UCanvasPanelSlot* BlockSlot = Canvas->AddChildToCanvas(Block))
		{
			BlockSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
			BlockSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			BlockSlot->SetSize(Size);
			BlockSlot->SetPosition(FVector2D::ZeroVector);
		}
		return Block;
	};
	Dot = MakeBlock(TEXT("ReticleDot"), FVector2D(DotSize, DotSize));
	Ticks.Add(MakeBlock(TEXT("ReticleTickUp"), FVector2D(TickThickness, TickLength)));
	Ticks.Add(MakeBlock(TEXT("ReticleTickDown"), FVector2D(TickThickness, TickLength)));
	Ticks.Add(MakeBlock(TEXT("ReticleTickLeft"), FVector2D(TickLength, TickThickness)));
	Ticks.Add(MakeBlock(TEXT("ReticleTickRight"), FVector2D(TickLength, TickThickness)));
	PlaceTicks(OpenSpread);
	// The widget itself stays visible (a collapsed widget is not ticked, so it could never show itself again); the
	// canvas with the marks is what hides.
	SetVisibility(ESlateVisibility::HitTestInvisible);
	Canvas->SetVisibility(ESlateVisibility::Collapsed);
	bShown = false;
}

void UVCReticleWidget::PlaceTicks(float Spread)
{
	if (Ticks.Num() < 4)
	{
		return;
	}
	const FVector2D Offsets[4] = { FVector2D(0.f, -Spread), FVector2D(0.f, Spread), FVector2D(-Spread, 0.f), FVector2D(Spread, 0.f) };
	for (int32 i = 0; i < 4; ++i)
	{
		if (UCanvasPanelSlot* TickSlot = Ticks[i] ? Cast<UCanvasPanelSlot>(Ticks[i]->Slot) : nullptr)
		{
			TickSlot->SetPosition(Offsets[i]);
		}
	}
	LastSpread = Spread;
}

void UVCReticleWidget::SetColor(const FLinearColor& InColor)
{
	if (Dot)
	{
		Dot->SetBrushColor(InColor);
	}
	for (UBorder* Tick : Ticks)
	{
		if (Tick)
		{
			Tick->SetBrushColor(InColor);
		}
	}
}

void UVCReticleWidget::UpdateReticle()
{
	const float InDeltaTime = UpdateInterval;
	const APlayerController* PC = GetOwningPlayer();
	const AVCCharacterBase* Character = PC ? Cast<AVCCharacterBase>(PC->GetPawn()) : nullptr;
	const bool bShow = Character && Character->IsAiming();
	if (bShow != bShown)
	{
		bShown = bShow;
		if (Canvas)
		{
			Canvas->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		}
		HostileCheckAccumulator = HostileCheckInterval; // check at once on showing
	}
	if (!bShow)
	{
		return;
	}
	const float Spread = ComputeSpread(Character->GetOwnDrawPower(), OpenSpread, ClosedSpread);
	if (!FMath::IsNearlyEqual(Spread, LastSpread, 0.1f))
	{
		PlaceTicks(Spread);
	}
	HostileCheckAccumulator += InDeltaTime;
	if (HostileCheckAccumulator >= HostileCheckInterval)
	{
		HostileCheckAccumulator = 0.f;
		const bool bHostile = Character->IsAimOnHostile();
		if (bHostile != bLastHostile || LastSpread < 0.f)
		{
			bLastHostile = bHostile;
			SetColor(bHostile ? HostileColor : Color);
		}
	}
}
