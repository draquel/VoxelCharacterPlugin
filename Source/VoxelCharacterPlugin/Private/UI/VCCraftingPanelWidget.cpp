// Copyright Daniel Raquel. All Rights Reserved.

#include "UI/VCCraftingPanelWidget.h"
#include "Core/VCCharacterBase.h"
#include "VoxelCharacterPlugin.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Interfaces/CGFRestPointInterface.h"

#if WITH_INVENTORY_PLUGIN
#include "Components/InventoryComponent.h"
#include "Data/CraftingRecipe.h"
#include "Data/ItemDefinition.h"
#include "Subsystems/CraftingSubsystem.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#endif

namespace
{
	UTextBlock* MakeText(UWidgetTree* Tree, const TCHAR* Name, int32 Size, const FLinearColor& Color)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = Size;
		Text->SetFont(Font);
		Text->SetColorAndOpacity(FSlateColor(Color));
		Text->SetShadowOffset(FVector2D(1.f, 1.f));
		Text->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.8f));
		return Text;
	}
}

// ---------------------------------------------------------------------------
// Row
// ---------------------------------------------------------------------------

void UVCCraftingRowWidget::BuildWidgetTree()
{
	if (!WidgetTree || NameText)
	{
		return;
	}
	UHorizontalBox* Root = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("RowRoot"));
	WidgetTree->RootWidget = Root;

	UVerticalBox* TextBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RowText"));
	NameText = MakeText(WidgetTree, TEXT("RecipeName"), 14, FLinearColor::White);
	SummaryText = MakeText(WidgetTree, TEXT("RecipeSummary"), 11, FLinearColor(0.8f, 0.8f, 0.8f));
	TextBox->AddChildToVerticalBox(NameText);
	TextBox->AddChildToVerticalBox(SummaryText);
	if (UHorizontalBoxSlot* TextSlot = Root->AddChildToHorizontalBox(TextBox))
	{
		TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TextSlot->SetPadding(FMargin(0.f, 2.f, 8.f, 2.f));
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}

	CraftButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("CraftButton"));
	CraftLabel = MakeText(WidgetTree, TEXT("CraftLabel"), 12, FLinearColor::White);
	CraftLabel->SetText(NSLOCTEXT("VCCrafting", "Craft", "Craft"));
	CraftButton->AddChild(CraftLabel);
	CraftButton->OnClicked.AddDynamic(this, &UVCCraftingRowWidget::HandleCraftClicked);
	if (UHorizontalBoxSlot* ButtonSlot = Root->AddChildToHorizontalBox(CraftButton))
	{
		ButtonSlot->SetPadding(FMargin(0.f, 2.f));
		ButtonSlot->SetVerticalAlignment(VAlign_Center);
	}
}

void UVCCraftingRowWidget::InitRow(UVCCraftingPanelWidget* Panel, FPrimaryAssetId InRecipeId, const FText& Name)
{
	BuildWidgetTree();
	OwnerPanel = Panel;
	RecipeId = InRecipeId;
	if (NameText)
	{
		NameText->SetText(Name);
	}
}

void UVCCraftingRowWidget::Refresh(const FText& Summary, bool bCanCraft)
{
	if (SummaryText)
	{
		SummaryText->SetText(Summary);
		SummaryText->SetColorAndOpacity(FSlateColor(bCanCraft ? FLinearColor(0.75f, 0.95f, 0.75f) : FLinearColor(0.85f, 0.6f, 0.55f)));
	}
	if (CraftButton)
	{
		CraftButton->SetIsEnabled(bCanCraft);
	}
}

void UVCCraftingRowWidget::HandleCraftClicked()
{
	if (UVCCraftingPanelWidget* Panel = OwnerPanel.Get())
	{
		Panel->RequestCraft(RecipeId);
	}
}

// ---------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------

void UVCCraftingPanelWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UVCCraftingPanelWidget::NativeDestruct()
{
	Unbind();
	Super::NativeDestruct();
}

void UVCCraftingPanelWidget::BuildWidgetTree()
{
	if (!WidgetTree || RowsBox)
	{
		return;
	}
	UBorder* Root = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("CraftingBorder"));
	Root->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.1f, 0.85f));
	Root->SetPadding(FMargin(10.f));
	WidgetTree->RootWidget = Root;

	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CraftingColumn"));
	Root->AddChild(Column);

	TitleText = MakeText(WidgetTree, TEXT("CraftingTitle"), 18, FLinearColor(1.f, 0.9f, 0.6f));
	TitleText->SetText(PanelTitle);
	if (UVerticalBoxSlot* TitleSlot = Column->AddChildToVerticalBox(TitleText))
	{
		TitleSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 6.f));
	}

	RowsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("CraftingRows"));
	Column->AddChildToVerticalBox(RowsBox);

	EmptyText = MakeText(WidgetTree, TEXT("CraftingEmpty"), 12, FLinearColor(0.7f, 0.7f, 0.7f));
	EmptyText->SetText(NSLOCTEXT("VCCrafting", "NoRecipes", "Nothing to craft here."));
	EmptyText->SetVisibility(ESlateVisibility::Collapsed);
	Column->AddChildToVerticalBox(EmptyText);
}

