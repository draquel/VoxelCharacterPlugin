# Ranged Combat Plan: the Bow

> **Agreed 2026-11-06: the user approved the plan as drafted, every decision as recommended (D52-D57, §6). B1-B3 done;
> B4 next.**
> Scope: a bow the player draws, aims and shoots, its
> arrows as items, and the arrow's flight on a plane and on a planet. Owner: VoxelCharacterPlugin; one field each in
> ItemInventoryPlugin and EquipmentPlugin; content and item data in the parent. It follows the character animation plan's
> P3 (`CHARACTER_ANIMATION_PLAN.md`), which left the Bow set waiting for a bow and a ranged attack. Terms:
> `Documentation/GLOSSARY.md` at the project root.

## 1. What exists

| Piece | State |
|---|---|
| Combat | Melee only: `UVCMeleeAttackAbility`, server-only, a sweep along the view, damage at the montage's hit time (P3). Factions, the damage pipeline (`UVCCombatStatics::ApplyDamageToActor`), crits and weapon wear are shared code a ranged attack can call |
| Animation | P3's montage component and action sets; the anim instance already computes `AimPitch` and `AimYaw` in the gravity frame; the stance layer `UpperBodyStance` takes an item's `AnimLayerClass` |
| Input | The primary action has a press (`Input_PrimaryAction`) and a release (`Input_StopPrimaryAction`, used by the cubic block break's hold) |
| Equipment | The main hand attaches to `hand_r`, the off hand to `hand_l`; an item's fragment can fit its mesh (`AttachOffset`) but not choose another bone. The action set, the melee damage and the weapon wear all read the main hand |
| Inventory | Stacks, `FindItemsByTag`, `TryRemoveItem(id, count)`: enough to use up an arrow a shot, no change needed |
| Gravity | `UVCMovementComponent::ComputePlanetGravityDirection` gives the radial gravity a body anywhere on a planet feels |

**The content is in the project** (no download): Essential Bow and Arrow (space.bar.anim) has the full cycle on the UE5
Mannequin: `Draw_A_Begin` / `Draw_A_Loop`, `Aim_A_Begin` / `Aim_A_Loop`, `Release_A`, `Reload_Back` / `_Hip` / `_Side`,
three bow idles (A, B, C), the aim offsets `AO_Stand_A`, `AO_Draw_A`, `AO_Aim_A`, equip and sheathe, directional hurts,
deaths and get-ups, plus pickups and potion drinking. Meshes: Fab's LowPolyRPGWeapons (ten bows, five arrows, five
quivers), and the pack's own bow (`SM_Bow`, skeletal, its string a bone) and `SM_Arrow`.

## 2. Principles

1. **The server shoots.** As with melee (D49): the owner plays the draw and the release at once; the server measures the
   draw, uses up the arrow, spawns the arrow and applies the damage. A client cannot fire without an arrow or faster than
   its draw.
2. **The arrow falls along local gravity.** On a planet that is toward the centre, re-read each step of the flight; on a
   plane it is the engine's. The engine's projectile movement assumes world down; ours overrides its acceleration.
3. **The bow is the main-hand weapon.** Everything that asks "what is the player wielding" (the action set, the attack,
   the wear) keeps reading the main hand; the bow only attaches to the other bone (D52).
4. **Data, not code, for the bow's feel.** Draw times, arrow speed and damage live on the item; montages and the stance in
   the action set and its layer, built by `setup_character_layers.py` like P3's.

## 3. Design

- **Holding (D52).** `UItemFragment_Equipment` (ItemInventoryPlugin) gains `AttachBoneOverride`; `UEquipmentManagerComponent`
  (EquipmentPlugin) attaches to it when set. The bow sits in the main-hand slot on `hand_l`.
- **The ranged weapon's data.** A new fragment, `UItemFragment_RangedWeapon` (ItemInventoryPlugin): the ammo's tag
  (`Item.Ammo.Arrow`), the arrow's class, its launch speed at full draw, the minimum and full draw times. Damage stays on
  the weapon fragment (`BaseDamage`, crits, damage type), scaled by the draw.
- **The arrows (D55).** `ID_Ammo_Arrow`: stackable, tagged `Item.Ammo.Arrow`; one is used per shot, on the server. Later
  a crafting recipe (wood and stone).
