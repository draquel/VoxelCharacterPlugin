# Voxel World Character Controller — Architecture Design

## Overview

This document defines a **hybrid first/third-person character controller** for Unreal Engine 5.7, designed as a standalone plugin (`VoxelCharacterPlugin`) that integrates with the existing plugin suite:

- **CommonGameFramework** — shared types, interfaces
- **ItemInventoryPlugin** — item/inventory management
- **InteractionPlugin** — world interaction system
- **EquipmentPlugin** — equipment slots and visual attachments
- **VoxelWorlds** — voxel terrain engine
- **Gameplay Ability System (GAS)** — abilities, attributes, effects (future integration)

The controller supports seamless first/third person switching, voxel-aware movement, and clean integration boundaries with each dependent system.

---

## Plugin Structure

```
VoxelCharacterPlugin/
├── Source/
│   ├── VoxelCharacterPlugin/
│   │   ├── Public/
│   │   │   ├── Core/
│   │   │   │   ├── VCCharacterBase.h
│   │   │   │   ├── VCPlayerController.h
│   │   │   │   ├── VCPlayerState.h
│   │   │   │   ├── VCAnimInstance.h
│   │   │   │   └── VCCharacterAttributeSet.h
│   │   │   ├── Camera/
│   │   │   │   ├── VCCameraManager.h
│   │   │   │   ├── VCFirstPersonCameraMode.h
│   │   │   │   └── VCThirdPersonCameraMode.h
│   │   │   ├── Movement/
│   │   │   │   ├── VCMovementComponent.h
│   │   │   │   ├── VCVoxelNavigationHelper.h
│   │   │   │   └── VCMovementModes.h
│   │   │   ├── Integration/
│   │   │   │   ├── VCInventoryInterface.h
│   │   │   │   ├── VCInteractionInterface.h
│   │   │   │   ├── VCEquipmentInterface.h
│   │   │   │   └── VCAbilityInterface.h
│   │   │   └── Input/
│   │   │       ├── VCInputConfig.h
│   │   │       └── VCInputActions.h
│   │   └── Private/
│   │       └── (mirrors Public/)
│   └── VoxelCharacterPluginEditor/
│       └── (editor utilities, debug visualizers)
├── Content/
│   ├── Input/
│   │   ├── IA_Move.uasset
│   │   ├── IA_Look.uasset
│   │   ├── IA_Jump.uasset
│   │   ├── IA_Interact.uasset
│   │   ├── IA_ToggleView.uasset
│   │   └── IMC_Default.uasset
│   └── Animation/
│       └── (ABP templates)
├── VoxelCharacterPlugin.uplugin
└── CLAUDE.md
```

---

## Core Classes

### 1. `UVCMovementComponent` (extends `UCharacterMovementComponent`)

The movement component is the foundational piece — it must understand voxel terrain and provide movement modes appropriate for a voxel world.

```cpp
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCMovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()

public:
    // --- Voxel-Aware Movement ---

    // Query voxel world for terrain data beneath/around the character
    // Used to adjust movement parameters based on voxel material
    void UpdateVoxelTerrainContext();

    // Override to use voxel collision when the voxel world doesn't provide
    // standard Unreal collision primitives
    virtual void PhysCustom(float DeltaTime, int32 Iterations) override;

    // Custom floor detection that consults voxel world geometry
    virtual void FindFloor(const FVector& CapsuleLocation,
                          FFindFloorResult& OutFloorResult,
                          bool bCanUseCachedLocation,
                          const FHitResult* DownwardSweepResult) const override;

    // --- Custom Movement Modes ---
    UPROPERTY(EditDefaultsOnly, Category = "Movement|Voxel")
    float VoxelClimbingSpeed = 200.f;

    UPROPERTY(EditDefaultsOnly, Category = "Movement|Voxel")
    float VoxelSwimmingSpeedMultiplier = 0.6f;

    UPROPERTY(EditDefaultsOnly, Category = "Movement|Voxel")
    float VoxelSurfaceGripMultiplier = 1.0f; // Modified by voxel material type

    // Terrain material affects movement (ice, mud, sand, etc.)
    UPROPERTY(BlueprintReadOnly, Category = "Movement|Voxel")
    EVoxelSurfaceType CurrentSurfaceType = EVoxelSurfaceType::Default;

protected:
    // Cached voxel query results to avoid per-frame queries
    FVoxelTerrainContext CachedTerrainContext;
    float TerrainContextCacheTime = 0.f;

    UPROPERTY(EditDefaultsOnly, Category = "Movement|Voxel")
    float TerrainCacheDuration = 0.1f; // Re-query every 100ms

    // Delegate bound to voxel world chunk updates for invalidation
    void OnVoxelChunkModified(const FIntVector& ChunkCoord);
};
```

