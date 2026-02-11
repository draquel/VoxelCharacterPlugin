// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Interface for interaction system integration.
 *
 * Provides view-mode-aware trace origin and direction so the
 * InteractionComponent's detection strategy can work correctly
 * in both first-person and third-person modes.
 *
 * AVCCharacterBase implements these methods.
 */
class IVCInteractionBridge
{
public:
	virtual ~IVCInteractionBridge() = default;

	/** Trace origin for interaction detection (camera in FP, eye height in TP). */
	virtual FVector GetInteractionTraceOrigin() const { return FVector::ZeroVector; }

	/** Trace direction for interaction detection (always camera forward). */
	virtual FVector GetInteractionTraceDirection() const { return FVector::ForwardVector; }

	/** Suggested interaction range for the current view mode. */
	virtual float GetInteractionRange() const { return 300.f; }
};
