# VoxelCharacterPlugin — Claude Code Instructions

## Plugin Identity

**Name:** VoxelCharacterPlugin
**Purpose:** Hybrid first/third-person character controller for voxel worlds with integrated inventory, interaction, equipment, and GAS support.
**Engine:** Unreal Engine 5.7
**Module:** `VoxelCharacterPlugin` (Runtime), `VoxelCharacterPluginEditor` (Editor/Dev)

## Architecture Overview

This plugin provides the player character, controller, player state, camera system, and movement component for a multiplayer voxel-based game. It does NOT own gameplay systems — it integrates with them through interfaces and component references.

### Ownership Boundaries

| This plugin OWNS | This plugin USES (does not own) |
|---|---|
| `AVCCharacterBase` | `UInventoryContainerComponent` (ItemInventoryPlugin) |
| `AVCPlayerController` | `UInteractionScannerComponent` (InteractionPlugin) |
| `AVCPlayerState` (+ ASC host) | `UEquipmentManagerComponent` (EquipmentPlugin) |
| `UVCMovementComponent` | VoxelWorlds terrain queries |
| `UVCCameraManager` + camera modes | `UAbilitySystemComponent` (GAS — owned by PlayerState but GAS is engine) |
| `UVCAnimInstance` | `UGameplayEffect`, `UGameplayAbility` (GAS) |
| `UVCInputConfig` + input actions | CommonGameFramework shared types |
| View mode system | |
| Voxel trace helpers | |

### Critical Rule: No Hard Plugin Dependencies in Headers

Integration with other plugins MUST go through interfaces or forward declarations. Never `#include` a header from ItemInventoryPlugin, InteractionPlugin, or EquipmentPlugin in a public header of this plugin. Use:

- `TScriptInterface<IInventoryOwnerInterface>` — not `UInventoryContainerComponent*`
- Forward declarations + cpp-only includes for component setup
- `UPROPERTY()` component pointers typed to the base class or interface when possible
- Runtime resolution via `FindComponentByClass` or interface casts

This ensures VoxelCharacterPlugin compiles even if other gameplay plugins are absent (graceful degradation).

## Code Conventions

### Prefix

All classes in this plugin use the `VC` prefix:
- `AVCCharacterBase`, `AVCPlayerController`, `AVCPlayerState`
- `UVCMovementComponent`, `UVCCameraManager`
- `UVCAnimInstance`, `UVCInputConfig`
- `EVCViewMode`, `FVCEquipmentSocketMapping`

### File Organization

```
Source/VoxelCharacterPlugin/
├── Public/
│   ├── Core/           # Character, Controller, PlayerState, AnimInstance, AttributeSets
│   ├── Camera/         # CameraManager, CameraModeBase, FP/TP modes
│   ├── Movement/       # MovementComponent, VoxelNavigationHelper, custom modes
│   ├── Integration/    # Interface bridges to other plugins
│   └── Input/          # InputConfig DataAsset, input action references
├── Private/
│   └── (mirrors Public/)
```

### Naming Rules

- Input Actions: `IA_Verb` (e.g., `IA_Move`, `IA_Interact`, `IA_ToggleView`)
- Input Mapping Contexts: `IMC_Context` (e.g., `IMC_Gameplay`, `IMC_UI`)
- Enums: `EVCName` (e.g., `EVCViewMode`, `EVoxelSurfaceType`)
- Structs: `FVCName` or `FVoxelName` (e.g., `FVCEquipmentSocketMapping`, `FVoxelTerrainContext`)
- Delegates: `FOnVCEvent` (e.g., `FOnVCViewModeChanged`)
- DataAssets: `UVC{Name}Config` or `UVC{Name}Data`

### C++ Standards

