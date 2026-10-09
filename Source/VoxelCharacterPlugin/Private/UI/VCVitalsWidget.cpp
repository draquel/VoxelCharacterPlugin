// Copyright Daniel Raquel. All Rights Reserved.

#include "UI/VCVitalsWidget.h"
#include "Core/VCCharacterBase.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Combat/VCCombatComponent.h"
#include "AbilitySystemComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"

namespace
{
	UProgressBar* MakeBar(UWidgetTree* Tree, UVerticalBox* Parent, TObjectPtr<UTextBlock>& OutText, const FLinearColor& Color, const TCHAR* Name)
	{
		USizeBox* Size = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), FName(FString::Printf(TEXT("%sSize"), Name)));
		Size->SetWidthOverride(260.f);
		Size->SetHeightOverride(22.f);

		UOverlay* Overlay = Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), FName(FString::Printf(TEXT("%sOverlay"), Name)));
		Size->AddChild(Overlay);

		UProgressBar* Bar = Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), FName(FString::Printf(TEXT("%sBar"), Name)));
		Bar->SetFillColorAndOpacity(Color);
		Bar->SetPercent(1.f);
		if (UOverlaySlot* BarSlot = Overlay->AddChildToOverlay(Bar))
		{
			BarSlot->SetHorizontalAlignment(HAlign_Fill);
			BarSlot->SetVerticalAlignment(VAlign_Fill);
		}

		OutText = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(FString::Printf(TEXT("%sText"), Name)));
		FSlateFontInfo Font = OutText->GetFont();
		Font.Size = 12;
		OutText->SetFont(Font);
		OutText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		OutText->SetShadowOffset(FVector2D(1.f, 1.f));
		OutText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
		if (UOverlaySlot* TextSlot = Overlay->AddChildToOverlay(OutText))
		{
			TextSlot->SetHorizontalAlignment(HAlign_Center);
			TextSlot->SetVerticalAlignment(VAlign_Center);
		}

		if (UVerticalBoxSlot* BoxSlot = Parent->AddChildToVerticalBox(Size))
		{
			BoxSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
		}
		return Bar;
	}
}

void UVCVitalsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UVCVitalsWidget::NativeDestruct()
{
	Unbind();
	Super::NativeDestruct();
}

void UVCVitalsWidget::BuildWidgetTree()
{
	// Idempotent: NativeOnInitialized only runs for widgets that have a player context, so a widget
	// created without one (automation tests) builds its tree on the first InitWithCharacter instead.
	if (!WidgetTree || HealthBar)
	{
		return;
	}
	UVerticalBox* Root = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("VitalsRoot"));
	WidgetTree->RootWidget = Root;

	HealthBar = MakeBar(WidgetTree, Root, HealthText, FLinearColor(0.75f, 0.12f, 0.12f), TEXT("Health"));
	StaminaBar = MakeBar(WidgetTree, Root, StaminaText, FLinearColor(0.15f, 0.6f, 0.2f), TEXT("Stamina"));

	EditModeText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("EditModeText"));
	FSlateFontInfo EditFont = EditModeText->GetFont();
	EditFont.Size = 14;
	EditModeText->SetFont(EditFont);
	EditModeText->SetText(FText::FromString(TEXT("EDIT")));
	EditModeText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.75f, 0.1f)));
	EditModeText->SetShadowOffset(FVector2D(1.f, 1.f));
	EditModeText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
	if (UVerticalBoxSlot* EditSlot = Root->AddChildToVerticalBox(EditModeText))
	{
		EditSlot->SetPadding(FMargin(2.f, 2.f, 0.f, 0.f));
	}

	SetHealth(0.f, 0.f);
	SetStamina(0.f, 0.f);
	SetEditModeShown(false);
}

