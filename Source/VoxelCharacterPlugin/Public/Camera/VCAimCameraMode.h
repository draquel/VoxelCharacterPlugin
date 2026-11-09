// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/VCThirdPersonCameraMode.h"
#include "VCAimCameraMode.generated.h"

class UCharacterMovementComponent;

/**
 * The third-person aim camera (the character refinement plan's C3, D73 and D74): closer, over the shoulder, a narrower
 * field, and no lag, so the camera's place follows from the pawn and the control rotation alone. That makes it
 * deterministic: the server computes the same origin for the shot's ray (UVCCombatStatics::ComputeAimRay) as the owner
 * draws, and a reticle at the screen's centre marks where the arrow lands (G156). Pushed while the character aims
 * (AVCCharacterBase::IsAiming), popped when it stops.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCAimCameraMode : public UVCThirdPersonCameraMode
{
	GENERATED_BODY()

public:
	UVCAimCameraMode();

	/**
	 * Where this camera sits for a pawn and a view (pure, the same on every machine): the pivot is the pawn's location plus
	 * TargetOffset turned by the view's yaw in the gravity frame, the camera ArmLength back from it along the view.
	 * @param ActorLocation  The pawn's location
	 * @param ViewRotation   The control (or base aim) rotation
	 * @param Movement       The pawn's movement, for the gravity frame (null: world Z up)
	 * @param InTargetOffset The offset from the pawn to the pivot (the class default's)
	 * @param InArmLength    The arm (the class default's)
	 * @return The camera's location
	 */
	static FVector ComputeCameraLocation(const FVector& ActorLocation, const FRotator& ViewRotation, const UCharacterMovementComponent* Movement,
		const FVector& InTargetOffset, float InArmLength);

	/** ComputeCameraLocation with this class's defaults. */
	static FVector ComputeDefaultCameraLocation(const FVector& ActorLocation, const FRotator& ViewRotation, const UCharacterMovementComponent* Movement);
};