- Use `TObjectPtr<>` for all `UPROPERTY` object pointers
- Use `GENERATED_BODY()`, never `GENERATED_UCLASS_BODY()`
- All public API functions must have `UFUNCTION()` with appropriate specifiers
- Use `Category = "VoxelCharacter|Subsystem"` for UPROPERTY/UFUNCTION categories
- Replicated properties use `ReplicatedUsing = OnRep_PropertyName`
- Server RPCs: `UFUNCTION(Server, Reliable)` prefixed `Server_`
- Client RPCs: `UFUNCTION(Client, Reliable)` prefixed `Client_`
- Multicast RPCs: `UFUNCTION(NetMulticast, Unreliable)` prefixed `Multicast_`

## GAS Architecture

### ASC Lives on PlayerState

The `UAbilitySystemComponent` and all `UAttributeSet` subclasses are owned by `AVCPlayerState`. This is a firm architectural decision — do NOT move the ASC to the character.

**Rationale:** Respawn-based death mechanic. Attributes, cooldowns, and persistent gameplay effects survive character destruction.

### Key Relationships

- **Owner Actor:** `AVCPlayerState` (stable, never destroyed during gameplay)
- **Avatar Actor:** `AVCCharacterBase` (destroyed on death, re-bound on respawn)
- `InitAbilityActorInfo(PlayerState, Character)` called in both `PossessedBy` (server) and `OnRep_PlayerState` (client)

### Attribute Sets

- `UVCCharacterAttributeSet` — Health, MaxHealth, Stamina, MaxStamina, MoveSpeedMultiplier, MiningSpeed, InteractionRange, IncomingDamage (meta)
- `UVCCombatAttributeSet` — (future) Damage, Defense, CritChance, CritMultiplier

### Death/Respawn Contract

1. Health reaches 0 → death triggered via `PostGameplayEffectExecute`
2. GEs tagged with `DeathCleanseTags` are removed (temporary buffs)
3. Persistent effects (curses, quest buffs) are NOT removed
4. Character destroyed, new character spawned and possessed
5. `PossessedBy` re-binds ASC avatar to new character
6. `RespawnResetEffect` applied to reset vitals
7. Equipment abilities re-granted on new avatar

### Rules for GAS Code

- Never cache `GetAbilitySystemComponent()` results on the character across frames — the ASC pointer itself is stable but always access through the getter for safety
- Attribute change delegates must be re-bound in `PossessedBy`/`OnRep_PlayerState` since they target the character's components
- Equipment-granted abilities are dynamic: granted on equip, removed on unequip, re-granted on respawn
- Use `IncomingDamage` as a meta attribute — apply damage through it in `PostGameplayEffectExecute`, never modify Health directly from gameplay code

## Camera System

### Camera Mode Stack

`UVCCameraManager` maintains a stack of `UVCCameraModeBase` instances with blend weights. Modes are pushed/popped, and the manager blends between the top two modes during transitions.

### View Mode Switching Rules

Changing view mode is NOT just a camera change. `SetViewMode()` must trigger ALL of these in order:

1. Push new camera mode onto stack
2. Update mesh visibility (FP: hide body/show arms, TP: show body/hide arms)
3. Re-attach all equipment visuals to correct mesh (FP arms vs TP body)
4. Update interaction scanner profile (different trace params per mode)
5. Update animation instance view mode property
6. Replicate to other clients (they need correct body mesh visibility)
7. Broadcast `OnViewModeChanged` delegate

If you add a new system that cares about view mode, add it to this sequence.

### Voxel Camera Collision

Third-person camera must trace against voxel geometry to prevent clipping into terrain. Use `ResolveVoxelCameraCollision()` in the camera manager. First-person mode does not need this.

## Movement Component

### Voxel Terrain Context

`UVCMovementComponent` caches voxel terrain data in `FVoxelTerrainContext` to avoid per-frame voxel queries. Cache rules:

- Re-query every `TerrainCacheDuration` seconds (default 100ms)
- Invalidate immediately when `OnVoxelChunkModified` fires for the character's current chunk
- `EVoxelSurfaceType` drives friction multiplier, footstep sounds, and movement speed modifiers

