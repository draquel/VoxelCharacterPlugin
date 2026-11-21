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

	// Apply camera lag (a teleport snaps: the camera is the streaming viewer, G41)
	if (!bPivotInitialized)
	{
		LaggedPivot = IdealPivot;
		bPivotInitialized = true;
	}
	else
	{
		bool bSnapped = false;
		LaggedPivot = ComputeLaggedPivot(LaggedPivot, IdealPivot, LastIdealPivot, DeltaTime, LagSpeed, TeleportSnapDistance, bSnapped, MaxLagDistance);
	}
	LastIdealPivot = IdealPivot;

	// Camera location: pull back from pivot along view direction
	const FVector ViewDirection = ViewRotation.Vector();
	const FVector CameraLocation = LaggedPivot - ViewDirection * ArmLength;

	return FTransform(ViewRotation, CameraLocation);
}

FVector UVCThirdPersonCameraMode::ComputeLaggedPivot(const FVector& InLaggedPivot, const FVector& IdealPivot, const FVector& InLastIdealPivot,
	float DeltaTime, float InLagSpeed, float SnapDistance, bool& bOutSnapped, float InMaxLagDistance)
{
	bOutSnapped = SnapDistance > 0.f && FVector::DistSquared(IdealPivot, InLastIdealPivot) > FMath::Square(static_cast<double>(SnapDistance));
	if (bOutSnapped || InLagSpeed <= 0.f || DeltaTime <= 0.f)
	{
		return IdealPivot;
	}
	FVector Lagged = FMath::VInterpTo(InLaggedPivot, IdealPivot, DeltaTime, InLagSpeed);
	// The cap on the trail (C4): never farther behind the character than InMaxLagDistance.
	if (InMaxLagDistance > 0.f)
	{
		const FVector Trail = Lagged - IdealPivot;
		const double Distance = Trail.Size();
		if (Distance > InMaxLagDistance)
		{
			Lagged = IdealPivot + Trail * (static_cast<double>(InMaxLagDistance) / Distance);
		}
	}
	return Lagged;
}
