// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VCLocomotionClipSet.generated.h"

class UAnimSequence;
class UAnimSequenceBase;
class UBlendSpace;
class UBlendSpace1D;

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

/** A set's turns in place (the locomotion plan's L2): the packs' 90 and 180 degree turns, their whole turn in the root's yaw. */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCLocomotionTurns
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequence> Left90;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequence> Right90;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequence> Left180;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequence> Right180;

	/** @return Whether the set has any turn */
	bool HasAny() const { return Left90 || Right90 || Left180 || Right180; }
};

/** A start, a stop or a turn the graph plays, and how (UVCLocomotionClipSet::ChooseStart, ChooseStop, ChooseTurn). */
struct FVCLocomotionPick
{
	/** The clip; null when the set has none for it (the graph inertializes between the states instead, D88). */
	UAnimSequence* Clip = nullptr;

	/** The clip's play rate: the pace its gait's loops play at, so a start or a stop keeps the cycle's steps. */
	float PlayRate = 1.f;

	/** A turn's angle in degrees, signed (positive to the right); 0 for a start or a stop. */
	float TurnDegrees = 0.f;
};

/** The cycle's pace at a speed (UVCLocomotionClipSet::ComputePace): the speed its blend space reads and the play rate. */
struct FVCLocomotionPace
{
	/** The speed the blend space is given (uu/s): an authored speed, so a gait's loops play whole, not blended toward the next gait. */
	float BlendSpeed = 0.f;

	/** The play rate that keeps the clips' feet with the ground at the real speed. */
	float PlayRate = 1.f;
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