### Custom Floor Finding

If VoxelWorlds uses async mesh generation (marching cubes), the standard `FindFloor` may fail during mesh rebuilds. The override must handle transitional states gracefully — if no valid floor is found and the character was grounded last frame, maintain grounded state briefly to avoid jitter.

### Movement Modes

- Walking: standard, modified by voxel surface friction
- Swimming: triggered by voxel water detection, speed scaled by `VoxelSwimmingSpeedMultiplier`
- Climbing: custom mode for vertical voxel surfaces (ladders, cliff faces)
- Falling: standard, no voxel modifications

## Input System

### Enhanced Input Only

All input uses UE5 Enhanced Input. No legacy `BindAction`/`BindAxis` calls.

### Input Mapping Context Priority

| Priority | Context | When Active |
|---|---|---|
| 0 | `IMC_Gameplay` | Always (base layer) |
| 1 | `IMC_UI` | Inventory/menu open (adds cursor, blocks gameplay) |
| 2 | `IMC_Vehicle` | In vehicle (future) |
| 10 | `IMC_Ability` | During GAS ability activation that overrides input (future) |

### Input → Action Routing

Primary/Secondary actions (LMB/RMB) route through a priority chain:

1. GAS ability activation (if an ability is bound and conditions met)
2. Equipped item action (tool mining, weapon attack, block placement)
3. Bare-hands fallback (punch/default)

Do not bypass this chain. New action types insert into the chain, they don't replace it.

## Terrain Spawn System

### FindSpawnablePosition

`FVCVoxelNavigationHelper::FindSpawnablePosition()` uses `IVoxelWorldMode::GetTerrainHeightAt()` to deterministically query terrain height from noise parameters — no loaded chunks required. Called at the top of `InitiateChunkBasedWait()` to correct the spawn position before chunk collision is requested.

- If position is above water (or water disabled): returns terrain height at that X,Y
- If underwater: spirals outward at `ChunkWorldSize` intervals in 8 directions until above-water terrain is found
- Returns false if no land within `MaxSearchRadius` (default 50,000 units)

**Critical**: The character must be placed AT the terrain surface height (not above it) so the chunk Z coordinate calculation picks the correct terrain-level chunks. Movement and collision are disabled during the wait, so the character won't fall.

### Capsule Overlap Gate for Block Placement

`Server_RequestVoxelModification_Implementation` rejects `Place` operations where the voxel's AABB overlaps the character's capsule bounding box. This prevents:
- Placing blocks inside the character's feet position
- The surface mesh topology absorbing the block the character stands on
- The character falling through the world due to collision surface removal

The check uses an AABB-vs-AABB test (voxel box against capsule bounding box) — slightly conservative at capsule corners, which is the safer direction.

## Multiplayer Rules

### Server Authority

- All voxel modifications go through `Server_RequestVoxelModification` RPC — never modify voxels client-side authoritatively
- Inventory operations are server-authoritative (via ItemInventoryPlugin)
- Equipment changes are server-authoritative (via EquipmentPlugin)
- GAS attribute changes are server-authoritative (via ASC)

### Client Prediction

- Movement: standard CharacterMovementComponent prediction
- Voxel block breaking: client can show visual feedback (cracks, particles) but actual removal waits for server confirmation
- GAS: use prediction keys for ability activation, server reconciles

### Replication Checklist

| Data | Replicated? | Method |
|---|---|---|
| View Mode | Yes | `ReplicatedUsing` on character |
| Movement | Yes | CMC built-in prediction |
| Equipment State | Yes | Via EquipmentPlugin |
| Inventory | Yes | Via ItemInventoryPlugin (on PlayerState) |
| GAS Attributes | Yes | Via ASC on PlayerState |
| Camera State | No | Local only, never replicated |
| Input State | No | Local only |

## UI Management (AVCPlayerController)

### Widget Ownership