FGameplayTag UVCCraftingPanelWidget::ResolveStationTag() const
{
	AActor* StationActor = Station.Get();
	if (StationActor && StationActor->Implements<UCGFRestPointInterface>())
	{
		return ICGFRestPointInterface::Execute_GetCraftingStationTag(StationActor);
	}
	return FGameplayTag();
}

void UVCCraftingPanelWidget::Unbind()
{
#if WITH_INVENTORY_PLUGIN
	if (AVCCharacterBase* Character = BoundCharacter.Get())
	{
		if (Character->InventoryComponent)
		{
			Character->InventoryComponent->OnInventoryChanged.RemoveDynamic(this, &UVCCraftingPanelWidget::HandleInventoryChanged);
		}
	}
#endif
	BoundCharacter.Reset();
}

void UVCCraftingPanelWidget::InitPanel(AVCCharacterBase* Character, AActor* InStation)
{
	BuildWidgetTree();
	Unbind();
	Station = InStation;
	BoundCharacter = Character;
	if (RowsBox)
	{
		RowsBox->ClearChildren();
	}
	Rows.Reset();
	if (!Character)
	{
		if (EmptyText)
		{
			EmptyText->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		return;
	}

#if WITH_INVENTORY_PLUGIN
	if (Character->InventoryComponent)
	{
		Character->InventoryComponent->OnInventoryChanged.AddDynamic(this, &UVCCraftingPanelWidget::HandleInventoryChanged);
	}
	UGameInstance* GameInstance = Character->GetGameInstance();
	UCraftingSubsystem* Crafting = GameInstance ? GameInstance->GetSubsystem<UCraftingSubsystem>() : nullptr;
	UItemDatabaseSubsystem* ItemDB = GameInstance ? GameInstance->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	if (Crafting && RowsBox)
	{
		for (UCraftingRecipe* Recipe : Crafting->GetRecipesForStation(ResolveStationTag()))
		{
			FText Name = Recipe->DisplayName;
			if (Name.IsEmpty())
			{
				const UItemDefinition* OutDef = ItemDB ? ItemDB->GetDefinition(Recipe->OutputItemId) : nullptr;
				Name = OutDef ? OutDef->DisplayName : FText::FromName(Recipe->OutputItemId.PrimaryAssetName);
			}
			UVCCraftingRowWidget* Row = CreateWidget<UVCCraftingRowWidget>(this, UVCCraftingRowWidget::StaticClass());
			if (!Row)
			{
				continue;
			}
			Row->InitRow(this, Recipe->GetPrimaryAssetId(), Name);
			if (UVerticalBoxSlot* RowSlot = RowsBox->AddChildToVerticalBox(Row))
			{
				RowSlot->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));
			}
			Rows.Add(Row);
		}
	}
#endif
	if (EmptyText)
	{
		EmptyText->SetVisibility(Rows.Num() == 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	}
	RefreshRows();
}

void UVCCraftingPanelWidget::RefreshRows()
{
#if WITH_INVENTORY_PLUGIN
	AVCCharacterBase* Character = BoundCharacter.Get();
	UGameInstance* GameInstance = Character ? Character->GetGameInstance() : nullptr;
	UCraftingSubsystem* Crafting = GameInstance ? GameInstance->GetSubsystem<UCraftingSubsystem>() : nullptr;
	UItemDatabaseSubsystem* ItemDB = GameInstance ? GameInstance->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	UInventoryComponent* Inventory = Character ? Character->InventoryComponent : nullptr;
	if (!Crafting || !Inventory)
	{
		return;
	}
	const FGameplayTag StationTag = ResolveStationTag();
	for (UVCCraftingRowWidget* Row : Rows)
	{
		const UCraftingRecipe* Recipe = Row ? Crafting->FindRecipe(Row->GetRecipeId()) : nullptr;
		if (!Recipe)
		{
			continue;
		}
		// "Wood 2/4, Stone 3/3"
		TArray<FString> Parts;
		for (const FCraftingIngredient& Ingredient : Recipe->Ingredients)
		{
			const UItemDefinition* Def = ItemDB ? ItemDB->GetDefinition(Ingredient.ItemId) : nullptr;
			const FString ItemName = Def ? Def->DisplayName.ToString() : Ingredient.ItemId.PrimaryAssetName.ToString();
			Parts.Add(FString::Printf(TEXT("%s %d/%d"), *ItemName, Inventory->GetItemCount(Ingredient.ItemId), Ingredient.Count));
		}
		const bool bCanCraft = Crafting->CanCraft(Inventory, Recipe, StationTag) == ECraftResult::Success;
		Row->Refresh(FText::FromString(FString::Join(Parts, TEXT(", "))), bCanCraft);
	}
#endif
}

void UVCCraftingPanelWidget::RequestCraft(FPrimaryAssetId RecipeId)
{
	if (AVCCharacterBase* Character = BoundCharacter.Get())
	{
		Character->RequestCraft(RecipeId, Station.Get());
	}
}

void UVCCraftingPanelWidget::HandleInventoryChanged()
{
	RefreshRows();
}