- **The attack (D56).** `UVCRangedAttackAbility`, server-only. Press with the bow held: the owner plays the draw montage
  (`Draw_A_Begin` into the aim loop) and the server's ability starts timing the draw. Release: the owner plays
  `Release_A`; the server checks the draw (shorter than the minimum is a cancelled shot, no arrow used), takes an arrow,
  spawns it and sends the release to the others. Without an arrow the draw does not start (the owner's check and the
  server's). The melee ability is unchanged for every other weapon; the primary-action chain picks the bow first when the
  main hand holds a ranged weapon.
- **The arrow (D54).** `AVCArrowProjectile`, replicated, spawned on the server at the bow toward the crosshair's point (the
  view's trace, so the arrow converges on what the player aims at). `UVCArrowMovementComponent` (a projectile movement
  whose acceleration is gravity along local down). On a pawn: damage through the combat pipeline (faction rules, crits),
  the hit react from the arrow's side (P3's component); on terrain or a prop: it sticks for a while, then goes.
- **Animation.** The Bow action set: draw, aim loop, release, reload, the bow's hurts and deaths, its get-up. The stance
  layer `ABP_VCLayer_Bow`: the bow idle over the upper body, and the aim offset (`AO_Aim_A` while drawn, `AO_Stand_A`
  otherwise) driven by `AimPitch` and `AimYaw`, which the layer reads from the main anim instance. The action-set enum
  gains `Draw`, `Release` and `Reload`.
- **Mesh (D53).** LowPolyRPGWeapons' `SM_Bow_01` and `SM_Accessories_Arrow_01`.
- **First person (D57).** The demo's player has no first-person arms mesh, so first person shows no draw; the crosshair,
  the shot and the arrow work the same. A first-person draw waits for the arms decision (D48's revisit).
- **P3's gaps.** The pack's pickup and potion clips become the Unarmed set's Place and Use montages (a table change in
  `setup_character_layers.py`); VanillaLoop's samples remain only for the mining loop and the downed crawl.

## 4. Phases