**Key Design Decisions:**
- **Terrain context caching:** Voxel queries can be expensive; cache results and invalidate on chunk modification events from VoxelWorlds.
- **Surface type affects movement:** The voxel material at the character's feet drives friction, speed multipliers, and footstep effects.
- **Custom floor finding:** If VoxelWorlds uses non-standard collision (e.g., marching cubes meshes updated async), the movement component needs to handle transitional states where collision geometry is being rebuilt.

### 2. `AVCCharacterBase` (extends `ACharacter`)

```cpp
UCLASS()
class VOXELCHARACTERPLUGIN_API AVCCharacterBase : public ACharacter
{
    GENERATED_BODY()

public:
    AVCCharacterBase(const FObjectInitializer& ObjectInitializer);

    // --- Components ---

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UVCCameraManager> CameraManager;

    // First-person arm mesh (visible only in FP mode)
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mesh")
    TObjectPtr<USkeletalMeshComponent> FirstPersonArmsMesh;

    // --- View Mode ---
    UPROPERTY(ReplicatedUsing = OnRep_ViewMode, BlueprintReadOnly, Category = "Camera")
    EVCViewMode CurrentViewMode = EVCViewMode::ThirdPerson;

    UFUNCTION(BlueprintCallable, Category = "Camera")
    void SetViewMode(EVCViewMode NewMode);

    UFUNCTION()
    void OnRep_ViewMode();

    // --- Integration Points (see Integration section) ---
    // These are NOT hard dependencies — they resolve at runtime via interfaces

    // Inventory: resolved from PlayerState or this actor's components
    UFUNCTION(BlueprintCallable, Category = "Integration")
    TScriptInterface<IInventoryOwnerInterface> GetInventoryOwner() const;

    // Interaction: component on this actor
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Integration")
    TObjectPtr<UInteractionScannerComponent> InteractionScanner;

    // Equipment: component managing equipment slots and visual attachments
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Integration")
    TObjectPtr<UEquipmentManagerComponent> EquipmentManager;

    // GAS: Ability System lives on PlayerState (survives respawn)
    // Access via GetAbilitySystemComponent() → PlayerState passthrough
    // See GAS Integration section for details

protected:
    virtual void BeginPlay() override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void PossessedBy(AController* NewController) override;

    // --- Mesh Visibility Management ---
    void UpdateMeshVisibility();
    // In FP: hide body mesh (or set to shadow-only), show arms mesh
    // In TP: show body mesh, hide arms mesh

    // --- Voxel Interaction ---
    // Block placement/destruction traces from camera
    UFUNCTION(BlueprintCallable, Category = "Voxel")
    bool TraceForVoxel(FHitResult& OutHit, float MaxDistance = 500.f) const;

    // Input callbacks
    void Input_Move(const FInputActionValue& Value);
    void Input_Look(const FInputActionValue& Value);
    void Input_Jump(const FInputActionValue& Value);
    void Input_Interact(const FInputActionValue& Value);
    void Input_ToggleView(const FInputActionValue& Value);
    void Input_PrimaryAction(const FInputActionValue& Value);
    void Input_SecondaryAction(const FInputActionValue& Value);
};
```

**View Mode Enum:**

```cpp
UENUM(BlueprintType)
enum class EVCViewMode : uint8
{
    FirstPerson,
    ThirdPerson,
    // Future: shoulder cam, top-down, etc.
};
```

### 3. `AVCPlayerController` (extends `APlayerController`)

```cpp
UCLASS()
class VOXELCHARACTERPLUGIN_API AVCPlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    // --- Enhanced Input ---
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UVCInputConfig> InputConfig;

    // --- UI Integration ---
    // Inventory UI toggling, HUD management
    UFUNCTION(BlueprintCallable, Category = "UI")
    void ToggleInventoryUI();

    UFUNCTION(BlueprintCallable, Category = "UI")
    void ShowInteractionPrompt(const FInteractionOption& Option);

    UFUNCTION(BlueprintCallable, Category = "UI")
    void HideInteractionPrompt();

    // --- Input Mode Management ---
    // Handles cursor visibility, input mode switching for UI vs gameplay
    UFUNCTION(BlueprintCallable, Category = "Input")
    void SetGameInputMode();

    UFUNCTION(BlueprintCallable, Category = "Input")
    void SetUIInputMode(UUserWidget* FocusWidget = nullptr);

    // --- Server RPCs for Authoritative Actions ---
    UFUNCTION(Server, Reliable)
    void Server_RequestVoxelModification(const FIntVector& VoxelCoord,
                                         EVoxelModificationType ModType,
                                         uint8 NewVoxelValue);

protected:
    virtual void BeginPlay() override;
    virtual void SetupInputComponent() override;
    virtual void OnPossess(APawn* InPawn) override;

    // Input mapping context management
    void AddInputMappingContext(const UInputMappingContext* Context, int32 Priority);
    void RemoveInputMappingContext(const UInputMappingContext* Context);

    // Track current input state
    bool bIsInUIMode = false;
};
```

### 4. `UVCCameraManager` (extends `UActorComponent`)

