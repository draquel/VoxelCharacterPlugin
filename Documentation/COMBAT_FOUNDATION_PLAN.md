# Combat Foundation — Plan

Status: APPROVED 2026-10-08 (decisions in §14 resolved). First feature of the gameplay roadmap below.

## 0. Agreed gameplay roadmap (2026-10-08)

| # | Feature | Notes |
|---|---------|-------|
| 1 | **Combat foundation** (this plan) | Attribute ownership for non-player pawns, damage pipeline, death, dummy target, minimal melee. No AI, no navigation. |
| 2 | Inventory + equipment loop end to end | Pickup → inventory → equip → stats. Weapons tested against the dummy. Settles where player items live across death. |
| 3 | Dungeon loot and keys | Treasure rooms → POI loot chest; crates/barrels as containers; keys unlock `Locked` doors; boss rooms locked. |
| 4 | Objectives and progression | Boss-room goal, reward on clear, cleared flag in the per-dungeon record, minimap markers. Boss clear = death event from this pipeline. |
| 5 | Hazards and dungeon enemies | Traps on the floor-decor path, patrols on the POI structure path, navmesh **inside dungeon volumes only**. |
| 6 | Clock and day/night cycle | Surface time-of-day driving the sun. Prerequisite for darkness mattering. |
| 7 | Light as a mechanic | Torch/lantern item with fuel, unlit hallways, torch state per dungeon. |
| 8 | Surface gameplay | Campsites as rest/craft points, voxel resource gathering, crafting recipes. No NPCs. |
| 9 | Persistence | Save system over the storage interface, dungeon records, voxel edits. |
| 10 | Surface NPCs + voxel navigation | Separate epic: needs a runtime navmesh from voxel geometry or a voxel pathfinder. |

## 1. Goal and scope

Give every later feature a real, tested damage path and a non-player pawn that can own health, so items 2–5 plan against concrete types instead of guesses.

