// Copyright Daniel Raquel. All Rights Reserved.

#include "VCGravityTrajectoryPredictor.h"

#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace VCGravityTrajectoryPredictor
{
	/** The engine's magnitude remap (UPoseSearchTrajectoryLibrary::RemapVectorMagnitudeWithCurve, private there). */
	FVector Remap(const FVector& Vector, bool bUseCurve, const FRuntimeFloatCurve& Curve)
	{
		if (bUseCurve)
		{
			const double Length = Vector.Length();
			if (Length > UE_KINDA_SMALL_NUMBER)
			{
				return Vector * (Curve.GetRichCurveConst()->Eval(Length) / Length);
			}
		}
		return Vector;
	}
}

float UVCGravityTrajectoryPredictor::SignedAngleAboutAxisDegrees(const FVector& From, const FVector& To, const FVector& Axis)
{
	const FVector UnitAxis = Axis.GetSafeNormal();
	const FVector A = FVector::VectorPlaneProject(From, UnitAxis).GetSafeNormal();
	const FVector B = FVector::VectorPlaneProject(To, UnitAxis).GetSafeNormal();
	if (UnitAxis.IsZero() || A.IsZero() || B.IsZero())
	{
		return 0.f;
	}
	// Positive turns A toward B the way a positive yaw turns X toward Y about Z (FQuat(Axis, +angle) maps A onto B)
	const double Cos = FMath::Clamp(A | B, -1.0, 1.0);
	const double Sin = (A ^ B) | UnitAxis;
	return static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(Sin, Cos)));
}

void UVCGravityTrajectoryPredictor::Capture(const ACharacter* Character, float DeltaSeconds,
	const FPoseSearchTrajectoryData& InTrajectoryData)
{
	// The tuning member is the animation worker's (SetPrediction); the capture reads its own copy
	if (!Character)
	{
		return;
	}
	const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
	const USkeletalMeshComponent* Mesh = Character->GetMesh();
	if (!Movement || !Mesh)
	{
		return;
	}

	// Speeds, friction and braking as the engine derives them; its yaw rate is the world-Z one, kept for the A/B
	FPoseSearchTrajectoryData::FState EngineState;
	EngineState.DesiredControllerYawLastUpdate = LastControllerYaw;
	InTrajectoryData.UpdateData(DeltaSeconds, Character, State.Derived, EngineState);
	LastControllerYaw = EngineState.DesiredControllerYawLastUpdate;

	State.Position = Mesh->GetComponentLocation();
	State.Facing = Mesh->GetComponentQuat();
	State.MeshRelativeRotation = Mesh->GetRelativeRotation().Quaternion();
	State.Velocity = Movement->Velocity;
	State.Acceleration = Movement->GetCurrentAcceleration();
	State.Up = -Movement->GetGravityDirection();
	State.GravityAccel = Movement->GetGravityDirection() * -Movement->GetGravityZ();
	State.bOrientRotationToMovement = Movement->bOrientRotationToMovement;
	State.bGrounded = !Movement->IsFalling() && !Movement->IsFlying();

	// Turn rates about the current up from world vectors kept one update: no gravity-relative yaw survives a frame
	const FVector ControlForward = Character->GetViewRotation().Vector();
	const FVector ActorForward = Character->GetActorForwardVector();
	State.ControllerYawRateDegrees = 0.f;
	State.ActorYawRateDegrees = 0.f;
	if (DeltaSeconds > UE_SMALL_NUMBER)
	{
		if (!LastControlForward.IsZero())
		{
			State.ControllerYawRateDegrees = SignedAngleAboutAxisDegrees(LastControlForward, ControlForward, State.Up) / DeltaSeconds;
			if (InTrajectoryData.MaxControllerYawRate >= 0.f)
			{
				State.ControllerYawRateDegrees = FMath::Clamp(State.ControllerYawRateDegrees,
					-InTrajectoryData.MaxControllerYawRate, InTrajectoryData.MaxControllerYawRate);
			}
		}
		if (!LastActorForward.IsZero())
		{
			State.ActorYawRateDegrees = SignedAngleAboutAxisDegrees(LastActorForward, ActorForward, State.Up) / DeltaSeconds;
		}
	}
	LastControlForward = ControlForward;
	LastActorForward = ActorForward;
}

