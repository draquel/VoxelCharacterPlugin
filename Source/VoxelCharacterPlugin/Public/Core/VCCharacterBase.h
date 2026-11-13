// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Core/VCTypes.h"
#include "Core/VCAnimatedCharacter.h"
#include "Types/CGFItemTypes.h"
#include "Interfaces/CGFInventoryInterface.h"
#include "Interfaces/CGFLightBearerInterface.h"
#include "Effects/VCRagdollBuoyancy.h"
#include "VCCharacterBase.generated.h"

class UVCCameraManager;
class UVCMovementComponent;
class UCameraComponent;
class UAbilitySystemComponent;
class UVCInputConfig;
class UVoxelCollisionManager;
class UVoxelWorldConfiguration;
class UVCCombatComponent;
class UVCBlockInteractionComponent;
class UVCActionMontageComponent;
class UVCAppearanceComponent;
class UVCCharacterAppearance;
class UVCUnderwaterPostProcess;
class UVCWaterEffectsComponent;
class UStaticMesh;
class UStaticMeshComponent;
enum class EVCAnimAction : uint8;
enum class EVCReloadFrom : uint8;
struct FInputActionValue;
struct FVoxelWorldBounds;

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
 * ASC on AVCPlayerState.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API AVCCharacterBase : public ACharacter,
	public IAbilitySystemInterface,
	public ICGFInventoryInterface,
	public ICGFLightBearerInterface,
	public IVCAnimatedCharacter
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

	// --- IVCAnimatedCharacter (character animation P2) ---
	virtual EVCViewMode GetAnimViewMode() const override;
	virtual FGameplayTag GetAnimActionSet() const override { return ActiveActionSet; }
	virtual const UVCCombatComponent* GetAnimCombatComponent() const override;
	virtual bool IsAnimAiming() const override { return IsAiming(); }

	/**
	 * The action set for what a character holds in its main hand: the item's own set when it names one under
	 * Equipment.AnimSet, Tool for any other held item, Unarmed for none.
	 * @param bHoldsItem     Whether the main hand holds an item
	 * @param ItemActionSet  The item's Equipment fragment ActionSetTag (may be empty)
	 * @return An Equipment.AnimSet.* tag
	 */
	static FGameplayTag PickActionSet(bool bHoldsItem, const FGameplayTag& ItemActionSet);

	/**
	 * Gives the mesh the locomotion vc.Anim.Locomotion names: 1 the motion-matching AnimBP of
	 * UVCSettings::PlayerLocomotionAnimClass (character animation P5), 2 the state machine of PlayerStateMachineAnimClass
	 * (the locomotion state machine plan), the worn appearance's AnimClass over either; 0, or a class that does not load,
	 * the Blueprint's own anim class. A dedicated server and the automation tests keep the Blueprint's. Re-links the
	 * equipped items' layers after a change.
	 */
	void ApplyLocomotionAnimClass();

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

	/**
	 * Chop the voxel tree (cubic worlds, world modes review WM-C8, D30) whose trunk block is under the crosshair, if it has
	 * a trunk above its stump; the server re-checks and yields, and fells it after the harvest entry's hits. No tool
	 * needed; in edit mode the primary action digs the block instead. @return True when a chop was sent.
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Gather")
	bool TryChopVoxelTree();

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

	/** Plays the action montages: the owning player's at input, everyone else's by multicast (character animation P3). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UVCActionMontageComponent> ActionMontages;

	/** Dresses the mesh in the player state's appearance (character animation P6). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UVCAppearanceComponent> AppearanceComponent;

	/**
	 * Cubic worlds (world modes review §22): the block highlight, the placement preview and timed block breaking (the
	 * primary action held on a block breaks it after its material's hardness / MiningSpeed, decided on the server).
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Voxel")
	TObjectPtr<UVCBlockInteractionComponent> BlockInteraction;

	/**
	 * How far the primary-action trace looks for a hostile target before falling through to the
	 * equipped-item / dig behaviour. Should match the melee ability's reach.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float AttackTargetRange = 250.f;

	/**
	 * Radius of the sweep that looks for that target along the view (world modes review WM-C9, KC12). A line trace was
	 * used before, and a ray that passes exactly through a capsule's axis misses the capsule (a Chaos query case), which
	 * an aim straight at a body's centre does: vox.Attack's auto-aim, a target a block below. A thin sphere has no such
	 * case and reaches a few uu further round a body's edge; the melee ability's own sweep is wider (MeleeRadius).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0.1"))
	float AttackTargetProbeRadius = 5.f;

	/**
	 * The hostile damageable under the crosshair within AttackTargetRange: the first thing a sphere of AttackTargetProbeRadius
	 * sweeping from the eyes along the view meets on the Pawn channel, if it is a hostile damageable (terrain and walls stop
	 * it). Step 1 of the primary-action chain asks this before it starts the melee ability.
	 *
	 * @return The target, or null
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Combat")
	AActor* FindMeleeTargetUnderCrosshair();

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

	/**
	 * ... and after this long while it is still rising through water (FVCRagdollBuoyancy): a body that died deep takes
	 * longer to reach the surface than one takes to fall over.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "1"))
	float CorpseFreezeAfterSecondsInWater = 40.f;

	/** The ragdoll's buoyancy between ticks on this machine: in water or not, its depth, the column it rises in. */
	FVCRagdollBuoyancyState RagdollBuoyancy;

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

	/**
	 * Press the primary action, as IA_PrimaryAction's press does: the whole chain (draw a bow, attack, chop, dig; G174's
	 * swing at the air with a melee weapon). For scripts and QA on the local player's pawn (Claudius call_actor_function,
	 * where injected Enhanced Input does not reach a PIE player).
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Input")
	void PressPrimaryAction();

	/** Release the primary action, as IA_PrimaryAction's release does: a bow's draw looses, a timed break stops. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Input")
	void ReleasePrimaryAction();

	/**
	 * Press the secondary action, as IA_SecondaryAction's press does: a bow aims, a shield raises its guard (D94), edit mode
	 * places. For scripts and QA, as PressPrimaryAction.
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Input")
	void PressSecondaryAction();

	/** Release the secondary action: the aim ends, the guard comes down. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Input")
	void ReleaseSecondaryAction();

	/** Whether this machine's player holds its guard up (the owner's input state; the server's is State.Blocking). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	bool IsBlocking() const { return bBlocking; }

	/**
	 * Start drawing the main hand's ranged weapon (ranged combat plan B3): the owner's check that there is an arrow
	 * (UVCRangedAttackAbility::CanStartDraw; a toast says none), its own draw montage, and the ability's activation on the
	 * server. Step 0 of the primary-action chain: a ranged weapon in hand takes the press, arrows or not; also callable
	 * from Blueprint / debug commands and scripts (a client's own pawn in a networked PIE).
	 * @return True when the main hand holds a ranged weapon (the chain stops)
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Combat")
	bool TryStartRangedAttack();

	/**
	 * Release a draw started by TryStartRangedAttack: the owner's release montage when the draw was long enough to shoot
	 * (its draw stops otherwise), and the release sent to the server, which decides the shot. Nothing without a draw.
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Combat")
	void ReleaseRangedAttack();

	/** @return True between TryStartRangedAttack and ReleaseRangedAttack on this machine */
	bool IsDrawingRangedWeapon() const { return bDrawingRangedWeapon; }

	// ==================== Aiming (the character refinement plan's C3, D73) ====================

	/**
	 * Whether the character aims a ranged weapon: the owner holds the secondary action with one in the main hand, or a
	 * draw is on. The server decides (bIsAiming, replicated, the ASC's State.Aiming); the owner counts its own input and
	 * draw at once, so the camera and the reticle do not wait a round trip. While aiming the body faces the view and
	 * strafes (SetFacesAim), the motion matching plays its aim set, the third-person camera moves in (UVCAimCameraMode)
	 * and the reticle shows.
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	bool IsAiming() const;

	/** The local view mode (third person on every machine but the owner's: it is not replicated, G87). */
	EVCViewMode GetViewMode() const { return CurrentViewMode; }

	/**
	 * The owner's secondary action with a ranged weapon in hand (pressed or released): aims at once on this machine and
	 * asks the server. Nothing without a ranged weapon. Callable so a Blueprint or a script can aim as the input does.
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Combat")
	void SetWantsToAim(bool bWantsToAim);

	/** The server's ranged attack says a draw began or ended (the ability's activation and end): a draw aims. */
	void SetDrawingOnServer(bool bDrawing);

	/**
	 * The power the owner's draw has now (UItemFragment_RangedWeapon::GetDrawPower from its own start time), 0 without a
	 * draw: the reticle's ring closes with it.
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	float GetOwnDrawPower() const;

	// ==================== Reload (the bow reload plan, D98-D101) ====================

	/**
	 * Whether an arrow is nocked on the ranged weapon in the main hand (D98): the server sets it at a reload's nock and
	 * clears it at the shot, when the weapon leaves the hand and when the archer goes down; replicated. A draw needs it
	 * (UVCRangedAttackAbility::CanStartDraw, the owner's check and the server's).
	 * @return True when an arrow is nocked
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	bool IsArrowNocked() const { return bArrowNocked; }

	/** @return True while the server runs a reload, from its start to its nock or its end */
	bool IsReloading() const { return bReloading; }

	/**
	 * Where a reload takes its arrow from (D99): a worn quiver (Equipment.Slot.Quiver), else the bag at the hip.
	 * @return The reload's variant
	 */
	EVCReloadFrom GetReloadFrom() const;

	/**
	 * Whether a reload may start now (the server's check, each tick while the weapon is empty): a ranged weapon that
	 * shoots ammo in the main hand, its ammo in the bag, nothing nocked, no reload, draw or sheath sequence running, not
	 * down, and the last shot's loose or a hit's flinch played out.
	 * @return True when a reload may start
	 */
	bool CanReload() const;

	/** The server's shot left the string (UVCRangedAttackAbility): the weapon is empty and reloads after the loose. */
	void HandleArrowLoosed();

	/** Whether the aim ray (UVCCombatStatics::ComputeAimRay) meets a hostile within AimRange: the reticle reddens. */
	bool IsAimOnHostile(float Range = 20000.f) const;

	/**
	 * Turns the body toward the view while a bow is drawn (ranged combat plan B4): in third person the movement turns
	 * the body toward the control rotation at its rotation rate instead of toward where it moves, in the gravity frame;
	 * first person faces the view already. The owner calls it at its draw and its release, the ranged attack on the
	 * server at its start and its end, so both turn the body alike.
	 * @param bFaceAim Whether the body follows the view
	 */
	void SetFacesAim(bool bFaceAim);

	/**
	 * Plays an action's montage at once on this machine: the owning player's own dig, chop or place, at input.
	 * Cosmetic; the server's path decides what happens.
	 * @param Action The action
	 */
	void PlayOwnAction(EVCAnimAction Action);

	/**
	 * Authority: plays an action the owning player started for every other machine (it played its own at input).
	 * @param Action The action
	 */
	void ShareOwnAction(EVCAnimAction Action);

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

	/**
	 * Current view perspective, local to the machine that set it (the owning player's toggle). Not replicated (G87): only the
	 * locally controlled pawn hides its body and holds items on the first-person arms; every other machine draws the pawn
	 * in third person (its body and the body sockets).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Camera")
	EVCViewMode CurrentViewMode = EVCViewMode::ThirdPerson;

	/**
	 * Switch between first and third person on this machine: the camera, the rotation rule, mesh visibility, equipment
	 * attachment, the interaction range, then OnViewModeChanged. No request goes to the server (nothing there reads it).
	 * @param NewMode The perspective to switch to
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Camera")
	void SetViewMode(EVCViewMode NewMode);

	/** Fired when view mode changes. */
	UPROPERTY(BlueprintAssignable, Category = "VoxelCharacter|Camera")
	FOnVCViewModeChanged OnViewModeChanged;

	// =================================================================
	// Equipment / Inventory State
	// =================================================================

	/**
	 * The action set of the main-hand item (Equipment.AnimSet.*), read by UVCAnimInstance. Set from the equipment's
	 * change event, which fires on every machine (the replicated slots), so a remote player's set is right too.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Equipment")
	FGameplayTag ActiveActionSet;

	/** Currently selected hotbar slot index. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Inventory")
	int32 ActiveHotbarSlot = 0;

	/** Number of hotbar slots available. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Inventory")
	int32 NumHotbarSlots = 9;

	/** FP/TP socket mappings for equipment attachment. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Equipment")
	TArray<FVCEquipmentSocketMapping> EquipmentSocketMappings;

	/**
	 * QA: a live offset on a slot's visual, in its bone's space after the item's fit and the body's correction (vc.Equip.Nudge,
	 * character refinement plan C1), keyed by the slot tag's name; vc.Equip.PrintFit prints the fit it adds up to. Never saved.
	 */
	TMap<FName, FTransform> EquipFitNudges;

	/** GetDrawnGripWeight at the last UpdateDrawnGrip, so the fit is restored once when the draw ends. */
	float LastDrawnGripWeight = 0.f;

	/**
	 * Where a slot's visual sits on its bone: the item's fit (its Equipment fragment's AttachOffset, or that times the slot's
	 * correction on the slot's own bone), then the body's correction for the bone (UVCCharacterAppearance::AttachCorrections,
	 * D71), then a QA nudge. Each is a transform in the bone's space applied after the one before.
	 * @param Fit The item's fit as EquipmentPlugin computes it
	 * @param BodyCorrection The appearance's correction for the bone
	 * @param Nudge vc.Equip.Nudge's offset (identity outside QA)
	 * @return The visual's relative transform on the bone
	 */
	static FTransform ComposeAttachTransform(const FTransform& Fit, const FTransform& BodyCorrection, const FTransform& Nudge);

	/** The body's correction for the bone a slot's visual sits on (identity without an appearance or an entry). */
	FTransform GetBodyAttachCorrection(FName Bone) const;

	/**
	 * How far the main hand's item sits at its set's DrawnGrip instead of its fit: the weight of the montage in the
	 * UpperBody slot (the draw and the release) while the held set has one, else 0.
	 */
	float GetDrawnGripWeight() const;

	/**
	 * Each tick while a drawn grip applies (and once more when it stops): re-place the main hand's visual between its fit and
	 * the set's DrawnGrip by GetDrawnGripWeight (UVCActionAnimSet::bHasDrawnGrip).
	 */
	void UpdateDrawnGrip();

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

	// --- Sheaths (the equipment sheathing plan, D77-D82; VCCharacterBase_Sheath.cpp) ---

	/**
	 * Draw a carried weapon from its sheath: the weapon drawn now is sheathed first (its montage), then this one's
	 * unsheathe plays and the server puts it in the hand at the montage's hand-off (D79, D82). The owner plays the
	 * montages at once; the server checks again.
	 * @param SheathSlotTag The sheath (Equipment.Slot.Sheath.*)
	 * @return True when the request was issued
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Equipment")
	bool RequestDrawWeapon(FGameplayTag SheathSlotTag);

	/**
	 * Put the drawn weapon back in its sheath (its montage; the weapon leaves the hand's use at once).
	 * @return True when the request was issued (a weapon was drawn)
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Equipment")
	bool RequestSheatheWeapon();

	/** R: sheathe the drawn weapon, or with none drawn draw the last one drawn (else the first carried). @return Issued */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Equipment")
	bool RequestToggleSheathe();

	/** X: sheathe the drawn weapon and draw the next carried one (the sheaths' order: belt, hip, back). @return Issued */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Equipment")
	bool RequestCycleWeapon();

	/** The main hand's sheaths holding a weapon, in the cycle's order (every machine, from the replicated slots). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Equipment")
	TArray<FGameplayTag> GetCarriedWeaponSlots() const;

	/** The sheath whose weapon is in the main hand; invalid when none is drawn. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Equipment")
	FGameplayTag GetDrawnSheath() const;

	/**
	 * The draw's and the sheathe's predicate on the owner and the server (D-CR16): alive and not downed, and for a draw an
	 * occupied sheath not drawn yet, for a sheathe (an invalid tag) a weapon drawn.
	 * @param SheathSlotTag The sheath to draw; invalid to ask about a sheathe
	 * @return True when the request may go ahead
	 */
	bool CanSheathAction(FGameplayTag SheathSlotTag) const;

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
	 * What the edit actions aim at (world modes review WM-C12): the terrain under the crosshair (TraceForVoxel) and the
	 * voxels a dig and a place would edit there (GetEditVoxelCoord). The primary and secondary actions, the block
	 * feedback (UVCBlockInteractionComponent) and vox.Dig all take their voxels from here, so what is outlined is what is
	 * dug. The server still checks reach (AVCPlayerController::IsVoxelInEditReach) and the voxel itself.
	 *
	 * @param OutHit        The trace's hit
	 * @param OutDigVoxel   Global voxel coordinate a dig would empty
	 * @param OutPlaceVoxel Global voxel coordinate a place would fill
	 * @return False when the trace hits nothing or there is no voxel world
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Voxel")
	bool FindEditTarget(FHitResult& OutHit, FIntVector& OutDigVoxel, FIntVector& OutPlaceVoxel) const;

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

	// --- Weather (the weather plan's P5) ---

	/** The wet level (UVCMovementComponent::GetWeatherWetLevel) at which State.Weather.Wet goes on. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Weather", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WetOnLevel = 0.6f;

	/** The wet level under which State.Weather.Wet goes off again (under WetOnLevel: no flicker at the edge). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Weather", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WetOffLevel = 0.2f;

	/** The air temperature (C) at and under which State.Weather.Cold goes on, with UVCWeatherColdEffect's speed penalty. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Weather")
	float ColdBelowC = 0.f;

	/** Degrees over ColdBelowC the air must warm to before the cold goes off again. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Weather", meta = (ClampMin = "0.0"))
	float ColdHysteresisC = 2.f;

	/** Soaked (State.Weather.Wet on the ability system; every machine once replicated). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Weather")
	bool IsWet() const;

	/** In the cold (State.Weather.Cold on the ability system; every machine once replicated). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Weather")
	bool IsCold() const;

	/**
	 * Pure: whether a state with hysteresis is on after a reading.
	 * @param bWasOn  The state before
	 * @param Value   The reading
	 * @param OnAt    The reading at which it goes on (and over, when bHighIsOn)
	 * @param OffAt   The reading at which it goes off again (short of OnAt)
	 * @param bHighIsOn High readings turn it on (the wet level); false: low readings do (the temperature)
	 * @return The state after
	 */
	static bool HysteresisOn(bool bWasOn, float Value, float OnAt, float OffAt, bool bHighIsOn);

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

	virtual UActorComponent* GetPrimaryInventory() const;
	virtual int32 GetActiveHotbarSlot() const;
	virtual void SetActiveHotbarSlot(int32 SlotIndex);
	virtual bool RequestPickupItem(AActor* WorldItem);
	virtual bool RequestDropActiveItem(int32 Count = 1);

	/**
	 * The eyes: BaseEyeHeight along local up (spherical Phase 4, WM-SY12). The engine adds it along world Z, which at a
	 * planet's equator puts the eyes beside the head; without custom gravity this is the engine's location.
	 */
	virtual FVector GetPawnViewLocation() const override;
	virtual float GetInteractionRange() const;

	virtual void UpdateEquipmentAttachments();

	/**
	 * Hide the body's equipment visuals from their own player in first person with the body (no arms mesh draws them, D57:
	 * a bow floating at a hidden hand); a slot drawn on the arms mesh keeps its visual. The lights stay.
	 */
	void UpdateEquipmentVisualVisibility();
	virtual USkeletalMeshComponent* GetTargetMeshForSlot(const FGameplayTag& SlotTag) const;

	virtual void OnEquipmentAbilitiesChanged(const FGameplayTag& SlotTag);

