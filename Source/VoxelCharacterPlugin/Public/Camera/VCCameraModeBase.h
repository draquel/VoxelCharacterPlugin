// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VCCameraModeBase.generated.h"

class AVCCharacterBase;

/**
 * Abstract base for a camera behaviour mode.
 *
 * Camera modes are lightweight UObjects managed by UVCCameraManager.
 * Each mode computes a desired camera transform; the manager blends
 * between the top two modes on the stack during transitions.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class VOXELCHARACTERPLUGIN_API UVCCameraModeBase : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Compute the desired camera world-space transform for this frame.
	 * @param Character  The owning character.
	 * @param DeltaTime  Frame delta.
	 * @return Desired camera transform.
	 */
	virtual FTransform ComputeDesiredTransform(const AVCCharacterBase* Character, float DeltaTime) const
		PURE_VIRTUAL(UVCCameraModeBase::ComputeDesiredTransform, return FTransform::Identity;);

	/** Desired field of view for this mode (degrees). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Camera")
	float FieldOfView = 90.f;

	/** Blend weight (0-1), managed by the camera manager during transitions. */
	float CurrentBlendWeight = 0.f;
};