```cpp
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCCameraManager : public UActorComponent
{
    GENERATED_BODY()

public:
    // --- Camera Mode Stack ---
    // Supports blending between camera modes for smooth transitions

    UFUNCTION(BlueprintCallable, Category = "Camera")
    void PushCameraMode(TSubclassOf<UVCCameraModeBase> CameraModeClass);

    UFUNCTION(BlueprintCallable, Category = "Camera")
    void PopCameraMode();

    // Called every frame to compute final camera transform
    void UpdateCamera(float DeltaTime);

    // --- Configuration ---
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    TSubclassOf<UVCCameraModeBase> FirstPersonModeClass;

    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    TSubclassOf<UVCCameraModeBase> ThirdPersonModeClass;

    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float ModeTransitionBlendTime = 0.3f;

    // --- Voxel-Aware Camera ---
    // Prevents camera from clipping into voxel terrain in TP mode
    UPROPERTY(EditDefaultsOnly, Category = "Camera|Collision")
    bool bUseVoxelCameraCollision = true;

    UPROPERTY(EditDefaultsOnly, Category = "Camera|Collision")
    float CameraCollisionProbeSize = 12.f;

protected:
    UPROPERTY()
    TArray<TObjectPtr<UVCCameraModeBase>> CameraModeStack;

    // Voxel collision trace for third-person camera
    FVector ResolveVoxelCameraCollision(const FVector& IdealLocation,
                                        const FVector& PivotLocation) const;
};
```

**Camera Mode Base:**

```cpp
UCLASS(Abstract)
class VOXELCHARACTERPLUGIN_API UVCCameraModeBase : public UObject
{
    GENERATED_BODY()

public:
    // Compute desired camera transform this frame
    virtual FTransform ComputeDesiredTransform(const AVCCharacterBase* Character,
                                                float DeltaTime) const PURE_VIRTUAL;

    // FOV for this mode
    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float FieldOfView = 90.f;

    // Blend weight (0-1) managed by camera manager during transitions
    float CurrentBlendWeight = 0.f;
};
```

**First-Person Mode** — locks to head socket, no boom, higher FOV:

```cpp
UCLASS()
class UVCFirstPersonCameraMode : public UVCCameraModeBase
{
    GENERATED_BODY()
public:
    UVCFirstPersonCameraMode() { FieldOfView = 100.f; }

    virtual FTransform ComputeDesiredTransform(const AVCCharacterBase* Character,
                                                float DeltaTime) const override;

    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    FName HeadSocketName = "head";

    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    FVector EyeOffset = FVector(0, 0, 5.f); // Fine-tune eye height
};
```

**Third-Person Mode** — spring arm behavior with voxel collision:

```cpp
UCLASS()
class UVCThirdPersonCameraMode : public UVCCameraModeBase
{
    GENERATED_BODY()
public:
    UVCThirdPersonCameraMode() { FieldOfView = 90.f; }

    virtual FTransform ComputeDesiredTransform(const AVCCharacterBase* Character,
                                                float DeltaTime) const override;

    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float ArmLength = 300.f;

    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    FVector TargetOffset = FVector(0, 50.f, 60.f); // Over-shoulder

    UPROPERTY(EditDefaultsOnly, Category = "Camera")
    float LagSpeed = 10.f;
};
```

---

## Integration Architecture

The character controller connects to each plugin system through **interfaces and components**, never hard class dependencies. This ensures the character plugin can compile and run even if a specific gameplay plugin isn't present.

### Integration Boundary Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                   AVCCharacterBase                          │
│                                                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │ VCMovement   │  │ VCCamera     │  │ AbilitySystem    │  │
│  │ Component    │  │ Manager      │  │ Component (GAS)  │  │
│  └──────┬───────┘  └──────────────┘  └────────┬─────────┘  │
│         │                                      │            │
│  ┌──────┴──────────────────────────────────────┴─────────┐  │
│  │              Integration Layer (Interfaces)           │  │
│  └──┬──────────────┬──────────────────┬─────────────┬────┘  │
│     │              │                  │             │        │
└─────┼──────────────┼──────────────────┼─────────────┼────────┘
      │              │                  │             │
      ▼              ▼                  ▼             ▼
┌───────────┐ ┌────────────┐ ┌──────────────┐ ┌────────────┐
│ Inventory │ │Interaction │ │  Equipment   │ │  Voxel     │
│  Plugin   │ │  Plugin    │ │   Plugin     │ │  Worlds    │
└───────────┘ └────────────┘ └──────────────┘ └────────────┘
```

### 3.1 Inventory Plugin Integration

The character doesn't own inventory directly — it accesses inventory through `IInventoryOwnerInterface` (defined in CommonGameFramework), which can live on the PlayerState, a component, or a separate actor.

```cpp
// In VCInventoryInterface.h — thin adapter, NOT a hard dependency

UINTERFACE(MinimalAPI, Blueprintable)
class UVCInventoryBridge : public UInterface { GENERATED_BODY() };

