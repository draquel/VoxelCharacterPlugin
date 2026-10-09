# Inventory + Equipment Loop — Plan (roadmap feature 2)

Status: APPROVED 2026-10-09 (all five open questions answered yes; use key F); IMPLEMENTED on the feature/equipment-loop branches. Follows the combat foundation (`COMBAT_FOUNDATION_PLAN.md`, merged 2026-10-09).

## 1. Goal

A player in the demo world can pick an item up off the ground, see it in the hotbar/inventory, equip it, see
it on the character, feel its stats (damage, defense, max health), use a consumable, drop it, die and keep it.
Everything server-authoritative, nothing hard-coded to a specific item.

## 2. What the survey found (more exists than the roadmap assumed)

| Area | State today |
|------|-------------|
| Pickup | Interact input → `UInteractionComponent::TryInteract` → `AWorldItem` (pooled) → `IInventoryOwner` → `TryAddItem`. Works. |
| Drop | `AVCCharacterBase::RequestDropActiveItem` spawns a pooled world item from the active hotbar slot. Works. |
| Inventory | `UInventoryComponent` on the pawn: fast-array replication, all operations + 50 tests, local JSON storage subsystem with auto-save and `StorageOwnerId`. |
| Equipment | `UEquipmentManagerComponent` on the pawn: `TryEquipFromInventory` / `TryUnequipToInventory` server RPCs, async mesh load + socket attach (FP arms / TP body remap in the character), anim-layer link, GAS ability grant + passive / on-equip effects. Hotbar, inventory panel, equipment panel and click-to-move UI exist in `AVCPlayerController`. |
| Items | 12 test definitions under `/Game/PluginTesting/Items` (swords, pickaxes, potions, bread, materials, leather helmet/chest) with Weapon / Equipment / Durability / Consumable / Stackable fragments. **No meshes, no icons, no stat effects, no consume effects**: world items show the fallback mesh, equipping attaches nothing, armour changes nothing, potions do nothing. |
| Stats | `UItemFragment_Equipment` only carries `TSubclassOf<UGameplayEffect>` arrays. Game content is gitignored, so there are no effect assets and nothing in source defines any. |
| Consumables | `UItemFragment_Consumable` {ConsumeEffect, ConsumeAbility, bConsumeOnUse, CooldownDuration}; **no use path anywhere** (no input action, no RPC, no caller). |
| Weapons | Melee ability reads BaseDamage / AttackSpeed / DamageType. CritChance / CritMultiplier unused. Durability never degrades. |
| Death | Inventory and equipment are pawn components; the pawn is destroyed on respawn, so the player respawns empty (accepted in feature 1, to be settled here). |
| Debug | `GiveItem <asset> [count]` and `SpawnWorldItem <asset> [count]` exec commands on the player controller. |
| Art | Blender pipeline has `calibration` and `dungeon` asset categories; the policy already declares an `Items` category (`static`, `auto_box` collision) with no assets yet. |
| Docs | `Plugins/InteractionPlugin/.claude/instructions.md` is a copy of the Equipment instructions (wrong plugin). Follow-up. |
| Tests | ItemInventory 50+, Equipment 10 + 1, Interaction **none**. |

So the loop is mechanically built; what is missing is **data-driven stats, consumable use, crit, item visuals, death
persistence of items, and a demo you can see it in**.

## 3. Decisions proposed

- **D1 — Stats are data on the fragment, not effect assets.** Add `TArray<FCGFAttributeModifier>` {Attribute, Op, Magnitude}
  to `UItemFragment_Equipment` (`StatModifiers`) and `UItemFragment_Consumable` (`AttributeChanges`). A small helper
  builds a transient `UGameplayEffect` at runtime (infinite for equipment, instant for consumables) and applies it.
  The existing `PassiveEffects` / `ConsumeEffect` class arrays stay for projects that author effect assets.
- **D2 — Helper lives in CommonGameFramework utilities**, since both EquipmentPlugin and VoxelCharacterPlugin need it
  and CGF already depends on GameplayAbilities. It constructs an object and applies it; no game state, no policy.
- **D3 — Consumable use is a character-plugin integration**, like pickup and drop: new `IA_Use` input (default key `F`
  — to confirm) → `AVCCharacterBase::RequestUseActiveItem` → server RPC → validate Consumable fragment + cooldown →
  apply `AttributeChanges` / `ConsumeEffect` / `ConsumeAbility` → `TryRemoveItem(1)` when `bConsumeOnUse`.
  Cooldowns are per item definition on the pawn (map of asset id → next-ready time), server-side.
- **D4 — Items survive death by snapshot on the player state.** `AVCCharacterBase::HandleDied` (authority) copies the
  inventory slots and equipped items into `AVCPlayerState::PendingItemSnapshot` (instance fragments re-outered via
  `DuplicateObject`); the new pawn's `PossessedBy` restores inventory first, then re-equips by slot tag, then clears the
  snapshot. Same `FItemInstance` data the storage subsystem serialises, so feature 9 can persist the snapshot unchanged.
  A "drop a loot bag instead" policy is a one-enum addition later (feature 3 has the container).
- **D5 — Crit is rolled on the server inside the melee ability** (`CritChance` → `BaseDamage × CritMultiplier`, context
  tag `Damage.Critical`). Durability degrade on hit is **deferred**: equipped items are value copies in a replicated
  array and mutating their instance fragments needs subobject replication that does not exist yet. Logged as follow-up.
