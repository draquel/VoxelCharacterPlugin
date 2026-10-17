// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCAnimInstance.h"
#include "Core/VCCharacterBase.h"
#include "Movement/VCMovementComponent.h"
#include "Movement/VCGravityFrame.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"

void UVCAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
}

void UVCAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const AVCCharacterBase* Character = Cast<AVCCharacterBase>(TryGetPawnOwner());
	if (!Character)
	{
		return;
	}

	const UCharacterMovementComponent* MovComp = Character->GetCharacterMovement();
	if (!MovComp)
	{
		return;
	}

	// --- Locomotion (speed, direction and aim in the gravity frame: the world's without custom gravity) ---
	const FVector Velocity = MovComp->Velocity;
	Speed = FVCGravityFrame::GetHorizontalSpeed(MovComp, Velocity);

	const bool bWasFalling = bIsFalling;
	bIsFalling = MovComp->IsFalling();
	bJustLanded = bWasFalling && !bIsFalling;

	bIsCrouching = MovComp->IsCrouching();
	bIsAccelerating = MovComp->GetCurrentAcceleration().SizeSquared() > KINDA_SMALL_NUMBER;

	// Direction: angle between velocity and character forward (for strafing)
	if (Speed > 1.f)
	{
		const FRotator ActorRotation = FVCGravityFrame::ToGravityRelative(MovComp, Character->GetActorRotation());
		const FRotator VelocityRotation = FVCGravityFrame::ToGravityRelative(MovComp, Velocity.Rotation());
		Direction = FMath::FindDeltaAngleDegrees(ActorRotation.Yaw, VelocityRotation.Yaw);
	}
	else
	{
		Direction = 0.f;
	}

	// --- View Mode ---
	ViewMode = Character->CurrentViewMode;

	// --- Aim ---
	if (const AController* Controller = Character->GetController())
	{
		const FRotator ControlRotation = FVCGravityFrame::ToGravityRelative(MovComp, Controller->GetControlRotation());
		const FRotator DeltaRotation = UKismetMathLibrary::NormalizedDeltaRotator(ControlRotation,
			FVCGravityFrame::ToGravityRelative(MovComp, Character->GetActorRotation()));
		AimPitch = FMath::ClampAngle(DeltaRotation.Pitch, -90.f, 90.f);
		AimYaw = FMath::ClampAngle(DeltaRotation.Yaw, -90.f, 90.f);
	}

	// --- Surface Type & Swimming ---
	if (const UVCMovementComponent* VCMovComp = Cast<UVCMovementComponent>(MovComp))
	{
		SurfaceType = VCMovComp->CurrentSurfaceType;
		bIsSwimming = MovComp->IsSwimming();
		WaterDepth = VCMovComp->GetTerrainContext().WaterDepth;
	}

	// --- Equipment Anim Type ---
	ActiveItemAnimType = Character->ActiveItemAnimType;
}
