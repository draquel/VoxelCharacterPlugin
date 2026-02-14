// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Core/VCTypes.h"
#include "VCPlayerController.generated.h"

class UVCInputConfig;
class UInputMappingContext;
class UUserWidget;
class UInventoryComponent;
class UItemCursorWidget;
class UEquipmentManagerComponent;

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

	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|UI")
	TSubclassOf<UUserWidget> ItemCursorWidgetClass;

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

	UPROPERTY()
	TObjectPtr<UItemCursorWidget> ItemCursorWidget;

	bool bInventoryOpen = false;

	// --- Click-to-move item management ---

	/** Distinguishes whether the held slot is from inventory or equipment. */
	enum class EVCHeldSource : uint8 { None, Inventory, Equipment };

	/** Current held source type. */
	EVCHeldSource HeldSourceType = EVCHeldSource::None;

	/** The slot index currently held/grabbed (-1 = nothing held). Inventory source only. */
	int32 HeldSlotIndex = INDEX_NONE;

	/** The inventory component the held slot belongs to. Inventory source only. */
	UPROPERTY()
	TObjectPtr<UInventoryComponent> HeldInventory;

	/** The equipment slot tag currently held. Equipment source only. */
	FGameplayTag HeldEquipmentSlotTag;

	/** The equipment manager the held slot belongs to. Equipment source only. */
	UPROPERTY()
	TObjectPtr<UEquipmentManagerComponent> HeldEquipmentManager;

	/** Whether we've bound slot click delegates (guard against double-bind). */
	bool bSlotDelegatesBound = false;

	/** Bind click delegates from hotbar + panel + equipment widgets. */
	void BindSlotClickDelegates();

	/** Handle an inventory slot left-click (state machine). */
	UFUNCTION()
	void OnSlotClickedFromUI(int32 ClickedSlotIndex, UInventoryComponent* Inventory);

	/** Handle an inventory slot right-click (cancel). */
	UFUNCTION()
	void OnSlotRightClickedFromUI(int32 ClickedSlotIndex, UInventoryComponent* Inventory);

	/** Handle an equipment slot left-click (state machine). */
	UFUNCTION()
	void OnEquipmentSlotClickedFromUI(FGameplayTag SlotTag, UEquipmentManagerComponent* EquipmentManager);

	/** Handle an equipment slot right-click (cancel). */
	UFUNCTION()
	void OnEquipmentSlotRightClickedFromUI(FGameplayTag SlotTag, UEquipmentManagerComponent* EquipmentManager);

	/** Enter the held state from an inventory slot. */
	void EnterHeldState(int32 InSlotIndex, UInventoryComponent* Inventory);

	/** Enter the held state from an equipment slot. */
	void EnterHeldStateFromEquipment(FGameplayTag InSlotTag, UEquipmentManagerComponent* EquipMgr);

	/** Swap held slot with target, clear held state. */
	void ExecuteSwapAndClearHeld(int32 TargetSlotIndex);

	/** Cancel held state: clear highlight, hide cursor. */
	void CancelHeldState();

	/** Set or clear the held visual on an inventory slot widget. */
	void SetSlotHeldVisual(int32 InSlotIndex, bool bHeld);

	/** Set or clear the held visual on an equipment slot widget. */
	void SetEquipmentSlotHeldVisual(FGameplayTag InSlotTag, bool bHeld);

	/** Show the item cursor with the icon for the given inventory slot. */
	void ShowItemCursor(int32 InSlotIndex, UInventoryComponent* Inventory);

	/** Show the item cursor with the icon for the given equipment slot. */
	void ShowItemCursorForEquipment(FGameplayTag InSlotTag, UEquipmentManagerComponent* EquipMgr);

	/** Hide the item cursor widget. */
	void HideItemCursor();
};