	/** The run loops (the packs' have a forward run only: Run_01). The sprint plays it below RunFast (D91). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	FVCLocomotionLoops Run;

	/** The fast run (the packs' Run_02): the sprint blends from Run to it by speed (D91). Empty: Run alone. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	FVCLocomotionLoops RunFast;

	/**
	 * The sprint: Run's and RunFast's forward loops along speed at their authored speeds (GetSprintSamples). Built by the
	 * script; empty: the sprint plays the Cycle faster (§3.3).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UBlendSpace1D> Sprint;

	/** The starts into the walk by direction (the locomotion plan's L2). Empty: the jog's at the walk's rate (D88). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Transitions")
	FVCLocomotionLoops WalkStart;

	/** The starts into the jog by direction. Empty: Idle inertializes into the Cycle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Transitions")
	FVCLocomotionLoops JogStart;

	/** The stops from the walk by direction. Empty: the jog's at the walk's rate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Transitions")
	FVCLocomotionLoops WalkStop;

	/** The stops from the jog by direction. Empty: the Cycle inertializes into Idle. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Transitions")
	FVCLocomotionLoops JogStop;

	/** The stop from the run (the packs': forward only, Run_Stop). Empty: the jog's. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Transitions")
	FVCLocomotionLoops RunStop;

	/** The turns in place. Empty: the procedural turn (the body eases round to the facing, D88). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Transitions")
	FVCLocomotionTurns Turns;

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

	/** The aim walk's starts and stops (the bow's C), in the directions it has; empty: the walk's. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Aim")
	FVCLocomotionLoops AimWalkStart;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Aim")
	FVCLocomotionLoops AimWalkStop;

	/** Standing while aiming (the bow's C idle); empty plays Idle at the aim cycle's speed 0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Aim")
	TObjectPtr<UAnimSequence> AimIdle;

	/**
	 * The cycle while aiming: GetCycleSamples(true), AimWalk in the walk's place and the Jog above it (a run while drawn
	 * falls back to the jog, D88). Built by the script; empty for a set without AimWalk.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Aim")
	TObjectPtr<UBlendSpace> AimCycle;

	/** Crouched at a stand (the bow's hold B: Crouch_Loop). With CrouchCycle, the set's own crouch (HasCrouch). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Crouch")
	TObjectPtr<UAnimSequence> CrouchIdle;

	/** Into the crouch at a stand and back up (the pack's Crouch_Begin, Crouch_End). Empty: the idles blend (D88). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Crouch")
	TObjectPtr<UAnimSequence> CrouchBegin;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Crouch")
	TObjectPtr<UAnimSequence> CrouchEnd;

	/** The slow crouch walk by direction (the pack's Crouch_Walk_Slow, 114 uu/s): the crouch's first gait. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Crouch")
	FVCLocomotionLoops CrouchWalkSlow;

	/** The crouch walk by direction (the pack's Crouch_Walk, 302 uu/s): the crouch's second, at CrouchSpeed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Crouch")
	FVCLocomotionLoops CrouchWalk;

	/** Each crouch gait's starts and stops by direction (ChooseCrouchStart, ChooseCrouchStop). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Crouch")
	FVCLocomotionLoops CrouchWalkSlowStart;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Crouch")
	FVCLocomotionLoops CrouchWalkStart;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Crouch")
	FVCLocomotionLoops CrouchWalkSlowStop;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Crouch")
	FVCLocomotionLoops CrouchWalkStop;

	/**
	 * Crouched while aiming (the bow's hold C, side-on, D92): its slow crouch walk, starts and stops in the directions it
	 * has, and its crouch idle; empty: the crouch's own.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Crouch")
	FVCLocomotionLoops AimCrouchWalk;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Crouch")
	FVCLocomotionLoops AimCrouchWalkStart;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Crouch")
	FVCLocomotionLoops AimCrouchWalkStop;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Crouch")
	TObjectPtr<UAnimSequence> AimCrouchIdle;

	/** The crouch's loops by local direction and speed: GetCrouchCycleSamples(false). Built by the script. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Crouch")
	TObjectPtr<UBlendSpace> CrouchCycle;

	/** The same while aiming: GetCrouchCycleSamples(true), AimCrouchWalk in the slow walk's place. Built by the script. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Crouch")
	TObjectPtr<UBlendSpace> AimCrouchCycle;

	/**
	 * A set with no crouch of its own crouches on this set's (§3.3: the borrowed set's crouch on the legs, the graph laying
	 * this set's idle over the upper body on the stance's ramp); empty: the crouch plays the walk's loops.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Crouch")
	TObjectPtr<UVCLocomotionClipSet> BorrowedCrouch;

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
	 * The gait table's forward speed caps for this set (uu/s; D91: retuned around the clips within a play-rate band, walk
	 * 120, run 400 and sprint 700 on the packs' sets). While the player plays the state machine the movement's walk, run
	 * and sprint rows scale to them (UVCMovementComponent::SetClipGaitCaps, the locomotion plan's L2), and each gait's
	 * loops play at its cap over their authored speed (ComputePace).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Speeds", meta = (ClampMin = "0"))
	float WalkSpeed = 120.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Speeds", meta = (ClampMin = "0"))
	float RunSpeed = 400.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Speeds", meta = (ClampMin = "0"))
	float SprintSpeed = 700.f;

	/**
	 * The crouch's cap (uu/s; the locomotion plan's L3): the movement's crouch row scales to the crouch set's
	 * (GetCrouchSet), and the crouch walk plays whole at it. CrouchSlowSpeed is where the slow crouch walk plays whole;
	 * between them the two blend (ComputeCrouchPace).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Speeds", meta = (ClampMin = "0"))
	float CrouchSpeed = 250.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Speeds", meta = (ClampMin = "0"))
	float CrouchSlowSpeed = 150.f;

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

	/** @return Whether the set has a sprint of its own (a Sprint blend space) */
	bool HasSprint() const { return Sprint != nullptr; }

	/**
	 * Pure: the pace at a speed from a ladder of (cap, authored speed) points ascending by cap (the locomotion plan's L2,
	 * D91). At a point the blend space reads the authored speed and the clips play at cap over authored; between two
	 * points both are interpolated, so the rate moves smoothly; past the last the clips play faster. With bFromRest the
	 * blend space has a rest at speed 0 (the Cycle's idle): below the first point it blends toward the rest at the first
	 * point's rate; without, the first point's clip plays slower.
	 * @param Speed     The speed across local up (uu/s)
	 * @param Points    (cap, authored speed) per gait, both above 0, ascending by cap
	 * @param bFromRest The blend space has a rest sample at speed 0
	 * @return The blend speed and the play rate; Speed and 1 with no points
	 */
	static FVCLocomotionPace ComputePace(float Speed, TConstArrayView<FVector2f> Points, bool bFromRest);

