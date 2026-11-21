# Bow Reload Plan: an arrow taken from the quiver and nocked before each draw

> **Frozen 2026-11-12.** This plan is history: agreed the same day as drafted ("Yes, build it": D98-D101 each as
> recommended), built in one pass (section 5). As it is now: `CHARACTER_SYSTEM.md` ("The bow" › The reload). Owner as
> planned: VoxelCharacterPlugin; the clips' markers and the fits in the parent's `setup_character_layers.py`. Numbers:
> D98-D101; the build found no finding.

## 1. What exists

Read 2026-11-12 from dev 500b67b.

- **The shot** (`UVCRangedAttackAbility`, ranged combat plan B3, D56, D83). The primary action held draws, its release
  shoots. The server times the draw. At the release it takes one arrow from the bag (`FindAmmo`, by the ranged weapon's
  `AmmoTag`, `Item.Ammo.Arrow`) and spawns it along the reticle's line. A draw needs an arrow in the bag (`CanStartDraw`,
  the owner's check and the server's, D-CR16). Aiming (the secondary action) raises the bow first (D76).
- **The clips.** The Bow set plays `Draw` (raise, string, loop) and `Release` ("the loose, the hand back to the bow,
  ready to nock", 1 s) in the layer's `UpperBody` slot, so the legs keep walking (`setup_character_layers.py`,
  `UPPER_BODY_ACTIONS`). The pack's six reloads are unused: `UE5_Reload_Back`, `_Hip`, `_Side` and their crouched
  versions. `Reload_Back` (1 s) reaches over the right shoulder: the wrist is behind the shoulder at 0.125 s (the grab)
  and at the bow at about 0.4 s (the nock).
- **No arrow is seen** until the shot: nothing is in the right hand while drawing.
- **The quiver** (D97) is worn on the back, its opening where `Reload_Back`'s hand reaches. It holds nothing: the arrows
  are in the bag. No NPC uses a bow.

## 2. Decisions

**D98, when a reload happens.** The bow is either nocked or empty: a replicated state on the character. Recommended:
**the archer reloads on their own**, after every shot (once the release's loose has played) and when the bow comes into
the hand, if an arrow is left. A draw needs a nocked arrow. A press during the reload draws as soon as the arrow is
nocked, if the button is still held. The alternative, a reload as the first part of every draw, needs no state but makes
every shot start a second late, and the archer stands with an empty string after each shot.

**D99, where the arrow comes from.** The arrows stay in the bag and are spent at the shot, as now; the reload is the
animation and the state. The clip follows where the arrows are carried:
- (a) **recommended:** with a quiver, `Reload_Back`; without one, `Reload_Hip` (from the bag at the hip), so a bow
  without a quiver still shoots;
- (b) no quiver, no reload: the quiver becomes part of the bow, and a bow without one cannot shoot.

**D100, the arrow seen.** An arrow mesh (the pack's `SM_Accessories_Arrow_01`, the projectile's) appears in the right
hand at the reload's grab, rides to the string at the nock, stays through the draw, and goes at the loose, where the
projectile takes its place. Its fit in the hand is solved from the clip, as the sword's was. It is cosmetic and runs
on every machine, from the replicated state and the montage's time (a hand-off marker in the clip, as the sheaths'
`UVCAnimNotify_ItemHandOff`). Recommended too: a few arrows standing in the quiver while the bag holds any (a cosmetic
cluster, not one per arrow).

**D101, timing and interruption.** The reload plays in the `UpperBody` slot with the draw and the release: walking and
crouching go on (crouched: the pack's `Reload_Crouch_*`). The server sets "nocked" at the nock time, read from the clip
(the D82 pattern: the owner plays its montage at once, the server plays it to the others and acts at the marker). A
reload broken off (the bow sheathed or swapped, a hit reaction, a stagger, death) leaves the bow empty; it starts again
when the bow is next in the hand and free. Sheathing a nocked bow keeps nothing: the next draw reloads.

## 3. Build and done

One pass, on `claude/bow-reload` from `dev` in VoxelCharacterPlugin and the parent:

1. **VoxelCharacterPlugin.**
   - `EVCAnimAction::Reload` with the set's montages per carry place (Back, Hip; crouched).
   - The nocked state and the reload's sequence on `AVCCharacterBase` (owner and server, as the sheath's).
   - `UVCRangedAttackAbility::CanStartDraw` needs a nocked arrow; the shot clears it.
   - The arrow's visual on the hand. Tests for the state (nocked after a shot; the draw refused while empty; the buffered
     draw; no arrows, no reload; a sheathe or a hit leaves it empty; the quiver picks the clip), a client's request.
   - `CHARACTER_SYSTEM.md`.
2. **Parent.**
   - `setup_character_layers.py`: the Reload clips, their markers, `Reload` among the upper-body actions.
   - `configure_ranged_items.py`: the arrow's hand fit, and the quiver's arrows if agreed.
   - FINDINGS D98-D101, the console guide, this plan frozen.

Done: on `CharacterTest`, a bow with a quiver reloads from the back after each shot, without one from the hip; the arrow
is seen from grab to loose; shots of the reload's frames. The gate from `Scripts/qa/README.md` (any code, and the
editor gate's replication test). PRs into `dev`, the board.

## 4. For the user

1. D98: the archer reloads on their own after each shot (recommended), or the reload is part of each draw?
2. D99: without a quiver, reload from the hip (recommended), or no quiver, no shot?
3. D100: show the arrow in the hand (yes), and a few arrows standing in the quiver (recommended)?

## 5. Results (2026-11-12)

Built as section 3 says, on `claude/bow-reload` in VoxelCharacterPlugin and the parent, with three changes from the
draft. The pack's skeleton carries the arrow on two bones, `arrow_hand_r` (a child of `hand_r`) and `arrow_bow_string`
(a child of the pack's `bow_string`, which ours lacks): the script reads the grab and the nock from them instead of
hand-placed markers, the arrow rides `hand_r` on the pack's own fit until the nock, and from the nock it runs from the
drawing hand toward the bow hand, so it follows the string through the draw. The arrows' mesh is replicated
(`ArrowMesh`): the bag is the owner's alone. The quiver's arrows sit by a layout on the character
(`QuiverArrowLayout`), not on the quiver item. Crouched clips were left out (the standing reload plays over crouched
legs, as the draw does).

- **Gate.** `build.ps1` 0 warnings; the headless suite and the editor gate in the parent PR;
  `VoxelCharacter.Combat.Ranged.ReloadCycle` (the quiver picks the clip, no arrow no reload, the nock before a draw, a
  shot, a hit, a sheathe, the last arrow).
- **PIE** on `CharacterTest` (`Saved/QA/reload/`): the bow unsheathed with the quiver worn, then the reload from the
  quiver, nocked 1.5 s after the press; the arrow in the raised hand at the grab, on the bow at the nock, across the bow
  at rest, from the cheek to the bow hand drawn; four arrows standing in the quiver; without the quiver the reload from
  the hip; a press held through a reload drawing at the nock.
- **Not done:** the crouched reloads; arrows in first person (no arms mesh, D57); the bow's string does not move.
