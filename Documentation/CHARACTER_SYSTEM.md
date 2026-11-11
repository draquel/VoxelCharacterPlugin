# Character System

> State as of 2026-11-02 (code review CR5). Terms: `Documentation/GLOSSARY.md`.

VoxelCharacterPlugin is the player and NPC layer of the project: the character, its controller, player state and game
mode, the movement component and camera, input, combat on GAS, NPC AI, surface gameplay (dig, place, chop, rest, craft)
and the HUD and map widgets. It owns no item, interaction or equipment system; it uses ItemInventoryPlugin,
InteractionPlugin and EquipmentPlugin when they are present and compiles without them (`WITH_INVENTORY_PLUGIN`,
`WITH_INTERACTION_PLUGIN`, `WITH_EQUIPMENT_PLUGIN`, set by `VoxelCharacterPlugin.Build.cs`). It depends on
CommonGameFramework, VoxelWorlds, GameplayAbilities and EnhancedInput. A runtime module, `VoxelCharacterPlugin`, and
an editor module, `VoxelCharacterEditor` (scripting the animation content, Animation below); UE 5.8. The rules for changing it are in `.claude/instructions.md`; the February design record is
`CharacterController_Design.md` (frozen).

## Class map

Paths are under `Source/VoxelCharacterPlugin/Public/` (headers) and `Private/` (the same folders).

