# VoxelCharacterPlugin — Claude Code Instructions

> State as of 2026-11-02 (code review CR5). Terms: `Documentation/GLOSSARY.md`.

The player and NPC layer: character, controller, player state, game mode, movement, camera, input, combat on GAS, NPC
AI, surface gameplay, the HUD and map widgets. Unreal Engine 5.8; a runtime module, `VoxelCharacterPlugin`, a second
runtime module, `VoxelCharacterAnimation` (the player's motion matching: PoseSearch stays out of the first, D45), and an
editor module, `VoxelCharacterEditor` (`UVCAnimAuthoringLibrary`: scripting the animation content; the graph node of
`FVCAnimNode_FootBoneIK`).

## Reading order

1. `Documentation/CHARACTER_SYSTEM.md` — the class map, where each area lives (the CR4 file split of
   `AVCCharacterBase` and `AVCPlayerController`), GAS, movement and the gravity frame, camera, input, editing, the
   spawn wait, surface gameplay, NPCs, the HUD, replication. The current reference.
2. `Documentation/MAP_UI_WIDGETS.md` — the minimap and world map, on planes and planets.
3. `Documentation/EDITOR_CONSOLE_GUIDE.md` at the project root, §4 — every `vc.*` cvar and Exec function; the game
   module's `vox.*` commands drive this plugin's pawn for QA.
4. `Documentation/FINDINGS.md` at the project root — open findings in this plugin (none since G75 closed, 2026-11-02).
5. `Documentation/CHARACTER_ANIMATION_PLAN.md` — the active plan (agreed 2026-11-05, D43-D51): animation, IK and
   bodies for the player and the NPCs, MetaHumans among them; its phases and spikes.
6. History only: `Documentation/CharacterController_Design.md` (the February design, frozen),
   `COMBAT_FOUNDATION_PLAN.md`, `EQUIPMENT_LOOP_PLAN.md` and `RANGED_COMBAT_PLAN.md` (frozen; the last is the bow,
   D52-D57, done 2026-11-07, now in `CHARACTER_SYSTEM.md` "The bow"), `Documentation/Archive/ImplementationPlan.md`.

Across plugins: `Plugins/CommonGameFramework/Documentation/ARCHITECTURE.md`; VoxelWorlds'
`Plugins/VoxelWorlds/Documentation/Features/WORLD_MODES.md` for frames, the sea and the surface queries this plugin
calls.

## Boundaries

| This plugin owns | It uses (does not own) |
|---|---|
| `AVCCharacterBase`, `AVCPlayerController`, `AVCPlayerState` (ASC host), `AVCGameModeBase`, `AVCNPCCharacterBase` (own ASC), `AVCNPCAIController` | `UInventoryComponent` (ItemInventoryPlugin), `UInteractionComponent` (InteractionPlugin), `UEquipmentManagerComponent` (EquipmentPlugin) |
| `UVCMovementComponent`, `FVCGravityFrame`, `FVCTerrainQueryHelper` | VoxelWorlds' chunk manager, edit manager, scatter manager and surface navigation |
| `UVCCameraManager` and its modes, `UVCAnimInstance`, `UVCActionMontageComponent` and `UVCActionAnimSet`, `UVCCharacterAppearance` and `UVCAppearanceComponent`, `FVCAnimNode_FootBoneIK`, `UVCInputConfig` | `UAbilitySystemComponent`, `UGameplayEffect`, `UGameplayAbility` (GAS) |
| `UVCCombatComponent` and the combat GAS classes, `UVCBlockInteractionComponent`, `UVCGatherTable` | `ICGFDamageableInterface`, `FCGFDamageContext`, `ICGFInventoryInterface`, `ICGFLightBearerInterface`, `ICGFRestPointInterface`, combat tags (CommonGameFramework) |
| The HUD widgets, the map widgets, `UVCMapMarkerRegistry`, `UVCSettings` | Widget classes from the gameplay plugins (`UHotbarWidget`, `UItemCursorWidget`, ...) |

## Rules

### No hard dependency on the gameplay plugins

- ItemInventoryPlugin, InteractionPlugin and EquipmentPlugin are optional. `VoxelCharacterPlugin.Build.cs` defines
  `WITH_INVENTORY_PLUGIN`, `WITH_INTERACTION_PLUGIN`, `WITH_EQUIPMENT_PLUGIN` (1 or 0) and adds each as a private
  dependency only when present.
- Never include their headers in a public header of this plugin. Forward-declare; include in the `.cpp` under the
  matching `#if WITH_*_PLUGIN`. Members of their types are not `UPROPERTY` (UHT forbids it inside `#if`).
- Cross-plugin contracts go through CommonGameFramework's `ICGF*` interfaces and gameplay tags; resolve at runtime with
  interface casts or `FindComponentByClass`.
- The plugin must compile and run (movement, camera, input, combat) with all three absent.
- WorldWeatherPlugin is optional the same way (`WITH_WORLD_WEATHER`, the weather plan's P4): the movement component
  reads the weather's conditions at the pawn in its `.cpp` only (the wind push, the wet grip, and since P5 the air
  temperature and a wet level); without the plugin the wind is zero, the ground dry and the air 15 C. The character's
  `State.Weather.Wet` / `Cold` (P5) read those cached values, so they need no include of their own.
- VoxelWorlds is a hard dependency, but read its settings by name where a dependency would be upward
  (`voxel.Edit.PlayerInputs` is looked up, not linked).
- MetaHumans are content, not a dependency: an appearance (`UVCCharacterAppearance`) names a MetaHuman's Blueprint as
  its parts actor, and nothing links RigLogic, HairStrands or LiveLink (the project enables those plugins). Which body
  a world gives the player is the project's data (`UVCSettings`' appearances, D43), never code.

