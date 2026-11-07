// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "VCWeatherEffects.generated.h"

/**
 * The cold's effect on a character (the weather plan's P5): an infinite gameplay effect that takes SpeedPenalty off
 * the move speed multiplier while the character is in the cold. AVCCharacterBase applies and removes it on the server
 * with the State.Weather.Cold tag (UpdateWeatherEffects); source-defined (no asset) so it replicates to the owning
 * client by class, as UVCEquipmentStatEffect does. The wet state has no effect of its own: State.Weather.Wet slows the
 * stamina's regeneration inside UVCMovementComponent::TickStamina.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCWeatherColdEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UVCWeatherColdEffect();

	/** What the cold takes off the move speed multiplier (1.0 = full speed). */
	static constexpr float SpeedPenalty = 0.15f;
};
