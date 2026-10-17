// Copyright Daniel Raquel. All Rights Reserved.

#include "Camera/VCFirstPersonCameraMode.h"
#include "Core/VCCharacterBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Movement/VCGravityFrame.h"

UVCFirstPersonCameraMode::UVCFirstPersonCameraMode()
{
	FieldOfView = 100.f;
}

FTransform UVCFirstPersonCameraMode::ComputeDesiredTransform(const AVCCharacterBase* Character, float DeltaTime) const
{
	if (!Character)
	{
		return FTransform::Identity;
	}

	FVector Location;
	FRotator Rotation;
	// Eye height and offset along the gravity frame (local up on a planet; world axes otherwise).
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();

	// Try to get location from head socket on the body mesh
	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (Mesh && Mesh->DoesSocketExist(HeadSocketName))
	{
		const FTransform SocketTransform = Mesh->GetSocketTransform(HeadSocketName, RTS_World);
		Location = SocketTransform.GetLocation() + FVCGravityFrame::VectorToWorld(Movement, EyeOffset);
	}
	else
	{
		// Fallback: actor location + BaseEyeHeight
		Location = Character->GetActorLocation() + FVCGravityFrame::VectorToWorld(Movement, FVector(0.f, 0.f, Character->BaseEyeHeight))
			+ FVCGravityFrame::VectorToWorld(Movement, EyeOffset);
	}

	// Use controller rotation for view direction
	if (const AController* Controller = Character->GetController())
	{
		Rotation = Controller->GetControlRotation();
	}
	else
	{
		Rotation = Character->GetActorRotation();
	}

	return FTransform(Rotation, Location);
}
