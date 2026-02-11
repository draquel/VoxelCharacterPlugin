# VoxelCharacterPlugin — Implementation Plan

## Current State

**Implemented:** Empty module shell (3 files: `VoxelCharacterPlugin.h`, `.cpp`, `.Build.cs`)
**Remaining:** All gameplay classes, all integration, all content assets

The design document (`CharacterController_Design.md`) and development instructions (`.claude/instructions.md`) are comprehensive and serve as the authoritative spec. This plan translates them into an ordered task list with compile gates, file manifests, and implementation notes for each step.

---

## Build Sequence Overview

| Gate | Name | Files | Outcome |
|------|------|-------|---------|
| 1 | Core Skeleton | 7 | Module loads, enums/structs/DataAsset exist |
| 2 | Movement & Camera | 10 | Character can move and look in FP/TP |
| 3 | Character & Controller | 10 | Full character spawns, moves, GAS initializes |
| 4 | Integration Bridges | 8+ | Compiles with and without gameplay plugins |
| 5 | Voxel Integration | 4+ | Character interacts with voxel terrain |

---

## Gate 1: Core Skeleton

**Goal:** Module compiles with all dependencies, core types exist for downstream use.

### 1.1 — Update `VoxelCharacterPlugin.uplugin`

Add plugin dependencies:

```json
"Plugins": [
    { "Name": "CommonGameFramework", "Enabled": true },
    { "Name": "VoxelWorlds", "Enabled": true },
    { "Name": "GameplayAbilities", "Enabled": true },
    { "Name": "EnhancedInput", "Enabled": true },
    { "Name": "ItemInventoryPlugin", "Enabled": true, "Optional": true },
    { "Name": "InteractionPlugin", "Enabled": true, "Optional": true },
    { "Name": "EquipmentPlugin", "Enabled": true, "Optional": true }
]
```

### 1.2 — Update `VoxelCharacterPlugin.Build.cs`

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core",
    "CoreUObject",
    "Engine",
    "InputCore",
    "EnhancedInput",
    "GameplayAbilities",
    "GameplayTags",
    "GameplayTasks",
    "CommonGameFramework",
    "NetCore",
});

PrivateDependencyModuleNames.AddRange(new string[] {
    "Slate",
    "SlateCore",
    "VoxelCore",
});
```

Add conditional (soft) dependencies for optional gameplay plugins:

```csharp
// Soft dependencies — compile without them, resolve at runtime
// These are PrivateDependency so headers only included in .cpp files
PrivateDependencyModuleNames.AddRange(new string[] {
    "ItemInventoryPlugin",
    "InteractionPlugin",
    "EquipmentPlugin",
});
```

**Note:** If the goal is to compile *without* these plugins present, use `ConditionalAddModuleDirectory` or `#if WITH_EDITOR` guards and dynamic loading. Alternatively, keep them as hard private deps during initial development and add conditional compilation later. The instructions say "compiles without them" — this requires `PublicDefinitions` feature flags and conditional `#if` blocks. Plan for this in Gate 4.

**Revised approach for build-without-optional-plugins:** Use `bUsePrecompiled` checks or define feature macros:

```csharp
// In Build.cs — detect if optional plugins exist
bool bHasInventory = Target.ProjectFile != null &&
    Directory.Exists(Path.Combine(PluginDirectory, "..", "ItemInventoryPlugin"));

if (bHasInventory)
{
    PrivateDependencyModuleNames.Add("ItemInventoryPlugin");
    PublicDefinitions.Add("WITH_INVENTORY_PLUGIN=1");
}
else
{
    PublicDefinitions.Add("WITH_INVENTORY_PLUGIN=0");
}
// Repeat for InteractionPlugin, EquipmentPlugin
```

For initial development (Gates 1-3), assume all plugins are present. Gate 4 adds the conditional compilation.

### 1.3 — Create `Public/Core/VCTypes.h`

Central types header containing:

