// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UActorComponent;
class AActor;

/**
 * Interface for inventory access from the character controller.
 *
 * Uses opaque types (UActorComponent*, AActor*) to avoid compile-time
 * dependency on ItemInventoryPlugin. Callers cast in .cpp files.
 *
 * AVCCharacterBase implements these methods; the bodies conditionally
 * route to UInventoryComponent when WITH_INVENTORY_PLUGIN is enabled.
 */
class IVCInventoryBridge
{
public:
	virtual ~IVCInventoryBridge() = default;

	/** Get the character's primary inventory component (UInventoryComponent*). */
	virtual UActorComponent* GetPrimaryInventory() const { return nullptr; }

	/** Get the currently selected hotbar slot index. */
	virtual int32 GetActiveHotbarSlot() const { return 0; }

	/** Set the active hotbar slot index. */
	virtual void SetActiveHotbarSlot(int32 SlotIndex) {}

	/** Attempt to pick up a world item into the inventory. */
	virtual bool RequestPickupItem(AActor* WorldItem) { return false; }

	/** Drop the active item from the hotbar into the world. */
	virtual bool RequestDropActiveItem(int32 Count = 1) { return false; }
};
