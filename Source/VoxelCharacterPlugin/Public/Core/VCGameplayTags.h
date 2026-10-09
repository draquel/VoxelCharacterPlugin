// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "NativeGameplayTags.h"

/**
 * Native gameplay tags owned by VoxelCharacterPlugin.
 *
 * SetByCaller.Stat.<AttributeName>: one key per attribute that UVCEquipmentStatEffect can modify.
 * The name after "Stat." must equal the attribute's property name, which is how
 * UCGFGameplayEffectStatics maps an FCGFAttributeModifier onto the effect's modifiers.
 */
namespace VCGameplayTags
{
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Stat_MaxHealth);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Stat_MaxStamina);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Stat_MoveSpeedMultiplier);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Stat_MiningSpeed);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Stat_InteractionRange);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Stat_AttackPower);
	VOXELCHARACTERPLUGIN_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(SetByCaller_Stat_Defense);
}
