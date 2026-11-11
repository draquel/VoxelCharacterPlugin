// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCLocomotionAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/VCLocomotionClipSet.h"
#include "Movement/VCMovementComponent.h"

namespace
{
	using EState = EVCLocomotionState;

	/** Every transition StepState takes, so the graph's state machine has each (the script builds them from this list). */
	const TPair<EState, EState> StateTransitions[] = {
		// The water, from anywhere, and out of it.
		{ EState::Idle, EState::Swim }, { EState::TurnInPlace, EState::Swim }, { EState::Start, EState::Swim },
		{ EState::Cycle, EState::Swim }, { EState::Sprint, EState::Swim }, { EState::Stop, EState::Swim },
		{ EState::Jump, EState::Swim }, { EState::Fall, EState::Swim }, { EState::Land, EState::Swim },
		{ EState::Swim, EState::Idle },
		// Into the air from the ground, through it, and down.
		{ EState::Idle, EState::Jump }, { EState::Idle, EState::Fall }, { EState::TurnInPlace, EState::Jump },
		{ EState::TurnInPlace, EState::Fall }, { EState::Start, EState::Jump }, { EState::Start, EState::Fall },
		{ EState::Cycle, EState::Jump }, { EState::Cycle, EState::Fall }, { EState::Sprint, EState::Jump },
		{ EState::Sprint, EState::Fall }, { EState::Stop, EState::Jump }, { EState::Stop, EState::Fall },
		{ EState::Jump, EState::Fall }, { EState::Jump, EState::Land }, { EState::Fall, EState::Land },
		{ EState::Land, EState::Jump }, { EState::Land, EState::Fall }, { EState::Land, EState::Cycle },
		{ EState::Land, EState::Idle },
		// On the ground.
		{ EState::Idle, EState::Start }, { EState::Idle, EState::Cycle }, { EState::Idle, EState::TurnInPlace },
		{ EState::TurnInPlace, EState::Start }, { EState::TurnInPlace, EState::Cycle }, { EState::TurnInPlace, EState::Idle },
		{ EState::Start, EState::Cycle }, { EState::Start, EState::Sprint }, { EState::Start, EState::Stop },
		{ EState::Start, EState::Idle },
		{ EState::Cycle, EState::Idle }, { EState::Cycle, EState::Stop }, { EState::Cycle, EState::Sprint },
		{ EState::Sprint, EState::Cycle }, { EState::Sprint, EState::Stop },
		{ EState::Stop, EState::Start }, { EState::Stop, EState::Cycle }, { EState::Stop, EState::Idle },
	};
}

UVCLocomotionAnimInstance::UVCLocomotionAnimInstance()
{
	// The body keeps its heading up to a half turn: on the move it eases round after the capsule's instant turn (the gait
	// table's), a pivot included, and standing a turn in place takes it back. Only the other graphs seat it past 90.
	TurnInPlaceMaxOffset = 180.f;
}

void UVCLocomotionAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	Slots = FVCLocomotionSlots();
	SecondsSinceLanded = 0.f;
	StateSeconds = 0.f;
	bWasFallingForLand = false;
	LocomotionState = EVCLocomotionState::Idle;
	bProceduralTurn = false;
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
		Clips.Loops = Loops;
	}
	Clips.AimCycle = Clips.bOwnLocomotion ? Held->AimCycle.Get() : nullptr;
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

