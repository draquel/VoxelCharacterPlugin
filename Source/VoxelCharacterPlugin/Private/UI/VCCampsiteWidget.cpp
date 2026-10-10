// Copyright Daniel Raquel. All Rights Reserved.

#include "UI/VCCampsiteWidget.h"
#include "Core/VCCharacterBase.h"
#include "Core/VCPlayerController.h"
#include "UI/VCCraftingPanelWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Interfaces/CGFRestPointInterface.h"

namespace
{
	UTextBlock* MakeLabel(UWidgetTree* Tree, const TCHAR* Name, const FText& Text, int32 Size)
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = Size;
		Label->SetFont(Font);
		Label->SetText(Text);
		Label->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		Label->SetShadowOffset(FVector2D(1.f, 1.f));
		Label->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
		return Label;
	}

	UButton* MakeButton(UWidgetTree* Tree, UHorizontalBox* Row, const TCHAR* Name, UTextBlock* Label)
	{
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		Button->AddChild(Label);
		if (UHorizontalBoxSlot* ButtonSlot = Row->AddChildToHorizontalBox(Button))
		{
			ButtonSlot->SetPadding(FMargin(0.f, 0.f, 8.f, 0.f));
		}
		return Button;
	}
}

void UVCCampsiteWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UVCCampsiteWidget::BuildWidgetTree()
{
	if (!WidgetTree || TitleText)
	{
		return;
	}
	UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CampsiteBorder"));
	Root->SetBrushColor(FLinearColor(0.08f, 0.06f, 0.04f, 0.9f));
	Root->SetPadding(FMargin(12.f));
	WidgetTree->RootWidget = Root;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CampsiteColumn"));
	Root->AddChild(Column);

	TitleText = MakeLabel(WidgetTree, TEXT("CampsiteTitle"), NSLOCTEXT("VCCampsite", "Title", "Campsite"), 20);
	TitleText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.8f, 0.45f)));
	if (UVerticalBoxSlot* TitleSlot = Column->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 8.f));
	}

	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("CampsiteButtons"));
	RestButton = MakeButton(WidgetTree, Buttons, TEXT("RestButton"),
		MakeLabel(WidgetTree, TEXT("RestLabel"), NSLOCTEXT("VCCampsite", "Rest", "Rest"), 13));
	SleepLabel = MakeLabel(WidgetTree, TEXT("SleepLabel"), NSLOCTEXT("VCCampsite", "Sleep", "Sleep until dawn"), 13);
	SleepButton = MakeButton(WidgetTree, Buttons, TEXT("SleepButton"), SleepLabel);
	CloseButton = MakeButton(WidgetTree, Buttons, TEXT("CloseButton"),
		MakeLabel(WidgetTree, TEXT("CloseLabel"), NSLOCTEXT("VCCampsite", "Close", "Close"), 13));
	RestButton->OnClicked.AddDynamic(this, &UVCCampsiteWidget::HandleRestClicked);
	SleepButton->OnClicked.AddDynamic(this, &UVCCampsiteWidget::HandleSleepClicked);
	CloseButton->OnClicked.AddDynamic(this, &UVCCampsiteWidget::HandleCloseClicked);
	if (UVerticalBoxSlot* ButtonsSlot = Column->AddChildToVerticalBox(Buttons))
	{
		ButtonsSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 10.f));
	}

	TSubclassOf<UVCCraftingPanelWidget> PanelClass = CraftingPanelClass ? CraftingPanelClass : TSubclassOf<UVCCraftingPanelWidget>(UVCCraftingPanelWidget::StaticClass());
	CraftingPanel = CreateWidget<UVCCraftingPanelWidget>(this, PanelClass);
	if (CraftingPanel)
	{
		Column->AddChildToVerticalBox(CraftingPanel);
	}
}

void UVCCampsiteWidget::InitPanel(AVCCharacterBase* Character, AActor* InRestPoint)
{
	BuildWidgetTree();
	BoundCharacter = Character;
	RestPoint = InRestPoint;
	if (CraftingPanel)
	{
		CraftingPanel->InitPanel(Character, InRestPoint);
	}
	RefreshButtons();
}

void UVCCampsiteWidget::RefreshButtons()
{
	AActor* Point = RestPoint.Get();
	const bool bRestPoint = Point && Point->Implements<UCGFRestPointInterface>();
	const bool bCanSleep = bRestPoint && ICGFRestPointInterface::Execute_CanSleepNow(Point);
	if (RestButton)
	{
		RestButton->SetIsEnabled(bRestPoint);
	}
	if (SleepButton)
	{
		SleepButton->SetIsEnabled(bCanSleep);
	}
	if (SleepLabel)
	{
		SleepLabel->SetText(bCanSleep
			? NSLOCTEXT("VCCampsite", "Sleep", "Sleep until dawn")
			: NSLOCTEXT("VCCampsite", "SleepDay", "Sleep (night only)"));
	}
}

void UVCCampsiteWidget::HandleRestClicked()
{
	if (AVCCharacterBase* Character = BoundCharacter.Get())
	{
		Character->RequestRest(RestPoint.Get());
	}
}

void UVCCampsiteWidget::HandleSleepClicked()
{
	if (AVCCharacterBase* Character = BoundCharacter.Get())
	{
		Character->RequestSleep(RestPoint.Get());
	}
	RefreshButtons();
}

void UVCCampsiteWidget::HandleCloseClicked()
{
	if (AVCPlayerController* PC = GetOwningPlayer<AVCPlayerController>())
	{
		PC->CloseCampsiteUI();
	}
}
