// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/VCAnimInstance.h"
#include "Animation/VCLocomotionClipSet.h"
#include "VCLocomotionAnimInstance.generated.h"

class UAnimSequenceBase;
class UBlendSpace;

/** What the locomotion graph plays for a held set: each family from the held set, else from the base set (D88). */
struct FVCLocomotionClips
{
	TObjectPtr<UAnimSequenceBase> Idle;
	TObjectPtr<UBlendSpace> Cycle;

	/** The cycle while aiming (the held set's own only: the bow's hold C), or null. */
	TObjectPtr<UBlendSpace> AimCycle;

	TObjectPtr<UAnimSequenceBase> JumpStart;
	TObjectPtr<UAnimSequenceBase> FallLoop;
	TObjectPtr<UAnimSequenceBase> Land;
	TObjectPtr<UAnimSequenceBase> LandMoving;

	/**
	 * The set whose idle and cycle play (the held set when it has its own locomotion, else the base set): its starts,
	 * stops, turns, sprint and paces go with them (the locomotion plan's L2), so a set's legs never step another's.
	 */
	const UVCLocomotionClipSet* Loops = nullptr;

	/** The Cycle's fastest authored speed (uu/s): above it the loops play faster. */
	float CycleTopSpeed = 0.f;

	/** The held set has its own locomotion (an idle and a Cycle): its stance layer gives way to its clips. */
	bool bOwnLocomotion = false;

	/**
	 * The set whose crouch plays (the locomotion plan's L3): Loops' own, else the one it borrows (§3.3), else null (the
	 * crouch plays the walk's loops); its crouch idle, cycles, starts, stops, begin and end go with it.
	 */
	const UVCLocomotionClipSet* Crouch = nullptr;

	/** The crouch is borrowed: the graph lays Loops' idle over the upper body on the stance's ramp while crouched. */
	bool bBorrowedCrouch = false;
};

/**
 * The idle and the cycle the graph plays, in two slots: a set change writes the new set's clips into the slot not
 * playing and flips to it, and the graph's blend by bSlotB inertializes from one to the other, so the legs never snap
 * (a player node that changes its asset starts it afresh).
 */
struct FVCLocomotionSlots
{
	TObjectPtr<UAnimSequenceBase> IdleA;
	TObjectPtr<UAnimSequenceBase> IdleB;
	TObjectPtr<UBlendSpace> CycleA;
	TObjectPtr<UBlendSpace> CycleB;
	bool bSlotB = false;
	bool bFilled = false;
};

/**
 * The locomotion's states (the locomotion plan's §3.1). The anim instance steps the state (StepState) and the graph's
 * state machine follows it: each transition into a state reads that state's bool (bInIdle ...).
 */
UENUM(BlueprintType)
enum class EVCLocomotionState : uint8
{
	Idle,
	/** A turn in place, the set's clip turning the body to the facing. */
	TurnInPlace,
	/** From a stand into the cycle, the set's start for the gait and direction. */
	Start,
	Cycle,
	/** The run (the set's Run to RunFast by speed). */
	Sprint,
	/** Into the crouch or up from it at a stand (the set's crouch begin or end; the locomotion plan's L3). */
	CrouchChange,
	/** From the cycle to a stand, the set's stop for the gait and direction. */
	Stop,
	Jump,
	Fall,
	Land,
	Swim,
};

/** What StepState reads: this update's movement and what the held set has for it. */
struct FVCLocomotionStepInput
{
	bool bOnGround = false;
	bool bFalling = false;
	bool bSwimming = false;
	/** There is movement input. */
	bool bHasInput = false;
	float Speed = 0.f;
	float UpSpeed = 0.f;
	/** Seconds in the current state. */
	float StateSeconds = 0.f;

	/** The set has a start for this update's gait and direction (ChooseStart), a stop (ChooseStop). */
	bool bHasStart = false;
	bool bHasStop = false;

	/** Standing, the body is far enough off the facing to turn, and the set has the turn (ChooseTurn). */
	bool bWantsTurn = false;

	/** The sprint applies: the sprint gait, the set has a sprint, fast enough and running forward of the body. */
	bool bWantsSprint = false;

	/** Standing, the crouch changed and the crouch set has the begin or end for it (the locomotion plan's L3). */
	bool bWantsCrouchChange = false;

