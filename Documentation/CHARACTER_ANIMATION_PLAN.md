# Character Animation Plan

> **Agreed 2026-11-05: the user approved the plan as drafted, every decision as recommended (D43-D51, §7). P0 under
> way.** Scope: the player's and the NPCs' animation, IK and bodies, MetaHumans among them. Owner:
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

### P4: IK and warping (AnimBP content, small C++ for inputs)

- Foot placement with pelvis adjustment on the base graph; clamped for block-step edges; tolerant of a floor that
  disappears for a frame during a re-mesh (the movement's `FloorGraceDuration` case).
- Orientation and stride warping for the player; slope warping only if foot placement is not enough.
- Look-at (head and eyes) toward the aim or the target; hand IK for two-handed items (bow, pickaxe) via Control Rig.
- `vc.Anim.FootIK`, `vc.Anim.LookAt`, `vc.Anim.Debug` (A/B and debug draw), listed in `EDITOR_CONSOLE_GUIDE.md` §4.
- Done when: PIE on `VoxelDemo`, `Review_Cubic` and `PlanetTest` (equator and south pole) shows planted feet on
  slopes and steps, with screenshots from above and from the side.

### P5: Player locomotion (new module `VoxelCharacterAnimation`)

- If S1 passed: motion matching from GASP's databases and choosers (unconverted, D50), our gravity-frame predictor, turn in
  place, starts, stops, pivots, jump and land; swimming from WaterAnimations (surface and underwater). GASP's CMC
  AnimBP is the starting point, reparented to `UVCAnimInstance`: its reads through GASP's Blueprint interfaces
  (`BPI_SandboxCharacter_Pawn`, `_ABP`) become our C++ values, and its trajectory generation becomes ours.
- If S1 failed: a state machine with blend spaces and distance matching (AnimationLocomotionLibrary), which is safe
  under gravity; motion matching waits for an engine fix.
- Tests: the predictor's samples lie on the tangent plane and turn about local up; without custom gravity they equal
  the engine's own prediction.

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

### P8: Close

- `CHARACTER_SYSTEM.md` gains an Animation section (the result); this plan gets its frozen banner; open items go to
  `FINDINGS.md`.

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
| D43 | MetaHumans' role | (a) every humanoid; (b) one supported body type, the default look decided after S2; (c) stylised only | **(b)**: the architecture does not bet on it; the art direction is yours once S2 shows one in the world |
| D44 | Player locomotion | (a) motion matching (GASP); (b) state machine with blend spaces | **(a) if S1 passes**, else (b); NPCs (b) either way |
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
