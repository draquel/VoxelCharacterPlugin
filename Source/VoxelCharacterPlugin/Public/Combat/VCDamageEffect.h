// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "VCDamageEffect.generated.h"

/**
 * Instant gameplay effect that runs UVCDamageExecution. Source-defined so no
 * content asset is needed; UVCCombatComponent::DamageEffectClass points here by
 * default and can be swapped for a Blueprint subclass with extra modifiers.
 *
 * The spec is expected to carry SetByCaller.Damage and a Damage.Type.* dynamic
 * asset tag (both set by UVCCombatComponent::ApplyDamage).
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCDamageEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UVCDamageEffect();
};
