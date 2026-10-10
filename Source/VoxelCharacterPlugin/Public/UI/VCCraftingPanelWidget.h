// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "VCCraftingPanelWidget.generated.h"

class AVCCharacterBase;
class UButton;
class UTextBlock;
class UVerticalBox;
class UVCCraftingPanelWidget;

/**
 * One recipe line of the crafting panel: name, ingredient summary ("Wood 2/4, Stone 3/3") and a Craft
 * button that asks the character to craft the recipe (server RPC). Built in code.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCCraftingRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Build the row for a recipe; Panel owns the character / station context. */
	void InitRow(UVCCraftingPanelWidget* Panel, FPrimaryAssetId InRecipeId, const FText& Name);

	/** Refresh the ingredient summary and the button state from the inventory. */
	void Refresh(const FText& Summary, bool bCanCraft);

	FPrimaryAssetId GetRecipeId() const { return RecipeId; }

protected:
	void BuildWidgetTree();

	UFUNCTION()
	void HandleCraftClicked();

	UPROPERTY()
	TObjectPtr<UTextBlock> NameText;

	UPROPERTY()
	TObjectPtr<UTextBlock> SummaryText;

	UPROPERTY()
	TObjectPtr<UButton> CraftButton;

	UPROPERTY()
	TObjectPtr<UTextBlock> CraftLabel;

	TWeakObjectPtr<UVCCraftingPanelWidget> OwnerPanel;
	FPrimaryAssetId RecipeId;
};

/**
 * Crafting list (feature 8): every recipe the station offers (hand recipes everywhere), with
 * ingredient counts against the character's inventory and a Craft button. Refreshes itself from
 * the inventory's OnInventoryChanged. Opened by itself for hand crafting (C) or embedded in the
 * campsite panel. Code-built; no UMG asset.
 */
UCLASS(BlueprintType, Blueprintable)
class VOXELCHARACTERPLUGIN_API UVCCraftingPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Bind to a character and a station (null = hand crafting). Rebuilds the rows.
	 * @param Character  Whose inventory is read and who crafts.
	 * @param InStation  The rest point / station actor, or null.
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void InitPanel(AVCCharacterBase* Character, AActor* InStation);

	/** Re-read the inventory and update every row. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void RefreshRows();

	/** Craft a recipe through the bound character (rows call this). */
	void RequestCraft(FPrimaryAssetId RecipeId);

	/** @return Number of recipe rows shown. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|UI")
	int32 GetRowCount() const { return Rows.Num(); }

	/** Title shown above the list. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|UI")
	FText PanelTitle = NSLOCTEXT("VCCrafting", "Title", "Crafting");

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	void BuildWidgetTree();
	void Unbind();
	FGameplayTag ResolveStationTag() const;

	UFUNCTION()
	void HandleInventoryChanged();

	UPROPERTY()
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY()
	TObjectPtr<UTextBlock> EmptyText;

	UPROPERTY()
	TObjectPtr<UVerticalBox> RowsBox;

	UPROPERTY()
	TArray<TObjectPtr<UVCCraftingRowWidget>> Rows;

	TWeakObjectPtr<AVCCharacterBase> BoundCharacter;
	TWeakObjectPtr<AActor> Station;
};
