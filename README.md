# VoxelCharacterPlugin

A hybrid first/third-person character controller for Unreal Engine 5.7, purpose-built for voxel worlds with integrated inventory, interaction, equipment, and Gameplay Ability System (GAS) support.

## Overview

VoxelCharacterPlugin provides the core player experience layer — the character you move, the camera you see through, and the connective tissue between the player and every gameplay system they touch. It is designed as a modular plugin that integrates with a suite of companion plugins but compiles and runs independently of any of them.

### What This Plugin Does

- **Hybrid FP/TP Camera** — Seamless blending between first-person and third-person views with a stackable camera mode system. Third-person mode includes voxel-aware collision to prevent clipping into terrain.
- **Voxel-Aware Movement** — Extended `CharacterMovementComponent` that queries voxel terrain for surface types, adjusts friction and speed accordingly, and handles edge cases like async mesh rebuilds in marching-cubes engines.
- **Plugin Integration Layer** — Interface-based bridges to ItemInventoryPlugin, InteractionPlugin, and EquipmentPlugin. The character routes input, provides attachment points, and coordinates visual updates without hard-coupling to any system.
- **GAS Foundation** — `AbilitySystemComponent` hosted on `PlayerState` for respawn persistence. Attribute sets, death/respawn flows, and equipment ability granting are architected and scaffolded.
- **Multiplayer-Ready** — Server-authoritative voxel modifications, replicated view modes, standard CMC prediction, and GAS replication through the PlayerState.
- **Enhanced Input** — Full UE5 Enhanced Input setup with prioritized mapping contexts for gameplay, UI, and future systems (vehicles, ability overrides).

## Plugin Dependencies

| Plugin | Required | Purpose |
|--------|----------|---------|
| **CommonGameFramework** | Yes | Shared types, interfaces, enums |
| **VoxelWorlds** | Yes | Voxel terrain queries, collision, block modification |
| **GameplayAbilities** (Engine) | Yes | GAS — ASC, attributes, effects, abilities |
| **EnhancedInput** (Engine) | Yes | Input actions and mapping contexts |
| **ItemInventoryPlugin** | No (soft) | Inventory access via `IInventoryOwnerInterface` |
| **InteractionPlugin** | No (soft) | `UInteractionScannerComponent` on character |
| **EquipmentPlugin** | No (soft) | `UEquipmentManagerComponent` on character |

Soft dependencies are resolved at runtime through interfaces and `FindComponentByClass`. The plugin compiles and the character functions (movement, camera, input) without them.

## Architecture

```
AVCPlayerController
  └── possesses ──► AVCCharacterBase
                      ├── UVCMovementComponent        (voxel-aware movement)
                      ├── UVCCameraManager             (FP/TP mode stack)
                      ├── USkeletalMeshComponent        (third-person body)
                      ├── USkeletalMeshComponent        (first-person arms)
                      ├── UInteractionScannerComponent  (from InteractionPlugin, optional)
                      ├── UEquipmentManagerComponent    (from EquipmentPlugin, optional)
                      └── IAbilitySystemInterface       (passthrough to PlayerState)

AVCPlayerState
  ├── UAbilitySystemComponent       (survives respawn)
  ├── UVCCharacterAttributeSet      (health, stamina, speed, mining)
  └── UVCCombatAttributeSet         (future: damage, defense)
```

### Why ASC on PlayerState?

The game uses respawn-based death rather than save/reload. Hosting the ASC on PlayerState means:

- Health, stamina, and other attributes reset via `GameplayEffect`, not reconstruction
- Active cooldowns persist through death
- Persistent effects (curses, quest buffs) survive without special serialization
- Temporary buffs are selectively cleansed using `DeathCleanseTags`

The character is the **Avatar** (re-bound each respawn). The PlayerState is the **Owner** (stable for the session).

## Core Systems

### Camera

The `UVCCameraManager` maintains a mode stack supporting smooth blended transitions. Two modes ship by default:

- **First-Person** — Locked to head socket with configurable eye offset. Higher FOV (100°). Only FP arms mesh and held items are visible.
- **Third-Person** — Over-shoulder spring arm behavior with camera lag. Voxel collision traces prevent terrain clipping. Full body and all equipment slots visible.

Switching view mode is a coordinated operation that updates camera, mesh visibility, equipment attachment targets, interaction scanner profiles, animation state, and replication in a defined sequence.

### Movement

`UVCMovementComponent` extends `UCharacterMovementComponent` with:

- **Terrain context caching** — Voxel material queries are cached and refreshed at configurable intervals (default 100ms) or immediately on chunk modification events.
- **Surface-driven parameters** — `EVoxelSurfaceType` (ice, mud, sand, stone, etc.) drives ground friction, speed multipliers, and footstep sound selection.
- **Custom floor finding** — Handles transitional states during async voxel mesh rebuilds to prevent grounded characters from briefly entering falling state.
- **Custom movement modes** — Climbing (vertical voxel surfaces), swimming (voxel water detection with depth tracking).

