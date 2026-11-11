// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCLocomotionAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/VCLocomotionClipSet.h"

void UVCLocomotionAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	Slots = FVCLocomotionSlots();
	SecondsSinceLanded = 0.f;
	bWasFallingForLand = false;
}

void UVCLocomotionAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
	if (const FVCAnimInput* Input = GetGatheredInput())
	{
		ComputeLocomotion(*Input);
	}
}

FVCLocomotionClips UVCLocomotionAnimInstance::ResolveClips(const UVCLocomotionClipSet* Held, const UVCLocomotionClipSet* Base)
{
	FVCLocomotionClips Clips;
	Clips.bOwnLocomotion = Held && Held->HasOwnLocomotion();
	// The idle and the cycle come as a pair, so a set's arms never cycle over another set's idle.
	const UVCLocomotionClipSet* Loops = Clips.bOwnLocomotion ? Held : Base;
	if (Loops)
	{
		Clips.Idle = Loops->Idle;
		Clips.Cycle = Loops->Cycle;
		Clips.CycleTopSpeed = Loops->GetCycleTopSpeed();
	}
	const auto Pick = [Held, Base](TObjectPtr<UAnimSequenceBase> UVCLocomotionClipSet::* Family) -> UAnimSequenceBase*
	{
		if (Held && Held->*Family)
		{
			return Held->*Family;
		}
		return Base ? (Base->*Family).Get() : nullptr;
	};
	Clips.JumpStart = Pick(&UVCLocomotionClipSet::JumpStart);
	Clips.FallLoop = Pick(&UVCLocomotionClipSet::FallLoop);
	Clips.Land = Pick(&UVCLocomotionClipSet::Land);
	// A moving landing falls back to the same set's standing one before another set's moving one.
	Clips.LandMoving = Held && (Held->LandMoving || Held->Land) ? (Held->LandMoving ? Held->LandMoving.Get() : Held->Land.Get())
		: Pick(&UVCLocomotionClipSet::LandMoving);
	if (!Clips.LandMoving)
	{
		Clips.LandMoving = Clips.Land;
	}
	return Clips;
}

float UVCLocomotionAnimInstance::ComputeStanceTarget(float BaseTarget, bool bOwnLocomotion)
{
	return bOwnLocomotion ? 0.f : BaseTarget;
}

bool UVCLocomotionAnimInstance::ApplySetChange(FVCLocomotionSlots& InOutSlots, const FVCLocomotionClips& Clips)
{
	if (!InOutSlots.bFilled)
	{
		InOutSlots.IdleA = InOutSlots.IdleB = Clips.Idle;
		InOutSlots.CycleA = InOutSlots.CycleB = Clips.Cycle;
		InOutSlots.bSlotB = false;
		InOutSlots.bFilled = true;
		return false;
	}
	const bool bB = InOutSlots.bSlotB;
	if ((bB ? InOutSlots.IdleB : InOutSlots.IdleA) == Clips.Idle && (bB ? InOutSlots.CycleB : InOutSlots.CycleA) == Clips.Cycle)
	{
		return false;
	}
	(bB ? InOutSlots.IdleA : InOutSlots.IdleB) = Clips.Idle;
	(bB ? InOutSlots.CycleA : InOutSlots.CycleB) = Clips.Cycle;
	InOutSlots.bSlotB = !bB;
	return true;
}

FVCLocomotionRules UVCLocomotionAnimInstance::ComputeRules(const FVCLocomotionRuleInput& In, float MovingSpeedThreshold, float LandMinSeconds)
{
	FVCLocomotionRules Rules;
	const bool bMoving = In.Speed > MovingSpeedThreshold || In.bAccelerating;
	const bool bGrounded = In.bOnGround && !In.bSwimming;
	Rules.bIdleToCycle = bGrounded && bMoving;
	Rules.bCycleToIdle = bGrounded && !bMoving;
	Rules.bToJump = In.bFalling && In.UpSpeed > 0.f;
	Rules.bToFall = In.bFalling && In.UpSpeed <= 0.f;
	Rules.bAirToLand = bGrounded;
	Rules.bLandToCycle = bGrounded && bMoving && In.SecondsSinceLanded >= LandMinSeconds;
	Rules.bToSwim = In.bSwimming;
	Rules.bSwimToIdle = !In.bSwimming;
	return Rules;
}

float UVCLocomotionAnimInstance::GetStanceTarget(const FVCAnimInput& Input, float BaseTarget) const
{
	return ComputeStanceTarget(BaseTarget, Input.LocomotionClips && Input.LocomotionClips->HasOwnLocomotion());
}

void UVCLocomotionAnimInstance::ComputeLocomotion(const FVCAnimInput& Input)
{
	const FVCLocomotionClips Clips = ResolveClips(Input.LocomotionClips, Input.BaseLocomotionClips);
	ApplySetChange(Slots, Clips);
	IdleA = Slots.IdleA;
	IdleB = Slots.IdleB;
	CycleA = Slots.CycleA;
	CycleB = Slots.CycleB;
	bSlotB = Slots.bSlotB;
	bOwnLocomotion = Clips.bOwnLocomotion;
	JumpStartClip = Clips.JumpStart;
	FallLoopClip = Clips.FallLoop;

	// The landing is chosen at the touch-down, so the Land state's clip never changes under it.
	const bool bMoving = Speed > MovingSpeedThreshold;
	if (bWasFallingForLand && !Input.bFalling)
	{
		SecondsSinceLanded = 0.f;
		LandClip = bMoving ? Clips.LandMoving : Clips.Land;
	}
	else
	{
		SecondsSinceLanded += Input.DeltaSeconds;
	}
	if (!LandClip)
	{
		LandClip = Clips.Land;
	}
	bWasFallingForLand = Input.bFalling;

	CyclePlayRate = Input.bSprintPaceEnabled && Input.bOnGround ? ComputeLocomotionPlayRate(Speed, Clips.CycleTopSpeed) : 1.f;

	FVector Up = -Input.GravityDirection;
	if (!Up.Normalize())
	{
		Up = FVector::UpVector;
	}
	FVCLocomotionRuleInput RuleInput;
	RuleInput.bOnGround = Input.bOnGround;
	RuleInput.bFalling = Input.bFalling;
	RuleInput.bSwimming = Input.bSwimming;
	RuleInput.bAccelerating = bIsAccelerating;
	RuleInput.Speed = Speed;
	RuleInput.UpSpeed = Input.Velocity | Up;
	RuleInput.SecondsSinceLanded = SecondsSinceLanded;
	const FVCLocomotionRules Rules = ComputeRules(RuleInput, MovingSpeedThreshold, LandMinSeconds);
	bRuleIdleToCycle = Rules.bIdleToCycle;
	bRuleCycleToIdle = Rules.bCycleToIdle;
	bRuleToJump = Rules.bToJump;
	bRuleToFall = Rules.bToFall;
	bRuleAirToLand = Rules.bAirToLand;
	bRuleLandToCycle = Rules.bLandToCycle;
	bRuleToSwim = Rules.bToSwim;
	bRuleSwimToIdle = Rules.bSwimToIdle;

	const bool bLegsOnLocomotion = !Input.bDead && !Input.bDowned && Input.bOnGround && !Input.bSwimming && bMoving;
	ActionLegsWeight = FMath::FInterpConstantTo(ActionLegsWeight, bLegsOnLocomotion ? 1.f : 0.f, Input.DeltaSeconds, IKBlendSpeed);
}
