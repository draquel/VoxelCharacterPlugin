// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCAnimInstance.h"
#include "Core/VCAnimatedCharacter.h"
#include "Core/VCGameplayTags.h"
#include "Combat/VCCombatComponent.h"
#include "Interfaces/CGFDamageableInterface.h"
#include "Movement/VCMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"

namespace VCAnimInstanceMath
{
	/** A world vector's part across Up. */
	FVector Horizontal(const FVector& Vector, const FVector& Up)
	{
		return Vector - (Vector | Up) * Up;
	}

	/** The signed angle from A to B about Up (degrees, positive to the right); A and B need not be unit length. */
	double SignedAngleAbout(const FVector& A, const FVector& B, const FVector& Up)
	{
		return FMath::RadiansToDegrees(FMath::Atan2((A ^ B) | Up, A | B));
	}

	/** The facing: the actor's forward across Up, unit length. */
	FVector Facing(const FQuat& ActorRotation, const FVector& Up)
	{
		const FVector Forward = ActorRotation.GetForwardVector();
		FVector Heading = Horizontal(Forward, Up);
		if (Heading.Normalize(UE_KINDA_SMALL_NUMBER))
		{
			return Heading;
		}
		// Forward along up: the actor's own up axis points back when the nose is up, ahead when it is down.
		Heading = Horizontal(ActorRotation.GetUpVector() * -FMath::Sign(Forward | Up), Up);
		if (Heading.Normalize(UE_KINDA_SMALL_NUMBER))
		{
			return Heading;
		}
		FVector AxisA, AxisB;
		Up.FindBestAxisVectors(AxisA, AxisB);
		return AxisA;
	}
}

void UVCAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	ResetKeptState();
}

void UVCAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	bHasGatheredInput = GatherInput(DeltaSeconds, GatheredInput);
}

void UVCAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
	if (bHasGatheredInput)
	{
		ComputeValues(GatheredInput);
	}
}

void UVCAnimInstance::ResetKeptState()
{
	LastFacing = FVector::ZeroVector;
	BodyHeading = FVector::ZeroVector;
	bHasKeptState = false;
}

bool UVCAnimInstance::GatherInput(float DeltaSeconds, FVCAnimInput& OutInput) const
{
	const ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return false;
	}

	OutInput = FVCAnimInput();
	OutInput.DeltaSeconds = DeltaSeconds;
	OutInput.GravityDirection = Movement->GetGravityDirection();
	OutInput.GravityAcceleration = FMath::Abs(Movement->GetGravityZ());
	OutInput.Velocity = Movement->Velocity;
	OutInput.Acceleration = Movement->GetCurrentAcceleration();
	OutInput.MaxAcceleration = Movement->GetMaxAcceleration();
	OutInput.MaxBrakingDeceleration = Movement->GetMaxBrakingDeceleration();
	OutInput.ActorRotation = Character->GetActorQuat();

	// A remote character has no controller here: its base aim rotation carries the replicated view pitch.
	const AController* Controller = Character->GetController();
	OutInput.ControlRotation = Controller ? Controller->GetControlRotation() : Character->GetBaseAimRotation();

	OutInput.bOnGround = Movement->IsMovingOnGround();
	OutInput.bFalling = Movement->IsFalling();
	OutInput.bCrouching = Movement->IsCrouching();
	OutInput.bSwimming = Movement->IsSwimming();
	OutInput.Immersion = OutInput.bSwimming ? Movement->ImmersionDepth() : 0.f;

	if (Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor())
	{
		OutInput.FloorNormal = Movement->CurrentFloor.HitResult.ImpactNormal;
	}

	// Ground below a falling character, along gravity from the capsule's bottom, on the channel the movement sweeps on.
	if (OutInput.bFalling && MaxGroundTraceDistance > 0.f)
	{
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const float HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.f;
		const FVector Feet = Character->GetActorLocation() + OutInput.GravityDirection * HalfHeight;
		const FVector End = Feet + OutInput.GravityDirection * MaxGroundTraceDistance;

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(VCAnimGroundTrace), false, Character);
		FCollisionResponseParams ResponseParams;
		Movement->InitCollisionParams(QueryParams, ResponseParams);
		const ECollisionChannel Channel = Movement->UpdatedComponent
			? Movement->UpdatedComponent->GetCollisionObjectType() : ECC_Pawn;

		FHitResult Hit;
		const UWorld* World = Character->GetWorld();
		OutInput.GroundDistance = World && World->LineTraceSingleByChannel(Hit, Feet, End, Channel, QueryParams, ResponseParams)
			? Hit.Distance : -1.f;
	}

	if (const UVCMovementComponent* VCMovement = Cast<UVCMovementComponent>(Movement))
	{
		OutInput.bSprinting = VCMovement->IsSprinting();
		OutInput.SurfaceType = VCMovement->CurrentSurfaceType;
		OutInput.WaterDepth = VCMovement->GetTerrainContext().WaterDepth;
	}

	if (const IVCAnimatedCharacter* Animated = Cast<IVCAnimatedCharacter>(Character))
	{
		OutInput.ViewMode = Animated->GetAnimViewMode();
		OutInput.ActionSet = Animated->GetAnimActionSet();
		if (const UVCCombatComponent* Combat = Animated->GetAnimCombatComponent())
		{
			OutInput.bDead = ICGFDamageableInterface::Execute_IsDead(Combat);
			OutInput.bDowned = Combat->IsDowned();
		}
	}
	return true;
}

