// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCLocomotionAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/VCLocomotionClipSet.h"
#include "Movement/VCMovementComponent.h"
#include "HAL/IConsoleManager.h"

namespace
{
	using EState = EVCLocomotionState;

	// The tunables' overrides (the locomotion plan's L3): negative keeps the anim instance's property.
	TAutoConsoleVariable<float> CVarSMTransitionBlendSeconds(
		TEXT("vc.Anim.SM.TransitionBlendSeconds"), -1.f,
		TEXT("The state machine (vc.Anim.Locomotion 2): seconds before its clip's end a jump, landing, start, turn or crouch change hands over (the graph's own blend is setup_character_state_machine.py's 0.2). Negative: the anim instance's TransitionBlendSeconds (0.2)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarSMTurnInPlaceAngle(
		TEXT("vc.Anim.SM.TurnInPlaceAngle"), -1.f,
		TEXT("The state machine: standing, the body turns in place once it is this far off the facing (degrees; the 180 from 135). Negative: the property (60)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarSMTurnPlayRate(
		TEXT("vc.Anim.SM.TurnPlayRate"), -1.f,
		TEXT("The state machine: the turns in place's play rate (the packs' turn in 1.33 s). Negative: the property (1.25)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarSMBodyMaxOffset(
		TEXT("vc.Anim.SM.BodyMaxOffset"), -1.f,
		TEXT("The state machine: while the body faces the view (aiming) the legs stay within this of the torso (degrees), dragged round past it. Negative: the property (90)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarSMStartExitSpeedFraction(
		TEXT("vc.Anim.SM.StartExitSpeedFraction"), -1.f,
		TEXT("The state machine: a start hands over to the cycle at this fraction of the speed cap (or at its clip's end; 0 to 1). Negative: the property (0.9)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarSMSprintEnterMargin(
		TEXT("vc.Anim.SM.SprintEnterMargin"), -1.f,
		TEXT("The state machine: the sprint state plays this far over the run's cap (uu/s; leaves this far under it). Negative: the property (20)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarSMSprintMaxAngle(
		TEXT("vc.Anim.SM.SprintMaxAngle"), -1.f,
		TEXT("The state machine: the sprint plays within this of the body's forward (degrees; 15 more once in it). Negative: the property (60)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarSMReversalAngle(
		TEXT("vc.Anim.SM.ReversalAngle"), -1.f,
		TEXT("The state machine: a reversal on the move (the input this far or more from the velocity, degrees, in orient mode) plays the set's stop, the body held, then a start the new way; 0 off (the cycle steps on while the body eases round). Negative: the property."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarSMRecoverySpeed(
		TEXT("vc.Anim.SM.RecoverySpeed"), -1.f,
		TEXT("The state machine: how fast the body eases round to the facing on the move and in the procedural turn (FInterpTo speed; a pivot's swing). Negative: the property (TurnInPlaceRecoverySpeed, 10)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarSMBorrowCrouch(
		TEXT("vc.Anim.SM.BorrowCrouch"), 1,
		TEXT("The state machine: a set without a crouch of its own crouches on another set's legs under its own idle upper body (the bow's, §3.3): 1 on, 0 the walk's loops while crouched (A/B by eye; the movement's crouch cap stays the borrowed set's)."),
		ECVF_Default);

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
		// The crouch's begin and end at a stand (L3).
		{ EState::Idle, EState::CrouchChange }, { EState::CrouchChange, EState::Idle }, { EState::CrouchChange, EState::Start },
		{ EState::CrouchChange, EState::Cycle }, { EState::CrouchChange, EState::Jump }, { EState::CrouchChange, EState::Fall },
		{ EState::CrouchChange, EState::Swim },
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
	bWasCrouchPose = false;
	bReversalStop = false;
}

void UVCLocomotionAnimInstance::NativeThreadSafeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeThreadSafeUpdateAnimation(DeltaSeconds);
	if (const FVCAnimInput* Input = GetGatheredInput())
	{
		ComputeLocomotion(*Input);
	}
}

FVCLocomotionClips UVCLocomotionAnimInstance::ResolveClips(const UVCLocomotionClipSet* Held, const UVCLocomotionClipSet* Base,
	bool bBorrowCrouch)
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
		// The crouch goes with the loops: their set's own, else the one it borrows (L3, §3.3).
		Clips.Crouch = Loops->GetCrouchSet(bBorrowCrouch);
		Clips.bBorrowedCrouch = Clips.Crouch && Clips.Crouch != Loops;
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
		if (In.bWantsCrouchChange)
		{
			return EState::CrouchChange;
		}
		return In.bWantsTurn ? EState::TurnInPlace : EState::Idle;
	case EState::CrouchChange:
		if (bMoving)
		{
			return Leave;
		}
		return In.bCrouchChangeDone ? EState::Idle : EState::CrouchChange;
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
		if (In.bReversal && In.bHasStop)
		{
			return EState::Stop;
		}
		return In.bWantsSprint ? EState::Sprint : EState::Cycle;
	case EState::Sprint:
		if (!In.bHasInput || (In.bReversal && In.bHasStop))
		{
			return In.bHasStop ? EState::Stop : EState::Cycle;
		}
		return In.bWantsSprint ? EState::Sprint : EState::Cycle;
	case EState::Stop:
		if (In.bHasInput)
		{
			// A reversal holds the stop until the velocity turns; then, or at a stand, a new start; still moving: the cycle.
			if (In.bReversal)
			{
				return EState::Stop;
			}
			return In.bHasStart && (In.Speed <= In.MovingSpeedThreshold || In.bReversalStop) ? EState::Start : EState::Cycle;
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

float UVCLocomotionAnimInstance::ResolveTunable(float Override, float Property)
{
	return Override >= 0.f ? Override : Property;
}

float UVCLocomotionAnimInstance::InputAngleToVelocity(const FVector& LocalInput, const FVector& LocalVelocity)
{
	const FVector2D In(LocalInput.X, LocalInput.Y);
	const FVector2D Moving(LocalVelocity.X, LocalVelocity.Y);
	if (In.IsNearlyZero() || Moving.IsNearlyZero())
	{
		return 0.f;
	}
	const double Cosine = FMath::Clamp(FVector2D::DotProduct(In.GetSafeNormal(), Moving.GetSafeNormal()), -1.0, 1.0);
	return static_cast<float>(FMath::RadiansToDegrees(FMath::Acos(Cosine)));
}

float UVCLocomotionAnimInstance::ReversalStopDistance(float Speed, float Acceleration, float Friction)
{
	// The movement turns the velocity toward the input at the friction's rate, which for an input straight against it
	// takes 2 * Friction * v a second off the speed, and adds the input's acceleration: v' = -2 F v - A until v is 0.
	const double V = FMath::Max(Speed, 0.f);
	const double K = 2.0 * FMath::Max(Friction, 0.f);
	const double A = FMath::Max(Acceleration, 0.f);
	if (V <= 0.0)
	{
		return 0.f;
	}
	if (K <= UE_KINDA_SMALL_NUMBER)
	{
		return A > 0.0 ? static_cast<float>(V * V / (2.0 * A)) : 0.f;
	}
	const double Floor = A / K;
	const double Time = FMath::Loge(1.0 + V / FMath::Max(Floor, UE_KINDA_SMALL_NUMBER)) / K;
	return static_cast<float>((V + Floor) * (1.0 - FMath::Exp(-K * Time)) / K - Floor * Time);
}

UVCLocomotionAnimInstance::FTuning UVCLocomotionAnimInstance::GetTuning() const
{
	FTuning Tuning;
	Tuning.TransitionBlendSeconds = ResolveTunable(CVarSMTransitionBlendSeconds.GetValueOnAnyThread(), TransitionBlendSeconds);
	Tuning.TurnInPlaceAngle = ResolveTunable(CVarSMTurnInPlaceAngle.GetValueOnAnyThread(), TurnInPlaceAngle);
	Tuning.TurnPlayRate = FMath::Max(ResolveTunable(CVarSMTurnPlayRate.GetValueOnAnyThread(), TurnPlayRate), 0.1f);
	Tuning.BodyMaxOffset = ResolveTunable(CVarSMBodyMaxOffset.GetValueOnAnyThread(), BodyMaxOffset);
	Tuning.StartExitSpeedFraction = ResolveTunable(CVarSMStartExitSpeedFraction.GetValueOnAnyThread(), StartExitSpeedFraction);
	Tuning.SprintEnterMargin = ResolveTunable(CVarSMSprintEnterMargin.GetValueOnAnyThread(), SprintEnterMargin);
	Tuning.SprintMaxAngle = ResolveTunable(CVarSMSprintMaxAngle.GetValueOnAnyThread(), SprintMaxAngle);
	Tuning.ReversalAngle = ResolveTunable(CVarSMReversalAngle.GetValueOnAnyThread(), ReversalAngle);
	Tuning.RecoverySpeed = ResolveTunable(CVarSMRecoverySpeed.GetValueOnAnyThread(), TurnInPlaceRecoverySpeed);
	return Tuning;
}

float UVCLocomotionAnimInstance::GetTurnInPlaceRecoverySpeed(const FVCAnimInput& Input) const
{
	// Through a reversal's stop the body keeps its heading, so the stop plays the way it was going.
	return LocomotionState == EVCLocomotionState::Stop && bReversalStop ? -1.f : GetTuning().RecoverySpeed;
}

float UVCLocomotionAnimInstance::GetStanceTarget(const FVCAnimInput& Input, float BaseTarget) const
{
	return ComputeStanceTarget(BaseTarget, Input.LocomotionClips && Input.LocomotionClips->HasOwnLocomotion());
}

double UVCLocomotionAnimInstance::AdjustRootYawOffset(const FVCAnimInput& Input, double Offset, bool bStanding)
{
	// While the body faces the view (aiming), the legs never fall further behind the torso than BodyMaxOffset (dragged
	// round past it). Toward where it goes the capsule turns at once to the input, a start backward included, so there
	// the body may lag a half turn and ease round.
	const double Limit = Input.bOrientToMovement ? 180.0 : static_cast<double>(GetTuning().BodyMaxOffset);
	return FMath::Clamp(TurnBody(Input, Offset, bStanding), -Limit, Limit);
}

double UVCLocomotionAnimInstance::TurnBody(const FVCAnimInput& Input, double Offset, bool bStanding)
{
	const FTuning Tuning = GetTuning();
	// A turn in place: its clip's root yaw turns the body, scaled to the offset the turn began with (the graph locks the
	// clip's own root, so the body turns once).
	if (LocomotionState == EVCLocomotionState::TurnInPlace && TurnClip && bStanding)
	{
		bProceduralTurn = false;
		// The view came back past the body: turning on would turn away from it, so the turn ends (StepState's bTurnDone).
		if (Offset * TurnSign > 1.0)
		{
			TurnTime = TurnClip->GetPlayLength();
			return Offset;
		}
		TurnTime = FMath::Min(TurnTime + Input.DeltaSeconds * Tuning.TurnPlayRate, TurnClip->GetPlayLength());
		const float Yaw = FMath::Abs(UVCLocomotionClipSet::RootYawAt(TurnClip, TurnTime));
		Offset += TurnSign * (Yaw - TurnLastYaw) * TurnScale;
		TurnLastYaw = Yaw;
		return Offset;
	}
	// A set without turns (D88), or crouched (the packs' turns stand): past the angle the body eases round to the facing,
	// as it does on the move.
	const bool bStill = bStanding && LocomotionState == EVCLocomotionState::Idle && Input.Acceleration.IsNearlyZero();
	if (!bStill || bSetHasTurns)
	{
		bProceduralTurn = false;
		return Offset;
	}
	bProceduralTurn |= FMath::Abs(Offset) >= Tuning.TurnInPlaceAngle;
	if (bProceduralTurn)
	{
		Offset = FMath::FInterpTo(Offset, 0.0, static_cast<double>(Input.DeltaSeconds), static_cast<double>(Tuning.RecoverySpeed));
		bProceduralTurn = FMath::Abs(Offset) >= 1.0;
	}
	return Offset;
}

void UVCLocomotionAnimInstance::ComputeLocomotion(const FVCAnimInput& Input)
{
	const FTuning Tuning = GetTuning();
	const FVCLocomotionClips Clips = ResolveClips(Input.LocomotionClips, Input.BaseLocomotionClips, CVarSMBorrowCrouch.GetValueOnAnyThread() != 0);
	const UVCLocomotionClipSet* Set = Clips.Loops;
	const UVCLocomotionClipSet* CrouchSet = Clips.Crouch;
	ApplySetChange(Slots, Clips);
	IdleA = Slots.IdleA;
	IdleB = Slots.IdleB;
	CycleA = Slots.CycleA;
	CycleB = Slots.CycleB;
	bSlotB = Slots.bSlotB;
	bOwnLocomotion = Clips.bOwnLocomotion;
	AimCycleClip = Clips.AimCycle;
	const bool bAiming = Input.bAiming && !Input.bDead && !Input.bDowned;
	bAimCycle = UsesAimCycle(bAiming, Clips);
	JumpStartClip = Clips.JumpStart;
	FallLoopClip = Clips.FallLoop;
	SprintClip = Set ? Set->Sprint.Get() : nullptr;

	// The crouch (L3): crouched, the idle and the cycle play the crouch set's (the aim crouch cycle while aiming); a set
	// without one plays the walk's loops as before. A borrowed crouch lays the loops' own idle over the upper body.
	bCrouchPose = Input.bCrouching && !Input.bSwimming && CrouchSet != nullptr;
	CrouchIdleClip = CrouchSet ? CrouchSet->CrouchIdle.Get() : nullptr;
	CrouchCycleClip = CrouchSet ? CrouchSet->CrouchCycle.Get() : nullptr;
	AimCrouchCycleClip = CrouchSet ? CrouchSet->AimCrouchCycle.Get() : nullptr;
	const bool bAimCrouch = bAiming && AimCrouchCycleClip != nullptr;
	CycleChoice = bCrouchPose ? (bAimCrouch ? 3 : 2) : (bAimCycle ? 1 : 0);
	BorrowedUpperClip = Clips.Idle;
	BorrowedCrouchWeight = FMath::FInterpConstantTo(BorrowedCrouchWeight, bCrouchPose && Clips.bBorrowedCrouch ? 1.f : 0.f,
		Input.DeltaSeconds, 1.f / FMath::Max(Tuning.TransitionBlendSeconds, 0.05f));
	bSetHasTurns = Set && Set->Turns.HasAny() && !bCrouchPose;

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
	// angle from the body. AimYaw stays the base class's, from the facing: the aim layer's raised bow is laid over the
	// upper body in mesh space, so it already starts from the capsule, not from the turned root (measured from the body,
	// the torso overshot the view by the legs' lag until the feet caught up, L4's hand pass).
	CycleDirection = Speed > 1.f ? FRotator::NormalizeAxis(Direction - RootYawOffset) : 0.f;

	// The paces (D91): each gait's loops whole at its cap, at the cap over their authored speed.
	const bool bPace = Input.bSprintPaceEnabled && Input.bOnGround && Set;
	const FVCLocomotionPace CyclePace = bPace ? Set->ComputeCyclePace(Speed, false) : FVCLocomotionPace{ Speed, 1.f };
	const FVCLocomotionPace AimPace = bPace ? Set->ComputeCyclePace(Speed, true) : FVCLocomotionPace{ Speed, 1.f };
	const FVCLocomotionPace SprintPace = bPace && Set->HasSprint() ? Set->ComputeSprintPace(Speed) : FVCLocomotionPace{ Speed, 1.f };
	const FVCLocomotionPace CrouchPace = bPace && CrouchSet ? CrouchSet->ComputeCrouchPace(Speed, false) : FVCLocomotionPace{ Speed, 1.f };
	const FVCLocomotionPace AimCrouchPace = bPace && CrouchSet ? CrouchSet->ComputeCrouchPace(Speed, true) : FVCLocomotionPace{ Speed, 1.f };
	CycleBlendSpeed = CyclePace.BlendSpeed;
	CyclePlayRate = CyclePace.PlayRate;
	AimCycleBlendSpeed = AimPace.BlendSpeed;
	AimCyclePlayRate = AimPace.PlayRate;
	SprintBlendSpeed = SprintPace.BlendSpeed;
	SprintPlayRate = SprintPace.PlayRate;
	CrouchCycleBlendSpeed = CrouchPace.BlendSpeed;
	CrouchCyclePlayRate = CrouchPace.PlayRate;
	AimCrouchCycleBlendSpeed = AimCrouchPace.BlendSpeed;
	AimCrouchCyclePlayRate = AimCrouchPace.PlayRate;

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
	else if (LocomotionState == EVCLocomotionState::CrouchChange && CrouchChangeClip)
	{
		CrouchChangeTime = FMath::Min(CrouchChangeTime + Input.DeltaSeconds, CrouchChangeClip->GetPlayLength());
	}

	// A crouch change at a stand plays the crouch set's begin or end; one that flips back mid-way plays the other from
	// the matching point, so the pelvis never jumps.
	const bool bCrouchChanged = bCrouchPose != bWasCrouchPose;
	bWasCrouchPose = bCrouchPose;
	UAnimSequenceBase* ChangeClip = bCrouchChanged && CrouchSet
		? (bCrouchPose ? CrouchSet->CrouchBegin.Get() : CrouchSet->CrouchEnd.Get()) : nullptr;
	if (ChangeClip && LocomotionState == EVCLocomotionState::CrouchChange && CrouchChangeClip)
	{
		const float Done = CrouchChangeTime / FMath::Max(CrouchChangeClip->GetPlayLength(), UE_KINDA_SMALL_NUMBER);
		CrouchChangeClip = ChangeClip;
		CrouchChangeTime = (1.f - FMath::Clamp(Done, 0.f, 1.f)) * ChangeClip->GetPlayLength();
	}

	// What the set has for this update: a start the way the input goes (forward of the body when the capsule turns to
	// it, so the body eases round into the run), a stop the way the body moves, a turn back to the facing. Crouched, the
	// crouch set's starts and stops.
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
	// A reversal (L3): the input turned against the velocity on the move, in orient mode (the capsule already faces it).
	const bool bReversal = Tuning.ReversalAngle > 0.f && Input.bOrientToMovement && bHasInput && bMoving && Input.bOnGround
		&& InputAngleToVelocity(LocalAcceleration, LocalVelocity) >= Tuning.ReversalAngle;
	FVCLocomotionPick StartPick;
	FVCLocomotionPick StopPick;
	if (bCrouchPose)
	{
		StartPick = bHasInput ? CrouchSet->ChooseCrouchStart(InputDegrees, bAimCrouch) : FVCLocomotionPick();
		StopPick = (!bHasInput || bReversal) && bMoving ? CrouchSet->ChooseCrouchStop(CycleDirection, bAimCrouch) : FVCLocomotionPick();
	}
	else if (Set)
	{
		StartPick = bHasInput ? Set->ChooseStart(ToLocomotionGait(Input.Gait), InputDegrees, bAimCycle) : FVCLocomotionPick();
		StopPick = (!bHasInput || bReversal) && bMoving
			? Set->ChooseStop(StopGaitAt(Set, Speed, bInSprintNow), CycleDirection, bAimCycle) : FVCLocomotionPick();
	}
	const bool bTurnable = bStanding && !bHasInput && !Input.bDead && !Input.bDowned && !bCrouchPose
		&& FMath::Abs(RootYawOffset) >= Tuning.TurnInPlaceAngle;
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
	Step.bWantsCrouchChange = ChangeClip && bStanding && !bHasInput;
	Step.bReversal = bReversal;
	Step.bReversalStop = bReversalStop;
	// The sprint: the sprint gait running forward of the body, past the run's cap (less, once in it, so it holds).
	const float SprintFloor = Set ? Set->RunSpeed + (bInSprintNow ? -Tuning.SprintEnterMargin : Tuning.SprintEnterMargin) : 0.f;
	const float SprintAngle = Tuning.SprintMaxAngle + (bInSprintNow ? 15.f : 0.f);
	Step.bWantsSprint = Set && Set->HasSprint() && Input.Gait == EVCGait::Sprint && !bAimCycle && Speed > SprintFloor
		&& FMath::Abs(CycleDirection) <= SprintAngle;
	const bool bStartDiverged = !Input.bOrientToMovement && bHasInput
		&& FMath::Abs(FMath::FindDeltaAngleDegrees(StartDegrees, InputDegrees)) > StartDirectionTolerance;
	Step.bStartDone = !StartClip || StartTime >= StartClip->GetPlayLength() - Tuning.TransitionBlendSeconds * StartRate
		|| (Input.MaxSpeed > 0.f && Speed >= Tuning.StartExitSpeedFraction * Input.MaxSpeed) || bStartDiverged || bCrouchChanged;
	Step.bStopDone = !StopClip || StopTime >= StopClip->GetPlayLength();
	Step.bTurnDone = !TurnClip || TurnTime >= TurnClip->GetPlayLength() || bCrouchPose;
	Step.bCrouchChangeDone = !CrouchChangeClip || CrouchChangeTime >= CrouchChangeClip->GetPlayLength() - Tuning.TransitionBlendSeconds;
	Step.MovingSpeedThreshold = MovingSpeedThreshold;
	Step.LandMinSeconds = LandMinSeconds;
	Step.JumpEndSeconds = JumpStartClip ? JumpStartClip->GetPlayLength() - Tuning.TransitionBlendSeconds : 0.f;
	Step.LandEndSeconds = LandClip ? LandClip->GetPlayLength() - Tuning.TransitionBlendSeconds : 0.f;

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
			bReversalStop = bReversal;
			// Entered where its travel left is the capsule's: the braking distance, or a reversal's (the input against the
			// velocity stops it sooner), so its feet land where the capsule stops.
			const float Braking = FMath::Max(Input.MaxBrakingDeceleration, 1.f);
			const float Remaining = bReversal ? ReversalStopDistance(Speed, Input.MaxAcceleration, Input.GroundFriction)
				: Speed * Speed / (2.f * Braking);
			const UAnimSequenceBase* Clip = StopClip;
			StopTime = Clip ? UVCLocomotionClipSet::FindStopEntryTime(
				[Clip](float Time) { return UVCLocomotionClipSet::RootDistanceAt(Clip, Time); },
				Clip->GetPlayLength(), Remaining) : 0.f;
			break;
		}
		case EVCLocomotionState::TurnInPlace:
			TurnClip = TurnPick.Clip;
			TurnTime = 0.f;
			TurnLastYaw = 0.f;
			TurnSign = TurnPick.TurnDegrees >= 0.f ? 1.f : -1.f;
			TurnScale = FMath::Abs(RootYawOffset) / FMath::Max(FMath::Abs(TurnPick.TurnDegrees), 1.f);
			break;
		case EVCLocomotionState::CrouchChange:
			CrouchChangeClip = ChangeClip;
			CrouchChangeTime = 0.f;
			break;
		default:
			break;
		}
		if (Next != EVCLocomotionState::Stop)
		{
			bReversalStop = false;
		}
		LocomotionState = Next;
		StateSeconds = 0.f;
	}
	bInIdle = LocomotionState == EVCLocomotionState::Idle;
	bInTurnInPlace = LocomotionState == EVCLocomotionState::TurnInPlace;
	bInStart = LocomotionState == EVCLocomotionState::Start;
	bInCycle = LocomotionState == EVCLocomotionState::Cycle;
	bInSprint = LocomotionState == EVCLocomotionState::Sprint;
	bInCrouchChange = LocomotionState == EVCLocomotionState::CrouchChange;
	bInStop = LocomotionState == EVCLocomotionState::Stop;
	bInJump = LocomotionState == EVCLocomotionState::Jump;
	bInFall = LocomotionState == EVCLocomotionState::Fall;
	bInLand = LocomotionState == EVCLocomotionState::Land;
	bInSwim = LocomotionState == EVCLocomotionState::Swim;

	const bool bLegsOnLocomotion = !Input.bDead && !Input.bDowned && Input.bOnGround && !Input.bSwimming && bMoving;
	ActionLegsWeight = FMath::FInterpConstantTo(ActionLegsWeight, bLegsOnLocomotion ? 1.f : 0.f, Input.DeltaSeconds, IKBlendSpeed);
}
