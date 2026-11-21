# Sword Moves Plan: the sword attack flow's phases B to F

> **Frozen 2026-11-20.** This plan is history: agreed 2026-11-19 (the user: "Yes, as recommended": D165-D171 each as
> recommended, section 7); B and C built 2026-11-19, F, D and E 2026-11-20 (section 8), lane 1 (B, C, D) beside lane 2
> (F), then E (D171). As it is now: `CHARACTER_SYSTEM.md` "GAS" › The sword's attack flow and The dodge and the roll,
> "Movement" › Montage root motion, "Input". The TwoHand set's moves (a phase G) were left until wanted.
> Phases B-F of `SWORD_ATTACK_FLOW_PLAN.md` (frozen; phase A built 2026-11-12), the pack demo's other moves:
> B the secondary branch (`draquel/UE5_PluginDev#422`), C the charge (#423), D the run attack (#424), E the air and
> plunge attacks (#425), F the dodge and the roll (#426). Owner: VoxelCharacterPlugin (the montage component, the melee
> ability, a new move ability, the movement component's root motion rule, the input config); the montages and their
> markers in the parent's `Scripts/setup_character_layers.py`. Numbers: D165-D171 (reserved here). G-numbers for what
> the building finds come from the next free when a phase starts. The system as it is: `CHARACTER_SYSTEM.md` "GAS" ›
> The sword's attack flow, "Movement" › Montage root motion, "Input" › The primary action.

## 1. What exists (dev 47bc37e)

- **The combo** (phase A): the SwordShield set's `AttackCombo` is `SSH_Attack_01`, `_02`, `_04`. Every attack montage
  carries its hit at the strike (`UVCAnimNotify_ActionHit`) and `UVCAnimNotify_ActionRecovery` where the follow-through
  ends, both measured from the clip by the layers script. `UVCActionMontageComponent::ClassifyAttackPress` sorts a
  press (too early, kept for the recovery, now, a new combo); the owner sends its combo index with
  `Server_RequestMeleeSwing`; the server's `AcceptAttack` takes it or picks its own. Past the recovery a moving
  character blends out to the locomotion (D103).
- **Root motion** (D106): `ApplyMontageRootMotionMode` sets the body's root motion mode per move from the move's input
  (`bMontageRootMotionOnlyStanding`): standing, a montage's root motion moves the capsule; moving, it is ignored. A
  dedicated server plays the montages that carry root motion. Measured with `vox.PIE.Net 2`, 100 ms of lag and
  `vox.AttackTrace`: the standing lunges (81-123 uu) end at the same place on every machine, with no correction.
- **The secondary action** is RMB (`IA_SecondaryAction`): with a ranged weapon it aims, with a shield in the off hand it
  guards (`UVCBlockAbility::CanStartBlock`), in edit mode it places. With a sword and no shield it does nothing.
- **The press chain** (`TryStartMeleeAttack`, then `StartMeleeSwing` at the air) knows no gait and no air: a press while
  sprinting or falling plays the ground combo.
- **Free keys** in `IMC_Gameplay` (read 2026-11-19): Left Alt, the middle mouse button, G, H, T, Z; on the gamepad
  `Gamepad_FaceButton_Right` and both shoulders. The demo's dodge is on Left Ctrl, our crouch.
- **The moves' network state.** The four saved-move custom flags are taken (sprint, swim rise, walk, the clip caps),
  but the movement component already sends extended move data (`FVCNetworkMoveData`, the wind's, G220), so a new
  predicted value has a place to ride.
- **Damage immunity** exists: a target holding `State.Invulnerable` is refused by `UVCCombatComponent::ApplyDamage`
  (`Rejected_Invulnerable`) and by the damage execution.
- **Stamina**: the sprint spends `SprintStaminaPerSecond` (15), regeneration waits `StaminaRegenDelay`; the guard pays
  for blocked damage.

## 2. The pack's demo

Read from `AnimBP_EssentialSwordShield_UE4` (a T3D export, nothing edited) and its character's input mapping context.
The demo binds LMB to the attack, RMB to the secondary attack, Left Shift to the run, Left Ctrl to the dodge.

| From | To | When | Blend |
|---|---|---|---|
| combo entry | Charge Begin | the primary press, not running, not the secondary | 0.2 |
| Charge Begin | `_01` | the button released | 0.2 |
| Charge Begin, then Charge Loop | `_01` | released in the loop's first 0.5 s | 0.2 |
| Charge Loop | Charged Attack (`_15`) | still held 1.0 s into the loop (about 1.45 s after the press): it fires by itself | 0.2 |
| Charged Attack | `_02` | a press once under 65 % is left | 0.2 |
| combo entry | Secondary_01 (`_08`) | the secondary press | 0.2 |
| `_01` / `_02` | Secondary_02 (`_09`) / Secondary_03 (`_10`) | the secondary press under 70 % left (the primary goes on to `_02`, `_04`) | 0.1 |
| `_08` | `_09` or `_02`; `_09`: `_10` or `_04` | the same | 0.1 |
| `_04`, `_10` | a new combo | a press under 50 % left | 0.2 |
| Idle/Run | Attack_Run (`_12`) | the attack pressed while running | 0 |
| Attack_Run | the combo | a press under 40 % left | 0.2 |
| Jump Start / Loop | Attack_Air_Begin | the attack pressed in the air | 0.1 |
| Jump Start / Loop | Attack_Plunge_Begin | the secondary pressed in the air | 0.1 |
| Air / Plunge Begin, Loop | Landing | Begin ends into Loop; Loop until on the ground | 0.2, 0 |
| Idle/Run, any attack of the combo | Dodge | the dodge pressed | 0 |
| Dodge | Roll | in its first 10 %, with the character's isRoll flag (set by its input graph, not traced) | 0.2 |
| Dodge | the combo | an attack press, any time | 0.2 |
| Roll | the combo | a press under 40 % left | 0 |
| Attack_Run, Jump End, a landing | Roll | the dodge pressed (the run attack under 62 % left) | 0.2 |

- **The guard** in the demo is reactive: Idle/Run goes to Guard only when a hit arrives while the secondary is held.
  Ours (D94) is a held guard on RMB; the demo's RMB-as-attack is what B has to fit beside it.
- **Movement does not leave** any of these before its clip ends; the dodge leaves an attack at any time. Phase A's rule
  (break the recovery on the move) applies to each new attack the same way unless a decision below says otherwise.

## 3. The clips

Measured 2026-11-19 from the UE5 clips by the layers script's rule (30 samples a second; the strike is the fastest
hand's fastest frame, the recovery the first settle after it; the travel is the root's, forward / side, with the time
by which 90 % of it is done; seconds at rate 1). Every clip below has root motion on unless marked; none moves the
root up or down. Phase A's three for scale: `_01` 1.67 s, strike 0.48, recovery 0.68, 83 uu.

| Phase | Clip | Length | Strike | Recovery | Travel |
|---|---|---|---|---|---|
| B | `SSH_Attack_08` (Secondary_01) | 2.00 | 0.47 | 0.77 | 232 forward by 0.43 |
| B | `SSH_Attack_09` (Secondary_02) | 1.67 | 0.41 | 0.65 | 123 by 0.37 |
| B | `SSH_Attack_10` (Secondary_03) | 1.67 | 0.41 | 0.88 | 101 by 0.37 |
| C | `Attack_Charge_Begin` | 0.50 | - | - | 0 (the windup into the held pose) |
| C | `Attack_Charge_Loop` | 1.50 | - | - | 0 (the held pose, looping) |
| C | `Attack_Charge_End` | 1.00 | - | - | 0 (lowers the charge without a strike) |
| C | `SSH_Attack_15` (charged) | 3.00 | 0.80 | 1.27 | 137 by 0.77 |
| D | `SSH_Attack_12` (run) | 2.50 | 0.77 | 1.07 | 452 by 0.67 (about 675 uu/s: the sprint's 700) |
| E | `Attack_Air_02` Begin / Loop / Landing | 0.50 / 0.83 / 2.17 | 0.37 (Begin) | 0.73 (Landing) | 0; Begin and Loop without root motion |
| E | `Attack_Plunge_01` Begin / Loop / Landing | 0.40 / 0.83 / 2.00 | 0.13 (Landing: the slam, the fastest hand of any clip here) | 0.53 (Landing) | 0; Begin and Loop without root motion |
| F | `Dodge_01` FL, FR / L / R / BL, BR | 1.00 | - | 0.63-0.77 | 206 forward / left / right / back by 0.27-0.33 |
| F | `Dodge_02` (the same six) | 1.17 | - | 0.55-0.89 | 218 by 0.65-0.79 (slower, lower) |
| F | `Roll` F / L / R / BL, BR | 1.08 | - | 0.51-0.88 | 446 by 0.44; the pelvis drops to 13 cm |

- **The dodges' FL and FR both go straight forward** (BL and BR straight back): the pair is which foot leads, not a
  diagonal. So the dodge has four directions, the roll four (forward, left, right, back), and no clip goes diagonally.
- **The run attack is authored at the sprint's speed**: its root carries the capsule at about 675 uu/s, so on the run it
  should move the capsule, unlike phase A's lunges (D168).
- **The charge's windup is short** (0.5 s) and the charged strike late (0.8 s into a 3 s clip): from the press to the
  charged hit is about 2.25 s.
- `_03`, `_05`-`_07`, `_11`, `_13` and `_14` stay unused (`_13`/`_14`: 330 uu dashes, no place in the demo's graph).
- The TwoHand set (`2H_*`) has its own charge, plunge, dodges and rolls; the data below is per set, so it can follow
  once the sword's work is done (section 5).

## 4. Decisions

**D165, one shape for every move.** Each new move is an action montage the owner plays at its input and the server
plays from a request, as phase A's attack: the same request carries a move kind and an index (the secondary branch,
the charge's start and its strike, the run attack, the air and plunge attacks, a dodge or a roll and its direction), the
server checks it with one predicate both sides call (D-CR16), and the montage carries the markers the layers script
measures (hit, recovery, and for the multi-part moves their sections). Recommended over a state machine in the anim
graph, the demo's way: the attack is already a montage, the network form is measured, and the graph stays the
locomotion's. The data lives on `UVCActionAnimSet` per set, so a set without a move simply has none (the press falls
back to what it does today).

**D166, the secondary branch's input (B).** Recommended: **its own key, the middle mouse button** (gamepad: the right
shoulder), so RMB stays the guard (D94) and nothing about blocking changes; a press of it plays the demo's branch
(`_08` from rest; `_09` after `_01` or `_08`, `_10` after `_02` or `_09`; then `_04`, `_10` end it as `_04` does now)
by the same press classes as the primary. The combo becomes a small table, each step naming its next step on the
primary and on the secondary. Alternative: the demo's, RMB attacks inside a combo window and guards at rest (with a
shield) or always attacks (without one); it keeps two buttons, but a player cannot raise the guard during a combo
(up to 1.3 s), and a held RMB means two things.

**D167, the charge (C).** Recommended: **the first attack of a combo starts at the release, as the demo's.** A press
from rest (not sprinting, not in the air) plays `Attack_Charge_Begin` at once (the windup reads as the response); a
release any time before the charged strike plays `_01` from that pose (corrected 2026-11-19 when C was built: this
read "in the windup or in the loop's first 0.5 s", the demo's rule, which leaves the rest of the loop without a
transition; section 5's agreed test has a release at 1.2 s play `_01`; `Attack_Charge_End` is only the cancel); still
held 1.0 s into the loop, the charged attack `_15` fires by itself at twice the weapon's damage. A press within a combo keeps phase A's timing (the
charge is from rest only). The cost: a tapped first attack starts when the button comes up, a tap's length later
(about 0.1 s), from the windup pose. The owner tells the server when the charge begins (others see the windup and the
server starts its clock) and sends the charged strike; the server takes it when its own clock has run the charge
time less the latency allowance (0.2 s), else plays `_01`. After `_15`'s recovery a press continues with `_02` (the
demo's). Alternatives: the charge on a hold of the secondary key (no delay on the primary, a third role for a key), or
no charge.

**D168, the run attack (D) and moves that carry the capsule.** A primary press while sprinting (the sprint gait and
over 550 uu/s, halfway from the run's 400 to the sprint's 700) plays `_12` instead of the combo. Recommended: its root
motion **moves the capsule on the move too**, the one exception to D106 so far, since it is authored at the sprint's
speed: a montage flag (carries the capsule) that `ApplyMontageRootMotionMode` reads on every machine, so the owner's
prediction and the server's replay agree as they do now. Its recovery (1.07 s) breaks to the locomotion when the
character still moves; a press in the recovery starts a new combo (`_01`). The dodge and the roll (D170) use the same
flag. Alternative: D106 as it is (the run carries the capsule and the clip's root is ignored): the body would slide,
since the clip's feet are planted for a lunge of 452 uu.

**D169, the air and plunge attacks (E).** A primary press while falling (not swimming, not in the QA flight)
plays `Attack_Air_02`: the Begin section (its hit at 0.37 s, swept as the ground swing's), the Loop while still falling,
the Landing section on touching the ground; one air attack per time in the air. The secondary key (D166's) while
falling plays `Attack_Plunge_01`: Begin, Loop, and on the ground the Landing's slam, which hits every hostile within
200 uu of the feet at 1.5 times the weapon's damage, swept by the server at its own landing. Recommended: **the fall
stays the fall** (gravity in the gravity frame, so a planet's down; no extra speed), so nothing new is predicted; a
faster plunge would ride in `FVCNetworkMoveData` and is a later change if the plunge reads slow. The server checks the
request against its own movement mode (falling, or landed within the latency allowance) and lands the slam on its own
landing. Alternatives: the plunge speeds the fall (a predicted value from the start), or only the air attack (no
plunge).

**D170, the dodge and the roll (F).** Recommended:
- **The input**: Left Alt (gamepad: `Gamepad_FaceButton_Right`), the demo's Left Ctrl being our crouch.
- **Which move**: with movement input, a roll toward the input (the capsule turns to it at the start, then `Roll_F`);
  with none, a back step (`Dodge_01_BL` or `BR`); while the facing is locked to the view (a bow drawn, the first-person
  view: `SetFacesAim`), a dodge toward the input relative to the facing (`Dodge_01` forward, left, right, back) and no
  roll. The guard does not lock the facing, so with it up the roll is the move. The capsule moves
  by root motion whatever the input (D168's flag).
- **From an attack**: any time, as the demo's; the cancelled swing's hit does not land (the server ends its ability).
  **Into an attack**: a press during a dodge or a roll is kept and plays `_01` at its recovery (0.63-0.77 s), the same
  press classes as the combo. No jump and no guard before the recovery.
- **The cost**: 15 stamina for a dodge, 20 for a roll; refused without it. Regeneration waits as after the sprint.
- **The immunity**: the server holds `State.Invulnerable` from the start to 90 % of the travel (a dodge 0.3 s, a roll
  0.44 s), on its own timeline. Alternative: none (the move only takes the body out of reach).
- **The network**: phase A's form (the owner plays at once, the server on the request). The dodge moves 206 uu in 0.3 s
  and the roll 446 in 0.44 s, four to six times the lunge's speed, so the server's late start (0.1-0.2 s) may correct
  the owner: the phase's first step measures it with `vox.PIE.Net 2`, 100 ms of lag and corrections shown. If it
  corrects, the move becomes a predicted root motion source (the plugin's first ability predicted on the owner), the
  one larger change in this plan, only if needed.

**D171, the order.** Recommended: two lanes. **Lane 1: B, then C, then D**, one after the other, since all three change
the press chain and the combo (`ClassifyAttackPress`, `AcceptAttack`, the request). **Lane 2: F**, in parallel with
lane 1: a new ability, the input, the movement component's root motion flag; it shares only the request's shape (D165),
so lane 2 adds the move kind and lane 1 merges onto it. **E last**, after both: it needs D166's key and D168's flag.
Each lane runs one session with its own worktree and editor port. The SwordShield set's data asset is binary and both
lanes change it: the one merged second reruns `Scripts/setup_character_layers.py` on top of the other (the script
builds the set from code) rather than merging the asset. The TwoHand set's moves follow as a short phase G once the
sword's are done, if wanted.

## 5. Phases

Each phase is built on a branch from `dev` in VoxelCharacterPlugin and the parent, gated as `Scripts/qa/README.md`
says (any code: the build, the headless suite, the editor gate with the replication test), and checked in PIE on
`CharacterTest` with `vox.PIE.Net 2` (a dedicated server, two clients, 100 ms of lag) and on a planet world for the
gravity frame (a lunge, a roll and a plunge on a slope away from the pole). Each adds its rows to
`CHARACTER_SYSTEM.md`, the console guide for any `vc.*` it adds, and closes its issue.

- **B, the secondary branch** (#422). The combo table (D166); the key and its input action (`Scripts/add_input_action.py`);
  the montages `_08`, `_09`, `_10` with their markers; the request's move kind (if lane 2 has not merged it). Tests: the
  table's walk (primary and secondary in every order), the press classes on the new step, the server's acceptance of a
  secondary index. Done: from rest the secondary plays `_08`; `_01`, secondary, primary plays `_01`, `_09`, `_04`; the
  guard still rises on RMB at any time.
- **C, the charge** (#423). The charge's three clips as one montage (Begin, a looping Loop, End for a cancel), `_15`
  with its markers and the damage multiplier; the press at rest starts the charge, the release or the time decides
  (D167); the server's charge clock. Tests: a tap plays `_01` at the release, a hold plays `_15` at 1.45 s, a release at
  1.2 s plays `_01`, a press in a combo is not a charge, the server refuses a charged strike sent early. Done: in PIE the
  three cases, and with lag the others see the windup and the same strike.
- **D, the run attack** (#424). The sprint test in the press chain, `_12` with its markers and D168's flag. Tests: the
  gait threshold, the flag's root motion mode on the move. Done: sprinting, a press plays `_12` and the speed holds
  through its travel (the trace; corrected 2026-11-20: the clip stops the body at its strike, section 8), a press in its recovery starts `_01`, walking or running below the threshold plays the combo.
- **E, the air and plunge attacks** (#425). The two montages with sections (Begin, a looping Loop, Landing), the jump to
  the Landing section on landing on every machine, the server's checks and its slam at its own landing (D169). Tests:
  the falling predicate and its allowance, one air attack per fall, the slam's radius. Done: from a jump each plays
  through, the plunge's slam hits two training dummies within 200 uu and not a third beyond; on a planet the same
  away from the pole.
- **F, the dodge and the roll** (#426). Step 1 measures the network form (D170) with the clips played as montages
  before any rule; then the move ability (server, its stamina and its immunity window), the input, the direction rule,
  the cancel from an attack and the buffered attack after. Tests: the direction pick (input, none, a locked facing),
  the cost refused, the immunity window on the server, a swing cancelled loses its hit, a press during the move fires at
  its recovery. Done: in PIE each direction, the cost and the immunity shown on a dummy's attack, no correction with lag
  (or the measured need for the predicted form, then built).

## 6. Friction

- **The combo stops being a list.** `AttackCombo` is an array today; B makes it a table of steps with two next steps.
  The SwordShield set is the only one with a branch; the others keep a list (a table with no secondary steps).
- **The charge delays the first press.** D167 is the only decision here that changes how the existing attack feels:
  every first swing of a combo waits for the release. It is easy to try both: the charge can sit behind a cvar for the
  A/B (named when built).
- **Root motion with input.** D168's flag is the first exception to D106. The rule stays one predicate per move on
  every machine; what is new is a montage that must carry the capsule on the move, which the server already plays.
- **The dodge's speed on the network** is the unknown (D170). Phase A's measurement holds for slow lunges; the dodge is
  measured first, and the predicted root motion source is the fallback, not the plan.
- **Binary assets in two lanes** (D171): the set's data asset and any montage both lanes touch. The montages are
  disjoint by move; the set is rebuilt by the script.
- **NPCs** use none of this (their locomotion graph is the open D93, #412); their attacks stay the ground combo.

## 7. For the user (agreed 2026-11-19: each as recommended)

1. D165: every move as a montage with a request and a shared server check, as phase A's attack (recommended), not the
   demo's anim-graph state machine?
2. D166: the secondary branch on its own key, the middle mouse button (recommended), or on RMB inside a combo as the
   demo (the guard then waits for the combo)?
3. D167: the charge from rest, the first attack at the release (recommended), the charge on the secondary key, or no
   charge?
4. D168: the run attack while sprinting, its root motion carrying the capsule on the move (recommended)?
5. D169: the air attack on the primary and the plunge on the secondary key in the air, the fall unchanged
   (recommended), a faster plunge, or no plunge?
6. D170: the dodge and roll on Left Alt, a roll toward the input or a back step without, 15 and 20 stamina, immunity
   for the travel (recommended); any of these changed?
7. D171: lane 1 (B, C, D) and lane 2 (F) in parallel, then E (recommended), or one phase at a time?

## 8. Results

**B, the secondary branch** (#422; built 2026-11-19 on `claude/sword-moves-b-c` in VoxelCharacterPlugin and the parent).
As D166: `IA_SecondaryAttack` on the middle mouse button and the gamepad's right shoulder (`add_input_action.py`),
`AVCCharacterBase::PressSecondaryAttack`; the combo a table of steps on the set (`FVCAttackComboStep`:
`AttackComboSteps`, `SecondaryAttackStart`), the other sets keeping their list (no table, no secondary). The layers
script measured the branch's markers as section 3 has them (`_08` 0.467 / 0.767 s, `_09` 0.408 / 0.646, `_10` 0.408 /
0.884). Built otherwise than drafted: **the secondary is no field of the request**: it rides on the
attack's combo index (first on phase A's `Server_RequestMeleeSwing`, since F's merge on D165's
`Server_RequestMove` with `EVCMoveKind::Attack`), the server reading the button from it (each attack of the table is one button's, `IsSecondaryAttack`) and
picking its own for that button when it refuses the owner's. Tests `VoxelCharacter.Anim.Sword.ComboTable` (the 16 orders
of four presses), `.SecondaryPress`, `VoxelCharacter.Combat.Melee.AcceptSecondary`. PIE on `CharacterTest` with a
dedicated server, two clients and 100 ms of lag: from rest the secondary plays `_08`; `_01`, the secondary, the primary
play `_01`, `_09`, `_04`, the server 0.12-0.13 s after the owner, all three machines ending at the same place, no
correction; the guard rises on RMB at rest, during `_08` and during a charge. On `PlanetTest` at 30 N 40 E, on a slope:
`_08` moved the capsule 232.3 uu across local up (the clip's 232) and 18.5 uu up the slope, the walk 305.7 (83 + 123 +
101). Found: **G252** (the guard raised during any attack's lunge makes the server correct the owner, 37 corrections over
phase A's `_01`, 18 over `_08`; on `dev`, not caused here).

**C, the charge** (#423; built 2026-11-19, the same branches). As D167, one montage `AM_SwordShield_Charge` (the windup,
`Loop` looping, `End`), `_15` at combo index 6 with twice the damage in its step (`DamageMultiplier`, applied by the melee
ability) and `_02` after it; the owner's charge changes as move requests (D165: `EVCMoveKind::Charge`, its index an
`EVCChargeChange`: begin, lower, stop; built first as a request of its own, `Server_SetCharge`, moved onto F's request at
the merge) and the charged strike as an attack request with its combo index, which `AcceptAttack` takes once the server's own clock has run the charge time less 0.2 s.
`vc.Anim.AttackCharge` (1; 0 the first attack at the press) is the A/B. Built otherwise than drafted: **a release any
time before the charged strike plays `_01`** (section 5's test, a release at 1.2 s plays `_01`; D167's text had the
demo's `_01` only within the loop's first 0.5 s and no transition after it; agreed with the coordinating session on
2026-11-19 and D167 corrected to match): the lowering (`End`) is the cancel, played
when a sprint or a jump begins during the charge; the guard over a charge stops it, the secondary attack replaces it, a
flinch interrupts it. The charge time is the windup's section and the hold, 1.5 s from the press (section 2's "about
1.45 s" is the demo's blend). The dedicated server plays the charge (its clips carry root motion, travelling none) and
now stops what it plays on a stop's multicast (it ignored stops before; its held loop went on). `vox.PIE.Net` gained
`lag=<ms>` (the session's network emulation: `NetEmulation.PktLag` typed during an in-process session did not reach its
drivers, the server taking a request in the frame after the owner's) and `vox.AttackTrace` the secondary (`s`), a held
primary (`h`), the guard (`g`) and a jump (`j`). Tests `VoxelCharacter.Anim.Sword.ChargeRule`,
`VoxelCharacter.Combat.Melee.ChargedStrike` (a strike sent early is `_01`, one at the charge time less the allowance is
`_15` at twice `_01`'s damage, `_02` after it). PIE as B's: a tap plays `_01` at the release; a release 1.2 s into the
charge plays `_01`; a hold plays `_15` 1.51 s after the press, the server taking it (its clock began 0.12 s after the
owner's) and a press in its recovery `_02`; the guard and the secondary over a charge, and a jump lowering it (the
montage at its `End`, 2.0 s); each on the three machines, the same place, no correction. On the planet the charged
`_15` moved 137.0 uu across local up (the clip's 137).

**On F** (2026-11-20, after lane 2 merged first): both branches rebased onto `dev` with F, the attack's and the charge's
requests on F's `Server_RequestMove` (`EVCMoveKind::Charge` added), a press during a dodge or a roll kept as F keeps it
(never a charge: a kept press does not start one, and the charge rule refuses during a move), a dodge or a roll ending a
charge on both machines; `IMC_Gameplay` and `VCInputConfig` taken from `dev` and `IA_SecondaryAttack` added again by
`add_input_action.py`, the SwordShield set rebuilt by the merged layers script (its seven attacks, the charge, F's dodges
and roll). PIE as before with `vox.PIE.Net 2 lag=100`: the secondary chain (`_01`, `_09`, `_04`), a release 1.2 s into
the charge (`_01`), the full hold (`_15`, then `_02`), a step back out of a charge (the release after it plays nothing)
and out of `_08`; each on the three machines, ending at the same place, no correction.

**Gate** (B and C on F, the rebased tip): `build.ps1` no warning against the baseline (`build_warnings.py`); the headless
suite 1074 of 1074 (F's 1069 and B's and C's five), no new warning kind against the run before the rebase (only F's new
test worlds' `CleanupWorld` line); the editor gate 36 of 36 (the replication tests with a dedicated server among them),
its 210 parity lines identical, the Nanite layouts 0 differ. Before the rebase the same gates had passed on `dev`
86c3483 (1068 of 1068, the editor gate 36 of 36) with `build.ps1 -Shipping` linking (the test file added); B alone built
with its tests passing before its commit.


**F, the dodge and the roll** (#426; built 2026-11-20 on `claude/sword-moves-f` in VoxelCharacterPlugin and the
parent). **The measurement first (D170's network):** the moves were built in phase A's form with D165's request and
D168's flag, then measured on `CharacterTest` with a dedicated server, two clients and 100 ms of lag on every machine
(`NetEmulation.PktLag 100` set in the editor before `vox.PIE.Net 2`: the emulation is kept for the drivers made later;
typed during a session it reaches only the drivers of the world it runs in), `p.NetShowCorrections 1` and
`vox.AttackTrace` (which gained the dodge's press, a walk to a side, the play world and the immunity). The server starts
each move 0.12-0.14 s after the owner and the other client 0.25 s after it; steps back standing (205 uu), rolls from a
walk forward, back, left and right (446 uu, the input held into the roll), a step out of a swing and an attack pressed in
a step all end at the same place on the three machines (within 1.2 uu) with **no correction** in about twenty runs:
the server's montage moves by the owner's moves, as the lunge's did. So the predicted root motion source was not built.
The owner sends its pending moves before the request (`vc.Net.MoveFlush`, as GAS does at an activation): without it
one roll in nine drew a burst of 12 corrections of up to 5.7 uu. **First person** was the one case that corrected (up
to 378 uu, a step to the side): the server's capsule faces where the body walked, the owner's the view, so the request
carries the facing the move starts with (`FVCMoveRequest::Direction`, a roll's input and a step's facing) and the capsule
keeps it through the move (`FaceRotation`); then steps from rest in first person end within 1 uu with no correction. The
cause also corrects a first-person strafe and an attack's lunge, which this phase leaves as **G256**.

**The move** as D170: `IA_Dodge` on Left Alt and `Gamepad_FaceButton_Right` (`add_input_action.py`); with movement
input a roll toward it (`Roll_F`, the capsule turned to the input first), with none a step back, with the facing locked
to the view a step to the input's side of it (`Dodge_01` FL, BR, L, R); 15 stamina a step and 20 a roll, refused short of
it, the regeneration waiting as after the sprint; the server's `State.Invulnerable` from the start to the montage's
travelled marker; from an attack any time (the swing's hit does not land); a press in the move plays `_01` at its
recovery; no jump and no guard before it. D165: `Server_RequestMeleeSwing` became `Server_RequestMove(FVCMoveRequest)`
(`EVCMoveKind` Attack, Dodge, Roll; an index; a facing), the attack's index riding as before. D168:
`UVCAnimMetaData_CarriesCapsule` on a montage, read by `ApplyMontageRootMotionMode` on every machine. The layers script
measured the clips as section 3 has them (travel 205-208 uu by 0.27-0.33 s a step, 446 uu by 0.44 s the roll; recovery
0.63, 0.67, 0.77 and 0.70 s forward, back, left, right, 0.68 s the roll). Built otherwise than drafted: **the step back
is `BR`, not `BL`** (`BL`'s fastest hand frame falls late, 0.43 s, and the attack's rule finds no settle after it; the
two are the same step on the other foot); **a step or a roll past its recovery breaks to the locomotion on the move**,
phase A's rule for an attack (D103); and **the press needs the ground** (no dodge in the air or swimming), which D170 does
not say.

**Checked in PIE** (`Saved/QA/sword_moves_f/`): each side of the step and the roll; six steps spend the stamina to
10 and the seventh is refused ("short of stamina"); `vox.Damage` inside the server's window is `Rejected_Invulnerable`
and 0.9 s later lands; a step 0.19 s into a swing cancels it (the server logs no swing); a press 0.3 s into a step plays
`_01` at its recovery (0.67 s) on every machine; mid-step the owner can neither jump nor raise the shield, after it both.
On `PlanetTest` at 35 N 30 E, on a slope: the step back moved 204.9 uu along the surface (the plane's 204.9), a roll from
a walk carried the body along the surface upright (`planet_roll_mid.png`). Found: **G255** (the QA teleport's settle
hold strands a pawn on a map without a voxel world under `vox.PIE.Net`), **G256**.

**Gate.** `build.ps1` with no warning against dev's (`build_warnings.py`) and the non-unity build of the two modules changed
(VoxelCharacterPlugin, VoxelEngine) clean; the headless suite 1069 of 1069, the six new tests among them
(`logwarnings.py`: no new kind, only the new test worlds' names in the EndPlay line every `FPlayWorld` test logs); the
editor gate 36 of 36 (the replication tests with a dedicated server among them), the 210 parity lines identical, the
Nanite layouts unchanged.

**For the merge with lane 1** (B and C, VoxelCharacterPlugin#128, the parent's #560): the request is this branch's
(`Server_RequestMove`), so B's secondary index rides on `EVCMoveKind::Attack`; `EVCAnimAction` gains `Dodge` and `Roll`
here and `Charge` there; both add an input action to `IMC_Gameplay` and `VCInputConfig` (take one side's assets and
rerun `add_input_action.py` for the other's action: idempotent); both extend `vox.AttackTrace` (here `d`, `u`, a walk
direction, `pie=`, `stam=`, `inv=`; there `s`, `h`, `g`, `j`). The SwordShield set (shared content) was given its dodge
and roll in memory only for this branch's checks: after both merge, rerun `Scripts/setup_character_layers.py` on top of
`dev` and save the set.

**D, the run attack** (#424; built 2026-11-20 on `claude/sword-moves-d` in VoxelCharacterPlugin and the parent, on
`dev` with B, C and F). As D168: a fresh primary press while sprinting faster than the set's `RunAttackSpeed` (550 uu/s,
across local up; `CanRunAttackFrom`) plays `_12`, the SwordShield set's `RunAttack` (combo index 7, its step `_01` /
`_08` after it), its montage flagged to carry the capsule (F's `UVCAnimMetaData_CarriesCapsule`, set by the layers
script, which measured the clip as section 3 has it: hit 0.767 s, recovery 1.067). A kept press (from an attack's swing
or a dodge) and a charge's release never play it (`bFreshPress`); the server takes the owner's index 7 while its own
character sprints faster than the speed less `RunAttackSpeedTolerance` (100 uu/s), else picks the primary's. Built
otherwise than drafted: **the speed holds through the travel, not through the strike.** The clip is a run-in lunge
(about 1000 uu/s at its start, then 580) that stops the body by 0.9 s, at the strike, and stands until its recovery
(1.07 s), when the attack's break hands it to the locomotion and the sprint starts again from rest (about 1 s back to
700). Asked before deciding; **the user chose to keep it as authored** (2026-11-20, "Stop as authored"), and section 5's
done line now says "through its travel". The alternative was the flag only up to the clip's travelled marker, then the
locomotion carrying the capsule under the strike. `vox.AttackTrace` gained a held sprint (`r0.5-2`). Tests
`VoxelCharacter.Anim.Sword.RunAttackRule` (the gait and the speed, the allowance, the set's table; the layers script's
set: `_12` carries the capsule, `_01` does not) and `VoxelCharacter.Combat.Melee.RunAttack` (a fighter sprinting on a
floor: walking plays `_01`, a kept press sprinting `_01`, a fresh one `_12`; the server takes `_12` sprinting fast
enough, `_01` under the speed and not sprinting; nothing before `_12`'s recovery, `_01` after it).

**Checked in PIE** on `CharacterTest` with `vox.PIE.Net 2 lag=100` (`Saved/QA/sword_moves/trace_d_*.txt`): a press at
670 uu/s plays `_12` on the owner and the server 0.13 s later, the body carried 453 uu (the clip's 452) on both; a press
0.96 s into it is kept and plays `_01` at its recovery; a press walking (371 uu/s), one 0.1 s into a sprint (448 uu/s)
and one after `_12`'s recovery under the speed (268 uu/s) play `_01`; a dodge 0.4 s into `_12` rolls out of it. Each
ends at the same place on the three machines with no correction. A press while falling plays the combo (the air attack
is E's). On `PlanetTest` at 30 N 40 E, on a slope: the sprint's press played `_12`, which carried the body 404 uu across
local up, standing; the slope slowed it as it slowed the sprint (0.89 of the plane's both, 624 uu/s against 700).

**Gate** (D, the branch's tip): `build.ps1` no warning (`build_warnings.py`), `build.ps1 -Shipping` linking with no
warning; the headless suite 1076 of 1076 (dev's 1074 and D's two), no new warning kind against B and C's last run (only the
new test world's name in the EndPlay line every `FPlayWorld` test logs); the editor gate 36 of 36 (the replication tests
with a dedicated server among them), its 210 parity lines identical, the Nanite layouts 0 of 9 differ.

**E, the air and plunge attacks** (#425; built 2026-11-20 on `claude/sword-moves-e` in VoxelCharacterPlugin and the
parent, on `dev` with B, C, D and F). As D169: a primary press while falling plays `_Air_02`, the secondary's
`_Plunge_01`, each one montage of three sections (the swing, the fall's loop looping, the landing; the layers script's
section names `Loop` and `Landing`), the SwordShield set's `AirAttack` 8 and `PlungeAttack` 9 in its combo, measured as
section 3 has them (the air swing's hit 0.367 s, its landing at 1.333 and its recovery 0.733 into it; the plunge's slam
0.133 into its landing at 1.233, recovering 0.533 into it). One per fall, either one. Every machine jumps to the landing
section at its own landing (the movement component's mode change, not in a replay of moves; into the water the montage
stops), the attack's times then counting from the landing; the plunge's slam at the server's own landing (the melee
ability waits for the fall's end) hits every hostile within `PlungeRadius` (200 uu) of the feet, across local up and
along it, at 1.5 times the weapon's damage (its step's multiplier). The server takes an air index while its character
falls or landed less than 0.2 s ago, else refuses it. The fall stays the fall, and to keep it so **no montage's root
motion is taken in a fall** (`ShouldTakeMontageRootMotion`'s `bFalling`): taken, a montage's root motion replaces the
fall's velocity across local up, so a jump forward with no input would stop in the air under the swing; D169 says the fall
is unchanged, so this follows from it and is not a new decision. Tests `VoxelCharacter.Anim.Sword.AirAttackRule` (the
fall and its allowance, one per fall, the slam's reach on a plane and under a planet's up, the set, no root motion in a
fall; the layers script's set: the sections, the hits, the recoveries) and `VoxelCharacter.Combat.Melee.Plunge` (a
fighter dropped onto a floor among three dummies: nothing before the landing, the two at 150 and 190 uu at 1.5 times its
swing, not the one at 260; then the press classes from the landing, the allowance, and the air attack once a fall).

**Checked in PIE** on `CharacterTest` with `vox.PIE.Net 2 lag=100` (`Saved/QA/sword_moves/trace_d_e_*.txt`): from a
jump, the air attack 0.3 s after it on the owner and the server 0.12-0.13 s later and the other client, each jumping to
the landing at its own touchdown (1.26, 1.39, 1.51 s), ending at the same place with no correction; from a 2 m ledge
drop the air attack and the plunge the same, the fall's speed kept (282 to 317 uu/s under the walk's input); a 30 m fall
held the loop until the touchdown and a second press in it did nothing; a running jump with the input let go kept its
341 uu/s through the swing. The plunge among three dummies placed 150, 176 and 260 uu from where it landed hit the first
two for 27 (the sword's swing at one of them: 18) and not the third. On `PlanetTest` at 30 N 40 E, on a slope, the air
attack from a jump played to its landing at the touchdown; at 31 N 40 E with the NPCs' AI off (`vc.AI.Enabled 0`; with
it on, the dummies walk up to the player before the slam) the plunge's slam hit a dummy 184 uu across local up from the
feet and missed one 617 uu away.

**Gate** (E, the branch's tip): `build.ps1` no warning (`build_warnings.py`), `build.ps1 -Shipping` linking with no warning;
the headless suite 1078 of 1078 (dev's 1076 and E's two), no new warning kind against D's run (only the new test world's
name in the EndPlay line every `FPlayWorld` test logs); the editor gate 36 of 36 (the replication tests with a dedicated
server among them), its 210 parity lines identical, the Nanite layouts 0 of 9 differ. No finding.
