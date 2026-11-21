# Character Animation Plan

> **Frozen 2026-11-07.** This plan is history: agreed 2026-11-05 as drafted, every decision as recommended (D43-D51,
> §7), P0-P8 merged 2026-11-05 to 11-07. Built: the spikes S1-S4 (P0); the content packs and skeleton compatibility
> (P1); `UVCAnimInstance` in the gravity frame for every character (P2); anim layers, action sets and their montages
> (P3); foot placement, the head's look-at, the off-hand grip and the sprint's pace (P4); the player's motion matching
> in the module `VoxelCharacterAnimation` (P5); bodies as data, MetaHumans among them, and the foot-bone IK (P6); the
> NPCs' skeletal bodies within an animation budget (P7). As it is now: `CHARACTER_SYSTEM.md` ("Animation",
> "Appearance", "NPCs" and its "Bodies", "Multiplayer"); open: G145, G147 and G148 (`FINDINGS.md`).
> Scope: the player's and the NPCs' animation, IK and bodies, MetaHumans among them. Owner:
> VoxelCharacterPlugin; content in VoxelWorldPOI's NPC tables, EquipmentPlugin's item data and the Blender pipeline.
> Terms: `Documentation/GLOSSARY.md` at the project root; the decisions' numbers are from its `FINDINGS.md`.

## 1. The question

Should the characters and NPCs be built on an Unreal system, or on the animation packs in the Fab library?

Both, at different layers. The system is Epic's animation stack, with the **Game Animation Sample (GASP, updated for
5.8)** as its reference build. GASP is free and ships motion matching, foot placement and a MetaHuman setup. Its
animations are on the **UEFN Mannequin**, and it retargets them each frame to every visible body (UE5 Manny, MetaHumans
and others). The Fab packs are content we put into that stack: they fill what GASP lacks (sword and shield, two-handed
weapons, bow, swimming, mining). They are on the **UE5 Mannequin**. So the project has two animation skeletons; D50
(decided after spike S4) keeps both and converts nothing: GASP's set plays on UE5 Mannequin and MetaHuman bodies through
skeleton compatibility.

The one 5.8 limit that matters here: motion matching's trajectory prediction assumes world Z is up, and our planets
are not Z-up (§3). That is why motion matching gets a spike before any work depends on it.

## 2. What exists today

| Area | State |
|---|---|
| Player body | `BP_VCPlayer`: `SKM_Grian07` (Fab "CR" modular low-poly character) on the UE5 Mannequin skeleton |
| Player anim | `ABP_VoxelCharacter` (parent `UVCAnimInstance`): idle/walk/run blend space, jump, fall, land, swim (`AS_Swim`). No attack, tool, hit-react or death animation; no IK; `AimPitch` / `AimYaw` computed but unused |
| `UVCAnimInstance` | Speed, direction, aim, falling, crouch, view mode, surface, swimming, `ActiveItemAnimType`, all in the gravity frame already. It casts to `AVCCharacterBase`, so an NPC (`AVCNPCCharacterBase` is a separate `ACharacter`) gets nothing from it |
| Item anim type | `EVCEquipmentAnimType` is set to `Tool` for anything in the main hand (`VCCharacterBase.cpp`, `HandleItemEquipped`): a placeholder |
| Equipment layers | EquipmentPlugin already links an Equipment fragment's `AnimLayerClass` with `LinkAnimClassLayers` on equip (the Lyra pattern). Nothing uses it yet |
| Melee | `UVCMeleeAttackAbility`: server-only, an instant sweep, no montage, no hit timing |
| First person | A separate `FirstPersonArmsMesh` with arms sockets (`FVCEquipmentSocketMapping`) |
| NPCs | `BP_Wolf`, `BP_Deer`, `BP_DungeonSkeleton`: static meshes from the Blender pipeline (`SM_Wolf`, ...), no skeleton, a topple on death. The only humanoid NPC is the dungeon skeleton |
| NPC cost | 48 NPCs: 2.3-2.4 ms a frame on the game thread, about 85% of it movement (`CHARACTER_SYSTEM.md`, "NPCs at scale") |
| Engine plugins | None of PoseSearch, Chooser, AnimationWarping, MotionWarping or MetaHuman is enabled (IK Rig and Control Rig are on by default) |

**Animation content in the project** (every pack is untracked and reaches a worktree through `content_packs.txt`):