void UVCGravityTrajectoryPredictor::PredictSamples(const FVCTrajectoryPredictionState& InState,
	const FPoseSearchTrajectoryData& InTrajectoryData, bool bInGravityFrame, FTransformTrajectory& InOutTrajectory,
	int32 NumPredictionSamples, float SecondsPerPredictionSample, int32 NumHistorySamples)
{
	using namespace VCGravityTrajectoryPredictor;

	TArray<FTransformTrajectorySample>& Samples = InOutTrajectory.Samples;
	const int32 Current = NumHistorySamples;
	if (!Samples.IsValidIndex(Current))
	{
		return;
	}

	// The start, as UpdatePrediction_SimulateCharacterMovementInit makes it
	FVector Position = InState.Position;
	FQuat Facing = InState.Facing;
	FVector Velocity = Remap(InState.Velocity, InTrajectoryData.bUseSpeedRemappingCurve, InTrajectoryData.SpeedRemappingCurve);
	FVector Acceleration = Remap(InState.Acceleration, InTrajectoryData.bUseAccelerationRemappingCurve, InTrajectoryData.AccelerationRemappingCurve);
	if (InTrajectoryData.BendVelocityTowardsAcceleration > UE_KINDA_SMALL_NUMBER && !Acceleration.IsNearlyZero())
	{
		const double Speed = Velocity.Length();
		const FVector AlongAcceleration = Acceleration.GetUnsafeNormal() * Speed;
		if (InTrajectoryData.BendVelocityTowardsAcceleration < 1.f - UE_KINDA_SMALL_NUMBER)
		{
			Velocity = FMath::Lerp(Velocity, AlongAcceleration, InTrajectoryData.BendVelocityTowardsAcceleration);
			const double NewLength = Velocity.Length();
			if (NewLength > UE_KINDA_SMALL_NUMBER)
			{
				Velocity *= Speed / NewLength;
			}
		}
		else
		{
			Velocity = AlongAcceleration;
		}
	}

	FPoseSearchTrajectoryData::FDerived Derived = InState.Derived;
	Derived.bStepGroundPrediction = InState.bGrounded;
	Derived.bOrientRotationToMovement = InState.bOrientRotationToMovement;
	Derived.MeshCompRelativeRotation = InState.MeshRelativeRotation;

	const FVector Up = InState.Up.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	const float YawRateDegrees = bInGravityFrame ? InState.ControllerYawRateDegrees : Derived.ControllerYawRate;
	const FQuat ControllerRotationPerStep = bInGravityFrame
		? FQuat(Up, FMath::DegreesToRadians(YawRateDegrees * SecondsPerPredictionSample))
		: FQuat::MakeFromEuler(FVector(0.f, 0.f, YawRateDegrees * SecondsPerPredictionSample));

	float AccumulatedSeconds = Samples[Current].TimeInSeconds;
	const int32 Last = FMath::Min(Current + NumPredictionSamples, Samples.Num() - 1);
	for (int32 Index = Current + 1; Index <= Last; ++Index)
	{
		// UpdatePrediction_SimulateCharacterMovementStep, with the turning done about local up in the gravity frame
		Position += Velocity * SecondsPerPredictionSample;
		AccumulatedSeconds += SecondsPerPredictionSample;

		if (Derived.bStepGroundPrediction)
		{
			Acceleration = Remap(ControllerRotationPerStep * Acceleration,
				InTrajectoryData.bUseAccelerationRemappingCurve, InTrajectoryData.AccelerationRemappingCurve);
			Velocity = Remap(InTrajectoryData.StepCharacterMovementGroundPrediction(SecondsPerPredictionSample, Velocity, Acceleration, Derived),
				InTrajectoryData.bUseSpeedRemappingCurve, InTrajectoryData.SpeedRemappingCurve);

			Facing = ControllerRotationPerStep * Facing;
			if (Derived.bOrientRotationToMovement && !Acceleration.IsNearlyZero())
			{
				FQuat Target;
				if (bInGravityFrame)
				{
					// Face the acceleration on the ground plane, upright in the gravity frame, then the mesh's offset
					const FVector Direction = FVector::VectorPlaneProject(Acceleration, Up).GetSafeNormal();
					Target = Direction.IsZero()
						? Facing
						: FRotationMatrix::MakeFromXZ(Direction, Up).ToQuat() * Derived.MeshCompRelativeRotation;
				}
				else
				{
					// The engine's: a world vector turned by the relative rotation, oriented against world up
					Target = Derived.MeshCompRelativeRotation.RotateVector(Acceleration).ToOrientationQuat();
				}
				Facing = FMath::QInterpConstantTo(Facing, Target, SecondsPerPredictionSample, InTrajectoryData.RotateTowardsMovementSpeed);
			}
		}

		Samples[Index].Position = Position;
		Samples[Index].Facing = Facing;
		Samples[Index].TimeInSeconds = AccumulatedSeconds;
	}
}

void UVCGravityTrajectoryPredictor::SetPrediction(const FPoseSearchTrajectoryData& InTrajectoryData, bool bInGravityFrame)
{
	TrajectoryData = InTrajectoryData;
	bGravityFrame = bInGravityFrame;
}

void UVCGravityTrajectoryPredictor::Predict(FTransformTrajectory& InOutTrajectory, int32 NumPredictionSamples,
	float SecondsPerPredictionSample, int32 NumHistorySamples)
{
	PredictSamples(State, TrajectoryData, bGravityFrame, InOutTrajectory, NumPredictionSamples, SecondsPerPredictionSample,
		NumHistorySamples);

	FScopeLock Lock(&LastTrajectoryLock);
	LastTrajectory = InOutTrajectory;
}

FTransformTrajectory UVCGravityTrajectoryPredictor::GetLastTrajectory() const
{
	FScopeLock Lock(&LastTrajectoryLock);
	return LastTrajectory;
}

void UVCGravityTrajectoryPredictor::GetGravity(FVector& OutGravityAccel)
{
	OutGravityAccel = State.GravityAccel;
}

void UVCGravityTrajectoryPredictor::GetCurrentState(FVector& OutPosition, FQuat& OutFacing, FVector& OutVelocity)
{
	OutPosition = State.Position;
	OutFacing = State.Facing;
	OutVelocity = State.Velocity;
}

void UVCGravityTrajectoryPredictor::GetVelocity(FVector& OutVelocity)
{
	OutVelocity = State.Velocity;
}

PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS
void UVCGravityTrajectoryPredictor::GetAngularVelocity(FVector& OutAngularVelocityDegrees)
{
	// About local up: the history's ground correction takes the rest of the frame-to-frame turn (a planet's curvature
	// turns the frame as the pawn walks) as the ground's, and carries the history samples with it
	OutAngularVelocityDegrees = State.Up * State.ActorYawRateDegrees;
}
PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS
