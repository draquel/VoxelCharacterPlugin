// Copyright Daniel Raquel. All Rights Reserved.

#include "Camera/VCAimCameraMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Movement/VCGravityFrame.h"

UVCAimCameraMode::UVCAimCameraMode()
{
	// Closer and over the shoulder, a narrower field; no lag, so the place is a function of the pawn and the view alone.
	ArmLength = 150.f;
	TargetOffset = FVector(0.f, 60.f, 55.f);
	FieldOfView = 70.f;
	LagSpeed = 0.f;
	MaxLagDistance = 0.f;
}

FVector UVCAimCameraMode::ComputeCameraLocation(const FVector& ActorLocation, const FRotator& ViewRotation, const UCharacterMovementComponent* Movement,
	const FVector& InTargetOffset, float InArmLength)
{
	const FRotator YawOnly = FVCGravityFrame::GetYawRotation(Movement, ViewRotation);
	const FVector Pivot = ActorLocation + YawOnly.RotateVector(InTargetOffset);
	return Pivot - ViewRotation.Vector() * InArmLength;
}

FVector UVCAimCameraMode::ComputeDefaultCameraLocation(const FVector& ActorLocation, const FRotator& ViewRotation, const UCharacterMovementComponent* Movement)
{
	const UVCAimCameraMode* Defaults = GetDefault<UVCAimCameraMode>();
	return ComputeCameraLocation(ActorLocation, ViewRotation, Movement, Defaults->TargetOffset, Defaults->ArmLength);
}
