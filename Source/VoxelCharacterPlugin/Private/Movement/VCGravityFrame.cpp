// Copyright Daniel Raquel. All Rights Reserved.

#include "Movement/VCGravityFrame.h"
#include "GameFramework/CharacterMovementComponent.h"

FQuat FVCGravityFrame::GetGravityToWorld(const FVector& GravityDirection)
{
	// The engine's UCharacterMovementComponent::SetGravityDirection builds WorldToGravityTransform the same way.
	return FQuat::FindBetweenNormals(FVector::UpVector, -GravityDirection.GetSafeNormal());
}

FRotator FVCGravityFrame::ToGravityRelative(const FVector& GravityDirection, const FRotator& WorldRotation)
{
	return (GetGravityToWorld(GravityDirection).Inverse() * WorldRotation.Quaternion()).Rotator();
}

FRotator FVCGravityFrame::ToWorld(const FVector& GravityDirection, const FRotator& GravityRotation)
{
	return (GetGravityToWorld(GravityDirection) * GravityRotation.Quaternion()).Rotator();
}

FRotator FVCGravityFrame::GetYawRotation(const FVector& GravityDirection, const FRotator& WorldRotation)
{
	const FRotator Relative = ToGravityRelative(GravityDirection, WorldRotation);
	return ToWorld(GravityDirection, FRotator(0.0, Relative.Yaw, 0.0));
}

FRotator FVCGravityFrame::ToGravityRelative(const UCharacterMovementComponent* Movement, const FRotator& WorldRotation)
{
	if (!Movement || !Movement->HasCustomGravity())
	{
		return WorldRotation;
	}
	return (Movement->GetGravityToWorldTransform() * WorldRotation.Quaternion()).Rotator();
}

FRotator FVCGravityFrame::ToWorld(const UCharacterMovementComponent* Movement, const FRotator& GravityRotation)
{
	if (!Movement || !Movement->HasCustomGravity())
	{
		return GravityRotation;
	}
	return (Movement->GetWorldToGravityTransform() * GravityRotation.Quaternion()).Rotator();
}

FRotator FVCGravityFrame::GetYawRotation(const UCharacterMovementComponent* Movement, const FRotator& WorldRotation)
{
	if (!Movement || !Movement->HasCustomGravity())
	{
		return FRotator(0.0, WorldRotation.Yaw, 0.0);
	}
	const FRotator Relative = ToGravityRelative(Movement, WorldRotation);
	return ToWorld(Movement, FRotator(0.0, Relative.Yaw, 0.0));
}

FVector FVCGravityFrame::VectorToWorld(const UCharacterMovementComponent* Movement, const FVector& GravityVector)
{
	if (!Movement || !Movement->HasCustomGravity())
	{
		return GravityVector;
	}
	// The engine's naming: GetWorldToGravityTransform rotates gravity-frame vectors into world space.
	return Movement->GetWorldToGravityTransform().RotateVector(GravityVector);
}

FVector FVCGravityFrame::GetUp(const UCharacterMovementComponent* Movement)
{
	return Movement ? -Movement->GetGravityDirection() : FVector::UpVector;
}

double FVCGravityFrame::GetHorizontalSpeed(const UCharacterMovementComponent* Movement, const FVector& Velocity)
{
	if (!Movement || !Movement->HasCustomGravity())
	{
		return Velocity.Size2D();
	}
	return Movement->ProjectToGravityFloor(Velocity).Size();
}
