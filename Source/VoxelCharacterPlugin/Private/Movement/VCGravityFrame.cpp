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

FVector FVCGravityFrame::Horizontal(const UCharacterMovementComponent* Movement, const FVector& WorldVector)
{
	if (!Movement || !Movement->HasCustomGravity())
	{
		return FVector(WorldVector.X, WorldVector.Y, 0.0);
	}
	return Movement->ProjectToGravityFloor(WorldVector);
}

FVector FVCGravityFrame::HorizontalDirection(const UCharacterMovementComponent* Movement, const FVector& WorldVector)
{
	if (!Movement || !Movement->HasCustomGravity())
	{
		return WorldVector.GetSafeNormal2D();
	}
	return Movement->ProjectToGravityFloor(WorldVector).GetSafeNormal();
}

double FVCGravityFrame::HorizontalDistance(const UCharacterMovementComponent* Movement, const FVector& A, const FVector& B)
{
	if (!Movement || !Movement->HasCustomGravity())
	{
		return FVector::Dist2D(A, B);
	}
	return Movement->ProjectToGravityFloor(A - B).Size();
}

double FVCGravityFrame::UpComponent(const UCharacterMovementComponent* Movement, const FVector& WorldVector)
{
	if (!Movement || !Movement->HasCustomGravity())
	{
		return WorldVector.Z;
	}
	return FVector::DotProduct(WorldVector, GetUp(Movement));
}

FRotator FVCGravityFrame::FacingRotation(const UCharacterMovementComponent* Movement, const FVector& WorldDirection)
{
	if (!Movement || !Movement->HasCustomGravity())
	{
		return FRotator(0.0, WorldDirection.Rotation().Yaw, 0.0);
	}
	return GetYawRotation(Movement, WorldDirection.Rotation());
}

FVector FVCGravityFrame::HorizontalAtAngle(const UCharacterMovementComponent* Movement, float AngleRadians)
{
	return VectorToWorld(Movement, FVector(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians), 0.0));
}