`AVCPlayerController` owns and manages all player-facing UI widgets. It creates them, positions them in the viewport, and handles input flow. The widget classes themselves live in their respective gameplay plugins (e.g., `UHotbarWidget` in ItemInventoryPlugin), but the controller is the orchestrator.

**Persistent widgets** (created in `CreatePersistentWidgets()` during `BeginPlay`):
- `HotbarWidget` — always visible at bottom-center, `HitTestInvisible` by default
- `InteractionPromptWidget` — shown/hidden by interaction scanner callbacks

**Toggle widgets** (lazy-created in `ShowInventoryPanels()`):
- `InventoryPanelWidget` — grid of slots [9, MaxSlots), removed from viewport on close
- `EquipmentPanelWidget` — equipment slots, removed from viewport on close
- `ItemCursorWidget` — floating cursor icon, lazy-created on first item grab, Z-order 100

All widget class references are `TSubclassOf<UUserWidget>` properties on the controller. If not set, they default to the C++ base class via `StaticClass()`. This allows Blueprint skinning without code changes.

### Click-to-Move State Machine

The controller manages a state machine for item rearrangement across inventory and equipment slots. The held source tracks what type of slot the player grabbed from:

```
EVCHeldSource::None       →  Nothing held
EVCHeldSource::Inventory  →  Item grabbed from an inventory slot (HeldSlotIndex, HeldInventory)
EVCHeldSource::Equipment  →  Item grabbed from an equipment slot (HeldEquipmentSlotTag, HeldEquipmentManager)
```

**Interaction matrix:**

| Source | Target | Operation |
|--------|--------|-----------|
| Inventory → Inventory | `TrySwapSlots(sourceSlot, targetSlot)` |
| Inventory → Equipment | `EquipMgr->TryEquipFromInventory(itemGuid, inventory, slotTag)` |
| Equipment → Inventory | `EquipMgr->TryUnequipToInventory(slotTag, inventory)` + `TrySwapSlots` to clicked slot |
| Equipment → Equipment | Cancel (not supported) |
| Any → Same slot | Cancel |
| Any → Right-click | Cancel |

The unequip flow (equipment → inventory) first unequips to the first available inventory slot via `TryUnequipToInventory`, then finds where the item landed by `FindSlotIndexByInstanceId` and swaps it to the player's clicked slot. This gives precise placement control.

**Delegate binding:** `BindSlotClickDelegates()` is called once from `ShowInventoryPanels()` **after both inventory and equipment panels are created**. It binds:
- Hotbar + inventory panel `OnSlotClicked`/`OnSlotRightClicked` → `OnSlotClickedFromUI`/`OnSlotRightClickedFromUI`
- Equipment panel `OnSlotClicked`/`OnSlotRightClicked` → `OnEquipmentSlotClickedFromUI`/`OnEquipmentSlotRightClickedFromUI`

A `bSlotDelegatesBound` guard prevents double-binding since widget objects persist across open/close cycles. **Critical:** The bind call must come after both `#if WITH_INVENTORY_PLUGIN` and `#if WITH_EQUIPMENT_PLUGIN` blocks so all panels exist.

**Slot routing:** `SetSlotHeldVisual(int32, bool)` routes inventory slots to the correct container: `< 9` → hotbar, `>= 9` → panel. `SetEquipmentSlotHeldVisual(FGameplayTag, bool)` routes equipment slots to the equipment panel's `SetSlotHeld()`.

**Cursor icon:** `ShowItemCursor(int32, UInventoryComponent*)` resolves icons from inventory items. `ShowItemCursorForEquipment(FGameplayTag, UEquipmentManagerComponent*)` resolves icons from equipped items. Both use the `ItemDatabaseSubsystem` to look up the `UItemDefinition::Icon`.

**Cancel/cleanup:** `CancelHeldState()` checks `HeldSourceType` to clear the correct visual (inventory or equipment), hides the cursor, and resets all held fields. `HideInventoryPanels()` calls `CancelHeldState()` before reverting the hotbar to `HitTestInvisible`.

