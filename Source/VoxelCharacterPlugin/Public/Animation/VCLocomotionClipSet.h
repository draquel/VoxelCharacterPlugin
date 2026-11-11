// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VCLocomotionClipSet.generated.h"

class UAnimSequence;
class UAnimSequenceBase;
class UBlendSpace;

/** A locomotion clip family by pace (the locomotion state machine plan's L1): the walk, the jog and the run loops. */
UENUM(BlueprintType)
enum class EVCLocomotionGait : uint8
{
	Walk,
	Jog,
	Run,
};

/** The eight directions a pack's loops are authored in, against the facing. A pack may have six (no forward diagonals). */
UENUM(BlueprintType)
enum class EVCLocomotionDirection : uint8
{
	Forward,
	ForwardRight,
	Right,
	BackwardRight,
	Backward,
	BackwardLeft,
	Left,
	ForwardLeft,
};

/** One gait's loops in each direction and the speed they are authored at. An empty direction is one the pack lacks. */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCLocomotionLoops
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequence> Forward;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequence> ForwardRight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequence> Right;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequence> BackwardRight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequence> Backward;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequence> BackwardLeft;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequence> Left;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequence> ForwardLeft;

	/** The speed the loops' root moves at (uu/s; the locomotion plan's L0 measured each pack's): 0 for a gait the set lacks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation", meta = (ClampMin = "0"))
	float AuthoredSpeed = 0.f;

	/**
	 * The loop of one direction.
	 * @param Direction The direction
	 * @return Its clip, or null when the pack has none for it
	 */
	UAnimSequence* Get(EVCLocomotionDirection Direction) const;

	/** @return Whether the gait has a clip in any direction and an authored speed */
	bool HasAny() const;
};

/**
 * The full-body locomotion of one action set (the locomotion state machine plan's D87): its idle, its loops by gait and
 * direction, its jump, fall and landings, and the speeds its gait table runs at (D91). ABP_VCPlayerLocomotion plays the
 * held set's through UVCLocomotionAnimInstance; a family the set lacks plays the base set's (Unarmed's, D88). A new pack
 * is a new asset of this class, not a new graph. Scripts/setup_character_state_machine.py builds them and each one's
 * Cycle blend space from GetCycleSamples.
 */
UCLASS(BlueprintType)
class VOXELCHARACTERPLUGIN_API UVCLocomotionClipSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Standing still. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequence> Idle;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	FVCLocomotionLoops Walk;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	FVCLocomotionLoops Jog;

	/** The run loops (the packs' have a forward run only). Not in the Cycle blend space yet: the sprint is the plan's L2. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	FVCLocomotionLoops Run;

	/**
	 * The loops by local direction (X, degrees, positive to the right) and speed across local up (Y, uu/s): the idle at 0,
	 * each gait of GetCycleSamples at its authored speed. Built by the script from the loops above.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UBlendSpace> Cycle;

	/**
	 * The walk while aiming a ranged weapon (the bow's hold C, D92: side-on to the aim as an archer stands, so the hips
	 * turn with the drawn shoulders), in the directions it has; empty for a set that aims no weapon.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Aim")
	FVCLocomotionLoops AimWalk;

	/** Standing while aiming (the bow's C idle); empty plays Idle at the aim cycle's speed 0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Aim")
	TObjectPtr<UAnimSequence> AimIdle;

	/**
	 * The cycle while aiming: GetCycleSamples(true), AimWalk in the walk's place and the Jog above it (a run while drawn
	 * falls back to the jog, D88). Built by the script; empty for a set without AimWalk.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Aim")
	TObjectPtr<UBlendSpace> AimCycle;

	/** Leaving the ground upward. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequenceBase> JumpStart;

	/** Falling, looped. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequenceBase> FallLoop;

	/** Landing standing still. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequenceBase> Land;

	/** Landing on the move, into the jog; empty plays Land. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequenceBase> LandMoving;

	/**
	 * The gait table's speed caps for this set (uu/s; D91: retuned around the clips within a play-rate band, walk 120, run
	 * 400 and sprint 700 to start for the packs). Data for the plan's L2, which applies and tunes them; the gait table
	 * does not read them yet.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Speeds", meta = (ClampMin = "0"))
	float WalkSpeed = 120.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Speeds", meta = (ClampMin = "0"))
	float RunSpeed = 400.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Speeds", meta = (ClampMin = "0"))
	float SprintSpeed = 700.f;

	/**
	 * The direction of a loop for a local direction.
	 * @param Degrees The velocity's angle from the facing (positive to the right), any range
	 * @return The nearest of the eight directions
	 */
	static EVCLocomotionDirection DirectionFromAngle(float Degrees);

	/**
	 * The angle of a direction, as the Cycle blend space places it.
	 * @param Direction The direction
	 * @return Degrees from the facing, positive to the right, -135 to 180 (Backward is 180)
	 */
	static float AngleOfDirection(EVCLocomotionDirection Direction);

	/**
	 * One gait's loops.
	 * @param Gait The gait
	 * @return Walk, Jog or Run
	 */
	const FVCLocomotionLoops& GetLoops(EVCLocomotionGait Gait) const;

	/**
	 * The loop that plays for a gait and a local direction: the gait's clip nearest the direction in angle (a pack without
	 * forward diagonals plays its forward or its strafe); a gait the set lacks gives the nearest gait it has, slower first
	 * (Run plays the jog, the walk plays the jog when the set has no walk).
	 * @param Gait    The pace
	 * @param Degrees The velocity's angle from the facing (positive to the right)
	 * @return The clip, or null when the set has no loops at all
	 */
	UAnimSequence* ChooseLoop(EVCLocomotionGait Gait, float Degrees) const;

	/** @return Whether the set has its own locomotion: an idle and a Cycle (else the base set's plays, D88) */
	bool HasOwnLocomotion() const { return Idle != nullptr && Cycle != nullptr; }

	/** @return The fastest authored speed among the gaits the Cycle blend space holds (Walk, Jog), 0 with none */
	float GetCycleTopSpeed() const;

	/**
	 * A cycle blend space's samples, so the script builds it from this asset's clips (the locomotion plan's L1): the idle
	 * at speed 0 at every direction used, then each of Walk and Jog with an authored speed at (its direction's angle, that
	 * speed); Backward is placed at both -180 and 180 so the axis wraps.
	 * @param bAim         The aim cycle's: AimWalk in the walk's place and AimIdle as the idle, where the set has them
	 * @param OutClips     The samples' clips
	 * @param OutPositions Each sample's (direction, speed)
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Animation")
	void GetCycleSamples(bool bAim, TArray<UAnimSequence*>& OutClips, TArray<FVector2D>& OutPositions) const;
};