**In scope**
- Contracts in CommonGameFramework: damageable interface, damage context struct, faction / damage-type / state / event tags, hostility rule.
- Non-player pawns own their ability system component; players keep it on the player state (unchanged, firm decision).
- Damage pipeline: gameplay effect with an execution calculation → existing `IncomingDamage` meta attribute → `Health`.
- Death: `State.Dead` tag, `OnDied` delegate, gameplay event. Player respawn through a game mode base class. NPC despawn after a delay.
- A combat component shared by player and NPC pawns (resolves the ASC wherever it lives).
- `UVCCombatAttributeSet` with `AttackPower` and `Defense` (the "future" set named in this plugin's instructions).
- Minimal melee attack ability (unarmed or main-hand weapon damage) wired into the primary-action input chain as its first step.
- Fix: equipment GAS integration resolves the ASC with `FindComponentByClass` on the pawn, which is null for players (ASC on player state). Abilities and passive effects from equipment are currently never granted to the player.
- Debug commands: `vox.SpawnDummy`, `vox.Damage`.
- Automation tests for the pipeline and the hostility rule.

**Out of scope** (deliberately): AI controllers, behavior/state trees, navmesh, attack animations/montages, hit reactions, projectiles, loot on death, crit (stays a weapon-fragment concern for item 2), UI health bars beyond what exists.

## 2. What the survey found

| Area | State today |
|------|-------------|
| Attributes | `UVCCharacterAttributeSet` (Health, MaxHealth, Stamina, MaxStamina, MoveSpeedMultiplier, MiningSpeed, InteractionRange, `IncomingDamage` meta). `PostGameplayEffectExecute` already folds `IncomingDamage` into `Health` and clamps. On zero health it only logs. |
| ASC ownership | `AVCPlayerState` owns the ASC + attribute set; `AVCCharacterBase` forwards `GetAbilitySystemComponent`. `InitAbilityActorInfo(PS, Character)` in `PossessedBy` / `OnRep_PlayerState`. |
| Death / respawn | Not wired. `HandleRespawnAttributeReset`, `RespawnResetEffect`, `DeathCleanseTags` exist on the player state and nothing calls them. No `RestartPlayer` anywhere. Demo game mode is the ThirdPerson template BP (`Scripts/setup_gamemode.py`). |
| Abilities / effects | Zero C++ `UGameplayAbility` / `UGameplayEffect` / execution classes in the project. `DefaultAbilities` on the player state is content-configured. Content is gitignored (except `Content/Art`), so anything that must be in source is a C++ class. |
| Non-player pawns | None. No pawn can own health. |
| Equipment GAS | `EquipmentGASIntegration` factory callbacks resolve the ASC via `Manager->GetOwner()->FindComponentByClass<UAbilitySystemComponent>()` → **null for the player** (ASC is on the player state). Equipment abilities/passives silently never apply. The character comment "EquipmentGASIntegration handles grant/revoke automatically" is currently false. |
| Weapon data | `UItemFragment_Weapon` { BaseDamage, AttackSpeed, DamageType tag, CritChance, CritMultiplier } exists and is unused. |
| Input chain | `Input_PrimaryAction`: main hand occupied → voxel dig; else unarmed dig. The documented step 1 ("GAS ability activation") is not implemented. |
| Tags | Native tags in `CGFGameplayTags.h` cover Item/Inventory/Equipment/Interaction only. No tag ini in `Config/`. |
| Navigation | No navmesh integration anywhere (VoxelWorlds, dungeon, character). |
| Tests | `Private/Tests/Test_*.cpp` with `IMPLEMENT_SIMPLE_AUTOMATION_TEST` in Equipment and ItemInventory plugins. VoxelCharacterPlugin has no tests yet. |
| Repos | CommonGameFramework, EquipmentPlugin, VoxelCharacterPlugin are **git submodules** (own PRs, then a parent bump PR). |

## 3. Decisions

- **D1 — ASC ownership.** Players: player state (unchanged). Non-player pawns: the pawn owns ASC + attribute sets. Everything that needs an ASC resolves it through `UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor)` (honours `IAbilitySystemInterface`), never `FindComponentByClass`.
- **D2 — Contracts in CommonGameFramework, logic in VoxelCharacterPlugin.** CGF gets the interface, struct, tags and a pure tag-rule helper (no gameplay logic, per its rules). The attribute sets, execution calc, effect, ability, component, NPC pawn and game mode go in VoxelCharacterPlugin, next to the attribute set that already lives there. Extraction into a voxel-independent `CombatPlugin` later is mechanical if ever wanted; not now.
- **D3 — Component implements the interface.** Same precedent as `IInteractable` on `UInteractableComponent`: `UVCCombatComponent` implements `ICGFDamageableInterface`; `UCGFCombatStatics::FindDamageable(Actor)` checks the actor, then its components. Pawns do not implement the interface themselves.
- **D4 — Source-defined GAS classes.** The damage effect, execution and melee ability are C++ classes configured in constructors (no `.uasset` needed). The player state gets a C++-defaulted `MeleeAttackAbilityClass` so no Blueprint edit is required.
- **D5 — Server authority.** `ApplyDamage` rejects on non-authority. The melee ability is `ServerOnly` net policy: client requests activation, server traces and applies. Death state replicates (`bIsDead` + replicated loose tag).
- **D6 — Hostility rule v1.** `AreHostile(A, B)`: false if either has no faction or either is `Faction.Neutral`; false if same faction; else true. One function in CGF so a faction-relationship table can replace it later without touching callers. `ApplyDamage` rejects friendly fire unless the context sets `bIgnoreFaction` (traps/hazards).
- **D7 — Death detection lives in the component**, from the Health attribute-change delegate on authority, not inside the attribute set (keeps the set free of actor logic; the set keeps clamping and the meta-attribute fold).

## 4. Ownership map

| Piece | Plugin / module | File |
|-------|-----------------|------|
| `ICGFDamageableInterface` | CommonGameFramework | `Public/Interfaces/CGFDamageableInterface.h` |
| `FCGFDamageContext`, `ECGFDamageResult` | CommonGameFramework | `Public/Types/CGFCombatTypes.h` |
| Faction / Damage / State / Event / Ability / SetByCaller tags | CommonGameFramework | `Tags/CGFGameplayTags.h/.cpp` |
| `UCGFCombatStatics` (FindDamageable, GetFactionTag, AreHostile) | CommonGameFramework | `Public/Utilities/CGFCombatStatics.h` |
| `UVCCombatAttributeSet` (AttackPower, Defense) | VoxelCharacterPlugin | `Public/Combat/VCCombatAttributeSet.h` |
| `UVCDamageExecution` | VoxelCharacterPlugin | `Public/Combat/VCDamageExecution.h` |
| `UVCDamageEffect` | VoxelCharacterPlugin | `Public/Combat/VCDamageEffect.h` |
| `UVCCombatComponent` | VoxelCharacterPlugin | `Public/Combat/VCCombatComponent.h` |
| `UVCMeleeAttackAbility` | VoxelCharacterPlugin | `Public/Combat/VCMeleeAttackAbility.h` |
| `UVCCombatStatics` (ApplyDamageToActor) | VoxelCharacterPlugin | `Public/Combat/VCCombatStatics.h` |
| `AVCNPCCharacterBase` | VoxelCharacterPlugin | `Public/Core/VCNPCCharacterBase.h` |
| `AVCGameModeBase` (respawn) | VoxelCharacterPlugin | `Public/Core/VCGameModeBase.h` |
| Player death/respawn hooks | VoxelCharacterPlugin | `VCCharacterBase`, `VCPlayerState` |
| Input chain step 1 | VoxelCharacterPlugin | `VCCharacterBase::Input_PrimaryAction` |
| ASC resolution fix | EquipmentPlugin / EquipmentGASIntegration | `Private/EquipmentGASIntegration.cpp` |
| `vox.SpawnDummy`, `vox.Damage` | Game module | `Source/VoxelEngine/*/Debug/VoxelDebugSubsystem` |
| Tests | VoxelCharacterPlugin, EquipmentPlugin | `Private/Tests/Test_CombatPipeline.cpp`, `Test_EquipmentGASResolution.cpp` |

## 5. CommonGameFramework contracts

### 5.1 Tags (native, `CGFGameplayTags`)

```
Faction.Player
Faction.Monster
Faction.Neutral
Damage.Type.Physical          (mitigated by Defense)
Damage.Type.Fire
Damage.Type.Poison
Damage.Type.Pure              (ignores Defense)
State.Dead
State.Downed
State.Invulnerable
Event.Combat.Damaged          (gameplay event payload = FCGFDamageContext)
Event.Combat.Died
Event.Combat.Downed
Ability.Attack.Melee
SetByCaller.Damage
```

### 5.2 Types (`CGFCombatTypes.h`)

```cpp
USTRUCT(BlueprintType)
struct FCGFDamageContext
{
    TWeakObjectPtr<AActor> Instigator;      // who is responsible (pawn); may be null for hazards
    TWeakObjectPtr<AActor> Causer;          // weapon / trap / projectile actor; may equal Instigator
    FGameplayTag  DamageType;               // Damage.Type.*; defaults to Physical when unset
    float         BaseDamage = 0.f;         // before AttackPower / Defense
    FVector       HitLocation = FVector::ZeroVector;
    FVector       HitNormal   = FVector::ZeroVector;
    FGuid         SourceItemInstanceId;     // the FItemInstance that caused it, if any
    bool          bIgnoreFaction = false;   // hazards hurt everyone
    FGameplayTagContainer ContextTags;      // free-form (e.g. Damage.Source.Trap) for later features
};

UENUM(BlueprintType)
enum class ECGFDamageResult : uint8
{ Applied, Rejected_NoTarget, Rejected_NotAuthority, Rejected_Dead, Rejected_Invulnerable, Rejected_Friendly, Rejected_NoAbilitySystem };
```

### 5.3 Interface (`CGFDamageableInterface.h`, BlueprintNativeEvent)

```cpp
FGameplayTag GetFactionTag() const;
bool IsDead() const;
bool CanReceiveDamage(const FCGFDamageContext& Context) const;   // default true; lets a target veto
```

### 5.4 Statics (`UCGFCombatStatics`, BlueprintFunctionLibrary, pure tag rules only)

- `FindDamageable(AActor*) → TScriptInterface<ICGFDamageableInterface>`: actor first, then components.
- `GetFactionTag(AActor*)`: via FindDamageable; empty tag if none.
- `AreHostile(AActor*, AActor*)`: rule D6.

## 6. VoxelCharacterPlugin pieces

### 6.1 `UVCCombatAttributeSet`
`AttackPower` (default 0), `Defense` (default 0). Replicated, clamped ≥ 0. Added to both the player state and the NPC pawn beside `UVCCharacterAttributeSet`. Equipment passive effects (item 2) modify these.

### 6.2 `UVCDamageExecution` (`UGameplayEffectExecutionCalculation`)
Captures source `AttackPower` and target `Defense`. Reads `SetByCaller.Damage`. If the target's ASC has `State.Invulnerable` → output nothing. For `Damage.Type.Pure` → `Base`; otherwise `max(0, Base + AttackPower − Defense)`. Outputs one additive modifier on `IncomingDamage`. The existing `PostGameplayEffectExecute` then folds it into `Health`.

### 6.3 `UVCDamageEffect` (`UGameplayEffect`, C++ ctor)
Instant, one execution `UVCDamageExecution`. The damage type tag is carried in the effect context / asset tags on the spec so the execution can read it.

### 6.4 `UVCCombatComponent` (implements `ICGFDamageableInterface`)
- Properties: `FactionTag` (EditAnywhere, replicated `COND_InitialOnly`), `bIsDead` (replicated, `OnRep_IsDead`).
- `InitializeWithAbilitySystem(ASC)`: called by the owner once its ASC is bound (player: `PossessedBy` / `OnRep_PlayerState`; NPC: `BeginPlay`). Binds the Health attribute-change delegate.
- `ApplyDamage(const FCGFDamageContext&) → ECGFDamageResult` (authority only): checks dead / `CanReceiveDamage` / faction (D6) / ASC present → builds a `UVCDamageEffect` spec with `SetByCaller.Damage = BaseDamage`, effect context with instigator + causer + hit result, applies to the target ASC → sends `Event.Combat.Damaged`.
- Death on authority when Health hits 0 and not already dead: set `bIsDead`, `AddReplicatedLooseGameplayTag(State.Dead)`, broadcast `OnDied(Context)`, send `Event.Combat.Died`. `OnRep_IsDead` broadcasts `OnDied` on clients (context empty).
- Delegates (BlueprintAssignable): `OnHealthChanged(float New, float Old, float Max)`, `OnDied(const FCGFDamageContext&)`.
- `Revive()` (authority): clears `bIsDead` and the tag. Used by the player respawn path.

### 6.5 `UVCCombatStatics::ApplyDamageToActor(Target, Context)`
Finds the combat component and forwards; `Rejected_NoTarget` otherwise. Blueprint-callable entry point for traps, abilities, debug commands.

### 6.6 `UVCMeleeAttackAbility`
- Tags: `Ability.Attack.Melee`. Net policy `ServerOnly`. Blocked while the owner has `State.Dead`.
- Properties: `MeleeRange` 250, `UnarmedDamage` 5, `UnarmedAttackSpeed` 1.
- Activate (server): sphere-sweep from the avatar's eyes along its control rotation (`GetActorEyesViewPoint`) for `MeleeRange` on **ECC_Pawn** (the engine's Pawn / CharacterMesh profiles ignore Visibility, so a visibility trace passes through every character; terrain still blocks Pawn); target = `FindDamageable(Hit.Actor)`; require `AreHostile(Avatar, Target)`; damage = main-hand `UItemFragment_Weapon.BaseDamage` + its `DamageType` if present (guarded by `WITH_INVENTORY_PLUGIN` / `WITH_EQUIPMENT_PLUGIN`), else unarmed; build `FCGFDamageContext` (SourceItemInstanceId from the equipped item) → `ApplyDamageToActor`. Rate-limit by `AttackSpeed` with a last-activation timestamp. End ability.
- Granted natively: `AVCPlayerState::MeleeAttackAbilityClass` (C++ default = this class) appended in `GrantDefaultAbilities`. NPCs get nothing by default (item 5 grants their abilities).