	/**
	 * The Cycle's pace (or the aim cycle's): ComputePace over the walk at WalkSpeed and the jog at RunSpeed, each at its
	 * loops' authored speed (the aim walk's in the walk's place with bAim), from rest.
	 * @param Speed The speed across local up (uu/s)
	 * @param bAim  The aim cycle's
	 * @return The pace
	 */
	FVCLocomotionPace ComputeCyclePace(float Speed, bool bAim) const;

	/**
	 * The sprint's pace: the Run and RunFast loops at their authored speeds (between them the blend on speed plays at 1).
	 * @param Speed The speed across local up (uu/s)
	 * @return The pace
	 */
	FVCLocomotionPace ComputeSprintPace(float Speed) const;

	/**
	 * The rate a gait's loops play at at its cap, which its starts and stops play at.
	 * @param Gait The gait: Walk at WalkSpeed, Jog at RunSpeed, Run (the sprint) at SprintSpeed
	 * @param bAim The aim walk's for the walk
	 * @return The rate; 1 when the set has no loops for it
	 */
	float GetGaitPlayRate(EVCLocomotionGait Gait, bool bAim) const;

	/**
	 * The start for a gait and a direction (the locomotion plan's L2, §3.3): the gait's own (the aim walk's with bAim and
	 * the set has them), the jog's for a walk without starts (at the walk's rate), the jog's for the run (a sprint from
	 * rest starts as a jog); none for a set without (the graph inertializes from Idle into the Cycle).
	 * @param Gait    The gait the character starts into
	 * @param Degrees The input's angle from the body's heading (positive to the right)
	 * @param bAim    The character aims
	 * @return The clip and its rate
	 */
	FVCLocomotionPick ChooseStart(EVCLocomotionGait Gait, float Degrees, bool bAim) const;

	/**
	 * The stop for a gait and a direction (§3.3): the gait's own, the run's for the run (forward only: off forward, the
	 * jog's), the jog's for a walk without stops (at the walk's rate); none for a set without.
	 * @param Gait    The gait the character stops from
	 * @param Degrees The velocity's angle from the body's heading (positive to the right)
	 * @param bAim    The character aims
	 * @return The clip and its rate
	 */
	FVCLocomotionPick ChooseStop(EVCLocomotionGait Gait, float Degrees, bool bAim) const;

	/**
	 * The turn in place toward the facing for a body offset from it (§3.1): the 180 from 135 degrees (or when the set
	 * has no 90 that way), else the 90; none for a set without turns (the procedural turn).
	 * @param OffsetDegrees The body's heading from the facing (UVCAnimInstance::RootYawOffset: negative when the facing
	 *                      has turned to the body's right, which a turn to the right takes back)
	 * @return The clip and its signed angle (positive: a turn to the right)
	 */
	FVCLocomotionPick ChooseTurn(float OffsetDegrees) const;

	/**
	 * The sprint blend space's samples: Run's and RunFast's forward loops at their authored speeds.
	 * @param OutClips     The samples' clips
	 * @param OutPositions Each sample's speed
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Animation")
	void GetSprintSamples(TArray<UAnimSequence*>& OutClips, TArray<float>& OutPositions) const;

	/**
	 * The root bone's turn about up in a clip since its start (the packs keep a turn's whole yaw in the root, L0).
	 * @param Clip The clip
	 * @param Time Seconds into it
	 * @return Degrees, positive to the right; 0 without a clip (or an empty one)
	 */
	static float RootYawAt(const UAnimSequenceBase* Clip, float Time);

	/**
	 * The root bone's travel across up in a clip since its start (the packs keep their root motion in the root, L0).
	 * @param Clip The clip
	 * @param Time Seconds into it
	 * @return Distance (uu); 0 without a clip (or an empty one)
	 */
	static float RootDistanceAt(const UAnimSequenceBase* Clip, float Time);

