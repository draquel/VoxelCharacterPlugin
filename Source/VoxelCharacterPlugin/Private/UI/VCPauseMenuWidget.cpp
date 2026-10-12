// Copyright Daniel Raquel. All Rights Reserved.

#include "UI/VCPauseMenuWidget.h"
#include "Core/VCPlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
	UTextBlock* MakePauseLabel(UWidgetTree* Tree, const TCHAR* Name, const FText& Text, int32 Size)
	{
		UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Label->SetText(Text);
		FSlateFontInfo Font = Label->GetFont();
		Font.Size = Size;
		Label->SetFont(Font);
		Label->SetJustification(ETextJustify::Center);
		return Label;
	}

	UButton* MakePauseButton(UWidgetTree* Tree, UVerticalBox* Column, const TCHAR* Name, UTextBlock* Label)
	{
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		Button->AddChild(Label);
		if (UVerticalBoxSlot* ButtonSlot = Column->AddChildToVerticalBox(Button))
		{
			ButtonSlot->SetPadding(FMargin(0.f, 4.f));
			ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		return Button;
	}
}

void UVCPauseMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UVCPauseMenuWidget::BuildWidgetTree()
{
	if (!WidgetTree || TitleText)
	{
		return;
	}
	UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PauseBorder"));
	Root->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.06f, 0.92f));
	Root->SetPadding(FMargin(24.f, 18.f));
	WidgetTree->RootWidget = Root;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PauseColumn"));
	Root->AddChild(Column);

	TitleText = MakePauseLabel(WidgetTree, TEXT("PauseTitle"), NSLOCTEXT("VCPause", "Paused", "Paused"), 24);
	if (UVerticalBoxSlot* TitleSlot = Column->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 12.f));
	}

	ResumeButton = MakePauseButton(WidgetTree, Column, TEXT("ResumeButton"),
		MakePauseLabel(WidgetTree, TEXT("ResumeLabel"), NSLOCTEXT("VCPause", "Resume", "Resume"), 16));
	SaveButton = MakePauseButton(WidgetTree, Column, TEXT("SaveButton"),
		MakePauseLabel(WidgetTree, TEXT("SaveLabel"), NSLOCTEXT("VCPause", "Save", "Save"), 16));
	QuitButton = MakePauseButton(WidgetTree, Column, TEXT("QuitButton"),
		MakePauseLabel(WidgetTree, TEXT("QuitLabel"), NSLOCTEXT("VCPause", "Quit", "Quit"), 16));
	ResumeButton->OnClicked.AddDynamic(this, &UVCPauseMenuWidget::HandleResumeClicked);
	SaveButton->OnClicked.AddDynamic(this, &UVCPauseMenuWidget::HandleSaveClicked);
	QuitButton->OnClicked.AddDynamic(this, &UVCPauseMenuWidget::HandleQuitClicked);
}

void UVCPauseMenuWidget::SetPausedState(bool bPaused)
{
	BuildWidgetTree();
	if (TitleText)
	{
		TitleText->SetText(bPaused ? NSLOCTEXT("VCPause", "Paused", "Paused")
			: NSLOCTEXT("VCPause", "MenuLive", "Menu (the game keeps running)"));
	}
}

void UVCPauseMenuWidget::HandleResumeClicked()
{
	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetOwningPlayer()))
	{
		PC->ResumeFromPauseMenu();
	}
}

void UVCPauseMenuWidget::HandleSaveClicked()
{
	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetOwningPlayer()))
	{
		PC->RequestSaveGame();
	}
}

void UVCPauseMenuWidget::HandleQuitClicked()
{
	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetOwningPlayer()))
	{
		PC->QuitFromPauseMenu();
	}
}