class VOXELCHARACTERPLUGIN_API IVCInventoryBridge
{
    GENERATED_BODY()
public:
    // Quick access to the player's primary inventory
    virtual UInventoryContainerComponent* GetPrimaryInventory() const = 0;

    // Hotbar slot selection (drives equipment and held item visuals)
    virtual int32 GetActiveHotbarSlot() const = 0;
    virtual void SetActiveHotbarSlot(int32 SlotIndex) = 0;

    // Item pickup request — returns true if any items were added
    virtual bool RequestPickupItem(AWorldItem* WorldItem) = 0;

    // Drop currently held item into the world
    virtual bool RequestDropActiveItem(int32 Count = 1) = 0;
};
```

**Flow: Picking Up an Item**

```
1. InteractionScanner detects AWorldItem in range
2. Player presses Interact → Input_Interact fires
3. Character calls InteractionScanner->TryInteract()
4. InteractionPlugin resolves the interaction → calls IVCInventoryBridge::RequestPickupItem
5. InventoryPlugin validates space, weight, adds item (server-authoritative)
6. On success: WorldItem is destroyed, inventory UI updates via delegate
7. EquipmentPlugin notified if item auto-equips to hotbar
```

### 3.2 Interaction Plugin Integration

The `InteractionScannerComponent` (from InteractionPlugin) lives on the character and handles detection of interactable objects. The character controller simply provides input routing and view-mode-aware trace origins.

```cpp
// The character provides trace context to the interaction system
// based on current view mode

FVector AVCCharacterBase::GetInteractionTraceOrigin() const
{
    if (CurrentViewMode == EVCViewMode::FirstPerson)
    {
        // Trace from camera/eye position
        return CameraManager->GetCurrentCameraLocation();
    }
    else
    {
        // Trace from character center + camera direction
        // Avoids interacting with objects behind the character in TP
        return GetActorLocation() + FVector(0, 0, BaseEyeHeight);
    }
}

FVector AVCCharacterBase::GetInteractionTraceDirection() const
{
    // Always use camera forward, regardless of view mode
    return CameraManager->GetCurrentCameraRotation().Vector();
}
```

**Interaction scanner configuration differs by view mode:**

| Parameter | First Person | Third Person |
|-----------|-------------|--------------|
| Trace Origin | Camera position | Character eye height |
| Trace Direction | Camera forward | Camera forward |
| Max Distance | 300 units | 400 units (compensate for camera offset) |
| Cone Angle | Narrow (15°) | Wider (25°) |
| Overlap Radius | Small | Larger (for proximity interactions) |

### 3.3 Equipment Plugin Integration

The `EquipmentManagerComponent` (from EquipmentPlugin) handles slot management and mesh attachment. The character controller's responsibility is:

1. **Providing attachment points** (sockets on the skeleton)
2. **Routing input** to equipped item actions
3. **View-mode-aware visuals** (different meshes/visibility in FP vs TP)

```cpp
// Equipment integration on the character

void AVCCharacterBase::OnEquipmentChanged(EEquipmentSlot Slot,
                                           const FEquipmentSlotState& NewState)
{
    // Update visuals based on current view mode
    UpdateEquipmentVisuals(Slot, NewState);

    // If this is the active held item, update first-person arms animation
    if (Slot == EEquipmentSlot::MainHand && CurrentViewMode == EVCViewMode::FirstPerson)
    {
        UpdateFirstPersonArmsForItem(NewState.ItemDefinition);
    }

    // Notify GAS if present — equipment may grant/remove abilities
    if (AbilitySystemComponent)
    {
        // Future: EquipmentPlugin provides ability sets per item
        // GAS grants/removes abilities based on equipment state
    }
}

void AVCCharacterBase::UpdateEquipmentVisuals(EEquipmentSlot Slot,
                                               const FEquipmentSlotState& State)
{
    if (CurrentViewMode == EVCViewMode::FirstPerson)
    {
        // Attach to first-person arms mesh
        // Only MainHand and OffHand are visible
        if (Slot == EEquipmentSlot::MainHand || Slot == EEquipmentSlot::OffHand)
        {
            EquipmentManager->AttachEquipmentToMesh(Slot, FirstPersonArmsMesh);
        }
    }
    else
    {
        // Attach to full body mesh — all slots visible
        EquipmentManager->AttachEquipmentToMesh(Slot, GetMesh());
    }
}
```

**Socket Map (defined as a DataAsset for flexibility):**

```cpp
USTRUCT(BlueprintType)
struct FVCEquipmentSocketMapping
{
    GENERATED_BODY()

    // Socket on the THIRD PERSON body mesh
    UPROPERTY(EditDefaultsOnly)
    FName BodySocket;

    // Socket on the FIRST PERSON arms mesh (may differ or be NAME_None)
    UPROPERTY(EditDefaultsOnly)
    FName ArmsSocket;
};