	/**
	 * A reversal: on the move, the input ReversalAngle or more from the velocity and a stop to play, so the cycle stops
	 * and starts again the other way (L3); bReversalStop: the playing stop began as one.
	 */
	bool bReversal = false;
	bool bReversalStop = false;

	/** The playing start, stop or turn has ended (its clip at its end), or the start is done another way. */
	bool bStartDone = false;
	bool bStopDone = false;
	bool bTurnDone = false;
	bool bCrouchChangeDone = false;

	/** Below it (uu/s, without input) the character stands. */
	float MovingSpeedThreshold = 10.f;
	/** A landing on the move holds this long before the cycle takes over. */
	float LandMinSeconds = 0.15f;
	/** The jump's and the landing's clip lengths less the transition's blend (s): the clip's end. */
	float JumpEndSeconds = 0.f;
	float LandEndSeconds = 0.f;
};

/**
 * The player's locomotion as a state machine of our own over the Essential Animation packs (the locomotion state machine
 * plan, D86-D92; vc.Anim.Locomotion 2): the parent of ABP_VCPlayerLocomotion, which Scripts/setup_character_state_machine.py
 * builds. It is a UVCAnimInstance, so the layers, the montages and the IK read the same values as under the other graphs.
 *
 * Each update it resolves the held set's UVCLocomotionClipSet (a family the set lacks from the base set's, D88), steps the
 * state (StepState), picks a start, stop or turn as its state begins and plays it by time (the packs have no distance
 * curves), computes the cycle's and the sprint's pace (D91), and gives the held stance a weight of 0 while the set plays
 * its own locomotion (its clips' arms show, D87). The body's heading off the facing (RootYawOffset) is the graph's root
 * yaw: a turn in place unwinds it with the clip's own root yaw, and on the move it eases out, so the capsule's instant
 * yaw (the gait table's) never snaps the body round.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCLocomotionAnimInstance : public UVCAnimInstance
{
	GENERATED_BODY()

public:
	UVCLocomotionAnimInstance();

	virtual void NativeInitializeAnimation() override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;

	/**
	 * Computes the locomotion's values (the clips, the state, the paces) after ComputeValues, from the same input. Touches
	 * only this instance; the tests call it directly.
	 * @param Input What the character's state was this update
	 */
	void ComputeLocomotion(const FVCAnimInput& Input);

	/**
	 * Pure: the clips for a held set (the locomotion plan's L1, D88): each family from the held set when it has it, else
	 * from the base set (Unarmed's); a set with no clip set plays the base set's whole.
	 * The crouch goes with the loops (L3): their set's own, else the one it borrows.
	 * @param Held          The held action set's clip set, or null
	 * @param Base          The base set's clip set, or null
	 * @param bBorrowCrouch Whether a borrowed crouch counts (vc.Anim.SM.BorrowCrouch)
	 * @return The clips; bOwnLocomotion when the held set has its own idle and Cycle
	 */
	static FVCLocomotionClips ResolveClips(const UVCLocomotionClipSet* Held, const UVCLocomotionClipSet* Base, bool bBorrowCrouch = true);

	/**
	 * Pure: the held stance's weight under this graph (D87): none while the set plays its own locomotion, else the base
	 * target (the stance over the base set's clips, as under the other graphs).
	 * @param BaseTarget      The stance target from the ground, the sprint and vc.Anim.Stance
	 * @param bOwnLocomotion  The held set has its own locomotion
	 * @return 0 or BaseTarget
	 */
	static float ComputeStanceTarget(float BaseTarget, bool bOwnLocomotion);

	/**
	 * Pure: whether the cycle plays the aim cycle (D92: the bow's hold C while raised or drawn, side-on with the drawn
	 * shoulders, so the hips do not twist away from them as the carry's strafes turn them).
	 * @param bAiming The character aims a ranged weapon (raised or drawn)
	 * @param Clips   This update's clips
	 * @return True when it aims and the held set has an aim cycle
	 */
	static bool UsesAimCycle(bool bAiming, const FVCLocomotionClips& Clips);

	/**
	 * Pure: a set change (the locomotion plan's L1). The first call fills both slots; later, when the idle or the cycle
	 * differs from the playing slot's, the new clips go into the other slot and it becomes the playing one.
	 * @param Slots The slots, changed in place
	 * @param Clips This update's clips
	 * @return True when the playing slot flipped (the graph inertializes to it)
	 */
	static bool ApplySetChange(FVCLocomotionSlots& Slots, const FVCLocomotionClips& Clips);

	/**
	 * Pure: the next state (D89: the choice in C++; the locomotion plan's L2 adds the start, the stop, the turn and the
	 * sprint). One step an update, along GetStateTransitions only, so the graph's state machine can follow it.
	 * @param Current The state now
	 * @param In      This update's movement and what the set has
	 * @return The state for this update (Current when nothing changes)
	 */
	static EVCLocomotionState StepState(EVCLocomotionState Current, const FVCLocomotionStepInput& In);

	/**
	 * The transitions StepState can take, which the graph's state machine has (the script builds them from this list).
	 * @param OutFrom Each transition's source state
	 * @param OutTo   Each transition's target state
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Animation|Locomotion")
	static void GetStateTransitions(TArray<FName>& OutFrom, TArray<FName>& OutTo);

	/**
	 * Whether the graph has a transition from one state to another (GetStateTransitions' list).
	 * @param From The source state
	 * @param To   The target state
	 * @return True for a transition in the list (and for From == To: no transition is needed)
	 */
	static bool IsStateTransition(EVCLocomotionState From, EVCLocomotionState To);

	/**
	 * Pure: the locomotion gait of the movement's gait (the walk's loops for the walk, and for the crouch of a set with no
	 * crouch set; the jog's for the run; the run's for the sprint).
	 * @param Gait The movement's gait
	 * @return The clips' gait
	 */
	static EVCLocomotionGait ToLocomotionGait(EVCGait Gait);

	/**
	 * Pure: the gait a stop is chosen for at a speed: the run's out of the sprint, else the jog's above halfway between the
	 * walk's cap and the run's, else the walk's.
	 * @param Set       The set whose loops play
	 * @param Speed     The speed across local up (uu/s)
	 * @param bSprinting The stop comes out of the sprint
	 * @return The gait
	 */
	static EVCLocomotionGait StopGaitAt(const UVCLocomotionClipSet* Set, float Speed, bool bSprinting);

	/**
	 * Pure: a tunable's value (the locomotion plan's L3): a vc.Anim.SM.* override of 0 or more, else the property.
	 * @param Override The cvar's value (negative: none)
	 * @param Property The anim instance's property
	 * @return The value in force
	 */
	static float ResolveTunable(float Override, float Property);

	/**
	 * Pure: the angle between the input and the velocity across local up (degrees, 0 to 180), for a reversal.
	 * @param LocalInput    The input's acceleration in the facing's frame
	 * @param LocalVelocity The velocity in the facing's frame
	 * @return The angle; 0 when either is nearly zero
	 */
	static float InputAngleToVelocity(const FVector& LocalInput, const FVector& LocalVelocity);

	/**
	 * Pure: how far the capsule travels from a reversal to its stop (the locomotion plan's L3): the movement turns the
	 * velocity toward an input straight against it at twice the friction's rate and adds the input's acceleration.
	 * @param Speed        The speed across local up (uu/s)
	 * @param Acceleration The input's acceleration (uu/s^2)
	 * @param Friction     The ground friction
	 * @return The distance (uu)
	 */
	static float ReversalStopDistance(float Speed, float Acceleration, float Friction);

	/** @return The locomotion's state as of the last update */
	EVCLocomotionState GetLocomotionState() const { return LocomotionState; }

	// ==================== What the graph plays ====================

	/** The idle of slot A and of slot B; bSlotB says which plays. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UAnimSequenceBase> IdleA;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UAnimSequenceBase> IdleB;

	/** The loops by direction and speed (UVCLocomotionClipSet::Cycle) of slot A and of slot B. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UBlendSpace> CycleA;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UBlendSpace> CycleB;

	/** Slot B plays (else A); flips at a set change. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bSlotB = false;

	/** The held set's cycle while aiming (UVCLocomotionClipSet::AimCycle), or null. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UBlendSpace> AimCycleClip;

	/** The Cycle state plays AimCycleClip (UsesAimCycle), else the slots' cycle; the switch is inertialized. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bAimCycle = false;

	/**
	 * The velocity's angle from the body (degrees, positive to the right): Direction less RootYawOffset, so while the
	 * body eases round after the capsule's instant turn the cycle steps the way the body moves. The cycles read it.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float CycleDirection = 0.f;

	/** The speed the slots' cycle reads and its play rate (UVCLocomotionClipSet::ComputeCyclePace, D91; vc.Anim.SprintPace). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float CycleBlendSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float CyclePlayRate = 1.f;

	/** The same for the aim cycle (its walk's authored speed in the walk's place). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float AimCycleBlendSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float AimCyclePlayRate = 1.f;

	/** The sprint (UVCLocomotionClipSet::Sprint) of the set whose loops play, and its pace (ComputeSprintPace). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UBlendSpace> SprintClip;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float SprintBlendSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float SprintPlayRate = 1.f;

	/** The start playing (chosen as the Start state begins) and where in it (s): the graph evaluates it at that time. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UAnimSequenceBase> StartClip;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float StartTime = 0.f;

	/** The stop playing (chosen as the Stop state begins, entered where its travel left is the braking distance) and where in it. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UAnimSequenceBase> StopClip;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float StopTime = 0.f;

	/** The turn in place playing and where in it; its root yaw turns the body through RootYawOffset, not the clip's root. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UAnimSequenceBase> TurnClip;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float TurnTime = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UAnimSequenceBase> JumpStartClip;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UAnimSequenceBase> FallLoopClip;

	/** The landing: the set's moving one when the character lands on the move, else its standing one (chosen at the touch-down). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UAnimSequenceBase> LandClip;

	// ==================== The crouch (the locomotion plan's L3) ====================

	/** The idle and the cycle play the crouch's (crouched, with a crouch set); the graph's blends follow it, inertialized. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bCrouchPose = false;

	/** The cycle that plays: 0 the slots' cycle, 1 the aim cycle, 2 the crouch cycle, 3 the aim crouch cycle (inertialized). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	int32 CycleChoice = 0;

	/** The crouch set's crouch idle, crouch cycle and aim crouch cycle. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UAnimSequenceBase> CrouchIdleClip;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UBlendSpace> CrouchCycleClip;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UBlendSpace> AimCrouchCycleClip;

	/** The crouch cycles' paces (UVCLocomotionClipSet::ComputeCrouchPace). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float CrouchCycleBlendSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float CrouchCyclePlayRate = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float AimCrouchCycleBlendSpeed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float AimCrouchCyclePlayRate = 1.f;

	/** The crouch's begin or end playing (chosen as CrouchChange begins) and where in it (s). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UAnimSequenceBase> CrouchChangeClip;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float CrouchChangeTime = 0.f;

	/**
	 * The borrowed crouch (§3.3): the idle of the set whose loops play, laid over the upper body on the stance's ramp at
	 * BorrowedCrouchWeight (1 crouched on a borrowed crouch, else 0, eased over TransitionBlendSeconds), so the borrowed
	 * set's crouch carries the legs and this set's own arms stay.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UAnimSequenceBase> BorrowedUpperClip;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float BorrowedCrouchWeight = 0.f;

	/** The held set plays its own locomotion (its stance layer at weight 0). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bOwnLocomotion = false;

	/**
	 * How far the default slot's montages (attacks, digs, hit reacts) keep the legs on the locomotion (0 to 1): 1 on the
	 * ground, moving, alive and not downed, so an attack on the move lays over the upper body on the stance's ramp; 0
	 * standing, where the montage plays over the whole body as authored. Eased at IKBlendSpeed.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float ActionLegsWeight = 0.f;

	// ==================== The state, one bool each for the graph's transitions ====================

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	EVCLocomotionState LocomotionState = EVCLocomotionState::Idle;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bInIdle = true;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bInTurnInPlace = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bInStart = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bInCycle = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bInSprint = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bInCrouchChange = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bInStop = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bInJump = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bInFall = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bInLand = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bInSwim = false;

	// ==================== Tuning ====================
	// Each of TransitionBlendSeconds, TurnInPlaceAngle, BodyMaxOffset, TurnPlayRate, StartExitSpeedFraction,
	// SprintEnterMargin, SprintMaxAngle, ReversalAngle and the body's recovery on the move (TurnInPlaceRecoverySpeed) has a
	// vc.Anim.SM.* override (the locomotion plan's L3; negative: the property).

	/** Seconds a landing on the move holds before the cycle takes over (the landing into the jog shows). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0"))
	float LandMinSeconds = 0.15f;

	/** Seconds of the graph's transitions (setup_character_state_machine.py's TRANSITION_BLEND): a clip's state ends this early. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0"))
	float TransitionBlendSeconds = 0.2f;

	/** Standing, the body turns in place once it is this far off the facing (degrees). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0", ClampMax = "180"))
	float TurnInPlaceAngle = 60.f;

	/**
	 * While the body faces the view (aiming), the legs stay within this of the torso (degrees): past it the body is
	 * dragged round with the facing, so a quick turn of the view never leaves them far enough behind to catch up the
	 * other way round. Facing where it goes the body may lag up to a half turn (a pivot, a start backward).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0", ClampMax = "180"))
	float BodyMaxOffset = 90.f;

	/** The turn clips' play rate (the packs' turn in 1.33 s). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0.1"))
	float TurnPlayRate = 1.25f;

	/** A start hands over to the cycle at this fraction of the movement's speed cap (or at its clip's end). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0", ClampMax = "1"))
	float StartExitSpeedFraction = 0.9f;

	/** A start in strafe gives way to the cycle when the input turns this far from the start's direction (degrees). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0", ClampMax = "180"))
	float StartDirectionTolerance = 60.f;

	/** The sprint plays this far over the run's cap (uu/s), within SprintMaxAngle of the body's forward. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0"))
	float SprintEnterMargin = 20.f;

	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0", ClampMax = "180"))
	float SprintMaxAngle = 60.f;

	/**
	 * A reversal on the move (in orient mode, the input this far or more from the velocity, degrees) plays the set's stop
	 * with the body held to its heading, then a start the new way (L3; the packs have no pivots); 0: off, the cycle steps
	 * on while the body eases round.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0", ClampMax = "180"))
	float ReversalAngle = 0.f;

protected:
	virtual float GetStanceTarget(const FVCAnimInput& Input, float BaseTarget) const override;
	virtual double AdjustRootYawOffset(const FVCAnimInput& Input, double Offset, bool bStanding) override;
	virtual float GetTurnInPlaceRecoverySpeed(const FVCAnimInput& Input) const override;

private:
	/** The tunables in force this update (the properties, each overridden by its vc.Anim.SM.* cvar when set). */
	struct FTuning
	{
		float TransitionBlendSeconds = 0.f;
		float TurnInPlaceAngle = 0.f;
		float BodyMaxOffset = 0.f;
		float TurnPlayRate = 1.f;
		float StartExitSpeedFraction = 0.f;
		float SprintEnterMargin = 0.f;
		float SprintMaxAngle = 0.f;
		float ReversalAngle = 0.f;
		float RecoverySpeed = 0.f;
	};

	/** @return The tunables in force (the cvars read on any thread: the update runs on a worker) */
	FTuning GetTuning() const;

	/**
	 * The turn in place's step of the body (its clip's root yaw), or the procedural turn's, before the clamp.
	 * @param Input     This update's input
	 * @param Offset    The body's offset from the facing (degrees)
	 * @param bStanding On the ground and slower than MovingSpeedThreshold
	 * @return The offset
	 */
	double TurnBody(const FVCAnimInput& Input, double Offset, bool bStanding);

	FVCLocomotionSlots Slots;

	/** The set whose loops play has turns in place (else the procedural turn), as of the last update. */
	bool bSetHasTurns = false;

	/** Seconds since the last touch-down (the land's hold), and in the current state. */
	float SecondsSinceLanded = 0.f;
	float StateSeconds = 0.f;

	bool bWasFallingForLand = false;

	float StartRate = 1.f;
	float StopRate = 1.f;

	/** The start's direction from the body (degrees), for StartDirectionTolerance. */
	float StartDegrees = 0.f;

	/** The turn: how far its clip's root yaw is scaled (the body's offset at the start over the clip's angle), its sign, the yaw so far. */
	float TurnScale = 1.f;
	float TurnSign = 1.f;
	float TurnLastYaw = 0.f;

	/** A set without turns eases the body round to the facing (D88) until it is within a degree. */
	bool bProceduralTurn = false;

	/** The crouch pose of the last update (a change at a stand plays the crouch's begin or end). */
	bool bWasCrouchPose = false;

	/** The playing stop began as a reversal: the body holds its heading through it. */
	bool bReversalStop = false;
};