### Conditional Compilation

All inventory/interaction/equipment UI code is wrapped in `#if WITH_INVENTORY_PLUGIN` / `#if WITH_INTERACTION_PLUGIN` / `#if WITH_EQUIPMENT_PLUGIN` guards. The controller compiles and functions (without UI) when gameplay plugins are absent.

Widget class includes go in the `.cpp` inside these guards — never in the header. The header uses forward declarations only: `class UItemCursorWidget;`, `class UInventoryComponent;`.

### Adding New UI Widgets

When adding a new widget that the controller manages:
1. Add `TSubclassOf<UUserWidget>` class property and `TObjectPtr<UUserWidget>` instance in the controller header
2. Forward-declare the widget class in the header; `#include` in the `.cpp` under the correct `WITH_*` guard
3. Create/add to viewport in the appropriate lifecycle method
4. If the widget needs click interaction, follow the delegate relay pattern: child widget → container delegate → controller handler

## Integration Testing Checklist

When modifying this plugin, verify:

- [ ] Plugin compiles with all gameplay plugins present
- [ ] Plugin compiles with all gameplay plugins ABSENT (graceful degradation)
- [ ] View mode switch updates: camera, meshes, equipment visuals, interaction scanner, animation, replication
- [ ] Death → respawn flow: ASC rebinds, attributes reset, equipment re-grants, delegates reconnect
- [ ] Voxel surface type changes propagate to: movement friction, footstep sounds, animation
- [ ] Input routing priority chain works: GAS → equipment → fallback
- [ ] Third-person camera does not clip into voxel terrain
- [ ] Server RPCs validate before applying voxel modifications
- [ ] Hotbar slot change updates: held item visual, FP arms animation, interaction behavior

## File Creation Order (Build Sequence)

When implementing from scratch, create files in this order with compile gates:

### Gate 1: Core Skeleton
1. `VoxelCharacterPlugin.Build.cs` (module dependencies)
2. `VoxelCharacterPlugin.h/.cpp` (module startup)
3. `EVCViewMode.h` (enum, no dependencies)
4. `FVoxelTerrainContext.h` (struct, no dependencies)
5. `UVCInputConfig.h/.cpp` (DataAsset with input action refs)
6. **Compile gate — module loads, types exist**

### Gate 2: Movement & Camera
7. `UVCMovementComponent.h/.cpp` (extend CMC)
8. `UVCCameraModeBase.h/.cpp` (abstract camera mode)
9. `UVCFirstPersonCameraMode.h/.cpp`
10. `UVCThirdPersonCameraMode.h/.cpp`
11. `UVCCameraManager.h/.cpp`
12. **Compile gate — movement and camera work standalone**

### Gate 3: Character & Controller
13. `UVCCharacterAttributeSet.h/.cpp` (GAS attribute set)
14. `AVCPlayerState.h/.cpp` (ASC owner)
15. `AVCCharacterBase.h/.cpp` (assembles all components)
16. `AVCPlayerController.h/.cpp`
17. `UVCAnimInstance.h/.cpp`
18. **Compile gate — character spawns, moves, camera works, GAS initializes**

### Gate 4: Integration Bridges
19. `IVCInventoryBridge.h` (interface)
20. `IVCInteractionBridge.h` (interface)
21. `IVCEquipmentBridge.h` (interface)
22. `IVCAbilityBridge.h` (interface)
23. Wire bridges into `AVCCharacterBase` and `AVCPlayerController`
24. **Compile gate — full plugin compiles with and without gameplay plugins**

### Gate 5: Voxel Integration
25. `UVCVoxelNavigationHelper.h/.cpp`
26. Voxel terrain context queries in movement component
27. Voxel camera collision in camera manager
28. Block trace and modification in character/controller
29. **Compile gate — character moves correctly on voxel terrain**