// Example mappings:
// MainHand   → BodySocket: "hand_r",    ArmsSocket: "fp_hand_r"
// OffHand    → BodySocket: "hand_l",    ArmsSocket: "fp_hand_l"
// Back       → BodySocket: "spine_03",  ArmsSocket: NAME_None (not visible in FP)
// Head       → BodySocket: "head",      ArmsSocket: NAME_None
```

### 3.4 VoxelWorlds Integration

The character controller interacts with the voxel world in three ways: movement, camera collision, and block manipulation.

```cpp
// --- Voxel Terrain Queries (Movement Component) ---

USTRUCT()
struct FVoxelTerrainContext
{
    GENERATED_BODY()

    EVoxelSurfaceType SurfaceType = EVoxelSurfaceType::Default;
    float SurfaceHardness = 1.f;       // Affects footstep sounds
    float FrictionMultiplier = 1.f;    // Affects ground friction
    bool bIsUnderwater = false;        // Voxel water detection
    float WaterDepth = 0.f;            // Depth below water surface
    FIntVector CurrentChunkCoord;       // For chunk update subscription
};

// --- Block Manipulation (Character) ---

// Trace from camera into voxel world
bool AVCCharacterBase::TraceForVoxel(FHitResult& OutHit, float MaxDistance) const
{
    const FVector Start = CameraManager->GetCurrentCameraLocation();
    const FVector End = Start + CameraManager->GetCurrentCameraRotation().Vector() * MaxDistance;

    // Use VoxelWorlds' custom trace channel or their provided query API
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    return GetWorld()->LineTraceSingleByChannel(OutHit, Start, End,
                                                 ECC_VoxelTerrain, Params);
}

// Primary action routes through equipped item, which may include voxel modification
void AVCCharacterBase::Input_PrimaryAction(const FInputActionValue& Value)
{
    if (Value.Get<bool>())
    {
        // 1. Check if GAS wants to handle this (future)
        // 2. Check equipped item action
        const auto* ActiveItem = EquipmentManager->GetEquippedItem(EEquipmentSlot::MainHand);
        if (ActiveItem)
        {
            if (ActiveItem->ItemType == EItemType::Tool)
            {
                // Tool interaction with voxel (mining, etc.)
                FHitResult VoxelHit;
                if (TraceForVoxel(VoxelHit))
                {
                    // Server-authoritative voxel modification
                    if (auto* PC = Cast<AVCPlayerController>(GetController()))
                    {
                        PC->Server_RequestVoxelModification(
                            WorldToVoxelCoord(VoxelHit.ImpactPoint),
                            EVoxelModificationType::Destroy,
                            0
                        );
                    }
                }
            }
        }

        // 3. Fallback: punch/default action
    }
}
```

### 3.5 GAS Integration (Planned / Scaffolding — ASC on PlayerState)

GAS integration is scaffolded with clear extension points. The `AbilitySystemComponent` and all `AttributeSets` live on `AVCPlayerState` so they survive character death/respawn. The character implements `IAbilitySystemInterface` as a passthrough.

**Why ASC on PlayerState:**
- Attributes (health, stamina, buffs) persist across respawn without reconstruction
- Active cooldowns survive death — no need to re-grant and re-track
- Debuffs/curses that should persist across death are still present to manage
- `GameplayEffects` can cleanly handle the "reset on respawn" vs "persist through death" distinction
- Standard pattern for respawn-based games (Fortnite, Lyra)

```cpp
// --- AVCPlayerState owns the ASC ---

UCLASS()
class VOXELCHARACTERPLUGIN_API AVCPlayerState : public APlayerState,
                                                 public IAbilitySystemInterface
{
    GENERATED_BODY()

public:
    AVCPlayerState();

    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override
    {
        return AbilitySystemComponent;
    }

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "GAS")
    TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

    // Attribute sets owned by PlayerState (stable across respawns)
    UPROPERTY()
    TObjectPtr<UVCCharacterAttributeSet> CharacterAttributes;

    UPROPERTY()
    TObjectPtr<UVCCombatAttributeSet> CombatAttributes;

    // --- Death / Respawn Attribute Management ---

    // Apply this GE on respawn to reset health/stamina to max
    // while preserving persistent effects (curses, quest buffs, etc.)
    UPROPERTY(EditDefaultsOnly, Category = "GAS|Respawn")
    TSubclassOf<UGameplayEffect> RespawnResetEffect;

    // Tags on GEs that should be REMOVED on death
    // (e.g., temporary combat buffs, food buffs)
    UPROPERTY(EditDefaultsOnly, Category = "GAS|Respawn")
    FGameplayTagContainer DeathCleanseTags;

    void HandleRespawnAttributeReset();
};

// --- Character is a passthrough for IAbilitySystemInterface ---

