// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCWeatherEffects.h"

#include "Core/VCCharacterAttributeSet.h"

UVCWeatherColdEffect::UVCWeatherColdEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;
	FGameplayModifierInfo Info;
	Info.Attribute = UVCCharacterAttributeSet::GetMoveSpeedMultiplierAttribute();
	Info.ModifierOp = EGameplayModOp::Additive;
	Info.ModifierMagnitude = FGameplayEffectModifierMagnitude(FScalableFloat(-SpeedPenalty));
	Modifiers.Add(Info);
}
