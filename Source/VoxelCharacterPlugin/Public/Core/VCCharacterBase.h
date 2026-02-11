// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "Core/VCTypes.h"
#include "Types/CGFItemTypes.h"
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
	// Voxel Interaction
	// =================================================================

	/** Line trace from camera into the world for voxel block targeting. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Voxel")
	bool TraceForVoxel(FHitResult& OutHit, float MaxDistance = 500.f) const;

	// =================================================================
	// Bridge Interface Overrides
	// =================================================================

	// --- IVCInventoryBridge ---
	virtual UActorComponent* GetPrimaryInventory() const override;
	virtual int32 GetActiveHotbarSlot() const override;
	virtual void SetActiveHotbarSlot(int32 SlotIndex) override;

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
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

	UFUNCTION()
	void OnRep_ViewMode();

	/** Update body / arms mesh visibility based on current view mode. */
	void UpdateMeshVisibility();

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

	UFUNCTION()
	void HandleItemUnequipped(const FItemInstance& Item, FGameplayTag SlotTag);

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
	void Input_HotbarSlot(const FInputActionValue& Value);
	void Input_ScrollHotbar(const FInputActionValue& Value);

	/** Resolve the InputConfig from the owning PlayerController. */
	const UVCInputConfig* GetInputConfig() const;
};
