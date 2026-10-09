// Copyright Daniel Raquel. All Rights Reserved.

#include "Combat/VCDamageExecution.h"
#include "Combat/VCCombatAttributeSet.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Tags/CGFGameplayTags.h"
#include "AbilitySystemComponent.h"

namespace
{
	/** Captured-attribute definitions, built once. */
	struct FVCDamageStatics
	{
		FGameplayEffectAttributeCaptureDefinition AttackPowerDef;
		FGameplayEffectAttributeCaptureDefinition DefenseDef;

		FVCDamageStatics()
			// Snapshot the attacker's power when the spec is made; read the target's defense live.
			: AttackPowerDef(UVCCombatAttributeSet::GetAttackPowerAttribute(), EGameplayEffectAttributeCaptureSource::Source, true)
			, DefenseDef(UVCCombatAttributeSet::GetDefenseAttribute(), EGameplayEffectAttributeCaptureSource::Target, false)
		{
		}
	};

	const FVCDamageStatics& DamageStatics()
	{
		static FVCDamageStatics Statics;
		return Statics;
	}
}

UVCDamageExecution::UVCDamageExecution()
{
	RelevantAttributesToCapture.Add(DamageStatics().AttackPowerDef);
	RelevantAttributesToCapture.Add(DamageStatics().DefenseDef);
}

float UVCDamageExecution::ComputeDamage(float BaseDamage, float AttackPower, float Defense, bool bPure)
{
	if (!FMath::IsFinite(BaseDamage) || BaseDamage <= 0.f)
	{
		return 0.f;
	}
	if (bPure)
	{
		return BaseDamage;
	}
	return FMath::Max(0.f, BaseDamage + FMath::Max(0.f, AttackPower) - FMath::Max(0.f, Defense));
}

void UVCDamageExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();

	FAggregatorEvaluateParameters EvalParams;
	EvalParams.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	EvalParams.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	// Invulnerable targets take nothing; the component also rejects earlier, this is the GAS-side guard
	// for effects applied by paths that bypass UVCCombatComponent::ApplyDamage.
	if (EvalParams.TargetTags && EvalParams.TargetTags->HasTag(CGFGameplayTags::State_Invulnerable))
	{
		return;
	}

	const float BaseDamage = Spec.GetSetByCallerMagnitude(CGFGameplayTags::SetByCaller_Damage, /*WarnIfNotFound*/ false, 0.f);

	float AttackPower = 0.f;
	float Defense = 0.f;
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().AttackPowerDef, EvalParams, AttackPower);
	ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().DefenseDef, EvalParams, Defense);

	// A spec the target applied to itself (hazards, debug commands, DoTs with no attacker) must not
	// add the *target's* attack power to the damage it takes.
	if (ExecutionParams.GetSourceAbilitySystemComponent() == ExecutionParams.GetTargetAbilitySystemComponent())
	{
		AttackPower = 0.f;
	}

	const bool bPure = Spec.GetDynamicAssetTags().HasTag(CGFGameplayTags::Damage_Type_Pure);
	const float Damage = ComputeDamage(BaseDamage, AttackPower, Defense, bPure);

	if (Damage > 0.f)
	{
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(
			UVCCharacterAttributeSet::GetIncomingDamageAttribute(), EGameplayModOp::Additive, Damage));
	}
}
