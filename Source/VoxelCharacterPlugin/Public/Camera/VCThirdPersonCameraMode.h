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

	/**
	 * A pivot that jumps farther than this in one frame snaps to the character instead of lagging after it: a teleport, a
	 * respawn, the settle onto the ground. The camera is the voxel world's streaming viewer, so a lagging pivot swept it
	 * along the straight line from the old spot, through a planet, loading ground under every direction it crossed
	 * (spherical Phase 6, G41). Walking, running and ordinary flight stay far under it (100,000 uu/s is 1,700 a frame at
	 * 60 Hz); the debug climb to orbit (vox.FlyTo) passes it high up, where the pivot then follows without lag (cosmetic).
	 * 0 = never snap.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Camera", meta = (ClampMin = "0.0"))
	float TeleportSnapDistance = 5000.f;

	/**
	 * Pure: one frame of the pivot's lag (ComputeDesiredTransform's): towards the ideal pivot at LagSpeed, or onto it when
	 * the ideal pivot jumped farther than SnapDistance since the last frame (a teleport, G41) or there is no lag.
	 * @param LaggedPivot The lagged pivot after the last frame
	 * @param IdealPivot This frame's ideal pivot
	 * @param LastIdealPivot The last frame's ideal pivot
	 * @param DeltaTime Frame time (s)
	 * @param InLagSpeed Lag speed (0 = no lag)
	 * @param SnapDistance Teleport threshold (0 = never snap)
	 * @param bOutSnapped Whether it snapped for a teleport
	 * @return The new lagged pivot
	 */
	static FVector ComputeLaggedPivot(const FVector& LaggedPivot, const FVector& IdealPivot, const FVector& LastIdealPivot, float DeltaTime,
		float InLagSpeed, float SnapDistance, bool& bOutSnapped);

private:
	/** Lagged pivot position (persisted across frames for smooth follow). */
	mutable FVector LaggedPivot = FVector::ZeroVector;
	mutable bool bPivotInitialized = false;

	/** The last frame's ideal pivot (a jump from it is a teleport). */
	mutable FVector LastIdealPivot = FVector::ZeroVector;
};
