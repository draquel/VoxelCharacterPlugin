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
class UVCMinimapWidget;
class UVCWorldMapWidget;

/** A voxel modification was (partially) blocked — RejectedVoxelCount voxels refused the edit. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVoxelEditBlocked, int32, RejectedVoxelCount);

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

	/** Open / close the hand-crafting panel (C key; feature 8). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void ToggleCraftingUI();

	/** Open the campsite panel for a rest point (the server confirms the interaction first). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void OpenCampsiteUI(AActor* RestPoint);

	/** Close the campsite panel. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void CloseCampsiteUI();

	/** Server -> owning client: show the campsite panel for a rest point the player just used. */
	UFUNCTION(Client, Reliable)
	void Client_OpenCampsite(AActor* RestPoint);

	/** Server -> owning client: a dig yielded items (Count 0 = plain message). Toast "+N Name". */
	UFUNCTION(Client, Reliable)
	void Client_NotifyGathered(const FText& ItemName, int32 Count);

	/** @return True while the campsite panel is open. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|UI")
	bool IsCampsiteUIOpen() const { return bCampsiteOpen; }

	/** @return True while the hand-crafting panel is open. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|UI")
	bool IsCraftingUIOpen() const { return bCraftingOpen; }

	/** What digging yields (feature 8). None = digging gives nothing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Gather")
	TObjectPtr<class UVCGatherTable> GatherTable;

	/** Override class for the hand-crafting panel (None = UVCCraftingPanelWidget). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|UI")
	TSubclassOf<UUserWidget> CraftingPanelWidgetClass;

	/** Override class for the campsite panel (None = UVCCampsiteWidget). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|UI")
	TSubclassOf<UUserWidget> CampsiteWidgetClass;

	/** Toggle the full-screen world map overlay. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void ToggleWorldMapUI();

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

	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|UI")
	TSubclassOf<UUserWidget> MinimapWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|UI")
	TSubclassOf<UUserWidget> WorldMapWidgetClass;

	/** Health/stamina bars (bottom-left). Defaults to UVCVitalsWidget. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|UI")
	TSubclassOf<UUserWidget> VitalsWidgetClass;

	/**
	 * Optional HUD clock (top-centre), any UUserWidget — the world-clock plugin's widget in the demo.
	 * None = no clock. This plugin never reads the time itself.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|UI")
	TSubclassOf<UUserWidget> ClockWidgetClass;

	// --- Objective / toast HUD (feature 4) ---

	/** Show an objective line on the HUD (local; gameplay systems call this on the owning client's controller). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void SetObjectiveText(const FText& Text);

	/** Hide the objective line. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void ClearObjectiveText();

	/** Flash a short HUD message for Duration seconds (local). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void ShowToast(const FText& Text, float Duration = 3.0f);

	/** @return The HUD's current objective text (empty when none). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|UI")
	FText GetObjectiveText() const;

	// --- Debug Commands ---

	/** Give an item to the possessed character's inventory by asset name substring. */
	UFUNCTION(Exec)
	void GiveItem(FString AssetName, int32 Count = 1);

	/** Spawn a WorldItem in front of the player by asset name substring. */
	UFUNCTION(Exec)
	void SpawnWorldItem(FString AssetName, int32 Count = 1);

	// --- Server RPCs ---

	/** Request a server-authoritative voxel modification. BlueprintCallable so BP tools/UI can
	 *  request edits through the same validated path as input-driven digging. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "VoxelCharacter|Voxel")
	void Server_RequestVoxelModification(const FIntVector& VoxelCoord, EVoxelModificationType ModType, uint8 MaterialID);

	// --- Edit feedback ---

	/**
	 * Fired on the OWNING CLIENT when a requested voxel modification was blocked (e.g. an edit
	 * validator vetoed a protected area — see VoxelWorlds IVoxelEditValidator). The controller
	 * stays policy-free: it reports "blocked", not why. Bind UI here for real messaging.
	 */
	UPROPERTY(BlueprintAssignable, Category = "VoxelCharacter|Voxel")
	FOnVoxelEditBlocked OnVoxelEditBlocked;

	/** Show the built-in minimal on-screen "protected" message when an edit is blocked. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Voxel")
	bool bShowEditBlockedMessage = true;

protected:
	/** Server->owning-client notification that the last modification request was blocked. */
	UFUNCTION(Client, Reliable)
	void Client_NotifyVoxelEditBlocked(int32 RejectedVoxelCount);

	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void InitPlayerState() override;
	virtual void OnRep_PlayerState() override;

	/** Bind the player state's progression delegate (server via InitPlayerState, clients via OnRep_PlayerState). */
	void BindProgression();

	UFUNCTION()
	void HandleProgressionChanged(const FVCProgressionStats& Stats);

	/** Last counters seen, so the toast says what changed. */
	FVCProgressionStats LastProgression;

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
	TObjectPtr<UUserWidget> ClockWidget;

	UPROPERTY()
	TObjectPtr<UUserWidget> InventoryPanelWidget;

	UPROPERTY()
	TObjectPtr<UUserWidget> EquipmentPanelWidget;

	UPROPERTY()
	TObjectPtr<UItemCursorWidget> ItemCursorWidget;

	UPROPERTY()
	TObjectPtr<UUserWidget> MinimapWidget;

	UPROPERTY()
	TObjectPtr<UUserWidget> WorldMapWidget;

	UPROPERTY()
	TObjectPtr<UUserWidget> VitalsWidget;

	/** Point the vitals widget at a (re)possessed character. */
	void BindVitalsToPawn(APawn* InPawn);

	bool bInventoryOpen = false;
	bool bWorldMapOpen = false;
	bool bCraftingOpen = false;
	bool bCampsiteOpen = false;

	UPROPERTY()
	TObjectPtr<UUserWidget> CraftingPanelWidget;

	UPROPERTY()
	TObjectPtr<UUserWidget> CampsiteWidget;

	/** Authority: turn a dug voxel material into items for the pawn (feature 8). */
	void GatherFromVoxel(uint8 MaterialId);

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