void UVCVitalsWidget::InitWithCharacter(AVCCharacterBase* Character)
{
	BuildWidgetTree();
	Unbind();
	if (!Character)
	{
		return;
	}

	BoundCharacter = Character;
	if (Character->CombatComponent)
	{
		Character->CombatComponent->OnHealthChanged.AddDynamic(this, &UVCVitalsWidget::HandleHealthChanged);
	}
	Character->OnEditModeChanged.AddDynamic(this, &UVCVitalsWidget::HandleEditModeChanged);

	if (UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent())
	{
		BoundASC = ASC;
		StaminaHandle = ASC->GetGameplayAttributeValueChangeDelegate(UVCCharacterAttributeSet::GetStaminaAttribute())
			.AddUObject(this, &UVCVitalsWidget::HandleStaminaChanged);
		MaxStaminaHandle = ASC->GetGameplayAttributeValueChangeDelegate(UVCCharacterAttributeSet::GetMaxStaminaAttribute())
			.AddUObject(this, &UVCVitalsWidget::HandleMaxStaminaChanged);
	}

	RefreshFromCharacter();
}

void UVCVitalsWidget::Unbind()
{
	if (AVCCharacterBase* Character = BoundCharacter.Get())
	{
		if (Character->CombatComponent)
		{
			Character->CombatComponent->OnHealthChanged.RemoveDynamic(this, &UVCVitalsWidget::HandleHealthChanged);
		}
		Character->OnEditModeChanged.RemoveDynamic(this, &UVCVitalsWidget::HandleEditModeChanged);
	}
	SetEditModeShown(false);
	if (UAbilitySystemComponent* ASC = BoundASC.Get())
	{
		ASC->GetGameplayAttributeValueChangeDelegate(UVCCharacterAttributeSet::GetStaminaAttribute()).Remove(StaminaHandle);
		ASC->GetGameplayAttributeValueChangeDelegate(UVCCharacterAttributeSet::GetMaxStaminaAttribute()).Remove(MaxStaminaHandle);
	}
	StaminaHandle.Reset();
	MaxStaminaHandle.Reset();
	BoundCharacter.Reset();
	BoundASC.Reset();
}

void UVCVitalsWidget::RefreshFromCharacter()
{
	AVCCharacterBase* Character = BoundCharacter.Get();
	UAbilitySystemComponent* ASC = BoundASC.Get();
	SetEditModeShown(Character && Character->IsEditModeEnabled());
	if (!Character || !ASC)
	{
		return;
	}
	SetHealth(ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetHealthAttribute()),
		ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetMaxHealthAttribute()));
	SetStamina(ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetStaminaAttribute()),
		ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetMaxStaminaAttribute()));
}

void UVCVitalsWidget::HandleHealthChanged(float NewHealth, float OldHealth, float MaxHealth)
{
	SetHealth(NewHealth, MaxHealth);
}

void UVCVitalsWidget::HandleEditModeChanged(bool bEnabled)
{
	SetEditModeShown(bEnabled);
}

bool UVCVitalsWidget::IsEditModeShown() const
{
	return EditModeText && EditModeText->GetVisibility() != ESlateVisibility::Collapsed;
}

void UVCVitalsWidget::SetEditModeShown(bool bShown)
{
	if (EditModeText)
	{
		EditModeText->SetVisibility(bShown ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
}

void UVCVitalsWidget::HandleStaminaChanged(const FOnAttributeChangeData& Data)
{
	const UAbilitySystemComponent* ASC = BoundASC.Get();
	SetStamina(Data.NewValue, ASC ? ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetMaxStaminaAttribute()) : 0.f);
}

void UVCVitalsWidget::HandleMaxStaminaChanged(const FOnAttributeChangeData& Data)
{
	const UAbilitySystemComponent* ASC = BoundASC.Get();
	SetStamina(ASC ? ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetStaminaAttribute()) : 0.f, Data.NewValue);
}

void UVCVitalsWidget::SetHealth(float Current, float Max)
{
	if (HealthBar)
	{
		HealthBar->SetPercent(Max > 0.f ? FMath::Clamp(Current / Max, 0.f, 1.f) : 0.f);
	}
	if (HealthText)
	{
		HealthText->SetText(FText::FromString(FString::Printf(TEXT("%.0f / %.0f"), Current, Max)));
	}
}

void UVCVitalsWidget::SetStamina(float Current, float Max)
{
	if (StaminaBar)
	{
		StaminaBar->SetPercent(Max > 0.f ? FMath::Clamp(Current / Max, 0.f, 1.f) : 0.f);
	}
	if (StaminaText)
	{
		StaminaText->SetText(FText::FromString(FString::Printf(TEXT("%.0f / %.0f"), Current, Max)));
	}
}