bool UVCLocomotionAnimInstance::UsesAimCycle(bool bAiming, const FVCLocomotionClips& Clips)
{
	return bAiming && Clips.AimCycle != nullptr;
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

EVCLocomotionState UVCLocomotionAnimInstance::StepState(EVCLocomotionState Current, const FVCLocomotionStepInput& In)
{
	const bool bMoving = In.bHasInput || In.Speed > In.MovingSpeedThreshold;
	const bool bGrounded = In.bOnGround && !In.bSwimming;
	const EState Air = In.UpSpeed > 0.f ? EState::Jump : EState::Fall;
	// Input from a stand: the set's start when it has one, else straight into the cycle (D88).
	const EState Leave = In.bHasStart && In.bHasInput ? EState::Start : EState::Cycle;

	if (In.bSwimming)
	{
		return EState::Swim;
	}
	switch (Current)
	{
	case EState::Swim:
		return EState::Idle;
	case EState::Jump:
		if (bGrounded)
		{
			return EState::Land;
		}
		return In.StateSeconds >= In.JumpEndSeconds ? EState::Fall : EState::Jump;
	case EState::Fall:
		return bGrounded ? EState::Land : EState::Fall;
	case EState::Land:
		if (In.bFalling)
		{
			return Air;
		}
		if (bMoving && In.StateSeconds >= In.LandMinSeconds)
		{
			return EState::Cycle;
		}
		return In.StateSeconds >= In.LandEndSeconds ? EState::Idle : EState::Land;
	default:
		break;
	}
	if (In.bFalling)
	{
		return Air;
	}
	if (!bGrounded)
	{
		return Current; // debug flight, or held: as it was
	}
	switch (Current)
	{
	case EState::Idle:
		if (bMoving)
		{
			return Leave;
		}
		return In.bWantsTurn ? EState::TurnInPlace : EState::Idle;
	case EState::TurnInPlace:
		if (bMoving)
		{
			return Leave;
		}
		return In.bTurnDone ? EState::Idle : EState::TurnInPlace;
	case EState::Start:
		if (!In.bHasInput)
		{
			if (In.Speed <= In.MovingSpeedThreshold)
			{
				return EState::Idle;
			}
			return In.bHasStop ? EState::Stop : EState::Cycle;
		}
		if (In.bStartDone)
		{
			return In.bWantsSprint ? EState::Sprint : EState::Cycle;
		}
		return EState::Start;
	case EState::Cycle:
		if (!In.bHasInput)
		{
			if (In.Speed <= In.MovingSpeedThreshold)
			{
				return EState::Idle;
			}
			return In.bHasStop ? EState::Stop : EState::Cycle;
		}
		return In.bWantsSprint ? EState::Sprint : EState::Cycle;
	case EState::Sprint:
		if (!In.bHasInput)
		{
			return In.bHasStop ? EState::Stop : EState::Cycle;
		}
		return In.bWantsSprint ? EState::Sprint : EState::Cycle;
	case EState::Stop:
		if (In.bHasInput)
		{
			// Still moving: back into the cycle; at a stand: a new start.
			return In.bHasStart && In.Speed <= In.MovingSpeedThreshold ? EState::Start : EState::Cycle;
		}
		return In.bStopDone ? EState::Idle : EState::Stop;
	default:
		return Current;
	}
}

void UVCLocomotionAnimInstance::GetStateTransitions(TArray<FName>& OutFrom, TArray<FName>& OutTo)
{
	OutFrom.Reset();
	OutTo.Reset();
	const UEnum* Enum = StaticEnum<EVCLocomotionState>();
	for (const TPair<EState, EState>& Transition : StateTransitions)
	{
		OutFrom.Add(FName(Enum->GetNameStringByValue(static_cast<int64>(Transition.Key))));
		OutTo.Add(FName(Enum->GetNameStringByValue(static_cast<int64>(Transition.Value))));
	}
}

bool UVCLocomotionAnimInstance::IsStateTransition(EVCLocomotionState From, EVCLocomotionState To)
{
	if (From == To)
	{
		return true;
	}
	for (const TPair<EState, EState>& Transition : StateTransitions)
	{
		if (Transition.Key == From && Transition.Value == To)
		{
			return true;
		}
	}
	return false;
}

EVCLocomotionGait UVCLocomotionAnimInstance::ToLocomotionGait(EVCGait Gait)
{
	switch (Gait)
	{
	case EVCGait::Run:    return EVCLocomotionGait::Jog;
	case EVCGait::Sprint: return EVCLocomotionGait::Run;
	default:              return EVCLocomotionGait::Walk;
	}
}

EVCLocomotionGait UVCLocomotionAnimInstance::StopGaitAt(const UVCLocomotionClipSet* Set, float InSpeed, bool bSprinting)
{
	if (bSprinting)
	{
		return EVCLocomotionGait::Run;
	}
	if (!Set)
	{
		return EVCLocomotionGait::Jog;
	}
	return InSpeed > 0.5f * (Set->WalkSpeed + Set->RunSpeed) ? EVCLocomotionGait::Jog : EVCLocomotionGait::Walk;
}

float UVCLocomotionAnimInstance::GetStanceTarget(const FVCAnimInput& Input, float BaseTarget) const
{
	return ComputeStanceTarget(BaseTarget, Input.LocomotionClips && Input.LocomotionClips->HasOwnLocomotion());
}

double UVCLocomotionAnimInstance::AdjustRootYawOffset(const FVCAnimInput& Input, double Offset, bool bStanding)
{
	// A turn in place: its clip's root yaw turns the body, scaled to the offset the turn began with (the graph locks the
	// clip's own root, so the body turns once).
	if (LocomotionState == EVCLocomotionState::TurnInPlace && TurnClip && bStanding)
	{
		TurnTime = FMath::Min(TurnTime + Input.DeltaSeconds * TurnPlayRate, TurnClip->GetPlayLength());
		const float Yaw = FMath::Abs(UVCLocomotionClipSet::RootYawAt(TurnClip, TurnTime));
		Offset += TurnSign * (Yaw - TurnLastYaw) * TurnScale;
		TurnLastYaw = Yaw;
		bProceduralTurn = false;
		return Offset;
	}
	// A set without turns (D88): past the angle the body eases round to the facing, as it does on the move.
	const bool bStill = bStanding && LocomotionState == EVCLocomotionState::Idle && Input.Acceleration.IsNearlyZero();
	if (!bStill || bSetHasTurns)
	{
		bProceduralTurn = false;
		return Offset;
	}
	bProceduralTurn |= FMath::Abs(Offset) >= TurnInPlaceAngle;
	if (bProceduralTurn)
	{
		Offset = FMath::FInterpTo(Offset, 0.0, static_cast<double>(Input.DeltaSeconds), static_cast<double>(TurnInPlaceRecoverySpeed));
		bProceduralTurn = FMath::Abs(Offset) >= 1.0;
	}
	return Offset;
}

void UVCLocomotionAnimInstance::ComputeLocomotion(const FVCAnimInput& Input)
{
	const FVCLocomotionClips Clips = ResolveClips(Input.LocomotionClips, Input.BaseLocomotionClips);
	const UVCLocomotionClipSet* Set = Clips.Loops;
	ApplySetChange(Slots, Clips);
	IdleA = Slots.IdleA;
	IdleB = Slots.IdleB;
	CycleA = Slots.CycleA;
	CycleB = Slots.CycleB;
	bSlotB = Slots.bSlotB;
	bOwnLocomotion = Clips.bOwnLocomotion;
	AimCycleClip = Clips.AimCycle;
	bAimCycle = UsesAimCycle(Input.bAiming && !Input.bDead && !Input.bDowned, Clips);
	JumpStartClip = Clips.JumpStart;
	FallLoopClip = Clips.FallLoop;
	SprintClip = Set ? Set->Sprint.Get() : nullptr;
	bSetHasTurns = Set && Set->Turns.HasAny();

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

	// The body's frame: the facing turned by the offset the graph turns the root by. The cycles step by the velocity's
	// angle from the body, and the aim layer aims from the body.
	CycleDirection = Speed > 1.f ? FRotator::NormalizeAxis(Direction - RootYawOffset) : 0.f;
	AimYaw = FMath::ClampAngle(FRotator::NormalizeAxis(ViewYaw - RootYawOffset), -90.f, 90.f);

	// The paces (D91): each gait's loops whole at its cap, at the cap over their authored speed.
	const bool bPace = Input.bSprintPaceEnabled && Input.bOnGround && Set;
	const FVCLocomotionPace CyclePace = bPace ? Set->ComputeCyclePace(Speed, false) : FVCLocomotionPace{ Speed, 1.f };
	const FVCLocomotionPace AimPace = bPace ? Set->ComputeCyclePace(Speed, true) : FVCLocomotionPace{ Speed, 1.f };
	const FVCLocomotionPace SprintPace = bPace && Set->HasSprint() ? Set->ComputeSprintPace(Speed) : FVCLocomotionPace{ Speed, 1.f };
	CycleBlendSpeed = CyclePace.BlendSpeed;
	CyclePlayRate = CyclePace.PlayRate;
	AimCycleBlendSpeed = AimPace.BlendSpeed;
	AimCyclePlayRate = AimPace.PlayRate;
	SprintBlendSpeed = SprintPace.BlendSpeed;
	SprintPlayRate = SprintPace.PlayRate;

	// The playing state's clip moves on (the turn's in AdjustRootYawOffset, before the offset is kept).
	StateSeconds += Input.DeltaSeconds;
	if (LocomotionState == EVCLocomotionState::Start && StartClip)
	{
		StartTime = FMath::Min(StartTime + Input.DeltaSeconds * StartRate, StartClip->GetPlayLength());
	}
	else if (LocomotionState == EVCLocomotionState::Stop && StopClip)
	{
		StopTime = FMath::Min(StopTime + Input.DeltaSeconds * StopRate, StopClip->GetPlayLength());
	}

	// What the set has for this update: a start the way the input goes (forward of the body when the capsule turns to
	// it, so the body eases round into the run), a stop the way the body moves, a turn back to the facing.
	FVector Up = -Input.GravityDirection;
	if (!Up.Normalize())
	{
		Up = FVector::UpVector;
	}
	const bool bHasInput = bIsAccelerating;
	const bool bStanding = Input.bOnGround && Speed < MovingSpeedThreshold;
	const float InputDegrees = Input.bOrientToMovement || !bHasInput ? 0.f
		: FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(LocalAcceleration.Y, LocalAcceleration.X)) - RootYawOffset);
	const bool bInSprintNow = LocomotionState == EVCLocomotionState::Sprint;
	const FVCLocomotionPick StartPick = Set && bHasInput ? Set->ChooseStart(ToLocomotionGait(Input.Gait), InputDegrees, bAimCycle)
		: FVCLocomotionPick();
	const FVCLocomotionPick StopPick = Set && !bHasInput && bMoving
		? Set->ChooseStop(StopGaitAt(Set, Speed, bInSprintNow), CycleDirection, bAimCycle) : FVCLocomotionPick();
	const bool bTurnable = bStanding && !bHasInput && !Input.bDead && !Input.bDowned && FMath::Abs(RootYawOffset) >= TurnInPlaceAngle;
	const FVCLocomotionPick TurnPick = Set && bTurnable ? Set->ChooseTurn(RootYawOffset) : FVCLocomotionPick();

	FVCLocomotionStepInput Step;
	Step.bOnGround = Input.bOnGround;
	Step.bFalling = Input.bFalling;
	Step.bSwimming = Input.bSwimming;
	Step.bHasInput = bHasInput;
	Step.Speed = Speed;
	Step.UpSpeed = Input.Velocity | Up;
	Step.StateSeconds = StateSeconds;
	Step.bHasStart = StartPick.Clip != nullptr;
	Step.bHasStop = StopPick.Clip != nullptr;
	Step.bWantsTurn = TurnPick.Clip != nullptr;
	// The sprint: the sprint gait running forward of the body, past the run's cap (less, once in it, so it holds).
	const float SprintFloor = Set ? Set->RunSpeed + (bInSprintNow ? -SprintEnterMargin : SprintEnterMargin) : 0.f;
	const float SprintAngle = SprintMaxAngle + (bInSprintNow ? 15.f : 0.f);
	Step.bWantsSprint = Set && Set->HasSprint() && Input.Gait == EVCGait::Sprint && !bAimCycle && Speed > SprintFloor
		&& FMath::Abs(CycleDirection) <= SprintAngle;
	const bool bStartDiverged = !Input.bOrientToMovement && bHasInput
		&& FMath::Abs(FMath::FindDeltaAngleDegrees(StartDegrees, InputDegrees)) > StartDirectionTolerance;
	Step.bStartDone = !StartClip || StartTime >= StartClip->GetPlayLength() - TransitionBlendSeconds * StartRate
		|| (Input.MaxSpeed > 0.f && Speed >= StartExitSpeedFraction * Input.MaxSpeed) || bStartDiverged;
	Step.bStopDone = !StopClip || StopTime >= StopClip->GetPlayLength();
	Step.bTurnDone = !TurnClip || TurnTime >= TurnClip->GetPlayLength();
	Step.MovingSpeedThreshold = MovingSpeedThreshold;
	Step.LandMinSeconds = LandMinSeconds;
	Step.JumpEndSeconds = JumpStartClip ? JumpStartClip->GetPlayLength() - TransitionBlendSeconds : 0.f;
	Step.LandEndSeconds = LandClip ? LandClip->GetPlayLength() - TransitionBlendSeconds : 0.f;

	const EVCLocomotionState Next = StepState(LocomotionState, Step);
	if (Next != LocomotionState)
	{
		// The state's clip, chosen as it begins and kept to its end.
		switch (Next)
		{
		case EVCLocomotionState::Start:
			StartClip = StartPick.Clip;
			StartRate = StartPick.PlayRate;
			StartTime = 0.f;
			StartDegrees = InputDegrees;
			break;
		case EVCLocomotionState::Stop:
		{
			StopClip = StopPick.Clip;
			StopRate = StopPick.PlayRate;
			// Entered where its travel left is the capsule's braking distance, so its feet land where the capsule stops.
			const float Braking = FMath::Max(Input.MaxBrakingDeceleration, 1.f);
			const UAnimSequenceBase* Clip = StopClip;
			StopTime = Clip ? UVCLocomotionClipSet::FindStopEntryTime(
				[Clip](float Time) { return UVCLocomotionClipSet::RootDistanceAt(Clip, Time); },
				Clip->GetPlayLength(), Speed * Speed / (2.f * Braking)) : 0.f;
			break;
		}
		case EVCLocomotionState::TurnInPlace:
			TurnClip = TurnPick.Clip;
			TurnTime = 0.f;
			TurnLastYaw = 0.f;
			TurnSign = TurnPick.TurnDegrees >= 0.f ? 1.f : -1.f;
			TurnScale = FMath::Abs(RootYawOffset) / FMath::Max(FMath::Abs(TurnPick.TurnDegrees), 1.f);
			break;
		default:
			break;
		}
		LocomotionState = Next;
		StateSeconds = 0.f;
	}
	bInIdle = LocomotionState == EVCLocomotionState::Idle;
	bInTurnInPlace = LocomotionState == EVCLocomotionState::TurnInPlace;
	bInStart = LocomotionState == EVCLocomotionState::Start;
	bInCycle = LocomotionState == EVCLocomotionState::Cycle;
	bInSprint = LocomotionState == EVCLocomotionState::Sprint;
	bInStop = LocomotionState == EVCLocomotionState::Stop;
	bInJump = LocomotionState == EVCLocomotionState::Jump;
	bInFall = LocomotionState == EVCLocomotionState::Fall;
	bInLand = LocomotionState == EVCLocomotionState::Land;
	bInSwim = LocomotionState == EVCLocomotionState::Swim;

	const bool bLegsOnLocomotion = !Input.bDead && !Input.bDowned && Input.bOnGround && !Input.bSwimming && bMoving;
	ActionLegsWeight = FMath::FInterpConstantTo(ActionLegsWeight, bLegsOnLocomotion ? 1.f : 0.f, Input.DeltaSeconds, IKBlendSpeed);
}
