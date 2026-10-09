// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "VCEquipmentStatEffect.generated.h"

/**
 * Infinite gameplay effect carrying one additive SetByCaller modifier per equipment-modifiable
 * attribute, keyed SetByCaller.Stat.<AttributeName> (VCGameplayTags). Equipment stats authored as
 * FCGFAttributeModifier lists are applied through it by UCGFGameplayEffectStatics; unrequested
 * stats are set to zero, so one class serves every item.
 *
 * Source-defined (no asset) so it replicates to the owning client by class reference, which a
 * runtime-built effect cannot. Configured as the project's stat-modifier effect in
 * Project Settings > Plugins > Equipment GAS.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCEquipmentStatEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UVCEquipmentStatEffect();
};
