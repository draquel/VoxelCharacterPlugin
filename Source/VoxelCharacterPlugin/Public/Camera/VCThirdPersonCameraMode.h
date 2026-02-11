// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/VCCameraModeBase.h"
#include "VCThirdPersonCameraMode.generated.h"

/**
 * Third-person over-shoulder camera mode.
 *
 * Computes a pivot behind and above the character with configurable offset
 * and arm length.  Camera lag provides smooth follow.  Voxel collision
 * (pulling the camera forward on terrain hits) is handled by the
 * UVCCameraManager, not here.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCThirdPersonCameraMode : public UVCCameraModeBase
{
	GENERATED_BODY()

public:
	UVCThirdPersonCameraMode();

	virtual FTransform ComputeDesiredTransform(const AVCCharacterBase* Character, float DeltaTime) const override;

	/** Distance from pivot to camera along the view direction. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Camera")
	float ArmLength = 300.f;

	/** Offset from actor origin to the camera pivot (local space). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Camera")
	FVector TargetOffset = FVector(0.f, 50.f, 60.f);

	/** Interpolation speed for camera lag (higher = snappier). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Camera", meta = (ClampMin = "0.0"))
	float LagSpeed = 10.f;

private:
	/** Lagged pivot position (persisted across frames for smooth follow). */
	mutable FVector LaggedPivot = FVector::ZeroVector;
	mutable bool bPivotInitialized = false;
};
