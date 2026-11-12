// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 * Native gameplay tags owned by VoxelCharacterPlugin.
 *
 * SetByCaller.Stat.<AttributeName>: one key per attribute that UVCEquipmentStatEffect can modify.
 * The name after "Stat." must equal the attribute's property name, which is how
 * UCGFGameplayEffectStatics maps an FCGFAttributeModifier onto the effect's modifiers.
 *
 * Equipment.AnimSet.<Set>: the action set of what a character holds, which picks its anim layer and montages
 * (character animation P2/P3). An item says its set in its definition's ItemTags; an item without one is a Tool, a
 * character holding nothing is Unarmed.
 *
 * Ability.Attack.Ranged: the bow's attack (UVCRangedAttackAbility, ranged combat plan B3), beside CommonGameFramework's
 * Ability.Attack.Melee.
 */
namespace VCGameplayTags
{
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_AnimSet);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_AnimSet_Unarmed);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_AnimSet_Tool);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_AnimSet_SwordShield);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_AnimSet_TwoHand);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Equipment_AnimSet_Bow);

	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Attack_Ranged);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Block);

	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Stat_MaxHealth);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Stat_MaxStamina);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Stat_MoveSpeedMultiplier);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Stat_MiningSpeed);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Stat_InteractionRange);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Stat_AttackPower);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Stat_Defense);

	// The weather on the character (the weather plan's P5): loose tags the server sets, replicated
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Weather_Wet);
	/** The character aims a ranged weapon (holds the secondary action, or draws): the server sets it, replicated (character refinement plan C3, D73). */
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Aiming);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Weather_Cold);
}
