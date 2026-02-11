// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UAbilitySystemComponent;
struct FGameplayTag;

/**
 * Interface for GAS integration points beyond the standard
 * IAbilitySystemInterface (which is on AVCCharacterBase already).
 *
 * Provides hooks for equipment-driven ability granting and
 * game-specific ability management.
 *
 * AVCCharacterBase implements these methods.
 */
class IVCAbilityBridge
{
public:
	virtual ~IVCAbilityBridge() = default;

	/** Called when equipment abilities change for a slot (grant or revoke). */
	virtual void OnEquipmentAbilitiesChanged(const FGameplayTag& SlotTag) {}
};