| Pack | Contents | Skeleton |
|---|---|---|
| `ContentPacks/Characters/Mannequins` | Template locomotion, unarmed attacks (3 and a charged one), pistol and rifle sets, hit reacts, six deaths | UE5 Mannequin |
| `EssentialAnimation/SwordShield` (space.bar.anim's Essential Sword & Shield) | 406 UE5 sequences: sword and shield (220) and two-handed (181) attacks, combos, air and plunge attacks, guard locomotion, dodges, rolls, hurt variations, equip, turn in place, executions | Its own copy of the UE5 Mannequin |
| `EssentialAnimation/BowAndArrow` (space.bar.anim's Essential Bow and Arrow) | 912 UE5 sequences (468 of them aim-offset poses): bow locomotion, draw, aim, release, reload, attacks, guard, hurt, die | Its own copy |
| `Fab/WaterAnimations` | Swim, underwater swim, deep dive, back swim, crawl (plus boards and kayaks) | Its own copy, plus an auto-generated retargeter |
| `Fab/CR` | The modular low-poly character (Grian 01-10), basic Manny/Quinn locomotion | Its own copy |

That makes five copies of the Mannequin skeleton. An animation plays only on the skeleton asset it is bound to, so the
copies are consolidated first (P1). The two space.bar.anim packs are the first two Fab links of 2026-11-05: they were
imported in February, so they need no download.

**Outside the project:**

| Source | Contents | Skeleton |
|---|---|---|
| Game Animation Sample 5.8 (`D:\UnrealProjects\GameAnimationSample`, 6.7 GB) | About 2,300 animation assets on the UEFN Mannequin: walk (452), run (457), crouch (431), sprint, idle, jump (145), traversal (145), interactions (187), slide, ragdoll and get-ups, look-at, aim offsets; 199 motion-matching data assets (databases, schemas, channels, choosers). Two characters, `SandboxCharacter_CMC` and `SandboxCharacter_Mover`. Bodies retargeted at run time by `ABP_GenericRetarget` (`RetargetPoseFromMesh`) with one IK retargeter per body: UE5 Manny, UE4 Mannequin, Echo, Twinblast, MetaHuman normal and overweight. A sample MetaHuman (Kellan) | UEFN Mannequin |
| PROTOFACTOR's Animal Variety Pack (free; not downloaded) | Wolf (26 animations), deer stag and doe (20 each), fox (26), pig (20), crow (17); 10-11K triangles an animal (the crow 2K), 4096 textures (the crow 2048), 31-35 bones (the crow 61). A 2018 pack, upgraded on import; the clip names are read after the download | One skeleton per animal |
| VanillaLoop's Free Sample Animation Set (the third link; not downloaded) | 15 loops, one from each of the seller's packs: a two-handed pickaxe mining loop, picking up from the floor and a light object, opening a locker, a forward roll, a downed crawl, stairs, ladder, heavy push, rope pull, cover walk, flashlight idle, a run and a jog. Each either root motion or in place; IK bones included | UE5 Mannequin |

**S3, the audit (done).** Against the game's needs:

| Need | Source | State |
|---|---|---|
| Locomotion, starts, stops, pivots, jump, land, crouch | GASP | Have |
| Sword and shield, two-handed melee | SwordShield | Have |
| Bow | BowAndArrow | Have |
| Unarmed attacks | Template mannequin | Have (3 swings) |
| Swimming at the surface and under water | WaterAnimations | Have |
| Hit reacts, deaths | Template, SwordShield, BowAndArrow; GASP's ragdoll and get-ups | Have |
| Downed (`State.Downed`) | VanillaLoop's crawl | After its download |
| Digging with a pickaxe | VanillaLoop's two-handed mining loop | After its download; one loop, no one-handed version |
| Picking up | VanillaLoop's pickups | After its download |
| Chopping with an axe | None; a two-handed attack or the mining loop stands in | **Gap** |
| Placing, using, eating | None; a pickup stands in | **Gap** |
| Quadrupeds (wolf, deer) | The Animal Variety Pack; fox, pig and crow besides | After its download |

## 3. Unreal Engine 5.8: what to use

Checked against the installed engine (`D:\Program Files\Epic Games\UE_5.8`) where it matters:

| System | Status in 5.8 | Use here |
|---|---|---|
| UE5 Mannequin skeleton | Standard | **The body skeleton** for every humanoid and the skeleton of our own animations (D50) |
| Game Animation Sample (5.8) | Free sample project (Fab) | Reference implementation and the locomotion animation set (on the UEFN Mannequin); its CMC character is the one to follow (our movement is CMC); copied in, not linked |
| PoseSearch (motion matching) + Chooser | Production | Player locomotion, if spike S1 passes |
| AnimationWarping: foot placement, orientation, stride and slope warping | Production | IK and warping. Foot placement traces along the owning component's down vector (`AnimNode_FootPlacement.cpp`: minus the owning component's up vector), so it works on a planet as long as the mesh stands upright to gravity, which it does |
| IK Rig / IK Retargeter | Production | Retargeting the UE4 sets, and MetaHuman bodies if they need it |
| Control Rig | Production | Hand IK on two-handed items, quadruped legs later |
| Motion Warping | Production | Attack lunges toward the target |
| Animation Budget Allocator, update rate optimisations | Production | NPC cost |
| MetaHuman Creator (in-engine, `MetaHumanCharacter`) | **Beta** | Making MetaHumans. Body skeleton `metahuman_base_skel`; `UMetaHumanComponentUE` runs the body and face post-process AnimBPs by component name (`Body`, `Face`) |
| MetaHuman Crowd | **Experimental** | Not yet; revisit for many humanoid NPCs |
| Mover | **Experimental** | No. `UVCMovementComponent` (CMC with planet gravity, swimming, block steps, prediction) stays |
| UAF (AnimNext) | **Experimental** | No. Keep the logic in C++ data so it could move later |

**The gravity problem, in the engine's source.** `PoseSearchTrajectoryLibrary.cpp` predicts the trajectory from the
CMC with world-Z assumptions:

- it reads the controller's facing as a world yaw (`FRotator(0, DesiredControllerYaw, 0)`, line 119);
- it turns the predicted facing about world Z (`FQuat::MakeFromEuler(FVector(0, 0, YawRate * dt))`, line 451);
- it snaps samples to the floor along world Z (`FVector::UpVector * 3000`, line 965).

The fall prediction already respects gravity (lines 887-939). GASP's CMC AnimBP (`SandboxCharacter_CMC_ABP`)
generates its trajectory with exactly this library (its references: `PoseSearchTrajectoryLibrary`,
`HandleTransformTrajectoryWorldCollisions`), then runs motion matching through blend stacks, orientation warping and
foot placement. A forum report of GASP under planet gravity matches this: the trajectory behaves as if the world were
flat. The engine exposes
`IPoseSearchTrajectoryPredictorInterface` (`Predict`, `GetGravity`, `GetCurrentState`, `GetVelocity`,
`GetAngularVelocity`; marked Experimental), so a predictor of ours can work in `FVCGravityFrame`. S1 proves that.

**MetaHumans in GASP** run through a retarget: an invisible Mannequin runs the motion-matching AnimBP, and the
MetaHuman `Body` runs `ABP_GenericRetarget` (a runtime retarget each frame, chosen by a component tag per body type),
with the outfit parts on leader pose. That is fine for one player. For NPCs it is a second pose evaluation per
character, so S2 also measures playing Mannequin animations on the MetaHuman body directly.

**The skeletons' bones**, from the assets' name tables (S4 and S2 confirm): the UEFN Mannequin and UE5 Manny share the
whole body hierarchy (pelvis, `spine_01`-`05`, arms, legs, fingers, twist bones, the `ik_*` bones). UEFN adds
attachment, camera and prop bones; Manny adds `thigh_twist_02`, `center_of_mass` and `interaction`. The MetaHuman body
(`metahuman_base_skel`) has Manny's body bones but none of the `ik_*` or `weapon_*` bones, so foot placement on a
MetaHuman uses the foot bones and weapons use sockets.

## 4. Architecture

```
Gameplay (server-authoritative)              Animation (visual, on every machine)
───────────────────────────────              ─────────────────────────────────────────────────────────────
UVCMovementComponent: velocity, gravity ──▶  UVCAnimInstance: gather on the game thread, compute thread-safe,
UVCCombatComponent: dead, downed, hit   ──▶    everything spatial in the gravity frame
GAS abilities: montages (replicated)    ──▶  1. Base locomotion
EquipmentPlugin: AnimLayerClass         ──▶       player: motion matching (PoseSearch + Chooser), or the fallback
                                                  NPC: state machine + blend spaces (cheap, same inputs)
                                             2. Linked layers (ALI_VCCharacter): Unarmed | SwordShield | Bow |
                                                  Tool (pickaxe, axe) | TwoHand; linked by the equipped item
                                             3. Montage slots: UpperBody, FullBody (attack, dig, chop, place, use,
                                                  hit react, death, revive)
                                             4. Post-process: foot placement + pelvis, orientation and stride
                                                  warping, look-at, hand IK
                                             5. Body: the Mannequin skeleton drives the appearance
                                                  (MetaHuman Body/Face/outfit | modular | creature)
```

### Principles

1. **One body skeleton, compatible animation skeletons (D50).** Every humanoid body is on the UE5 Mannequin or a
   skeleton marked compatible with it (a MetaHuman's `metahuman_base_skel`); our own animations and the Fab packs are
   on the UE5 Mannequin; GASP's set stays on the UEFN Mannequin, marked compatible both ways, and plays on any of these
   bodies directly: the engine remaps bones when it samples, motion matching's pose history too (spikes S4 and S2). A
   body (MetaHuman, modular character, the dungeon skeleton) is a skin, never a reason for a second set of animations.
   Not GASP's way (an unseen UEFN body animated, each visible body retargeted from it every frame), so no character pays
   for a second pose; not a conversion, so a GASP update is taken as it comes. The conversion stays the fallback for a
   body whose proportions show a fault the remapping causes (S4's notes).
2. **Animation follows gameplay; gameplay never reads a pose.** The server decides (movement, abilities, damage);
   animation shows replicated state. A hit's timing comes from the montage asset's data (a notify's time, read once),
   not from the server evaluating poses, so a dedicated server can run NPC meshes at `OnlyTickMontagesWhenNotRendered`
   and skip faces, grooms and outfits entirely.
3. **The gravity frame everywhere.** Trajectory, foot IK, aim offset, look-at and turn-in-place go through
   `FVCGravityFrame`. Without custom gravity each is the Z-up expression, so heightmap worlds are unchanged. No
   gravity-relative yaw is kept across frames (the frame twists near a planet's south pole): turn-in-place keeps the
   mesh's world facing and takes the yaw offset in the current frame each update; trajectory history is stored in world
   space and converted per frame.
4. **Players and NPCs share the inputs, not the cost.** One anim instance class reads any `ACharacter` on
   `UVCMovementComponent`. The NPC graph is cheaper (no motion matching, IK only at LOD0, budgeted updates).
5. **Bodies are data.** An appearance asset says which meshes, physics asset, anim class and attach rules a character
   uses. No C++ in this plugin links to the MetaHuman modules: a MetaHuman arrives as content (its components and its
   own `UMetaHumanComponentUE`).
6. **Equipment picks the layer and the montage set.** The item's Equipment fragment carries the `AnimLayerClass`
   (already linked by EquipmentPlugin) and a gameplay tag for its action set (`Equipment.AnimSet.*`), replacing the
   `EVCEquipmentAnimType` placeholder. Cross-plugin identity by tag, as `CLAUDE.md` asks.

### Where the code goes

| Piece | Home |
|---|---|
| `UVCAnimInstance` (reworked for players and NPCs), the anim layer interface's C++ side, `UVCCharacterAppearance`, `UVCActionAnimSet`, montage use in `UVCMeleeAttackAbility` and the surface actions | VoxelCharacterPlugin, its runtime module |
| The gravity-frame trajectory predictor, motion-matching glue, `vc.Anim.*` diagnostics | **A new runtime module `VoxelCharacterAnimation`** in VoxelCharacterPlugin (depends on PoseSearch, Chooser, AnimationWarping, MotionWarping), so the core module and a project without motion matching carry none of it (D45) |
| Item anim layers and action-set tags | EquipmentPlugin item data (content); no code change expected |
| NPC classes in the spawn tables | VoxelWorldPOI content (`DA_SurfaceNPCTable`, dungeon enemies) |
| Quadruped meshes and clips | PROTOFACTOR's Animal Variety Pack, a content pack (D47); were they ever authored here, the Blender pipeline's skeletal FBX kind (`Tools/Blender/README.md`) |
| QA commands that drive the pawn | The game module's `vox.*`, as today |

### Where the content goes

The repository holds only `Content/PluginTesting` and `Content/Art`. Pack content and anything derived from it stays
out of git and reaches worktrees through `content_packs.txt` (D46):

- GASP's content, unconverted. Its assets sit at the root of its `/Game` (`/Game/Blueprints`, `/Game/Characters`,
  `/Game/Input`, `/Game/MetaHumans`, ...) and a migrate keeps paths. P1 gathers them under one folder,
  `Content/GameAnimationSample/`, in a full editor on a copy of the GASP project (never the project you made; the
  commandlet route failed in S1), or, failing that, keeps GASP's own roots, which collide with nothing of ours (the
  spike ran so), each one a pack folder. Only what is used comes over (the UEFN Mannequin's animations, the
  motion-matching data, the CMC character and its AnimBP, the UE5 Mannequins, the sample MetaHuman), not Echo,
  Twinblast or the levels.
- `Content/CharacterAnimation/`: ours on top of it: the anim layer interface and layer AnimBPs, the action montages,
  the pack animations re-pointed to the one UE5 Mannequin skeleton (GASP's copy). One new pack folder.
- `Content/MetaHumans/`: what MetaHuman Creator writes. Another pack folder (large: hundreds of MB per MetaHuman).
- Tracked: the character Blueprints and AnimBPs in `Content/PluginTesting`, as today (they reference pack content, as
  `ABP_VoxelCharacter` already does).
- `Scripts/setup_character_animation.py` (run through Claudius): the repeatable recipe for re-pointing pack animations to
  the one skeleton, adding the equipment sockets and building the action sets, so the result is never only in one
  editor.

## 5. Phases

Each phase is one PR per repository into `dev` (VoxelCharacterPlugin submodule first, then the parent's gitlink),
gated as `Scripts/qa/README.md` says for a VoxelCharacterPlugin change. The order puts the architecture before the
bodies: P2-P4 show results on today's character before any MetaHuman exists.

### P0: Spikes (throwaway branch; results written back into this plan)

- **S1, motion matching on a planet.** GASP 5.8's CMC AnimBP and motion-matching data migrated into a scratch branch
  (as §4 says, through a copy of the GASP project); the AnimBP on a UEFN-Mannequin pawn on `UVCMovementComponent`,
  its trajectory from a predictor of ours in the gravity frame (`IPoseSearchTrajectoryPredictorInterface`, or a
  trajectory our anim instance builds, whichever the motion-matching node takes more simply). S1 does not wait for S4:
  the gravity question is the same on either skeleton. Pass: on `PlanetTest`
  at the equator and near the south pole the trajectory's debug draw lies on the tangent plane and turns about local up;
  starts, stops and pivots look as they do on a plane; foot placement holds on voxel slopes and on 75 uu block steps.
  Measure the pose search's cost per character.

  **S1 result (2026-11-05, spike branch `spike/character-animation-p0`, parent repository; not to merge): passes, so
  D44 is (a).** GASP's CMC character reparented onto a pawn on `UVCMovementComponent` (`AVoxelSpikeGASPCharacter`), its
  AnimBP onto `UVoxelSpikeMMAnimInstance`; `vox.Spike.GASP` swaps the player onto it on `PlanetTest` (radius 250,000
  uu).
  - GASP's motion matching runs on the planet unchanged: the character stands upright and walks, runs, turns and stops
    at the equator and at 70°S, on slopes.
  - GASP's own trajectory is the engine's CMC prediction (its pose-history node takes the AnimBP's `Trajectory`
    variable; it does not generate its own). On a planet it goes wrong only when the view turns (strafing) or the
    character orients to its movement: the prediction turns about world Z. With the view turning at 60°/s while
    walking, its samples are upside down at the equator (sample up · local up −0.98) and at 70°S tilted (0.71-0.74)
    with a heading error of 20°. Straight walks, turns of the walk direction and stops match ours.
  - `UVoxelSpikeGravityPredictor` (`IPoseSearchTrajectoryPredictorInterface`, turning about local up, the engine's
    speed and braking step reused) keeps every sample upright (1.000) with no heading error; with up = world Z it is the
    engine's prediction to 0.01 uu (`VoxelEngine.Spike.Trajectory.*`, 3/3, headless). It reaches motion matching with
    no graph edit: the anim instance's proxy `Update` runs after the AnimBP's thread-safe update and before its graph,
    and replaces the trajectory there (`vox.Spike.GravityTrajectory 0|1`, the A/B in one build).
  - Motion matching's own numbers (`FMotionMatchingState::SearchResult`): the same at the equator (the engine's error
    there is a roll about the walk direction, which the trajectory features do not see); at 70°S lower match costs
    with ours (straight walk −0.38 against −0.23, view turning −0.41 against −0.37) and fewer clip switches on the
    straight walk (0.7 against 3.1 a second). Suggestive, not conclusive: the runs cross different ground.
  - Cost: both trajectories (the engine's for the A/B, ours, and the collision pass's traces) 0.05-0.08 ms per update
    on the animation worker.
  - Not covered: foot placement on 75 uu block steps (`Review_Cubic`), the pose search's own time (`stat anim`).
  - For P5: GASP's camera (`GameplayCameras`) is world-Z up, so on a planet the view lies on its side; the character
    keeps `UVCCameraManager`. GASP's content needs its data-driven cvars (`[/Script/Engine.DataDrivenConsoleVariableSettings]`)
    and plugins in the project. The single-folder gather does not work as a commandlet (`rename_assets` is refused by
    a dialog `-unattended` answers Cancel, and stops on the MetaHuman retarget poses' Control Rig errors): P1 does it
    in a full editor, or keeps GASP at its own paths; the spike copied it at its own paths, which collide with nothing.
- **S2, a MetaHuman on the pawn.** One MetaHuman made in Creator on `BP_VCPlayer`: GASP's runtime retarget against
  direct playback on the MetaHuman body. Measure ms per character, visible offsets (hands, feet, sockets), the face's
  idle, the first-person view, disk and memory; check a dedicated server can drop face, groom and outfits.

  **S2 result (2026-11-05, the spike branch; GASP's sample MetaHuman Kellan instead of a new one from Creator):
  direct playback works, so the runtime retarget is not needed.**
  - `vox.Spike.MetaHuman` makes Kellan's body (`m_med_nrw_body`, skeleton `metahuman_base_skel`, marked compatible
    with the UEFN and UE5 Mannequins) the pawn's animated mesh. GASP's AnimBP and databases drive it directly. His
    top, trousers and shoes follow it by leader pose; his face keeps its own AnimBP, which copies the head from its
    attach parent, so it is re-attached under the animated body; the grooms ride on the face. On `PlanetTest` he
    stands, runs and stops with the motion-matched body, the head, face and hair in place.
  - What a MetaHuman needs in the project: the three renderer settings S4 found (and a round of skinning shader
    compiles), the LiveLink and LiveLinkControlRig plugins (the face AnimBP has LiveLink nodes; without them Kellan's
    Blueprint does not compile and spawns no components), RigLogic and HairStrands.
  - On a planet the outfit physics falls along world −Z: the hoodie's drawstrings stand out sideways at the equator.
    Cloth and rigid-body nodes on MetaHuman outfits need the movement's gravity each frame (P6).
  - The MetaHuman body has no sockets: the equipment sockets go on `metahuman_base_skel` (or each body), with their
    own attach offsets (P6).
  - Kellan is an optimised MetaHuman (`*_Simplified` materials): 81 MB of his own, 220 MB shared (`MetaHumans/Common`).
  - Not measured: the frame cost. Another editor shared the machine, and the game-thread counter read a constant 172 ms
    in this setup; P6 measures it with `voxel.Bench.Run` on a quiet machine, against today's body. Not tested: a
    dedicated server without face, grooms and outfits, the first-person view, MetaHuman Creator's own output.
  - For D43 (the default look): the images are `Saved/Screenshots/Claudius/s2_*.png` in the spike worktree.
- **S3, content audit. Done** (§2): the gaps are chopping, and placing and using.
- **S4, GASP on the UE5 Mannequin.** In the copy of GASP: convert the walk, run and idle sets and their databases from
  the UEFN Mannequin to UE5 Manny (the IK retargeter's batch export for the sequences, a script that duplicates the
  databases and choosers and points them at the converted sequences, then a database rebuild). Pass: side by side with
  GASP's own runtime-retargeted Manny, the converted character picks the same kinds of poses and its feet slide no more.
  Measure what the conversion of the whole set would take. If it fails, D50 falls back to (b).

  **S4 result (2026-11-05, the spike branch): a cheaper way passes; the user took it as D50 (a').**
  - **S4a, no conversion.** The UEFN Mannequin and UE5 Manny skeletons marked compatible with each other (each
    skeleton asset's `CompatibleSkeletons`). GASP's CMC AnimBP, its motion-matching databases and its UEFN
    animations then drive a `SKM_Manny` mesh directly (`vox.Spike.Mesh`): the engine remaps bones between compatible
    skeletons when it samples, and motion matching's pose history does too (`FSkeletonRemappingRegistry` in
    `PoseSearchHistory.cpp`). On `PlanetTest` Manny runs, turns with the view and stops like the UEFN body. Nothing is
    converted, and no second pose is evaluated each frame.
  - Foot slide (`vox.Spike.FootSlide`: how far the balls of the feet move while within 2.5 uu of their flat-foot
    height): too noisy to rank the bodies. The UEFN body's own run measured 110 and 36 uu/s in two identical runs;
    Manny's 103 and 195. Both bodies' feet dip up to 7-11 uu below flat-foot height on this ground.
  - The comparison with GASP's runtime retarget was not reproduced: a Manny under the animated mesh running
    `ABP_GenericRetarget` (`vox.Spike.Retarget`) stays in a standing pose here. A hidden source mesh stops posing, and
    GASP's visual override evidently sets up more than the attach parent and the component tag. GASP's own project
    shows that path working; it is the reference to judge by eye.
  - S4b, the conversion, was not run. `UIKRetargetBatchOperation::RunBatchRetarget` duplicates and retargets
    sequences, blend spaces, montages and AnimBPs, but not motion-matching databases or choosers; a conversion would
    also need a script to duplicate those and point them at the converted sequences.
  - **D50 (a'), decided:** the UE5 Mannequin is the one skeleton for bodies and our animations; GASP's set stays on
    its UEFN skeleton and plays through skeleton compatibility; the conversion is done only if a body shows a fault it
    causes (feet, hands on weapons). P1 then keeps GASP's content as it is.
  - Also found: GASP's sample MetaHuman needs three renderer settings the project lacked
    (`r.GPUSkin.Support16BitBoneIndex`, `r.GPUSkin.UnlimitedBoneInfluences`, `SkeletalMesh.UseExperimentalChunking`;
    the editor's Missing Project Settings toast), and GASP sets `p.EnableCharacterAccelerationReplication=1`, which
    motion matching on simulated proxies needs. All four are on the spike branch; P5 and P6 take them. A Live Coding
    patch that changes a struct a running ticker uses crashes the editor: a restart, not a patch.

### P1: One skeleton, one content home (content and a script; parent PR)

- GASP's content brought in unconverted (§4, "Where the content goes"), with the plugins and the
  `[/Script/Engine.DataDrivenConsoleVariableSettings]` cvars it needs.
- The canonical UE5 Mannequin skeleton is GASP's copy (`/Game/Characters/UE5_Mannequins`); every pack animation
  re-pointed to it, the packs' own skeleton copies retired; UE4 sets retargeted only where a UE5 set is missing.
- The skeletons marked compatible both ways: the UEFN Mannequin, the UE5 Mannequin, `metahuman_base_skel` (S4, S2).
- Equipment sockets on the skeleton (`hand_r_socket`, `head_socket`, the back and hip slots), matching the slots'
  `AttachSocket` names (`UEquipmentManagerComponent::AvailableSlots`) and `FVCEquipmentSocketMapping`.
- `setup_character_animation.py`; `content_packs.txt` gains `CharacterAnimation` and GASP's folder(s).
- Done when: `BP_VCPlayer` and `ABP_VoxelCharacter` run unchanged on the canonical skeleton; nothing references the
  retired copies.

**P1 result (2026-11-06; the parent's feature branch, no VoxelCharacterPlugin code).**
- GASP is the content pack `GameAnimationSample` (`/Game/GameAnimationSample`, 3,809 assets, 5.3 GB, in
  `content_packs.txt`). `Scripts/gather_game_animation_sample.py` gathers it in a copy of the GASP project: the move
  as a commandlet works once the copy's config names the new paths and the MetaHuman poses' Control Rig errors are
  caught; its check finds no asset under the folder that depends on a `/Game` path outside it or on a missing one.
- Plugins (`VoxelEngine.uproject`): PoseSearch, Chooser, AnimationWarping, AnimationLocomotionLibrary, MotionWarping,
  SmartObjects, GameplayInteractions, GameplayCameras, and DrawDebugLibrary and CurveExpression (experimental; GASP's
  helper library and CMC AnimBP use them). With these, GASP's CMC character, its AnimBP and the Blueprints they use
  compile clean. Not enabled: Mover and MovieSceneAnimMixer. GASP's Mover character, its AnimBP and one smart-object
  StateTree task then do not compile; only GASP's own game mode and levels load them, and in a session that did, PIE
  asks to play with "Blueprint Asset Compilation Errors". The MetaHuman settings and LiveLink wait for P6.
- `Config/DefaultEngine.ini`: GASP's data-driven cvars (`DDCvar.*`, in `EDITOR_CONSOLE_GUIDE.md` §4), PoseSearch's
  buffer size, `p.EnableCharacterAccelerationReplication=1`.
- `Scripts/setup_character_animation.py` makes GASP's UE5 Mannequin (`/Game/GameAnimationSample/Characters/
  UE5_Mannequins`) the canonical skeleton: its compatible list takes the UEFN Mannequin, `metahuman_base_skel` and the
  five pack copies (template, CR, WaterAnimations, Bow, Sword and Shield); the UEFN and MetaHuman skeletons take the
  canonical one and each other. A second run changes nothing.
- Two items of the list above turned out unneeded:
  - **Re-pointing the pack animations and retiring their skeleton copies.** A compatible list is only an editor
    filter (`USkeleton::IsCompatibleForEditor` reads this skeleton's own list); at run time the engine remaps bones
    between any two skeletons (today's player already runs `ABP_VoxelCharacter`, on the template's copy, on Grian's
    mesh, on CR's copy, with no list at all). So only the canonical skeleton lists the others, and the packs' assets,
    which live untracked in the main checkout with no history, are left as they are.
  - **Equipment sockets.** The slots attach to bones, not sockets (`hand_r`, `hand_l`, `head`, `spine_03`, with an
    offset per slot: `AVCCharacterBase`'s slot setup), and the UE5 Mannequin, the UEFN Mannequin and the MetaHuman body
    all have those bones. Per-body offsets go in P6's appearance asset.
- Unchanged: `BP_VCPlayer` (Grian on CR's copy) with `ABP_VoxelCharacter` idles, walks and swims in `VoxelDemo` as
  before. The headless suite passes (787 of 787).
- `content_packs.txt` gains `CharacterAnimation` when P3 puts something in it.

### P2: One anim instance for every character (VoxelCharacterPlugin)

- `UVCAnimInstance` reads any `ACharacter` on `UVCMovementComponent`: view mode and equipment state through a small
  interface both character classes implement. Game-thread gather in `NativeUpdateAnimation`, the rest in
  `NativeThreadSafeUpdateAnimation`.
- New values, all in the gravity frame: ground distance and time to land, acceleration and lean, slope under the feet,
  the turn-in-place offset (principle 3), the action-set tag, dead, downed and hit state, surface or underwater swim.
- `EVCEquipmentAnimType` replaced by the action-set tag on the Equipment fragment (CoreRedirects as needed).
- Tests (`VoxelCharacter.Anim.*`): the gathered values equal the Z-up expressions without custom gravity and match
  them in the gravity frame on a planet (as `VoxelCharacter.Planet.GravityFrame` does for movement); the turn-in-place
  offset is stable across the south-pole twist.

**P2 result (2026-11-06; VoxelCharacterPlugin and ItemInventoryPlugin).** The reference is `CHARACTER_SYSTEM.md`,
Animation.
- `UVCAnimInstance` serves the player and the NPCs: `IVCAnimatedCharacter` gives it the view mode, the action set and
  the combat component, and `AVCCharacterBase` and `AVCNPCCharacterBase` implement it. The game thread gathers an
  `FVCAnimInput` (with a trace along gravity for the ground under a falling character); `ComputeValues` does the rest
  on the worker thread. The names `ABP_VoxelCharacter` reads are unchanged.
- The new values: local velocity and acceleration, the relative acceleration and lean, yaw rate, the floor's pitch and
  roll against the facing, ground distance and time to land, the turn-in-place offset, the action set, dead and downed,
  sprinting, underwater swimming. The aim of a character with no controller here (a remote player, an NPC on a
  client) now comes from its base aim rotation; before, it stayed at zero.
- Every value is measured in a frame built each update from local up and the facing, and the kept state is world
  vectors, so nothing depends on the gravity frame's yaw. Carried round a closed walk, the body heading turns against a
  world-fixed facing by the solid angle the walk encloses (parallel transport): 0.014° per lap half a degree from the
  pole, against the 20° per step the gravity frame's yaw jumps there.
- `EVCEquipmentAnimType` is gone (no asset used it, so no redirect). `Equipment.AnimSet.Unarmed`, `Tool`, `SwordShield`,
  `TwoHand` and `Bow` are native tags; the Equipment fragment carries `ActionSetTag`; `PickActionSet` gives the
  item's set, `Tool` for an item that names none, `Unarmed` for an empty hand. It is set from `OnEquipmentChanged`,
  which fires on clients too: the old anim type came from `OnItemEquipped`, which fires on the server only, so a
  client never saw its own pawn's tool type. NPCs take an `ActionSet` property. No item names a set yet; P3 sets them
  with the layers.
- Hit reacts stay with P3: `UVCCombatComponent::OnDamaged` fires on the server only, and P3's montage path replicates
  them.
- Found in PIE: the first cut kept the body heading whenever the character was slow, and the spawn restores a saved
  rotation while the pawn is held in `MOVE_None` for the terrain wait, so the player stood with a 90° offset. The body
  now keeps its heading only on the ground, and a turn past the clamp in one update (a rotation set by code) seats it.
- Tests `VoxelCharacter.Anim.*` (five): `PlaneValues` (500 random states: speed is `Size2D` bit for bit, direction,
  aim and local velocity are the old expressions to 1e-5°, slopes, landing), `PlanetFrame` (seven sites with the poles,
  three random twists about up, 120 updates each: the plane's values to 8e-6), `TurnInPlace` (held, clamped, returned
  within a second, no offset while held off the ground, a code-set turn, the south-pole laps), `ActionSet`,
  `GatherFromCharacter` (an NPC on three frames falling toward a slab: speed, local velocity, ground distance and time to
  land as computed). The headless suite passes (797 of 797, the five new among them).
- PIE in `VoxelDemo`: `BP_VCPlayer` with `ABP_VoxelCharacter` idles, runs and swims as before; read live, the values
  are right (a dive at the saved spot under water, swimming at the surface, running at 600 with the local velocity
  straight ahead, the offset 0 after the spawn and back to 0 when moving), and equipping a pickaxe sets
  `Equipment.AnimSet.Tool`.

### P3: Layers and actions (VoxelCharacterPlugin, content, item data)

- `ALI_VCCharacter` and the layer AnimBPs: Unarmed, SwordShield and TwoHand (SwordShield), Bow (BowAndArrow), Tool
  (VanillaLoop's mining loop, with D51's stand-ins), linked by the existing `AnimLayerClass` path.
- `UVCActionAnimSet` (per action-set tag): attack combo, dig, chop, place, use, hit react by direction, death by
  direction, revive.
- Melee: the ability plays its montage; damage stays on the server, at the montage's hit time read from the asset.
  The owner plays the montage at once on input and the server's replicated montage is not played twice (D49).
- Dig, chop, place and use play their montages as cosmetic events: the owner at input, others from one unreliable
  multicast. Voxel edits and harvests keep their server paths unchanged.
- The death ragdoll stays (`StartRadialRagdoll` on planets); directional death montages where there is no physics
  asset.
- Tests: the hit time from a montage; the PIE dedicated-server test (D-CR23) extended so a simulated proxy receives the
  attack montage.

**P3 result (2026-11-06; VoxelCharacterPlugin, the parent's content, script and test).** The reference is
`CHARACTER_SYSTEM.md`, Animation.
- `UVCActionMontageComponent` on both character classes plays the actions of `UVCActionAnimSet`s, one per action-set
  tag: an attack combo, dig, chop, place, use, hit reacts and deaths by side, revive. The owner plays its own at once;
  the server sends one unreliable multicast that the owner skips for those, so nothing plays twice (D49). Hit reacts,
  deaths (for a body with no physics asset; the player's ragdolls as before) and get-ups come from the combat events on
  the server.
- The melee ability keeps its damage on the server and lands it at the montage's hit time, read from the asset (the
  first `UVCAnimNotify_ActionHit`), through an ability task; with no montage it is immediate. NPCs list no sets yet, so
  theirs stay immediate until P7 gives them bodies.
- Layers: `ALI_VCCharacter` has one layer, `UpperBodyStance`; `ABP_VoxelCharacter` implements it (the base pose through)
  and runs it before its slot; the swords link `ABP_VCLayer_SwordShield`, the pickaxes `ABP_VCLayer_Tool` (the
  two-handed idle, a D51 stand-in). The Bow set and its layer wait for a bow item and a ranged ability; TwoHand's is
  built and no item uses it yet.
- The engine exposes no anim layer or graph editing to Python, so a new editor module, `VoxelCharacterEditor`, has
  `UVCAnimAuthoringLibrary` (add a layer, implement an interface, add nodes, set node properties and pin defaults,
  connect pins, point a linked layer node), and `Scripts/setup_character_layers.py` builds the content with it:
  `/Game/CharacterAnimation` (the interface, three layers, 46 montages, four sets: a content pack, rebuilt by the
  script), the layer in `ABP_VoxelCharacter`, the items' tags. A second run creates and saves nothing. Two findings on
  the way: `ABP_VoxelCharacter` must implement the interface itself for its linked layer node to compile, and a linked
  layer node keeps its layer name in a member reference besides its own fields.
- VanillaLoop's samples are still not in: placing and using play nothing, the pickaxe digs with a two-handed strike, and
  the downed crawl waits. (The ranged combat plan's B4 has since given placing and using the bow pack's pickup and
  potion.)
- Tests `VoxelCharacter.Anim.*` (four more): `ActionHitTime`, `ActionSetLookup` (variants, fallbacks, the side of a hit
  on a plane and a tilted frame), `AttackCombo` (the window and the play rate), `MeleeHitTime` (the damage at 0.3 s of a
  swing, not at its start, the victim's flinch, immediate without a montage). The replication test gains two steps: an
  attack montage reaches the client's copy of a server-owned actor (a simulated proxy) and resolves the same montage
  there; the owning client skips an action it played itself and plays one it did not.
- PIE in `VoxelDemo`: the sword's stance and the pickaxe's, the swing (the owner's, under `slomo`), the hit landing at
  the hit time (0.40 s after the swing at the sword's speed), a hit react, a chop.

### P4: IK and warping (AnimBP content, small C++ for inputs)

- Foot placement with pelvis adjustment on the base graph; clamped for block-step edges; tolerant of a floor that
  disappears for a frame during a re-mesh (the movement's `FloorGraceDuration` case).
- Orientation and stride warping for the player; slope warping only if foot placement is not enough.
- Look-at (head and eyes) toward the aim or the target; hand IK for two-handed items (bow, pickaxe) via Control Rig.
- `vc.Anim.FootIK`, `vc.Anim.LookAt`, `vc.Anim.Debug` (A/B and debug draw), listed in `EDITOR_CONSOLE_GUIDE.md` §4.
- Done when: PIE on `VoxelDemo`, `Review_Cubic` and `PlanetTest` (equator and south pole) shows planted feet on
  slopes and steps, with screenshots from above and from the side.

**P4 result (2026-11-07; VoxelCharacterPlugin, the parent's content and script).** The reference is
`CHARACTER_SYSTEM.md`, Animation.
- `ABP_VoxelCharacter` runs the IK after its slot, so over the montages too, in component space: foot placement and its
  leg IK (GASP's settings less the contact curves, which our clips do not have: a foot plants when its ball stops in the
  pose; the pelvis drops at most 50 uu, so one foot on a 75 uu block step bends that leg instead of crouching the body),
  a look-at on the head and a two-bone IK that puts the off hand on a two-handed item's grip. `UVCAnimInstance` computes
  their targets and eases their weights (`FootIKAlpha`, `LookAtLocation` and `LookAtAlpha`, `HandIKEffector` and
  `HandIKAlpha`), so a floor lost for a frame during a re-mesh fades the weight a frame's step instead of snapping it,
  and the cvars switch each off without changing the graph. The foot placement traces along the mesh's own down and the
  look-at target is a world point: a planet needs nothing more.
- Stride warping was tried on the sprint and changed nothing (the widest step stayed the same: the jog's feet are near
  the legs' reach at both ends of a stride, where the warp clamps). Instead the Walk/Run blend space plays faster above
  its top speed (`LocomotionPlayRate`, speed over 600), so the feet keep pace: 182 uu a step at 600 and at a sprint's
  960, 280 with the pace off. Orientation warping is not needed (the blend space has eight directions), nor slope
  warping (the foot placement plants both feet on every slope tried), nor Control Rig for the hands (a two-bone IK onto a
  point in `hand_r`'s space holds the pickaxe's handle: `UVCActionAnimSet::bOffHandIK` and `OffHandGrip`, the Tool and
  TwoHand sets at (0, 2, 20); the bow's hands are its clips'). The mannequin has no eyes; MetaHumans' wait for P6.
- The look-at node with its look-up axis and its interpolation on did not turn the head at all; with both off it turns
  (a target 90 degrees aside: the head's forward 0.53 along the facing and 0.84 toward the target, against 0.97 along
  the facing with the look-at off). A player looks along its view, an NPC at its AI controller's focus.
- `UVCAnimAuthoringLibrary` gains `ListGraphs`, `ExportGraphNodes` and `ImportGraphNodes` (nodes as text, which read
  GASP's foot placement settings) and `SetNodePinVisible`; `Scripts/setup_character_ik.py` builds the chain and the
  play rate into `ABP_VoxelCharacter`, and a second run changes nothing.
- `vc.Anim.FootIK`, `vc.Anim.SprintPace`, `vc.Anim.LookAt`, `vc.Anim.HandIK` (A/B) and `vc.Anim.Debug` (the look-at
  target, the weights and the engine's foot placement draw), in `EDITOR_CONSOLE_GUIDE.md` section 4.
- Test `VoxelCharacter.Anim.IKValues`: the foot weight on the ground and off falling, swimming, flying or dead, eased; the
  pace (1 up to the reference speed, 1.6 at a sprint, 1 in the air); the look-at off behind, in first person or with no
  target, on 90 degrees aside, the same in a tilted frame; the hand IK and its grip; each cvar.
- PIE, from the side and from above, IK on against off: `VoxelDemo`'s slope (off, the downhill foot floats; on, both
  feet stand on the slope), `Review_Cubic`'s 75 uu block step (off, the lower foot hangs in the air at the upper
  block's height; on, it reaches down to the lower ground and the other leg bends on the step), `PlanetTest` at the
  equator (a slope at latitude 2, longitude 20) and at the south pole (-89.6, 0): planted as on the plane, the body
  upright in the gravity frame. Found on the way: the Crouch state is not wired (G140); the full headless suite, two
  tests the weather plan's P3 and P4 had left failing on dev, fixed here (G141, G142).

### P5: Player locomotion (new module `VoxelCharacterAnimation`)

- If S1 passed: motion matching from GASP's databases and choosers (unconverted, D50), our gravity-frame predictor, turn in
  place, starts, stops, pivots, jump and land; swimming from WaterAnimations (surface and underwater). GASP's CMC
  AnimBP is the starting point, reparented to `UVCAnimInstance`: its reads through GASP's Blueprint interfaces
  (`BPI_SandboxCharacter_Pawn`, `_ABP`) become our C++ values, and its trajectory generation becomes ours.
- If S1 failed: a state machine with blend spaces and distance matching (AnimationLocomotionLibrary), which is safe
  under gravity; motion matching waits for an engine fix.
- Tests: the predictor's samples lie on the tangent plane and turn about local up; without custom gravity they equal
  the engine's own prediction.

**P5 result (2026-11-07; VoxelCharacterPlugin, the parent's script and settings).** The reference is
`CHARACTER_SYSTEM.md`, Animation. S1 passed, so motion matching:
- `Scripts/setup_character_locomotion.py` copies GASP's `SandboxCharacter_CMC_ABP` and the five choosers whose context is
  its class to `/Game/CharacterAnimation/Locomotion` (GASP's pack reaches every checkout through one junction, so it is
  copied, not changed), every reference among the copies pointed at the copies. An Evaluate Chooser node names its
  context pin for the context's class, so the pin GASP's graph linked to Self is moved to the copy's. The copy is
  reparented to `UVCMotionMatchingAnimInstance` (a `UVCAnimInstance`, in the new runtime module `VoxelCharacterAnimation`,
  D45), which fills GASP's `CharacterProperties` from our pawn; GASP's fill through its pawn interface is emptied.
- GASP's reads became our values where they hide world Z: three `VSizeXY`, two `CalculateDirection`, six
  `NormalizedDeltaRotator`, the trajectory generation, and two Euler yaw sums (the root transform's 90 degrees, the
  strafe's offset) call the parent's gravity-frame functions of the same pins, the engine's own arithmetic where up is
  world Z (test `VoxelCharacter.Anim.MotionMatchingFrame`). The trajectory is spike S1's predictor, made production code
  (`UVCGravityTrajectoryPredictor`; tests `VoxelCharacter.Anim.Trajectory.MatchesEngineOnPlane` and `.PlanetFrame`:
  samples on the tangent plane, upright, turning about local up; the engine's prediction on a plane to 0.01 uu).
- The copy's AnimGraph gained swimming (`AS_Swim`), the item's `UpperBodyStance` layer before the slot, and P4's look-at
  and off-hand IK after GASP's foot placement and leg IK, which `FootIKAlpha` now drives. GASP's own turn in place, starts,
  stops, pivots, jumps, landings and crouching come with it; GASP's camera does not (the character keeps
  `UVCCameraManager`).
- `UVCSettings::PlayerLocomotionAnimClass` names the copy; `AVCCharacterBase::ApplyLocomotionAnimClass` sets it at
  BeginPlay (not on a dedicated server, not in the automation tests) and again on `vc.Anim.MotionMatching`, re-linking
  the equipped items' layers. Without the copy the player keeps `ABP_VoxelCharacter`, with one warning.
- `UVCAnimAuthoringLibrary` gains `CopyAssetsWithReferences`, `RenameMemberVariable`, `RemoveGraphNodes`,
  `ReplaceFunctionCalls`, `RecombinePin` and `GetPinLinks`.
- PIE: on `VoxelDemo` idle, run, stop, a sword's stance and swing, the bow's draw, swimming; on `Review_Cubic` the 75 uu
  block step (with `vc.Anim.FootIK 0` the lower foot hangs in the air, with it on it reaches the lower block); on
  `PlanetTest` at the equator running north (along world Z) the speed reads 600 (world Z's `VSizeXY` would read about
  25) and a start, a reversal and a stop pick a run start, run loops, a 180-degree pivot and the idle; at the south pole
  the samples stand upright (up . up 1.000; the engine's -0.9995) and the same input picks run loops, a pivot and the
  idle, where the engine's trajectory picks spin transitions (cost -0.86) and a 180-degree turn in place on the stop
  (-1.20). Crouching, which the state machine never posed (G140), plays GASP's crouch idle and walk.
- Cost for one running player (the CSV profiler): 0.50 ms game thread and 1.39 ms on the animation worker, against 0.22
  and 0.33 ms for `ABP_VoxelCharacter`.

### P6: Appearance and MetaHumans (VoxelCharacterPlugin, content)

- `UVCCharacterAppearance` (primary data asset): body mesh or MetaHuman parts, physics asset, anim class, retarget mode
  (direct or runtime retarget, from S2), follower parts, first-person rules, socket overrides, LOD and budget settings.
- Both character classes apply an appearance. The animated mesh stays the character's `Mesh`; MetaHuman `Body`, `Face`,
  outfit and groom follow it. On a dedicated server only the animated mesh is created.
- The player's appearance replicates from the player state and is saved in `FVCPlayerSaveState`. A character-creation
  screen is out of scope.
- First person: the arms mesh as now, or the body itself with the head hidden (D48).
- Done when: the player plays as a MetaHuman and as the low-poly body, switched by data, on a listen server and a
  dedicated server; `voxel.Bench.Run` shows the MetaHuman's frame cost against today's body.

**P6 result (2026-11-07): done.** `CHARACTER_SYSTEM.md` "Appearance" has the rules.

- `UVCCharacterAppearance` holds the body mesh, the parts actor (a MetaHuman's Blueprint) and its body component, the
  parts that animate themselves, the anim class and whether the body has the Mannequin's `ik_foot` bones.
  `UVCAppearanceComponent` dresses both character classes in one. What the first bullet listed and P6 did not need: the
  physics asset comes with the mesh; S2 played GASP's set on the body directly, so there is no retarget mode; socket
  overrides were not needed (equipment attaches to bones the MetaHuman skeleton has, oriented as the Mannequin's); LOD
  and budget settings are P7's, with the NPCs.
- D43, the default look, decided by the user after S2: for this project's demos a MetaHuman in smooth worlds and the
  low-poly body in cubic ones; for the plugins it is the project's data. `UVCSettings` names a default and one per
  world meshing mode, a game mode may name one for its map, and the player's own choice (saved) comes first;
  `Config/DefaultGame.ini` names Kellan for Marching Cubes and Dual Contouring and the low-poly body for Cubic and by
  default. `vc.Appearance` switches it live.
- The player's appearance is resolved on the server and replicated from the player state; the choice is saved in
  `FVCPlayerSaveState::ChosenAppearance`. A pawn possessed before play began resolves at BeginPlay: the world's meshing
  mode is known only once the voxel world has begun.
- Multiplayer (PIE in one process, `VoxelDemo`): a dedicated server with one and with two clients, a listen server with
  one. Every machine that draws dresses every player, its own and the others, in Kellan with his parts and motion
  matching; the dedicated server animates Kellan's body alone, on `ABP_VoxelCharacter`; `vc.Appearance LowPoly` and
  `rule` on the server switch every machine both ways. A dedicated server took the body from the parts actor, which it
  does not spawn, so it kept the low-poly body at first; it reads the parts class's body component now. The first
  dedicated-server run found G144 (VoxelWorlds: every world's biome lookup made under one name, so a second world in one
  process replaced the first's while it was held and the editor stopped on the engine's check); fixed here, with a
  test.
- Feet: a MetaHuman body has no `ik_foot` bones, so the engine's foot placement skips it.
  `FVCAnimNode_FootBoneIK` places the feet through the foot bones (a trace per foot along the mesh's down, the pelvis
  to the lower foot, two-bone IK from the animated pose, the foot tilted to the ground); both AnimBPs run it after their
  leg IK, weighted by `FootBoneIKAlpha`. On a 28-degree slope in `VoxelDemo` Kellan's feet stand 11 and 33 uu over the
  ground without it and 0.6 and 1.4 uu with it. On `Review_Cubic`'s 75 uu block step the upper leg bends and the lower
  foot reaches down 52 uu, 25 short of the lower block: Kellan's legs are short for the 50 uu pelvis drop, where the
  low-poly body's reach (a MetaHuman is not this project's cubic body; G145).
- Outfit physics: Kellan's hoodie runs its post-process AnimBP, which copies the body's pose (Copy Pose From Mesh) and
  simulates the drawstrings in a rigid-body node; under leader pose it never ran. The component points every rigid-body
  node's gravity, and any cloth's, along the movement's each tick: at `PlanetTest`'s equator the strings hang toward
  the planet's centre, where S2 saw them stand out sideways.
- What a MetaHuman needs in the project: RigLogic, HairStrands, LiveLink and LiveLinkControlRig, and the three
  renderer settings in `DefaultEngine.ini`; `Scripts/setup_character_appearances.py` builds the two appearances.
- Cost for one player, in-editor PIE on a quiet machine. `voxel.Bench.Run`'s `VoxelDemo` chord (1,500 uu/s over
  40,000 uu), Kellan and the low-poly body interleaved, three runs each: with the hoodie under leader pose the frame's
  median was 29.3 against 26.3 ms and its P95 47.4 against 44.2 ms (the game thread 29.2 against 26.3, the GPU 5.97
  against 5.70); with the hoodie simulating, in a later session, 18.4 against 17.4 ms (the game thread 18.4 against
  16.5, the GPU 5.40 against 5.21), the P95s inside the low-poly runs' spread (25.5 to 36.0 ms). Standing, the CSV
  profiler: 3.0 to 3.7 ms more game thread a frame and 0.2 to 0.7 ms more GPU, most of it animation-worker time the
  game thread waits for: the body's post-process AnimBP (the MetaHuman's corrective pose drivers) 2.3 ms, the face's
  RigLogic 0.9 ms (both measured by switching each off), the hoodie's simulation 0.6 ms. The correctives are the one
  large item, a project's choice to keep (P7's budget does not cover the player).
- Tests: `VoxelCharacter.Appearance.*` (the pick order, the save round trip, dressing and undressing, the foot IK's
  pelvis drop and tilt) and `VoxelCharacter.Anim.IKValues`' foot-bone weight; `VoxelWorlds.Rendering.BiomeLookup.TwoWorlds`
  (G144).
- Not done: a character-creation screen (out of scope); first person keeps the arms mesh (D48), the parts hidden from
  the owner with the body; no NPC wears an appearance yet (P7).

### P7: NPC bodies and the budget (content, VoxelCharacterPlugin config, VoxelWorldPOI tables)

- The dungeon skeleton skinned to the Mannequin skeleton, sharing the player's layers and action sets.
- Wolf and deer from the Animal Variety Pack, replacing the Blender pipeline's static `SM_Wolf` and `SM_Deer`. Each
  animal has its own skeleton, so the creatures share one template AnimBP (not bound to a skeleton) on
  `UVCAnimInstance`, and each animal is a child AnimBP that only names its clips: idle, walk, run, attack, hit, death,
  and eat or graze while wandering. A new animal is one child AnimBP and one row in the spawn table. Capsule and attack
  reach are fitted to each mesh. Foot placement for four legs (the same node takes a list of legs) only if the creatures
  float on slopes without it.
- The NPC graph: blend spaces and a state machine, foot placement only at LOD0, the Animation Budget Allocator, update
  rate optimisations tied to the AI's distance bands (`vc.AI.FarTickDistance`).
- Gate: `Scripts/qa/npc_bench.py` before and after at 12, 24, 48 and 96 NPCs. Proposed target: animation adds at most
  0.5 ms of game thread at 48 NPCs.

**P7 result (2026-11-07): done.** `CHARACTER_SYSTEM.md` "NPCs", "Bodies" has the rules.

- The dungeon foe wears the CR pack's Grian04 (the user's choice over skinning the blocky placeholder, 2026-11-07) on
  the Mannequin skeleton and shares the player's `ABP_VoxelCharacter`, layers and action sets. A new
  `AVCNPCCharacterBase::HeldItem` holds an item as the player would (the Equipment fragment's mesh, bone, offset, stance
  layer and action set): the foe holds the iron sword in the sword-and-shield stance.
- The wolf and the doe come from the Animal Variety Pack (a content pack, the user's Fab download). They share one
  template AnimBP, `ABP_VCCreature`, bound to no skeleton, on `UVCAnimInstance`. Each animal is a `UVCLocomotionAnimSet`
  (a blend space by speed at the clips' own speeds, the run's speed for the pace, an idle variant: the doe grazes, the
  wolf looks round) and a `UVCActionAnimSet` (bite, hits, death) instead of a child AnimBP: the same one asset per
  animal, and a script sets data assets where a child AnimBP's overrides are out of Python's reach. A new animal is an
  entry in `Scripts/setup_creature_animation.py` and its creature Blueprint. Capsules fit the bodies; the reach did not
  need changing (the wolf closes to 200 uu and its head is about 90 uu ahead of its centre). The four-legged foot
  placement was not needed on the meadows checked; steep slopes were not checked.
- The budget is the engine's Animation Budget Allocator rather than update-rate bands tied to `vc.AI.FarTickDistance`:
  NPC meshes are `USkeletalMeshComponentBudgeted`, significance by distance, `a.Budget.BudgetMs` 0.5. A world's
  allocator starts disabled and only the game enables it (the cvar is a gate), which the first measurements missed: the
  first NPC to play enables it. Foot placement and its IK run at LOD 0 only.
- What the bodies cost, and the two fixes that found: 48 wolves standing in view at about 15 m (the CSV profiler) took
  7.1 ms of game-thread animation, mostly the physics asset's bodies following the pose for a query collision nothing
  used (the capsule is the NPC's collision). A living NPC's mesh now has none, and takes its bounds from the mesh: 2.5 ms.
  With the allocator at 0.5 ms the NPCs' budgeted animation is 0.52 ms (23 of the 48 meshes tick a frame, the rest
  interpolated), the target met.
- `npc_bench.py`, the gate (wolves chasing a moving player; the game thread's median at 12, 24, 48 and 96): `VoxelDemo`
  21.8, 20.5, 20.9 and 24.1 ms against the placeholders' 21.8, 23.0, 29.6 and 43.0; the cubic world 15.4, 18.3, 18.9 and
  19.2 against 18.7, 21.3, 24.1 and 27.3. Lower at every count: the placeholders (six stacked static meshes a creature,
  G146) cost more than the budgeted bodies, and the NPCs' own time (AI, paths, movement) fell with them, 3.0 against
  6.1 ms at 48 on `VoxelDemo`.
- The creature Blueprints carried six static meshes each (G146): the setup script's name check never matched a
  component's `_GEN_VARIABLE` template, so each run added one. The bodies replace them all.
- Verified in PIE: does graze and flee at a run; wolves chase at a run, bite and ragdoll when killed (the melee sweep
  meets the capsule); the foe idles and patrols with the sword in hand.
- Tests: `VoxelCharacter.Anim.CreatureLocomotion` (the set's assets, the idle variant's turns, the pace) and
  `VoxelCharacter.NPC.BodyBudgetAndHeldItem` (the budgeted mesh, the held item's mesh, socket, offset and action set).

### P8: Close

- `CHARACTER_SYSTEM.md` gains an Animation section (the result); this plan gets its frozen banner; open items go to
  `FINDINGS.md`.

**P8 result (2026-11-07): done.** `CHARACTER_SYSTEM.md` had grown its "Animation" section phase by phase (P2 to P5),
"Appearance" (P6) and "NPCs", "Bodies" (P7); the close adds the plan's results paragraph at the head of "Animation" (what
was built, the skeleton, the scripts in their order, what is open). The glossary gains appearance, held item and
locomotion set. This plan is frozen. Open items in `FINDINGS.md`:
- G145: a MetaHuman on a 75 uu block step reaches the lower block 25 uu short (P6).
- G147: the action sets' stand-ins (D51) remain: VanillaLoop's sample set was never downloaded, so the pickaxe digs
  with a two-handed strike and placing and using play the bow pack's pickup and potion.
- G148: a MetaHuman body's post-process AnimBP (its corrective pose drivers) costs 2.3 ms of animation worker a player
  and no appearance setting turns it off (P6's measurement).
Decided at the close: D48 stays (a), the first-person arms mesh (the parts hide with the body in first person). Not
built and not needed so far: four-legged foot placement (the creatures stand on the meadows; steep slopes unchecked),
the stag, fox, pig and crow (one entry and one Blueprint each), NPC MetaHumans. The spike branch
`spike/character-animation-p0` (local, never merged) and the GASP copy `GameAnimationSample_P0` are the user's to delete.

## 6. Risks

| Risk | Response |
|---|---|
| Motion matching's Z-up trajectory (§3) | Our predictor in the gravity frame (S1); the engine interface is Experimental, so it sits in one class; the fallback is P5's state machine |
| MetaHuman cost and art fit: photoreal humans in a stylised low-poly world; Creator is Beta | The appearance layer makes MetaHuman one body among several; S2 shows one on the terrain under the project's lighting before anything depends on it (D43) |
| Fab licences | Pack content and anything derived from it stays out of git (D46) |
| Animations bound to a retired skeleton copy | P1 first; the script finds stragglers |
| GASP's set played through skeleton compatibility (D50): a body whose proportions show (feet dipping, hands off a weapon's grip) | P4's foot placement and hand IK take the small offsets; the conversion (S4's notes: batch retarget plus a script for the databases and choosers) is the fallback for one body or one set |
| Server cost | No pose evaluation for gameplay; parts dropped on a dedicated server |
| The south-pole twist | Principle 3; a test at the pole (P2, P5) |
| Voxel collision under IK: trimesh edges, collision swapped during a re-mesh, 75 uu blocks | Clamp, smoothing and a short hold; tested on all three maps (P4) |
| Build cycle: new modules and UCLASS changes cannot Live-Code | Close the editor, build, relaunch (`CLAUDE.md`, "Notes / gotchas") |

## 7. Decisions

Decided 2026-11-05 by the user, each as recommended; D44 and D50 with their spikes' results the same day (S1 passed, so
D44 is (a); S4 found a cheaper way than D50's (a), and the user took it: (a')).

| ID | Decision | Options | Decided |
|---|---|---|---|
| D43 | MetaHumans' role | (a) every humanoid; (b) one supported body type, the default look decided after S2; (c) stylised only | **(b)**: the architecture does not bet on it; the art direction is yours once S2 shows one in the world. The default look (the user, 2026-11-07): a MetaHuman in smooth worlds and the low-poly body in cubic ones for this project's demos; for the plugins the project's data (P6) |
| D44 | Player locomotion | (a) motion matching (GASP); (b) state machine with blend spaces | **(a) if S1 passes**, else (b); NPCs (b) either way. **Reversed 2026-11-11** (the locomotion state machine plan's D86, at its L4): the player's default is a state machine over the Essential Animation packs, motion matching selectable under `vc.Anim.Locomotion 1` |
| D45 | Code placement | (a) a new module `VoxelCharacterAnimation`; (b) the deps in the core module | **(a)** |
| D46 | Content home | (a) untracked pack folders in `content_packs.txt` (`CharacterAnimation`, GASP's content, `MetaHumans`; the names as D50 left them); (b) tracked under LFS | **(a)**, the existing policy |
| D47 | Quadruped source | (a) PROTOFACTOR's Animal Variety Pack (wolf, deer, fox, pig, crow); (b) the Blender pipeline (rig and actions as scripts); (c) static meshes for now | **(a)**: it has both animals the game spawns, with their clips; its look (realistic low poly) is checked beside the stylised nature packs when it is imported |
| D48 | First person | (a) the arms mesh, as now; (b) the body with the head hidden | **(a)** for now: it keeps the socket map and the view-mode rules; revisit after P6 |
| D49 | Melee feel | (a) server-only ability, the owner's montage cosmetic; (b) a LocalPredicted ability | **(a)**: it keeps damage where it is now; (b) only if (a) feels late on a real connection |
| D50 | The animation skeleton | (a) the UE5 Mannequin, GASP's set converted to it once by a script; (a') the UE5 Mannequin for bodies and our animations, GASP's set unconverted on the UEFN Mannequin, the skeletons marked compatible (S4); (b) GASP's way: the UEFN Mannequin, every body retargeted each frame | **(a')** (the user, 2026-11-05, after S4): nothing converted, no second pose per character, a GASP update taken as it comes; the conversion is the fallback for a body that shows a fault |
| D51 | The gaps (chopping, placing and using) | (a) stand-ins from what we have (a two-handed swing, the mining loop, a pickup); (b) buy a pack; (c) author them | **(a)** for now: the action set is data, so a better clip replaces a stand-in without code |

## 8. Not in scope

Traversal (vault and mantle onto voxel blocks: GASP's traversal needs ledge markup that voxel terrain lacks; a
procedural ledge finder is its own plan), a character-creation screen, facial performance and dialogue, cloth
simulation, MetaHuman Crowd, Mover, UAF.

## Inputs

- GASP for 5.8: `D:\UnrealProjects\GameAnimationSample` (the user's project, left untouched); P0 works on a copy.
- Two Fab downloads the user makes, neither needed by P0: VanillaLoop's sample set (for P3) and the Animal Variety
  Pack (for P7). Each lands in its own folder under `Content/` and joins `content_packs.txt`.
