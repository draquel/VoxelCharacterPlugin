// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/VCTypes.h"
#include "VCPlayerController.generated.h"

class UVCInputConfig;
class UInputMappingContext;
class UUserWidget;

/**
 * Player controller for the voxel character system.
 *
 * Manages Enhanced Input mapping contexts, input mode switching
 * (gameplay vs UI), and server-authoritative voxel modification RPCs.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API AVCPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AVCPlayerController();

	/** Input configuration DataAsset (assign in Blueprint or GameMode defaults). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Input")
	TObjectPtr<UVCInputConfig> InputConfig;

	/** Accessor for the InputConfig (used by character for input binding). */
	const UVCInputConfig* GetInputConfig() const { return InputConfig; }

	// --- Input Mode ---

	/** Switch to game input (hide cursor, capture mouse). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Input")
	void SetGameInputMode();

	/** Switch to UI input (show cursor, release mouse). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Input")
	void SetUIInputMode(UUserWidget* FocusWidget = nullptr);

	// --- UI ---

	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void ToggleInventoryUI();

	/** Show the interaction prompt for the given interactable actor. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void ShowInteractionPrompt(AActor* InteractableActor);

	/** Hide the interaction prompt. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void HideInteractionPrompt();

	/** Update the hotbar selection highlight. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void UpdateHotbarSelection(int32 SlotIndex);

	// --- Widget Class Overrides (set in Blueprint defaults for skinning) ---

	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|UI")
	TSubclassOf<UUserWidget> HotbarWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|UI")
	TSubclassOf<UUserWidget> InteractionPromptWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|UI")
	TSubclassOf<UUserWidget> InventoryPanelWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|UI")
	TSubclassOf<UUserWidget> EquipmentPanelWidgetClass;

	// --- Debug Commands ---

	/** Give an item to the possessed character's inventory by asset name substring. */
	UFUNCTION(Exec)
	void GiveItem(FString AssetName, int32 Count = 1);

	/** Spawn a WorldItem in front of the player by asset name substring. */
	UFUNCTION(Exec)
	void SpawnWorldItem(FString AssetName, int32 Count = 1);

	// --- Server RPCs ---

	/** Request a server-authoritative voxel modification. */
	UFUNCTION(Server, Reliable, Category = "VoxelCharacter|Voxel")
	void Server_RequestVoxelModification(const FIntVector& VoxelCoord, EVoxelModificationType ModType, uint8 MaterialID);

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	/** Add a mapping context with the given priority. */
	void AddInputMappingContext(const UInputMappingContext* Context, int32 Priority);

	/** Remove a mapping context. */
	void RemoveInputMappingContext(const UInputMappingContext* Context);

	/** Current input mode state. */
	bool bIsInUIMode = false;

private:
	/** Create always-visible widgets (hotbar, interaction prompt). Called from BeginPlay on local controller. */
	void CreatePersistentWidgets();

	/** Show inventory + equipment panels (lazy-created). */
	void ShowInventoryPanels();

	/** Hide inventory + equipment panels. */
	void HideInventoryPanels();

	// --- Widget instances ---
	UPROPERTY()
	TObjectPtr<UUserWidget> HotbarWidget;

	UPROPERTY()
	TObjectPtr<UUserWidget> InteractionPromptWidget;

	UPROPERTY()
	TObjectPtr<UUserWidget> InventoryPanelWidget;

	UPROPERTY()
	TObjectPtr<UUserWidget> EquipmentPanelWidget;

	bool bInventoryOpen = false;
};
