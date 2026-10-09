// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"
#include "VCDamageExecution.generated.h"

/**
 * Execution calculation behind UVCDamageEffect.
 *
 * Inputs on the spec:
 *  - SetByCaller.Damage              base damage from FCGFDamageContext::BaseDamage
 *  - dynamic asset tag Damage.Type.* damage type (Pure skips mitigation)
 * Captured attributes:
 *  - source AttackPower (snapshot at spec creation; ignored for self-applied specs)
 *  - target Defense (live)
 *
 * Output: one additive modifier on UVCCharacterAttributeSet::IncomingDamage,
 * which the attribute set folds into Health in PostGameplayEffectExecute.
 * Targets holding State.Invulnerable receive no output.
 *
 * Formula v1: Pure → Base; otherwise max(0, Base + AttackPower − Defense).
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCDamageExecution : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UVCDamageExecution();

	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
		FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;

	/** Pure formula, exposed so tests and tooltips can mirror the execution without an ASC. */
	static float ComputeDamage(float BaseDamage, float AttackPower, float Defense, bool bPure);
};
