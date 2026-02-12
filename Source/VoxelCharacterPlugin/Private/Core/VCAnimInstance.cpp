// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCAnimInstance.h"
#include "Core/VCCharacterBase.h"
#include "Movement/VCMovementComponent.h"
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

	// --- Locomotion ---
	const FVector Velocity = MovComp->Velocity;
	Speed = Velocity.Size2D();

	const bool bWasFalling = bIsFalling;
	bIsFalling = MovComp->IsFalling();
	bJustLanded = bWasFalling && !bIsFalling;

	bIsCrouching = MovComp->IsCrouching();
	bIsAccelerating = MovComp->GetCurrentAcceleration().SizeSquared() > KINDA_SMALL_NUMBER;

	// Direction: angle between velocity and character forward (for strafing)
	if (Speed > 1.f)
	{
		const FRotator ActorRotation = Character->GetActorRotation();
		const FRotator VelocityRotation = Velocity.Rotation();
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
		const FRotator ControlRotation = Controller->GetControlRotation();
		const FRotator DeltaRotation = UKismetMathLibrary::NormalizedDeltaRotator(ControlRotation, Character->GetActorRotation());
		AimPitch = FMath::ClampAngle(DeltaRotation.Pitch, -90.f, 90.f);
		AimYaw = FMath::ClampAngle(DeltaRotation.Yaw, -90.f, 90.f);
	}

	// --- Surface Type ---
	if (const UVCMovementComponent* VCMovComp = Cast<UVCMovementComponent>(MovComp))
	{
		SurfaceType = VCMovComp->CurrentSurfaceType;
	}

	// --- Equipment Anim Type ---
	ActiveItemAnimType = Character->ActiveItemAnimType;
}
