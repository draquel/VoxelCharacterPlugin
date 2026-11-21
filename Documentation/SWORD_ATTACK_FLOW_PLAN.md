# Sword Attack Flow Plan: the pack's combo, broken off by movement after the follow-through

> **Frozen 2026-11-12.** This plan is history: agreed the same day as drafted (the user: "Yes, as recommended":
> D102-D107 each as recommended), phase A built in one pass (section 8); phases B-F wait until wanted
> (`draquel/UE5_PluginDev#422`-`#426`). As it is now: `CHARACTER_SYSTEM.md` "GAS" › The sword's attack flow, "Movement"
> › Montage root motion. The user, on the sword's attacks: walking or running
> should interrupt an attack as soon as its follow-through ends and blend back to the locomotion, while standing the
> recovery plays out; and the sequence should follow the Sword and Shield pack's demo character. Owner: VoxelCharacterPlugin
> (the montage component, the melee ability, the character's primary action); the montages' markers in the parent's
> `setup_character_layers.py`. Numbers: D102-D107, G178-G180 (reserved here, entered in `Documentation/FINDINGS.md` when
> built). Terms: `Documentation/GLOSSARY.md`. The system as it is: `CHARACTER_SYSTEM.md` "GAS" › the sword and the
> shield, "Animation" › Action montages, What plays when, The state machine.

## 1. What exists

Read 2026-11-12 from dev 1bdd0d0, and in PIE on `CharacterTest` (the Longsword drawn, the state machine graph).

- **The swing.** The primary action with a melee weapon (`StartMeleeSwing`): the owner picks the attack
  (`UVCActionMontageComponent::ChooseAttack`) and plays it at once; the server's `UVCMeleeAttackAbility` (ServerOnly)
  picks again, sends it to everyone else and lands the sweep at the montage's hit time, read from the asset. Both sides
  rate-limit to one swing per `1 / AttackSpeed`.
- **The combo.** The SwordShield set's `AttackCombo` is `SSH_Attack_01`, `_02`, `_03`; `_04` is the Chop. The next
  attack continues the combo within `ComboWindow` (0.6 s) after the montage's end. Each montage plays at the rate that
  fits it into one attack interval: the Longsword's `AttackSpeed` 1.1 plays the 1.67 s clips at 1.83, so a swing lasts
  0.91 s, the rate limit is 0.91 s too, and the next attack can only start once the last one has fully recovered.
- **The hit** is a `UVCAnimNotify_ActionHit` at 40 % of the montage (`HIT_FRACTION`): 0.67 s into the clip.
- **The graph.** Attacks play in the default slot of `ABP_VCPlayerLocomotion`: over the upper body on `ActionLegsWeight`
  while the character moves, over the whole body standing. Nothing ends a montage early.
- **Root motion is on.** The attack clips, unlike the locomotion clips L0 read, have `bEnableRootMotion` set, and the anim
  instance takes root motion from montages: in PIE a standing `Attack_1` carried the capsule 81 uu forward (the clip's
  root: 83). The graph's zeroed root does not lose the lunge; the montage takes it first.

Three findings from that look (§6 has the rest):
- **G178, the hit lands late.** The blade passes (the right hand's fastest frame) at 24-29 % of each clip; the notify at
  40 % lands about 0.2 s after it at rate 1 (0.1 s at the Longsword's 1.83), when the sword is already still.
- **G179, the combo skips.** In standalone PIE (and on a listen server's own character) the owner and the server share
  one montage component, so `ChooseAttack` runs twice per swing: two presses played `Attack_1` then `Attack_3`. With a
  dedicated server each side keeps its own index on its own clock, so they can disagree (the owner sees one clip, the
  others another).
- **G180, an attack stops a run.** Root motion takes the capsule's velocity for the whole montage: running at 400 uu/s,
  a swing dropped the speed to 240 at 0.33 s and to 7 by the montage's end, where the run starts again from a stand.
  The upper body swings over running legs (`ActionLegsWeight`) while the capsule stops under them.

## 2. The pack's demo

`AnimBP_EssentialSwordShield_UE4` loads and compiles under 5.8 (its skeleton the pack's UE4 mannequin; the UE5 child and
`BP_ThirdPersonCharacter` load too). It was read through a T3D export of the asset, nothing edited
(`Saved/QA/attack_flow/`). Its attacks are states of a state machine, not montages: `SSH` › `Normal` › `Attack` holds
the sub-machine `ComboState SSH`.

| From | To | When | Blend |
|---|---|---|---|
| Idle/Run | Attack (its entry, `State`) | `isAttack` | 0 |
| `State` | Charge Begin (`Attack_Charge_Begin`) | the primary press, not running, not the secondary | 0.2 |
| Charge Begin | Attack_01 (`SSH_Attack_01`) | the button released | 0.2 |
| Charge Begin, then Charge Loop | Charged Attack (`SSH_Attack_15`) | held past the loop's first second | 0.2 |
| Attack_01 | Attack_02 (`SSH_Attack_02`) / Secondary_02 (`_09`) | a press (primary / secondary) once under 70 % of the clip is left | 0.1 |
| Attack_02 | Attack_03 (`SSH_Attack_04`) / Secondary_03 (`_10`) | the same | 0.1 |
| Secondary_01 (`_08`, from `State` on the secondary) | Secondary_02 / Attack_02 | the same | 0.1 |
| Secondary_02 (`_09`) | Attack_03 (`_04`) / Secondary_03 (`_10`) | the same | 0.1 |
| Attack_03, Secondary_03 | `State` (a new combo) | a press once under 50 % is left | 0.2 |
| Charged Attack | Attack_02 | a press once under 65 % is left | 0.2 |
| any attack | EndAttackCombo, then Idle/Run | under 1 % left: `setAttackComboFalse` clears `isAttackCombo` | 0 |

- **The flags.** `isAttack` is set at `IA_Attack`'s press and cleared at its release and by `ResetAttack`, which every
  attack state sends as it begins; `isAttackInput` follows the button for the charge. So a press counts when the button
  is down as the window opens or goes down inside it: there is no buffer beyond holding. `isAttackCombo` holds the body in
  the Attack state until EndAttackCombo.
- **The rest of `Normal`:** Attack_Run (`SSH_Attack_12`) on a press while running; Attack_Air (`Air_02` begin, loop,
  landing) on a press in a jump; Attack_Plunge (`Plunge_01` begin, loop, landing) on the secondary in a jump; Dodge and
  Roll (`IA_Dodge`), which an attack can be broken into; Guard, Guard_Hit, Guard_Break; Shield_Throw (`IA_Melee`'s
  `isThrow`) from any attack past 30 %.
- **Movement does not interrupt** an attack in the demo: only the dodge leaves one before the clip's end. The user's
  rule is new.
- **No lunge.** Its attacks are sequences in states, so the anim instance (root motion from montages only) extracts
  none: the capsule stays and the mesh's root walks ahead within each clip.

## 3. The clips' timing

Measured from the UE5 clips (`setup_character_layers.py`'s bone chains, 30 samples a second, seconds at rate 1). The
strike is the right hand's fastest frame. The follow-through ends where the body settles after it: the first
minimum of the bones' mean speed under a quarter of the strike's. Every clip ends in the idle (within 2 cm of
`Attack_01`'s first frame).

| Clip | Length | Strike | Follow-through end | Starts the next clip | Recovery half-way | Lunge |
|---|---|---|---|---|---|---|
| `SSH_Attack_01` | 1.67 | 0.48 (29 %) | 0.68 (41 %) | `_02` and `_09` begin in its pose at 0.65 (10 cm) | 1.26 | 83 uu, done by 0.51 |
| `SSH_Attack_02` | 1.67 | 0.41 (24 %) | 0.65 (39 %) | `_03` and `_04` at 0.65 (2 cm), `_10` (8 cm) | 1.22 | 123 by 0.41 |
| `SSH_Attack_04` | 1.67 | 0.41 (24 %) | 0.58 (35 %) | none: it recovers to the idle | 1.12 | 101 by 0.44 |
| `SSH_Attack_08` | 2.00 | 0.47 | 0.73 (37 %) | none | 1.43 | 232 by 0.43 |
| `SSH_Attack_09` | 1.67 | 0.41 | 0.68 (41 %) | `_10` at 0.61 (1 cm), `_04` at 0.68 (8 cm) | 1.19 | 123 by 0.41 |
| `SSH_Attack_10` | 1.67 | 0.41 | 0.88 (53 %) | none | 1.26 | 101 by 0.41 |
| `SSH_Attack_12` (run) | 2.50 | 0.77 | 1.03 (41 %) | none | 1.97 | 452 by 0.73 |
| `SSH_Attack_15` (charged) | 3.00 | 0.80 | 1.27 (42 %) | `_02` nearest at 1.20 (31 cm) | 2.33 | 137 by 0.83 |

The pack is authored to chain: `Attack_02` begins in `Attack_01`'s follow-through pose, `Attack_04` (and `_03`) in
`Attack_02`'s. The follow-through end is the combo's branch point, the demo's "70 % left" (0.5 s) approximated it, and
every lunge is over before it, so the recovery holds the feet still. The frames:
`Saved/QA/attack_flow/ssh_combo_frames.png` (start, strike, follow-through end, recovery half-way, end of `_01`, `_02`
and `_04`, on the low-poly body in PIE).

## 4. Decisions

**D102, the branch point.** Each attack montage carries a `UVCAnimNotify_ActionRecovery` ("VC Action Recovery") at its
follow-through end, placed by the script from the clip by the rule in §3 (the measured times above), as the hit and the
hand-off are; `UVCActionAnimSet::GetRecoveryTime` reads it from the asset (a dedicated server plays no montage). Without
one, the montage's end: nothing changes for a set without markers. Recommended over a montage section: one notify is how
the hit and the hand-off are read already, and the next attack is a montage of its own, not a section to jump to.

**D103, the interrupt.** Past the recovery time, an attack montage that is still playing is stopped with a blend-out
(0.2 s) when the character wants to move; standing, the recovery plays out. "Wants to move" is the input's acceleration
on the owner and on the server (the server has it from the owner's moves), and the speed (over 50 uu/s) on a simulated
proxy, which has no acceleration and sees the capsule move a replication later. It is one predicate on the anim set and
the component (`UVCActionMontageComponent::ShouldBreakRecovery`), checked each tick while an attack plays, the same on
every machine (D-CR16). It is cosmetic on a simulated proxy; on the owner and the server it also frees the capsule
(D106). A dig, a chop and a hit react keep their own end (no marker, no change). Recommended: the montage's blend-out,
which works in both graphs; an inertialized blend needs an Inertialization node after the default slot in each graph
(a graph change, a later polish if the 0.2 s blend shows).

**D104, the combo and its buffer.** The next attack follows from the current one's recovery time to its end plus 0.2 s,
replacing `ComboWindow` after the end. A press from the strike on is held (one press) and fires at the recovery time; a
press in the recovery fires at once. Each next clip starts from its first frame with a 0.1 s blend, as the demo's; at
the recovery time that is the pose it was authored from. The owner picks the index and sends it with the request; the
server takes it when the shared predicate allows it (the server's last swing of that combo, its recovery time, a
latency allowance of 0.2 s), else picks its own and the owner's clip plays on (cosmetic). This fixes G179: a
machine that is both owner and server picks once. The alternative, the demo's (a press counts only while held as the
window opens), loses taps.

**D105, the sequence.** The SwordShield set's `AttackCombo` becomes the demo's: `SSH_Attack_01`, `_02`, `_04`; after
`_04`, a press starts `_01` again with a 0.2 s blend (its recovery and the idle are near). `_03` leaves the combo. The
Chop keeps `_04` in a montage of its own. Later phases, not built now (§5): the secondary branch (`_08`, `_09`, `_10`;
the right mouse button is the block, D94, so it needs an input), the charge (`Charge_Begin`, `_Loop`, `_15`: the attack
would start at the release instead of the press), the run attack (`_12` while sprinting), the air and plunge attacks,
the dodge and the roll (a movement ability of their own), and the shield throw (no thrown shield in the game: dropped
unless asked). The guard's hit and break exist (D94).

**D106, the lunge.** Recommended: **standing, the lunge stays; moving, it goes.** Standing, the montage's root motion
carries the capsule as the clip steps (83-123 uu per swing, done before the recovery). While the character moves, the
anim instance ignores root motion (`ActionLegsWeight`'s condition, on the same predicate as D103), so the legs and the
capsule stay the locomotion's and the run no longer stops under a swing (G180). On the network a root motion montage
must play on the server too; today a dedicated server plays none. Phase A's first step measures it with a dedicated server
in PIE: if the owner's lunge is corrected, the server plays the attack montages (not drawn, for their root motion
only, from the ability's start); if that still corrects visibly (the server starts later than the owner by the request's
trip), the lunge becomes a root motion source the server's ability applies from the clip's measured distance and time
(§6, the one larger change). Alternatives: no lunge (the clips' root motion off: the feet slide 1 m a combo), or the
lunge on the move too (the capsule leaps out of the run).

**D107, the speed.** The clips play at the weapon's `AttackSpeed` as a rate (1 = as authored, clamped 0.5-2): the
Longsword's 1.1 plays them 10 % fast, the combo's cadence the recovery time over the rate (0.59 s). The rate limit on
both sides becomes that time. Today's rule (the whole clip into one interval) played the Longsword at 1.83 and made the
recovery the cadence.

## 5. Phases

**Phase A, built in one pass on approval** (`claude/sword-attack-flow` from `dev` in VoxelCharacterPlugin and the
parent):
1. **Measure** the standing lunge with a dedicated server and a client in PIE (does the owner correct?): it picks
   D106's network form before code.
2. **VoxelCharacterPlugin.** `UVCAnimNotify_ActionRecovery`, `GetRecoveryTime`; the interrupt predicate and its tick in
   the montage component; the root motion switch on the move; the buffer and the combo index with the request (a
   Server RPC carrying the index, validated as D104 says, then the ability); the rate (D107); the hit from the marker
   (G178). Tests: the predicate (standing plays out, moving past the marker breaks, before it does not), the buffer (a
   press in the swing fires at the marker, in the recovery at once, after the window starts a new combo), one pick per
   swing on a machine that is both (G179), the rate limit, a client's request.
3. **Parent.** `setup_character_layers.py`: the sequence `_01`, `_02`, `_04`; the hit at each clip's measured strike
   and the recovery marker at its follow-through end, both by the script's measure; `CHARACTER_SYSTEM.md`, the console
   guide for any `vc.*` (a `vc.Anim.AttackBreak 0` for the A/B), FINDINGS G178-G180 and D102-D107.

Done: on `CharacterTest`, standing, three presses play `_01`, `_02`, `_04` each from the last one's follow-through and
the recovery plays out; walking or running, each swing breaks to the locomotion at its follow-through and the speed
holds; the hit at the strike. Frames and the speed trace as evidence. The gate from `Scripts/qa/README.md` (any code,
and the editor gate's replication test).

**Later phases** (each on its own when wanted): B the secondary branch on an input; C the charge; D the run attack; E
the air and plunge attacks; F the dodge and roll.

## 6. Friction

Nothing here needs a new architecture: the attack stays a cosmetic montage the owner plays at input, the server times
the hit from the asset, and the new marker is read the same way. What the current one does not do cleanly:
- **The combo index is picked twice** (G179): the request carries no payload (`TryActivateAbilitiesByTag`). D104 adds a
  Server RPC with the index in front of the ability.
- **Root motion and a server that plays no montage.** A root motion montage is only safe when the server plays it too;
  today's design (D49: the dedicated server draws nothing) never did. If phase A's measurement shows corrections, the
  server plays the attack montages; if the late start still shows, the lunge moves to a root motion source applied by a
  predicted ability: the ability's net policy changes, the largest change here, and only if needed.
- **The rate and the cadence.** `AttackSpeed` fitted a whole clip into one interval; with the branch point the
  cadence is the clip's recovery time, so D107 changes what the number means for every melee weapon (the swords only
  today).
- **An inertialized blend-out** needs an Inertialization node downstream of the default slot; both graphs have their
  only one upstream (the state machine's). The montage blend-out avoids it.

## 7. For the user

1. D103: break the recovery when the player wants to move, by input (recommended), not only once the capsule moves?
2. D104: hold a press made during the swing until the follow-through (recommended), or only count presses in the
   window, as the demo does?
3. D105: the combo `_01`, `_02`, `_04` as the demo, and the secondary, charge, run, air, dodge as later phases?
4. D106: the lunge standing only (recommended), none, or always?
5. D107: the weapon's attack speed as the clips' play rate (recommended)?

## 8. Results (2026-11-12)

Phase A built as section 5 says, on `claude/sword-attack-flow` in VoxelCharacterPlugin and the parent, with these
changes from the draft:
- **The measurement came after the code**, with D106's first form built (the server plays the montages with root
  motion): it needed a dedicated-server session the editor's Python cannot start, so the game module gained
  `vox.PIE.Net` (a dedicated server and clients in this process) and `vox.AttackTrace` (every character of every play
  world, each frame). With two clients and 100 ms of lag (`NetEmulation.PktLag`, `p.NetShowCorrections 1`) the standing
  lunge ends at the same place on the owner, the server and the other client, with no correction logged; the server
  starts each attack about 0.1-0.2 s after the owner and catches up by the moves. The root motion source was not needed.
- **The rule is in the movement, per move:** `UVCMovementComponent::PerformMovement` sets the anim instance's root
  motion mode from the move's input, so the owner's prediction and the server's replay agree; a simulated proxy (no
  moves) by its speed. Every montage with root motion follows it (the attacks, and any other clip that carries it).
- **The markers are measured for every set's attacks** (Unarmed, TwoHand and Tool too), not only the sword's, and the
  attack montages blend in 0.1 s and out 0.2 s.

- **Gate.** `build.ps1` 0 warnings; the headless suite 915 of 916, the one failure the new rule test's own boundary
  sample (a double at a float's time), fixed and rerun (`VoxelCharacter.Anim`, `.Combat`: 50 of 50); the editor gate
  27 of 27 (the replication test with a dedicated server among them), the parity lines identical.
- **PIE** on `CharacterTest` (`Saved/QA/attack_flow/`): standing, three presses (one kept from the swing) play
  `_01`, `_02`, `_04`, the kept one at the recovery, the lunges 81, 123 and 101 uu, the last recovery played out;
  walking, three swings hold 400 uu/s and each breaks to the walk at its recovery marker
  (`walk_attack_break.png`, at `slomo 0.1`).
- **Not done:** phases B-F; an inertialized blend-out (the montage's 0.2 s blend reads well); the Chop keeps its hit at
  45 % of `_04` (0.75 s, its strike at 0.41 s: the chop is not an attack of the combo, left as it was).