### Naming and C++

- Every type carries `VC` after the UE letter (D-CR3): `AVCCharacterBase`, `FVCTerrainContext`, `EVCSurfaceType`,
  `FOnVCViewModeChanged`. `Voxel` after `VC` says what a type is about (`EVCVoxelModificationType`), never that it is
  VoxelWorlds'. Renamed types get `[CoreRedirects]` in `Config/DefaultVoxelCharacterPlugin.ini` (checked by
  `VoxelCharacter.Config.CoreRedirects`).
- Input actions `IA_<Verb>`, mapping contexts `IMC_<Context>`; data assets `UVC<Name>Config` / `UVC<Name>Data`.
- `TObjectPtr<>` for `UPROPERTY` object pointers; `GENERATED_BODY()`; categories `"VoxelCharacter|<Area>"`.
- RPC prefixes: `Server_` (Server, Reliable), `Client_` (Client, Reliable), `Multicast_`. Replicated properties use
  `ReplicatedUsing = OnRep_<Name>`.
- A new piece of a large class goes in the file of its area (`VCCharacterBase_Spawn.cpp`, `_Surface.cpp`,
  `VCPlayerController_Items.cpp`, `_VoxelEdit.cpp`; CHARACTER_SYSTEM.md has the table), not back into the main file.

### GAS

- The player's ASC and attribute sets live on `AVCPlayerState`. Do not move them to the character: respawn keeps
  attributes, cooldowns and lasting effects. NPCs own theirs.
- Resolve an ASC with `UAbilitySystemGlobals::GetAbilitySystemComponentFromActor`, never `FindComponentByClass` on the
  pawn (null for players). Do not cache it across frames; go through the getter.
- Attribute delegates bound to the character's components are bound again in `PossessedBy` / `OnRep_PlayerState`.
- Damage goes through `IncomingDamage` (folded in `PostGameplayEffectExecute`); gameplay code never writes Health.
- Hostility is `UCGFCombatStatics::AreHostileFactions` and nowhere else.
- Equipment-granted abilities are granted on equip, removed on unequip, granted again on respawn.

### Traces at pawns

- Target traces use the Pawn channel from `GetActorEyesViewPoint`, never Visibility (pawn profiles ignore it), and
  sweep a small sphere, not a ray (a ray through a capsule's axis misses it: KC12).

### Movement and the gravity frame

- Anything "up", "horizontal" or yaw-based goes through `FVCGravityFrame` (each helper is the Z-up expression without
  custom gravity, so heightmap worlds are unchanged). Only frame-independent work is done in the frame; never keep a
  gravity-relative yaw across frames (the frame twists near a planet's south pole).
- After placing a pawn on a planet: `UpdatePlanetGravity()` then `SnapUprightToGravity()`.
- NPCs use `UVCMovementComponent` too; do not give a voxel walker the stock component.
- Edits must never trap a pawn: the server refuses a place overlapping any pawn (`WouldPlaceTrapPawn`), and
  `ResolveTerrainEmbedding` is the safety net for everything else. Keep both.

### Input and the action chain

- Enhanced Input only. `IMC_Gameplay` at priority 0, `IMC_UI` at 1 while a panel is open.
- Primary and secondary actions go through one chain (melee target, chop, dig / place). New action types insert into
  the chain; they do not replace or bypass it. Dead or downed characters ignore both.
- Edit mode is local input state and gates unarmed digging and placing; the server validates every request regardless.
- A view-mode-dependent system joins `AVCCharacterBase::SetViewMode`'s sequence.
- The underwater post-process (`UVCUnderwaterPostProcess`) is local-player-only (G133): the character creates it on the
  pawn a local player controller views through and destroys it when that controller goes (`UpdateUnderwaterPostProcess`
  on `BeginPlay` and `NotifyControllerChanged`). A view effect never lives on a remote player's or an NPC's pawn: an
  unbound post-process volume tints every view in its world.

### Server authority

- Every voxel edit goes through `Server_RequestVoxelModification` or the timed break (`Server_BeginBlockBreak`); every
  inventory, equipment, rest and crafting change through the owning plugin's server path. Clients predict visuals only
  (cracks, the ghost).
- Voxel edits are not replicated to clients (a VoxelWorlds gap); do not build client logic that assumes they are.

### UI

- `AVCPlayerController` creates and places every player widget. A new widget: a `TSubclassOf<UUserWidget>` class
  property and a `TObjectPtr<UUserWidget>` instance in the controller header, the class forward-declared, its include in
  the `.cpp` under the right `WITH_*` guard, created in the matching lifecycle method; clicks relay child widget ->
  container delegate -> controller handler.
- Widgets are built in C++ (`NativeOnInitialized`), no designer assets.
- Place with anchors, alignment and padding; never `SetPositionInViewport` on a bottom-anchored widget. Check the HUD
  with `Shot showui`; `Shot` and `pie_screenshot` omit UMG.
- `BindSlotClickDelegates` must run after both the inventory and equipment panels exist.

## Before you finish a change

- [ ] Compiles with all gameplay plugins present, and with them absent.
- [ ] The `VoxelCharacter.*` tests pass.
- [ ] Death and respawn: the ASC binds again, attributes reset, items and equipment restored, delegates reconnect.
- [ ] Heightmap behaviour unchanged by planet code (the gravity-frame helpers fall back to Z-up).
- [ ] Server RPCs validate before they change voxels or items.
- [ ] A changed or new `vc.*` cvar is in `Documentation/EDITOR_CONSOLE_GUIDE.md` §4; a changed system in
      `Documentation/CHARACTER_SYSTEM.md`.
