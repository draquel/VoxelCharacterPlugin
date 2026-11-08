# Character Refinement Plan: holding, the upper body, aiming, handling

> **Agreed 2026-11-08** as drafted, every decision as recommended (D70-D75, §6; the FINDINGS register has the row).
> Scope: what the character animation plan (D43-D51) and the ranged combat plan
> (D52-D57) left rough once they met in play: held items off the hand and turned wrong (the bow first), the torso
> frozen while anything is held, no aim state (camera, reticle), and a capsule that slides into and out of strafing.
> Plus a level to test and tune all of it by hand and by script. Owner: VoxelCharacterPlugin; one fix each in
> EquipmentPlugin and the appearance data; the level, its kit and the QA rows in the parent. Terms:
> `Documentation/GLOSSARY.md` at the project root. The findings the survey turned up are G153-G158 in
> `Documentation/FINDINGS.md` (recorded with C0). Phases: C0 done 2026-11-08 (§5); C1-C5 to come.

## 1. What exists, and what is wrong

Read 2026-11-08 from the code, the content scripts and the graph dumps the scripts save (`Saved/CharacterIKSetup.json`,
`Saved/CharacterLocomotionSetup.json`); nothing below was re-checked in PIE yet (C0's first pass does that).

### 1.1 Held items (the bow)

| Piece | State |
|---|---|
| The attach | `UEquipmentManagerComponent::OnMeshLoaded` attaches the item's static mesh to `GetMesh()` at the item's `AttachBoneOverride` (the bow: `hand_l`) or the slot's socket (`hand_r`), then `SetRelativeTransform(GetVisualAttachOffset)`: on an own bone the item's `AttachOffset` alone. `AVCCharacterBase::UpdateEquipmentAttachments` re-snaps on equip, on view change and when the visual lands |
| The bow's fit | `configure_ranged_items.py`: pitch 90, no translation. Judged in B1 ("upright in the fist") with the arm hanging, before the Bow set (B4) and before the MetaHuman body (P6) existed. The grip pivot sits on the wrist joint, not in the palm; pitch 90 lays the stave along the bone's X, the hand's own length, so with the arm raised in the bow idle the bow lies along the forearm |
| The pickaxes | rotation only (`PICKAXE_GRIP`, the pack socket's rotation), the socket's translation not taken; `configure_item_visuals.py` run after `setup_character_layers.py` wipes the grip (scale-only offset) (G157) |
| The bodies | no sockets on either: the low-poly `SK_Mannequin` (CR pack) and `metahuman_base_skel` are pack content (junctioned, shared by every checkout; never edited). Kellan's hands differ in size and the MetaHuman claim ("orients as the Mannequin does") was checked with the sword only. The plan's per-body offsets ("P6's appearance asset", `CHARACTER_ANIMATION_PLAN.md`) were never built: `UVCCharacterAppearance` has no such field |
| The pack's own hold | Essential Bow and Arrow holds its skeletal bow on a `weapon_l` bone (with `bow_string`, `arrow_bow_string`); neither of our skeletons has it, the runtime remap drops the track. Our bow is static (D53), so a fixed fit to `hand_l` is right; it has to be tuned in the pack's idle and draw poses |
| "Sometimes wrong" | a hotbar swap is Unequip then Equip in one server frame; the client's `OnRep_EquipmentSlots` sees occupied → occupied and does nothing, so a remote view keeps the old mesh, offset and layer under the new action set (G153) |
| First person | the body is `OwnerNoSee`, the visuals are not, and there is no arms mesh (D57): the bow and tools float at the hidden body's hands |

### 1.2 The upper body while holding

Every stance layer (`ABP_VCLayer_SwordShield`, `_TwoHand`, `_Tool`, `_Bow`; `setup_character_layers.py` `make_layer`) is
one looping full-body idle clip into a **Layered blend per bone over `spine_01` at depth 0, weight 1, mesh-space
rotation on**. No alpha, no speed input, nothing additive. So the torso, arms, neck and head are the static idle at
every gait: no arm swing, no run lean (GASP's lean additive and stand aim offset sit upstream and are overwritten on
the torso), no spine counter-rotation, no head bob; mesh-space rotation also holds the torso at the clip's orientation
against the pelvis twist of turns and strafes. Unarmed has no layer, which is why it shows "only when holding".
Montages in `DefaultSlot` (attack, dig, chop, place, use) still override the legs too. The hoodie is innocent: it copies
the animated `Mesh`'s pose (`bUseAttachedParent`), the layer included.

The packs have what a moving upper body needs: Bow and Arrow `Walk_Loop`, `Jog_Loop`, `UE5_A_Stand_Forward/Backward_BlendSpace`;
Sword & Shield and 2H `Walk`, `Jog_Loop`, `*_Jog_Forward_BlendSpace`; the Tool set keeps the 2H stand-in (D51).

### 1.3 Aiming

| Piece | State |
|---|---|
| State | none: the draw is `bDrawingRangedWeapon` on the owner and `bDrawing` in the server ability; no `State.Aiming` tag, nothing replicated, `UVCMotionMatchingAnimInstance::WantsToAim` is always false, so GASP's aim databases never play |
| Body | `SetFacesAim` flips `bOrientRotationToMovement` / `bUseControllerDesiredRotation` in one frame at the draw and the release; `SetViewMode` resets the orientation flags and would undo it mid-draw (G155) |
| Camera | `UVCCameraManager` is a stack with push and no pop; third person is arm 300, offset (0, 50, 60), lag 10, FOV 90, no aim or zoom mode, no `vc.Camera.*` cvar |
| Reticle | none; "crosshair" in the code is the view ray |
| The shot | `ComputeShot` sweeps from the pawn's eyes (`GetActorEyesViewPoint`) along the control rotation, not from the camera: parallel to the screen centre but about 50 uu left of it and lower, worse up close, so a centre reticle would not mark where arrows land (G156). The server has no client camera; melee and `TraceForVoxel` disagree the same way |
| Input | `IA_SecondaryAction` (RMB) is bound Started only and does nothing outside edit mode (places stone in it); GASP's `IA_Aim` is in the pack, unused |

### 1.4 Handling

The player plays GASP's CMC AnimBP but not the per-frame movement tuning GASP's character runs beside it, so the
capsule is far snappier than the clips the databases hold, and the Offset Root Bone drags the mesh after it:

| Setting | Ours | GASP (`SandboxCharacter_CMC`, read from the asset; verify in the editor) |
|---|---|---|
| Max speed walk / run / sprint | none / 600 every direction / 960 | 200 / 500 / 700 forward; strafe 180 / 350, back 150 / 300; crouch 225 |
| Max acceleration | 2048 (engine default) | 800; a sprint 800 → 300 with speed |
| Braking deceleration | 4096 always | 500 with input, 2000 stopping |
| Ground friction | 8 × surface × wet | 5; a sprint 5 → 3 |
| Rotation rate (yaw) | 500 °/s always; the capsule turns and the graph (steering, orientation warping, offset root) smooths again | instant on the ground (−1), 200 in the air: the graph does all the visual turning |
| Strafe | only while the bow is drawn or in first person; the flag flip is one frame, nothing eases | `IA_Strafe` / `IA_Aim` toggle the same two flags, with the strafe speed map |

Also: `CurrentMaxDeceleration` fed to GASP's graph is the constant 4096 where GASP passes `GetMaxBrakingDeceleration`;
`MaxAcceleration` stays 256 after leaving water by any path but the swim exit and the shore climb (G154); the wind
drift is always on (clear weather 15-18 uu/s at a run) and the ground's wetness decays over an hour so a dry day after
rain still cuts grip (both weather P4 design, kept, but taken out of every A/B with `vox.Wind 0 0`); the camera pivot's
lag (speed 10) trails 60-96 uu at a run and swings on direction changes, which reads as the body sliding. No `vc.*` cvar
touches movement; `UVCSettings` has no locomotion tuning. The spike's foot-slide metric (`vox.Spike.FootSlide`) was
never merged, so nothing measures the slide.

### 1.5 Testing

`/Game/PluginTesting/Tests/Inventory/InventoryTest` is an empty prototype-grid floor (a plane, three cubes, a player
start, the sky set, `BP_ThirdPersonGameMode`), nothing refers to it but the far-shadow script. A pawn spawns on a static
floor at once ("No VoxelChunkManager found, placing immediately"); the appearance falls to `DefaultPlayerAppearance`
and `vc.Appearance` switches bodies live. Maps are made by copying (`setup_planet_test.py`, `setup_smooth_terrain.py`),
never `new_level`. The QA plan is generated from `Tools/QA/qa_rows.py` (areas W, F1-F12; F11 the bow) into
`QA_TEST_PLAN.md` and the tracker artifact; the items come from `GiveItem`, `vox.Equip`, `vox.SpawnDummy`, `vox.Shoot`,
`vox.AutoRun`. The automation tests build bare worlds (`VCTesting::FPlayWorld`) and never load a map; feel, camera and
HUD are hand QA or the Claudius PIE loop.

## 2. Principles

1. **Tune in the pose, measure on a lane.** Every fit, blend and speed is judged in the held pose and the moving gait
   on the test level, from fixed camera positions, with a number beside the eye where there is one (foot slide,
   time-to-stop, overshoot). Nothing is tuned from the hanging arm again.
2. **Data, not code, for the feel.** Fits on the items and the appearance, blends in the layers the script builds,
   speeds and friction in a gait table on the movement component (`BP_VCPlayer` can override it). Code adds the
   mechanisms (an aim state, a camera pop, a gait table applied each tick) and the cvars to A/B them.
3. **The server decides, the owner predicts, both from one predicate** (D-CR16): the aim camera's origin and the shot's
   ray are one static function; the gait table is applied inside the predicted movement from state both sides have.
4. **Both bodies, both views, every machine.** A fit holds on the low-poly body and on Kellan; first person hides what it
   must and third person shows it; a remote client's view of a swap is the server's.
5. **GASP's capsule is the reference, not the law.** Its numbers are the starting preset; ours end where the lane says.

## 3. Design

### 3.1 Holding (C1)

- **Fit tuned in the pose.** `vc.Equip.Nudge <slot> <dx dy dz> [dp dy dr]` (dev cvar family `vc.Equip.*`, not shipped)
  moves the attached visual live and `vc.Equip.PrintFit` writes the resulting `AttachOffset` as the Python line the
  script wants. The bow is fitted in `UE5_A_Idle_Seq` and `Draw_A_Loop` (the string hand on the string), the pickaxes
  with the socket's translation too, the swords checked.
- **Per-body corrections** (D71). `UVCCharacterAppearance::AttachCorrections` (`TMap<FName, FTransform>` by bone, default
  empty): the correction for a hand the body shapes differently, composed by `UpdateEquipmentAttachments` after the
  item's fit (so an item fits once, on the Mannequin's hand, and Kellan's hand corrects it). `ApplyPlayerAppearance`
  calls `UpdateEquipmentAttachments` (it does not today). No socket on any pack skeleton.
- **The swap on clients (G153).** `OnRep_EquipmentSlots` compares the item instance (id and definition) a slot held with
  the one it holds, not only its occupancy, and re-applies the visual and the layer on a change.
- **First person.** Hand items on the body are hidden with it (`OwnerNoSee` on the visual, the torch's light kept) until
  an arms mesh exists (D57 stays); in third person nothing changes.
- **Script order.** `configure_item_visuals.py` keeps an existing rotation (sets scale only) so the grips survive any
  order; `setup_gamemode.py`'s stale controller path (`BP_VCPlayerController` under PluginTesting) is corrected (G158).

### 3.2 The upper body (C2)

- **A moving stance** (D72). Each stance layer becomes: a 1D blend space per set of the pack's own idle / walk / jog
  upper-body clips driven by the body's `Speed` (the Tool set shares 2H's), into the layered blend over a **depth ramp**
  from `spine_01` (so the lower spine keeps most of the locomotion's bounce and twist and the clavicles and arms take the
  stance), mesh-space rotation **off** by default with the on/off an A/B, and GASP's lean additive and stand aim offset
  re-applied after the layer (the script moves the layer link below them, or the layer takes `LeanAmount` and applies
  the additive itself). The bow's aim chain stays as it is inside the new blend.
- **The weight.** `UVCAnimInstance::StanceWeight` (1 on the ground and alive; eased at `IKBlendSpeed` to a lower value
  while sprinting, falling, swimming, dead or downed) drives the layer's alpha, so a sprint shows the full run and a
  stance never freezes a ragdoll's lead-in.
- **The script.** `make_layer` rebuilds a layer whose wiring is older than the script's (a version string on the layer's
  description) instead of keeping any linked Root, so a wiring change reaches the main checkout's content on the next
  run; the early-out stays for an unchanged version.
- `vc.Anim.Stance 0|1|2` A/B: off (locomotion only), the old static override, the moving stance.

### 3.3 Aiming (C3)

- **The aim state** (D73). Holding the secondary action with a ranged weapon in hand aims; drawing aims too (so hold
  to draw still works alone). The state is `State.Aiming` on the ASC (replicated with its tags, so every machine strafes
  and poses), set by the owner at input and by the server (`UVCAimAbility`, local-predicted, or the ranged attack
  ability's own tag), with `bWantsToAim` for the motion-matching instance (`WantsToAim` true, `RotationMode` Strafe) and
  `SetFacesAim` driven by it instead of the draw alone. `SetViewMode` re-derives the orientation flags from the aim state
  (G155). Edit mode keeps RMB (placing); the two never overlap since a held bow is no placing tool.
- **The camera.** `UVCAimCameraMode`: arm 150, offset (0, 60, 55), FOV 70, **no lag** (the pivot is the pawn's eye point
  turned by the control rotation), blended in over `ModeTransitionBlendTime` by the stack; `UVCCameraManager::PopCameraMode`
  (new) returns to the mode below so the aim mode leaves cleanly (today a push never leaves). First person: the FOV
  narrows the same, nothing else.
- **The reticle.** `UVCReticleWidget` (C++, `NativeOnInitialized`; a class property and instance on the controller as the
  others, `HitTestInvisible`, centre-anchored): a dot while a ranged weapon is held, a ring that closes with the draw's
  power (`GetDrawPower` on the owner's clock), red over a hostile target, hidden with the HUD. Shown in both views.
- **The shot from the camera** (D74). `UVCCombatStatics::ComputeAimRay(Character)` is one static predicate: in aim the
  aim camera's origin (deterministic, so the server computes the same from the control rotation and the pawn), otherwise
  the eye ray as today; `ComputeShot` and melee use it. The arrow still leaves the bow (`ArrowStartOffset`) toward the
  ray's hit, so it converges on the reticle. No new RPC payload.
- Input: `IA_SecondaryAction` gets a Completed / Canceled binding (`Input_StopSecondaryAction`); no new action asset
  (GASP's `IA_Aim` stays unused: one RMB).

### 3.4 Handling (C4)

- **A gait table** (D75). `FVCGaitSettings` (max speed forward / strafe / back, max acceleration, braking deceleration
  moving and stopping, ground friction, braking friction factor) per gait (walk, run, sprint, crouch) and the rotation
  rates (ground orient: instant, ground strafe, air) on `UVCMovementComponent` as `UPROPERTY`s with GASP's numbers as
  the defaults, applied every tick inside the predicted movement from the gait (the sprint flag and the crouch, both
  saved-move state) and the rotation mode (the aim state above): the strafe speed map by the input's angle to the
  facing, the sprint's acceleration and friction by speed as GASP's. The surface and wet multipliers scale the table's
  friction as they scale the constant today; the swim and flight values are untouched; `OnMovementModeChanged` restores
  the ground values (G154). NPCs keep their own numbers (the table is the player's; `AVCNPCCharacterBase` sets
  `bUseGaitTable` false): `npc_bench.py` must not move.
- **Turning.** Instant capsule yaw on the ground in orient mode, the graph turning the body (GASP's offset root, steering
  and orientation warping already in the copy); 200 in the air. The face-aim flip eases: the rotation rate in strafe
  mode starts at the orient rate and the mode change waits for the body to be within `AimTurnTolerance` of the view
  (or `AimTurnMaxTime`), so a draw no longer snaps the capsule 180° under the mesh.
- **The graph's inputs.** `CurrentMaxDeceleration` from `GetMaxBrakingDeceleration`; the trajectory tuning
  (`RotateTowardsMovementSpeed`, `MaxControllerYawRate`) exposed on `UVCMotionMatchingAnimInstance` for the pass.
- **The measure.** `vox.FootSlide 1` (game module, the spike's metric rebuilt): each frame, for each foot whose ball is
  planted (the pose's foot speed under a threshold), the world speed of that bone over the floor; the log prints the
  mean slide in uu/s per gait, `vox.Stop` logs time-to-stop and overshoot from a full run, `vox.Lane` runs the lane
  (below) hands-free. `ToggleVoxelDebug` adds the gait, the rotation mode and the table's row in force.
- `vc.Move.Preset Legacy|GASP|Tuned` swaps the whole table live for the A/B; `vc.Move.<Field>` cvars override one value
  during the pass and are printed back as the table row to commit.
- **The camera's lag** is re-tuned on the lane last (speed, and a cap on the lag distance), after the capsule stops
  sliding, so the two are not confused.

## 4. The test level (C0, D70)

**`/Game/PluginTesting/Tests/Character/CharacterTest`**, built by `Scripts/setup_character_test.py` from
`InventoryTest` (copy, as every map here; idempotent; refuses to run in PIE). A static prototype-grid floor, no voxel
world: deterministic, loads in seconds, no streaming or weather in the way (the terrain rows stay on `VoxelDemo` for
Kellan and `Review_Cubic` for the low-poly body, as the QA plan runs them). The script places:

| Station | What | For |
|---|---|---|
| **The lane** | 40 m straight, a grid cube every 5 m, a 90° and a 180° turn marked, a stop line | starts, stops, pivots, strafe in and out, foot slide; `vox.Lane` drives it: run to the turn, aim (strafe) through it, release, stop at the line |
| **The steps** | 75 uu block steps up and a 28° ramp (static meshes) | the step ease, foot IK on both bodies (G145's reach), the capsule on slopes |
| **The range** | target posts at 10, 20 and 40 m with a `vox.SpawnDummy` point each, a wall for stuck arrows, a hostile and a neutral dummy point | the reticle against the arrow's landing, the draw, the aim camera against the wall, hit reacts |
| **The rack** | `AVoxelCharacterTestKit` (game module, QA tooling beside `UVoxelDebugSubsystem`): at BeginPlay gives the local player every weapon (`ID_Weapon_Bow`, 20 arrows, both swords, both pickaxes, the torch), spawns the dummies on their points, and exposes `vox.Kit` to give it again | nothing to type before a pass |
| **The cameras** | five named `pie_camera` poses in the script's header (front, side, over-shoulder, lane end, range) | the same shot every pass, both bodies; the front pose is the pass's mirror |

World settings: `BP_ThirdPersonGameMode` (as every map); the clock pinned to noon (`vox.Time 12`, `vox.TimeScale 0`
from the kit) so shots stay lit; the save system's per-map autoload is cleared by the kit on start (`vox.DeleteSave`
semantics for this map only) so a pass begins the same.

**The QA rows.** A new area **F13 "Character: holding, aiming, handling"** in `Tools/QA/qa_rows.py` (setup: load
`CharacterTest`, PIE, `vc.Appearance LowPoly` then `Kellan` for each row), one row per check below, the console
shortcut beside it, `gen_md.py` and `gen_tracker.py` run, the tracker republished:

- F13-01 bow in the hand: idle, drawn, released; both bodies; third person. F13-02 pickaxe and swords the same.
  F13-03 a hotbar swap seen from a second client (listen server, machine free). F13-04 first person: no floating item.
- F13-05 torso walks and runs with each item held; F13-06 the lean and the head on a turn; F13-07 sprint shows the run.
- F13-08 RMB aims: camera in, reticle, strafe; release returns. F13-09 the arrow lands on the reticle at 10 / 20 / 40 m.
  F13-10 the draw's ring. F13-11 view toggle mid-draw keeps the aim. F13-12 a remote player seen aiming.
- F13-13 start and stop on the lane, no slide (`vox.FootSlide` under the threshold agreed on the first pass);
  F13-14 the 90° and 180° pivots; F13-15 strafe in and out at a run; F13-16 the steps and the ramp; F13-17 wind off
  then a storm (`vox.Wind`) to see the drift as drift.

**Automation.** What has a number gets a test in `VoxelCharacter.*` on bare worlds: the gait table's row for each gait
and direction (`Movement.GaitTable`), the aim ray predicate equal on owner and server (`Combat.AimRay`), the
appearance's correction composed with the fit (`Appearance.AttachCorrection`), the client swap re-applying the visual
(`EquipmentLoop.SwapOnClient`, and a step in `VoxelEngine.Replication.Framework`), the stance weight's values
(`Anim.StanceWeight`). The feel stays hand QA on the lane, with the metrics logged.

## 5. Phases

Each a branch from `dev`, a PR per repository into `dev`, the gate `Scripts/qa/README.md` names (code: build,
non-unity `-Changed`, the suite; replication: the editor gate). After a merge the main checkout builds and re-runs the
content scripts the phase changed (never while another editor runs, G150).

### C0: The test level (parent: the map script, the kit, F13 rows; S)
- `setup_character_test.py`, `AVoxelCharacterTestKit`, `vox.Kit`, `vox.Lane`, `vox.FootSlide`, `vox.Stop`; the F13 rows
  and the tracker; the first pass on both bodies **records the baseline** (screenshots from the four cameras, the
  slide and stop numbers) into this plan's §1, which is where the user's four reports get their evidence.
- FINDINGS: G153-G158 recorded (§1's six).

**C0 result (2026-11-08; the parent, and this plan in VoxelCharacterPlugin).**
- `Scripts/setup_character_test.py` builds `/Game/PluginTesting/Tests/Character/CharacterTest` from `InventoryTest`
  (the template's plane and cubes replaced by a 70 x 40 m prototype floor; the sky, lights and game mode kept): the lane
  (markers every 5 m, the pivot stripe at x = 0, the stop line at x = 1000), the steps and the 28 degree ramp at
  y = 2000 (first at 1200, where the strafe leg ran into the ramp's side and the pawn stood against it until the leg
  timed out), the range at y = -1200 (posts at 10, 20, 40 m, the wall at 45 m, a hostile and a neutral dummy point),
  the kit at the origin, the player start on the lane start. Every placed actor carries the tag `CharacterTest` and
  is rebuilt on each run; the map is copied only when missing (`CHARACTER_TEST_REBUILD=1` copies it again: a deleted
  map that is loaded stays in memory and is saved back, so the rebuild saves the template over it instead).
- `AVoxelCharacterTestKit` (game module, `Debug/VoxelCharacterTestKit.h`): at play, on the authority, once the player's
  pawn exists, `vox.DeleteSave`, `vox.Time 12` and `vox.TimeScale 0`, seven `GiveItem` lines through the controller's
  console, a dummy (`SpawnDummyAt`, split out of `vox.SpawnDummy`) on each tagged point; `vox.Kit` repeats it. The pawn
  spawns at once on the static floor ("No VoxelChunkManager found, placing immediately").
- `vox.Lane`, `vox.Stop`, `vox.FootSlide` (`Debug/VoxelDebugSubsystem_Lane.cpp`; the console guide §2.1, §2.2). The
  slide: a foot (`foot_l`, `foot_r`) within 4 uu of the lowest it has been in 2 s, on the ground, adds its speed across
  up to the gait's bucket; it counts the swing's low pass too, so it is a relative number (the A/B's), not a truth.
- The QA rows F13-01 to F13-17 (`Tools/QA/qa_rows.py`, the plan and the tracker regenerated); G153-G158 recorded.
- **The baseline**, both bodies, the lane at a run with the bow in hand (`Saved/Screenshots/Claudius/ct_<Body>_*.png`
  from the five poses; the log lines `[Lane]`, `[Stop]`, `[FootSlide]`):

  | Leg | LowPoly slide uu/s (planted s) | Kellan |
  |---|---|---|
  | run-up +X 15 m | 251.7 (1.8) | 302.8 (1.6) |
  | pivot 90 to +Y / back | 214.2 (0.4) / 200.1 (0.4) | 242.7 (0.4) / 296.5 (0.3) |
  | pivot 180 to -X / back | 324.3 (0.7) / 259.2 (0.8) | 229.3 (0.7) / 208.8 (0.5) |
  | strafe +Y drawn / released -Y | 344.5 (0.5) / 464.8 (0.8) | 566.7 (0.5) / 512.0 (0.7) |
  | straight +X | 180.0 (0.4) | 318.5 (0.5) |
  | stop from 600 uu/s | 0.13 s, 38 uu | 0.13 s, 38 uu |
  | whole lane: walk / run | 137.7 / 313.8 | 228.9 / 343.8 |

  The eye, from the shots: the bow at rest hangs beside the left leg on both bodies (plausible); **drawn, the stave lies
  horizontal along the raised arm on both bodies** (§1.1's first hypothesis, confirmed); the pickaxe sits in both hands
  with its head up (the 2H stand-in); the stop is a 0.13 s snap. The strafe legs slide most, the stop is the capsule's
  4096 braking. These numbers are what C2 and C4 are measured against.

### C1: Holding (EquipmentPlugin, VoxelCharacterPlugin, the parent's scripts; M)
- `vc.Equip.Nudge` / `PrintFit`; the bow, the pickaxes and the swords fitted in the pose on the low-poly body;
  `AttachCorrections` on the appearance and Kellan's; `OnRep_EquipmentSlots` by item; first-person hiding; the script
  order and the game mode script. Tests above. Docs: `EQUIPMENT_SYSTEM.md` (visual attachment, client sync),
  `CHARACTER_SYSTEM.md` ("The bow", "Appearance" › Sockets).

### C2: The upper body (VoxelCharacterPlugin, `setup_character_layers.py`; M)
- The blend spaces and the depth ramp in `make_layer`, the layer versioning, `StanceWeight`, the lean re-applied,
  `vc.Anim.Stance`. Judged on the lane with each item, both bodies, walk / run / sprint / turn. Docs: "Animation" ›
  Layers.

### C3: Aiming (VoxelCharacterPlugin, CommonGameFramework if the tag goes there; L)
- `State.Aiming`, the aim ability or tag, `bWantsToAim`, `SetViewMode` fix, the camera mode and the pop, the reticle,
  `ComputeAimRay`, the RMB release binding. Tests above; the replication test gets an aiming step. Docs: "The bow",
  "Camera", "Input", "HUD and widgets", `EDITOR_CONSOLE_GUIDE.md` §4.

### C4: Handling (VoxelCharacterPlugin, VoxelCharacterAnimation; L)
- `FVCGaitSettings` and its application, the rotation rates, the eased face-aim flip, the graph's inputs, G154,
  `vc.Move.*`, then the pass on the lane: Legacy → GASP → Tuned, the numbers in this plan's results, the camera lag last.
  `npc_bench.py` before and after. Docs: "Movement", §4 of the console guide.

**C4 result (2026-11-08; VoxelCharacterPlugin, and the parent's `lane_pass.py`, console guide and FINDINGS).**
- `FVCMovementTable GaitTable` on `UVCMovementComponent` (`CHARACTER_SYSTEM.md` "Movement"): a `FVCGaitSettings` row per
  gait, the rotation rates by mode, the sprint's ramps, applied at the start of every `PerformMovement` from saved-move
  state (the sprint, a new walk flag, the crouch) and the rotation mode; `ComputeGaitValues` is the pure form. GASP's
  numbers were read from `SandboxCharacter_CMC`'s class defaults and `Curve_StrafeSpeedMap` rather than §1.4's table:
  the speeds as §1.4 has them; `BrakingFrictionFactor` 0 (a stop is the deceleration alone); the strafe map holds the
  forward cap to 45 degrees and the backward cap from 135, linear between. `bUseGaitTable` off on `AVCNPCCharacterBase`
  (the NPC's numbers asserted by `VoxelCharacter.Movement.GaitTableApplied`; `npc_bench.py` was not re-run: the NPC
  path gained one flag check per move). G154 closed: `OnMovementModeChanged` restores the ground acceleration on any
  way out of the water.
- Turning: instant capsule yaw on the ground in both modes, 200 in the air, the swim's 500 kept. The face-aim flip is a
  turn (`UVCMovementComponent::SetFacesAim`, `AdvanceAimTurn` in `PhysicsRotation`): the capsule turns toward the view
  (or back toward where it moves) at `AimTurnRate` 540 degrees a second and the flags change within `AimTurnTolerance`
  15 degrees or after `AimTurnMaxTime` 0.5 s. The planet A/B on `PlanetTest` (the probe at the equator and at 75 degrees
  south, running north then a 90 degree pivot, running then a 180): the body's up along the radius to 1.000 throughout,
  the forward on the velocity within 0.5 s of each pivot, the table's values in force (the friction 10: the row's 5
  times the surface's scale; the cap 425 at the south: the cold's 0.85).
- The graph: `CurrentMaxDeceleration` from `GetMaxBrakingDeceleration`; `RotateTowardsMovementSpeed` and
  `MaxControllerYawRate` on `UVCMotionMatchingAnimInstance` and the cvars `vc.Anim.TrajectoryRotateSpeed` /
  `vc.Anim.TrajectoryYawRate` (not changed by the pass).
- `vc.Move.Preset Legacy|GASP|Tuned`, `vc.Move.Set` (a row's field, a rate or ramp, or the flip's three), `vc.Move.Print`;
  `ToggleVoxelDebug` adds the gait, the mode and the row (the console guide §4, and a recipe in §12).
  `Scripts/qa/lane_pass.py` runs the lane per body and preset and tables the legs; it resets the slide measure before
  each lane (left running, the teleport back to the start counted as a 2,700 uu/s slide on the run-up leg).
- **The pass** (both bodies, the bow in hand, the wind off, the slide in uu/s with the planted seconds in brackets; the
  runs' spread from five GASP runs: the release leg 99-230 on LowPoly, Kellan's second 180 pivot 106-419, the rest within
  about 30):

  | Leg | LowPoly C0 | Legacy | GASP | Kellan C0 | Legacy | GASP |
  |---|---|---|---|---|---|---|
  | run-up +X 15 m | 251.7 (1.8) | 271.4 (1.8) | 110.1 (2.0) | 302.8 (1.6) | 296.5 (1.6) | 190.0 (1.9) |
  | pivot 90 to +Y / back | 214.2 / 200.1 | 199.9 / 200.5 | 139.2 / 128.5 | 242.7 / 296.5 | 245.1 / 311.0 | 150.3 / 147.7 |
  | pivot 180 to -X / back | 324.3 / 259.2 | 260.6 / 248.1 | 161.6 / 73.5 | 229.3 / 208.8 | 249.5 / 280.2 | 182.7 / 106.0 |
  | strafe +Y drawn / released -Y | 344.5 / 464.8 | 351.4 / 161.1 | 82.8 / 230.1 | 566.7 / 512.0 | 517.3 / 143.1 | 126.7 / 166.9 |
  | straight +X | 180.0 (0.4) | 211.0 (0.6) | 131.2 (0.6) | 318.5 (0.5) | 574.8 (0.3) | 99.5 (0.3) |
  | stop from the run | 0.13 s, 38 uu | 0.13 s, 38 uu | 0.27 s, 67 uu | 0.13 s, 38 uu | 0.14 s, 40 uu | 0.27 s, 66 uu |
  | whole lane: walk / run | 137.7 / 313.8 | 132.1 / 261.6 | 83.0 / 149.5 | 228.9 / 343.8 | 264.1 / 338.6 | 153.6 / 159.7 |

  The legs take longer on GASP's table (a 500 uu pivot leg 1.48 s against 1.08: the 800 acceleration and the 500 cap).
  Legacy against C0's baseline is the same lane on the same code with the flip eased (the released leg 465 to 161 on
  LowPoly, 512 to 143 on Kellan: the eased flip alone); GASP's rows then halve the run's slide over the whole lane and
  cut the drawn strafe's four to five times. The one leg GASP leaves worse on LowPoly, the release, is within the runs'
  spread, and so were the flip's variants (`AimTurnRate` 5000, `AimTurnMaxTime` 0.25) and GASP repeated: **`Tuned` is
  GASP's table verbatim** and the default; the preset is where C2's and C3's passes put their changes. The camera's
  `LagSpeed` 10 to 15 and a `MaxLagDistance` of 40 uu (the pivot settled 50 uu behind a run and swung past on a pivot).
- Gates: the editor build; the headless suite (866 tests: the two new tests' own expectations fixed, then
  `VoxelCharacter.Movement`, `.Combat.Ranged`, `.Camera`, `.Planet` 25 of 25); `editor_gate.py` 27 of 27 with
  `VoxelEngine.Replication.Framework` (the dedicated server and client) and the parity lines identical; `-NonUnity
  -Changed` on the two modules.

### C5: Close (S)
- This plan frozen; results paragraphs in `CHARACTER_SYSTEM.md`; the glossary (stance, gait table, aim state, the
  lane); FINDINGS rows moved to Closed; the QA tracker's F13 results; the main checkout synced and its scripts re-run.

**Order.** C0 first (everything is judged on it). C1 and C4 touch different files and can run in parallel sessions
(C1: EquipmentPlugin, the item scripts, the appearance; C4: the movement component and the motion-matching instance),
then C2 (the layers script, after C1's fits so the hands are judged with the items in place), then C3 (needs C4's
strafe map and C1's bow for the feel), then C5. Estimate: C0 a day, C1 and C4 two each, C2 one to two, C3 two to three,
C5 half a day.

## 6. Decisions

| ID | Question | Recommendation |
|---|---|---|
| D70 | The test level: a flat static-floor map built from `InventoryTest`, or a voxel map with a near-flat configuration | **The static floor** (`CharacterTest`, §4): deterministic and fast, both bodies by `vc.Appearance`; terrain stays on `VoxelDemo` / `Review_Cubic` rows. A voxel variant can be added later by the same script with a flat `UVoxelWorldConfiguration` if the lane turns out to need real terrain |
| D71 | Where a fit lives: per-body corrections on the appearance asset composed with the item's fit, or weapon sockets on the skeletons | **Corrections on the appearance**: no edit to pack skeletons (shared junctions), one fit per item on the Mannequin's hand, Kellan corrected once per bone; first-person hand items hidden with the body while D57 holds |
| D72 | The upper body while holding: an additive stance over the locomotion, a reduced blend depth, or a per-set locomotion blend space on a depth ramp | **The blend space on a depth ramp** (§3.2), mesh-space rotation off by default, the lean re-applied, a stance weight eased by state; `vc.Anim.Stance` keeps the old override for the A/B |
| D73 | The aim state: hold RMB (plus the draw), a toggle, or the draw alone as today | **Hold RMB, and the draw aims too**; `State.Aiming` on the ASC so every machine strafes and poses; edit mode keeps RMB for placing |
| D74 | Where the shot comes from: the camera ray (client-sent aim point) or a deterministic aim camera both sides compute | **The deterministic aim camera** (lag-free in aim, so the server computes the same origin from the control rotation): one predicate, no new RPC payload; the reticle marks where arrows land |
| D75 | Handling: GASP's per-gait tuning as a data table on the movement component with instant ground yaw, or tune the existing constants in place | **The gait table with GASP's numbers as the starting preset**, instant capsule yaw on the ground in orient mode (the graph turns the body), the strafe speed map, the eased face-aim flip; players only, NPCs unchanged; `vc.Move.Preset` for the A/B |

## 7. Risks

- **Instant capsule yaw and the planet.** GASP's −1 rotation rate turns the capsule to the input in one frame; on a
  planet the input heading is in the gravity frame and the turn must go through `FVCGravityFrame` (`FaceRotation`
  already does). A/B on `PlanetTest` before C4 closes.
- **The gait table and prediction.** Applied inside the predicted movement from saved-move state only (sprint, crouch,
  the aim's rotation mode is set on both sides by the state); a value read from unreplicated state would desync.
  The dedicated-server PIE check runs in C4.
- **The layers script on the main checkout.** A rebuilt layer replaces a tracked `ABP_VCLayer_*` the item fragments
  refer to; the versioned rebuild resets in place (as G150's `ResetMontage`), never delete-and-create.
- **Kellan's hands.** If a bone-level correction cannot fit both the idle and the draw, the fallback is a correction per
  bone per action set (a map keyed by the set tag); not expected.
- **Two editors.** The pass on both bodies with a second client (F13-03, F13-12) runs with the machine free (P6's
  freeze).
- **Scope creep.** A walk gait toggle, a quiver, arrows visible on the string, first-person arms, GASP's aim databases
  beyond `WantsToAim`: §8.

## 8. Not in scope

A walk toggle (`IA_Walk`), a visible nocked arrow and a quiver, first-person arms (D48/D57), stride warping, NPC gait
tuning, the weather's wet-grip decay (weather P4's, left as designed), new animation packs.

## Inputs

- `CHARACTER_SYSTEM.md` ("The bow", "Movement", "Animation", "Appearance", "Camera", "Input", "HUD and widgets");
  `CHARACTER_ANIMATION_PLAN.md` P4-P6 and spike S1 (frozen); `RANGED_COMBAT_PLAN.md` B1, B4 (frozen).
- `Plugins/EquipmentPlugin/Documentation/EQUIPMENT_SYSTEM.md` (visual attachment, client sync).
- `Scripts/setup_character_layers.py`, `setup_character_locomotion.py`, `configure_ranged_items.py`,
  `configure_item_visuals.py`, `setup_gamemode.py`; `Saved/CharacterIKSetup.json`, `Saved/CharacterLocomotionSetup.json`.
- `Content/GameAnimationSample/Blueprints/SandboxCharacter_CMC` (read only: the numbers in §1.4).
- `Tools/QA/qa_rows.py`, `Plugins/VoxelWorldPOI/Documentation/QA_TEST_PLAN.md`, `Scripts/qa/README.md`.
