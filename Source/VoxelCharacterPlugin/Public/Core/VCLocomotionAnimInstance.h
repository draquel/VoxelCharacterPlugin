// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/VCAnimInstance.h"
#include "VCLocomotionAnimInstance.generated.h"

class UAnimSequenceBase;
class UBlendSpace;
class UVCLocomotionClipSet;

/** What the locomotion graph plays for a held set: each family from the held set, else from the base set (D88). */
struct FVCLocomotionClips
{
	TObjectPtr<UAnimSequenceBase> Idle;
	TObjectPtr<UBlendSpace> Cycle;
	TObjectPtr<UAnimSequenceBase> JumpStart;
	TObjectPtr<UAnimSequenceBase> FallLoop;
	TObjectPtr<UAnimSequenceBase> Land;
	TObjectPtr<UAnimSequenceBase> LandMoving;

	/** The Cycle's fastest authored speed (uu/s): above it the loops play faster. */
	float CycleTopSpeed = 0.f;

	/** The held set has its own locomotion (an idle and a Cycle): its stance layer gives way to its clips. */
	bool bOwnLocomotion = false;
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

/** What the state machine's transition rules read, from this update's values. */
struct FVCLocomotionRuleInput
{
	bool bOnGround = false;
	bool bFalling = false;
	bool bSwimming = false;
	bool bAccelerating = false;
	float Speed = 0.f;
	float UpSpeed = 0.f;
	float SecondsSinceLanded = 0.f;
};

/** The state machine's transition rules (D89: the choice in C++, each rule one bool the graph reads). */
struct FVCLocomotionRules
{
	bool bIdleToCycle = false;
	bool bCycleToIdle = false;
	bool bToJump = false;
	bool bToFall = false;
	bool bAirToLand = false;
	bool bLandToCycle = false;
	bool bToSwim = false;
	bool bSwimToIdle = false;
};

/**
 * The player's locomotion as a state machine of our own over the Essential Animation packs (the locomotion state machine
 * plan, D86-D92; vc.Anim.Locomotion 2): the parent of ABP_VCPlayerLocomotion, which Scripts/setup_character_state_machine.py
 * builds. It is a UVCAnimInstance, so the layers, the montages and the IK read the same values as under the other graphs.
 *
 * Each update it resolves the held set's UVCLocomotionClipSet (a family the set lacks from the base set's, D88), hands the
 * graph's players their clips, computes the transition rules and the play rate, and gives the held stance a weight of 0
 * while the set plays its own locomotion (its clips' arms show, D87). The state machine has Idle, Cycle, Jump, Fall, Land
 * and Swim; starts, stops and turns are the plan's L2.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCLocomotionAnimInstance : public UVCAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;

	/**
	 * Computes the locomotion's values (the clips, the rules, the play rate) after ComputeValues, from the same input.
	 * Touches only this instance; the tests call it directly.
	 * @param Input What the character's state was this update
	 */
	void ComputeLocomotion(const FVCAnimInput& Input);

	/**
	 * Pure: the clips for a held set (the locomotion plan's L1, D88): each family from the held set when it has it, else
	 * from the base set (Unarmed's); a set with no clip set plays the base set's whole.
	 * @param Held The held action set's clip set, or null
	 * @param Base The base set's clip set, or null
	 * @return The clips; bOwnLocomotion when the held set has its own idle and Cycle
	 */
	static FVCLocomotionClips ResolveClips(const UVCLocomotionClipSet* Held, const UVCLocomotionClipSet* Base);

	/**
	 * Pure: the held stance's weight under this graph (D87): none while the set plays its own locomotion, else the base
	 * target (the stance over the base set's clips, as under the other graphs).
	 * @param BaseTarget      The stance target from the ground, the sprint and vc.Anim.Stance
	 * @param bOwnLocomotion  The held set has its own locomotion
	 * @return 0 or BaseTarget
	 */
	static float ComputeStanceTarget(float BaseTarget, bool bOwnLocomotion);

	/**
	 * Pure: a set change (the locomotion plan's L1). The first call fills both slots; later, when the idle or the cycle
	 * differs from the playing slot's, the new clips go into the other slot and it becomes the playing one.
	 * @param Slots The slots, changed in place
	 * @param Clips This update's clips
	 * @return True when the playing slot flipped (the graph inertializes to it)
	 */
	static bool ApplySetChange(FVCLocomotionSlots& Slots, const FVCLocomotionClips& Clips);

	/**
	 * Pure: the state machine's transition rules.
	 * @param In                  This update's movement
	 * @param MovingSpeedThreshold Below it (uu/s, without input) the character stands
	 * @param LandMinSeconds      A landing on the move holds this long before the cycle takes over
	 * @return The rules
	 */
	static FVCLocomotionRules ComputeRules(const FVCLocomotionRuleInput& In, float MovingSpeedThreshold, float LandMinSeconds);

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

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UAnimSequenceBase> JumpStartClip;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UAnimSequenceBase> FallLoopClip;

	/** The landing: the set's moving one when the character lands on the move, else its standing one (chosen at the touch-down). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	TObjectPtr<UAnimSequenceBase> LandClip;

	/** The cycle's play rate: 1 up to the set's fastest authored loop, then the speed over it (vc.Anim.SprintPace). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	float CyclePlayRate = 1.f;

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

	// ==================== The transition rules ====================

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bRuleIdleToCycle = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bRuleCycleToIdle = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bRuleToJump = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bRuleToFall = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bRuleAirToLand = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bRuleLandToCycle = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bRuleToSwim = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation|Locomotion")
	bool bRuleSwimToIdle = false;

	// ==================== Tuning ====================

	/** Seconds a landing on the move holds before the cycle takes over (the landing into the jog shows). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0"))
	float LandMinSeconds = 0.15f;

protected:
	virtual float GetStanceTarget(const FVCAnimInput& Input, float BaseTarget) const override;

private:
	FVCLocomotionSlots Slots;

	/** Seconds since the last touch-down (the land's hold). */
	float SecondsSinceLanded = 0.f;

	bool bWasFallingForLand = false;
};
