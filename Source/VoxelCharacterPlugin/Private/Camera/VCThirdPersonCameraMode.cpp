// Copyright Daniel Raquel. All Rights Reserved.

#include "Camera/VCThirdPersonCameraMode.h"
#include "Core/VCCharacterBase.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Movement/VCGravityFrame.h"

UVCThirdPersonCameraMode::UVCThirdPersonCameraMode()
{
	FieldOfView = 90.f;
}

FTransform UVCThirdPersonCameraMode::ComputeDesiredTransform(const AVCCharacterBase* Character, float DeltaTime) const
{
	if (!Character)
	{
		return FTransform::Identity;
	}

	// Controller rotation drives the view direction
	FRotator ViewRotation;
	if (const AController* Controller = Character->GetController())
	{
		ViewRotation = Controller->GetControlRotation();
	}
	else
	{
		ViewRotation = Character->GetActorRotation();
	}

	// Compute ideal pivot: character origin + local-space target offset (turned by the view's yaw in the gravity frame:
	// the world's yaw without custom gravity, the local horizontal on a planet)
	const FVector CharacterLocation = Character->GetActorLocation();
	const FRotator YawOnly = FVCGravityFrame::GetYawRotation(Character->GetCharacterMovement(), ViewRotation);
	const FVector RotatedOffset = YawOnly.RotateVector(TargetOffset);
	const FVector IdealPivot = CharacterLocation + RotatedOffset;

	// Apply camera lag
	if (!bPivotInitialized)
	{
		LaggedPivot = IdealPivot;
		bPivotInitialized = true;
	}
	else if (LagSpeed > 0.f && DeltaTime > 0.f)
	{
		LaggedPivot = FMath::VInterpTo(LaggedPivot, IdealPivot, DeltaTime, LagSpeed);
	}
	else
	{
		LaggedPivot = IdealPivot;
	}

	// Camera location: pull back from pivot along view direction
	const FVector ViewDirection = ViewRotation.Vector();
	const FVector CameraLocation = LaggedPivot - ViewDirection * ArmLength;

	return FTransform(ViewRotation, CameraLocation);
}
