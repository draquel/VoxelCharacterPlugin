// Copyright Daniel Raquel. All Rights Reserved.

#include "Combat/VCDamageEffect.h"
#include "Combat/VCDamageExecution.h"

UVCDamageEffect::UVCDamageEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayEffectExecutionDefinition Execution;
	Execution.CalculationClass = UVCDamageExecution::StaticClass();
	Executions.Add(Execution);
}