class AVCCharacterBase : public ACharacter, public IAbilitySystemInterface
{
    virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override
    {
        if (const auto* PS = GetPlayerState<AVCPlayerState>())
        {
            return PS->GetAbilitySystemComponent();
        }
        return nullptr;
    }
};
```

**Character ↔ PlayerState ASC Initialization Flow:**

```cpp
void AVCCharacterBase::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);

    // Server: Initialize ASC with this character as the avatar
    if (auto* PS = GetPlayerState<AVCPlayerState>())
    {
        UAbilitySystemComponent* ASC = PS->GetAbilitySystemComponent();
        if (ASC)
        {
            // ASC->OwnerActor = PlayerState (stable)
            // ASC->AvatarActor = this Character (changes on respawn)
            ASC->InitAbilityActorInfo(PS, this);

            // Grant default abilities if first possession
            if (!PS->bAbilitiesGranted)
            {
                GrantDefaultAbilities(ASC);
                PS->bAbilitiesGranted = true;
            }

            // Bind GAS attribute change delegates to character systems
            BindAttributeChangeDelegates(ASC);
        }
    }
}

// Client-side equivalent
void AVCCharacterBase::OnRep_PlayerState()
{
    Super::OnRep_PlayerState();

    if (auto* PS = GetPlayerState<AVCPlayerState>())
    {
        if (auto* ASC = PS->GetAbilitySystemComponent())
        {
            ASC->InitAbilityActorInfo(PS, this);
            BindAttributeChangeDelegates(ASC);
        }
    }
}

void AVCCharacterBase::BindAttributeChangeDelegates(UAbilitySystemComponent* ASC)
{
    // Movement component listens to speed attribute
    if (auto* MovComp = Cast<UVCMovementComponent>(GetCharacterMovement()))
    {
        ASC->GetGameplayAttributeValueChangeDelegate(
            UVCCharacterAttributeSet::GetMoveSpeedMultiplierAttribute()
        ).AddUObject(MovComp, &UVCMovementComponent::OnMoveSpeedAttributeChanged);
    }

    // Interaction scanner listens to range attribute
    if (InteractionScanner)
    {
        ASC->GetGameplayAttributeValueChangeDelegate(
            UVCCharacterAttributeSet::GetInteractionRangeAttribute()
        ).AddUObject(InteractionScanner, &UInteractionScannerComponent::OnRangeAttributeChanged);
    }
}
```

**Death → Respawn Flow:**

```
1. Character dies (Health <= 0, handled by GE or ASC callback)
2. Server: ASC removes GEs tagged with DeathCleanseTags (temp buffs)
         ASC does NOT remove persistent effects (curses, quest buffs)
3. Character is destroyed / unpossessed
4. PlayerState (+ ASC, attributes, surviving GEs) remains
5. New character spawns, Controller possesses it
6. PossessedBy → ASC->InitAbilityActorInfo(PS, NewCharacter)
         → rebinds avatar, delegates reconnect
7. RespawnResetEffect applied: sets Health = MaxHealth, Stamina = MaxStamina
8. Equipment re-granted abilities via EquipmentManager on new character
```

**Attribute Sets (on PlayerState):**

```cpp
UCLASS()
class UVCCharacterAttributeSet : public UAttributeSet
{
    GENERATED_BODY()
public:
    // Core vitals
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Health)
    FGameplayAttributeData Health;

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth)
    FGameplayAttributeData MaxHealth;

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Stamina)
    FGameplayAttributeData Stamina;

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxStamina)
    FGameplayAttributeData MaxStamina;

    // Movement-affecting (consumed by VCMovementComponent via delegate)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MoveSpeed)
    FGameplayAttributeData MoveSpeedMultiplier;

    // Voxel interaction
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MiningSpeed)
    FGameplayAttributeData MiningSpeed;

    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_InteractionRange)
    FGameplayAttributeData InteractionRange;

    // Meta attribute: incoming damage (not replicated, used in Pre/PostGameplayEffectExecute)
    UPROPERTY(BlueprintReadOnly)
    FGameplayAttributeData IncomingDamage;

    ATTRIBUTE_ACCESSORS(UVCCharacterAttributeSet, Health)
    ATTRIBUTE_ACCESSORS(UVCCharacterAttributeSet, MaxHealth)
    ATTRIBUTE_ACCESSORS(UVCCharacterAttributeSet, Stamina)
    ATTRIBUTE_ACCESSORS(UVCCharacterAttributeSet, MaxStamina)
    ATTRIBUTE_ACCESSORS(UVCCharacterAttributeSet, MoveSpeedMultiplier)
    ATTRIBUTE_ACCESSORS(UVCCharacterAttributeSet, MiningSpeed)
    ATTRIBUTE_ACCESSORS(UVCCharacterAttributeSet, InteractionRange)
    ATTRIBUTE_ACCESSORS(UVCCharacterAttributeSet, IncomingDamage)

    virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
    virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;
};
```

**GAS Integration Points:**

| System | GAS Connection | Notes |
|--------|---------------|-------|
| Movement | `MoveSpeedMultiplier` attribute → `MaxWalkSpeed` | Via attribute change delegate on movement component |
| Equipment | Equipment items grant `GameplayAbility` sets | Equip = grant, unequip = revoke; re-granted on respawn |
| Inventory | Weight/encumbrance as `GameplayEffect` | Modifies move speed attribute; persists across respawn |
| Interaction | `InteractionRange` attribute | Modifies scanner reach via delegate |
| Voxel Mining | `MiningSpeed` attribute | Scales block break time; tool bonuses stack as GEs |
| Abilities | Input binding via `GameplayAbility` activation | Replaces direct input for combat/special abilities |
| Death | `IncomingDamage` meta attribute → clamp Health | `PostGameplayEffectExecute` triggers death flow |
| Respawn | `RespawnResetEffect` GE resets vitals | `DeathCleanseTags` controls which GEs to strip |

**GAS Architecture:**

```
AVCPlayerState (stable across respawns)
  └── UAbilitySystemComponent
        ├── Owner: PlayerState
        ├── Avatar: Current Character (re-bound on respawn)
        ├── UVCCharacterAttributeSet (health, stamina, speed, mining)
        ├── UVCCombatAttributeSet (damage, defense, crit — future)
        └── Granted Abilities:
              ├── From Character Defaults (granted once, persistent)
              ├── From Equipment (dynamic, re-granted per new avatar)
              └── From Voxel Environment (swimming, burning, etc.)
