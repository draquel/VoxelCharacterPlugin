// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCAnimInstance.h"
#include "Core/VCAnimatedCharacter.h"
#include "Core/VCGameplayTags.h"
#include "Animation/VCActionAnimSet.h"
#include "Animation/VCActionMontageComponent.h"
#include "Animation/VCAppearanceComponent.h"
#include "Animation/VCLocomotionAnimSet.h"
#include "Animation/VCLocomotionClipSet.h"
#include "Combat/VCCombatComponent.h"
#include "Interfaces/CGFDamageableInterface.h"
#include "Movement/VCMovementComponent.h"
#include "AIController.h"
#include "AISystem.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarAnimFootIK(
		TEXT("vc.Anim.FootIK"), 1,
		TEXT("Foot placement and the leg IK on the characters' anim graph: 1 on, 0 off (A/B; character animation P4)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarAnimSprintPace(
		TEXT("vc.Anim.SprintPace"), 1,
		TEXT("Above the locomotion's top authored speed (a sprint), the walk/run clips play faster so the feet keep pace with the ground: 1 on, 0 off (A/B; character animation P4)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarAnimLookAt(
		TEXT("vc.Anim.LookAt"), 1,
		TEXT("The head looking toward the view's aim (a player) or the focus (an NPC): 1 on, 0 off (A/B; character animation P4)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarAnimHandIK(
		TEXT("vc.Anim.HandIK"), 1,
		TEXT("The off hand's IK onto a two-handed item's grip: 1 on, 0 off (A/B; character animation P4)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarAnimStance(
		TEXT("vc.Anim.Stance"), 2,
		TEXT("A held item's stance over the upper body (character refinement plan C2): 0 off (the locomotion alone), 1 the static idle as before C2, 2 the moving stance (the set's idle, walk and jog by speed on a blend ramp from spine_01), faded by speed where the layer asks (the bow's, D85), 3 the moving stance at every gait (C2, before D85; A/B)."),
		ECVF_Default);

	/** Mirrors vc.Anim.Debug into the engine's foot placement debug draw. */
	void OnAnimDebugChanged(IConsoleVariable* Variable)
	{
		if (IConsoleVariable* FootPlacementDebug = IConsoleManager::Get().FindConsoleVariable(TEXT("a.AnimNode.FootPlacement.Debug")))
		{
			FootPlacementDebug->Set(Variable->GetInt(), ECVF_SetByConsole);
		}
	}

	TAutoConsoleVariable<int32> CVarAnimDebug(
		TEXT("vc.Anim.Debug"), 0,
		TEXT("1 draws each character's look-at target and IK values, and turns on the engine's foot placement debug draw (a.AnimNode.FootPlacement.Debug); 0 off."),
		FConsoleVariableDelegate::CreateStatic(&OnAnimDebugChanged),
		ECVF_Default);
}

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
	if (CVarAnimDebug.GetValueOnGameThread() != 0)
	{
		DrawDebug();
	}
}

float UVCAnimInstance::ComputeLocomotionPlayRate(float InSpeed, float ReferenceSpeed)
{
	if (ReferenceSpeed <= UE_KINDA_SMALL_NUMBER)
	{
		return 1.f;
	}
	return FMath::Max(InSpeed / ReferenceSpeed, 1.f);
}

void UVCAnimInstance::DrawDebug() const
{
	const APawn* Pawn = TryGetPawnOwner();
	UWorld* World = Pawn ? Pawn->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	// The values are last update's: the thread-safe update writes them after this.
	if (LookAtAlpha > 0.f)
	{
		DrawDebugSphere(World, LookAtLocation, 12.f, 8, FColor::Cyan, false, -1.f);
		DrawDebugLine(World, GatheredInput.EyeLocation, LookAtLocation, FColor::Cyan, false, -1.f);
	}
	const FString Text = FString::Printf(TEXT("foot %.2f  pace x%.2f  look %.2f  hand %.2f"), FootIKAlpha, LocomotionPlayRate,
		LookAtAlpha, HandIKAlpha);
	DrawDebugString(World, Pawn->GetActorLocation() - GatheredInput.GravityDirection * 110.f, Text, nullptr, FColor::White, 0.f, true);
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
	OutInput.GroundFriction = Movement->GroundFriction;
	OutInput.ActorRotation = Character->GetActorQuat();

	// A remote character has no controller here: its base aim rotation carries the replicated view pitch.
	const AController* Controller = Character->GetController();
	OutInput.ControlRotation = Controller ? Controller->GetControlRotation() : Character->GetBaseAimRotation();

	OutInput.bOnGround = Movement->IsMovingOnGround();
	OutInput.bFalling = Movement->IsFalling();
	OutInput.bCrouching = Movement->IsCrouching();
	OutInput.bSwimming = Movement->IsSwimming();
	OutInput.bFlying = Movement->IsFlying();
	OutInput.MaxSpeed = Movement->GetMaxSpeed();
	OutInput.bOrientToMovement = Movement->bOrientRotationToMovement;

	// The head's target: an AI controller's focus (none when it has no focus), else the view's aim ahead.
	OutInput.EyeLocation = Character->GetPawnViewLocation();
	if (const AAIController* AI = Cast<AAIController>(Controller))
	{
		OutInput.LookTarget = AI->GetFocalPoint();
		OutInput.bHasLookTarget = FAISystem::IsValidLocation(OutInput.LookTarget);
	}
	else
	{
		OutInput.LookTarget = OutInput.EyeLocation + OutInput.ControlRotation.Vector() * LookAtDistance;
		OutInput.bHasLookTarget = true;
	}

	if (!CachedMontages.IsValid())
	{
		CachedMontages = Character->FindComponentByClass<UVCActionMontageComponent>();
	}
	if (const UVCActionAnimSet* Set = CachedMontages.IsValid() ? CachedMontages->GetCurrentActionAnimSet() : nullptr)
	{
		OutInput.bOffHandIK = Set->bOffHandIK;
		OutInput.OffHandGrip = Set->OffHandGrip;
		OutInput.LocomotionClips = Set->LocomotionClips;
	}
	if (const UVCActionAnimSet* Base = CachedMontages.IsValid()
		? CachedMontages->FindActionAnimSet(VCGameplayTags::Equipment_AnimSet_Unarmed.GetTag()) : nullptr)
	{
		OutInput.BaseLocomotionClips = Base->LocomotionClips;
	}

	if (!CachedAppearance.IsValid())
	{
		CachedAppearance = Character->FindComponentByClass<UVCAppearanceComponent>();
	}
	OutInput.bFootBonesOnly = CachedAppearance.IsValid() && !CachedAppearance->HasIKFootBones();

	OutInput.StanceMode = CVarAnimStance.GetValueOnGameThread();
	OutInput.bFootIKEnabled = CVarAnimFootIK.GetValueOnGameThread() != 0;
	OutInput.bSprintPaceEnabled = CVarAnimSprintPace.GetValueOnGameThread() != 0;
	OutInput.bLookAtEnabled = CVarAnimLookAt.GetValueOnGameThread() != 0;
	OutInput.bHandIKEnabled = CVarAnimHandIK.GetValueOnGameThread() != 0;
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
		OutInput.Gait = VCMovement->GetGait();
		OutInput.SurfaceType = VCMovement->CurrentSurfaceType;
		OutInput.WaterDepth = VCMovement->GetTerrainContext().WaterDepth;
	}

	if (const IVCAnimatedCharacter* Animated = Cast<IVCAnimatedCharacter>(Character))
	{
		OutInput.ViewMode = Animated->GetAnimViewMode();
		OutInput.ActionSet = Animated->GetAnimActionSet();
		OutInput.LocomotionSet = Animated->GetAnimLocomotionSet();
		OutInput.bAiming = Animated->IsAnimAiming();
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
	const float RecoverySpeed = GetTurnInPlaceRecoverySpeed(Input);
	if (!bStanding && RecoverySpeed >= 0.f)
	{
		Offset = FMath::FInterpTo(Offset, 0.0, DeltaSeconds, RecoverySpeed);
	}
	Offset = AdjustRootYawOffset(Input, Offset, bStanding);
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
	ViewYaw = FRotator::NormalizeAxis(AimRelative.Yaw);

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

	// --- IK and pace: each alpha eases toward on or off, so a landing or a change of hands blends ---
	const auto Ease = [this, DeltaSeconds](float Current, bool bOn)
	{
		return FMath::FInterpConstantTo(Current, bOn ? 1.f : 0.f, DeltaSeconds, IKBlendSpeed);
	};
	const bool bAble = !Input.bDead && !Input.bDowned;
	FootIKAlpha = Ease(FootIKAlpha, Input.bFootIKEnabled && bAble && Input.bOnGround && !Input.bSwimming && !Input.bFlying);
	FootBoneIKAlpha = Input.bFootBonesOnly ? FootIKAlpha : 0.f;

	// The held stance over the upper body (C2): full on the ground, part of it at a sprint, none in the air or the water,
	// dead or downed, or switched off; eased so a jump or a sprint never snaps the arms.
	StanceMode = Input.StanceMode;
	const bool bStanceOn = Input.StanceMode != 0 && bAble && Input.bOnGround && !Input.bSwimming && !Input.bFlying;
	const float StanceTarget = GetStanceTarget(Input, !bStanceOn ? 0.f : (Input.bSprinting ? StanceSprintWeight : 1.f));
	StanceWeight = FMath::FInterpConstantTo(StanceWeight, StanceTarget, DeltaSeconds, IKBlendSpeed);
	// The aim raises the bow over AimRaiseTime and lowers it as fast (the user's flow: aim raises, the draw pulls).
	AimRaiseWeight = FMath::FInterpConstantTo(AimRaiseWeight, Input.bAiming && bAble ? 1.f : 0.f, DeltaSeconds, 1.f / AimRaiseTime);
	const UVCLocomotionAnimSet* Locomotion = Input.LocomotionSet;
	const float ReferenceSpeed = Locomotion && Locomotion->ReferenceSpeed > 0.f ? Locomotion->ReferenceSpeed : LocomotionReferenceSpeed;
	LocomotionPlayRate = Input.bSprintPaceEnabled && Input.bOnGround ? ComputeLocomotionPlayRate(Speed, ReferenceSpeed) : 1.f;

	// --- A body's own locomotion and its idle variant (character animation P7): the variant takes turns with the idle
	// while the body stands, so a herd that stopped at different times grazes at different times ---
	LocomotionBlendSpace = Locomotion ? Locomotion->Locomotion : nullptr;
	IdleVariantAnim = Locomotion ? Locomotion->IdleVariant : nullptr;
	const bool bStandingStill = bAble && Input.bOnGround && Speed < MovingSpeedThreshold;
	StandingSeconds = bStandingStill ? StandingSeconds + DeltaSeconds : 0.f;
	bIdleVariant = false;
	if (Locomotion && Locomotion->IdleVariant && bStandingStill)
	{
		const float Cycle = Locomotion->IdleVariantAfter + Locomotion->IdleVariantFor;
		bIdleVariant = Cycle > 0.f && FMath::Fmod(StandingSeconds, Cycle) >= Locomotion->IdleVariantAfter;
	}

	// The look-at fades for a target behind the body; the node itself clamps how far the head turns.
	bool bLookTargetAhead = false;
	if (Input.bHasLookTarget)
	{
		LookAtLocation = Input.LookTarget;
		const FVector ToTarget = Horizontal(Input.LookTarget - Input.EyeLocation, Up);
		bLookTargetAhead = ToTarget.IsNearlyZero() || FMath::Abs(SignedAngleAbout(Forward, ToTarget, Up)) <= LookAtMaxAngle;
	}
	LookAtAlpha = Ease(LookAtAlpha, Input.bLookAtEnabled && bAble && bLookTargetAhead && Input.ViewMode != EVCViewMode::FirstPerson);

	HandIKAlpha = Ease(HandIKAlpha, Input.bHandIKEnabled && bAble && Input.bOffHandIK);
	if (Input.bOffHandIK)
	{
		HandIKEffector = Input.OffHandGrip;
	}

	LastFacing = Forward;
	bHasKeptState = true;
}