```cpp
// EVCViewMode enum
enum class EVCViewMode : uint8 { FirstPerson, ThirdPerson };

// EVoxelSurfaceType enum
enum class EVoxelSurfaceType : uint8 { Default, Stone, Dirt, Sand, Ice, Mud, Snow, Wood, Metal };

// EVoxelModificationType enum
enum class EVoxelModificationType : uint8 { Destroy, Place, Paint };

// FVoxelTerrainContext struct
struct FVoxelTerrainContext { ... };

// FVCEquipmentSocketMapping struct
struct FVCEquipmentSocketMapping { ... };

// Delegates
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVCViewModeChanged, EVCViewMode, OldMode, EVCViewMode, NewMode);
```

### 1.4 — Create `Public/Input/VCInputConfig.h` + `Private/Input/VCInputConfig.cpp`

`UVCInputConfig : UDataAsset` with all input action and mapping context references per design doc. Pure data container — no logic.

### 1.5 — Update Module Files

Update `VoxelCharacterPlugin.h/.cpp` with proper API export macro and log category:

```cpp
DECLARE_LOG_CATEGORY_EXTERN(LogVoxelCharacter, Log, All);
```

### Gate 1 Compile Check
- Module loads in editor
- `EVCViewMode`, `EVoxelSurfaceType`, `FVoxelTerrainContext` available
- `UVCInputConfig` DataAsset can be created in editor

**Files created/modified this gate:**
| # | File | Action |
|---|------|--------|
| 1 | `VoxelCharacterPlugin.uplugin` | Modify — add plugin deps |
| 2 | `VoxelCharacterPlugin.Build.cs` | Modify — add module deps |
| 3 | `Public/VoxelCharacterPlugin.h` | Modify — add API macro, log category |
| 4 | `Private/VoxelCharacterPlugin.cpp` | Modify — define log category |
| 5 | `Public/Core/VCTypes.h` | Create — enums, structs, delegates |
| 6 | `Public/Input/VCInputConfig.h` | Create — input DataAsset header |
| 7 | `Private/Input/VCInputConfig.cpp` | Create — input DataAsset impl |

---

## Gate 2: Movement & Camera

**Goal:** Movement component and camera system work standalone. A character with these components can walk, jump, look around, and switch between FP/TP camera.

### 2.1 — `UVCMovementComponent`

**Files:** `Public/Movement/VCMovementComponent.h`, `Private/Movement/VCMovementComponent.cpp`

Extends `UCharacterMovementComponent`. Initial implementation:

- Terrain context cache (`FVoxelTerrainContext CachedTerrainContext`)
- `UpdateVoxelTerrainContext()` — stub that defaults to `EVoxelSurfaceType::Default` (voxel queries added in Gate 5)
- `TerrainCacheDuration` property (default 0.1s)
- Surface type → friction multiplier mapping (a simple switch/map)
- `OnVoxelChunkModified()` delegate handler — stub
- `FindFloor()` override — call Super, add transitional state handling for async mesh rebuilds (if no floor found and was grounded last frame, hold grounded briefly)
- `PhysCustom()` override — placeholder for climbing mode
- Exposed properties: `VoxelClimbingSpeed`, `VoxelSwimmingSpeedMultiplier`, `VoxelSurfaceGripMultiplier`, `CurrentSurfaceType`
- `OnMoveSpeedAttributeChanged()` — GAS delegate callback for `MoveSpeedMultiplier` attribute (updates `MaxWalkSpeed`)

**Note:** At this gate, the movement component works as a standard CMC extension. Voxel-specific queries are stubs returning defaults.

### 2.2 — Camera Mode Base

**Files:** `Public/Camera/VCCameraModeBase.h`, `Private/Camera/VCCameraModeBase.cpp`

Abstract base class (UObject):

- `ComputeDesiredTransform(Character, DeltaTime)` — pure virtual
- `FieldOfView` property (default 90)
- `CurrentBlendWeight` (managed by CameraManager)

### 2.3 — First-Person Camera Mode

**Files:** `Public/Camera/VCFirstPersonCameraMode.h`, `Private/Camera/VCFirstPersonCameraMode.cpp`

Extends `UVCCameraModeBase`:

- `HeadSocketName` (default "head")
- `EyeOffset` (default (0, 0, 5))
- `FieldOfView = 100`
- `ComputeDesiredTransform()` — gets head socket transform from character mesh, applies eye offset, returns transform. Falls back to actor location + BaseEyeHeight if socket missing.

