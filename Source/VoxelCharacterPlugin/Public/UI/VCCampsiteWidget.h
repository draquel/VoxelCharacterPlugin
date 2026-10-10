// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VCCampsiteWidget.generated.h"

class AVCCharacterBase;
class UButton;
class UTextBlock;
class UVCCraftingPanelWidget;

/**
 * Campsite panel (feature 8): Rest, Sleep until dawn and the station's crafting list. Opened by the
 * controller when the server confirms an interaction with a rest point; every button goes back
 * through the character's server RPCs, which re-validate the rest point. Code-built.
 */
UCLASS(BlueprintType, Blueprintable)
class VOXELCHARACTERPLUGIN_API UVCCampsiteWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Bind to the character and the rest point actor (ICGFRestPointInterface). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void InitPanel(AVCCharacterBase* Character, AActor* InRestPoint);

	/** Re-evaluate the Sleep button (night / day). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void RefreshButtons();

	/** The embedded crafting list (null before InitPanel). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|UI")
	UVCCraftingPanelWidget* GetCraftingPanel() const { return CraftingPanel; }

	/** Override class for the embedded crafting list. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|UI")
	TSubclassOf<UVCCraftingPanelWidget> CraftingPanelClass;

protected:
	virtual void NativeOnInitialized() override;

	void BuildWidgetTree();

	UFUNCTION()
	void HandleRestClicked();

	UFUNCTION()
	void HandleSleepClicked();

	UFUNCTION()
	void HandleCloseClicked();

	UPROPERTY()
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY()
	TObjectPtr<UButton> RestButton;

	UPROPERTY()
	TObjectPtr<UButton> SleepButton;

	UPROPERTY()
	TObjectPtr<UTextBlock> SleepLabel;

	UPROPERTY()
	TObjectPtr<UButton> CloseButton;

	UPROPERTY()
	TObjectPtr<UVCCraftingPanelWidget> CraftingPanel;

	TWeakObjectPtr<AVCCharacterBase> BoundCharacter;
	TWeakObjectPtr<AActor> RestPoint;
};