- **D6 — Item visuals through the Blender pipeline**, new `Tools/Blender/assets/items/` category: one low-poly prop per
  test item (wooden sword, iron sword, stone pickaxe, iron pickaxe, leather helmet, leather chest, potion ×2, bread,
  wood, stone, iron ore). Exported to `SourceArt/Exports/Items/`, imported to `/Game/Art/Items/`, assigned to the
  definitions by an extended `Scripts/create_test_items.py` (WorldDisplay mesh, Equipment mesh, icon rendered by
  Blender `render_thumbnail_to_path` → imported as texture). Equip meshes attach at the existing `hand_r` /
  helmet / chest sockets; armour pieces are simple attached meshes, not modular body swaps.
- **D7 — Minimal vitals HUD** (health + stamina bars) as a programmatic widget owned by the player controller, bound to
  `UVCCombatComponent::OnHealthChanged` and the stamina attribute. Without it the stat changes are invisible in the demo.
  Small; can be cut if you would rather keep UI out of this feature.

## 4. Ownership map

| Piece | Plugin | Files |
|-------|--------|-------|
| `FCGFAttributeModifier` + `UCGFGameplayEffectStatics::ApplyAttributeModifiers(ASC, Modifiers, bInfinite, SourceObject)` | CommonGameFramework | `Types/CGFCombatTypes.h`, `Utilities/CGFGameplayEffectStatics.h/.cpp` |
| `UItemFragment_Equipment::StatModifiers`, `UItemFragment_Consumable::AttributeChanges` | ItemInventoryPlugin | `Data/Fragments/*.h` |
| Apply / remove stat modifiers on equip / unequip (+ tests) | EquipmentPlugin / EquipmentGASIntegration | `EquipmentEffectApplier.cpp`, `Private/Tests/` |
| Consumable use (input, RPC, cooldown), crit roll, item snapshot across death, vitals HUD, `EquipHotbarItem` / `UseActiveItem` callables | VoxelCharacterPlugin | `Core/VCCharacterBase`, `Core/VCPlayerState`, `Core/VCPlayerController`, `Combat/VCMeleeAttackAbility`, `UI/VCVitalsWidget`, `Input/VCInputConfig` |
| Item art, policy category, import, definition data script, `vox.Give / vox.Equip / vox.Use` | Parent | `Tools/Blender/assets/items/*.py`, `pipeline_policy.json`, `Scripts/create_test_items.py`, `Scripts/import_art_assets.py`, `UVoxelDebugSubsystem`, `CLAUDE.md` |

## 5. Data flow additions

```
Equip (server, existing path)                     Use (new)
  TryEquipFromInventory                             IA_Use → RequestUseActiveItem → Server_UseHotbarItem(slot)
   → EffectApplier.ApplyEffects                      → item in slot? Consumable fragment? cooldown ready?
      → PassiveEffects (asset classes, unchanged)     → ApplyAttributeModifiers(ASC, AttributeChanges, instant)
      → NEW ApplyAttributeModifiers(StatModifiers,    → ConsumeEffect / ConsumeAbility if set
           infinite) → handle stored on the slot      → bConsumeOnUse ? TryRemoveItem(id, 1)
  Unequip → RemoveEffects removes it by handle        → cooldown[def] = now + CooldownDuration

Death (server)                                     Respawn (server)
  HandleDied → PS->CaptureItemSnapshot(pawn)        PossessedBy → PS->RestoreItemSnapshot(newPawn)
    inventory slots + equipped items (deep copy)      inventory TryAddItem per slot index, then
                                                       TryEquipFromInventory per saved slot tag
```

## 6. Replication

Unchanged patterns: inventory fast array, equipment slot array, ASC attributes. New server RPC `Server_UseHotbarItem`
on the character (client never applies effects). The snapshot lives only on the server (player state, not replicated).

## 7. Tests

- CGF: `ApplyAttributeModifiers` adds/removes an infinite effect and applies an instant one (world harness from feature 1).
- EquipmentGASIntegration: equip an item with `StatModifiers` → attribute changed → unequip → restored.
- VoxelCharacterPlugin: consumable use changes Health and decrements the stack, cooldown rejects a second use, non-consumable
  rejected; crit roll with chance 1 doubles damage; snapshot capture → restore round-trips inventory slots and equipment.
- Interaction gets its first tests only if touched (not planned).

## 8. PR plan

| PR | Repo | Content |
|----|------|---------|
| A | CommonGameFramework | attribute modifier type + runtime effect helper + test; docs |
| B | ItemInventoryPlugin | fragment fields; docs |
| C | EquipmentPlugin | stat modifiers applied/removed; test |
| D | VoxelCharacterPlugin | use path + input, crit, item snapshot on death, vitals HUD, callables; instructions |
| E | Parent | bumps, Blender item assets + policy + import, definition data script, `vox.Give/Equip/Use`, CLAUDE.md |

Verification: build, in-editor automation, then PIE on the demo world: `vox.Give ID_Weapon_IronSword` → `vox.Equip` →
`vox.SpawnDummy` → `vox.Attack 1` shows 18 damage (and occasional crit) instead of 5; `vox.Give ID_Armor_LeatherHelmet` →
equip → `vox.Damage 10` shows mitigated damage and raised max health; `vox.Give ID_Consumable_HealthPotion` → `vox.Use`
heals; `vox.Damage 1000` → respawn with the sword still equipped and the potion count intact; screenshots of the sword
in hand and the world items on the ground.

## 9. Open questions
1. Item art through the Blender pipeline in this feature (12 low-poly props, recommended since the pipeline and policy
   category exist), or engine primitives as placeholders for now?
2. Consumables in scope (recommended; the only new mechanic is the use path)?
3. Vitals HUD in scope (recommended; small, programmatic like the hotbar)?
4. Use key: `F`? Also fine to route "use" through the secondary action when the active hotbar item is a consumable.
5. Keep-everything-on-death snapshot now (recommended), with loot-bag drop as a later policy?