### 6.7 Input chain step 1 (`AVCCharacterBase::Input_PrimaryAction`)
Before the equipment / dig branches: eye-point trace on ECC_Pawn for `AttackTargetRange` (same view and channel as the server sweep; the third-person camera sits metres behind the pawn and lags the control rotation); if the hit actor resolves to a damageable that is hostile → `ASC->TryActivateAbilitiesByTag(Ability.Attack.Melee)` and return. Otherwise fall through to the existing behaviour unchanged. This is the documented "GAS ability → equipped item → fallback" order with the first step now real.

### 6.8 Player death and respawn
- `AVCCharacterBase` binds `CombatComponent->OnDied` → `HandleDied`: authority disables movement + input, capsule stops blocking pawns, mesh ragdolls if it has a physics asset (else nothing), then notifies the game mode via `AVCGameModeBase::HandlePlayerDied(Controller, DeathTransform)`.
- `AVCGameModeBase : AGameModeBase` (new, intended parent for the demo GM): `RespawnDelay` (5 s), `RespawnPolicy` { AtDeathLocation (default), AtPlayerStart }. After the delay: destroy the old pawn, `RestartPlayerAtTransform` / `RestartPlayer`. The new pawn's terrain-ready spawn handles placement.
- `PossessedBy` on the new pawn (existing ASC rebind) additionally: if the player state has `State.Dead` → `CombatComponent->Revive()`, then `PS->HandleRespawnAttributeReset()` (now actually called; `RespawnResetEffect` may be unset → fall back to setting Health = MaxHealth, Stamina = MaxStamina directly on authority).
- Demo: `Scripts/setup_gamemode.py` reparents / points the demo GM at `AVCGameModeBase` (local content, not in git).