protected:
	/** Server, every half second: the weather's state tags and the cold's effect from the movement component's weather read. */
	void UpdateWeatherEffects();
	float WeatherEffectsTimer = 0.f;

	// --- Lifecycle ---
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void NotifyControllerChanged() override;

	/**
	 * The underwater view tint, on the pawn a local player controller views through and nowhere else (G133): its
	 * volume is unbound and would tint the local view for a swimming remote player or NPC. Null on every other pawn.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UVCUnderwaterPostProcess> UnderwaterPostProcess;

	/** Create or destroy UnderwaterPostProcess so it exists exactly while a local player controller views through this pawn. */
	void UpdateUnderwaterPostProcess();

	/**
	 * The effects at the water's surface (a splash, a wake, their sounds; the water plan's W1), made from this pawn's own
	 * movement mode on every machine but a dedicated server (UVCWaterEffectsComponent::PlaysIn). Null on a dedicated server.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UVCWaterEffectsComponent> WaterEffects;

	/** Replicated corpse flag: clients apply the corpse state when it arrives. */
	UPROPERTY(ReplicatedUsing = OnRep_IsCorpse)
	bool bIsCorpse = false;

	UFUNCTION()
	void OnRep_IsCorpse();

	/** Seconds after a shot before the reload starts, the release's loose played first (D101). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float ReloadAfterShotSeconds = 0.35f;

	/** Seconds after a hit broke a reload off before the next one starts, the flinch played first (D101). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float ReloadAfterHitSeconds = 0.6f;

	/**
	 * The arrows standing in a worn quiver while the bag holds the ranged weapon's ammo (D100), in the quiver's mesh space
	 * (its opening up +Z): a cosmetic cluster with the ammo's mesh, not one per arrow. Empty: none shown.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Combat")
	TArray<FTransform> QuiverArrowLayout;

	/** The server's nock (D98); every machine shows the arrow from it and the owner's waiting draw starts. */
	UPROPERTY(ReplicatedUsing = OnRep_ArrowNocked)
	bool bArrowNocked = false;

	UFUNCTION()
	void OnRep_ArrowNocked();

	/**
	 * The arrows' look (D100): the ammo mesh the server finds in the bag (the held weapon's ammo, else any), null with
	 * none; replicated, since the bag is the owner's alone. The arrow in the hand and those in a worn quiver show it.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ArrowMesh)
	TObjectPtr<UStaticMesh> ArrowMesh;

	UFUNCTION()
	void OnRep_ArrowMesh();

	/** The server runs a reload (StartReload until FinishReload or CancelReload). */
	bool bReloading = false;

	/** World time before which no reload starts (the shot's loose, a hit's flinch). */
	double NextReloadTime = 0.0;

	/** The server's nock at the reload montage's nock time (its UVCAnimNotify_ActionHit). */
	FTimerHandle ReloadNockTimer;

	/** The owner pressed to draw while the weapon was being loaded: the draw starts at the nock if still held. */
	bool bDrawWaitsForNock = false;

	/** A ranged weapon is in the main hand (refreshed at every equipment change, every machine): the reload's gate. */
	bool bRangedWeaponInHand = false;

	/** The arrow in the drawing hand (D100), created on first use; cosmetic, every machine. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> HeldArrowComponent;

	/** The arrows standing in the quiver (QuiverArrowLayout), cosmetic, every machine. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> QuiverArrowComponents;

	/** Server, each tick: start a reload when CanReload says so. */
	void UpdateBowReload();

	/** Server: play the reload to everyone and nock at its nock time (D101). */
	void StartReload();

	/** Server: the reload's nock (the weapon still in the hand, the archer up). */
	void FinishReload();

	/**
	 * Server: break a running reload off (its montage stopped everywhere); the next starts no sooner than RetryDelay.
	 * @param RetryDelay Seconds before a reload may start again
	 */
	void CancelReload(float RetryDelay);

	/** Server: set the nock and act on it as a client's OnRep does. */
	void SetArrowNocked(bool bNocked);

	/** Every machine, at a change of the nock: the owner's waiting draw starts. */
	void HandleNockChanged();

	/** Every machine, each tick: show the arrow in the hand from a reload's grab to the loose. */
	void UpdateHeldArrow();

	/** Every machine: the arrows standing in a worn quiver (ArrowMesh on QuiverArrowLayout). */
	void UpdateQuiverArrows();

	/** Server: ArrowMesh from the bag. */
	void RefreshArrowMesh();

	/** A hit landed (server): a reload is broken off (D101). */
	UFUNCTION()
	void HandleDamagedForReload(const FCGFDamageContext& Context);

	/** The bag changed (server): the quiver's arrows and a reload waiting for ammo. */
	UFUNCTION()
	void HandleInventoryChangedForReload();

	/** The server's aim state (C3): the owner's request or a draw on the server; clients apply it as it arrives. */
	UPROPERTY(ReplicatedUsing = OnRep_IsAiming)
	bool bIsAiming = false;

	UFUNCTION()
	void OnRep_IsAiming();

	/** The owner's request to aim, as the server knows it (Server_SetWantsToAim). */
	bool bServerWantsToAim = false;

	/** A draw on the server (SetDrawingOnServer). */
	bool bServerDrawing = false;

	/** The owner's own request (SetWantsToAim), counted before the server answers. */
	bool bLocalWantsToAim = false;

	/** What ApplyAimState last applied, so the camera is pushed and popped once per change. */
	bool bAimStateApplied = false;

	UFUNCTION(Server, Reliable)
	void Server_SetWantsToAim(bool bWantsToAim);

	/** The server's rule: bIsAiming from the request and the draw; applies it here and replicates it. */
	void UpdateServerAiming();

	/**
	 * Apply what IsAiming says on this machine: the body facing the view (SetFacesAim, third person), the ASC's
	 * State.Aiming (authority), and for the local player the aim camera pushed or popped. Called on every change of input,
	 * draw, replication or view mode.
	 */
	void ApplyAimState();

	/** Corpse state on this machine: no capsule or movement, keep the ragdoll, watch for it to settle. */
	void ApplyCorpseState();

	/** Freeze the ragdoll once it has settled (or after CorpseFreezeAfterSeconds). */
	void CheckCorpseSettled();

	FTimerHandle CorpseSettleTimer;
	int32 CorpseCalmChecks = 0;
	double CorpseStartTime = 0.0;
	virtual void OnRep_PlayerState() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

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

	/** FellOutOfWorld ran for this pawn (it is dying or has been moved back). */
	bool bFellOutOfWorld = false;

	/** Recoveries from a terrain wait with nothing under the pawn, this wait (RecoverFromMissingTerrain). */
	int32 TerrainRecoveryAttempts = 0;

	/** This wait's spawn is at the sea's surface (a planet with no land, spherical Phase 6, G36): the pawn drops in and swims. */
	bool bSpawnAtSea = false;

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

	/** An item was equipped: re-attach the visuals for the current view mode. */
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

	/** Equipment changed on this machine (an equip, or the replicated slots arriving): refresh the action set. */
	UFUNCTION()
	void HandleEquipmentChanged();

	/** Sets ActiveActionSet from the main-hand slot. */
	void RefreshActionSet();

	/**
	 * Gives the movement the gait caps of the clips the state machine plays for the held set (its clip set's, else the
	 * base set's; the locomotion plan's L2, D91). Held items are replicated state, so every machine sets the same caps.
	 */
	void UpdateClipGaitCaps();

	/** Links the equipped items' anim layers again (after the mesh's anim class changed). */
	void RelinkEquipmentAnimLayers();

	/**
	 * Wears the player state's appearance (character animation P6), then the anim class it plays, the items' layers again
	 * and the first-person visibility. Nothing to do when it already wears it.
	 */
	void ApplyPlayerAppearance();

	/** The player state's appearance changed (resolved on the server, replicated to clients). */
	UFUNCTION()
	void HandleAppearanceChanged();

	/** Whether ApplyPlayerAppearance has dressed the mesh once (the first time applies even the class's own body). */
	bool bAppearanceApplied = false;

	/** The mesh's anim class from the Blueprint, before ApplyLocomotionAnimClass first ran (vc.Anim.Locomotion 0 plays it). */
	UPROPERTY(Transient)
	TSubclassOf<UAnimInstance> BlueprintAnimClass;

	/** Equipment manager reports a carried-light change: forward to OnCarriedLightChanged. */
	UFUNCTION()
	void HandleCarriedLightChanged(FGameplayTag SlotTag, bool bLit, float FuelSeconds, float MaxFuel);

	/** Last reported carried-light lit state (prompt text depends on it; fuel ticks alone do not refresh). */
	bool bLastCarriedLightLit = false;

	/**
	 * Swing: by the combo's rule (the sword attack flow plan, D104) the owner's montage at once, or the press kept until
	 * the current attack's recovery, or nothing in its wind-up; then the melee ability's activation with the owner's combo
	 * index, which sweeps on the server at the hit time. TryStartMeleeAttack's swing at a target and the primary action's
	 * swing at the air with a melee weapon in hand (G174).
	 * @return True if the press was taken (a swing requested, or kept)
	 */
	bool StartMeleeSwing();

	/**
	 * Authority: the melee ability's activation for a swing, with the owner's combo index for the ability to take (G179).
	 * @param ComboIndex The owner's combo index, -1 for none
	 * @return True if the ability activated
	 */
	bool ActivateMeleeSwing(int32 ComboIndex);

	/** The owner's swing: the server activates the melee ability with the owner's combo index (D104). */
	UFUNCTION(Server, Reliable)
	void Server_RequestMeleeSwing(int8 ComboIndex);

	/**
	 * Raise the guard (D94): with a shield in the off hand (UVCBlockAbility::CanStartBlock), the owner's guard at once and
	 * the block ability's activation, which the server holds while the secondary action is held.
	 * @return True when the guard went up (or was up)
	 */
	bool StartBlock();

	/** Lower the guard: the owner's lowering (when its guard still plays) and the ability's release on the server. */
	void StopBlock();

	/** The owner holds its guard up (StartBlock until StopBlock). */
	bool bBlocking = false;

	/** A draw this machine started (TryStartRangedAttack) and its world time, for the owner's release montage. */
	bool bDrawingRangedWeapon = false;
	double OwnDrawStartTime = -1.0;

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
	/** The primary action is released (or cancelled): a timed block break stops (WM-C15). */
	void Input_StopPrimaryAction(const FInputActionValue& Value);
	void Input_SecondaryAction(const FInputActionValue& Value);
	/** The secondary action is released (or cancelled): the aim ends (C3). */
	void Input_StopSecondaryAction(const FInputActionValue& Value);
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

	void Input_Sheathe(const FInputActionValue& Value);
	void Input_CycleWeapon(const FInputActionValue& Value);

	/** The owner's draw (a sheath) or sheathe (an invalid tag): the server checks and runs the same sequence. */
	UFUNCTION(Server, Reliable)
	void Server_SheathSequence(FGameplayTag DrawSheath);

	/**
	 * The sheathe of the drawn weapon (when another or none is wanted), then the unsheathe of DrawSheath after it. On the
	 * owning client: its montages only. On the server: the montages to everyone (the owner skipping what it played when
	 * bOwnerPlayedIt), the sheathe at once and the draw at the unsheathe's hand-off (D82).
	 * @param DrawSheath     The sheath to draw; invalid to sheathe only
	 * @param bOwnerPlayedIt The owning client played the montages itself
	 */
	void RunSheathSequence(FGameplayTag DrawSheath, bool bOwnerPlayedIt);

	/** One montage of a sequence (and on the server, its state change). */
	void PlaySheathStep(EVCAnimAction Action, FGameplayTag Sheath, bool bOwnerPlayedIt);

	/** The action set of the weapon in a sheath (its Equipment fragment's ActionSetTag through PickActionSet). */
	FGameplayTag GetSheathActionSet(FGameplayTag Sheath) const;

	/** The unsheathe after a sequence's sheathe, and the server's draw at an unsheathe's hand-off. */
	FTimerHandle SheathStepTimer;
	FTimerHandle SheathHandOffTimer;

	/** The sheath drawn last on this machine (R draws it again). */
	FGameplayTag LastDrawnSheath;

	/** Whether a sheath's hand-off was pending at the last tick, so the visual is re-seated once more when it passes. */
	bool bSheathHandOffWasPending = false;

	/** Each tick while an unsheathe or a sheathe plays: re-seat the visuals so the weapon changes bone at the hand-off. */
	void UpdateSheathHandOff();

	/** A sheath's weapon was drawn or put back (every machine): re-seat the visuals, remember the drawn sheath. */
	UFUNCTION()
	void HandleEquipmentDrawnChanged(FGameplayTag SlotTag, bool bDrawn);

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
	void Server_ChopVoxelTree(const FIntVector& VoxelCoord);

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

#if !UE_BUILD_SHIPPING
	/** Draw on-screen debug overlay with terrain, camera, and movement info. */
	void DrawVoxelDebugInfo();
#endif
};
