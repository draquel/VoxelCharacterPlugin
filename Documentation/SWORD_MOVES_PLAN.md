# Sword Moves Plan: the sword attack flow's phases B to F

> **Agreed 2026-11-19** (the user: "Yes, as recommended": D165-D171 each as recommended, section 7); phases not
> started. Lane 1 (B, C, D) and lane 2 (F) run in parallel, then E (D171).
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
release in the windup or in the loop's first 0.5 s plays `_01` from that pose; still held 1.0 s into the loop, the
charged attack `_15` fires by itself at twice the weapon's damage. A press within a combo keeps phase A's timing (the
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
  through it (the trace), a press in its recovery starts `_01`, walking or running below the threshold plays the combo.
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