### 6.9 `AVCNPCCharacterBase`
`ACharacter` + `IAbilitySystemInterface`. Default subobjects: ASC (replication mode Minimal), `UVCCharacterAttributeSet`, `UVCCombatAttributeSet`, `UVCCombatComponent` (faction default `Faction.Monster`). `StartingMaxHealth` applied on authority in `BeginPlay` via the attribute set init, then `InitAbilityActorInfo(this, this)` + `CombatComponent->InitializeWithAbilitySystem`. `DefaultAbilities` array (granted on authority). On death: stop movement, capsule no-collide, ragdoll if possible, `DestroyAfterDeathDelay` (10 s, 0 = keep). `OnNPCDied` is just the component's `OnDied`; items 3–5 bind to it for loot / objectives. No AI controller, no voxel terrain wait (dungeon enemies stand on static tiles; surface NPCs are epic 10).

## 7. EquipmentPlugin fix
`EquipmentGASIntegration.cpp` equip/unequip callbacks: replace `FindComponentByClass<UAbilitySystemComponent>()` with `UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Manager->GetOwner())`. Add a test with an owner implementing `IAbilitySystemInterface` whose ASC lives on another actor. Note for item 2: the equipment manager lives on the pawn, so it is destroyed on death; nothing to re-grant on respawn until item 2 decides where player items live.

