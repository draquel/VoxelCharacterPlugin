# VoxelCharacterPlugin

> State as of 2026-11-02 (code review CR5).

The player and NPC layer for the voxel worlds of this project, for Unreal Engine 5.8: a first/third-person character
on voxel terrain (heightmap worlds, cubic block worlds and planets, with gravity toward a planet's centre), its
controller, player state and game mode, combat and attributes on the Gameplay Ability System (the player's ability
system on the player state, so a respawn keeps it), NPCs with their own AI, digging, placing, chopping, resting and
crafting, and the HUD, minimap and world map. It depends on CommonGameFramework, VoxelWorlds, GameplayAbilities and
EnhancedInput; ItemInventoryPlugin, InteractionPlugin and EquipmentPlugin are used when present and optional at compile
time. Every gameplay change is server-authoritative.

- [Documentation/CHARACTER_SYSTEM.md](Documentation/CHARACTER_SYSTEM.md) — how it works now: the class map, GAS,
  movement and the gravity frame, camera, input, editing, spawning, NPCs, the HUD, replication
- [Documentation/MAP_UI_WIDGETS.md](Documentation/MAP_UI_WIDGETS.md) — the minimap and the world map
- [.claude/instructions.md](.claude/instructions.md) — the rules for changing the plugin
- `Documentation/EDITOR_CONSOLE_GUIDE.md` at the project root — the `vc.*` console variables
- History: [CharacterController_Design.md](Documentation/CharacterController_Design.md) (the February design),
  [COMBAT_FOUNDATION_PLAN.md](Documentation/COMBAT_FOUNDATION_PLAN.md),
  [EQUIPMENT_LOOP_PLAN.md](Documentation/EQUIPMENT_LOOP_PLAN.md),
  [Archive/ImplementationPlan.md](Documentation/Archive/ImplementationPlan.md)

To use it: enable the plugin with CommonGameFramework, VoxelWorlds, GameplayAbilities and EnhancedInput; make a
`UVCInputConfig` data asset with the input actions and the `IMC_Gameplay` / `IMC_UI` contexts and set it as the
controller's `InputConfig`; set the game mode to `AVCGameModeBase` (or a subclass) with `AVCPlayerController`,
`AVCPlayerState` and a Blueprint of `AVCCharacterBase` that assigns the meshes and socket mappings.
