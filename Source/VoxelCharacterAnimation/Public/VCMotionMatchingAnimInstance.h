// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/VCAnimInstance.h"
#include "Animation/TrajectoryTypes.h"
#include "PoseSearch/PoseSearchTrajectoryLibrary.h"
#include "VCMotionMatchingAnimInstance.generated.h"

class ACharacter;
class UVCGravityTrajectoryPredictor;

/**
 * The player's motion-matching anim instance (character animation P5): the C++ parent of the Game Animation Sample's CMC
 * AnimBP, which Scripts/setup_character_locomotion.py copies to /Game/CharacterAnimation/Locomotion.
 *
 * GASP's graph reads its character through one struct variable, CharacterProperties (S_CharacterPropertiesForAnimation),
 * which GASP's own pawn fills through a Blueprint interface. Here NativeUpdateAnimation fills it from any character on a
 * character movement component, field by field by name; the script empties the graph's own fill. The rest of GASP's
 * graph works in world space and takes world Z as up (VSizeXY, CalculateDirection, rotator deltas and yaw sums, the
 * engine's trajectory prediction); the script swaps those nodes for the functions below, which work in the gravity frame
 * and are the engine's own arithmetic where up is world Z. Everything UVCAnimInstance computes (the IK inputs, the action
 * set, the aim) is here too, for the layers and the IK the script adds.
 */
UCLASS()
class VOXELCHARACTERANIMATION_API UVCMotionMatchingAnimInstance : public UVCAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	// ==================== GASP's world-Z arithmetic, in the gravity frame ====================
	// Each takes the pins of the function it replaces, so the script swaps one node for another and keeps the links.

	/**
	 * VSizeXY in the gravity frame.
	 * @param A  A world vector
	 * @return Its length across local up
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Animation|MotionMatching", meta = (BlueprintThreadSafe))
	double GravityVSizeXY(FVector A) const;

	/**
	 * CalculateDirection in the gravity frame.
	 * @param Velocity      A world velocity
	 * @param BaseRotation  A world rotation
	 * @return The velocity's angle from the rotation's forward about local up (degrees, positive to the right)
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Animation|MotionMatching", meta = (BlueprintThreadSafe))
	float GravityCalculateDirection(const FVector& Velocity, const FRotator& BaseRotation) const;

	/**
	 * NormalizedDeltaRotator in the gravity frame.
	 * @param A  A world rotation
	 * @param B  A world rotation
	 * @return A - B with yaw about local up and pitch from the local horizontal, each normalized
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Animation|MotionMatching", meta = (BlueprintThreadSafe))
	FRotator GravityNormalizedDeltaRotator(FRotator A, FRotator B) const;

	/**
	 * A rotator's yaw plus an angle, the yaw taken about local up (the graph's sum of a world yaw and an offset).
	 * @param Rotation  A world rotation
	 * @param Yaw       Degrees to turn about local up
	 * @return The turned world rotation
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Animation|MotionMatching", meta = (BlueprintThreadSafe))
	FRotator GravityAddYaw(FRotator Rotation, float Yaw) const;

	/**
	 * A transform turned about local up, scale 1 (the root transform: the offset root bone's plus the mesh's 90 degrees).
	 * @param InTransform  A world transform
	 * @param Yaw          Degrees to turn about local up
	 * @return Its location with the turned rotation
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Animation|MotionMatching", meta = (BlueprintThreadSafe))
	FTransform GravityTransformAddYaw(const FTransform& InTransform, float Yaw) const;

	/**
	 * PoseSearchGenerateTransformTrajectory from the gravity-frame predictor (vc.Anim.GravityTrajectory 0: the engine's).
	 * @param InAnimInstance                       The anim instance (the engine's path reads its character)
	 * @param InTrajectoryData                     The prediction's tuning
	 * @param InDeltaTime                          The update's delta time
	 * @param InOutTrajectory                      The trajectory kept across updates (its history)
	 * @param InOutDesiredControllerYawLastUpdate  The engine's controller yaw kept across updates
	 * @param OutTrajectory                        The new trajectory
	 * @param InHistorySamplingInterval            Seconds between history samples
	 * @param InTrajectoryHistoryCount             History samples
	 * @param InPredictionSamplingInterval         Seconds between prediction samples
	 * @param InTrajectoryPredictionCount          Prediction samples
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Animation|MotionMatching", meta = (BlueprintThreadSafe))
	void GravityGenerateTransformTrajectory(const UObject* InAnimInstance, UPARAM(ref) const FPoseSearchTrajectoryData& InTrajectoryData,
		float InDeltaTime, UPARAM(ref) FTransformTrajectory& InOutTrajectory, UPARAM(ref) float& InOutDesiredControllerYawLastUpdate,
		FTransformTrajectory& OutTrajectory, float InHistorySamplingInterval = 0.04f, int32 InTrajectoryHistoryCount = 10,
		float InPredictionSamplingInterval = 0.2f, int32 InTrajectoryPredictionCount = 8);

	// ==================== Pure, for the functions above and the tests ====================

	/** @return Whether Up is world Z (then every function above is the engine's own arithmetic) */
	static bool IsWorldUp(const FVector& Up);

	/** @return A rotation taking world Z to Up (the frame the functions measure in; its yaw is arbitrary, the results are not) */
	static FQuat FrameBasis(const FVector& Up);

	/** @return The vector's length across Up */
	static double GroundSpeed(const FVector& Vector, const FVector& Up);

	/** @return CalculateDirection of the velocity and rotation, both expressed in Up's frame */
	static float DirectionInFrame(const FVector& Velocity, const FRotator& BaseRotation, const FVector& Up);

	/** @return NormalizedDeltaRotator of two rotations expressed in Up's frame */
	static FRotator DeltaRotatorInFrame(const FRotator& A, const FRotator& B, const FVector& Up);

	/** @return The rotation turned by YawDegrees about Up (on world Z: the Euler yaw sum) */
	static FRotator AddYawInFrame(const FRotator& Rotation, float YawDegrees, const FVector& Up);

	/** @return A world vector in Up's frame (its Z along Up) */
	static FVector ToFrame(const FVector& Vector, const FVector& Up);

	// ==================== State ====================

	/** Local up this update: minus the movement's gravity direction (the functions above measure against it). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|MotionMatching")
	FVector GravityUp = FVector::UpVector;

	/** How long after a landing GASP's graph is told the character just landed (seconds; GASP's pawn's delay). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|MotionMatching", meta = (ClampMin = "0"))
	float JustLandedDuration = 0.3f;

	/** A maximum speed at or below this is GASP's Walk gait (uu/s); above it Run, and Sprint while sprinting. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|MotionMatching", meta = (ClampMin = "0"))
	float WalkGaitMaxSpeed = 300.f;

	/** @return The gravity-frame predictor (created in NativeInitializeAnimation) */
	UVCGravityTrajectoryPredictor* GetGravityPredictor() const { return GravityPredictor; }

	/**
	 * What the graph's motion matching plays this update (vc.Anim.Debug draws it over the character).
	 * @return "Database / Animation (cost)"; empty when the graph has no motion matching node or it has not searched yet
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Animation|MotionMatching")
	FString GetMotionMatchingSelection() const;

	/** @return Whether the graph has every field of CharacterProperties this instance fills */
	bool HasCharacterProperties() const { return Fields.Root != nullptr && Fields.Missing.IsEmpty(); }

