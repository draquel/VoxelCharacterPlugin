// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCGameplayTags.h"

namespace VCGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_AnimSet,             "Equipment.AnimSet",             "Action sets: what a held item animates the character with (layer and montages)");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_AnimSet_Unarmed,     "Equipment.AnimSet.Unarmed",     "Action set: nothing in the main hand");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_AnimSet_Tool,        "Equipment.AnimSet.Tool",        "Action set: a tool (pickaxe, axe), and any held item that names no set");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_AnimSet_SwordShield, "Equipment.AnimSet.SwordShield", "Action set: a one-handed weapon with a shield");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_AnimSet_TwoHand,     "Equipment.AnimSet.TwoHand",     "Action set: a two-handed melee weapon");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Equipment_AnimSet_Bow,         "Equipment.AnimSet.Bow",         "Action set: a bow");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Ability_Attack_Ranged,         "Ability.Attack.Ranged",         "The bow's attack: hold to draw, release to shoot (UVCRangedAttackAbility)");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Stat_MaxHealth,          "SetByCaller.Stat.MaxHealth",          "Equipment stat key: UVCCharacterAttributeSet::MaxHealth");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Stat_MaxStamina,         "SetByCaller.Stat.MaxStamina",         "Equipment stat key: UVCCharacterAttributeSet::MaxStamina");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Stat_MoveSpeedMultiplier,"SetByCaller.Stat.MoveSpeedMultiplier","Equipment stat key: UVCCharacterAttributeSet::MoveSpeedMultiplier");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Stat_MiningSpeed,        "SetByCaller.Stat.MiningSpeed",        "Equipment stat key: UVCCharacterAttributeSet::MiningSpeed");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Stat_InteractionRange,   "SetByCaller.Stat.InteractionRange",   "Equipment stat key: UVCCharacterAttributeSet::InteractionRange");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Stat_AttackPower,        "SetByCaller.Stat.AttackPower",        "Equipment stat key: UVCCombatAttributeSet::AttackPower");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(SetByCaller_Stat_Defense,            "SetByCaller.Stat.Defense",            "Equipment stat key: UVCCombatAttributeSet::Defense");
}
