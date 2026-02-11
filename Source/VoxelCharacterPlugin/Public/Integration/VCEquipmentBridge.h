// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class USkeletalMeshComponent;
struct FGameplayTag;

/**
 * Interface for equipment visual management across view modes.
 *
 * Handles the FP/TP mesh distinction: in first-person, MainHand and
 * OffHand items attach to the FP arms mesh; in third-person, all
 * equipment attaches to the full body mesh.
 *
 * AVCCharacterBase implements these methods; the bodies conditionally
 * route to UEquipmentManagerComponent when WITH_EQUIPMENT_PLUGIN is enabled.
 */
class IVCEquipmentBridge
{
public:
	virtual ~IVCEquipmentBridge() = default;

	/** Re-attach all equipment visuals to the appropriate mesh for the current view mode. */
	virtual void UpdateEquipmentAttachments() {}

	/** Get the target skeletal mesh for a given equipment slot in the current view mode. */
	virtual USkeletalMeshComponent* GetTargetMeshForSlot(const FGameplayTag& SlotTag) const { return nullptr; }
};
