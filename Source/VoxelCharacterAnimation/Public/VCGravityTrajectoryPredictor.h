// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Animation/TrajectoryTypes.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"
#include "PoseSearch/PoseSearchTrajectoryPredictor.h"
#include "VCGravityTrajectoryPredictor.generated.h"

class ACharacter;

/**
 * The state one trajectory prediction starts from, world space, gathered on the game thread so the prediction can run on
 * the animation worker (character animation P5).
 */
struct FVCTrajectoryPredictionState
{
	/** The mesh component's location and rotation (motion matching works in the mesh's space). */
	FVector Position = FVector::ZeroVector;
	FQuat Facing = FQuat::Identity;

	/** The mesh's rotation relative to the capsule (the mannequins' -90 yaw). */
	FQuat MeshRelativeRotation = FQuat::Identity;

	FVector Velocity = FVector::ZeroVector;
	FVector Acceleration = FVector::ZeroVector;

	/** Local up: minus the movement component's gravity direction (world Z without custom gravity). */
	FVector Up = FVector::UpVector;

	/** Gravity as an acceleration (the direction times the magnitude), as the predictor interface returns it. */
	FVector GravityAccel = FVector(0.0, 0.0, -980.0);

	/** The controller's and the capsule's turn rates about Up, in degrees per second. */
	float ControllerYawRateDegrees = 0.f;
	float ActorYawRateDegrees = 0.f;

	bool bOrientRotationToMovement = false;
	bool bGrounded = true;

	/** Speeds, friction and braking from the movement component (the engine's own derivation). */
	FPoseSearchTrajectoryData::FDerived Derived;
};

/**
 * A motion-matching trajectory predictor that works in the gravity frame, so a planet's surface is ground (character
 * animation P5; spike S1 proved it, CHARACTER_ANIMATION_PLAN.md).
 *
 * The engine's character-movement prediction (UPoseSearchTrajectoryLibrary) turns the prediction about world Z, builds
 * the facing toward the acceleration against world up (FVector::ToOrientationQuat) and applies the mesh's relative
 * rotation to a world vector; each is right only when up is world Z. This predictor reuses the engine's speed and braking
 * step (FPoseSearchTrajectoryData::StepCharacterMovementGroundPrediction) and turns about local up instead. With up =
 * world Z it is the engine's prediction (test VoxelCharacter.Anim.Trajectory.MatchesEngineOnPlane).
 *
 * Use: Capture on the game thread each update, then PoseSearchGenerateTransformTrajectoryWithPredictor with this object on
 * the animation worker (UVCMotionMatchingAnimInstance does both).
 */
UCLASS()
class VOXELCHARACTERANIMATION_API UVCGravityTrajectoryPredictor : public UObject, public IPoseSearchTrajectoryPredictorInterface
{
	GENERATED_BODY()

public:
	/**
	 * Gathers the state the next prediction starts from (game thread).
	 * @param Character         The animated character
	 * @param DeltaSeconds      The update's delta time
	 * @param InTrajectoryData  The prediction's tuning (the AnimBP's)
	 */
	void Capture(const ACharacter* Character, float DeltaSeconds, const FPoseSearchTrajectoryData& InTrajectoryData);

	/**
	 * Pure: predicts the future samples of a trajectory from a state.
	 * @param State                       The state at the current sample
	 * @param TrajectoryData              The tuning (rotation speed toward the movement, remapping curves)
	 * @param bGravityFrame               True: turn about State.Up; false: the engine's world-Z arithmetic (the A/B reference)
	 * @param InOutTrajectory             History, current and prediction samples; the prediction ones are written
	 * @param NumPredictionSamples        Samples after the current one
	 * @param SecondsPerPredictionSample  Time between prediction samples
	 * @param NumHistorySamples           Samples before the current one (the current sample is at this index)
	 */
	static void PredictSamples(const FVCTrajectoryPredictionState& State, const FPoseSearchTrajectoryData& TrajectoryData,
		bool bGravityFrame, FTransformTrajectory& InOutTrajectory, int32 NumPredictionSamples,
		float SecondsPerPredictionSample, int32 NumHistorySamples);

	/**
	 * Pure: the signed angle from one direction to another about an axis, both projected on the axis's plane.
	 * @param From  The direction measured from
	 * @param To    The direction measured to
	 * @param Axis  The axis turned about
	 * @return Degrees, positive counter-clockwise looking down the axis; 0 when either projection is degenerate
	 */
	static float SignedAngleAboutAxisDegrees(const FVector& From, const FVector& To, const FVector& Axis);

	/**
	 * Sets the prediction's tuning and whether it turns about local up (on the animation worker, before the generation).
	 * @param InTrajectoryData  The tuning the engine's own generation would use
	 * @param bInGravityFrame   False predicts with the engine's world-Z arithmetic (vc.Anim.GravityTrajectory 0)
	 */
	void SetPrediction(const FPoseSearchTrajectoryData& InTrajectoryData, bool bInGravityFrame);

	/** @return A copy of the last predicted trajectory (for the debug draw on the game thread) */
	FTransformTrajectory GetLastTrajectory() const;

	/** @return The state the last prediction started from */
	const FVCTrajectoryPredictionState& GetState() const { return State; }

	// IPoseSearchTrajectoryPredictorInterface
	virtual void Predict(FTransformTrajectory& InOutTrajectory, int32 NumPredictionSamples, float SecondsPerPredictionSample, int32 NumHistorySamples) override;
	virtual void GetGravity(FVector& OutGravityAccel) override;
	virtual void GetCurrentState(FVector& OutPosition, FQuat& OutFacing, FVector& OutVelocity) override;
	virtual void GetVelocity(FVector& OutVelocity) override;
	PRAGMA_DISABLE_EXPERIMENTAL_WARNINGS
	virtual void GetAngularVelocity(FVector& OutAngularVelocityDegrees) override;
	PRAGMA_ENABLE_EXPERIMENTAL_WARNINGS

private:
	FPoseSearchTrajectoryData TrajectoryData;
	bool bGravityFrame = true;
	FVCTrajectoryPredictionState State;

	/** Last update's control and capsule forward vectors, world space (re-projected each update: no yaw is kept). */
	FVector LastControlForward = FVector::ZeroVector;
	FVector LastActorForward = FVector::ZeroVector;
	float LastControllerYaw = 0.f;

	mutable FCriticalSection LastTrajectoryLock;
	FTransformTrajectory LastTrajectory;
};
