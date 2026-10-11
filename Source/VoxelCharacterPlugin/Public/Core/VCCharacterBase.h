// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Core/VCTypes.h"
#include "Types/CGFItemTypes.h"
#include "Interfaces/CGFInventoryInterface.h"
#include "Interfaces/CGFLightBearerInterface.h"
#include "Integration/VCInventoryBridge.h"
#include "Integration/VCInteractionBridge.h"
#include "Integration/VCEquipmentBridge.h"
#include "Integration/VCAbilityBridge.h"
#include "VCCharacterBase.generated.h"

class UVCCameraManager;
class UVCMovementComponent;
class UCameraComponent;
class UAbilitySystemComponent;
class UVCInputConfig;
class UVoxelCollisionManager;
class UVCCombatComponent;
struct FInputActionValue;

#if WITH_INTERACTION_PLUGIN
class UInteractionComponent;
#endif

#if WITH_EQUIPMENT_PLUGIN
class UEquipmentManagerComponent;
#endif

#if WITH_INVENTORY_PLUGIN
class UInventoryComponent;
#endif

/**
 * Base character class for the voxel character controller.
 *
 * Assembles movement, camera, and integration components.
 * Implements IAbilitySystemInterface as a passthrough to the
 * ASC on AVCPlayerState. Implements bridge interfaces for
 * optional plugin integration (inventory, interaction, equipment, GAS).
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API AVCCharacterBase : public ACharacter,
	public IAbilitySystemInterface,
	public ICGFInventoryInterface,
	public ICGFLightBearerInterface,
	public IVCInventoryBridge,
	public IVCInteractionBridge,
	public IVCEquipmentBridge,
	public IVCAbilityBridge
{
	GENERATED_BODY()

public:
	AVCCharacterBase(const FObjectInitializer& ObjectInitializer);

	// --- IAbilitySystemInterface ---
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// --- ICGFInventoryInterface ---
	virtual UActorComponent* GetInventoryComponent_Implementation() const override;
	virtual TArray<UActorComponent*> GetInventoryComponents_Implementation() const override;

	// --- ICGFLightBearerInterface (feature 7: the equipped torch is the light) ---
	virtual float GetCarriedLightLevel_Implementation() const override;
	virtual bool ConsumeCarriedLightFuel_Implementation(float Seconds) override;

	/**
	 * Fuel of the carried light (the equipped light item's durability).
	 * @param OutFuel     Seconds left (0 when the light needs no fuel).
	 * @param OutMaxFuel  Capacity (0 when the light needs no fuel).
	 * @return True while a lit light source is equipped.
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Light")
	bool GetCarriedLightFuel(float& OutFuel, float& OutMaxFuel) const;

	/** Fired on every machine when the carried light lights, goes dark, or its fuel moves (HUD binds here). */
	UPROPERTY(BlueprintAssignable, Category = "VoxelCharacter|Light")
	FOnVCCarriedLightChanged OnCarriedLightChanged;

	// =================================================================
	// Surface gameplay (feature 8): gathering, crafting, rest points
	// =================================================================

	/** Current MiningSpeed attribute (1 bare-handed; pickaxes add to it). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Gather")
	float GetMiningSpeed() const;

	/** True while an equipped tool lets the primary action dig without edit mode (MiningSpeed above 1). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Gather")
	bool HasDiggingTool() const;

	/** The point a chop aims at: a little ahead of the character at chest height. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Gather")
	FVector GetChopAimPoint() const;

	/**
	 * Chop the harvestable scatter instance (a tree) nearest the aim point, if the local scatter has
	 * one; the server re-checks and yields. No edit mode or tool needed. @return True when a chop was sent.
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Gather")
	bool TryChopScatter();

	/** Authority: restore health and stamina to their maximums (resting). */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "VoxelCharacter|Rest")
	void RestoreVitals();

	/** Rest at a rest point (ICGFRestPointInterface) within interaction range. Routes to the server. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Rest")
	void RequestRest(AActor* RestPoint);

	/** Sleep until morning at a rest point within interaction range. Routes to the server. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Rest")
	void RequestSleep(AActor* RestPoint);

	/**
	 * Craft a recipe, by hand (null station) or at a rest point's station within interaction range.
	 * Routes to the server; the result comes back as a toast.
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Craft")
	void RequestCraft(FPrimaryAssetId RecipeId, AActor* Station);

	/** Is the actor a rest point within interaction range of this character. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Rest")
	bool IsRestPointInRange(const AActor* RestPoint) const;

	// =================================================================
	// Components
	// =================================================================

	/** Camera management component (mode stack, blending). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Camera")
	TObjectPtr<UVCCameraManager> CameraManager;

	/** Scene camera driven by the CameraManager each tick. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Camera")
	TObjectPtr<UCameraComponent> CameraComponent;

	/** First-person arms mesh (visible only in FP mode). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Mesh")
	TObjectPtr<USkeletalMeshComponent> FirstPersonArmsMesh;

	/** Faction, damage intake and death/downed state. Bound to the player state's ASC on possession. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Combat")
	TObjectPtr<UVCCombatComponent> CombatComponent;

	/**
	 * How far the primary-action trace looks for a hostile target before falling through to the
	 * equipped-item / dig behaviour. Should match the melee ability's reach.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float AttackTargetRange = 250.f;

	/** True once this avatar has died (or been downed). Input and movement are off. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	bool IsIncapacitated() const;

	/**
	 * Start the melee ability if a hostile damageable is within AttackTargetRange under the crosshair.
	 * Step 1 of the primary-action chain; also callable from Blueprint / debug commands.
	 * @return True if the ability activation was requested.
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Combat")
	bool TryStartMeleeAttack();

	// --- Optional Integration Components ---
	// Not UPROPERTY — UHT forbids UPROPERTY inside #if blocks.
	// Components are rooted as DefaultSubobjects of the actor.
	// Access through bridge interface methods for Blueprint use.

#if WITH_INTERACTION_PLUGIN
	TObjectPtr<UInteractionComponent> InteractionComponent;
#endif

#if WITH_EQUIPMENT_PLUGIN
	TObjectPtr<UEquipmentManagerComponent> EquipmentManager;
#endif

#if WITH_INVENTORY_PLUGIN
	TObjectPtr<UInventoryComponent> InventoryComponent;
#endif

	// =================================================================
	// View Mode
	// =================================================================

	/** Current view perspective. */
	UPROPERTY(ReplicatedUsing = OnRep_ViewMode, BlueprintReadOnly, Category = "VoxelCharacter|Camera")
	EVCViewMode CurrentViewMode = EVCViewMode::ThirdPerson;

	/** Switch between first and third person. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Camera")
	void SetViewMode(EVCViewMode NewMode);

	/** Fired when view mode changes. */
	UPROPERTY(BlueprintAssignable, Category = "VoxelCharacter|Camera")
	FOnVCViewModeChanged OnViewModeChanged;

	// =================================================================
	// Equipment / Inventory State
	// =================================================================

	/** Animation archetype of the currently equipped main-hand item. Read by AnimInstance. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Equipment")
	EVCEquipmentAnimType ActiveItemAnimType = EVCEquipmentAnimType::Unarmed;

	/** Currently selected hotbar slot index. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Inventory")
	int32 ActiveHotbarSlot = 0;

	/** Number of hotbar slots available. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Inventory")
	int32 NumHotbarSlots = 9;

	/** FP/TP socket mappings for equipment attachment. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Equipment")
	TArray<FVCEquipmentSocketMapping> EquipmentSocketMappings;

	// =================================================================
	// Item actions (client-safe: route through the owning component's server RPCs)
	// =================================================================

	/**
	 * Equip the item in a hotbar slot into the slot its Equipment fragment names.
	 * @param HotbarSlot Slot index; -1 = the active hotbar slot.
	 * @return True if the request was issued (the server decides the outcome).
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Equipment")
	bool EquipHotbarItem(int32 HotbarSlot = -1);

	/** Unequip an equipment slot back into the inventory. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Equipment")
	bool UnequipSlotToInventory(FGameplayTag SlotTag);

	/** Use (consume) the active hotbar item. Client: sends the server request. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Inventory")
	void RequestUseActiveItem();

	/**
	 * Server: use the item in an inventory slot. Requires a Consumable fragment; honours its
	 * cooldown; applies AttributeChanges, ConsumeEffect and ConsumeAbility; removes one from the
	 * stack when bConsumeOnUse.
	 * @return True if the item was used.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "VoxelCharacter|Inventory")
	bool UseItemInSlot(int32 SlotIndex);

	/** Server: copy inventory + equipment onto the player state so the next avatar gets them. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "VoxelCharacter|Items")
	void CaptureItemSnapshotToPlayerState();

	/** Build the inventory + equipment snapshot of this avatar without touching the player state (saves). */
	void BuildItemSnapshot(FVCItemSnapshot& OutSnapshot) const;

	/** Server: restore a pending snapshot from the player state into this avatar, then clear it. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "VoxelCharacter|Items")
	bool RestoreItemSnapshotFromPlayerState();

	/**
	 * Server: a save was imported onto the player state while this avatar is already up (the world
	 * save loads after the first spawn; mid-session loads): drop the items it carries, restore the
	 * pending snapshot, move to the pending spawn transform (waiting for terrain collision there like
	 * a fresh spawn) and apply the pending vitals (feature 9).
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "VoxelCharacter|Items")
	void ApplyPendingSaveState();

	// =================================================================
	// Voxel Interaction
	// =================================================================

	/** Line trace from camera into the world for voxel block targeting. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Voxel")
	bool TraceForVoxel(FHitResult& OutHit, float MaxDistance = 500.f) const;

	/**
	 * Whether the primary / secondary actions may dig and place voxels. Off by default so attacking and
	 * interacting never carve terrain; toggled with IA_ToggleEditMode (B) or vox.EditMode. Local input
	 * state, not replicated: the server still validates every modification request it receives.
	 */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "VoxelCharacter|Voxel")
	bool bEditModeEnabled = false;

	/** Turn voxel edit mode on or off. Broadcasts OnEditModeChanged only when the state actually changes. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Voxel")
	void SetEditModeEnabled(bool bEnabled);

	/** @return True while the primary / secondary actions dig and place voxels. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Voxel")
	bool IsEditModeEnabled() const { return bEditModeEnabled; }

	/** Flip voxel edit mode (what the IA_ToggleEditMode key does). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Voxel")
	void ToggleEditMode();

	/** Fired when edit mode is turned on or off (HUD cue binds here). */
	UPROPERTY(BlueprintAssignable, Category = "VoxelCharacter|Voxel")
	FOnVCEditModeChanged OnEditModeChanged;

	// =================================================================
	// Terrain Ready Spawn
	// =================================================================

	/** Wait for voxel terrain collision before allowing movement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|Spawn")
	bool bWaitForTerrain = true;

	/** Whether the character is currently waiting for terrain. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Spawn")
	bool bIsWaitingForTerrain = false;

	/**
	 * Resume at the current location once collision is there instead of snapping to the terrain
	 * surface from above (a saved position may be inside a dungeon, under the surface). Waits for a
	 * floor within FloorProbeDistance below the feet (tile floors stream in with their POI) up to
	 * TerrainWaitTimeout. Set by ApplyPendingSaveState.
	 */
	bool bResumeAtExactLocation = false;

	/** How far below the feet the exact-location resume looks for a floor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|Spawn", meta = (ClampMin = "0"))
	float FloorProbeDistance = 400.f;

	// =================================================================
	// Debug
	// =================================================================

	/** Toggle on-screen debug overlay showing terrain, camera, and movement state. */
	UFUNCTION(Exec, BlueprintCallable, Category = "VoxelCharacter|Debug")
	void ToggleVoxelDebug();

	/** When true, draws on-screen debug info each frame. Toggle via console: ToggleVoxelDebug */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|Debug")
	bool bShowVoxelDebug = false;

	// =================================================================
	// Bridge Interface Overrides
	// =================================================================

	// --- IVCInventoryBridge ---
	virtual UActorComponent* GetPrimaryInventory() const override;
	virtual int32 GetActiveHotbarSlot() const override;
	virtual void SetActiveHotbarSlot(int32 SlotIndex) override;
	virtual bool RequestPickupItem(AActor* WorldItem) override;
	virtual bool RequestDropActiveItem(int32 Count = 1) override;

	// --- IVCInteractionBridge ---
	virtual FVector GetInteractionTraceOrigin() const override;
	virtual FVector GetInteractionTraceDirection() const override;
	virtual float GetInteractionRange() const override;

	// --- IVCEquipmentBridge ---
	virtual void UpdateEquipmentAttachments() override;
	virtual USkeletalMeshComponent* GetTargetMeshForSlot(const FGameplayTag& SlotTag) const override;

	// --- IVCAbilityBridge ---
	virtual void OnEquipmentAbilitiesChanged(const FGameplayTag& SlotTag) override;