## 8. Game module debug commands (`UVoxelDebugSubsystem`)
- `vox.SpawnDummy [maxHealth=100] [faction=Monster]`: spawns `AVCNPCCharacterBase` 300 uu in front of the PIE pawn on the terrain, with a visible engine cylinder mesh attached, logs the actor name.
- `vox.Attack`: calls `AVCCharacterBase::TryStartMeleeAttack` (same path as the primary action) so the melee loop can be driven from the console.
- `vox.Damage <amount> [type=Physical]`: applies damage to the PIE player pawn through the pipeline (tests player death → respawn).

## 9. Data flow

```
LMB  ──► Input_PrimaryAction: trace hits hostile damageable?
          yes ──► ASC->TryActivateAbilitiesByTag(Ability.Attack.Melee)   [client → server RPC]
                  server: UVCMeleeAttackAbility::ActivateAbility
                    trace → FindDamageable → AreHostile → FCGFDamageContext
                    → UVCCombatStatics::ApplyDamageToActor
                    → UVCCombatComponent::ApplyDamage (authority, faction/dead/invuln checks)
                    → UVCDamageEffect spec (SetByCaller.Damage) → target ASC
                    → UVCDamageExecution (AttackPower, Defense, type) → +IncomingDamage
                    → UVCCharacterAttributeSet::PostGameplayEffectExecute → Health −= dmg
                    → Health change delegate → UVCCombatComponent
                        Health > 0: OnHealthChanged
                        Health = 0: bIsDead, State.Dead, OnDied, Event.Combat.Died
                            player: AVCCharacterBase::HandleDied → AVCGameModeBase respawn
                            NPC:    AVCNPCCharacterBase::HandleDied → despawn timer
          no  ──► existing equipment / dig path
```

## 10. Replication
- Attributes: via ASC (player state Mixed as today; NPC Minimal).
- `bIsDead`, `FactionTag`: replicated on the component; `OnRep_IsDead` drives client death visuals.
- `State.Dead` as a replicated loose tag so client-side ability checks see it.
- Damage, death, respawn: authority only. Clients never call `ApplyDamage`; the enum result tells them why if they do.

