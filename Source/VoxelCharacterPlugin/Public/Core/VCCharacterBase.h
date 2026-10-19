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
#include "VoxelWorldModeTypes.h"
#include "VCCharacterBase.generated.h"

class UVCCameraManager;
class UVCMovementComponent;
class UCameraComponent;
class UAbilitySystemComponent;
class UVCInputConfig;
class UVoxelCollisionManager;
class UVoxelWorldConfiguration;
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
	 * Where TryPlaceItem looks for ground: PlaceDistance ahead on the view's heading, from 200 above to 400 below, along
	 * local up (spherical Phase 4, WM-SY16; world Z without custom gravity, the old expressions).
	 * @param Movement The character's movement component (its gravity)
	 * @param ActorLocation The character's location
	 * @param View The view rotation
	 * @param PlaceDistance How far ahead
	 * @param OutStart Trace start
	 * @param OutEnd Trace end
	 */
	static void GetPlacementTrace(const UCharacterMovementComponent* Movement, const FVector& ActorLocation, const FRotator& View,
		float PlaceDistance, FVector& OutStart, FVector& OutEnd);

	/** A placed actor's transform: upright on local up, facing back along the view's heading, OffsetZ above the ground. */
	static FTransform GetPlacementTransform(const UCharacterMovementComponent* Movement, const FRotator& View, const FVector& GroundPoint, float OffsetZ);

	/** The chop aim point: 160 uu ahead on the view's heading (in the local horizontal on a planet). */
	static FVector GetChopAimPointFor(const UCharacterMovementComponent* Movement, const FVector& ActorLocation, const FRotator& View);

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
	 * Server: turn this dead, unpossessed body into the player's corpse (QA R19). The ragdoll stays where it fell;
	 * capsule collision and movement go; once it settles its pose freezes, so it does not drop when its chunk unloads.
	 * The game mode keeps one corpse per player.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "VoxelCharacter|Combat")
	void BecomeCorpse();

	/** This body is a kept corpse (no longer anyone's pawn). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	bool IsCorpse() const { return bIsCorpse; }

	/** A corpse whose ragdoll never settles freezes after this long. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "1"))
	float CorpseFreezeAfterSeconds = 10.f;

	/** A planet's ragdoll falls toward the centre: its bodies' world gravity off, this pull applied every tick (zero = none). */
	FVector RagdollGravityDirection = FVector::ZeroVector;

	/** Start pulling a simulating body along the movement component's custom gravity, if it has one (spherical Phase 4). */
	static bool StartRadialRagdoll(USkeletalMeshComponent* Body, const UCharacterMovementComponent* Movement, FVector& OutGravityDirection);

	/** One tick of a radial ragdoll's pull (gravity's magnitude along the direction, every body). */
	static void TickRadialRagdoll(USkeletalMeshComponent* Body, const FVector& GravityDirection);

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

	/** Use the active hotbar item (F): consume it, place it, or equip it. Client: sends the server request. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Inventory")
	void RequestUseActiveItem();

	/**
	 * Use the item in any inventory slot (an inventory double-click): consume, place or equip it. Client: sends the
	 * server request.
	 * @param SlotIndex Inventory slot
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Inventory")
	void RequestUseItemInSlot(int32 SlotIndex);

	/**
	 * Server: drop Count of the item in an inventory slot as a world pickup in front of the character.
	 * @return True if something was dropped.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "VoxelCharacter|Inventory")
	bool DropItemInSlot(int32 SlotIndex, int32 Count = 1);

	/**
	 * Server: use the item in an inventory slot. Placeable items are placed. Consumables honour their cooldown,
	 * apply AttributeChanges, ConsumeEffect and ConsumeAbility, and remove one from the stack when bConsumeOnUse.
	 * Equippable items go into the slot their Equipment fragment names, swapping out what was there (QA R18,
	 * hotbar option C: F on a weapon equips it).
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
	 * The voxel an edit aimed at a terrain hit changes (world modes review WM-C2, KC5). A place fills the voxel
	 * half a voxel in front of the hit face. In a cubic world a dig removes the block whose face was hit: the
	 * voxel half a voxel behind the impact point along the normal (the impact point itself lies on the face, so
	 * floor() picks the air voxel on +X, +Y and +Z faces, and floating-point noise picks a side on the others).
	 * Smooth worlds dig at the voxel containing the impact point, as before.
	 *
	 * @param ImpactPoint  Where the trace hit the terrain
	 * @param ImpactNormal The hit face's normal
	 * @param Config       The world's configuration (voxel size, origin, meshing mode)
	 * @param bPlace       true = the voxel a placement fills, false = the voxel a dig removes
	 * @return Global voxel coordinate (relative to WorldOrigin); zero without a configuration
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Voxel")
	static FIntVector GetEditVoxelCoord(const FVector& ImpactPoint, const FVector& ImpactNormal, const UVoxelWorldConfiguration* Config, bool bPlace);

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
	// Leaving the world (world modes review WM-I4, KI14; WM-I5)
	// =================================================================

	/**
	 * A player falling this far below the voxel world's lowest terrain has fallen out of the world, and recovers as at
	 * KillZ (FellOutOfWorld) without the minutes-long fall to it. On a planet: this far inside the shell's inner radius,
	 * along the radius (spherical SP-F5). 0 = KillZ only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|Spawn", meta = (ClampMin = "0"))
	float OutOfWorldDepth = 20000.f;

	/**
	 * Falling out of the world (KillZ, or OutOfWorldDepth below the terrain) runs the death and respawn path instead of
	 * the engine default, which destroyed the pawn and left the player without one (KI14). The body goes back to the last
	 * ground it stood on inside the world first, so the corpse and a respawn at the death location are there. A corpse or
	 * an unpossessed body keeps the engine default. Server only.
	 */
	virtual void FellOutOfWorld(const UDamageType& DamageType) override;

	/**
	 * Planets: face the control rotation's yaw in the gravity frame and stay upright (first person turns the body with
	 * the view; spherical SP-F3). Without custom gravity, the engine's world-space version.
	 */
	virtual void FaceRotation(FRotator NewControlRotation, float DeltaTime = 0.f) override;

	/**
	 * Pure: whether a position has fallen out of a world (WM-I4; spherical SP-F5). Heightmap worlds: below the lowest
	 * terrain (Bounds.MinZ) by Depth. A planet (a shell): closer to the centre than the shell's inner radius by Depth, or
	 * farther from it than the orbit radius (Bounds.OrbitRadius: the datum plus the orbit ceiling, spherical Phase 6), the
	 * outer bound of the space a flying pawn may use.
	 * @param Bounds The voxel world's bounds
	 * @param Location The pawn's position
	 * @param Depth OutOfWorldDepth (0 = never)
	 * @return True when out of the world
	 */
	static bool IsOutOfWorld(const FVoxelWorldBounds& Bounds, const FVector& Location, float Depth);

	/**
	 * The last place this pawn stood on the ground inside the world (sampled a few times a second on the server).
	 * @param OutLocation Receives the location (the capsule centre)
	 * @return False until the pawn has stood somewhere
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Spawn")
	bool GetLastSafeLocation(FVector& OutLocation) const;

	/** Fired when the pawn enters or leaves the push-back band before a bounded world's edge (the HUD hint binds here). */
	UPROPERTY(BlueprintAssignable, Category = "VoxelCharacter|Movement")
	FOnVCWorldEdgeChanged OnWorldEdgeChanged;

	/** @return True while the pawn is in the push-back band before the world's edge */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement")
	bool IsNearWorldEdge() const { return bNearWorldEdge; }

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

	/**
	 * The eyes: BaseEyeHeight along local up (spherical Phase 4, WM-SY12). The engine adds it along world Z, which at a
	 * planet's equator puts the eyes beside the head; without custom gravity this is the engine's location.
	 */
	virtual FVector GetPawnViewLocation() const override;
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

	/** Replicated corpse flag: clients apply the corpse state when it arrives. */
	UPROPERTY(ReplicatedUsing = OnRep_IsCorpse)
	bool bIsCorpse = false;

	UFUNCTION()
	void OnRep_IsCorpse();

	/** Corpse state on this machine: no capsule or movement, keep the ragdoll, watch for it to settle. */
	void ApplyCorpseState();

	/** Freeze the ragdoll once it has settled (or after CorpseFreezeAfterSeconds). */
	void CheckCorpseSettled();

	FTimerHandle CorpseSettleTimer;
	int32 CorpseCalmChecks = 0;
	double CorpseStartTime = 0.0;
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

	/** How many chunks the terrain wait asked for (the "mostly ready" fallback's denominator). */
	int32 TerrainWaitTotalChunks = 0;

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

	/**
	 * The terrain wait ran out with nothing under the pawn (WM-I4): move it to the respawn point (the last rest point,
	 * else a player start, else the world's centre) and wait for terrain there. Server only; at most twice per wait.
	 * @return True if the pawn was moved and is waiting again
	 */
	bool RecoverFromMissingTerrain();

	/**
	 * Where a pawn that left the world goes back to: the last safe ground, else the rest point, else a player start,
	 * else the world's centre.
	 * @param bAllowLastSafe Whether the last safe location may be used
	 * @param OutLocation Receives the location (a surface point for the spawn search to settle)
	 * @return False if nothing is known (no voxel world and no player start)
	 */
	bool ResolveRecoveryLocation(bool bAllowLastSafe, FVector& OutLocation) const;

	/** Server: remember safe ground, catch a fall below the world early. Every machine: the world edge hint. */
	void TickWorldSafety(float DeltaSeconds);

	/** Last ground the pawn stood on inside the world (see GetLastSafeLocation). */
	FVector LastSafeLocation = FVector::ZeroVector;
	bool bHasLastSafeLocation = false;
	float SafeLocationTimer = 0.f;

	/** World Z below which the pawn has fallen out of the world (lowest terrain - OutOfWorldDepth); cached. */
	float OutOfWorldZ = -UE_BIG_NUMBER;
	bool bHasOutOfWorldZ = false;

	/** FellOutOfWorld ran for this pawn (it is dying or has been moved back). */
	bool bFellOutOfWorld = false;

	/** Recoveries from a terrain wait with nothing under the pawn, this wait (RecoverFromMissingTerrain). */
	int32 TerrainRecoveryAttempts = 0;

	/** In the push-back band before the world's edge (see OnWorldEdgeChanged). */
	bool bNearWorldEdge = false;

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

	/** Last reported carried-light lit state (prompt text depends on it; fuel ticks alone do not refresh). */
	bool bLastCarriedLightLit = false;

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
	void Input_StartSprint(const FInputActionValue& Value);
	void Input_StopSprint(const FInputActionValue& Value);
	void Input_StartCrouch(const FInputActionValue& Value);
	void Input_StopCrouch(const FInputActionValue& Value);
	void Input_Interact(const FInputActionValue& Value);
	void Input_ToggleView(const FInputActionValue& Value);
	void Input_PrimaryAction(const FInputActionValue& Value);
	void Input_SecondaryAction(const FInputActionValue& Value);
	void Input_OpenInventory(const FInputActionValue& Value);
	void Input_OpenMap(const FInputActionValue& Value);
	void Input_Pause(const FInputActionValue& Value);
	void Input_HotbarSlot(const FInputActionValue& Value);
	void Input_ScrollHotbar(const FInputActionValue& Value);
	void Input_Drop(const FInputActionValue& Value);
	void Input_Use(const FInputActionValue& Value);
	void Input_ToggleEditMode(const FInputActionValue& Value);

	UFUNCTION(Server, Reliable)
	void Server_UseItemInSlot(int32 SlotIndex);

	UFUNCTION(Server, Reliable)
	void Server_DropItemInSlot(int32 SlotIndex, int32 Count);

	/** An equipment visual finished loading: re-apply the view-mode socket choice (first-person arms vs body). */
	UFUNCTION()
	void HandleEquipmentVisualAttached(FGameplayTag SlotTag);

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