```

---

## Input System

Uses UE5 Enhanced Input with context-sensitive mapping.

```cpp
UCLASS(BlueprintType)
class UVCInputConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    // --- Input Actions ---
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_Move;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_Look;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_Jump;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_Interact;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_PrimaryAction;    // LMB — attack/mine/use

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_SecondaryAction;  // RMB — place/aim/alt-use

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_ToggleView;       // V — switch FP/TP

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_OpenInventory;     // Tab/I

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_HotbarSlot;        // 1-9

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputAction> IA_ScrollHotbar;      // Mouse wheel

    // --- Input Mapping Contexts ---
    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputMappingContext> IMC_Gameplay;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputMappingContext> IMC_UI;

    UPROPERTY(EditDefaultsOnly, Category = "Input")
    TObjectPtr<UInputMappingContext> IMC_Vehicle;  // Future
};
```

**Input Mapping Context Priority Stack:**

| Priority | Context | Active When |
|----------|---------|-------------|
| 0 | `IMC_Gameplay` | Always (base) |
| 1 | `IMC_UI` | Inventory/menu open |
| 2 | `IMC_Vehicle` | In vehicle (future) |
| 10 | `IMC_Ability` | During ability activation (GAS, future) |

---

## Multiplayer Architecture

```
┌──────────── SERVER ────────────┐     ┌──────────── CLIENT ────────────┐
│                                │     │                                │
│  AVCCharacterBase              │     │  AVCCharacterBase              │
│  ├─ Movement (authority)       │◄────┤  ├─ Movement (prediction)     │
│  ├─ EquipmentManager (auth)    │     │  ├─ EquipmentManager (visual) │
│  ├─ Inventory (auth, on PS)    │     │  ├─ Inventory (replicated)    │
│  ├─ ViewMode (replicated)      │────►│  ├─ ViewMode (local + rep)    │
│  └─ ASC (authority)            │     │  └─ ASC (prediction)          │
│                                │     │                                │
│  Voxel Modification            │     │  Voxel Modification           │
│  └─ Validate → Apply           │────►│  └─ Predict → Reconcile      │
└────────────────────────────────┘     └────────────────────────────────┘
```

**Replication Strategy:**

| Property/Action | Owner | Replication |
|----------------|-------|-------------|
| View Mode | Server | `ReplicatedUsing` — other clients see correct mesh visibility |
| Movement | Server | Standard CMC prediction/correction |
| Equipment State | Server | Via EquipmentPlugin replication |
| Inventory | Server (PlayerState) | Via InventoryPlugin replication |
| Voxel Modifications | Server | Client predicts, server validates & broadcasts |
| GAS Attributes | Server | Via ASC replication (prediction for owning client) |
| Camera | Local Only | Never replicated |

---

## Animation Integration

```cpp
UCLASS()
class UVCAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    // --- Locomotion ---
    UPROPERTY(BlueprintReadOnly, Category = "Animation")
    float Speed = 0.f;

    UPROPERTY(BlueprintReadOnly, Category = "Animation")
    float Direction = 0.f; // Strafe angle

    UPROPERTY(BlueprintReadOnly, Category = "Animation")
    bool bIsFalling = false;

    UPROPERTY(BlueprintReadOnly, Category = "Animation")
    bool bIsCrouching = false;

    // --- View Mode ---
    UPROPERTY(BlueprintReadOnly, Category = "Animation")
    EVCViewMode ViewMode = EVCViewMode::ThirdPerson;

    // --- Equipment ---
    UPROPERTY(BlueprintReadOnly, Category = "Animation")
    EEquipmentAnimType ActiveItemAnimType = EEquipmentAnimType::Unarmed;
    // Drives upper body animation layer (sword, pickaxe, bow, etc.)

    // --- Aim/Look ---
    UPROPERTY(BlueprintReadOnly, Category = "Animation")
    float AimPitch = 0.f;  // For aim offset / look-at

    UPROPERTY(BlueprintReadOnly, Category = "Animation")
    float AimYaw = 0.f;

    // --- Voxel Surface ---
    UPROPERTY(BlueprintReadOnly, Category = "Animation")
    EVoxelSurfaceType SurfaceType = EVoxelSurfaceType::Default;
    // Drives footstep anim notifies (different sounds per surface)
};
```

---

## View Mode Transition Details

Switching between first and third person is more than just moving a camera — it requires coordinated updates across multiple systems:

```cpp
void AVCCharacterBase::SetViewMode(EVCViewMode NewMode)
{
    if (CurrentViewMode == NewMode) return;

    const EVCViewMode OldMode = CurrentViewMode;
    CurrentViewMode = NewMode;

    // 1. Camera transition
    if (NewMode == EVCViewMode::FirstPerson)
        CameraManager->PushCameraMode(CameraManager->FirstPersonModeClass);
    else
        CameraManager->PushCameraMode(CameraManager->ThirdPersonModeClass);

    // 2. Mesh visibility
    UpdateMeshVisibility();

    // 3. Equipment re-attachment (FP arms vs TP body)
    if (EquipmentManager)
    {
        EquipmentManager->ReattachAllEquipment(
            NewMode == EVCViewMode::FirstPerson ? FirstPersonArmsMesh : GetMesh()
        );
    }

    // 4. Interaction scanner parameters
    if (InteractionScanner)
    {
        InteractionScanner->SetScanProfile(
            NewMode == EVCViewMode::FirstPerson
                ? EInteractionScanProfile::FirstPerson
                : EInteractionScanProfile::ThirdPerson
        );
    }

    // 5. Animation
    if (auto* AnimInst = Cast<UVCAnimInstance>(GetMesh()->GetAnimInstance()))
    {
        AnimInst->ViewMode = NewMode;
    }

    // 6. Replicate to other clients (they need to see correct body mesh)
    if (HasAuthority())
    {
        OnRep_ViewMode(); // Local execution
    }

    // 7. Broadcast event for any other listeners
    OnViewModeChanged.Broadcast(OldMode, NewMode);
}
```

---

## Development Phases

### Phase 1: Core Character (Week 1-2)
- `AVCCharacterBase` with basic movement
- `UVCMovementComponent` (standard CMC extension, no voxel yet)
- Enhanced Input setup with `UVCInputConfig`
- Basic capsule + mesh setup

### Phase 2: Camera System (Week 2-3)
- `UVCCameraManager` with mode stack
- First-person and third-person camera modes
- Smooth blending between modes
- Mesh visibility management on mode switch

### Phase 3: Plugin Integration (Week 3-5)
- Wire up `InteractionScannerComponent`
- Wire up `EquipmentManagerComponent`
- Inventory bridge interface
- Equipment visual attachment (FP/TP aware)
- Input routing to interaction and equipment systems

### Phase 4: Voxel World Integration (Week 5-7)
- Voxel terrain context queries in movement component
- Surface-type-based movement modifiers
- Voxel camera collision (third person)
- Block trace and modification routing
- Chunk update subscription for cache invalidation

### Phase 5: GAS Scaffolding (Week 7-8)
- `UAbilitySystemComponent` on character
- `UVCCharacterAttributeSet` with core attributes
- Movement component reads from attribute set
- Equipment ability granting hooks (stubs)

### Phase 6: Animation & Polish (Week 8-10)
- `UVCAnimInstance` with locomotion + equipment layers
- First-person arms animation
- View mode transition polish
- Footstep surface detection from voxel data

---

## Open Questions & Future Considerations

1. **ASC on PlayerState (Decided):** The `AbilitySystemComponent` and all `AttributeSets` live on `AVCPlayerState`. This supports respawn-based death where attributes, cooldowns, and persistent effects survive character destruction. The character is the Avatar (re-bound on each possession), PlayerState is the Owner (stable). See §3.5 for the full initialization flow.

2. **Vehicle / Mount System:** The camera manager's mode stack and input context system are designed to support future vehicle possession. A vehicle would push its own camera mode and input context.

3. **Voxel Water:** How does VoxelWorlds represent water? The movement component needs to detect water voxels for swimming transitions. This may require a custom query or the voxel engine may expose a water level API.

4. **Networked Voxel Prediction:** Block placement/destruction should be client-predicted for responsiveness, but needs server validation. The `Server_RequestVoxelModification` RPC handles this, but a rollback mechanism may be needed if the server rejects a modification.

5. **First-Person Body Visibility:** Some games show the player's body when looking down in FP. This is possible by keeping the body mesh visible with `bOwnerNoSee` on certain bones, but adds complexity. Recommend starting with separate FP arms mesh and revisiting.
