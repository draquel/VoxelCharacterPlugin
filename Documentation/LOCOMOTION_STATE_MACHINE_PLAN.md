# Locomotion State Machine Plan: the player's locomotion from the animation packs

> **Agreed 2026-11-10.** Proposed and revised the same day; the user approved D86-D91 (§6) as recommended. L0 done
> 2026-11-10 (§4 "L0 results": the bow carries hold `B`, D92; D91's speeds proposed again); L1 next. Scope:
> the player's locomotion played from the Essential Animation packs' clips through a state machine of our own, built up
> one feature at a time as the content and the need arrive, with Game Animation Sample's motion matching kept selectable
> beside it for comparison. The user's words: GASP is "a difficult system to tune for reliable smooth character behavior
> and feel"; "dropping GASP might be the way to go allowing us to develop the features we actually want/need as we
> approach them rather than trying to rely on this experimental system"; the bow pack's walk and run hold "the bow and
> the arrow together in front of the character", not the stiff chest carry G171 described; the same animator has
> unarmed, two-handed and staff packs that can come later; keep GASP "on the sideline for comparison". The first draft
> (GASP's clips under a state machine) is replaced. Owner: VoxelCharacterPlugin (the runtime module, the editor module),
> the parent's content scripts. Terms: `Documentation/GLOSSARY.md`. The system as it is: `CHARACTER_SYSTEM.md`
> "Animation", "Movement", "Appearance".

## 1. What exists

Read 2026-11-10 from the code, the scripts and the content on disk; nothing below was re-checked in PIE (L0 does that).

### 1.1 The graphs and the switch

| Piece | State |
|---|---|
| `ABP_VoxelCharacter` | Tracked (`/Game/PluginTesting/Blueprints`). A state machine with a Walk/Run blend space, then the `UpperBodyStance` layer, the `UpperBodyAction` slot, the default slot, foot placement and leg IK, look-at, off-hand IK. Plays for NPCs, on a dedicated server, in the automation tests, and for the player under `vc.Anim.MotionMatching 0` |
| `ABP_VCPlayerMotionMatching` | Built by `setup_character_locomotion.py` (GASP's CMC AnimBP and choosers, copied). Parent `UVCMotionMatchingAnimInstance` (module `VoxelCharacterAnimation`). The same layer, slots and IK after GASP's graph. 0.50 ms game thread and 1.39 ms worker, against 0.22 and 0.33 for `ABP_VoxelCharacter` |
| The switch | `AVCCharacterBase::ApplyLocomotionAnimClass`: the appearance's `AnimClass`, else `UVCSettings::PlayerLocomotionAnimClass` (the motion matching copy) when `vc.Anim.MotionMatching` is 1, else the Blueprint's class; live, relinks the held items' layers; a class that does not load falls back to the Blueprint's with a warning. Neither appearance sets `AnimClass` |
| The values | `UVCAnimInstance`, in the gravity frame: `Speed`, `Direction`, `LocalVelocity`, `LocalAcceleration`, `YawRate`, `RootYawOffset` (turn in place), `TimeToLand`, `AimPitch`/`AimYaw`, the IK weights, `StanceWeight`, `ActionSet` |
| The split body and the look | Independent of motion matching: the stance layer (`ALI_VCCharacter`, linked per item, the three-bone ramp from `spine_01`, `MovingStanceWeight` and its fade, D72/D85), the `UpperBodyAction` slot (G162), the bow's aim branch in its layer, the look-at node, the off-hand IK |
| The action sets | `setup_character_layers.py`: Unarmed (the template Mannequin's attacks, no stance), SwordShield (`SSH`), TwoHand (`2H`), Tool (the two-handed set as a stand-in, D51, VanillaLoop's dig), Bow (variant `A`: `UE5_A_Idle`, `_Walk_Loop_F`, `_Jog_Loop_F` as its stance blend space). Each set's stance is idle, walk and jog forward only, laid over GASP's legs |
| The scripts | `setup_character_layers.py` puts the layer into `ABP_VoxelCharacter`; `setup_character_ik.py` builds its IK chain and play rate; `setup_character_locomotion.py` builds the motion matching copy and adds the same pieces to it. Each finds its nodes by name in a named graph |
| The lane | `CharacterTest`, `vox.Lane` (run-up, 90 and 180 pivots, strafe, stop; foot slide per leg), `vox.Stop`, `vox.FootSlide` (C0) |

### 1.2 The packs' locomotion

| Set | Pack folder (UE5) | Locomotion clips |
|---|---|---|
| Bow | `BowAndArrow/.../Sequence` | Three holds, `A`, `B`, `C`, each with its idle, transitions between them (`Idle_A-to-B` ...), walk start, loop, stop in six directions (`C`'s in FL/FR/L/R/BL/BR), jog start, loop, stop in six (`A` and `B` only), crouch idle (begin, loop, end) and crouch walk start, loop, stop (a slow crouch walk too), turns in place 90/180, a forward run and its stop, jump begin, loop, landings (one into a jog), and aim, draw, release per hold, standing and crouched. A `UE5_Unarmed_Idle` |
| SwordShield | `SwordShield/.../SSH` | Idle (normal, combat, the transitions), walk loops and jog start, loop, stop in six directions, each also in a guard variant, turns 90/180, three forward runs and a run stop, jump begin, loops, landings (one into a jog). No walk start or stop, no crouch, no pivots |
| TwoHand | `SwordShield/.../2H` | The same locomotion as `SSH` |
| Unarmed | the template Mannequin (`ContentPacks/.../Unarmed`) | 8-direction walk and jog loops, an idle, jump, fall loop, land. No starts, stops, turns, crouch or run. The packs add two unarmed idles (the bow pack's; the sword pack's with the sword on the back or hip). The animator's unarmed pack is not owned |
| Staff | none | The animator's staff pack is not owned |

What the earlier sets did with this: each laid its pack's forward idle, walk and jog over the upper body only, on
GASP's legs and hips. So the bow's carry (the hands 3 to 8 cm from their idle place at a walk, 12 to 28 at the jog,
G171) was never seen with the pack's own legs and pelvis under it, and with the pack's arrow missing from the right
hand (our arrow shows only while drawn) and the pack's skeletal bow on a `weapon_l` bone our skeletons lack. That is
the likeliest reason the pack's carry, held in front with bow and arrow together, read as a stiff chest carry in play.

Curves: none on any pack clip; root motion: in the root bone of every locomotion clip (both read in L0, §4). The packs and GASP's
folder are junctions shared by every checkout and never edited: a curve or a sync marker has to go on a copy.

### 1.3 What GASP still provides besides the locomotion

The `GameAnimationSample` pack stays installed whatever this plan decides: Kellan, the smooth worlds' default body
(`setup_character_appearances.py`, `BP_Kellan` from its `MetaHumans`), the get-up montage (`GET_UP`, the revive of the
sword sets), the wind sound (`Config/DefaultGame.ini`), its gameplay tags (`Config/DefaultGameplayTags.ini`). So the
comparison graph costs no extra content.

### 1.4 The engine pieces

- `AnimationLocomotionLibrary` and `AnimationWarping` are enabled. Distance matching reads a `Distance` curve, made by
  the editor's `DistanceCurveModifier` from root motion; orientation and stride warping work in component space, which
  follows the capsule, so they hold on a planet given gravity-frame inputs.
- `UAnimCharacterMovementLibrary::PredictGroundMovementStopLocation` and `...PivotLocation` take world Z as up.
- `UVCAnimAuthoringLibrary` adds nodes, sets properties, links pins and imports node text into a graph; no state
  machine helpers.

## 2. What dropping GASP as the locomotion costs (the friction)

| Area | Friction | How the plan meets it |
|---|---|---|
| Content gaps | Unarmed has loops only: no starts, stops, turns, crouch or run until its pack; no set has pivots; SSH and 2H have no walk start or stop and no crouch | Every clip family is optional and its absence degrades, never blocks (§3.3, D88). Unarmed is the weakest set until its pack: a step back from GASP's unarmed, which is the set players see most |
| Crouch | Today GASP crouches every set. Only the bow has crouch clips | A set without them crouches on a borrowed set's crouch legs under its own idle upper body (the split body, §3.3); judged in L3, else crouch waits for content |
| Pivots and feel | Motion matching picks a pivot or a plant on a reversal; the packs have none | Reversals are a stop-then-start, or an inertialized turn of the cycle with orientation warping; pivots return if a pack brings them |
| Decisions it reopens | D44 (motion matching for the player), D85 (the bow carried by the locomotion's arms on the move), D72 (the stance over the moving upper body), D75 (the gait table is GASP's) | D86, D87, D91 below; under the motion matching graph each keeps its meaning (the sideline is unchanged) |
| The gait table | Its speeds are GASP's (C4, `Tuned` is GASP's table verbatim); the packs' loops have their own authored speeds | D91: the speeds follow the packs' clips (L0 measures them); motion matching matches any speed, so the comparison loses nothing |
| Three graphs | `ABP_VoxelCharacter` (NPCs, server), the motion matching copy (sideline), the new graph: the three content scripts wire the layer, slots and IK into each | The motion matching copy is frozen at today's wiring: new features land in the new graph only; the comparison is of locomotion feel, not features. Its script keeps running for a fresh checkout |
| Root motion and curves | Distance matching needs root motion in the clips; the packs' is unknown | L0 checks. Without it, starts and stops play by time at a play rate; distance matching is a later feature |
| Arrow in hand | The pack's carry holds an arrow; ours shows only while drawn | L1 judges the carry without one; a nocked-arrow visual on `hand_r` at the carry is a backlog item (§5) |
| Turn in place on Unarmed | No clips | The procedural turn `ABP_VoxelCharacter` has (`RootYawOffset`) until the pack |
| Learning the graph | No helpers to build a state machine by script | L0 tries node-text import; else helpers in `UVCAnimAuthoringLibrary` |

## 3. Design

### 3.1 One graph, clips by action set

`ABP_VCPlayerLocomotion`, parent `UVCLocomotionAnimInstance : UVCAnimInstance` in the runtime module (it needs
`AnimationLocomotionLibrary` and `AnimationWarping`, both runtime, not PoseSearch). Built by
`Scripts/setup_character_state_machine.py` into `/Game/CharacterAnimation/Locomotion` (D90).

Each action set names its full-body locomotion in a data asset, `UVCLocomotionClipSet` (on `UVCActionAnimSet`, D87):
the idle, the loops by gait and direction, and, where the pack has them, starts, stops, turns, crouch, run, jump. The
anim instance resolves the held set's clip set and the graph's players read their clips from it, so a new pack is a new
data asset, not a new graph or layer. A set change while moving blends by inertialization. The stance layer remains for
a set with no clip set of its own: such a set plays the base (Unarmed) clip set and lays its stance over the upper
body, as today. While the held set has its own clip set the main instance gives its layer a `StanceWeight` of 0, so
the clip's own arms show.

| State | Plays | Leaves when |
|---|---|---|
| Idle | the set's idle (its crouch idle crouched) | input or speed: Start; a turn past `TurnInPlaceAngle`: TurnInPlace; airborne: Jump |
| TurnInPlace | the set's 90 or 180 turn, `RootYawOffset` unwinding with the clip's root yaw; without clips the procedural turn | the clip ends, or input |
| Start | the set's start for the gait and direction | at the loop's speed or the clip's end: Cycle |
| Cycle | the set's loops by gait (walk, jog, run as the sprint, crouch walk) in a blend space by local direction, orientation warping between the six directions, the play rate against the clip's speed | no input: Stop |
| Stop | the set's stop for the gait and direction | stopped: Idle; input: Start |
| Jump / Fall / Land | the set's jump begin, loop, landing (into a jog when moving) | on the ground: Idle or Cycle |
| Swim | `AS_Swim`, as both graphs now | out of the water |

### 3.2 The split body and the look

After the state machine, in this order and with these names, as in `ABP_VoxelCharacter`: the `UpperBodyStance` layer
(implemented by the graph itself, the base pose through), the cached pose `VCStanced`, the `UpperBodyAction` slot on
the three-bone ramp (draws, attacks, digs while the legs keep the locomotion), the default slot, foot placement and leg
IK, `FVCAnimNode_FootBoneIK`, look-at, off-hand IK. The bow's aim branch stays in its layer and lays over whatever the
locomotion plays. `UVCAnimLayerInstance` is unchanged. The three scripts learn the new graph as a target.

### 3.3 Missing clips (D88)

| Missing | Instead |
|---|---|
| Start | inertialize from Idle into Cycle |
| Stop | inertialize from Cycle into Idle |
| Walk start or stop (SSH, 2H) | the jog's, played at the walk's rate, else as above |
| Turn | the procedural turn (`RootYawOffset`) |
| Run | the jog at the play rate |
| Crouch | a borrowed set's crouch clips (the bow's, the one set with them) on the legs, the set's own idle on the upper body by the three-bone ramp; judged by eye in L3 |
| Pivot | stop then start, or the cycle's inertialized turn |

### 3.4 The gravity frame

Every value the state machine reads is measured about local up (`UVCAnimInstance` already does). If distance matching
comes (L0 finds root motion), the stop is predicted in the tangent plane: the engine's arithmetic with local up for Z.

### 3.5 The switch and the sideline

`vc.Anim.MotionMatching` becomes `vc.Anim.Locomotion 0|1|2`: 0 the Blueprint's (`ABP_VoxelCharacter`), 1 motion
matching, 2 the new graph (`UVCSettings::PlayerStateMachineAnimClass`). The old name stays as an alias. The default
stays 1 until L4 flips it to 2; after that 1 remains for comparison as long as the GASP pack is installed (it always is
here, §1.3), and a project without GASP loses only that choice (the switch already falls back when a class does not
load). The dedicated server and the tests keep the Blueprint's.

## 4. Phases

### L0: Look and measure (S)

In the editor: the bow's holds `A`, `B` and `C` side by side, which one the user means by the carry in front, which the
set plays; root motion and the authored speed of each set's walk, jog and run loops (for D91); sync markers; whether a
state machine can be put into a graph by node-text import. The motion matching baseline on `CharacterTest`: `vox.Lane`
on the low-poly body and on Kellan, three runs each (the slide per leg and gait, `vox.Stop`), the CSV profiler's
animation cost.

#### L0 results (2026-11-10)

Read in the editor on `claude/locomotion-l0`; nothing in the packs changed (the scripts are read only; the scratch
content under `/Game/L0Scratch` was deleted after).

**1. The bow's holds.** Every pack clip animates the skeleton's `bow`, `bow_string`, `arrow_bow_string` and
`arrow_hand_r` bones, so the authored carry is a bow with an arrow nocked on its string. Placed by those bones on the
pack's own mannequin (the sheet shown to the user on 2026-11-10; the pack's demo places its bow the same way, its `bow` bone at
(31, 9, 75) cm in its idle against (31, 9, 77) read from the clip, and hides its arrow until a draw):

| Hold | The carry | The right hand | Locomotion |
|---|---|---|---|
| `A` (the Bow set's today, `setup_character_layers.py`; the demo's default) | The bow flat and low in the left hand at the hip, string up, the arrow nocked | Free: 60 cm from the nock at the idle, 67 at the walk, swinging | Walk and jog start, loop, stop in six directions, run, crouch |
| `B` | The bow flat across the front of the hips, string up, the arrow nocked | On the nock: 14 cm from it at the idle, the walk and the jog | As `A` |
| `C` | As `B`, the body turned about 90 degrees from the travel (a sideways stance) | On the nock, 14 cm | Walk only (start, loop, stop in FL, FR, L, R, BL, BR), the slow crouch walk; no jog, run or crouch walk |

"The bow and the arrow together in front of the character" reads as `B` (or `C`, sideways): both hands meet on the bow
and the nocked arrow in front. `A` carries them together only in the sense that the arrow sits on the bow; the right
arm swings free. **The user chose `B`** (2026-11-10, "B, both hands"; D92): the Bow clip set plays `B`'s idle, walk,
jog, run, starts, stops and turns; `B`'s crouch is the crouch. `C` reads as the walk while aiming (the user: "sounds
like C is for walking while aiming"): side-on to the travel as an archer stands to shoot, walk only, its own `Aim_C`,
`Draw_C` and `Release_C`. Proposed for when the drawn bow moves (L3 or the backlog): while drawn the legs play `C`'s
walk by direction, which is the lane's "strafe +Y, bow drawn" leg. `A` stays for the backlog's holds as states.

**2. Per set.** Measured from the clips' bones: the root bone's displacement over the clip, and
the planted ball's speed against the root, which agree within a few uu/s.

| Set | Root motion | Walk loop | Jog loop | Run | Starts / stops (root distance) | Sync markers, curves |
|---|---|---|---|---|---|---|
| Bow (`A`, `B`; `C` walk) | In the root bone, every locomotion clip; `bEnableRootMotion` off except six landings | 91.5 uu/s, every direction | 302 uu/s, every direction (`A`, `B`) | `Run_01` 446, `Run_02` 920 (a sprint), run stop 218 uu | walk start 58 uu / 0.6 s, walk stop 30 / 0.83, jog start 115 / 0.5, jog stop 50 / 1.17 | none, none |
| SwordShield (`SSH`) | As the bow (landings flagged) | 92.3 | 306 | `Run_01`, `_03` 378, `Run_02` 920, run stop 218 uu | jog start 67 / 0.5, jog stop 30 / 1.17; no walk start or stop | none, none |
| TwoHand (`2H`) | As `SSH` | 92.3 | 306 | as `SSH` | as `SSH` | none, none |
| Unarmed (template) | In the root, flag on | 300, every direction | 600, every direction | none | none | `L`/`R` foot markers on every loop; no curves |
| Bow crouch | In the root | slow crouch walk 114, crouch walk 302 | | | crouch walk start 58 / 0.6, stop 59 / 0.83 | none |

The pack's own demo blend spaces play these faster than authored: the bow's walk at 112.5 (1.23) and jog at 300; the
sword sets' walk at 100, jog at 500 (1.63) and `Run_01` at 800 (2.1). Every directional loop of a set has one speed,
the strafes and backs included. Every clip is free of curves, so distance matching needs a `Distance` curve made on a
copy (the root motion is there to make it from) and sync groups need markers on copies too (the template's loops have
them).

**3. A state machine by script: yes.** `UVCAnimAuthoringLibrary::ExportGraphNodes` / `ImportGraphNodes` (the editor's
copy and paste) carry a whole state machine: `ABP_VoxelCharacter`'s `Locomotion` (8 states, 23 transitions, their
state and rule graphs) pasted into a throwaway ABP on the same skeleton, wired to the output pose by `ConnectPins`, and
compiled clean. Text rewritten before the paste imports too (a blend space path swapped for the bow's). States paste
into an existing state machine's graph, alone (`Land` arrived as `Land_1`) or with their transitions (`Fall_1 to
Land_2`); a name that clashes is made unique, and a transition whose ends are left behind arrives dangling and still
compiles, so the script must check what arrived. Renaming a state by editing the text broke the state (only its
transition arrived): names come from the paste or a helper. So D90's script can keep a template state machine as text
and paste it, or paste states one by one; a `RenameState` (and perhaps `AddTransition`) helper is the one gap.

**4. The motion matching baseline** (`CharacterTest`, the bow in hand, `vc.Move.Preset Tuned`, the weather locked
clear, the wind off, the editor's background throttle off; `Scripts/qa/lane_pass.py`, three runs; `Saved/QA/lane/l0_mm_*`):

The foot slide per leg in uu/s, the mean of three runs (the range in brackets); relative, as C0's measure counts the
swing's low pass too, so compare graphs, not against zero:

| Leg | Low-poly body | Kellan |
|---|---|---|
| run-up +X | 124 (117-136) | 168 (164-174) |
| pivot 90 to +Y | 145 (143-146) | 121 (116-129) |
| pivot 90 back to +X | 141 (134-149) | 68 (55-77) |
| pivot 180 to -X | 151 (148-158) | 137 (135-139) |
| pivot 180 back to +X | 76 (74-81) | 184 (180-191) |
| strafe +Y, bow drawn | 87 (86-87) | 179 (174-184) |
| released, -Y | 104 (104-104) | 195 (167-248) |
| straight +X | 134 (133-135) | 138 (95-218) |
| whole lane, walk gait | 83 (79-91) | 133 (130-135) |
| whole lane, run gait | 136 (134-138) | 166 (162-172) |
| `vox.Stop` from 500 uu/s | 0.27 s, 67 uu, every run | 0.27 s, 67 uu, every run |

Close to C4's pass with `Tuned` (150 and 160 over the whole lane's run): the low-poly body is steady between runs,
Kellan's release and last straight vary as much as C4 saw. The stop is the movement's (GASP's 2000 uu/s^2 braking),
the same on both bodies; the new graph's stops (L2) are judged against these 67 uu.

The CSV profiler over one lane (one player, in-editor PIE, about 840 frames each, the bow in hand, `CsvProfile` around `vox.Lane`, `vc.Anim.MotionMatching 1` then 0),
in ms a frame, mean (p95):

| | Game thread, `Animation` | Workers, animation evaluation | Game thread, whole frame |
|---|---|---|---|
| Low-poly, motion matching | 0.28 (0.37) | 0.97 (1.31) | 9.8 |
| Low-poly, `ABP_VoxelCharacter` | 0.14 (0.17) | 0.40 (0.56) | 9.0 |
| Kellan, motion matching | 0.55 (0.78) | 4.20 (5.44), three tasks | 13.6 |
| Kellan, `ABP_VoxelCharacter` | 0.36 (0.51) | 3.38 (4.16), three tasks | 12.3 |

Motion matching costs about 0.15-0.2 ms of game thread and 0.6-0.8 ms of worker over the Blueprint's graph on either
body; Kellan's evaluation is mostly his MetaHuman parts (three tasks), whatever the locomotion. (The figures in
`CHARACTER_SYSTEM.md`, 0.50 / 1.39 against 0.22 / 0.33, were a running player, not a lane; this lane is the L3
comparison's baseline.) The new graph's target from §7: near `ABP_VoxelCharacter`'s plus warping.

The packs' turns in place carry their whole turn in the root (90 and 180 degrees exactly, no travel, 1.33 s), so
TurnInPlace can unwind `RootYawOffset` from the clip's root yaw as §3.1 plans.

**What it changes (proposals for the user, not decisions).**

- D91 as written would slow the player a lot: following the clips, the run (the jog loops) is about 300 uu/s against
  today's 500, the walk 92 against 200, and the sprint either 378-446 (`Run_01`, slower than today's run) or 920
  (`Run_02`). The packs' author plays the sword jog at 1.63 in his own demo. Proposed: D91 becomes "the gait table
  retuned around the clips within a play-rate band" (for example walk 120, run 400, sprint 700: the walk at 1.3, the jog
  at 1.33, the sprint between `Run_01` at 1.6 and `Run_02` at 0.76 by a blend on speed), stride warping taking part of
  the gap, judged on the lane in L2. The user picks the speeds.
- Unarmed's template loops are authored at 300 and 600, the packs' at 92 and 300: one gait table cannot fit both at a
  play rate of 1. Either the table's speeds are per action set (a row set per clip set, `UVCLocomotionClipSet` naming
  its speeds), or Unarmed plays its walk at about 0.4 at a shared walk speed. Proposed: speeds on the clip set (L1), the
  gait table's caps from the held set.
- Root motion is in every pack clip, so distance matching for starts and stops is possible (a `Distance` curve on
  copies, L2 or the backlog); until then the starts and stops play by time, as §2 plans.
- `C` has no jog or run, so it is the walk while drawn, not a carry (D92): the drawn bow walks side-on in `C`'s six
  directions, and a run while drawn falls back to `B`'s jog under the aim layer (§3.3's rules).
- The pack's arrow: its carry includes a nocked arrow (the bones), which the demo hides until a draw. A nocked arrow on
  `arrow_hand_r` in the carry (§5) is a visual choice; our skeletons lack the bow bones, so it would be placed on
  `hand_r` by an offset per hold.

### L1: The mechanism (M)

`UVCLocomotionAnimInstance`, `UVCLocomotionClipSet` and its place on `UVCActionAnimSet`, the switch (§3.5), the script
building the graph with Idle, Cycle, Jump/Fall/Land and Swim, the split body and the IK (§3.2), the clip sets for Bow,
SwordShield, TwoHand (and Tool as the two-handed stand-in, D51), Unarmed on the template loops. Pure functions and
tests: the clip choice by set, gait and direction; the stance weight 0 under a set's own locomotion; a set change.
Done when every set walks, jogs and jumps in its own clips under `vc.Anim.Locomotion 2`, the draws and attacks play over
the moving legs, the look-at and the bow's aim as under 1.

### L2: What the packs have between the loops (M)

Start, Stop, TurnInPlace from the packs, the missing-clip rules (§3.3), the run as the sprint, the gait speeds (D91).
Done when `vox.Lane` runs clean on both bodies with each set and the stops land within the gait table's distance.

### L3: Crouch and the tuning pass (M)

The bow's crouch; the borrowed crouch for the others (kept or dropped by eye); the tunables as properties with
`vc.Anim.SM.*` overrides; a pass by hand with the user on `CharacterTest` and at `PlanetTest`'s equator, each set, both
bodies, against motion matching under `vc.Anim.Locomotion 1`.

### L4: Close (S)

The default flips to 2 (D44 reversed), motion matching stays selectable; the plan is frozen and its result goes into
`CHARACTER_SYSTEM.md` "Animation" and the console guide. NPCs and the server staying on `ABP_VoxelCharacter` or moving to
the new graph is recorded as a follow-up.

## 5. Backlog (features as they are wanted)

Each is a later piece of work on the same graph, taken when the content or the need arrives: the unarmed pack's clip
set (and Unarmed's turns, starts, stops, crouch); the two-handed pack's, if it differs from the sword pack's `2H`; a
staff set (`Equipment.AnimSet.Staff`, its pack's clip set, an item tagged); the bow's holds as states (`A`/`B`/`C` and
the pack's transitions, e.g. lowered while exploring, ready in combat; `C`'s walk while drawn, D92); the SSH guard variants while blocking; a nocked
arrow on `hand_r` in the bow's carry; distance matching (if root motion); pivots; idle breaks; NPCs on the graph.

## 6. Decisions (agreed 2026-11-10, each as recommended)

| ID | Question | Recommended | Alternatives |
|---|---|---|---|
| D86 | The player's locomotion | A state machine of our own over the Essential Animation packs, built feature by feature; motion matching kept selectable beside it for comparison, the default flipped at L4 (D44 reversed then) | GASP's clips under our state machine (the first draft); keep tuning motion matching |
| D87 | Where the clips come from | Each action set names a full-body clip set (`UVCLocomotionClipSet` on `UVCActionAnimSet`), one graph plays the held set's; a set without one plays Unarmed's under its stance layer. The bow plays its pack's carry under the new graph (D85 holds under motion matching only) | A linked anim layer per set holding its own state machine (Lyra's); the stance over shared legs as now |
| D88 | A clip family a set lacks | Degrade, never block (§3.3); Unarmed on the template loops until its pack | Wait for the content before a set moves to the new graph |
| D89 | Where the choice lives | C++ on `UVCLocomotionAnimInstance` (pure, tested, gravity frame); the tunables as properties with cvar overrides | Blueprint thread-safe functions in the graph |
| D90 | How the graph is made | By script into the content pack, its numbers in data so a rebuild keeps them | A tracked asset edited by hand (as `ABP_VoxelCharacter`) |
| D91 | The gait speeds | Follow the packs' clips (L0 measures the walk, jog and run loops), the gait table retuned once; motion matching unaffected | Keep GASP's speeds and stretch the packs' clips by play rate |
| D92 | The bow's carry under the new graph (the user after L0, 2026-11-10: "B, both hands") | Hold `B`: the bow flat across the front of the hips, both hands on the bow and the nocked arrow; its clips for every family the pack has. `C` (side-on, walk only) is the walk while aiming, proposed for later | `A` (today's stance, the right arm free) |

## 7. Risks

- **Authoring a state machine by script.** Node text import may not carry states and transitions; L0 finds out, the
  fallback is helpers in `UVCAnimAuthoringLibrary` or D90's alternative.
- **Unarmed looks worse before it looks better.** It is the most-seen set and the thinnest until its pack.
- **The bow's carry still reads wrong** with the pack's legs under it: then the hold (`B` or `C`) or the missing arrow is
  the next lever, not a return to D85's fade.
- **Kellan's legs.** His stride is shorter than the clips' mannequin; the play rate and stride warping carry it
  (`vox.FootSlide` per body).
- **Cost.** Expected near `ABP_VoxelCharacter`'s plus warping; measured in L0 and L3.

## 8. Not in scope

The NPCs' and the server's graph (L4 records it), the creatures (`ABP_VCCreature`), first-person arms (D57), buying the
animator's other packs (the user's call; §5 takes them when they arrive), removing GASP's pack or the motion matching
module.

## Inputs

`CHARACTER_SYSTEM.md` "Animation", "Movement", "Appearance"; `CHARACTER_ANIMATION_PLAN.md` (D44, D45, D50, frozen);
`CHARACTER_REFINEMENT_PLAN.md` (C0's lane, C2's stance, C4's gait table, frozen); FINDINGS D85, G171; the scripts
`setup_character_layers.py`, `setup_character_ik.py`, `setup_character_locomotion.py`,
`setup_character_appearances.py`; `VCCharacterBase.cpp` `ApplyLocomotionAnimClass`; the pack folders in §1.2.