private:
	UPROPERTY(Transient)
	TObjectPtr<UVCGravityTrajectoryPredictor> GravityPredictor;

	/** GASP's CharacterProperties variable, its fields and the enum values it takes, found by name once. */
	struct FGASPFields
	{
		FStructProperty* Root = nullptr;
		FProperty* ActorTransform = nullptr;
		FProperty* Velocity = nullptr;
		FProperty* InputAcceleration = nullptr;
		FProperty* CurrentMaxAcceleration = nullptr;
		FProperty* CurrentMaxDeceleration = nullptr;
		FProperty* OrientationIntent = nullptr;
		FProperty* AimingRotation = nullptr;
		FProperty* JustLanded = nullptr;
		FProperty* LandVelocity = nullptr;
		FProperty* GroundNormal = nullptr;
		FProperty* Gait = nullptr;
		FProperty* MovementMode = nullptr;
		FProperty* RotationMode = nullptr;
		FProperty* Stance = nullptr;
		FStructProperty* InputState = nullptr;
		FProperty* WantsToSprint = nullptr;
		FProperty* WantsToWalk = nullptr;
		FProperty* WantsToStrafe = nullptr;
		FProperty* WantsToAim = nullptr;

		int64 GaitWalk = 0, GaitRun = 1, GaitSprint = 2;
		int64 ModeOnGround = 0, ModeInAir = 1;
		int64 RotationOrient = 0, RotationStrafe = 1;
		int64 StanceStand = 0, StanceCrouch = 1;

		/** The tunings GASP picks between by speed (TrajectoryGenerationData_Idle, _Moving), for the predictor's capture. */
		FStructProperty* TrajectoryDataIdle = nullptr;
		FStructProperty* TrajectoryDataMoving = nullptr;

		/** The graph's motion matching node (its last search: the database, the animation, the cost). */
		FStructProperty* MotionMatchingNode = nullptr;

		TArray<FString> Missing;
	};
	FGASPFields Fields;

	/** Finds the graph's variables (NativeInitializeAnimation). */
	void FindGASPFields();

	/** Fills CharacterProperties from the character (game thread, before the graph's update). */
	void FillCharacterProperties(const ACharacter& Character);

	/** vc.Anim.Debug: the trajectory motion matching receives (game thread). */
	void DrawTrajectory() const;

	/** The landing GASP's graph is told about: when, and the velocity in the gravity frame just before. */
	bool bWasFalling = false;
	FVector FallingVelocity = FVector::ZeroVector;
	double LandTime = -1.0;
	FVector LandVelocityInFrame = FVector::ZeroVector;
};