	/**
	 * Pure: where a stop is entered so that its remaining travel is the capsule's braking distance (the locomotion plan's
	 * L2: the stop lands within the gait table's distance; it plays by time from there). The travel is taken as
	 * non-decreasing; the earliest time whose remaining travel is no more than Remaining.
	 * @param DistanceAt The clip's travel since its start at a time
	 * @param Length     The clip's length (s)
	 * @param Remaining  The distance the capsule has left to brake (uu)
	 * @return Seconds into the clip; 0 when the whole clip travels no further than Remaining
	 */
	static float FindStopEntryTime(TFunctionRef<float(float)> DistanceAt, float Length, float Remaining);

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

	// ==================== The crouch (the locomotion plan's L3) ====================

	/** @return Whether the set has its own crouch: a crouch idle and a CrouchCycle */
	bool HasCrouch() const { return CrouchIdle != nullptr && CrouchCycle != nullptr; }

	/**
	 * The set whose crouch plays for this one (§3.3): its own, else BorrowedCrouch's when that has one.
	 * @param bBorrow Whether a borrowed crouch counts (vc.Anim.SM.BorrowCrouch)
	 * @return The set, or null when the crouch plays the walk's loops
	 */
	const UVCLocomotionClipSet* GetCrouchSet(bool bBorrow = true) const;

	/**
	 * The crouch's pace: ComputePace over the slow crouch walk at CrouchSlowSpeed and the crouch walk at CrouchSpeed (the
	 * aim crouch walk in the slow walk's place with bAim), from rest.
	 * @param Speed The speed across local up (uu/s)
	 * @param bAim  The aim crouch cycle's
	 * @return The pace
	 */
	FVCLocomotionPace ComputeCrouchPace(float Speed, bool bAim) const;

	/**
	 * The crouch start for a direction: the family of the crouch gait that plays at CrouchSpeed (the slow walk's when the
	 * pace there is nearer its authored speed; the aim crouch walk's with bAim on the slow walk), else the other, at the
	 * crouch's rate at its cap; none for a set without.
	 * @param Degrees The input's angle from the body's heading (positive to the right)
	 * @param bAim    The character aims
	 * @return The clip and its rate
	 */
	FVCLocomotionPick ChooseCrouchStart(float Degrees, bool bAim) const;

	/**
	 * The crouch stop for a direction, as ChooseCrouchStart.
	 * @param Degrees The velocity's angle from the body's heading (positive to the right)
	 * @param bAim    The character aims
	 * @return The clip and its rate
	 */
	FVCLocomotionPick ChooseCrouchStop(float Degrees, bool bAim) const;

	/**
	 * The crouch cycle's samples (the locomotion plan's L3), as GetCycleSamples: the crouch idle at speed 0, the slow
	 * crouch walk and the crouch walk at their authored speeds.
	 * @param bAim         The aim crouch cycle's: AimCrouchWalk in the slow walk's place and AimCrouchIdle as the idle
	 * @param OutClips     The samples' clips
	 * @param OutPositions Each sample's (direction, speed)
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Animation")
	void GetCrouchCycleSamples(bool bAim, TArray<UAnimSequence*>& OutClips, TArray<FVector2D>& OutPositions) const;

private:
	/** The crouch's slow row (the aim crouch walk with bAim when the set has it). */
	const FVCLocomotionLoops& CrouchSlowRow(bool bAim) const;

	/** Whether the crouch plays its slow walk at CrouchSpeed (the pace there nearer the slow walk's authored speed). */
	bool PlaysSlowCrouchAtCap(bool bAim) const;

	/** A cycle's samples: each row with an authored speed at (its direction's angle, that speed), the idle at 0. */
	static void BuildCycleSamples(UAnimSequence* IdleClip, std::initializer_list<const FVCLocomotionLoops*> Rows,
		TArray<UAnimSequence*>& OutClips, TArray<FVector2D>& OutPositions);
};