| Area | Class | Role |
|---|---|---|
| Core | `AVCCharacterBase` | The player pawn: components, view mode, input, the primary-action chain, voxel edit targets, the terrain wait, leaving the world, surface gameplay. Implements `IAbilitySystemInterface` (a pass-through to the player state), `ICGFInventoryInterface`, `ICGFLightBearerInterface` and `IVCAnimatedCharacter` |
| Core | `AVCPlayerController` | HUD and panels, the click-to-move item state machine, the voxel edit server path, gathering, chopping, rest and crafting RPCs, rotation in the gravity frame |
| Core | `AVCPlayerState` | Hosts the ASC and both attribute sets; item snapshot across death, progression, rest point, save state; the player's appearance (replicated; the player's own choice saved) |
| Core | `AVCGameModeBase` | Respawn (`RespawnPolicy`, `RespawnDelay`), the corpse kept per player (`bKeepCorpse`), log-in / log-out events, save requests; the players' appearance on its map (`PlayerAppearance`) |
| Core | `AVCNPCCharacterBase` | NPC pawn with its own ASC and attribute sets, behaviour and temperament, a floating health bar (`UVCNPCHealthBarWidget`), a skeletal body within the animation budget, the item it holds (`HeldItem`) and an animal's locomotion (`LocomotionAnimSet`); implements `IVCAnimatedCharacter` |
| Core | `UVCCharacterAttributeSet` | Health, Stamina, Breath (each with a max), MoveSpeedMultiplier, MiningSpeed, InteractionRange, IncomingDamage (meta) |
| Core | `UVCAnimInstance`, `IVCAnimatedCharacter` | The anim instance of the player and the NPCs, every value in the gravity frame (Animation, below); the interface it reads a character's view mode, action set and combat state through |
| Core | `UVCAnimLayerInstance` | The parent of a stance layer that aims or plays montages of its own (the bow's): the body's montages in the layer's slots, the body's aim and upper-body montage weight copied each update |
| Animation | `UVCActionMontageComponent`, `UVCActionAnimSet`, `UVCAnimNotify_ActionHit` | The action montages of both character classes: the owner's at input, everyone else's by multicast, hit reacts, deaths and get-ups from the combat events; a set's montages per action; the moment an action lands |
| Animation | `UVCCharacterAppearance`, `UVCAppearanceComponent` | A body as data (a mesh, or a MetaHuman's Blueprint whose parts dress it) and the component that dresses both character classes in one (Appearance, below) |
| Animation | `UVCLocomotionAnimInstance`, `UVCLocomotionClipSet` | The player's locomotion as a state machine of our own over the Essential Animation packs (`vc.Anim.Locomotion 2`, the default; the locomotion state machine plan): the anim instance that resolves the held set's clips and computes the rules, and a set's full-body clips (Animation, below) |
| Animation | `UVCLocomotionAnimSet` | An animal's locomotion for the creature template AnimBP: its blend space by speed, the run's own speed, its idle variant (NPCs, below) |
| Animation | `FVCAnimNode_FootBoneIK` (its graph node `UVCAnimGraphNode_FootBoneIK` in `VoxelCharacterEditor`) | Foot placement on the foot bones, for a body without the Mannequin's `ik_foot` bones (a MetaHuman's) |
| Animation | `UVCMotionMatchingAnimInstance`, `UVCGravityTrajectoryPredictor` (`VoxelCharacterAnimation`) | The player's motion matching: the parent of the Game Animation Sample's CMC AnimBP, which fills it from our pawn and does its world-Z arithmetic in the gravity frame; the trajectory predictor that turns about local up (Animation, below) |
| Editor | `UVCAnimAuthoringLibrary` (`VoxelCharacterEditor`) | Anim layers, interfaces, graph nodes, pins and properties, a montage's sections and slot, copies of assets with their references among them, from Python, which the engine does not expose: `Scripts/setup_character_layers.py`, `setup_character_ik.py` and `setup_character_locomotion.py` build the content with it |
| Core | `UVCSettings` | Project Settings > Plugins > Voxel Character (CR4): `NPCHealthBars`, overridden by `vc.UI.NPCHealthBars` once set; the player's action sets, motion-matching AnimBP and appearances |
| Core | `VCTypes.h`, `VCGameplayTags.h` | `EVCViewMode`, `EVCSurfaceType`, `EVCVoxelModificationType`, `EVCOutOfHealthPolicy`, `EVCNPCBehavior`, `EVCNPCTemperament`, `EVCRespawnPolicy`, `FVCTerrainContext`, `FVCEquipmentSocketMapping`, `FVCItemSnapshot`, `FVCProgressionStats`, `FVCPlayerSaveState`; the native tags |
| Movement | `UVCMovementComponent` | Voxel-aware character movement (players and NPCs): terrain context, swimming, sprint, planet gravity, block steps, embed resolve, world-edge push-back, debug flight |
| Movement | `FVCGravityFrame` | Conversions between world space and the gravity frame |
| Movement | `FVCTerrainQueryHelper` | Static terrain queries: `QueryTerrainContext`, `IsPositionUnderwater`, `FindSpawnablePosition` and the pure spawn searches |
| Camera | `UVCCameraManager`, `UVCCameraModeBase`, `UVCFirstPersonCameraMode`, `UVCThirdPersonCameraMode` | The camera mode stack and its blend; third-person collision against voxel terrain; the block-step ease |
| Camera | `UVCUnderwaterPostProcess` | Post-process tint and fog while the local player's camera is in a water voxel; on that player's pawn only |
| Combat | `UVCCombatComponent`, `UVCCombatAttributeSet`, `UVCDamageEffect`, `UVCDamageExecution`, `UVCMeleeAttackAbility`, `UVCCombatStatics`, `UVCEquipmentStatEffect`, `IVCDamageReceiver` | Faction, damage intake, death and downed state; AttackPower and Defense; the damage pipeline; equipment stats |
| Combat | `UVCRangedAttackAbility`, `AVCArrowProjectile`, `UVCArrowMovementComponent` | The bow: the draw timed and the shot taken on the server; the replicated arrow, its hits and sticking; its flight under local gravity (The bow, below) |
| Editing | `UVCBlockInteractionComponent` | Cubic worlds: the block outline, the placement ghost and timed breaking |
| Gathering | `UVCGatherTable` | Voxel material to item, scatter harvest entries, tool yield |
| AI | `AVCNPCAIController`, `IVCPathProvider`, `UVCVoxelPathProvider` | The NPC state machine and its path provider over VoxelWorlds' surface navigation |
| Input | `UVCInputConfig` | Data asset with every input action and the two mapping contexts |
| Map | `UVCMinimapWidget`, `UVCWorldMapWidget`, `UVCMapMarkerRegistry`, `VCMapSamplers.h` | The minimap, the world map, the marker gather; see `MAP_UI_WIDGETS.md` |
| UI | `UVCVitalsWidget`, `UVCPauseMenuWidget`, `UVCCraftingPanelWidget`, `UVCCampsiteWidget`, `UVCNPCHealthBarWidget` | Built in C++ (no designer assets) |
| Testing | `VCTestWorld.h` | Test-world helpers for the `VoxelCharacter.*` automation tests (`Private/Tests/`) |

### Where the two large classes live (CR4)

`AVCCharacterBase` and `AVCPlayerController` are split into files by area, one class each:

| File | Contents |
|---|---|
| `Core/VCCharacterBase.cpp` | Construction, lifecycle, possession and ASC binding, view mode, equipment attachment, input binding and callbacks, the primary-action chain, the debug overlay |
| `Core/VCCharacterBase_Spawn.cpp` | The terrain wait (`FreezeForTerrainWait`, `InitiateChunkBasedWait`, `PlaceOnTerrainAndResume`), world safety (`TickWorldSafety`, `IsOutOfWorld`, `FellOutOfWorld`, `RecoverFromMissingTerrain`, `GetLastSafeLocation`), `FaceRotation` |
| `Core/VCCharacterBase_Surface.cpp` | Placing, the chop aim and chopping (scatter and voxel trees), radial ragdolls, mining speed, rest, sleep and crafting requests |
| `Core/VCPlayerController.cpp` | Input contexts, rotation, persistent widgets, panels, toasts, objective text, the world map and pause menu |
| `Core/VCPlayerController_Items.cpp` | The click-to-move state machine, held visuals, the item cursor |
| `Core/VCPlayerController_VoxelEdit.cpp` | `Server_RequestVoxelModification`, `IsVoxelInEditReach`, `WouldPlaceTrapPawn`, `DigVoxelAt`, gathering, `ChopScatterAt`, `ChopVoxelTreeAt`, the crafting and campsite panels, the client notifications |

## GAS

The player's `UAbilitySystemComponent` and both attribute sets live on `AVCPlayerState` (owner); the character is the
avatar, destroyed on death and bound again on respawn. `InitAbilityActorInfo(PlayerState, Character)` runs in
`PossessedBy` (server) and `OnRep_PlayerState` (client). Attributes, cooldowns and lasting effects survive a respawn.
`AVCNPCCharacterBase` owns its ASC and sets; its death is final. Code that needs an ASC resolves it with
`UAbilitySystemGlobals::GetAbilitySystemComponentFromActor`, which honours `IAbilitySystemInterface`;
`FindComponentByClass` on a player pawn returns null.

### Damage pipeline

```
attacker builds FCGFDamageContext
  -> UVCCombatStatics::ApplyDamageToActor
  -> UVCCombatComponent::ApplyDamage      (authority; rejects dead, invulnerable, friendly, vetoed)
  -> UVCDamageEffect spec                 (SetByCaller.Damage, Damage.Type.* dynamic asset tag)
  -> UVCDamageExecution                   (Pure: Base; else max(0, Base + AttackPower - Defense)) -> IncomingDamage
  -> UVCCharacterAttributeSet::PostGameplayEffectExecute folds IncomingDamage into Health
  -> UVCCombatComponent: OnHealthChanged; at 0 Dead or Downed per OutOfHealthPolicy
```

- `UVCCombatComponent` implements `ICGFDamageableInterface`; attackers find it with `UCGFCombatStatics::FindDamageable`.
  Hostility is `UCGFCombatStatics::AreHostileFactions` and nowhere else (player `Faction.Player`, NPCs `Faction.Monster`
  by default, breakables `Faction.Object`, hostile to everyone).
- An actor without a combat component can take damage through `IVCDamageReceiver` (breakable props).
- `State.Dead` / `State.Downed` are replicated loose tags on the ASC and block ability activation; `bIsDead` /
  `bIsDowned` replicate on the component for client visuals.
- `State.Weather.Wet` / `State.Weather.Cold` (the weather plan's P5) are replicated loose tags too, set by the server
  every half second in `AVCCharacterBase::UpdateWeatherEffects` from the movement component's weather read with
  hysteresis (`WetOnLevel` 0.6 / `WetOffLevel` 0.2 on the wet level; `ColdBelowC` 0 / `ColdHysteresisC` 2 on the air
  temperature) and read back from the ASC, so a respawn clears a stale pair. Wet halves the stamina's regeneration
  (`UVCMovementComponent::WetStaminaRegenMultiplier`); cold applies `UVCWeatherColdEffect`, an infinite source-defined
  effect taking 0.15 off `MoveSpeedMultiplier`, removed by class when the cold lifts. A corpse or a downed character
  loses both. The vitals widget's weather line ("Soaked", "Cold", "Soaked and cold") follows the tags' events; a
  change logs once, and `vc.Weather.Log 1` logs the level and the temperature every half second.
- `UVCMeleeAttackAbility` is server-only; the client requests it by tag `Ability.Attack.Melee`. Damage is the main-hand
  `UItemFragment_Weapon`'s, else `UnarmedDamage`; the crit is rolled on the server from the weapon fragment
  (`Damage.Critical` in the context's tags); a landed hit wears the weapon (`ApplyWeaponWear`), and a weapon at zero
  durability swings at `WornOutDamageMultiplier`.
- Targets are found on the Pawn channel from `GetActorEyesViewPoint` (the engine's Pawn and CharacterMesh profiles
  ignore Visibility) with a sweep, not a ray: `AVCCharacterBase::FindMeleeTargetUnderCrosshair` sweeps a sphere of
  `AttackTargetProbeRadius` (5 uu) out to `AttackTargetRange` (250 uu), since a ray through a capsule's axis misses it
  (KC12); the ability sweeps `MeleeRadius`.
- Equipment stats go through `UVCEquipmentStatEffect` (SetByCaller keys `SetByCaller.Stat.*` in `VCGameplayTags.h`;
  configured under `EquipmentGASSettings` in `Config/DefaultGame.ini`). A new modifiable attribute is one tag and one
  `AddStatModifier` line.
- **The sword and the shield** (G174, G175, D94, 2026-11-11). The demo's swords (`ID_Weapon_WoodenSword`,
  `ID_Weapon_IronSword`, and `ID_Weapon_Longsword`, LowPolyRPGWeapons' `SM_Sword_03`) carry the SwordShield action set;
  the primary action swings them at a target or at the air ("The primary action" below). `Scripts/configure_melee_items.py`
  builds the longsword and `ID_Weapon_Shield` (`SM_Shield_01`, worn on the forearm: `AttachBoneOverride` `lowerarm_l`,
  upright and facing forward in the guard; +3 `Defense` while on the arm; a Shield fragment); both are in
  `CharacterTest`'s kit. The shield lives in the character's `Equipment.Slot.Sheath.ShieldBack` (on `spine_03`, drawn into
  the off hand, `bDrawWhenFree`): on the arm whenever the off hand is free, on the back while the bow or a pickaxe is out
  (their `BlockedSlotTags`; EquipmentPlugin's "Sheaths" › Blocked hands). The weapon cycle and the sheathe key ignore it
  (`GetCarriedWeaponSlots` lists the main hand's sheaths only).
- **Block** (D94). Held with the secondary action while a shield is in the off hand (`UVCBlockAbility::CanStartBlock`):
  the owner raises its guard at once (`StartBlock`: the set's `Block` montage, the pack's `Guard_Begin` then `Guard_Idle`
  looping, in the `UpperBodyAction` slot so the legs keep walking) and asks for `UVCBlockAbility` (Ability.Block,
  ServerOnly, granted through `AVCPlayerState::BlockAbilityClass`), which holds `State.Blocking` while it runs and plays
  the guard for everyone else; the release lowers it (`BlockEnd`, `Guard_End`) through `ReleaseBlock`, as the bow's draw
  is released. On the server `UVCCombatComponent::ApplyDamage` hands every hit to `UVCBlockAbility::ApplyGuard` first: a
  hit from within the shield's arc (`BlockHalfAngle` either side of the facing, about the gravity frame's up) while
  `State.Blocking` is held loses the shield's `BlockDamageReduction` (0.8) and costs `StaminaPerBlockedDamage` stamina per
  point taken; the context carries `Damage.Blocked` and the body plays `BlockHit` (`Guard_Hit_01`, then the hold) instead
  of a hit react. A hit the stamina cannot pay for lands what it could not pay for and breaks the guard (`BreakGuard`):
  the block is cancelled, `State.GuardBroken` (replicated) refuses a new guard for `GuardBreakSeconds` (1.5), and everyone
  sees `GuardBreak` (`Guard_Break`). Pure damage and a hit with no source are not blocked; a body without stamina (an NPC)
  never breaks. While the guard is up the primary action does nothing and stamina does not regenerate. Not done: the
  guard does not slow the walk, NPCs do not block, and the shield's swap has no montage.

### The bow (ranged combat plan, 2026-11-06 to 11-07)

Hold to draw, release to shoot; the server measures the draw, uses the arrow and spawns it (D52-D57,
`RANGED_COMBAT_PLAN.md`, frozen).

- **Holding (D52).** The bow is a main-hand weapon: everything that asks what the player wields (the action set, the
  attack, the wear) reads the main hand. Its Equipment fragment's `AttachBoneOverride` (ItemInventoryPlugin) puts it on
  `hand_l`; EquipmentPlugin attaches it there and `UpdateEquipmentAttachments` keeps that bone in both views. In first
  person the body's hand items are hidden with the body (`UpdateEquipmentVisualVisibility`, `OwnerNoSee` on a visual
  whose parent is the body mesh; a torch's light stays): no arms mesh draws them (D57), so a bow floated at the hidden
  hand (the character refinement plan's C1). A hotbar swap seen from another machine rebuilds the visual (G153,
  EquipmentPlugin). **One grip** (G161, 2026-11-08, after the character refinement plan): the item's `AttachOffset`
  lays the stave (the mesh's Z) along the left fist's hole (`hand_l`'s Z), the grip 8 uu down the hand into the fist
  and the string (the mesh's -X) away from the knuckles: up while the arm hangs at rest, the bow lying through the fist
  from front to back, and toward the archer across the raised fist. The pack's clips hold their bow on a `weapon_l`
  bone that stands still against `hand_l` in every bow clip (the idle, the raise, the draw, the release: (1.2, -1.3,
  0.5) and one rotation at every key), so one fit is right at rest and drawn. C1 had read a quarter turn between the two
  poses from its rest fit (pitch 90), which stood the stave along the forearm with the grip on the wrist bone and the
  string ahead (upright from a distance; close up the stave rose behind the forearm), and added a drawn grip for the
  draw (`UVCActionAnimSet::DrawnGrip`, blended by `AVCCharacterBase::UpdateDrawnGrip`); the mechanism stays for a set
  that needs one, the Bow set has none. Close shots on both bodies at rest and drawn: the grip wrap in the fist, the
  same numbers on Kellan and the low-poly body (no `AttachCorrections`).
- **The data.** `UItemFragment_RangedWeapon` (ItemInventoryPlugin): the ammo's tag (`Item.Ammo.Arrow`), the arrow's
  class, the speed at full draw, the shortest and the full draw times and the power at the shortest (`GetDrawPower`: none
  below it, linear to full). Damage, crits and type stay on the Weapon fragment, times the draw's power.
  `Scripts/configure_ranged_items.py` builds `ID_Weapon_Bow` (LowPolyRPGWeapons' `SM_Bow_01`; 15 damage at full draw,
  6000 uu/s, 0.25 to 1 s, 150 shots of wear) and `ID_Ammo_Arrow` (stacks of 50). Arrows are items, one used a shot
  (D55).
- **The attack (D56).** `UVCRangedAttackAbility` (`Combat/VCRangedAttackAbility.h`), ServerOnly, granted with the melee
  attack (`AVCPlayerState::RangedAttackAbilityClass`), tag `Ability.Attack.Ranged`. Its activation starts the draw on
  the server's clock. The release reaches it as the ability system component's input release (`ReleaseDraw`:
  `ServerSetInputReleased` from a client, on the activation's channel so it cannot overtake it). At the release a draw
  under the shortest is no shot; otherwise one arrow leaves the bag and `SpawnArrow` launches it along the reticle's line
  (Aiming, below) at the bow's speed times the draw's power plus the archer's own velocity, and the bow wears by its
  `DegradeRate`. No ranged weapon, a dead or downed shooter or no arrow at the release: no shot, nothing used. The
  owner's check and the server's are one predicate, `CanStartDraw` (D-CR16).
- **The arrow (D54).** `AVCArrowProjectile` (`Combat/VCArrowProjectile.h`), replicated and spawned by the server only
  (no client prediction), carries an `FVCArrowShot` (damage at full draw, the draw's power, type, crits, the bow). A 4 uu
  sphere sweeps and ignores its shooter. On a pawn or anything with a combat component it applies the hit through the
  damage pipeline (the crit rolled at the hit, the arrow the causer, so the hit react faces it) and goes; on anything else
  it sticks with its collision off for `StuckLifetime` (30 s), riding a movable prop only when every machine knows that
  prop; unhit it goes after `FlightLifetime` (10 s). A shooter gone before the hit: the faction it had at the launch
  decides. `UVCArrowMovementComponent` is the engine's projectile movement with gravity replaced on a planet by the pull
  toward the centre from where the arrow is, read every forced 1/60 s substep; below the generated surface in a chunk with
  no cooked collision it stops at that surface. **The wind** (D83, with WorldWeatherPlugin, `WITH_WORLD_WEATHER`): every
  `WindSampleInterval` (0.1 s) the arrow reads the weather's wind where it is times the exposure there (none under
  shelter); across its launch heading in the local horizontal its speed takes the cross wind's at `WindResponse` (0.4)
  a second, so it drifts downwind and never faster than the wind, while a head or tail wind and the vertical are left
  alone (the bow's speed and the drop stay the bow's and gravity's; `ComputeWindAcceleration`, pure). A full draw drifts
  about 28 uu at 30 m and 63 at 45 m in the weather's strong wind (600 uu/s); measured on the range, 2.8 uu at 10 m, 35 at
  33 m and 62.5 at 44 m in a 637 uu/s cross wind. Test `VoxelCharacter.Combat.Arrow.WindRule`.
- **The look.** The Bow action set and `ABP_VCLayer_Bow` (Animation, below): aiming raises the bow (Aiming, below), the
  draw and the release play over the
  upper body only, the drawn bow follows the view's pitch, and in third person the body turns toward the view while the
  bow is drawn (`SetFacesAim`). First person shows no draw (D57: the demo has no arms mesh); the crosshair, the shot and
  the arrow work the same.
- **Aiming** (the character refinement plan's C3, D73 and D74, 2026-11-08). Holding the secondary action with a ranged
  weapon in the main hand aims, and a draw aims too (`AVCCharacterBase::IsAiming`): the server decides (`bIsAiming`,
  replicated; the ASC's `State.Aiming` tag) from the owner's request (`Server_SetWantsToAim`, checked again there) and
  its own ability's draw (`SetDrawingOnServer`), and the owner counts its own input and draw at once so nothing waits a
  round trip. `ApplyAimState` runs on every change (input, draw, replication, a view toggle: G155) and does three
  things: the body faces the view and strafes (`SetFacesAim`, the movement's eased flip, third person), the motion
  matching plays GASP's aim set (`WantsToAim`), and for the local player in third person the camera moves in
  (`UVCAimCameraMode`, pushed on the stack and popped after: "Camera"). The reticle (`UVCReticleWidget`: "HUD and
  widgets") shows while aiming in both views, its ticks closing with the owner's draw (`GetOwnDrawPower`), red over a
  hostile (`IsAimOnHostile`). **The shot's ray** (G156): `UVCCombatStatics::ComputeAimRay` is one predicate on the owner
  and the server: a character aiming in third person aims from its aim camera, whose place follows from the pawn and
  the view alone (`UVCAimCameraMode::ComputeDefaultCameraLocation`, no lag), so the screen's centre is the ray;
  anyone else aims from the eyes along the view as before. The arrow flies along that ray (D83, below). Edit mode keeps the secondary action for placing (a held bow is no placing tool). Tests
  `VoxelCharacter.Combat.AimCameraLocation`.
  **Raise, draw, release** (D76, 2026-11-08, the user's flow after the character refinement plan): holding the
  secondary action raises the bow, the arm up and the string undrawn (the pack's `Aim_A_Loop`, faded in by
  `UVCAnimInstance::AimRaiseWeight` over `AimRaiseTime`, 0.3 s, from `IVCAnimatedCharacter::IsAnimAiming`); holding the
  primary action then draws from there (`UVCActionMontageComponent::PlayActionLocal` starts the Draw montage at its
  second section, the string, once the raise is past one half) and the release looses; the primary action alone still
  raises and draws in one. Measured on `CharacterTest`, aiming without a draw: the bow arm's pitch -73 degrees (hanging,
  before) to +6 (raised). Test `VoxelCharacter.Anim.AimRaise`.
  **The bow on the view** (G160): the bow layer laid the aim offsets, the draw and the release over the upper body in
  local space from `spine_01` (C2's moving stance), so over the locomotion's forward-facing pelvis. The pack's aim and
  draw turn the pelvis some 70 degrees (its heading in the clips' mesh space 103 in the idle, 144 in the raise, 173 in
  the draw's hold, the root fixed; the arm then 10 degrees right of ahead), and that turn was lost: with the view fixed
  on the 20 m post, the capsule on the view (yaw 0), the offset root at 0 and `AimYaw` 0, the bow arm (upper arm to
  hand) pointed 61 degrees left of the view. None of the suspects named (GASP's offset root, the eased flip's
  tolerance, the aim offsets' axes, the strafe databases) was it: the offset root reaches 6 degrees in a 350 uu/s
  strafe and nothing at a stand. The layer's aim branch (the raised pose, the `UpperBody` slot, the drawn aim offset) is
  now laid over the stance in mesh space from `spine_01` (the same ramp), weighted by `DrawnAimWeight` (the raise or the
  slot's montage, the larger), so the upper body keeps the clips' turn wherever the hips face; the idle's stance stays in
  local space as C2 made it. After: the bow arm 10 degrees right of the view drawn (the clip's own), 17 raised, 4 to 8
  in the strafe; from behind and above, the drawn bow points down the range at the reticle on both bodies.
  **The shot along the reticle's line** (D83, 2026-11-09, the user: a physical shot, no aim assist, an honest view so
  the player learns to hold over for range and off for wind): the arrow starts on the aim ray abreast of where it is
  nocked (`ArrowStartOffset` (75, 4, -8) from the eyes, the drawn bow's grip as measured on both bodies; the start is
  that point's projection onto the ray, `ComputeLaunchStart`) and flies along the ray (`ComputeShot`), at the bow's
  speed times the draw's power plus the archer's own velocity (`ComputeLaunchVelocity`: strafing carries the arrow).
  Nothing the ray meets changes the shot, so the reticle is the launch line at every range: gravity drops the arrow
  below it with distance (more for a weak draw: the power builds from 0.3 at the shortest draw, 0.25 s, to full at one
  second, `UItemFragment_RangedWeapon::GetDrawPower`), the wind pushes it downwind (the arrow, above), and the player
  allows for both. The start is beside the body under the aim camera's shoulder, so a wall between the eyes and it
  starts the arrow at the wall, never through it. The owner and the server compute the same ray (D74) and so the same
  shot; the server logs each one's start, direction and the shooter's velocity. The red reticle over a hostile stays: it
  names what the ray is on, it does not move the shot. In first person the ray is the eyes' and the arrow starts 75 uu
  ahead on it. The visible cost: in third person the arrow appears on the screen's centre line, some 56 uu right of the
  drawn bow, for its first frame (100 uu at 60 fps).
  **G164** (fixed by D83): G159 had the arrow leave the drawn bow's grip straight at what the ray met within 20,000 uu
  (G159's launch direction, now gone), from a grip 56 uu left of the aim camera, so the line bent by what lay behind the reticle.
  On the range, a full draw at the 45 m wall: the reticle on the wall 3.8 m up struck 0.0 uu aside of the reticle's line;
  raised 1 m, past the wall's top into open sky, it struck 43 uu left (the ray met nothing, so the arrow aimed at a point
  200 m out). That was the player's "held over, it lands left; on the deer, it hits the legs" (the legs: the drop).
  Now, in calm air: 0.0 uu aside in all four shots (the reticle on the 10 m post, on the wall at 1.5 and 3.8 m, and
  over its top), 11 uu under the line at 10 m and 264 at 45 m (gravity's 263). Strafing at 360 uu/s the arrow struck
  where the archer's motion carries it (903.8 against 897 predicted, -83 without it). Test
  `VoxelCharacter.Combat.LaunchAlongAimRay`.

### Death and respawn

1. Health reaches 0: `UVCCombatComponent::HandleOutOfHealth` sets `State.Dead` (`OnDied`, `Event.Combat.Died`) or
   `State.Downed` (`OnDowned`; `Revive` recovers, `DownedTimeout` promotes to death).
2. `AVCCharacterBase::HandleDied`: movement off, capsule passable, input ignored, ragdoll when there is a physics asset
   (on a planet pulled toward the centre, `StartRadialRagdoll`), items captured to the player state
   (`CaptureItemSnapshotToPlayerState`), then `AVCGameModeBase::HandlePlayerDied`.
3. After `RespawnDelay` the game mode restarts the controller at `ChooseRespawnTransform` (`AtDeathLocation`,
   `AtPlayerStart` or `AtLastRest`); the old body becomes the player's corpse (`BecomeCorpse`, one per player).
4. The new avatar's `PossessedBy` binds the ASC again, revives a dead or downed state, removes effects tagged
   `DeathCleanseTags` and applies `RespawnResetEffect` (or sets vitals to max), then restores the items by slot and
   equips them again (`RestoreItemSnapshotFromPlayerState`).

A game mode must derive from `AVCGameModeBase`, or no respawn is scheduled (a warning is logged).

## Movement

`UVCMovementComponent` extends the character movement component and is used by players and NPCs alike (the stock
component's `FindFloor` / `IsValidLandingSpot` reject walkable voxel slopes on trimesh edge normals; this one checks them
again with a line trace, and keeps a pawn grounded for `FloorGraceDuration` when its floor disappears during a
re-mesh).

- **Terrain context.** `FVCTerrainContext` (surface type, material, friction, water depth, chunk) is refreshed every
  `TerrainCacheDuration` (0.1 s) and at once when an edit touches the pawn's chunk. `EVCSurfaceType` sets ground
  friction (`GetSurfaceFriction`, times `VoxelSurfaceGripMultiplier`). The context and `IsPositionUnderwater` read the
  voxel with its edits (`UVoxelChunkManager::GetVoxelAt`; G86, 2026-11-02): a dig under the sea floods in the edit layer
  (VoxelWorlds' water propagation), and a pawn in it swims and the camera in it gets the underwater post-process. Test
  `VoxelCharacter.Terrain.FloodedDigIsWater`.
- **Swimming.** Entered at `SwimmingEntryDepth` (40 uu) of voxel water, left under `SwimmingExitDepth`; buoyancy, drag,
  surface damping, a shore climb and dive keys (jump rises, crouch dives; the held rise is a saved-move flag). Breath
  drains under water. `ImmersionDepth` uses the voxel sea, not physics volumes.
- **Sprint and crouch.** `SetSprinting` (a saved-move flag, so clients predict it) spends stamina; regeneration waits
  `StaminaRegenDelay`. With the gait table on (players) the sprint row sets the speed; with it off (NPCs) the sprint is
  the walk times `SprintSpeedMultiplier`. Crouch is a slow walk. `SetWalking` (a third saved-move flag) asks for the walk
  gait; no input is bound to it yet (the plan's §8).
- **The gait table** (the character refinement plan's C4, D75, 2026-11-08). The player plays the Game Animation
  Sample's motion-matching graph, whose clips were made for GASP's per-frame movement tuning, and ran it on the engine's
  defaults (600 uu/s every direction, acceleration 2048, braking 4096, friction 8, a 500 degree a second yaw): the
  capsule outran the clips and the offset root bone dragged the mesh after it, which read as sliding, worst into and out
  of strafing. `FVCMovementTable GaitTable` holds a `FVCGaitSettings` row per gait (`EVCGait`: walk, run, sprint,
  crouch: the speed caps forward / strafe / back, the acceleration, the braking with input and stopping, the friction,
  the braking friction factor), the yaw rates by mode (ground orient, ground strafe, air, swim; negative = instant) and
  the sprint's ramps (acceleration 800 to 300 over 300-700 uu/s, friction 5 to 3 over 0-500). `ApplyGaitTable` runs at
  the start of every `PerformMovement`, so the server's replay and the client's prediction set the same engine values
  from the same state: the gait from the saved-move flags (`GetGait`: crouched, else sprinting and not out of breath,
  else walking, else the run), the rotation mode from the flags both sides set (`GetRotationMode`), the cap by the
  movement's angle to the facing in strafe mode (`MaxSpeedAtAngle`, GASP's `Curve_StrafeSpeedMap`: forward within 45
  degrees, the strafe speed at 90, backward from 135, linear between; the velocity's angle moving, the input's starting), the braking by whether there is input, the
  friction times the surface and wetness scale (`GroundFrictionScale`, what used to multiply the constant 8), the yaw
  rate by mode. `ComputeGaitValues` is the pure form (the tests'). The swim's and the flight's values are untouched (the
  swim rotation rate aside); out of the water by any mode change `OnMovementModeChanged` restores the ground
  acceleration (G154 closed). `bUseGaitTable` is on for players and off on `AVCNPCCharacterBase` (NPCs keep 300 uu/s,
  360 degrees a second and the engine's acceleration; `npc_bench.py` unchanged). The defaults are GASP's, read from
  `SandboxCharacter_CMC`'s class defaults (its `BrakingFrictionFactor` is 0: a stop is the deceleration alone, the same
  on every surface); `vc.Move.Preset Legacy|GASP|Tuned` swaps the table live, `vc.Move.Set` one value, `vc.Move.Print`
  logs it, and `ToggleVoxelDebug` shows the gait, the mode and the row in force (`Documentation/EDITOR_CONSOLE_GUIDE.md`
  §4). The lane's pass (`Scripts/qa/lane_pass.py`, both bodies, the bow in hand): against the engine defaults GASP's
  table halves the run's foot slide over the whole lane (262 to 150 uu/s on the low-poly body, 339 to 160 on Kellan)
  and cuts the drawn strafe's four to five times (351 to 83, 517 to 127); the stop goes from the 0.13 s, 38 uu snap to
  0.27 s and 67 uu (GASP's 2000 uu/s^2); the release leg and Kellan's second 180 pivot vary as much between identical
  runs (105-230 and 106-419 uu/s) as between any change tried, so `Tuned` is GASP's table verbatim and the preset is
  where the next pass's changes go. The numbers per leg: `CHARACTER_REFINEMENT_PLAN.md` §5, C4. Tests
  `VoxelCharacter.Movement.GaitTable`, `GaitTableApplied`.
- **The held set's caps** (the locomotion state machine plan's L2, D91, 2026-11-10). Under the state machine
  (`vc.Anim.Locomotion 2`) the packs' loops are authored at about 92 and 302 uu/s, so the walk, run and sprint rows
  scale to the held set's `UVCLocomotionClipSet` (`WalkSpeed`, `RunSpeed`, `SprintSpeed`: 120, 400, 700 on the packs'
  sets, 200, 500, 700 on the template Unarmed's), the strafe and backward caps in proportion
  (`FVCMovementTable::WithForwardCaps`), and since L3 the crouch row to the crouch set's `CrouchSpeed` (250: the set's
  own crouch, else the one it borrows, `GetCrouchSet`; FVCGaitCaps' `Crouch`), the accelerations, the braking and the
  rates as they are. **The aim's ceiling** (G173, 2026-11-11): while the character aims, a set with an `AimSpeed` (the
  bow's 150) caps every gait at every angle at it (FVCGaitCaps' `Aim`, `ResolveSpeedCeiling`, `ComputeGaitValues`'
  `SpeedCeiling`): aiming walks, crouched too, and the aim cycle plays the aim walk alone (below, "Animation"). Whether
  the character aims is `bAimingForGait`, set on every machine by `AVCCharacterBase::ApplyAimState` (the owner at its
  input, the server from its `bIsAiming`), the aim state that already turns the capsule to the view. The caps
  come from the held item, replicated state: `AVCCharacterBase::UpdateClipGaitCaps` sets them on every machine
  (`SetClipGaitCaps`, from `RefreshActionSet` and `ApplyLocomotionAnimClass`; the set whose loops play, its own or
  Unarmed's). Whether they apply is the anim class this machine plays (`bWantsClipGaitCaps`), which travels in the saved
  move (`FLAG_Custom_3`), so a server, whose graph is the Blueprint's, applies what the predicting client predicted.
  Motion matching, the Blueprint's graph and the NPCs keep the table as it is (`ResolveGaitTable`). Test
  `VoxelCharacter.Movement.GaitClipCaps`.
- **The face-aim flip, eased** (C4). `AVCCharacterBase::SetFacesAim` (a bow's draw and release, the ranged ability on the
  server) used to flip `bOrientRotationToMovement` and `bUseControllerDesiredRotation` in one frame; with the table's
  instant ground yaw that would snap the capsule up to 180 degrees under the mesh. `UVCMovementComponent::SetFacesAim`
  now starts a turn: `PhysicsRotation` turns the capsule toward the new mode's target (the view on a draw; where the body
  moves on a release, nothing when standing) at `AimTurnRate` (540 degrees a second) about local up, and the flags change
  once the body is within `AimTurnTolerance` (15 degrees) of it or `AimTurnMaxTime` (0.5 s) has passed (`AdvanceAimTurn`;
  `IsAimTurnPending` meanwhile). A flip with nothing to turn through completes at once, as before. Test
  `VoxelCharacter.Movement.AimTurnEase`.
- **Planet gravity.** On a planet `PerformMovement` points the engine's custom gravity at the centre before every move
  (`UpdatePlanetGravity`), so the server's replay, the client's prediction and NPCs agree with no new replicated state.
  The component's own vertical code (swim controls, buoyancy, floor and landing traces, the shore climb, the embed
  resolve, the sprint's ground speed) works in gravity space (`GetGravitySpaceZ`). After placing a pawn on a planet call
  `UpdatePlanetGravity()` then `SnapUprightToGravity()`. Heightmap worlds never set a custom gravity, and every
  expression is then the old Z arithmetic (`VoxelCharacter.Planet.GravityFrame`).
- **Block steps.** In a cubic world (`UVoxelChunkManager::IsBlockSurface`) `RefreshBlockStepHeight` sets `MaxStepHeight`
  to one block (`GetBlockStepHeight`: voxel size + `BlockStepMargin`, 80 at 75 uu), so one block is walked up and two are
  a wall (D29; `bStepUpOneBlock`). Smooth worlds keep 50. Surface navigation uses the same rule.
- **Terrain closing around a pawn.** An edit near a pawn arms a check; when the new collision lands,
  `ResolveTerrainEmbedding` lifts a pawn whose centre and upper body are inside solid voxels onto the new surface
  (authority). "Near" is a brush's sphere, or for an edit with no centre (a stamp, undo, a load) the box of voxels it
  changed (`UVoxelEditManager::GetBroadcastEditBounds`, G55).
- **The world's edge.** In the band of `WorldEdgePushBackDistance` before a bounded world's edge a current carries the
  pawn back (`ApplyWorldEdgePushBack`; `IsNearWorldEdge` drives the HUD hint).
- **The weather** (the weather plan's P4, 2026-11-07; behind `WITH_WORLD_WEATHER`, so the plugin stays optional).
  `UpdateVoxelTerrainContext` reads WorldWeatherPlugin's conditions at the pawn with the terrain context (every 0.1 s)
  and keeps the wind, the exposure and the ground's wetness. `CalcVelocity` adds the wind's drift, `ComputeWindDrift`
  (a pure static beside the edge's push-back: the wind x `WindPushFactor` (0.1) x the exposure, scaled by the pawn's
  own speed over its max speed, or full in the air), after the braking and the friction, and takes the previous
  tick's off before them, which would otherwise eat it: a standing pawn idles in any wind, a pawn walking into the
  wind is slowed by it and one walking with it helped, a fall past `WindAirborneDelay` (0.35 s: a jump or a real
  drop, not a floor lost for a frame on rough ground) is carried; flying is exempt. The ground friction loses `Wetness x WetGripFactor` (0.4) of itself. Both run inside the predicted movement,
  so the client and the server agree (the weather's sync point is the same on both). Test
  `VoxelCharacter.Movement.WindPush`. The same read keeps the air temperature and a wet level (P5, `AdvanceWetLevel`):
  the rain reaching the pawn (its rate times the exposure, snow at a third) soaks it over `WetSoakSeconds` (12), a swim
  at once, and nothing falling dries it over `WetDrySeconds` (90); the character's weather states read them
  (`VoxelCharacter.Movement.WetLevel`).
- **Debug flight.** `SetDebugFlight` (QA tooling, not replicated): `MOVE_Flying` in the gravity frame, a climb along
  local up (`SetFlightClimb`), the altitude held otherwise (`GetFlightLevel`); the game module's `vox.Fly`, `vox.FlyTo`
  and `vox.TPLatLon ... hold=1` drive it.

### The gravity frame

On a planet the pawn's up is minus the movement component's gravity direction. `FVCGravityFrame` (`VCGravityFrame.h`)
converts between world space and that frame: `ToGravityRelative` / `ToWorld`, `GetYawRotation`, `VectorToWorld`,
`GetUp`, `GetHorizontalSpeed`, and the Z-up helpers `Horizontal`, `HorizontalDirection`, `HorizontalDistance`,
`UpComponent`, `FacingRotation`, `HorizontalAtAngle`, `VectorToGravity`. Each returns the old Z-up expression without
custom gravity.

- `AVCPlayerController::UpdateRotation` keeps the control rotation in world space, turns it each frame by the change
  in gravity and clamps pitch / zeroes roll in the gravity frame. Every possession levels the view on the first planet
  frame (`bLevelViewInGravity`).
- Whatever reads the control rotation's yaw (ground input, the third-person offset, `FaceRotation`) goes through
  `GetYawRotation`; eye heights and camera offsets through `VectorToWorld`; characters and NPCs override
  `GetPawnViewLocation` (the eye height along local up).
- The NPC AI (steering, waypoints, the stuck probe, the auto-jump, wander and flee goals), placing
  (`GetPlacementTrace` / `GetPlacementTransform`) and the chop aim (`GetChopAimPointFor`) use the frame's helpers.
- Only frame-independent things are done in the frame (zero a pitch or roll, take a yaw-only heading). The frame's own
  twist spins near a planet's south pole, so a gravity-relative yaw is never kept across frames.

## Animation

`UVCAnimInstance` (`VCAnimInstance.h`) is the anim instance of every character: the player, the NPCs, any `ACharacter`
with a character movement component (`UVCMovementComponent` adds the surface type and the sprint). What is not movement
it reads through `IVCAnimatedCharacter` (`VCAnimatedCharacter.h`), which both character classes implement:
`GetAnimViewMode` (the local player's mode for its own pawn, third person otherwise and for NPCs), `GetAnimActionSet`,
`GetAnimCombatComponent` (dead and downed).

The character animation plan (2026-11-05 to 11-07, D43-D51; `CHARACTER_ANIMATION_PLAN.md`, frozen) built what this
section, "Appearance" and "NPCs", "Bodies" describe: one anim instance for every character, its values in the gravity
frame (P2); anim layers, action sets and their montages (P3); foot placement, the head's look-at, the off-hand grip and
the sprint's pace (P4); the player's motion matching on the Game Animation Sample's set (P5); bodies as data, MetaHumans
among them (P6); the NPCs' skeletal bodies within an animation budget (P7). The body skeleton is the UE5 Mannequin's, and
GASP's set plays on it unconverted through skeleton compatibility (D50; `Scripts/setup_character_animation.py` marks the
skeletons compatible). The animation content is built by scripts into content packs (D46), in this order:
`setup_character_layers.py`, `setup_character_ik.py`, `setup_character_locomotion.py`,
`setup_character_appearances.py`, `setup_creature_animation.py` (`Scripts/README.md`). Open: G145
(`Documentation/FINDINGS.md`); G147 (VanillaLoop's clips) and G148 (the correctives near the camera only) closed after it. The character refinement plan (2026-11-08, D70-D75; `CHARACTER_REFINEMENT_PLAN.md`, frozen) then took what the two
plans left rough once they met in play: the bow's two grips and per-body attach corrections ("The bow", "Appearance"),
the moving stance over the upper body ("Layers" below), the aim state with its camera and reticle ("The bow" › Aiming,
"Camera", "HUD and widgets"), GASP's gait table ("Movement"), all judged on the `CharacterTest` level ("Tests"). The
locomotion state machine plan (2026-11-10 to 11-11, D86-D92; `LOCOMOTION_STATE_MACHINE_PLAN.md`, frozen) then gave the
player a locomotion of our own over the Essential Animation packs ("The state machine" below), the default since its
L4: D44 is reversed, and motion matching stays selectable for comparison.

- **Two halves.** `NativeUpdateAnimation` gathers an `FVCAnimInput` on the game thread (the character's state in world
  space, and a line trace along gravity for the ground under a falling character, on the movement's channel, up to
  `MaxGroundTraceDistance`); `NativeThreadSafeUpdateAnimation` computes the values from it with `ComputeValues`, which
  touches only the instance and is what the tests call.
- **The frame.** Each update builds local up (minus the gravity direction), the facing (the actor's forward across up)
  and right; every value is measured against them, so it is the same on a plane and anywhere on a planet, and without
  custom gravity it is the Z-up expression (`Speed` is `Size2D` bit for bit). The state kept across updates is world
  vectors (the last facing, the body heading), re-projected each update: no gravity-relative yaw is kept.
- **Values** (`BlueprintReadOnly`; the names `ABP_VoxelCharacter` reads are unchanged): `Speed`, `Direction`,
  `bIsFalling`, `bJustLanded`, `bIsCrouching`, `bIsAccelerating`, `bIsSprinting`; `LocalVelocity`, `LocalAcceleration`,
  `RelativeAcceleration` (over the maximum acceleration or the braking deceleration, Lyra's lean input), `LeanAmount`,
  `YawRate`; `SlopePitch`, `SlopeRoll` (the floor under the feet, against the facing), `GroundDistance`, `TimeToLand`;
  `RootYawOffset`; `ViewMode`, `AimPitch`, `AimYaw` (a remote character's from its base aim rotation); `ActionSet`;
  `bIsDead`, `bIsDowned`; `SurfaceType`, `bIsSwimming`, `bIsUnderwater` (the capsule under at `UnderwaterImmersion`),
  `WaterDepth`.
- **Turn in place.** While the character stands on the ground, the body keeps its heading as the facing turns:
  `RootYawOffset` is the body's angle from the facing, clamped to `TurnInPlaceMaxOffset` (the body is dragged past it),
  and it returns to zero at `TurnInPlaceRecoverySpeed` once the character moves or leaves the ground (falling,
  swimming, held in `MOVE_None` while the terrain loads, when a restored save turns the pawn). A turn past
  `TurnInPlaceMaxOffset` in one update is a rotation set by code (a teleport) and seats the body on the facing at once.
  Carried along a walk, the body heading
  is a heading in each tangent plane, so round a closed loop it turns by the solid angle the loop encloses; the
  frame's twist at the south pole does not reach it.
- **Action sets.** `Equipment.AnimSet.*` (`VCGameplayTags.h`: Unarmed, Tool, SwordShield, TwoHand, Bow) name which
  layer and montages animate a character. An item names its set in its Equipment fragment's `ActionSetTag`
  (ItemInventoryPlugin); `AVCCharacterBase::PickActionSet` makes the main hand's set the item's own, `Tool` for an item
  that names none and `Unarmed` for an empty hand (the held item: a weapon drawn from a sheath counts, a sheathed one
  does not). A set's `SheathMontages` hold its `Unsheathe` and `Sheathe` per sheath ("Items, progression and saves" ›
  Carried weapons). The character refreshes `ActiveActionSet` on the equipment's
  `OnEquipmentChanged`, which fires on every machine (on clients from the replicated slots). An NPC's set is its
  `ActionSet` property (`Unarmed` unset).
- **Layers.** `ABP_VoxelCharacter` runs the layer `UpperBodyStance` of `ALI_VCCharacter` between its state machine and
  its slot, and implements it itself (the base pose through). An item's Equipment fragment links its stance layer on
  equip (`AnimLayerClass`, EquipmentPlugin's `LinkAnimClassLayers`): `ABP_VCLayer_SwordShield`, `_TwoHand`, `_Tool` and
  `_Bow`. **The moving stance** (the character refinement plan's C2, D72, 2026-11-08; before it each layer laid the
  set's one idle clip over `spine_01` at full weight in mesh space, so the torso, arms and head froze in the idle at every
  gait while anything was held): each layer plays its set's stance blend space `BS_VCStance_<Set>` (the pack's own idle,
  walk and jog for that weapon at 0, 200 and 500 uu/s, the jog carrying the sprint) at the body's speed, and lays it over
  the base pose with a layered blend ramped over three bones from `spine_01` (a third there, two thirds at `spine_02`,
  full from `spine_03`: the lower spine keeps most of the locomotion's bounce and twist, the clavicles, arms and head
  take the stance), in local space, weighted by the body's `StanceWeight` (`UVCAnimInstance`: 1 on the ground,
  `StanceSprintWeight` at a sprint so the sprint's arms show through, 0 in the air, the water, dead or downed, eased at
  `IKBlendSpeed`). Every layer's parent is `UVCAnimLayerInstance`, which copies the body's `Speed` and `StanceWeight`
  each update (and holds the speed at 0 under `vc.Anim.Stance 1`, the static stance as before). **The bow's carry**
  (D85, 2026-11-09, the user: "the running and walking is still using the standing idle"; G171): the pack's bow walk and
  jog hold the bow as its idle does, the hands 3 to 8 cm from where they idle against the chest at a walk and 12 to 28 at
  the jog (their whole swing), so laid over the upper body the bow walked and ran in its idle; and a crouch laid the
  standing idle over the crouched legs, the torso upright. A layer's `MovingStanceWeight` (`UVCAnimLayerInstance`, a class
  default the script sets; 1 unset) is its stance's share once the body moves: between `StanceFadeSpeeds` (10 and 150
  uu/s) the layer's `StanceWeight` goes from the body's to that share of it (`ComputeStanceFade`, pure). The bow's is 0:
  at a stand it holds the pack's idle, moving it gives the body to the locomotion, both arms swinging (GASP's walk, run,
  sprint and crouch walk) and the bow riding in the left hand; aiming and drawing lay over it as before. The swords',
  the two-handed set's and the tool's are 1, unchanged. A set's `stance_crouch` (the bow's: the pack's
  `UE5_A_Crouch_Loop`, the bow low at the side) plays instead of the stance blend space while the body crouches (a blend
  by the layer's `bIsCrouching`, 0.2 s each way), so a crouch at a stand bends the torso with the legs (`LAYER_VERSION`
  `stance-v5`). `vc.Anim.Stance 0|1|2|3` is the A/B: 3 is C2's moving stance at every gait (no fade). `setup_character_layers.py` builds the blend spaces and versions the layers (`LAYER_VERSION` in the
  Blueprint's description: a wiring change rewires every layer in place on the next run). Montages play in the slot
  above, over the stance. **The upper-body action slot** (G162, 2026-11-09): between the layer and that slot both main
  graphs (`ABP_VoxelCharacter` and the motion matching copy) have a second slot, `UpperBodyAction`, laid over the body
  as the stance is (a layered blend on the same three-bone ramp from `spine_01`, local space, full weight; the layer's
  pose reaches the slot and the blend's base through the cached pose `VCStanced`, since a pose pin feeds one input).
  With no montage in it the slot passes the pose through; a montage in it moves the spine, arms and head while the
  legs keep the locomotion. The sheaths' `Unsheathe` and `Sheathe` play there (before, in the default slot, a draw on
  the move stood the legs still for the clip's second). It is not the layer's `UpperBody`: a layer plays the body's
  montages in its own slots, so a second `UpperBody` after the bow's layer would lay the bare draw over the aimed one.
  On `CharacterTest` (both graphs, the bow, the sword and the pickaxe, under `slomo 0.1`): the speed holds at 200 uu/s
  walking and 500 running through every sheathe and draw and both feet keep their stride; at the hand-off the hand
  comes as near its sheath as in the clip itself, standing or running (24 uu at the hip's draw, 30 at its sheathe, 30
  to 32 at the back's, the clips' 24, 31 and 32 to 33), since the layered blend keeps the clip's arms on the spine.
  `ABP_VCLayer_Bow`
  (ranged combat plan B4) also aims: `UVCAnimLayerInstance` plays the body's montages in the layer's own
  slots (`bUseMainInstanceMontageEvaluationData`) and copies the body's `AimPitch`, `AimYaw` and `AimRaiseWeight` and its
  montage weight in the `UpperBody` slot each update; the bow idle follows the view through the stand aim offset inside
  the stance; an aim branch over the stance, in mesh space (G160), plays the raised bow, the `UpperBody` slot (the draw
  and the release, the upper body only, so the legs keep walking) and the drawn aim offset that points the bow where the
  view looks, weighted by `DrawnAimWeight` (the raise or the montage, the larger; `LAYER_VERSION` `stance-v4`).
- **Action montages.** `UVCActionMontageComponent` (on both character classes) plays an action from the
  `UVCActionAnimSet` of what the character holds: `ChooseAttack` steps through the set's combo within its window and
  picks the play rate that fits one swing into an attack's interval; `PlayActionLocal` plays on this machine (a dig or a
  chop already playing keeps playing); `PlayActionForEveryone` sends one unreliable multicast, which a dedicated server
  and, for an action it played at input, the owning player skip (D49). On the server it answers the combat component: a
  hit react from the side the hit came from (`DirectionOfHit`, about local up), a death montage for a body with no
  physics asset (one with a physics asset ragdolls), a get-up on revive.
- **What plays when.** The melee attack: the owner plays its swing at once (`StartMeleeSwing`, from
  `TryStartMeleeAttack` or a swing at the air; the server's rate limit mirrored); the server's ability sends it to the others and lands the sweep and the damage at the montage's hit
  time, the first `UVCAnimNotify_ActionHit` (`GetHitTime`; 40% of the length without one), and at once with no montage.
  Dig, place and chop: the owner at input, the others once the server accepted the edit or the chop. Use and placing an
  item: everyone, from the server, since the server may refuse. The bow's draw and release: as the melee attack (ranged
  combat plan B3); while drawn in third person the body turns toward the view (`SetFacesAim`: the controller's desired
  rotation instead of the movement's direction, upright in the gravity frame, the flip eased by the movement since C4:
  "Movement" above), set by the owner at its draw and release and by the server's ranged attack at its start and end.
- **Where the sets come from.** The player's are `UVCSettings::PlayerActionAnimSets` (Project Settings > Voxel
  Character, `Config/DefaultGame.ini`) unless its Blueprint lists its own; an NPC lists its own (none yet: its attacks
  land at once, as before).
- **IK and pace** (the plan's P4). After its slot, so over the montages too, `ABP_VoxelCharacter` runs in component
  space: foot placement and its leg IK (`FootIKAlpha`), a look-at on the head (`LookAtLocation`, `LookAtAlpha`) and a
  two-bone IK that puts the left hand on a two-handed item's grip (`HandIKEffector`, `HandIKAlpha`); the Walk/Run blend
  space plays at `LocomotionPlayRate`. The anim instance eases each weight at `IKBlendSpeed` a second, so nothing
  snaps, and a switch only changes a value, never the graph.
  - Feet: on while the character is on the ground, alive, not downed, not swimming or flying. The node is GASP's with
    the Graph speed mode (a foot plants when its ball stops in the pose; our clips carry no contact curves) and traces
    along the mesh's own down, so it plants on a planet as on a plane; the pelvis drops at most 50 uu, so one foot on a
    75 uu block step bends that leg instead of crouching the body.
  - Feet on a body without `ik_foot` bones (a MetaHuman's; the engine's node skips there, P6): `FVCAnimNode_FootBoneIK`
    after the leg IK, weighted by `FootBoneIKAlpha` (`FootIKAlpha` on such a body, 0 on one with the bones, so no body
    is placed twice). Each foot traces the ground under it along the mesh's own down (a small sphere sweep on the
    animation worker, as the engine's node traces), the pelvis drops to the lowest foot's ground (at most
    `MaxPelvisDrop`, 50), each leg reaches its ground by two-bone IK from the animated pose (the lengths from that pose,
    so the shin never stretches) and a foot near the ground tilts to its normal (at most 30 degrees); the heights ease at
    `InterpSpeed`. On a 28-degree slope in `VoxelDemo` Kellan's feet stand 11 and 33 uu over the ground without it and
    0.6 and 1.4 with it; on `Review_Cubic`'s 75 uu block step the upper leg bends and the lower foot reaches down 52 uu,
    25 short of the lower block (his legs are short for the 50 uu pelvis drop, where the low-poly body's reach; G145).
  - All three run at LOD 0 only (P7): an NPC a room away skips the traces and the solve.
  - Pace: past `LocomotionReferenceSpeed` (600, the blend space's top) the clips play at the speed over it (1.6 at a
    sprint), so the feet keep pace with the ground. Stride warping measured nothing here (the jog's feet are near the
    legs' reach, where it clamps) and orientation warping is not needed (eight directions).
  - Look-at: a player's head turns toward the point `LookAtDistance` along its view, an NPC's toward its AI controller's
    focus, at most 50 degrees (the node's clamp); it fades for a target more than `LookAtMaxAngle` from the facing
    (about local up), in first person, dead or downed. The node's look-up axis and interpolation stay off: with them on
    the head did not turn.
  - Hands: an action set with `bOffHandIK` puts the left wrist at `OffHandGrip`, a point in `hand_r`'s space, on the
    handle 20 cm from the main hand (the TwoHand set (0, 2, 20); the Tool set's turned with the pickaxes' tilted grip,
    G147); the elbow keeps its plane. A set with `bHasDrawnGrip` re-seats the main hand's item at `DrawnGrip` while its
    Draw plays (none since G161: the bow's one fit holds at rest and drawn, "The bow" above).
  - `vc.Anim.FootIK`, `vc.Anim.SprintPace`, `vc.Anim.LookAt` and `vc.Anim.HandIK` switch each off for an A/B;
    `vc.Anim.Debug 1` draws each character's look-at target and weights and turns on the engine's foot placement draw.
    `Scripts/setup_character_ik.py` builds the chain and the play rate; a crouched character has no pose yet (G140).
- **Motion matching** (the player under `vc.Anim.Locomotion 1`: its default until the locomotion state machine plan's
  L4, kept since for comparison; the plan's P5). The player's mesh plays `ABP_VCPlayerMotionMatching`, the Game
  Animation Sample's CMC AnimBP that `Scripts/setup_character_locomotion.py` copies with its choosers
  (`UVCSettings::PlayerLocomotionAnimClass`). `AVCCharacterBase::ApplyLocomotionAnimClass` gives it to the mesh at
  BeginPlay on every machine that draws the player; a dedicated server and the automation tests keep the Blueprint's
  `ABP_VoxelCharacter`, and `vc.Anim.Locomotion 0` gives that back at once, the held items' layers linked again. It
  plays GASP's databases unconverted through skeleton compatibility (D50): starts, stops, pivots, turns in place, jumps
  and landings, walking, running and sprinting, and crouching (the state machine's Crouch state has no pose, G140).
  - Its parent, `UVCMotionMatchingAnimInstance`, lives in the module `VoxelCharacterAnimation`, which carries PoseSearch
    so the runtime module does not (D45). It is a `UVCAnimInstance`, so the layers, the montages and the IK work as on
    `ABP_VoxelCharacter`. GASP's graph reads its character through one struct, `CharacterProperties`; the parent fills
    it from our pawn by its fields' names (the velocity, the input acceleration, the maximum acceleration and the
    braking in force (`GetMaxBrakingDeceleration`, as GASP passes it; it was the constant 4096 before C4), the aim, the
    gait (`UVCMovementComponent::GetGait` since C4: the crouch counts as the walk's pace, its stance says the rest; a
    stock movement component's by the sprint and the maximum speed), the stance, the rotation mode, the landing), and the
    copy's own fill through GASP's pawn interface is emptied. The trajectory's tuning is exposed for the handling pass
    (C4): `RotateTowardsMovementSpeed` and `MaxControllerYawRate` on the instance override the graph's two
    `FPoseSearchTrajectoryData` when set (negative, the default: the graph's), and `vc.Anim.TrajectoryRotateSpeed` /
    `vc.Anim.TrajectoryYawRate` override those live (`TunedTrajectoryData`).
  - The gravity frame. GASP's arithmetic takes world Z as up (`VSizeXY`, `CalculateDirection`, rotator deltas, two Euler
    yaw sums, the engine's trajectory prediction); the script swaps each such node for the parent's function of the same
    pins, which measures in the gravity frame and is the engine's own arithmetic where up is world Z. The trajectory is
    `UVCGravityTrajectoryPredictor`'s, which turns about local up (spike S1's); the engine's collision pass already
    follows the movement's gravity. `vc.Anim.GravityTrajectory 0` is the A/B with the engine's prediction, and
    `vc.Anim.Debug 1` draws the trajectory and writes what motion matching plays (`GetMotionMatchingSelection`).
  - The copy's AnimGraph gets what `ABP_VoxelCharacter` has: a swimmer plays `AS_Swim` in place of the locomotion, the
    held item's `UpperBodyStance` layer runs before GASP's slot, the `UpperBodyAction` slot between them (G162), and
    after GASP's foot placement and leg IK (which
    `FootIKAlpha` drives) come P4's look-at and off-hand IK. GASP's member `RelativeAcceleration` is
    `RootRelativeAcceleration` in the copy (`UVCAnimInstance` has one of that name).
  - Cost for one running player (the CSV profiler): 0.50 ms of animation on the game thread and 1.39 ms on the animation
    worker, against 0.22 and 0.33 ms for `ABP_VoxelCharacter`.
- **The state machine** (the player's locomotion, `vc.Anim.Locomotion 2`, the default since 2026-11-11, D86 reversing D44;
  `LOCOMOTION_STATE_MACHINE_PLAN.md`, D86-D92, L0-L4 2026-11-10 to 11-11, frozen). `ABP_VCPlayerLocomotion`
  (`Scripts/setup_character_state_machine.py`, `UVCSettings::PlayerStateMachineAnimClass`) plays the held action set's
  full-body clips from the Essential Animation packs: each `UVCActionAnimSet` names a `UVCLocomotionClipSet`
  (`LocomotionClips`: the idle, the walk, jog and run loops by direction with their authored speeds, the fast run, the
  starts and stops by direction, the run's stop, the turns in place, the jump, fall and landings, and the set's gait
  caps, D91, which the movement scales to: "Movement"), Bow on hold `B` (D92), SwordShield on `SSH`, TwoHand and
  Tool on `2H`, Unarmed on the template Mannequin's loops. Its parent `UVCLocomotionAnimInstance` (a `UVCAnimInstance`,
  so the layers, the montages and the IK read the same values) takes each family from the held set, else from
  Unarmed's (D88, `ResolveClips`), gives the graph the idle and the Cycle (each set's loops in a 2D blend space by
  local direction and speed) in two slots that a set change flips with an inertialized blend (`ApplySetChange`),
  steps the state (`StepState`, below) and gives the cycle its pace, and gives the held stance a weight of 0 while the set plays its own locomotion
  (`ComputeStanceTarget`), so the clip's own arms show (D87); the bow's aim branch in its layer lays over them as
  before. While the character aims (raised or drawn) the Cycle plays the held set's aim cycle (`AimWalk`, `AimIdle`,
  `AimCycle`: the bow's hold `C`, side-on with the drawn shoulders, its walk under `B`'s jog; `UsesAimCycle`), so
  the hips do not twist away from the aim as the carry's strafes turn them. A set with an `AimSpeed` (the bow's 150, G173)
  plays its aim walk alone while aiming, whole at that speed (1.6 for hold `C`; `ComputeCyclePace`), and every gait's
  start and stop is the aim walk's (`ChooseStart`, `ChooseStop`; the aimed crouch's slow walk at it, `CrouchCap`): the
  movement holds the speed there. Without it the aim cycle blended `C`'s walk (92 uu/s) with `B`'s jog (302) at the
  run's strafe (266), and toward the character's back the two step differently, so a strafe left crossed the legs (the
  feet 2-6 cm apart across the travel, 25-33 with the ceiling).
  The states (L2, 2026-11-10; L3 2026-11-11): Idle, TurnInPlace, Start, Cycle, Sprint, CrouchChange, Stop, Jump, Fall,
  Land (the moving landing when it touches down on the move), Swim. The anim instance steps the state itself (`StepState`, one step an
  update along `GetStateTransitions`' list) and the graph's state machine follows it, each transition reading its
  target's bool (`bInIdle` ...), so the choices are C++ and tested. Each starts its clip as it begins and plays it by
  time (the packs have no distance curves; the graph evaluates `StartClip` at `StartTime` and so on): the start for the
  gait and the input's direction from the body (forward in orient mode, where the capsule turns to the input; by
  direction in strafe), handing over to the cycle at 90 % of the cap or its clip's end; the stop for the gait and the
  direction, entered where its root's travel left is the capsule's braking distance (`FindStopEntryTime`), so its feet
  land where the capsule stops; a turn when the body stands 60 degrees or more off the facing (the 90, or the 180 from
  135). `UVCLocomotionClipSet::ChooseStart`, `ChooseStop`, `ChooseTurn` hold §3.3's rules: the sword sets' walk starts
  and stops are their jog's at the walk's rate, the run's stop is forward only, a set without a family inertializes
  between the states, and a set without turns (Unarmed) eases the body round (the procedural turn). Each gait's loops
  play whole at its cap at the cap over their authored speed (`ComputeCyclePace`: the Cycle's blend speed and play rate,
  D91); the sprint plays the set's `Sprint` (Run_01 to Run_02 along speed, `ComputeSprintPace`) past the run's cap,
  forward of the body. The body keeps its heading off the facing (`RootYawOffset`, up to a half turn under this graph):
  standing, a turn in place unwinds it with its clip's root yaw scaled to the offset; moving, it eases out after the
  capsule's instant ground yaw (the gait table's), so a pivot swings the body round rather than snapping it; while the
  body faces the view (aiming) the legs stay within `BodyMaxOffset` (90 degrees) of the torso, dragged round past it,
  and a turn ends if the view swings back past the body; and the
  cycles step by the velocity's angle from the body (`CycleDirection`). The aim layer aims from the facing, as in the
  other graphs (`AimYaw`): its raised bow is laid in mesh space, so it starts from the capsule, and measured from the
  body it overshot the view by the legs' lag until the feet caught up (fixed in L4's pass). After
  the state machine the graph zeroes the root bone's translation and rotation (the packs carry their root motion in the
  root with the root-motion flag off, which would walk the mesh off the capsule; a turn's yaw comes back through the
  offset), then an Inertialization node, the root turned by `RootYawOffset`, and `ABP_VoxelCharacter`'s own chain, pasted (the layer, `UpperBodyAction`, the default
  slot, foot placement, the foot-bone IK, look-at, off-hand IK), with one change: while the character moves the
  default slot's montages (attacks, digs, hit reacts) lay over the upper body on the stance's ramp
  (`ActionLegsWeight`), so a swing on the move keeps the legs running; standing they play over the whole body.
  **The crouch** (L3, 2026-11-11). A clip set's crouch (`CrouchIdle`, `CrouchBegin`, `CrouchEnd`, the slow crouch walk
  and the crouch walk by direction with their starts and stops, `AimCrouchWalk` and its starts, stops and idle for the
  aim, `CrouchCycle` and `AimCrouchCycle`) is the bow's hold `B` (114 and 302 uu/s authored, the aim's hold `C` slow
  crouch walk); a set without one names the set it borrows (`BorrowedCrouch`: SwordShield, TwoHand and Unarmed borrow
  the bow's). Crouched (`bCrouchPose`), Idle plays the crouch set's crouch idle, Cycle its crouch cycle or, aiming, its
  aim crouch cycle (`CycleChoice` 0-3: the slots' cycle, the aim cycle, the crouch cycle, the aim crouch cycle, each
  inertialized), the starts and stops are its (`ChooseCrouchStart`, `ChooseCrouchStop`: the family that plays at the
  cap), and a change at a stand plays its begin or end in `CrouchChange` (a change back mid-way plays the other from the
  matching point). The pace (`ComputeCrouchPace`) has the slow crouch walk whole at `CrouchSlowSpeed` (150, 1.31) and
  the crouch walk at `CrouchSpeed` (250, 0.83), the movement's crouch cap. Crouched the body turns procedurally (the
  packs' turns stand). On a borrowed crouch the graph lays the set's own idle (`BorrowedUpperClip`) over the upper body
  on the stance's ramp at `BorrowedCrouchWeight` (§3.3), so the bow's legs crouch under the sword's or the empty
  hands' arms; `vc.Anim.SM.BorrowCrouch 0` plays the walk's loops instead. **The tunables** (L3): the anim instance's
  `TransitionBlendSeconds`, `TurnInPlaceAngle`, `TurnPlayRate`, `BodyMaxOffset`, `StartExitSpeedFraction`,
  `SprintEnterMargin`, `SprintMaxAngle`, `ReversalAngle` and the body's recovery each have a `vc.Anim.SM.*` override
  (`Documentation/EDITOR_CONSOLE_GUIDE.md` §4). `ReversalAngle` (a stop the way the body went, the body held, then a
  start the new way, on an input that far against the velocity) is off by default (the plan's L3 results). **The
  cost** (L3's lane, the CSV profiler): about `ABP_VoxelCharacter`'s, half of motion matching's game thread and a
  third of its worker on the low-poly body. **What is left** (the plan's §5): the packs the project does not own
  (Unarmed's own starts, stops, turns and crouch; a staff), the bow's holds `A` and `C` as states, pivot clips (the 180
  reversals slide most), distance matching, and hold `B`'s raised bow about 17 degrees right of the view with the head
  short of it (the base pose). The NPCs, the dedicated server and the tests play `ABP_VoxelCharacter`; whether they
  move to this graph is the open decision D93 (`Documentation/FINDINGS.md`).
- **The content.** `Scripts/setup_character_layers.py` builds the interface, the layers, the montages (from the
  template mannequin's, Sword & Shield's, Bow and Arrow's, GASP's and VanillaLoop's sequences, a hit notify at the
  strike; the bow's draw a raise, a pull and a held loop in sections, `UVCAnimAuthoringLibrary::AppendMontageSegment`;
  part of a clip, `TrimMontage`) and the five sets under `/Game/CharacterAnimation` (a content pack), puts the layer
  into `ABP_VoxelCharacter` and tags the swords, the pickaxes and the bow. A montage whose clip the tables change is
  reset to it in place (`ResetMontage`: its path and what refers to it kept; deleting it and making it again stopped on
  the editor's "Overwrite Existing Object" dialog, G150). The pickaxe digs with VanillaLoop's mining loop (`FreeSampleAnimationSet`, a content pack: one swing in
  2 s, the dig landing at the strike, 0.28 of the clip); the pickaxes sit in the hand as the pack's own does (its
  socket on `hand_r`: the handle tilted 13.6 degrees, the Equipment fragment's `AttachOffset`), so the strike lands
  at knee height before the player. Placing by hand or with the bow reaches out with the pack's right-hand pickup (its
  first second: the rest stows the item behind the back) (G147). Still
  stand-ins (D51), for want of a clip in the packs: the pickaxe holds the two-handed idle, and using by hand drinks the
  bow pack's potion.

## Appearance

A character's body is data (the plan's P6): `UVCCharacterAppearance` (`Animation/VCCharacterAppearance.h`, a primary
data asset) names the mesh the character animates (`BodyMesh`), an actor whose skeletal meshes dress it
(`PartsActorClass`, a MetaHuman's Blueprint) and its component that lends the body (`PartsBodyComponent`), the parts
that animate themselves (`PartsWithOwnAnimation`), an anim class (`AnimClass`, else
`UVCSettings::PlayerLocomotionAnimClass`), whether the body has the Mannequin's `ik_foot` bones (`bHasIKFootBones`)
and where its post-process AnimBP runs (`bBodyPostProcess`, `BodyPostProcessMaxLOD`). Nothing links to the MetaHuman
modules: a MetaHuman arrives as content.

- **Dressing.** `UVCAppearanceComponent` ("Appearance", on both character classes) applies one. The character's `Mesh`
  stays the animated mesh and takes the body's mesh and materials. The parts actor is spawned (transient, without
  collision, owned by the character), its body component hidden and its other skeletal meshes attached to the `Mesh`:
  they follow it by leader pose, except the parts in `PartsWithOwnAnimation`, which run their own AnimBP on the new
  parent (Kellan's face copies the head and runs RigLogic, his grooms on it; his hoodie's post-process AnimBP copies the
  body's pose and simulates the drawstrings). A null appearance gives the class's own body and materials back. In first
  person the parts are hidden with the mesh.
- **Which body.** The project's choice, never the plugin's (D43): `UVCSettings::DefaultPlayerAppearance` and
  `PlayerAppearanceByMeshing` (one per world meshing mode), a game mode's `PlayerAppearance` for its map, and the
  player's own choice. `UVCAppearanceComponent::PickAppearance` takes the player's choice, then the game mode's, then
  the world meshing mode's, then the default; none at all is the Blueprint's own body. This project: Kellan in smooth
  worlds (Marching Cubes and Dual Contouring), the low-poly body in cubic ones (`Config/DefaultGame.ini`). An NPC names
  its own (`AVCNPCCharacterBase::Appearance`).
- **The player's.** The server resolves it (`AVCPlayerState::ResolveAppearance`: when the pawn is possessed, at
  BeginPlay for a pawn possessed before play began, since the world's meshing mode is known only then, and when the
  save loads) and replicates it from the player state (`Appearance`, `OnAppearanceChanged`); every machine dresses the
  pawn, swaps the anim class and links the held items' layers again. `SetChosenAppearance` (server) sets the player's
  choice, saved in `FVCPlayerSaveState::ChosenAppearance`; a save without one takes the rule.
- **A dedicated server** spawns no parts and draws nothing: the body is the appearance's `BodyMesh`, else the mesh of
  the parts class's own body component (read from the class), so the server animates the skeleton its clients do.
- **Outfit physics** falls along the movement's gravity: each tick while there are any, the component points the
  gravity of every rigid-body node in the worn meshes' anim and post-process instances, and of their cloth, along the
  movement's. The engine's take world Z, so at a planet's equator the hoodie's drawstrings stood out sideways; now they
  hang toward the planet's centre.
- **Sockets.** None: equipment attaches to the `hand_r`, `hand_l`, `head` and `spine_03` bones, which the MetaHuman
  skeleton has and orients as the Mannequin does. **Per-body corrections** (the character refinement plan's C1, D71,
  2026-11-08): an item is fitted once to the Mannequin's hand (its Equipment fragment's `AttachOffset`, in the pose it
  is held in: the bow in the draw) and a body whose hand differs corrects every item on a bone in `AttachCorrections`
  (by bone, in the bone's space, after the fit; `GetAttachCorrection`). `AVCCharacterBase::UpdateEquipmentAttachments`
  composes fit, correction and a QA nudge (`ComposeAttachTransform`: fit, then correction, then nudge) whenever a visual
  attaches, the view changes or the appearance is applied (`ApplyPlayerAppearance` re-attaches, so a body swap corrects
  at once). `vc.Equip.Nudge` moves a held item live and `vc.Equip.PrintFit` prints the item's line and the body's entry
  (`Core/VCEquipFitConsole.cpp`, dev builds). Tests `VoxelCharacter.Appearance.AttachCorrection`.
- **Cost** (one player, in-editor PIE). Standing, the CSV profiler: Kellan adds 3.0 to 3.7 ms of game thread a frame
  (11.5 against 8.5 ms at `PlanetTest`'s equator, 17.4 against 13.7 in `VoxelDemo`) and 0.2 to 0.7 ms of GPU. Most of
  it is animation-worker time the game thread waits for at the frame's end: the body's post-process AnimBP (the
  MetaHuman's corrective pose drivers, 2.3 ms), the face's RigLogic (0.9 ms), the hoodie's simulation (0.6 ms).
  `voxel.Bench.Run`'s `VoxelDemo` chord, three runs each, gives the frame's median 1 to 3 ms above the low-poly
  body's.
- **The correctives near the camera only** (G148). The body's post-process AnimBP is the one large item, so the
  appearance says where it runs: `bBodyPostProcess` false never, `BodyPostProcessMaxLOD` up to that LOD (the
  component's `PostProcessAnimBPLODThreshold`, -1 every LOD), and never while the body is hidden from its own player's
  view (first person: `UVCAppearanceComponent::SetBodyHiddenFromView`, from the character's mesh visibility). A null
  appearance gives the class's own settings back. Kellan's run at LOD 0: the local player in third person keeps them
  (the body is at LOD 0 at the camera's distance); in first person the animation worker's total falls from 5.2 to 3.2
  ms a frame; a body past LOD 0 (another player farther off) saves 0.85 ms (with the LOD forced to 1: 2.6 to 3.1 ms
  with them, 1.8 to 2.2 without). The CSV profiler, one player standing in `VoxelDemo`, with two other editors open:
  the game thread's numbers were noise, the worker's held across repeated runs.
- `vc.Appearance [name|path|rule]` sets every player's choice on the server (an appearance the settings name, by its
  name, or a path; `rule` clears it; no argument lists them). `Scripts/setup_character_appearances.py` builds
  `DA_VCAppearance_LowPoly` and `DA_VCAppearance_Kellan` (`/Game/CharacterAnimation/Appearances`).

## Camera

`UVCCameraManager` keeps a stack of `UVCCameraModeBase` instances and blends the top two over `ModeTransitionBlendTime`.
First person sits at the head socket with a wider field of view; third person is an over-shoulder offset whose pivot
follows at `LagSpeed`, never farther than `MaxLagDistance` behind the character (0 = no cap; the character refinement
plan's C4: at a steady speed the pivot settles speed / `LagSpeed` behind and swings past on a direction change, which
read as the body sliding) and snaps across a teleport (`ComputeLaggedPivot`, G41), with a trace against voxel terrain when
`bUseVoxelCameraCollision` is set (`ResolveVoxelCameraCollision`). **The aim camera** (C3): `UVCAimCameraMode`, a
third-person mode closer in (arm 150, offset (0, 60, 55), 70 degrees) with no lag, so its place is a function of the pawn
and the control rotation alone (`ComputeCameraLocation`, pure), which is what lets the server compute the shot's ray
from the same origin (D74). The character pushes it while aiming (`AimModeClass`) and `PopCameraMode` brings the mode
under it back as a fresh instance on top (the stack had a push and no pop); the blend is the stack's as for a view
change. For a walker that
`IsSteppingBlocks()` the view eases through each block step (`bEaseBlockSteps`, `BlockStepEaseSpeed`, pure
`ComputeStepEaseOffset`). `UVCUnderwaterPostProcess` follows the camera, not the feet, so a third-person view above the
surface stays clear while the pawn swims. It is the local player's view and nothing else's (G133, 2026-11-06): its
post-process volume is unbound, so it tints every view in its world, and the character creates the component only on
the pawn a local player controller views through (`UpdateUnderwaterPostProcess`, at `BeginPlay` and again on every
controller change through `NotifyControllerChanged`: possession, unpossession, the owning client's `OnRep_Controller`)
and destroys it, volume included, when that controller goes. A pawn with no local player controller (another player's
character on a client, an NPC, every pawn on a dedicated server) has no view to tint and gets none; it used to get one
that read the pawn's own terrain context, so a swimming remote player or NPC blended the local view blue, several of
them stacking. Test `VoxelCharacter.Underwater.LocalPlayerOnly`; the dedicated-server PIE check (one client swims, the
other's view stays clear) is in the G133 row of `Documentation/FINDINGS.md`.

`AVCCharacterBase::SetViewMode` pushes the camera mode, switches rotation (first person: yaw follows the controller;
third person: orient to movement), hides the body for the local first-person view after the blend, attaches the
equipment visuals to the arms or the body (`FVCEquipmentSocketMapping`; an item that names its own bone, the bow on
`hand_l`, keeps it in both views), updates the interaction range and broadcasts
`OnViewModeChanged`; the anim instance reads `CurrentViewMode`. The view mode is the local player's and is not
replicated (G87, 2026-11-02): `SetViewMode` runs on the machine that calls it (the owning client's toggle), and every
other machine draws the pawn in third person, its equipment on the body sockets. Nothing on the server reads it (the
server's interaction range check keeps the third-person range). It used to replicate from the server, which never
received a client's change; a listen-server host in first person sent its mode to the clients, which attached its
weapon to arms they do not draw. Test `VoxelCharacter.ViewMode.NotReplicated`. A system that cares about the view mode
joins this sequence, on the local machine.

## Input

Enhanced Input only. `UVCInputConfig` holds the actions (`IA_Move`, `IA_Look`, `IA_Jump`, `IA_Interact`,
`IA_PrimaryAction`, `IA_SecondaryAction`, `IA_ToggleView`, `IA_OpenInventory`, `IA_OpenMap`, `IA_HotbarSlot`,
`IA_ScrollHotbar`, `IA_Drop`, `IA_Use` (F), `IA_Sheathe` (R), `IA_CycleWeapon` (X), `IA_ToggleEditMode` (B), `IA_Craft` (C), `IA_Sprint` (Shift), `IA_Crouch`
(Ctrl), `IA_Pause` (P)) and two mapping contexts: `IMC_Gameplay` at priority 0, always on, and `IMC_UI` at 1 while an
inventory or menu is open (cursor shown). The asset is the controller's `InputConfig`; the character binds the actions,
the controller adds the contexts. The secondary action is bound on its release too (C3): held with a ranged weapon in
hand it aims ("The bow" › Aiming), held with a shield in the off hand it guards ("GAS" › Damage pipeline, Block), in edit
mode it places. `PressSecondaryAction` / `ReleaseSecondaryAction` run it for scripts.

### The primary action

Primary and secondary actions (LMB / RMB) go through one chain; new action types insert into it, they do not replace
it. Dead or downed characters ignore both; the primary action waits while a weapon is being unsheathed or sheathed, and
does nothing while the guard is up (D94).
"The main hand" below is the held item: the hand slot's own or a weapon drawn from a sheath.

1. A ranged weapon in the main hand (the code's step 0): `TryStartRangedAttack` on the press, arrows or not ("No
   arrows" as a toast and no draw without one), and `ReleaseRangedAttack` when the input ends (`Input_StopPrimaryAction`,
   completed or cancelled).
2. A hostile damageable under the crosshair within `AttackTargetRange`: `TryStartMeleeAttack`.
3. Chop: a harvestable scatter tree near `GetChopAimPoint()` (`TryChopScatter`), then, in a cubic world outside edit
   mode, a trunk block of a standing voxel tree (`TryChopVoxelTree`).
4. Outside edit mode with no digging tool, a melee weapon in the main hand (`UVCMeleeAttackAbility::HoldsMeleeWeapon`: a
   Weapon fragment and no Ranged Weapon fragment) swings at the air (`StartMeleeSwing`, G174): the same swing as step 2,
   and the server's sweep at the hit time lands on whatever is in reach then. Bare hands do nothing here.
5. Dig or place: with edit mode on, or a digging tool in the main hand for a dig (`HasDiggingTool`: `GetMiningSpeed() >
   1`). Unarmed digging and placing need edit mode.

The input handlers call `PressPrimaryAction` and `ReleasePrimaryAction` (BlueprintCallable), which a script can call on
the local player's pawn too: Enhanced Input injected from the editor's Python does not reach a PIE player.

Edit mode (`bEditModeEnabled`, off by default) is local input state: B, `SetEditModeEnabled`, or the game module's
`vox.EditMode`. It drives the `EDIT` cue on the vitals widget and mirrors into VoxelWorlds' `voxel.Edit.PlayerInputs`
(looked up by name, no dependency): 1 lets the demo's `AVoxelDemoActor` brush follow the same key, 2 in a cubic world
keeps only its keys. The server validates every request whatever the flag says.

## Voxel editing

Every edit action takes its voxels from `AVCCharacterBase::FindEditTarget` (the crosshair trace, then
`GetEditVoxelCoord`): a place fills the voxel half a voxel in front of the hit face; in a cubic world a dig takes the
block half a voxel behind it (KC5), in a smooth world the voxel containing the impact point. Clients send
`Server_RequestVoxelModification`; the server checks reach (`IsVoxelInEditReach`, `EditReach` 800 uu to the voxel's
corner), refuses a place whose voxel cube overlaps any pawn (`WouldPlaceTrapPawn`; the player gets "Too close to place a
block") and edits: a cubic world one full-strength cube brush per block, a smooth world the density brushes. A dig that
removed voxels pays the gather table (`GatherFromVoxel`).

**Block feedback and timed breaking** (cubic worlds; D31-D34). `UVCBlockInteractionComponent`
(`AVCCharacterBase::BlockInteraction`) on the locally controlled player outlines the block a dig would break and draws
a ghost of the block a place would add, red where the server would refuse it, with its own `ULineBatchComponent`.
A dig is a hold: the press starts `StartHold`, the release `StopHold`; the owning client sends
`Server_BeginBlockBreak(voxel)` for each target and `Server_StopBlockBreak` on release, and draws cracks from its own
prediction (`BuildCrackLines`). The server keeps one break per player, checks it as a dig (reach, a solid voxel,
`UVoxelEditManager::CanEditAt`), advances it by elapsed time over the material's hardness
(`FVoxelMaterialRegistry::GetHardness`) divided by `MiningSpeed`, and digs through `DigVoxelAt` when it completes
(`Client_BlockBreakFinished`, `OnBlockBroken`). `Server_RequestVoxelModification(Destroy)` is refused while breaks are
timed. Smooth worlds dig at once. Voxel edits are not replicated to clients (VoxelWorlds' edit layer), so a remote
client sees its cracks but not the dug block.

**The ghost after a dig** (G75, closed 2026-11-02). Once a dig empties a block, the trace through its cell lands on the
block behind it, whose place cell is the dug one, so the ghost used to move into the hole at once: a pale block where
the dug one was, and a one-block dig read as not having happened. Now `NoteDug` (from `DigVoxelAt` for this machine's
own player, from `Client_BlockBreakFinished` for a remote one) holds the ghost off the dug cell until the aim moves:
the camera turns more than `vc.Dig.GhostRearmDegrees` (2) or moves more than a quarter of a block from where it was at
the dig (`HasAimMoved`), or the cell is solid again. The ghost is also held off while the trace still hits the dug
block's collision, for the frames until it is rebuilt (`FVCBlockTargets::HitVoxel`), where it would hang in front of
the hole. The outline moves to the block behind as before, and the place is not held back: the secondary action fills
the hole whether the ghost shows or not. A held dig goes on to the next block with the ghost hidden by the break and
then by the hold-off. Smooth worlds draw no ghost (D32), so nothing changes there. In first person the dug cell can
still sit under the held tool's head; that is the view model's framing, left as it is (judge a dig from above, or turn
the camera a little).

## Spawn and the terrain wait

A pawn spawns frozen (`FreezeForTerrainWait`: no movement, no collision) until the ground under it has collision.

1. `InitiateChunkBasedWait` corrects the position first with `FVCTerrainQueryHelper::FindSpawnablePosition`: the
   generated surface (`UVoxelChunkManager::GetGeneratedSurfacePoint`, conditioning zones included, no chunk needed); if
   it is under the sea, rings a chunk apart in eight directions out to `MaxSearchRadius` (50,000 uu)
   (`FindPlaneSpawnPoint`), only inside the world's bounds; with `bSearchWholeWorld` it goes on past the ring
   (`FindPlaneSpawnPointAtScale`, WM-MP10). On a planet the search is by direction (`FindPlanetSpawnPoint`, then
   `FindPlanetSpawnPointAtScale` over the whole planet, G36).
2. It requests collision for the chunks around the pawn and waits for them (`OnChunkCollisionReady`), at most
   `TerrainWaitTimeout` (60 s). The chunks are `FVCTerrainQueryHelper::GetTerrainWaitChunks`: a grid of
   `TerrainWaitChunkRadius` chunks either way in the tangent plane at the pawn's chunk layer (the X/Y grid on a heightmap
   world), and the chunk half a chunk under it along local down, in every kind of world (G84, 2026-11-02: a heightmap
   pawn in the lower half of its chunk stands on the layer below and was released before that chunk had collision).
   Test `VoxelCharacter.Terrain.WaitChunkUnderFeet`.
3. `PlaceOnTerrainAndResume` traces along local up for the surface, stands the pawn upright and restores movement.

`AVCPlayerState::PendingSpawnTransform` (a loaded save) puts a player back where they stood; the wait then settles the
height. `AVCGameModeBase` parks the world-item pool at the planet centre once the voxel world is up.

**The start trace (G110).** `log LogVoxelCharacter Verbose` traces each player's start in `AVCGameModeBase`: before and
after `HandleStartingNewPlayer`, at `RestartPlayer` and at the player start `SpawnDefaultPawnFor` spawns at, one line
of `DescribeSpawnState` (the controller's class, local flag and state, the player state's spectator flags,
`CanRestartPlayer` and `HasClientLoadedCurrentWorld`, `MustSpectate`, `bStartPlayersAsSpectators`, the pawn or
spectator it holds, its start spot, the pawn and controller classes). Whatever the verbosity, a Warning with that line
marks a starting player left without a pawn although it should have one, a failed restart (`FailedToRestartPlayer`)
and any player controller still without a pawn 5 s into play (`CheckPlayersHavePawns`, not counting a pending
respawn). G110, a PIE start on a planet that once came up with a spectator and no pawn, is open until one of these
lines catches it (`Documentation/FINDINGS.md`).

**Leaving the world.** `TickWorldSafety` records the last safe ground (`GetLastSafeLocation`). Falling out of the world
(KillZ, `OutOfWorldDepth` below the terrain, on a planet that depth inside the shell's inner radius: `IsOutOfWorld`)
runs `FellOutOfWorld`; `RecoverFromMissingTerrain` puts a pawn whose terrain never came at a recovery location
(`ResolveRecoveryLocation`).

## Surface gameplay

- **Gathering.** `UVCGatherTable` (`AVCPlayerController::GatherTable`) maps a voxel material to an item, with a base and
  a tool yield (`ToolMiningSpeedThreshold` 1.5). A dig that removed voxels creates the item, adds it or drops it at the
  feet, and toasts "+N Wood" (`Client_NotifyGathered`).
- **Chopping.** A scatter tree: `TryChopScatter` asks the local `UVoxelScatterManager::HarvestScatterNear` within
  `ChopSearchRadius` of the aim point; `Server_ChopScatter` -> `ChopScatterAt` checks again, pays the table's
  `HarvestEntries` per hit (tools double it) and after `HitsToRemove` hits `Multicast_ScatterHarvested` removes the
  instance on every machine (scatter is local). A voxel tree in a cubic world (D30): `Server_ChopVoxelTree` ->
  `ChopVoxelTreeAt` checks reach and the tree (`IsVoxelTreeStanding`), pays the `VoxelTreeHarvestCategory` entry and at
  `HitsToRemove` fells it (`UVoxelChunkManager::FellVoxelTree`: one player edit outside the undo history, saved with the
  edit layer). No edit mode or tool needed for either.
- **Placing.** `UseItemInSlot` handles `UItemFragment_Placeable` before consumables: `TryPlaceItem` traces the ground
  `PlaceDistance` ahead, rejects steep ground in the gravity frame, spawns the actor on the authority (tagged `Placed`
  for the save) and consumes the item.
- **Consumables.** `IA_Use` -> `RequestUseActiveItem` -> `Server_UseItemInSlot`: a Consumable fragment, a per-definition
  cooldown, `AttributeChanges` as an instant effect, `ConsumeEffect` / `ConsumeAbility`, one removed when
  `bConsumeOnUse`.
- **Rest and crafting.** `RequestRest` / `RequestSleep` / `RequestCraft` route to `Server_RestAt` / `Server_SleepAt` /
  `Server_CraftRecipe`; the server checks `IsRestPointInRange` (an `ICGFRestPointInterface` actor within interaction
  range + 100 uu) and calls the interface, which owns healing, the respawn point and the clock. Results return as
  `Client_CraftResult` toasts. `UVCCraftingPanelWidget` lists a station's recipes (`UCraftingSubsystem`);
  `UVCCampsiteWidget` adds Rest and Sleep. C opens hand crafting (`ToggleCraftingUI`); a campsite interaction opens its
  panel (`Client_OpenCampsite`).
- **Carried light.** The character implements `ICGFLightBearerInterface` over the equipment manager's carried-light API
  (`GetCarriedLightLevel`, `ConsumeCarriedLightFuel`, `OnCarriedLightChanged`); the vitals widget shows "Torch M:SS",
  and a burnt-out light toasts through `Client_LightBurntOut`.

## Items, progression and saves

- **Items across death.** Inventory and equipment are components of the pawn (`InventoryComponent`,
  `EquipmentManager`) and die with it; `HandleDied` captures them to the player state as `FVCItemSnapshot` (the storage
  subsystem's item JSON), and the next avatar's `PossessedBy` restores them by slot and equips them again
  (`TryEquipFromInventory`). Server only.
- **Callables.** `EquipHotbarItem(slot)`, `UnequipSlotToInventory(tag)`, `RequestUseActiveItem()`.
- **Carried weapons** (the equipment sheathing plan, 2026-11-08, D77-D82; EquipmentPlugin's "Sheaths"). The character
  has three sheaths beside its four slots, in the cycle's order: `Equipment.Slot.Sheath.Belt` (`pelvis`, the
  pickaxes), `.HipLeft` (`pelvis`, the swords), `.Back` (`spine_05`, the bow), each drawn into the main hand. A weapon
  lives in its sheath and is drawn by the slot's replicated state; everything that asks what the player wields asks
  the hand (`GetHeldItem(MainHand)`): the action set (all sheathed is `Unarmed`), the primary chain, digging (a belted
  pickaxe grants no mining speed: a sheath's GAS applies while drawn), the drawn grip.
  - **Asking.** R (`IA_Sheathe`): sheathe the drawn weapon, or draw the last one drawn (else the first carried). X
    (`IA_CycleWeapon`): sheathe it and draw the next carried one. F on a weapon in the bag (`UseItemInSlot`, on the
    server): into its sheath (the sheath's weapon to the bag), then drawn with its montage, the weapon drawn before
    back to its own sheath. `RequestDrawWeapon(sheath)`, `RequestSheatheWeapon()`, `RequestToggleSheathe()`,
    `RequestCycleWeapon()`, one predicate on the owner and the server (`CanSheathAction`, D-CR16). No auto-sheathe.
  - **The sequence** (`VCCharacterBase_Sheath.cpp`). `RunSheathSequence`: the drawn weapon's `Sheathe`, then after it
    the wanted one's `Unsheathe`. The owner plays both at input; the server plays them to everyone else
    (`PlaySheathActionForEveryone`, the owner skipping, D49), sheathes at once (the weapon leaves the hand's use) and
    draws at the unsheathe's hand-off, timed from the montage asset (`UVCActionAnimSet::GetHandOffTime`, its
    `UVCAnimNotify_ItemHandOff`, else 40% of it; a dedicated server plays nothing). Dead or downed before the hand-off:
    still sheathed. The owner ignores the primary action while either montage plays. Both montages play in the main
    graphs' `UpperBodyAction` slot ("Animation" › Layers; G162), so a draw or a sheathe on the move keeps the legs
    walking or running.
  - **The visual's hand-off.** On every machine `UpdateEquipmentAttachments` seats a sheath's weapon where its montage
    has it until the hand-off (at the sheath while an unsheathe reaches for it, in the hand while a sheathe carries it
    back), else where the state says (`GetVisualAttachSocketForState` / `GetVisualAttachOffsetForState`); in the hand
    it takes the main hand's view mapping, and the bow's drawn grip as before. `UpdateSheathHandOff` re-seats each tick
    while a hand-off is pending and once after; `OnDrawnChanged` re-seats on a replicated change.
  - **Saves.** `FVCSnapshotEquipmentEntry::bDrawn`: a drawn weapon comes back drawn (no montage); an old save's
    main-hand weapon lands in its sheath, drawn (EquipmentPlugin maps it).
  - **NPCs** keep `HeldItem` in the hand (no sheaths); drawing on aggro is not built.
- **Progression.** `FVCProgressionStats { DungeonsCleared, BossesKilled }` replicated on `AVCPlayerState`
  (`AddDungeonCleared(bBossKilled)` on the authority, `OnProgressionChanged`). Layers above (VoxelWorldPOI's dungeon
  objective) push objective text to the controller; this plugin knows nothing of dungeons.
- **Saves.** `AVCPlayerState::ExportSaveState` / `ImportSaveState(FVCPlayerSaveState)`: items, progression, rest point,
  last transform, vitals. `GetSaveKey()` strips the Null subsystem's per-session GUID, so a PIE player keeps one key.
  The engine drops the pawn before `Logout`, so the controller calls `CaptureAvatarState()` on unpossess and
  `PawnLeavingGame`; `AVCGameModeBase` broadcasts `OnPlayerLoggingIn` (before the first pawn) and `OnPlayerLoggingOut`.

## NPCs

`AVCNPCCharacterBase` defaults to `AVCNPCAIController` (`AutoPossessAI = PlacedInWorldOrSpawned`), walks at 300 uu/s on
`UVCMovementComponent`, and topples when it dies without a physics asset (`ToppleSeconds`). `bAIEnabled` false makes a
training dummy (`vox.SpawnDummy`).

The controller is a server-only state machine (`EVCNPCAIState`: idle, patrol, wander, chase, attack, return, flee, and
hold, keeping its distance from a lit torch it fears), no Behavior Tree and no navmesh. Perception is a Pawn-channel sphere overlap within `SightRadius` plus a Visibility line of sight; a lit
`ICGFLightBearerInterface` target is seen out to `SightRadius * LitTargetSightMultiplier` (1.5). Attacks activate
`Ability.Attack.Melee` on `AttackCooldown`. `DecideState` is a pure function over `FVCNPCAIDecisionInput` (tested).
Behaviour (`EVCNPCBehavior`: Hostile or Prey) and temperament (`EVCNPCTemperament`: Aggressive or Wary) live on the
pawn with `WanderRadius`, `PackRadius` and `Faction`; a hostile entering Chase alerts its pack. Movement follows waypoints
from an `IVCPathProvider`: `UVCVoxelPathProvider` over `UVoxelSurfaceNavigationSubsystem` by default
(`bUseVoxelNavigation`), a dungeon's grid provider in dungeons, straight lines without one. A stuck walker blocks the
cells ahead for 8 s and plans again. `SetPatrolRoute` gives a loop; without one an NPC guards `SetLeashOrigin`.
`vc.AI.Enabled 0` freezes every NPC; `SetAIEnabled(false)` one. `SetWeatherHold(true)` (the weather plan's P5; the
surface spawner sets it in a storm for the entries that hide in them) turns a prey's roaming and patrolling into Idle
until it is lifted, a threat still making it flee; hostiles ignore it (`FVCNPCAIDecisionInput::bHoldsForWeather`,
`VoxelCharacter.AI.WeatherHold`).

### Bodies (the plan's P7)

Every NPC animates a skeletal body on its own `Mesh`. The static placeholders are gone; each run of
`Scripts/configure_surface_npcs.py` had stacked another on the creatures (G146).

- **The dungeon foe** wears the CR pack's Grian04 (on the Mannequin skeleton) and plays `ABP_VoxelCharacter` with the
  player's layers and action sets (its `ActionMontages` lists Unarmed and SwordShield). `HeldItem` names the item an NPC
  holds as the player would: its Equipment fragment's mesh on the hand (the fragment's bone, else `hand_r`, at its
  attach offset), its stance layer linked and its action set the NPC's unless `ActionSet` names one. The foe holds the
  iron sword in the sword-and-shield stance. `Scripts/configure_dungeon_enemies.py` builds it.
- **The animals** (the Animal Variety Pack, a content pack: the wolf and the doe) share `ABP_VCCreature`, a template
  AnimBP bound to no skeleton, on `UVCAnimInstance`. It plays the body's locomotion blend space by speed at
  `LocomotionPlayRate` (quicker past the run clip's own speed), an idle variant while the body stands (`bIdleVariant`:
  after the set's `IdleVariantAfter` seconds, for its `IdleVariantFor`, again after the same wait; the doe grazes, the
  wolf looks round), then the montage slot. The NPC's `UVCLocomotionAnimSet` names those assets, through
  `IVCAnimatedCharacter::GetAnimLocomotionSet`. The animal's `UVCActionAnimSet`, its Unarmed set, holds the bite (it
  lands at the montage's hit time), the hit reacts and the death. `Scripts/setup_creature_animation.py` builds the
  template, the blend spaces (idle, walk and run at the clips' own speeds: the wolf's 93 and 445 uu/s, the doe's 108 and
  574), the montages and the sets; `configure_surface_npcs.py` puts them on `BP_Wolf` and `BP_Deer`. A new animal is one
  entry in the first script and its creature Blueprint. The capsules fit the bodies (half-heights: the wolf 52, the doe
  78, the foe 94).
- **The budget.** An NPC's mesh is `USkeletalMeshComponentBudgeted`: the engine's Animation Budget Allocator ticks the
  NPCs' animation within `a.Budget.BudgetMs` of game thread (0.5, `Config/DefaultEngine.ini`). The less significant
  meshes (by distance to the view) tick less often and are interpolated between; one off screen does not tick. A world's
  allocator starts disabled, so the first NPC to play turns it on (`a.Budget.Enabled` is only a gate over every world's).
  A living NPC's mesh has no collision (the capsule is its collision; arrows and the melee sweep meet the capsule) and
  takes its bounds from the mesh's own: the physics asset's bodies would otherwise follow the pose every frame. A death
  that ragdolls gives back both.
- **Cost**, 48 wolves standing in view about 15 m away (the CSV profiler): with mesh collision 7.1 ms of game-thread
  animation; with none 3.0; with fixed bounds too 2.5; with the allocator at work, the NPCs' budgeted animation 1.1 ms at
  a 1 ms budget (every mesh ticked) and 0.52 ms at 0.5 (23 of the 48 a frame). `npc_bench.py` (wolves chasing a moving
  player, the game thread's median): at 48 NPCs 20.9 ms on `VoxelDemo` and 18.9 on the cubic world, against 29.6 and 24.1
  with the static placeholders; at 96, 24.1 and 19.2 against 43.0 and 27.3.

### NPCs at scale (G73, 2026-11-04)

What an NPC costs the game thread, and the three things that keep it down (three more for a player on the move: G125
below):

- **Perception at a random phase.** An NPC's first think falls at a random point of `PerceptionInterval` (0.25 s), so a
  group spawned together does not run its overlaps, line-of-sight traces and decisions in one frame every interval (96
  spawned at once: AI P99 7.8 → 4.4 ms).
- **No search again right after a partial route.** When the surface provider's route ends short of its goal
  (`UVCVoxelPathProvider::WasLastPathPartial`: out of reach, the search's budget or its sampling cap), the walker follows
  it and searches again after `PartialRepathSeconds` (1 s) at the earliest. A wolf that could not reach the player used
  to search again every time it reached the end of a partial route, each search the full budget.
- **Distant movement at a lower rate.** Once a second the controller sets its pawn's movement tick interval from the
  nearest player: every frame within `vc.AI.FarTickDistance` (4,000 uu), every `vc.AI.FarTickInterval` (0.05 s) beyond
  it, twice that past twice the distance; every frame while not on the ground. The movement integrates the longer step,
  so routes and speeds are unchanged; only how often a distant NPC's position updates.

`FVCNPCCost` counts the AI ticks, the path requests inside them and the movement ticks of pawns no player controls; the
game module's `vox.NPCBench` reads it per frame (`Scripts/qa/npc_bench.py`, `npc_bench_table.py`). Two scenarios at the
bench site (25000, 12000), each count in its own 20 s run after 3 s of warm-up, the before and after in one build
(temporary switches, adjacent runs), the means of two rounds, editor PIE at 2,874 × 1,358:

| NPCs (ambient: 36-84 m out, mostly wandering) | VoxelDemo: NPC ms / frame | Movement | Review_Cubic: NPC ms / frame |
|---|---|---|---|
| 12 | 1.12 → 0.78 | 0.98 → 0.64 | 1.01 → 0.68 |
| 24 | 1.85 → 1.32 | 1.62 → 1.11 | 1.56 → 1.13 |
| 48 | 3.47 → 2.40 | 3.05 → 2.01 | 2.86 → 2.26 |
| 96 | 6.75 → 4.51 | 5.79 → 3.60 | 5.98 → 4.21 |

Movement is about 85% of an NPC's cost (`UVCMovementComponent` sweeping against the voxel collision, 60-80 µs a pawn a
frame at every-frame rate), the AI about 10-15%, path searches 1-3% on average. The searches owned the hitches: in the
chase scenario (N wolves 4-21 m out chasing a player driven round a 12 m circle) the longest path request at 96 wolves
on the block world was 197-206 ms before (a full-budget search over cold block columns to a player the wolves could not
reach) and 1-5 ms after; the game thread's P99 there went from 51-73 ms to 32-35 ms. Navigation's own numbers:
VoxelWorlds `SURFACE_NAVIGATION.md`, "The search (G73)" and "Costs".

**On the move (G125, 2026-11-05).** A player crossing the land fast (the streaming bench's planet flight, 1,500 uu/s)
keeps a fresh population about it: at the demo table's density (VoxelWorldPOI `POI_SYSTEM.md` §12) about 9 creatures
alive, most 40-80 m out in the 20 Hz band. A movement tick there cost 120-190 µs: the engine's capsule sweeps against the
voxel trimesh (the floor sweep alone about 40 µs a call), our own pre-tick work 10-30 µs. Three changes:

- **One step a slow tick.** The movement cuts a tick longer than `MaxSimulationTimeStep` (0.05 s) into steps, each
  moving the capsule and sweeping for the floor, so a pawn ticked every 0.1 s did the work of two or three every-frame
  ticks and the far rate saved little. While a pawn ticks slowly its step spans the tick (the interval and a frame, at
  most 0.15 s); every frame its own step is back. Floor sweeps a tick: 1.3-1.7 → 1.0.
- **Resting at a slow rate.** A pawn on the ground with no route, idle or between wander legs (`WanderWaitMin`-`Max`),
  with no velocity, acceleration or launch pending, ticks its movement every `vc.AI.RestTickInterval` (0.5 s) wherever the
  players are: its tick only held it on its floor. The controller decides after its state's work, so a route, a threat or
  a hit found in a frame wakes the movement in the same frame (`SetComponentTickIntervalAndCooldown`). Resting ticks:
  21-37% of all movement ticks → 4-9%. `AVCNPCAIController::MovementTickInterval` is the decision (a pure function).
- **Chase searches at most every `ChaseRepathSeconds` (0.5 s).** A chaser searched again whenever its target was
  `RepathDistance` from the route's goal: a wolf after a player sprinting or flying past searched several times a second.
  Now one whose target moved waits `ChaseRepathSeconds` since its last such search (its route still leads where the
  target was); one whose route ran out searches at once. Chase searches on the flight: 125-299 a run → 35.

The flight (`Scripts/qa/npc_flight.py`: `PlanetTest`'s great circle with `vox.NPCBench observe` beside it, the clock at
noon), the change behind temporary switches in one build, three interleaved rounds; the NPCs' cost (movement, AI, path
requests, the spawner) in milliseconds per second of play, which compares runs the machine slowed with quick ones:

| Density (the table's) | NPCs alive | NPC ms / s | Movement ms / s | Movement ticks / NPC / s | Game thread P50 (ms) |
|---|---|---|---|---|---|
| 1x (0.333) | 4.1 | 14.4 → 11.8 | 7.7 → 6.1 | 12.4 → 9.9 | 23.6 → 21.3 |
| 3x (1, the demo's) | 9.2 | 37.0 → 32.7 | 21.7 → 18.0 | 20.8 → 15.9 | 22.7 → 23.0 |

The game thread's P50 at 3x is within the runs' noise of 1x's (19-26 ms either way); what is left of 3x's cost is per
creature (2.25 times as many alive), not the spawner's churn. The next lever is `vc.AI.FarTickInterval`: at 0.1 the flight's
movement at 3x is 19.0 → 14.4 ms/s (NPCs 34.7 → 29.0), at the price of a distant creature's position updating 10 times a
second (5 past 80 m), so it stays 0.05.

## HUD and widgets

`AVCPlayerController` owns every player-facing widget; the widget classes come from the gameplay plugins
(`UHotbarWidget` in ItemInventoryPlugin) or this one, each a `TSubclassOf<UUserWidget>` property defaulting to the C++
class.

- Persistent (`CreatePersistentWidgets`): the hotbar, the interaction prompt, `UVCVitalsWidget` (health, stamina,
  breath, the `EDIT` cue, the torch, an objective line and a timed toast), the minimap, the aim reticle
  (`UVCReticleWidget`, centre-anchored: a dot and four ticks, shown while the pawn aims (`IsAiming`), the ticks closing
  with the owner's draw and the colour red over a hostile; C3), `UVCCarriedWeaponsWidget` just above the hotbar (one
  icon per carried weapon in the sheaths' order, the drawn one lit blue, the R / X hint; display only, rebound with the
  vitals to each possessed character; the equipment sheathing plan), and an optional clock
  (`ClockWidgetClass`; the demo uses WorldClockPlugin's widget, this plugin never reads the time).
- On demand: the inventory and equipment panels, the item cursor, the world map (`ToggleWorldMapUI`), the pause menu
  (`IA_Pause`), the crafting panel and the campsite panel.
- Place widgets with anchors, alignment and padding; never `SetPositionInViewport` on a bottom-anchored widget. Check the
  HUD with `Shot showui` (plain `Shot` and Claudius' `pie_screenshot` omit UMG).

**Click-to-move** (`VCPlayerController_Items.cpp`). The held source (`EVCHeldSource`: None, Inventory, Equipment)
decides the operation: inventory to inventory `TrySwapSlots`; inventory to equipment `TryEquipFromInventory`;
equipment to inventory `TryUnequipToInventory`, then a swap to the clicked slot; equipment to equipment, the same
slot or a right click cancel. Slots under 9 are the hotbar, the rest the panel. `BindSlotClickDelegates` runs once,
after both panels exist (`bSlotDelegatesBound`). `CancelHeldState` clears the right visual and hides the cursor.

**Maps.** The minimap and world map, the planet's tangent-plane minimap and globe, the marker registry and the
minimap's refill rule (G64): `MAP_UI_WIDGETS.md`.

## Multiplayer

| Data | Authority | Replication |
|---|---|---|
| Movement | Server (character movement) | Built-in prediction; sprint and swim-rise as saved-move flags; planet gravity recomputed on both sides |
| Voxel edits, block breaks | Server (`Server_RequestVoxelModification`, `Server_BeginBlockBreak`) | Edits are not replicated (VoxelWorlds); the client predicts cracks only |
| Inventory, equipment | Server (the gameplay plugins' components on the pawn) | Their own replication |
| Attributes, effects, abilities | Server (ASC on the player state; an NPC's on the NPC) | GAS replication; prediction keys for activation |
| Progression | Server | `Progression` on the player state |
| View mode | The local player's machine | Not replicated (G87); other machines draw third person |
| Action set | Server (the equipment) | Derived on every machine from the replicated equipment slots |
| Appearance | Server (`AVCPlayerState::ResolveAppearance`: the player's choice, the game mode's, the settings') | `Appearance` on the player state; every machine dresses the pawn; a dedicated server sets the body mesh only |
| Action montages | The owner at input; the server for what it accepted | One unreliable multicast, skipped by the owner for its own actions (D49); the melee damage on the server at the montage's hit time; a draw let go too early stopped by a reliable multicast |
| The bow's draw and shot | Server (`UVCRangedAttackAbility`: the draw's time, the arrow used, the spawn) | The activation and the input release over the ability system component, in order; the owner's montages at input, the others' from the server |
| Arrows | Server (spawned there, no client prediction, D54) | A replicated actor: its flight, mesh and stuck state; hits and damage on the server |
| Facing while drawn | The owner and the server alike (`SetFacesAim`) | Through the movement: the server simulates the same turn from the client's moves |
| Camera, input, edit mode, debug flight | Local | Not replicated |

## Console variables

`vc.*` groups: `vc.AI.*` (NPC AI), `vc.Anim.*` (the IK, pace and motion-matching switches, the trajectory tuning, the debug draw), `vc.Dig.*` (the cubic break times' scale, the highlight), `vc.Minimap.*` (the
minimap's refill stats), `vc.Move.*` (the player's movement table: the preset, one value, the print), `vc.UI.*` (NPC health bars). The Exec functions `GiveItem`, `SpawnWorldItem`
and `ToggleVoxelDebug` are on the controller and the character. Every name, default and meaning:
`Documentation/EDITOR_CONSOLE_GUIDE.md` §4 at the project root. The `vox.*` QA commands that drive this plugin's pawn
(`vox.TP`, `vox.AutoRun`, `vox.Fly`, `vox.Mine`, `vox.Chop`, `vox.EditMode`, `vox.Shoot`, ...) live in the game module.

## Tests

`VoxelCharacter.*` in `Private/Tests/`: AI, Anim, Camera, Combat, Config, Cubic, Edit, EditMode, EquipmentLoop, Gather,
Island, Map, Plane, Planet, Progression, Respawn, Save, Terrain, ViewMode, WorldEdge; the weather's `Movement.WindPush`,
`Movement.WetLevel` and `AI.WeatherHold`; the bodies' `Appearance.*` (P6), `Anim.CreatureLocomotion` and
`NPC.BodyBudgetAndHeldItem` (P7); the gait table's `Movement.GaitTable`, `Movement.GaitTableApplied` and
`Movement.AimTurnEase` (the character refinement plan's C4), `Appearance.AttachCorrection` (C1), `Anim.StanceWeight` (C2)
and `Combat.AimCameraLocation` (C3); the bow's `Combat.LaunchAlongAimRay` and `Combat.Arrow.WindRule` (D83), the swing at the air's `Combat.Melee.HoldsMeleeWeapon` (G174), the guard's `Combat.Block.Rules` and
`Combat.Block.GuardTakesFrontHits` (D94), `Anim.AimRaise` (D76) and `Anim.StanceFade` (D85); the sheaths'
`EquipmentLoop.SheathSlots`, `SheathActionSet` (the set follows the drawn weapon, R and X, the snapshot's drawn state),
`SheathSaveEntry`, `SheathHandOffTime` and `SheathCarriedStrip` (the equipment sheathing plan's S2-S4); the state
machine's `Locomotion.ClipChoice`, `StanceWeight`, `SetChange` (L1), `States`, `Pace`, `Transitions`, `Instance` and
`Movement.GaitClipCaps` (L2). The feel is judged by hand on `/Game/PluginTesting/Tests/Character/CharacterTest`
(the plan's C0; `Scripts/setup_character_test.py` builds it: a flat floor with a measured lane, block steps and a ramp,
a shooting range and a kit that hands out every held item): the game module's `vox.Lane` runs the lane hands-free with
the foot slide per leg, `vox.Stop` measures a stop, `vox.FootSlide` the planted feet's drift; the QA rows are area F13.