void UVCAnimInstance::ComputeValues(const FVCAnimInput& Input)
{
	using namespace VCAnimInstanceMath;

	// The facing's frame this update: local up, the actor's forward across it, and right. Without custom gravity these
	// are +Z and the actor's yaw, so every value below is the Z-up expression.
	// The movement component keeps its gravity direction unit length; normalizing it again would only round it.
	FVector Up = -Input.GravityDirection;
	if (!Up.IsNormalized() && !Up.Normalize())
	{
		Up = FVector::UpVector;
	}
	const FVector Forward = Facing(Input.ActorRotation, Up);
	const FVector Right = Up ^ Forward;
	const auto ToLocal = [&Forward, &Right, &Up](const FVector& Vector)
	{
		return FVector(Vector | Forward, Vector | Right, Vector | Up);
	};
	const float DeltaSeconds = Input.DeltaSeconds;

	// --- Locomotion ---
	const FVector HorizontalVelocity = Horizontal(Input.Velocity, Up);
	Speed = HorizontalVelocity.Size();
	Direction = Speed > 1.f ? SignedAngleAbout(Forward, HorizontalVelocity, Up) : 0.f;

	const bool bWasFalling = bIsFalling;
	bIsFalling = Input.bFalling;
	bJustLanded = bWasFalling && !bIsFalling;
	bIsCrouching = Input.bCrouching;
	bIsAccelerating = Input.Acceleration.SizeSquared() > UE_KINDA_SMALL_NUMBER;
	bIsSprinting = Input.bSprinting;

	LocalVelocity = ToLocal(Input.Velocity);
	LocalAcceleration = ToLocal(Input.Acceleration);

	// Speeding up (or starting from rest) measures against the maximum acceleration, slowing down against braking.
	const float AccelerationScale = (Input.Acceleration | Input.Velocity) >= 0.f
		? Input.MaxAcceleration : Input.MaxBrakingDeceleration;
	RelativeAcceleration = AccelerationScale > UE_KINDA_SMALL_NUMBER
		? LocalAcceleration.GetClampedToMaxSize(AccelerationScale) / AccelerationScale : FVector::ZeroVector;
	LeanAmount = RelativeAcceleration.Y;

	// --- Turning: the last facing is a world vector, re-projected onto this update's horizontal ---
	YawRate = 0.f;
	double Turn = 0.0;
	if (bHasKeptState)
	{
		FVector Last = Horizontal(LastFacing, Up);
		if (Last.Normalize(UE_KINDA_SMALL_NUMBER))
		{
			Turn = SignedAngleAbout(Last, Forward, Up);
			YawRate = DeltaSeconds > UE_SMALL_NUMBER ? Turn / DeltaSeconds : 0.f;
		}
	}

	// --- Turn in place: the body heading is a world vector too, so the frame's twist near the south pole never reaches it ---
	FVector Body = bHasKeptState ? Horizontal(BodyHeading, Up) : FVector::ZeroVector;
	double Offset = Body.Normalize(UE_KINDA_SMALL_NUMBER) ? SignedAngleAbout(Forward, Body, Up) : 0.0;
	if (FMath::Abs(Turn) > TurnInPlaceMaxOffset)
	{
		Offset = 0.0;
	}
	const bool bStanding = Input.bOnGround && Speed < MovingSpeedThreshold;
	if (!bStanding)
	{
		Offset = FMath::FInterpTo(Offset, 0.0, DeltaSeconds, TurnInPlaceRecoverySpeed);
	}
	Offset = FMath::Clamp(Offset, -double(TurnInPlaceMaxOffset), double(TurnInPlaceMaxOffset));
	RootYawOffset = Offset;
	BodyHeading = FQuat(Up, FMath::DegreesToRadians(Offset)).RotateVector(Forward);

	// --- Ground ---
	SlopePitch = 0.f;
	SlopeRoll = 0.f;
	if (!Input.bFalling && !Input.bSwimming && !Input.FloorNormal.IsNearlyZero())
	{
		const FVector Normal = ToLocal(Input.FloorNormal);
		SlopePitch = FMath::RadiansToDegrees(FMath::Atan2(-Normal.X, Normal.Z));
		SlopeRoll = FMath::RadiansToDegrees(FMath::Atan2(-Normal.Y, Normal.Z));
	}

	GroundDistance = Input.bFalling ? Input.GroundDistance : 0.f;
	TimeToLand = -1.f;
	if (Input.bFalling && Input.GroundDistance >= 0.f)
	{
		// Height along up over time: Vu t - g t^2 / 2 reaches -GroundDistance at the larger root.
		const double UpSpeed = Input.Velocity | Up;
		const double Gravity = Input.GravityAcceleration;
		const double Height = Input.GroundDistance;
		if (Gravity > UE_KINDA_SMALL_NUMBER)
		{
			TimeToLand = (UpSpeed + FMath::Sqrt(UpSpeed * UpSpeed + 2.0 * Gravity * Height)) / Gravity;
		}
		else if (UpSpeed < -UE_KINDA_SMALL_NUMBER)
		{
			TimeToLand = Height / -UpSpeed;
		}
	}

	// --- Aim: the look rotation relative to the facing's frame ---
	const FQuat FacingToWorld = FRotationMatrix::MakeFromZX(Up, Forward).ToQuat();
	const FRotator AimRelative = (FacingToWorld.Inverse() * Input.ControlRotation.Quaternion()).Rotator();
	AimPitch = FMath::ClampAngle(AimRelative.Pitch, -90.f, 90.f);
	AimYaw = FMath::ClampAngle(AimRelative.Yaw, -90.f, 90.f);

	// --- What the character is and holds ---
	ViewMode = Input.ViewMode;
	ActionSet = Input.ActionSet.IsValid() ? Input.ActionSet : VCGameplayTags::Equipment_AnimSet_Unarmed.GetTag();
	bIsDead = Input.bDead;
	bIsDowned = Input.bDowned;

	// --- Surface and swimming ---
	SurfaceType = Input.SurfaceType;
	bIsSwimming = Input.bSwimming;
	bIsUnderwater = Input.bSwimming && Input.Immersion >= UnderwaterImmersion;
	WaterDepth = Input.WaterDepth;

	LastFacing = Forward;
	bHasKeptState = true;
}