### 2.4 — Third-Person Camera Mode

**Files:** `Public/Camera/VCThirdPersonCameraMode.h`, `Private/Camera/VCThirdPersonCameraMode.cpp`

Extends `UVCCameraModeBase`:

- `ArmLength = 300`
- `TargetOffset = (0, 50, 60)` (over-shoulder)
- `LagSpeed = 10`
- `FieldOfView = 90`
- `ComputeDesiredTransform()` — compute pivot at character + target offset, compute desired at pivot - forward * arm length, apply camera lag interpolation. Does NOT handle voxel collision here (that's in CameraManager).

### 2.5 — Camera Manager

**Files:** `Public/Camera/VCCameraManager.h`, `Private/Camera/VCCameraManager.cpp`

`UActorComponent` on the character:

- `CameraModeStack` (TArray of UVCCameraModeBase*)
- `PushCameraMode(TSubclassOf<>)` — creates instance, pushes to stack, starts blend
- `PopCameraMode()` — removes top mode
- `UpdateCamera(DeltaTime)` — called from character Tick:
  1. Update blend weights on stack (top mode blends toward 1.0, others toward 0.0)
  2. Compute each active mode's desired transform
  3. Blend between top two transforms based on weights
  4. Apply voxel camera collision (TP mode only) — `ResolveVoxelCameraCollision()`
  5. Set owning character's camera transform via `GetOwner<ACharacter>()->Controller->SetControlRotation()` or via a camera component
- Config: `FirstPersonModeClass`, `ThirdPersonModeClass`, `ModeTransitionBlendTime`
- Voxel collision: `bUseVoxelCameraCollision`, `CameraCollisionProbeSize`
- `GetCurrentCameraLocation()`, `GetCurrentCameraRotation()` — accessors for current blended result
- `ResolveVoxelCameraCollision()` — line trace from pivot to desired camera location, pull camera forward on hit. Stub in Gate 2, real voxel trace in Gate 5.

**Camera approach decision:** The character needs a `UCameraComponent`. Two options:
1. CameraManager directly moves a `UCameraComponent` attached to the character
2. CameraManager sets `APlayerController::SetControlRotation` and overrides `CalcCamera`

**Recommended:** Add a `UCameraComponent` to `AVCCharacterBase`, and have `UVCCameraManager::UpdateCamera()` set its world transform each tick. This is simpler and more Unreal-idiomatic than overriding CalcCamera.

### Gate 2 Compile Check
- A test character with `UVCMovementComponent` and `UVCCameraManager` can be spawned
- FP/TP camera modes produce correct transforms
- Camera blending works during mode switch
- Movement component applies surface friction (defaulting to 1.0)

**Files created this gate:**
| # | File | Action |
|---|------|--------|
| 1 | `Public/Movement/VCMovementComponent.h` | Create |
| 2 | `Private/Movement/VCMovementComponent.cpp` | Create |
| 3 | `Public/Camera/VCCameraModeBase.h` | Create |
| 4 | `Private/Camera/VCCameraModeBase.cpp` | Create |
| 5 | `Public/Camera/VCFirstPersonCameraMode.h` | Create |
| 6 | `Private/Camera/VCFirstPersonCameraMode.cpp` | Create |
| 7 | `Public/Camera/VCThirdPersonCameraMode.h` | Create |
| 8 | `Private/Camera/VCThirdPersonCameraMode.cpp` | Create |
| 9 | `Public/Camera/VCCameraManager.h` | Create |
| 10 | `Private/Camera/VCCameraManager.cpp` | Create |

---

## Gate 3: Character & Controller

**Goal:** Full character class with all components assembled. Player can spawn, move, look, switch views, and GAS initializes.

### 3.1 — `UVCCharacterAttributeSet`

**Files:** `Public/Core/VCCharacterAttributeSet.h`, `Private/Core/VCCharacterAttributeSet.cpp`

Extends `UAttributeSet`:

- Attributes: Health, MaxHealth, Stamina, MaxStamina, MoveSpeedMultiplier, MiningSpeed, InteractionRange, IncomingDamage (meta)
- `ATTRIBUTE_ACCESSORS` macros for all attributes
- `GetLifetimeReplicatedProps()` — replicate all except IncomingDamage
- `OnRep_*` functions for each replicated attribute
- `PreAttributeChange()` — clamp Health to [0, MaxHealth], Stamina to [0, MaxStamina]
- `PostGameplayEffectExecute()` — handle IncomingDamage: subtract from Health, clamp, trigger death if Health <= 0

### 3.2 — `AVCPlayerState`

**Files:** `Public/Core/VCPlayerState.h`, `Private/Core/VCPlayerState.cpp`

Extends `APlayerState`, implements `IAbilitySystemInterface`:

- Components: `UAbilitySystemComponent`, `UVCCharacterAttributeSet` (created in constructor)
- `GetAbilitySystemComponent()` override
- `RespawnResetEffect` (TSubclassOf<UGameplayEffect>) — applied on respawn
- `DeathCleanseTags` (FGameplayTagContainer) — GEs with these tags removed on death
- `HandleRespawnAttributeReset()` — removes death-cleanse GEs, applies respawn reset effect
- `bAbilitiesGranted` flag — prevents re-granting default abilities on respawn
- Replication setup

### 3.3 — `AVCCharacterBase`

**Files:** `Public/Core/VCCharacterBase.h`, `Private/Core/VCCharacterBase.cpp`

Extends `ACharacter`, implements `IAbilitySystemInterface`:

**Constructor (ObjectInitializer):**
- Override CharacterMovement with `UVCMovementComponent` via `ObjectInitializer.SetDefaultSubobjectClass`
- Create sub-objects: `UVCCameraManager`, `UCameraComponent`, `USkeletalMeshComponent` (FirstPersonArmsMesh)
- Attach camera component to root (CameraManager will drive its transform)
- Attach FP arms mesh to camera component (moves with camera)
- Set replication flags

**Components (UPROPERTY):**
- `CameraManager` (UVCCameraManager)
- `CameraComponent` (UCameraComponent)
- `FirstPersonArmsMesh` (USkeletalMeshComponent)
- `InteractionScanner` — typed as `UActorComponent*` or the actual type with `#if WITH_INTERACTION_PLUGIN` guard (Gate 4 decision)
- `EquipmentManager` — same conditional approach

**View Mode System:**
- `CurrentViewMode` (replicated with OnRep)
- `SetViewMode(EVCViewMode)` — the coordinated 7-step sequence from the design doc
- `OnRep_ViewMode()` — update visuals on remote clients
- `UpdateMeshVisibility()` — FP: body shadow-only + arms visible, TP: body visible + arms hidden
- `OnViewModeChanged` delegate

**Input Handling:**
- `SetupPlayerInputComponent()` — bind all input actions from `UVCInputConfig`
- Input callbacks: `Input_Move`, `Input_Look`, `Input_Jump`, `Input_Interact`, `Input_ToggleView`, `Input_PrimaryAction`, `Input_SecondaryAction`
- Input config resolved from `AVCPlayerController::InputConfig`

**GAS Passthrough:**
- `GetAbilitySystemComponent()` → delegate to PlayerState
- `PossessedBy()` — server-side: init ASC, grant default abilities, bind attribute delegates
- `OnRep_PlayerState()` — client-side: init ASC, bind attribute delegates
- `BindAttributeChangeDelegates(ASC)` — connect MoveSpeed → MovementComponent, InteractionRange → scanner

**Voxel Interaction (stubs for Gate 5):**
- `TraceForVoxel(FHitResult&, MaxDistance)` — line trace from camera
- `Input_PrimaryAction` / `Input_SecondaryAction` — routing chain (GAS → equipment → fallback)

**Tick:**
- Call `CameraManager->UpdateCamera(DeltaTime)`

### 3.4 — `AVCPlayerController`

**Files:** `Public/Core/VCPlayerController.h`, `Private/Core/VCPlayerController.cpp`

Extends `APlayerController`:

- `InputConfig` reference (UPROPERTY, EditDefaultsOnly)
- `SetupInputComponent()` — add default `IMC_Gameplay` mapping context
- `OnPossess()` — resolve InputConfig, set input mode
- Input mode management: `SetGameInputMode()`, `SetUIInputMode(FocusWidget)`
- UI stubs: `ToggleInventoryUI()`, `ShowInteractionPrompt()`, `HideInteractionPrompt()`
- Server RPC: `Server_RequestVoxelModification(FIntVector, EVoxelModificationType, uint8)` — validate range + permissions, then call VoxelWorlds edit API
- Input context management helpers: `AddInputMappingContext()`, `RemoveInputMappingContext()`

### 3.5 — `UVCAnimInstance`

**Files:** `Public/Core/VCAnimInstance.h`, `Private/Core/VCAnimInstance.cpp`

Extends `UAnimInstance`:

- `NativeUpdateAnimation(DeltaSeconds)` — read from owning character:
  - Speed, Direction (from velocity)
  - bIsFalling, bIsCrouching (from movement component)
  - ViewMode (from character)
  - AimPitch, AimYaw (from controller rotation)
  - SurfaceType (from movement component)
  - ActiveItemAnimType (from equipment manager, if available)
- All properties `BlueprintReadOnly` for AnimBP consumption

### Gate 3 Compile Check
- Set GameMode to use `AVCPlayerController`, `AVCCharacterBase`, `AVCPlayerState`
- Character spawns with capsule, moves with WASD, jumps with Space
- Camera switches between FP and TP with smooth blend (V key)
- Mesh visibility toggles correctly per view mode
- GAS initializes — ASC on PlayerState, attributes created with default values
- AnimInstance populates locomotion properties

**Files created this gate:**
| # | File | Action |
|---|------|--------|
| 1 | `Public/Core/VCCharacterAttributeSet.h` | Create |
| 2 | `Private/Core/VCCharacterAttributeSet.cpp` | Create |
| 3 | `Public/Core/VCPlayerState.h` | Create |
| 4 | `Private/Core/VCPlayerState.cpp` | Create |
| 5 | `Public/Core/VCCharacterBase.h` | Create |
| 6 | `Private/Core/VCCharacterBase.cpp` | Create |
| 7 | `Public/Core/VCPlayerController.h` | Create |
| 8 | `Private/Core/VCPlayerController.cpp` | Create |
| 9 | `Public/Core/VCAnimInstance.h` | Create |
| 10 | `Private/Core/VCAnimInstance.cpp` | Create |

---

## Gate 4: Integration Bridges

**Goal:** Clean interface-based integration with ItemInventoryPlugin, InteractionPlugin, and EquipmentPlugin. Plugin compiles with and without these optional dependencies.

### 4.1 — Conditional Compilation Setup

Update `Build.cs` to detect optional plugin presence and define feature macros:

```csharp
PublicDefinitions.Add("WITH_INVENTORY_PLUGIN=" + (bHasInventory ? "1" : "0"));
PublicDefinitions.Add("WITH_INTERACTION_PLUGIN=" + (bHasInteraction ? "1" : "0"));
PublicDefinitions.Add("WITH_EQUIPMENT_PLUGIN=" + (bHasEquipment ? "1" : "0"));
```

Detection via directory existence check or `Target.bBuildAllModules` awareness.

### 4.2 — `IVCInventoryBridge`

**File:** `Public/Integration/VCInventoryBridge.h`

Interface for inventory access:

```cpp
class IVCInventoryBridge
{
    virtual UActorComponent* GetPrimaryInventory() const = 0;
    virtual int32 GetActiveHotbarSlot() const = 0;
    virtual void SetActiveHotbarSlot(int32 SlotIndex) = 0;
    virtual bool RequestPickupItem(AActor* WorldItem) = 0;
    virtual bool RequestDropActiveItem(int32 Count = 1) = 0;
};
```

**Note:** Uses `UActorComponent*` (not `UInventoryComponent*`) in the interface to avoid header dependency. Callers cast in .cpp files.

### 4.3 — `IVCInteractionBridge`

**File:** `Public/Integration/VCInteractionBridge.h`

Interface for interaction system queries:

```cpp
class IVCInteractionBridge
{
    virtual FVector GetInteractionTraceOrigin() const = 0;
    virtual FVector GetInteractionTraceDirection() const = 0;
    virtual void SetInteractionScanProfile(EVCViewMode ViewMode) = 0;
};
```

### 4.4 — `IVCEquipmentBridge`

**File:** `Public/Integration/VCEquipmentBridge.h`

Interface for equipment visual management:

```cpp
class IVCEquipmentBridge
{
    virtual void OnEquipmentChanged(const FGameplayTag& Slot, ...) = 0;
    virtual void UpdateEquipmentVisuals(EVCViewMode ViewMode) = 0;
    virtual void ReattachAllEquipment(USkeletalMeshComponent* TargetMesh) = 0;
};
```

### 4.5 — `IVCAbilityBridge`

**File:** `Public/Integration/VCAbilityBridge.h`

Interface for ability system integration points:

```cpp
class IVCAbilityBridge
{
    virtual void GrantDefaultAbilities(UAbilitySystemComponent* ASC) = 0;
    virtual void OnEquipmentAbilitiesGranted(const FGameplayTag& Slot) = 0;
};
```

### 4.6 — Wire Bridges into Character

Update `AVCCharacterBase.cpp`:

- In constructor/BeginPlay: conditionally create/find `UInteractionComponent` and `UEquipmentManagerComponent` using `#if WITH_*_PLUGIN` guards
- Implement `IVCInventoryBridge` on the character (or a helper component)
- Implement `IVCInteractionBridge` — provide trace origin/direction based on view mode
- Implement `IVCEquipmentBridge` — handle equipment visual attachment to correct mesh
- Wire `OnEquipmentChanged` delegate from EquipmentManagerComponent
- Wire interaction input to InteractionComponent
- Add hotbar input handling (IA_HotbarSlot, IA_ScrollHotbar)

Update `AVCPlayerController.cpp`:

- Wire `ToggleInventoryUI()` to inventory component show/hide
- Wire `ShowInteractionPrompt()` / `HideInteractionPrompt()` to interaction events

### 4.7 — Equipment Socket Mapping

Create or integrate `FVCEquipmentSocketMapping` DataAsset/DataTable for socket name resolution between FP arms and TP body meshes. This maps `EEquipmentSlot` → (BodySocket, ArmsSocket) pairs.

### Gate 4 Compile Check
- Plugin compiles with all gameplay plugins present — full integration works
- Plugin compiles with gameplay plugins ABSENT — character still spawns, moves, camera works
- Equipment visuals attach to correct mesh based on view mode
- Interaction traces originate from correct position per view mode
- Hotbar selection updates held item visual

**Files created this gate:**
| # | File | Action |
|---|------|--------|
| 1 | `VoxelCharacterPlugin.Build.cs` | Modify — conditional deps |
| 2 | `Public/Integration/VCInventoryBridge.h` | Create |
| 3 | `Public/Integration/VCInteractionBridge.h` | Create |
| 4 | `Public/Integration/VCEquipmentBridge.h` | Create |
| 5 | `Public/Integration/VCAbilityBridge.h` | Create |
| 6 | `Private/Core/VCCharacterBase.cpp` | Modify — wire bridges |
| 7 | `Private/Core/VCPlayerController.cpp` | Modify — wire bridges |
| 8 | `Public/Core/VCCharacterBase.h` | Modify — add interface impls |

---

## Gate 5: Voxel Integration

**Goal:** Character fully interacts with voxel terrain — movement adapts to surface, camera avoids terrain, block operations work.

### 5.1 — `UVCVoxelNavigationHelper`

**Files:** `Public/Movement/VCVoxelNavigationHelper.h`, `Private/Movement/VCVoxelNavigationHelper.cpp`

Static utility class for voxel world queries:

- `QueryTerrainContext(UWorld*, FVector Location)` → `FVoxelTerrainContext`
  - Find VoxelChunkManager in world
  - Convert world position to voxel coordinate
  - Read voxel material at feet → map to `EVoxelSurfaceType`
  - Check water level → populate `bIsUnderwater`, `WaterDepth`
  - Return chunk coordinate for subscription
- `TraceVoxelTerrain(UWorld*, FVector Start, FVector End)` → `FHitResult`
  - Line trace against voxel collision channel
- `WorldToVoxelCoord(FVector)` → `FIntVector`
  - Utility coordinate conversion using VoxelWorlds' VoxelCoordinates
- `GetVoxelMaterialAtLocation(UWorld*, FVector)` → `EVoxelMaterial`
  - Direct voxel data lookup

### 5.2 — Wire Voxel Queries into Movement Component

Update `UVCMovementComponent`:

- `UpdateVoxelTerrainContext()` — call `UVCVoxelNavigationHelper::QueryTerrainContext()` with real voxel world data
- Apply surface type to friction: `GetGroundFriction()` returns modified value based on `CurrentSurfaceType`
- Subscribe to `OnChunkEdited` delegate from VoxelChunkManager for cache invalidation
- Implement water detection → transition to swimming mode when `bIsUnderwater && WaterDepth > threshold`
- Climbing mode detection — check for vertical voxel surface adjacent to character

### 5.3 — Wire Voxel Camera Collision

Update `UVCCameraManager::ResolveVoxelCameraCollision()`:

- Sphere trace from pivot to desired camera location against voxel collision channel
- On hit, pull camera to hit location + probe size offset toward pivot
- Uses `CameraCollisionProbeSize` for trace radius

### 5.4 — Wire Block Trace and Modification

Update `AVCCharacterBase`:

- `TraceForVoxel()` — real implementation using voxel collision channel
- `Input_PrimaryAction()` — tool-based block destruction: trace → server RPC
- `Input_SecondaryAction()` — block placement: trace → adjacent position → server RPC

Update `AVCPlayerController`:

- `Server_RequestVoxelModification_Implementation()`:
  - Validate: distance from character, permissions, tool requirements
  - Call `VoxelEditManager` API (from VoxelCore) to apply edit
  - Modification type routing: Destroy → Subtract edit, Place → Add edit, Paint → material-only edit

### 5.5 — Footstep Surface Detection

Update `UVCAnimInstance`:

- `SurfaceType` property read from movement component's `CurrentSurfaceType`
- AnimBP can use this to select footstep sound/particle via Anim Notifies

### Gate 5 Compile Check
- Character movement adjusts friction on different voxel surfaces
- Swimming triggers when entering voxel water
- TP camera pulls forward when terrain is between camera and character
- LMB destroys voxel blocks through server RPC
- RMB places voxel blocks through server RPC
- Footstep type changes based on terrain material

**Files created/modified this gate:**
| # | File | Action |
|---|------|--------|
| 1 | `Public/Movement/VCVoxelNavigationHelper.h` | Create |
| 2 | `Private/Movement/VCVoxelNavigationHelper.cpp` | Create |
| 3 | `Private/Movement/VCMovementComponent.cpp` | Modify — real voxel queries |
| 4 | `Private/Camera/VCCameraManager.cpp` | Modify — voxel collision |
| 5 | `Private/Core/VCCharacterBase.cpp` | Modify — block trace/modify |
| 6 | `Private/Core/VCPlayerController.cpp` | Modify — server RPC impl |

---

## Content Assets (Created alongside or after Gates)

These are Unreal assets created in the Content Browser, not C++ files:

| Asset | Type | When |
|-------|------|------|
| `Content/Input/IA_Move` | InputAction (Axis2D) | Gate 1 |
| `Content/Input/IA_Look` | InputAction (Axis2D) | Gate 1 |
| `Content/Input/IA_Jump` | InputAction (Digital) | Gate 1 |
| `Content/Input/IA_Interact` | InputAction (Digital) | Gate 1 |
| `Content/Input/IA_PrimaryAction` | InputAction (Digital) | Gate 1 |
| `Content/Input/IA_SecondaryAction` | InputAction (Digital) | Gate 1 |
| `Content/Input/IA_ToggleView` | InputAction (Digital) | Gate 1 |
| `Content/Input/IA_OpenInventory` | InputAction (Digital) | Gate 1 |
| `Content/Input/IA_HotbarSlot` | InputAction (Axis1D/Digital) | Gate 1 |
| `Content/Input/IA_ScrollHotbar` | InputAction (Axis1D) | Gate 1 |
| `Content/Input/IMC_Gameplay` | InputMappingContext | Gate 1 |
| `Content/Input/IMC_UI` | InputMappingContext | Gate 4 |
| `Content/Input/DA_DefaultInputConfig` | UVCInputConfig DataAsset | Gate 3 |

**Note:** Input assets can be created as C++ defaults in the DataAsset, or manually in the editor. Recommend creating them in-editor for designer iteration. The DataAsset references them via `TSoftObjectPtr` or `TObjectPtr`.

---

## Implementation Order Summary

```
Gate 1 (7 files)     ──► Compile ──► Gate 2 (10 files)    ──► Compile
                                                                  │
Gate 3 (10 files)    ◄────────────────────────────────────────────┘
       │
       ▼
   Compile ──► Gate 4 (8 files)     ──► Compile ──► Gate 5 (6 files) ──► Compile
```

**Total C++ files: ~41** (21 headers + 20 implementations)

---

## Risk Areas & Design Decisions to Resolve During Implementation

### 1. Camera Component Ownership
The design doc describes `UVCCameraManager` computing transforms but doesn't specify the actual `UCameraComponent`. **Decision needed:** Add a `UCameraComponent` to `AVCCharacterBase` and have CameraManager set its transform each tick. The FP arms mesh should be attached to the camera component so it moves with the view.

**Recommendation:** UCameraComponent on the character, driven by CameraManager. This is the simplest approach and works with standard UE player camera management.

### 2. Optional Plugin Detection in Build.cs
The `.claude/instructions.md` requires the plugin to compile without optional gameplay plugins. This needs either:
- Directory existence checks in `Build.cs` with `PublicDefinitions` macros
- Or a single `Build.cs` that always includes the deps (fails to compile without them)

**Recommendation:** Start with all deps present (hard deps). Add conditional compilation in Gate 4 using directory checks. This avoids blocking early development on build system complexity.

### 3. Interaction Component Type
The design references `UInteractionScannerComponent` from InteractionPlugin but the actual plugin has `UInteractionComponent`. The character should use the actual class name from InteractionPlugin.

**Resolution:** Use `UInteractionComponent` (the actual class from InteractionPlugin). Update any design doc references.

### 4. Equipment Component Type
Similarly, the actual plugin has `UEquipmentManagerComponent`. The design doc matches this — no conflict.

### 5. Camera Mode as UObject
Camera modes extend `UObject` (not `UActorComponent`). This means they need explicit `NewObject<>()` creation and manual lifecycle. They can be Outer'd to the CameraManager. This is fine for lightweight objects but requires manual cleanup in `BeginDestroy` or when popping modes.

### 6. Inventory Location
The design says inventory access through `IInventoryOwnerInterface` which can be on PlayerState or a component. The actual `ItemInventoryPlugin` uses `UInventoryComponent` as an `UActorComponent`. **Decision:** Add `UInventoryComponent` to the character (or PlayerState). The bridge interface wraps access.

**Recommendation:** Inventory component on the character pawn (standard for action games). PlayerState already hosts ASC; adding inventory there too overloads it.

### 7. CombatAttributeSet Scope
The design mentions `UVCCombatAttributeSet` as future. **Decision:** Create the header/class as an empty placeholder in Gate 3 to avoid later restructuring, but don't populate attributes until needed.

---

## Post-Gate Polish Tasks

After all 5 gates compile and work:

1. **Animation Blueprint template** — Create a default AnimBP that reads from `UVCAnimInstance` for basic locomotion
2. **Debug visualization** — Add console commands for voxel terrain context display, camera mode debug, surface type overlay
3. **Network testing** — Verify replication: view mode, voxel modifications, equipment visuals in a listen server
4. **Performance profiling** — Ensure terrain context caching prevents per-frame voxel queries; camera collision trace is efficient
5. **Input asset defaults** — Ship default `IMC_Gameplay` with sensible WASD/mouse bindings as a starter
6. **Editor module** — `VoxelCharacterPluginEditor` for custom detail panels, debug HUD widgets