### B1: Holding and data (ItemInventoryPlugin, EquipmentPlugin, the parent)
- `AttachBoneOverride` on the Equipment fragment and in the equip path, with a test (an override attaches to its bone,
  none to the slot's).
- `UItemFragment_RangedWeapon`; the items `ID_Weapon_Bow` (main hand, `hand_l`, the Bow set) and `ID_Ammo_Arrow`.
- Docs: `ITEM_INVENTORY_SYSTEM.md`, `EQUIPMENT_SYSTEM.md`.

**B1 result (2026-11-06; ItemInventoryPlugin, EquipmentPlugin, VoxelCharacterPlugin, the parent).**
- `UItemFragment_Equipment::AttachBoneOverride`; EquipmentPlugin attaches the visual (and a torch's flame) to it, drops
  the slot's correction there (`GetVisualAttachOffset` is the item's fit alone), and exposes the rule
  (`ChooseAttachSocket`, `GetVisualAttachSocket`, `GetItemAttachBone`). `AVCCharacterBase::UpdateEquipmentAttachments`
  keeps an item's own bone in both views.
- `UItemFragment_RangedWeapon` with `GetDrawPower` (no shot below the shortest draw, `MinDrawPower` at it, linear to full).
- `Scripts/configure_ranged_items.py` builds `ID_Weapon_Bow` (LowPolyRPGWeapons' `SM_Bow_01` in the main hand on `hand_l`,
  pitched 90 so it stands upright in the fist; 15 damage at full draw, the iron sword's being 18; 150 shots of wear; 6000
  uu/s at full draw over 0.25-1 s) and `ID_Ammo_Arrow` (stacks of 50); the tags `Item.Ammo` and `Item.Ammo.Arrow` are
  config tags (`Config/DefaultGameplayTags.ini`). A second run saves nothing; it and `setup_character_layers.py` now refuse
  to run during PIE, where the editor's asset library does not work (a run in PIE asked to overwrite the bow it could not see).
- Tests: `ItemInventory.Fragments.RangedWeapon.DrawPower`, `Equipment.Visuals.AttachBoneOverride`.
- PIE in `VoxelDemo`: the bow equipped from the hotbar rides `hand_l`, upright, the action set `Equipment.AnimSet.Bow`;
  twenty arrows in the bag. The bow has no layer, montages or attack yet (B3, B4).

### B2: The arrow (VoxelCharacterPlugin)
- `AVCArrowProjectile` and `UVCArrowMovementComponent`: flight, sticking, lifespan, damage on a pawn.
- Tests: on a plane the flight is the ballistic arc; on a planet the acceleration points to the centre at every step and
  an arrow fired level falls toward it; a hit applies the weapon's damage scaled by the draw, and respects factions.

**B2 result (2026-11-06; VoxelCharacterPlugin).**
- `AVCArrowProjectile` (`Combat/VCArrowProjectile.h`): replicated, spawned by the server (`SpawnArrow(Shooter, Class,
  Location, Velocity, Shot, Mesh)`, which B3's attack calls; `Launch` and `SetArrowMesh` underneath), carrying an
  `FVCArrowShot` (the bow's full-draw damage, the draw's power, damage type, crits, the bow's instance). A 4 uu sphere
  (`BlockAllDynamic` without camera and visibility) sweeps and ignores the shooter on every machine. On a pawn or anything
  with a combat component: `ApplyHit` (the shot's damage times the draw, the crit rolled at the hit, the arrow the causer so
  P3's hit react faces it) and the arrow goes. On anything else it sticks, collision off, for `StuckLifetime` (30 s); an
  `IVCDamageReceiver` prop takes the damage first. It rides a movable prop only when every machine knows the prop
  (replicated or loaded with the level): the terrain's per-chunk collision is movable but local, so a stuck arrow stays where
  it is. Unhit it goes after `FlightLifetime` (10 s). A shooter gone before the hit: the faction it had at the launch still
  decides (else the pipeline would take the arrow for a hazard and hurt anyone). The mesh is the ammo's (replicated);
  the component's fit is LowPolyRPGWeapons' (60 uu along +Z, nock to tip): turned onto +X, the tip 8 uu past the sphere.
- `UVCArrowMovementComponent` (`Combat/VCArrowMovementComponent.h`): the engine's projectile movement, gravity replaced on
  a planet by the same pull toward the centre from where the arrow is, read every substep (forced, 1/60 s; the pawns'
  `ComputePlanetGravityDirection`); -Z elsewhere. §5's far-shot risk is answered without a range: in a chunk the collision
  manager has not cooked (`HasCollision` false), an arrow below the generated surface stops at that surface, as a hit on the
  terrain would; arcs through the air beyond the collision fly on.
- Tests (`VoxelCharacter.Combat.Arrow.*`, 6): `GravityRule`; `PlaneArc` (a second's flight is the ballistic arc to half a uu);
  `PlanetFall` (on the equator and under the south pole every substep accelerates toward the centre at the engine's
  gravity, and a level shot falls 440-500 uu toward it in a second, none of it out of its plane); `HitsPawn` (spawned from
  inside the shooter's capsule, it meets the target for half the damage at half draw and goes); `Sticks` (a static wall, a
  movable replicated one it rides, a movable local one it does not); `DamageAndFactions`.
- PIE in `VoxelDemo` (arrows spawned from the player's eyes by script, `SpawnArrow`): stuck tip first in the slope at 3 m
  and 10 m; the training dummy hit for 15 at full draw and the arrow gone; a 47° shot arced to 8,300 uu up and came down
  357 m out, past the cooked collision, stopping on the generated surface where the far mesh shows it.

### B3: The attack (VoxelCharacterPlugin)
- `UVCRangedAttackAbility`: the draw on press, the shot on release, the arrow used, the release sent to others; the
  primary-action chain's bow step; `vox.Shoot [draw=1]` for QA (the game module).
- Tests: the draw's fraction and its minimum; no arrow, no draw; one arrow per shot; a client's release with no arrow on
  the server is refused. The PIE dedicated-server test gains a step: the server's arrow reaches the client.

**B3 result (2026-11-06; VoxelCharacterPlugin, the parent).**
- `UVCRangedAttackAbility` (`Combat/VCRangedAttackAbility.h`), ServerOnly, granted with the melee attack
  (`AVCPlayerState::RangedAttackAbilityClass`), tag `Ability.Attack.Ranged` (this plugin's native tags). Its activation
  times the draw on the server; the release reaches it as the ability system component's input release
  (`ReleaseDraw`: `ServerSetInputReleased` from a client, on the component's channel after the activation request, so
  it cannot overtake it; `AbilitySpecInputReleased` on the server). At the release: a draw under the minimum is no shot;
  otherwise one arrow leaves the bag, `SpawnArrow` launches it from `ArrowStartOffset` (ahead of the eyes, by the bow's
  hand) toward what the view meets within `AimRange` (a thin sphere on the Pawn channel), at the bow's speed times the
  draw's power, carrying the Weapon fragment's damage (halved when worn out), its type and crits, and the ammo's mesh;
  the bow wears by its DegradeRate. No ranged weapon, a dead or downed shooter, or no arrow left at the release: no shot
  and nothing used. One predicate for the owner's check and the server's (`CanStartDraw`, D-CR16).
- The primary-action chain's step 0, `AVCCharacterBase::TryStartRangedAttack` / `ReleaseRangedAttack` (the input's
  end): a ranged weapon in the main hand takes the press, arrows or not ("No arrows" as a toast); the owner plays its draw
  at once and, at the release, its release when its own clock says the draw was long enough, else stops the draw; the
  server sends both to everyone else (D49). `EVCAnimAction::Draw` / `Release` and the set's `Draw` / `Release` montages
  are there for B4; `UVCActionMontageComponent::StopActionLocal` / `StopActionForEveryone` stop a draw let go too early
  (a reliable multicast: a lost stop would leave a looping draw). `vox.Shoot [draw=1] [autoAim=0]` in the game module.
- Tests: `VoxelCharacter.Combat.Ranged.DrawRules` (the power curve, no draw without a bow or an arrow),
  `.OneArrowPerShot` (a full draw at 6000 uu/s, one arrow and one point of wear; a 0.1 s draw nothing; a partial draw a
  partial speed), `.ReleaseWithoutArrowRefused`. `VoxelEngine.Replication.Framework` gains two steps: the server's arrow
  reaches the client with its shooter and mesh, then sticks in a replicated wall there, riding the client's copy with its
  collision off.
- PIE in `VoxelDemo`, standalone: `vox.Shoot 1 1` hit the dummy for 15 (one arrow used); a 0.12 s draw shot nothing; with
  no arrows no draw started and the toast showed. With a dedicated server and a client (the editor's play settings), the
  client's own primary action (`Input.+action IA_PrimaryAction`) activated the ability on the server, the release came
  in order, the server shot after its own measure of the draw and used an arrow, and the arrow stuck on both machines.
  The client's arrow count stayed at 20 while the server's went to 19: G133, a framework replication bug (a stack's count
  changed in place is never sent), not the attack's. Python cannot drive a client's server RPCs (it calls every function
  under the editor's script guard, where an RPC runs locally); a key or action forced into the client's input can.

### B4: Animation and feel (content, the parent's script)
- The Bow set and `ABP_VCLayer_Bow` (stance and aim offset) in `setup_character_layers.py`; the bow's montages; P3's
  Place and Use from the pack's pickup and potion.
- PIE: draw, aim up and down, shoot the dummy in `VoxelDemo` and on `PlanetTest` (an arrow's arc toward the planet).

### B5: Closing
- `CHARACTER_SYSTEM.md` (combat, animation, multiplayer), the console guide (`vox.Shoot`), the QA rows; this plan's
  results paragraph and its frozen banner.

## 5. Risks

| Risk | Answer |
|---|---|
| An arrow outruns the streamed collision (far shots) | A lifespan and range; it stops when nothing is loaded ahead (it hits nothing, no damage) |
| Fast arrows through thin colliders | The projectile movement sweeps; the arrow's speed stays under a voxel per substep |
| The owner sees its arrow late (one round trip) | D54 (a) accepts it, as D49 did for melee; (b) if it feels late on a real connection |
| The aim offset on a planet | `AimPitch` and `AimYaw` are already gravity-relative (P2) |
| A bow on the left bone breaks the first-person socket map | First person draws nothing for now (D57); the map stays for the other items |

## 6. Decisions

Decided 2026-11-06 by the user, each as recommended; numbers from `Documentation/FINDINGS.md`.

| ID | Decision | Options | Decided |
|---|---|---|---|
| D52 | How the bow is held | (a) the main-hand slot with a per-item attach bone (`hand_l`); (b) the off-hand slot, the action set and the attack reading the off hand when the main hand is empty | **(a)**: every "what is wielded" rule already reads the main hand |
| D53 | The bow's mesh | (a) LowPolyRPGWeapons' static bow (the low-poly look of the character and items; the string does not bend); (b) the pack's skeletal bow (the string bends with the draw; a generic look, and its string needs driving); (c) a bow from the Blender pipeline | **(a)** now; (b) later if the rigid string reads badly |
| D54 | The arrow | (a) spawned and replicated by the server, no client prediction; (b) a predicted arrow on the owner, replaced by the server's | **(a)**, as D49 |
| D55 | Ammo | (a) arrows are items, one used a shot, no shot without one; (b) unlimited arrows | **(a)**: the inventory already has what it needs |
| D56 | The draw | (a) hold to draw and release to shoot, the arrow's speed and damage scaled by the draw, a minimum draw to shoot; (b) a click shoots at full power after a fixed draw | **(a)** |
| D57 | First person | (a) no draw shown in first person until the arms decision (D48's revisit); (b) first-person arms and their bow animations now | **(a)**: the demo has no arms mesh |

## 7. Not in scope

Crossbows and thrown weapons (the fragment and the arrow class generalise to them later), NPC archers (P7 gives NPCs
bodies; the ability is server-side and an AI can activate it), picking stuck arrows back up, special arrows (fire,
poison: the damage type is already data), a quiver on the back.