## 11. Tests (`VoxelCharacter.Combat.*`, flags `ApplicationContextMask | ProductFilter`, run in-editor via unreal-mcp `AutomationTestToolset`, headless-safe)
1. Hostility: Player↔Monster true; same faction false; Neutral never; no faction never.
2. Damage math: Health drops by `Base + AttackPower − Defense`, floored at 0.
3. Pure damage ignores Defense.
4. `State.Invulnerable` → `Rejected_Invulnerable`, Health unchanged.
5. Lethal hit → `IsDead`, `State.Dead` present, `OnDied` exactly once, next hit `Rejected_Dead`.
6. Friendly fire → `Rejected_Friendly`; same context with `bIgnoreFaction` → `Applied`.
7. Non-authority `ApplyDamage` → `Rejected_NotAuthority` (client-role test actor).
8. Equipment ASC resolution test (EquipmentPlugin).

## 12. PR plan (user merges each)

| PR | Repo | Content | Depends on |
|----|------|---------|-----------|
| A | CommonGameFramework | §5 contracts + tags + statics; `COMMON_TYPES.md` and `.claude/instructions.md` updated | — |
| B | EquipmentPlugin | §7 ASC-resolution fix + test | — (bumped together with A) |
| C | VoxelCharacterPlugin | §6 everything, tests §11, `.claude/instructions.md` death contract updated to what is real, `Combat/` folder added to the file org | A |
| D | Parent (UE5_PluginDev) | Submodule bumps A+B+C, §8 debug commands, `Scripts/setup_gamemode.py` GM parent, CLAUDE.md `vox.*` table rows | A, B, C |

Verification per PR: Rider `get_file_problems` on edited files → close editor → `Build.bat VoxelEngineEditor Win64 Development` from `D:\Program Files\Epic Games\UE_5.8` → relaunch → unreal-mcp `AutomationTestToolset` (`VoxelCharacter.Combat.*`, `Equipment.*`) → PIE in `/Game/PluginTesting/Demo/VoxelDemo`: `vox.SpawnDummy`, attack it until it dies, `vox.Damage 1000` and watch the respawn, read `LogVoxelCharacter` via `LogsToolset`.

## 13. How later features plug in
- **Item 2 (equipment loop):** weapon damage/type already read from `UItemFragment_Weapon`; add crit there. Armor = passive effects on `Defense`. Decides pawn- vs player-state-owned inventory (affects "keep items on death").
- **Item 3 (loot/keys):** no dependency.
- **Item 4 (objectives):** boss = `AVCNPCCharacterBase` subclass; "cleared" listens to its `OnDied`.
- **Item 5 (hazards/enemies):** traps call `ApplyDamageToActor` with `bIgnoreFaction`; enemies subclass the NPC base, add an AI controller + abilities; navmesh over dungeon tiles.
- **Item 7 (light):** darkness penalties can be gameplay effects on the same ASC.
- **Epic 10 (surface NPCs):** same pawn base; only movement/navigation is new.

## 14. Decisions taken with the user (2026-10-08)
1. **Respawn location:** at the death location by default. A POI or designated respawn point will replace it later, so the game mode exposes `RespawnPolicy` and a `ChooseRespawnTransform` hook rather than hard-coding either.
2. **Items on death:** respawn empty for this PR; ownership of inventory/equipment across death is settled in item 2.
3. **Home for combat classes:** VoxelCharacterPlugin, next to the attribute set.
4. **Knock-out state (user request, designed in now, not fully built):** out-of-health must not mean dead unconditionally. The component routes zero health through `HandleOutOfHealth`, which consults `OutOfHealthPolicy` { `Die` (default), `Downed` }. `Downed` sets `State.Downed` (new tag) instead of `State.Dead`, broadcasts `OnDowned`, and leaves the pawn recoverable: `Revive(float HealthFraction)` clears the state, and `DownedTimeout` (0 = never) promotes to death. This PR ships the policy, tags, `OnDowned`, `Revive` and the timeout; the gameplay that recovers a downed player (ally interaction, consumable) comes with later features. `State.Downed` blocks the melee ability like `State.Dead` does.
