// Copyright Daniel Raquel. All Rights Reserved.

#include "Combat/VCEquipmentStatEffect.h"
#include "Combat/VCCombatAttributeSet.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Core/VCGameplayTags.h"

namespace
{
	void AddStatModifier(UGameplayEffect& Effect, const FGameplayAttribute& Attribute, const FGameplayTag& Tag)
	{
		FSetByCallerFloat SetByCaller;
		SetByCaller.DataTag = Tag;

		FGameplayModifierInfo Info;
		Info.Attribute = Attribute;
		Info.ModifierOp = EGameplayModOp::Additive;
		Info.ModifierMagnitude = FGameplayEffectModifierMagnitude(SetByCaller);
		Effect.Modifiers.Add(Info);
	}
}

UVCEquipmentStatEffect::UVCEquipmentStatEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	using namespace VCGameplayTags;
	AddStatModifier(*this, UVCCharacterAttributeSet::GetMaxHealthAttribute(),           SetByCaller_Stat_MaxHealth);
	AddStatModifier(*this, UVCCharacterAttributeSet::GetMaxStaminaAttribute(),          SetByCaller_Stat_MaxStamina);
	AddStatModifier(*this, UVCCharacterAttributeSet::GetMoveSpeedMultiplierAttribute(), SetByCaller_Stat_MoveSpeedMultiplier);
	AddStatModifier(*this, UVCCharacterAttributeSet::GetMiningSpeedAttribute(),         SetByCaller_Stat_MiningSpeed);
	AddStatModifier(*this, UVCCharacterAttributeSet::GetInteractionRangeAttribute(),    SetByCaller_Stat_InteractionRange);
	AddStatModifier(*this, UVCCombatAttributeSet::GetAttackPowerAttribute(),            SetByCaller_Stat_AttackPower);
	AddStatModifier(*this, UVCCombatAttributeSet::GetDefenseAttribute(),                SetByCaller_Stat_Defense);
}