### Input

All input uses UE5 Enhanced Input via `UVCInputConfig` (a `UDataAsset`). Mapping contexts are layered by priority — gameplay at the base, UI overlay when menus are open, with slots reserved for vehicles and ability overrides.

Primary and secondary actions (LMB/RMB) route through a priority chain: GAS ability activation → equipped item action → bare-hands fallback.

### GAS Integration

Scaffolded and architecturally complete, with attribute sets defined and death/respawn flows mapped. Key attributes:

| Attribute | Driven By | Consumed By |
|-----------|-----------|-------------|
| Health / MaxHealth | Combat GEs, environmental damage | Death trigger, UI |
| Stamina / MaxStamina | Sprinting, abilities | Movement component, ability costs |
| MoveSpeedMultiplier | Encumbrance, buffs/debuffs, surface | Movement component `MaxWalkSpeed` |
| MiningSpeed | Tool bonuses, buffs | Voxel block break time |
| InteractionRange | Equipment, buffs | Interaction scanner reach |

Equipment items grant and revoke `GameplayAbility` sets dynamically. On respawn, equipment abilities are re-granted to the new avatar.

## View Mode Switching

Changing between first and third person touches multiple systems. The full sequence:

1. Push new camera mode (blend over `ModeTransitionBlendTime`)
2. Swap mesh visibility (FP: shadow-only body + visible arms, TP: visible body + hidden arms)
3. Re-attach equipment visuals to correct skeleton (FP arms sockets vs TP body sockets)
4. Switch interaction scanner profile (trace origin, distance, cone angle differ per mode)
5. Update `UVCAnimInstance` view mode property
6. Replicate `EVCViewMode` to other clients
7. Broadcast `OnViewModeChanged` delegate for external listeners

## Multiplayer

### Authority Model

| System | Authority | Client Behavior |
|--------|-----------|-----------------|
| Movement | Server (CMC) | Client prediction with server reconciliation |
| Voxel modification | Server | Client shows visual feedback, waits for confirmation |
| Inventory operations | Server | Replicated state from ItemInventoryPlugin |
| Equipment changes | Server | Replicated state from EquipmentPlugin |
| GAS attributes | Server (ASC) | Prediction keys for ability activation |
| Camera | Local only | Never replicated |
| View mode | Server | Replicated to all clients for mesh visibility |

### Voxel Modification Flow

Clients never modify voxel data directly. All modifications route through `Server_RequestVoxelModification` on the player controller. The server validates the request (range check, permissions, tool requirements) before applying. Clients can display predicted visual feedback (block crack overlays, particles) that reconciles when the server response arrives.

## File Structure

```
VoxelCharacterPlugin/
├── Source/
│   ├── VoxelCharacterPlugin/
│   │   ├── Public/
│   │   │   ├── Core/            # Character, Controller, PlayerState, AnimInstance, AttributeSets
│   │   │   ├── Camera/          # CameraManager, CameraModeBase, FP/TP camera modes
│   │   │   ├── Movement/        # MovementComponent, VoxelNavigationHelper, movement modes
│   │   │   ├── Integration/     # Interface bridges (Inventory, Interaction, Equipment, Ability)
│   │   │   └── Input/           # InputConfig DataAsset, input action references
│   │   └── Private/             # Implementation (mirrors Public/)
│   └── VoxelCharacterPluginEditor/
│       └── ...                  # Editor utilities, debug visualizers
├── Content/
│   ├── Input/                   # IA_*, IMC_* assets
│   └── Animation/               # ABP templates
├── .claude/
│   └── instructions.md          # Claude Code development instructions
├── VoxelCharacterPlugin.uplugin
└── README.md
```

## Compatibility

This plugin is versioned in lockstep with its companion plugins using Git tags. Compatible versions:

| VoxelCharacterPlugin | CommonGameFramework | ItemInventoryPlugin | InteractionPlugin | EquipmentPlugin |
|----------------------|--------------------|--------------------|-------------------|-----------------|
| 0.1.x | 0.1.x | 0.1.x | 0.1.x | 0.1.x |

When integrating via Git submodules, ensure all plugins share the same minor version tag.

## Getting Started

1. Clone into your project's `Plugins/` directory
2. Ensure CommonGameFramework and VoxelWorlds plugins are present
3. Enable `GameplayAbilities` and `EnhancedInput` engine plugins in your `.uproject`
4. Create a `UVCInputConfig` DataAsset and assign your input actions
5. Set your GameMode to use `AVCPlayerController`, `AVCCharacterBase` (or subclass), and `AVCPlayerState`
6. Subclass `AVCCharacterBase` in Blueprint to assign meshes, socket mappings, and default ability sets

Optional gameplay plugins (ItemInventoryPlugin, InteractionPlugin, EquipmentPlugin) are detected at runtime. Add them to `Plugins/` and their components will auto-integrate on the character.