protected:
	// --- Lifecycle ---
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UFUNCTION()
	void OnRep_ViewMode();

	/** Update body / arms mesh visibility based on current view mode. */
	void UpdateMeshVisibility();

	/** When true, mesh hide is deferred until the FP camera blend is nearly complete. */
	bool bPendingFPMeshHide = false;

	// --- Terrain Ready Spawn ---

	/** Elapsed time waiting for terrain (seconds). Used for timeout fallback. */
	float TerrainWaitElapsed = 0.f;

	/** Max seconds to wait for terrain before falling back to line trace placement. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Spawn")
	float TerrainWaitTimeout = 60.f;

	/** How many chunks around the spawn chunk to wait for (1 = 3x3 grid). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Spawn")
	int32 TerrainWaitChunkRadius = 1;

	/** Chunks still waiting for collision during terrain-ready spawn. */
	TSet<FIntVector> PendingTerrainChunks;

	/** Handle for the OnCollisionReady delegate (for cleanup in EndPlay). */
	FDelegateHandle CollisionReadyDelegateHandle;

	/** Cached collision manager pointer for delegate unbinding. */
	TWeakObjectPtr<UVoxelCollisionManager> CachedCollisionManager;

	/** Freeze character movement and collision until terrain is ready. */
	void FreezeForTerrainWait();

	/** Begin event-driven wait for surrounding chunks to have collision. */
	void InitiateChunkBasedWait();

	/** Callback fired when any chunk's collision becomes ready. */
	void OnChunkCollisionReady(const FIntVector& ChunkCoord);

	/** Unfreeze and place character on terrain surface. */
	void PlaceOnTerrainAndResume();

	/** Bind GAS attribute change delegates to character subsystems. */
	void BindAttributeChangeDelegates(UAbilitySystemComponent* ASC);

	/** Grant default abilities from PlayerState config (called once on first possession). */
	void GrantDefaultAbilities(UAbilitySystemComponent* ASC);

	// =================================================================
	// Integration Delegate Handlers
	// =================================================================

	/** Interaction target found — forward to PlayerController for HUD prompt. */
	UFUNCTION()
	void HandleInteractableFound(AActor* InteractableActor);

	/** Interaction target lost — hide HUD prompt. */
	UFUNCTION()
	void HandleInteractableLost(AActor* InteractableActor);

	/** Equipment changed — update animation type and visuals. */
	UFUNCTION()
	void HandleItemEquipped(const FItemInstance& Item, FGameplayTag SlotTag);

	/** Combat component reports death: stop input/movement, present, hand the respawn to the game mode. */
	UFUNCTION()
	void HandleDied(const FCGFDamageContext& Context);

	/** Combat component reports knock-out: stop input/movement, wait for a revive. */
	UFUNCTION()
	void HandleDowned(const FCGFDamageContext& Context);

	/** Shared part of death and knock-out: movement off, capsule passable, input ignored. */
	void Incapacitate();

	/** Blueprint hook for death presentation (ragdoll, camera, UI). */
	UFUNCTION(BlueprintImplementableEvent, Category = "VoxelCharacter|Combat", meta = (DisplayName = "On Died"))
	void BP_OnDied(const FCGFDamageContext& Context);

	/** Blueprint hook for knock-out presentation. */
	UFUNCTION(BlueprintImplementableEvent, Category = "VoxelCharacter|Combat", meta = (DisplayName = "On Downed"))
	void BP_OnDowned(const FCGFDamageContext& Context);


	UFUNCTION()
	void HandleItemUnequipped(const FItemInstance& Item, FGameplayTag SlotTag);

	/** Equipment manager reports a carried-light change: forward to OnCarriedLightChanged. */
	UFUNCTION()
	void HandleCarriedLightChanged(FGameplayTag SlotTag, bool bLit, float FuelSeconds, float MaxFuel);

	/** Equipment manager removed a worn-out item (authority): a burnt-out torch tells the owner. */
	UFUNCTION()
	void HandleItemBroken(FGameplayTag SlotTag, const FItemInstance& Item);

	/** Owning client: show the "torch burnt out" toast. */
	UFUNCTION(Client, Reliable)
	void Client_LightBurntOut();

	// =================================================================
	// Input Callbacks
	// =================================================================

	void Input_Move(const FInputActionValue& Value);
	void Input_Look(const FInputActionValue& Value);
	void Input_Jump(const FInputActionValue& Value);
	void Input_StopJump(const FInputActionValue& Value);
	void Input_Interact(const FInputActionValue& Value);
	void Input_ToggleView(const FInputActionValue& Value);
	void Input_PrimaryAction(const FInputActionValue& Value);
	void Input_SecondaryAction(const FInputActionValue& Value);
	void Input_OpenInventory(const FInputActionValue& Value);
	void Input_OpenMap(const FInputActionValue& Value);
	void Input_HotbarSlot(const FInputActionValue& Value);
	void Input_ScrollHotbar(const FInputActionValue& Value);
	void Input_Drop(const FInputActionValue& Value);
	void Input_Use(const FInputActionValue& Value);
	void Input_ToggleEditMode(const FInputActionValue& Value);

	UFUNCTION(Server, Reliable)
	void Server_UseItemInSlot(int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void Server_RestAt(AActor* RestPoint);

	UFUNCTION(Server, Reliable)
	void Server_ChopScatter(const FVector& AimPoint);

	UFUNCTION(Server, Reliable)
	void Server_SleepAt(AActor* RestPoint);

	UFUNCTION(Server, Reliable)
	void Server_CraftRecipe(FPrimaryAssetId RecipeId, AActor* Station);

	/** Owning client: a craft finished (or failed) — toast. */
	UFUNCTION(Client, Reliable)
	void Client_CraftResult(const FText& RecipeName, bool bSuccess, const FText& Reason);

	/** Server: place a Placeable item from the inventory in front of the character. */
	bool TryPlaceItem(int32 SlotIndex, const FItemInstance& Item, const class UItemFragment_Placeable& Placeable);

	void Input_Craft(const FInputActionValue& Value);

	/** Server: next world time at which a consumable definition may be used again. */
	TMap<FPrimaryAssetId, double> ConsumableReadyTime;

	/** Resolve the InputConfig from the owning PlayerController. */
	const UVCInputConfig* GetInputConfig() const;

	/** Draw on-screen debug overlay with terrain, camera, and movement info. */
	void DrawVoxelDebugInfo();
};
