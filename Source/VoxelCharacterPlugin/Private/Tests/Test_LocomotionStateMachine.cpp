// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Core/VCLocomotionAnimInstance.h"
#include "Animation/VCLocomotionClipSet.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Movement/VCMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"

// ---------------------------------------------------------------------------
// The locomotion state machine's choices (LOCOMOTION_STATE_MACHINE_PLAN.md L1, D87-D89), pure and on the anim instance:
// - The clip choice by set, gait and direction: the nearest direction a pack has (six or eight), a missing gait from the
//   nearest the set has, the Cycle blend space's samples; each family from the held set, else the base set's.
// - The stance weight: 0 while the held set plays its own locomotion, the other graphs' weight otherwise.
// - A set change: the new clips go into the slot not playing and it becomes the playing one.
// - The state the graph follows (L2), the paces (D91), the starts, stops and turns with §3.3's missing-clip rules, the
//   stop's entry by distance, and on the anim instance the body's frame and the procedural turn.
// - The crouch (L3): own or borrowed, its pace, starts and stops, the begin and end; the tunables; the reversal.
// ---------------------------------------------------------------------------

namespace VCLocomotionTestHelpers
{
	constexpr float Dt = 1.0f / 60.0f;

	UAnimSequence* NewClip(const TCHAR* Name)
	{
		return NewObject<UAnimSequence>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UAnimSequence::StaticClass(), Name));
	}

	/** A pack's loops in six directions (the Essential Animation packs: no forward diagonals). */
	FVCLocomotionLoops SixWay(const TCHAR* Prefix, float Speed)
	{
		FVCLocomotionLoops Loops;
		Loops.Forward = NewClip(*FString::Printf(TEXT("%s_F"), Prefix));
		Loops.Right = NewClip(*FString::Printf(TEXT("%s_R"), Prefix));
		Loops.BackwardRight = NewClip(*FString::Printf(TEXT("%s_BR"), Prefix));
		Loops.Backward = NewClip(*FString::Printf(TEXT("%s_B"), Prefix));
		Loops.BackwardLeft = NewClip(*FString::Printf(TEXT("%s_BL"), Prefix));
		Loops.Left = NewClip(*FString::Printf(TEXT("%s_L"), Prefix));
		Loops.AuthoredSpeed = Speed;
		return Loops;
	}

	/** A set with its own idle, walk, jog and Cycle, and a jump; no landing on the move. */
	UVCLocomotionClipSet* NewSet(const TCHAR* Name)
	{
		UVCLocomotionClipSet* Set = NewObject<UVCLocomotionClipSet>(GetTransientPackage());
		Set->Idle = NewClip(*FString::Printf(TEXT("%s_Idle"), Name));
		Set->Walk = SixWay(*FString::Printf(TEXT("%s_Walk"), Name), 92.f);
		Set->Jog = SixWay(*FString::Printf(TEXT("%s_Jog"), Name), 302.f);
		Set->Cycle = NewObject<UBlendSpace>(GetTransientPackage());
		Set->JumpStart = NewClip(*FString::Printf(TEXT("%s_Jump"), Name));
		Set->FallLoop = NewClip(*FString::Printf(TEXT("%s_Fall"), Name));
		Set->Land = NewClip(*FString::Printf(TEXT("%s_Land"), Name));
		return Set;
	}

	UVCLocomotionAnimInstance* NewAnimInstance()
	{
		USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(GetTransientPackage());
		return NewObject<UVCLocomotionAnimInstance>(Mesh);
	}

	/** Standing on a plane with the default gravity, holding a set. */
	FVCAnimInput Standing(const UVCLocomotionClipSet* Held, const UVCLocomotionClipSet* Base)
	{
		FVCAnimInput In;
		In.DeltaSeconds = Dt;
		In.bOnGround = true;
		In.MaxAcceleration = 800.f;
		In.MaxBrakingDeceleration = 2000.f;
		In.LocomotionClips = Held;
		In.BaseLocomotionClips = Base;
		return In;
	}

	void Update(UVCLocomotionAnimInstance* Anim, const FVCAnimInput& In)
	{
		Anim->ComputeValues(In);
		Anim->ComputeLocomotion(In);
	}
}

#define VC_LOCO_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// The clip choice by set, gait and direction.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCLocomotion_ClipChoice, "VoxelCharacter.Locomotion.ClipChoice", VC_LOCO_FLAGS)
bool FVCLocomotion_ClipChoice::RunTest(const FString& Parameters)
{
	using namespace VCLocomotionTestHelpers;

	// The eight directions by angle, positive to the right, any wrap.
	TestTrue(TEXT("0 is forward"), UVCLocomotionClipSet::DirectionFromAngle(0.f) == EVCLocomotionDirection::Forward);
	TestTrue(TEXT("22 is forward"), UVCLocomotionClipSet::DirectionFromAngle(22.f) == EVCLocomotionDirection::Forward);
	TestTrue(TEXT("23 is forward right"), UVCLocomotionClipSet::DirectionFromAngle(23.f) == EVCLocomotionDirection::ForwardRight);
	TestTrue(TEXT("90 is right"), UVCLocomotionClipSet::DirectionFromAngle(90.f) == EVCLocomotionDirection::Right);
	TestTrue(TEXT("-90 is left"), UVCLocomotionClipSet::DirectionFromAngle(-90.f) == EVCLocomotionDirection::Left);
	TestTrue(TEXT("180 is backward"), UVCLocomotionClipSet::DirectionFromAngle(180.f) == EVCLocomotionDirection::Backward);
	TestTrue(TEXT("-180 is backward"), UVCLocomotionClipSet::DirectionFromAngle(-180.f) == EVCLocomotionDirection::Backward);
	TestTrue(TEXT("-135 is backward left"), UVCLocomotionClipSet::DirectionFromAngle(-135.f) == EVCLocomotionDirection::BackwardLeft);
	TestTrue(TEXT("405 wraps to forward right"), UVCLocomotionClipSet::DirectionFromAngle(405.f) == EVCLocomotionDirection::ForwardRight);
	for (uint8 Index = 0; Index < 8; ++Index)
	{
		const EVCLocomotionDirection Direction = static_cast<EVCLocomotionDirection>(Index);
		TestTrue(TEXT("A direction's angle maps back to it"), UVCLocomotionClipSet::DirectionFromAngle(UVCLocomotionClipSet::AngleOfDirection(Direction)) == Direction);
	}

	// A six-direction pack: the nearest direction it has.
	UVCLocomotionClipSet* Set = NewSet(TEXT("Bow"));
	TestTrue(TEXT("Forward plays F"), Set->ChooseLoop(EVCLocomotionGait::Walk, 0.f) == Set->Walk.Forward);
	TestTrue(TEXT("30 plays F (no diagonal)"), Set->ChooseLoop(EVCLocomotionGait::Walk, 30.f) == Set->Walk.Forward);
	TestTrue(TEXT("45 is a tie: F before the strafe"), Set->ChooseLoop(EVCLocomotionGait::Walk, 45.f) == Set->Walk.Forward);
	TestTrue(TEXT("60 plays R"), Set->ChooseLoop(EVCLocomotionGait::Walk, 60.f) == Set->Walk.Right);
	TestTrue(TEXT("125 plays BR"), Set->ChooseLoop(EVCLocomotionGait::Jog, 125.f) == Set->Jog.BackwardRight);
	TestTrue(TEXT("-170 plays B"), Set->ChooseLoop(EVCLocomotionGait::Jog, -170.f) == Set->Jog.Backward);
	TestTrue(TEXT("-100 plays L"), Set->ChooseLoop(EVCLocomotionGait::Jog, -100.f) == Set->Jog.Left);
	TestTrue(TEXT("The jog's own clips"), Set->ChooseLoop(EVCLocomotionGait::Jog, 0.f) == Set->Jog.Forward);

	// A gait the set lacks: the run plays the jog; a set without a walk walks in its jog.
	TestTrue(TEXT("No run: the jog"), Set->ChooseLoop(EVCLocomotionGait::Run, 90.f) == Set->Jog.Right);
	Set->Run.Forward = NewClip(TEXT("Bow_Run_F"));
	Set->Run.AuthoredSpeed = 446.f;
	TestTrue(TEXT("A forward-only run plays forward"), Set->ChooseLoop(EVCLocomotionGait::Run, 90.f) == Set->Run.Forward);
	UVCLocomotionClipSet* NoWalk = NewSet(TEXT("SSH"));
	NoWalk->Walk = FVCLocomotionLoops();
	TestTrue(TEXT("No walk: the jog"), NoWalk->ChooseLoop(EVCLocomotionGait::Walk, 0.f) == NoWalk->Jog.Forward);
	TestNull(TEXT("No loops: nothing"), NewObject<UVCLocomotionClipSet>()->ChooseLoop(EVCLocomotionGait::Walk, 0.f));

	// An eight-direction set (the template Unarmed) plays its diagonals.
	UVCLocomotionClipSet* Eight = NewSet(TEXT("Unarmed"));
	Eight->Jog.ForwardRight = NewClip(TEXT("Unarmed_Jog_FR"));
	Eight->Jog.ForwardLeft = NewClip(TEXT("Unarmed_Jog_FL"));
	TestTrue(TEXT("Eight ways: 40 plays FR"), Eight->ChooseLoop(EVCLocomotionGait::Jog, 40.f) == Eight->Jog.ForwardRight);
	TestTrue(TEXT("Eight ways: -50 plays FL"), Eight->ChooseLoop(EVCLocomotionGait::Jog, -50.f) == Eight->Jog.ForwardLeft);

	// The Cycle's samples: the walk and the jog in six directions with Backward at both ends, the idle along speed 0; the
	// run stays out (the sprint is L2).
	TArray<UAnimSequence*> Clips;
	TArray<FVector2D> Positions;
	Set->GetCycleSamples(false, Clips, Positions);
	TestEqual(TEXT("Two gaits of 7 samples, 7 idles"), Clips.Num(), 21);
	TestEqual(TEXT("A position per clip"), Positions.Num(), Clips.Num());
	TestTrue(TEXT("The walk forward at its speed"), Positions.Contains(FVector2D(0.0, 92.0)) && Clips[Positions.IndexOfByKey(FVector2D(0.0, 92.0))] == Set->Walk.Forward);
	TestTrue(TEXT("The jog backward at 180"), Clips[Positions.IndexOfByKey(FVector2D(180.0, 302.0))] == Set->Jog.Backward);
	TestTrue(TEXT("and at -180"), Clips[Positions.IndexOfByKey(FVector2D(-180.0, 302.0))] == Set->Jog.Backward);
	TestTrue(TEXT("The idle at -180, speed 0"), Clips[Positions.IndexOfByKey(FVector2D(-180.0, 0.0))] == Set->Idle);
	TestFalse(TEXT("No run sample"), Clips.Contains(Set->Run.Forward.Get()));
	TestEqual(TEXT("The top speed is the jog's"), Set->GetCycleTopSpeed(), 302.f);
	Set->GetCycleSamples(true, Clips, Positions);
	TestEqual(TEXT("No aim walk: the aim cycle's samples are the cycle's"), Clips.Num(), 21);

	// The aim cycle (the bow's hold C: side-on, no forward or backward loop): its walk and idle, the jog above them.
	Set->AimWalk = SixWay(TEXT("Bow_C_Walk"), 91.5f);
	Set->AimWalk.Forward = nullptr;
	Set->AimWalk.Backward = nullptr;
	Set->AimWalk.ForwardLeft = NewClip(TEXT("Bow_C_Walk_FL"));
	Set->AimWalk.ForwardRight = NewClip(TEXT("Bow_C_Walk_FR"));
	Set->AimIdle = NewClip(TEXT("Bow_C_Idle"));
	Set->GetCycleSamples(true, Clips, Positions);
	TestTrue(TEXT("The aim walk at its speed"), Clips[Positions.IndexOfByKey(FVector2D(45.0, 91.5))] == Set->AimWalk.ForwardRight);
	TestFalse(TEXT("No carry walk in the aim cycle"), Clips.Contains(Set->Walk.Forward.Get()));
	TestTrue(TEXT("The jog above it"), Clips[Positions.IndexOfByKey(FVector2D(0.0, 302.0))] == Set->Jog.Forward);
	TestTrue(TEXT("The aim idle at speed 0"), Clips[Positions.IndexOfByKey(FVector2D(0.0, 0.0))] == Set->AimIdle);
	TestEqual(TEXT("Six aim walks, seven jogs, nine idles"), Clips.Num(), 6 + 7 + 9);

	// Each family from the held set, else the base set's.
	UVCLocomotionClipSet* Base = NewSet(TEXT("Unarmed"));
	FVCLocomotionClips Resolved = UVCLocomotionAnimInstance::ResolveClips(Set, Base);
	TestTrue(TEXT("A set with its own locomotion"), Resolved.bOwnLocomotion);
	TestTrue(TEXT("plays its idle and cycle"), Resolved.Idle == Set->Idle && Resolved.Cycle == Set->Cycle);
	TestTrue(TEXT("and its jump"), Resolved.JumpStart == Set->JumpStart && Resolved.FallLoop == Set->FallLoop);
	TestTrue(TEXT("No moving landing: its standing one"), Resolved.LandMoving == Set->Land);
	TestEqual(TEXT("The cycle's top speed"), Resolved.CycleTopSpeed, 302.f);
	TestNull(TEXT("No aim cycle yet"), Resolved.AimCycle.Get());
	Set->AimCycle = NewObject<UBlendSpace>(GetTransientPackage());
	Resolved = UVCLocomotionAnimInstance::ResolveClips(Set, Base);
	TestTrue(TEXT("The held set's aim cycle"), Resolved.AimCycle == Set->AimCycle);
	TestTrue(TEXT("Aiming plays it"), UVCLocomotionAnimInstance::UsesAimCycle(true, Resolved));
	TestFalse(TEXT("Not aiming does not"), UVCLocomotionAnimInstance::UsesAimCycle(false, Resolved));
	Base->AimCycle = NewObject<UBlendSpace>(GetTransientPackage());
	TestNull(TEXT("Never the base set's"), UVCLocomotionAnimInstance::ResolveClips(nullptr, Base).AimCycle.Get());

	UVCLocomotionClipSet* JumpOnly = NewObject<UVCLocomotionClipSet>(GetTransientPackage());
	JumpOnly->JumpStart = NewClip(TEXT("Partial_Jump"));
	Resolved = UVCLocomotionAnimInstance::ResolveClips(JumpOnly, Base);
	TestFalse(TEXT("A set without an idle and a cycle has no locomotion of its own"), Resolved.bOwnLocomotion);
	TestTrue(TEXT("It plays the base set's idle and cycle"), Resolved.Idle == Base->Idle && Resolved.Cycle == Base->Cycle);
	TestTrue(TEXT("but its own jump"), Resolved.JumpStart == JumpOnly->JumpStart);
	TestTrue(TEXT("and the base set's fall"), Resolved.FallLoop == Base->FallLoop);

	Resolved = UVCLocomotionAnimInstance::ResolveClips(nullptr, Base);
	TestTrue(TEXT("No clip set: the base set's whole"), !Resolved.bOwnLocomotion && Resolved.Idle == Base->Idle && Resolved.Land == Base->Land);
	Resolved = UVCLocomotionAnimInstance::ResolveClips(nullptr, nullptr);
	TestTrue(TEXT("Nothing at all: nothing"), !Resolved.Idle && !Resolved.Cycle && !Resolved.Land);
	return true;
}

// ---------------------------------------------------------------------------
// The stance weight: none under a set's own locomotion, the other graphs' weight under the base set's.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCLocomotion_StanceWeight, "VoxelCharacter.Locomotion.StanceWeight", VC_LOCO_FLAGS)
bool FVCLocomotion_StanceWeight::RunTest(const FString& Parameters)
{
	using namespace VCLocomotionTestHelpers;

	TestEqual(TEXT("Own locomotion: 0"), UVCLocomotionAnimInstance::ComputeStanceTarget(1.f, true), 0.f);
	TestEqual(TEXT("Own locomotion at a sprint: 0"), UVCLocomotionAnimInstance::ComputeStanceTarget(0.35f, true), 0.f);
	TestEqual(TEXT("The base set's: the target"), UVCLocomotionAnimInstance::ComputeStanceTarget(0.35f, false), 0.35f);

	UVCLocomotionClipSet* Base = NewSet(TEXT("Unarmed"));
	UVCLocomotionClipSet* Bow = NewSet(TEXT("Bow"));
	UVCLocomotionAnimInstance* Anim = NewAnimInstance();
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Update(Anim, Standing(Bow, Base));
	}
	TestEqual(TEXT("Holding a set with its own locomotion: no stance"), Anim->StanceWeight, 0.f);
	TestTrue(TEXT("and the graph knows it"), Anim->bOwnLocomotion);

	// A set without a clip set of its own (none yet, but a new pack's before its clips): the stance over the base set's.
	Update(Anim, Standing(nullptr, Base));
	TestTrue(TEXT("The stance eases in, it does not snap"), Anim->StanceWeight > 0.f && Anim->StanceWeight < 1.f);
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Update(Anim, Standing(nullptr, Base));
	}
	TestTrue(TEXT("Then holds at 1"), FMath::IsNearlyEqual(Anim->StanceWeight, 1.f));
	TestFalse(TEXT("No own locomotion"), Anim->bOwnLocomotion);
	Update(Anim, Standing(Bow, Base));
	TestTrue(TEXT("Back to the bow: it eases out"), Anim->StanceWeight > 0.f && Anim->StanceWeight < 1.f);

	// The plain anim instance (ABP_VoxelCharacter, motion matching) is unchanged by a clip set.
	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	UVCAnimInstance* Plain = NewObject<UVCAnimInstance>(Mesh);
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Plain->ComputeValues(Standing(Bow, Base));
	}
	TestTrue(TEXT("The other graphs keep the stance"), FMath::IsNearlyEqual(Plain->StanceWeight, 1.f));
	return true;
}

// ---------------------------------------------------------------------------
// A set change: the slot not playing takes the new clips and plays; nothing flips while the clips stay.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCLocomotion_SetChange, "VoxelCharacter.Locomotion.SetChange", VC_LOCO_FLAGS)
bool FVCLocomotion_SetChange::RunTest(const FString& Parameters)
{
	using namespace VCLocomotionTestHelpers;

	UVCLocomotionClipSet* Base = NewSet(TEXT("Unarmed"));
	UVCLocomotionClipSet* Bow = NewSet(TEXT("Bow"));
	UVCLocomotionClipSet* Sword = NewSet(TEXT("SSH"));
	const FVCLocomotionClips UnarmedClips = UVCLocomotionAnimInstance::ResolveClips(nullptr, Base);
	const FVCLocomotionClips BowClips = UVCLocomotionAnimInstance::ResolveClips(Bow, Base);
	const FVCLocomotionClips SwordClips = UVCLocomotionAnimInstance::ResolveClips(Sword, Base);

	FVCLocomotionSlots Slots;
	TestFalse(TEXT("The first fill does not flip"), UVCLocomotionAnimInstance::ApplySetChange(Slots, UnarmedClips));
	TestTrue(TEXT("It fills both slots"), Slots.IdleA == Base->Idle && Slots.IdleB == Base->Idle && Slots.CycleA == Base->Cycle && Slots.CycleB == Base->Cycle);
	TestFalse(TEXT("A plays"), Slots.bSlotB);
	TestFalse(TEXT("The same clips: no flip"), UVCLocomotionAnimInstance::ApplySetChange(Slots, UnarmedClips));

	TestTrue(TEXT("The bow drawn: a flip"), UVCLocomotionAnimInstance::ApplySetChange(Slots, BowClips));
	TestTrue(TEXT("B plays the bow"), Slots.bSlotB && Slots.IdleB == Bow->Idle && Slots.CycleB == Bow->Cycle);
	TestTrue(TEXT("A still holds Unarmed (the blend's source)"), Slots.IdleA == Base->Idle && Slots.CycleA == Base->Cycle);
	TestFalse(TEXT("Held: no flip"), UVCLocomotionAnimInstance::ApplySetChange(Slots, BowClips));

	TestTrue(TEXT("The sword: a flip"), UVCLocomotionAnimInstance::ApplySetChange(Slots, SwordClips));
	TestTrue(TEXT("A plays the sword, B keeps the bow"), !Slots.bSlotB && Slots.CycleA == Sword->Cycle && Slots.CycleB == Bow->Cycle);

	// On the anim instance: the values the graph reads follow, and the base pose's families too.
	UVCLocomotionAnimInstance* Anim = NewAnimInstance();
	Update(Anim, Standing(nullptr, Base));
	TestTrue(TEXT("Unarmed in A"), !Anim->bSlotB && Anim->IdleA == Base->Idle && Anim->CycleA == Base->Cycle);
	TestTrue(TEXT("Unarmed's jump"), Anim->JumpStartClip == Base->JumpStart);
	FVCAnimInput Moving = Standing(Bow, Base);
	Moving.Velocity = FVector(400.0, 0.0, 0.0);
	Update(Anim, Moving);
	TestTrue(TEXT("The bow in B, playing"), Anim->bSlotB && Anim->IdleB == Bow->Idle && Anim->CycleB == Bow->Cycle);
	TestTrue(TEXT("The bow's jump"), Anim->JumpStartClip == Bow->JumpStart && Anim->FallLoopClip == Bow->FallLoop);
	TestTrue(TEXT("At the run's cap the cycle plays the jog whole, faster (D91)"),
		FMath::IsNearlyEqual(Anim->CycleBlendSpeed, 302.f, 0.01f) && FMath::IsNearlyEqual(Anim->CyclePlayRate, 400.f / 302.f, 1.0e-3f));
	Moving.Velocity = FVector(604.0, 0.0, 0.0);
	Update(Anim, Moving);
	TestTrue(TEXT("At twice the jog's speed it plays twice as fast"), FMath::IsNearlyEqual(Anim->CyclePlayRate, 2.f, 1.0e-3f));
	return true;
}

// ---------------------------------------------------------------------------
// The state the graph follows (L2: StepState), and the landing chosen at the touch-down.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCLocomotion_States, "VoxelCharacter.Locomotion.States", VC_LOCO_FLAGS)
bool FVCLocomotion_States::RunTest(const FString& Parameters)
{
	using namespace VCLocomotionTestHelpers;
	using EState = EVCLocomotionState;

	FVCLocomotionStepInput In;
	In.bOnGround = true;
	TestTrue(TEXT("Standing: idle"), UVCLocomotionAnimInstance::StepState(EState::Idle, In) == EState::Idle);
	In.bHasInput = true;
	TestTrue(TEXT("Input, no start clip: straight into the cycle (D88)"), UVCLocomotionAnimInstance::StepState(EState::Idle, In) == EState::Cycle);
	In.bHasStart = true;
	TestTrue(TEXT("Input with a start: the start"), UVCLocomotionAnimInstance::StepState(EState::Idle, In) == EState::Start);
	TestTrue(TEXT("The start holds"), UVCLocomotionAnimInstance::StepState(EState::Start, In) == EState::Start);
	In.bStartDone = true;
	TestTrue(TEXT("then hands over to the cycle"), UVCLocomotionAnimInstance::StepState(EState::Start, In) == EState::Cycle);
	In.bWantsSprint = true;
	TestTrue(TEXT("or the sprint"), UVCLocomotionAnimInstance::StepState(EState::Start, In) == EState::Sprint);
	TestTrue(TEXT("The cycle into the sprint"), UVCLocomotionAnimInstance::StepState(EState::Cycle, In) == EState::Sprint);
	In.bWantsSprint = false;
	TestTrue(TEXT("and back"), UVCLocomotionAnimInstance::StepState(EState::Sprint, In) == EState::Cycle);

	In.bHasInput = false;
	In.Speed = 300.f;
	TestTrue(TEXT("Released, no stop clip: the cycle coasts"), UVCLocomotionAnimInstance::StepState(EState::Cycle, In) == EState::Cycle);
	In.bHasStop = true;
	TestTrue(TEXT("Released with a stop: the stop"), UVCLocomotionAnimInstance::StepState(EState::Cycle, In) == EState::Stop);
	TestTrue(TEXT("from the sprint too"), UVCLocomotionAnimInstance::StepState(EState::Sprint, In) == EState::Stop);
	TestTrue(TEXT("The stop holds"), UVCLocomotionAnimInstance::StepState(EState::Stop, In) == EState::Stop);
	In.bStopDone = true;
	TestTrue(TEXT("then stands"), UVCLocomotionAnimInstance::StepState(EState::Stop, In) == EState::Idle);
	In.bHasInput = true;
	TestTrue(TEXT("Input in a stop while moving: the cycle"), UVCLocomotionAnimInstance::StepState(EState::Stop, In) == EState::Cycle);
	In.Speed = 0.f;
	TestTrue(TEXT("at a stand: a new start"), UVCLocomotionAnimInstance::StepState(EState::Stop, In) == EState::Start);
	In.bHasInput = false;
	TestTrue(TEXT("A start released at a stand: idle"), UVCLocomotionAnimInstance::StepState(EState::Start, In) == EState::Idle);

	In = FVCLocomotionStepInput();
	In.bOnGround = true;
	In.bWantsTurn = true;
	TestTrue(TEXT("Standing off the facing: a turn"), UVCLocomotionAnimInstance::StepState(EState::Idle, In) == EState::TurnInPlace);
	TestTrue(TEXT("The turn holds"), UVCLocomotionAnimInstance::StepState(EState::TurnInPlace, In) == EState::TurnInPlace);
	In.bTurnDone = true;
	TestTrue(TEXT("then stands"), UVCLocomotionAnimInstance::StepState(EState::TurnInPlace, In) == EState::Idle);
	In.bHasInput = true;
	In.bHasStart = true;
	TestTrue(TEXT("Input during a turn: the start"), UVCLocomotionAnimInstance::StepState(EState::TurnInPlace, In) == EState::Start);

	In = FVCLocomotionStepInput();
	In.bFalling = true;
	In.UpSpeed = 400.f;
	TestTrue(TEXT("Rising: a jump"), UVCLocomotionAnimInstance::StepState(EState::Cycle, In) == EState::Jump);
	In.JumpEndSeconds = 0.5f;
	In.StateSeconds = 0.2f;
	TestTrue(TEXT("The jump holds"), UVCLocomotionAnimInstance::StepState(EState::Jump, In) == EState::Jump);
	In.StateSeconds = 0.6f;
	TestTrue(TEXT("then falls at its end"), UVCLocomotionAnimInstance::StepState(EState::Jump, In) == EState::Fall);
	In.UpSpeed = -10.f;
	TestTrue(TEXT("Off a ledge: a fall"), UVCLocomotionAnimInstance::StepState(EState::Sprint, In) == EState::Fall);
	In.bFalling = false;
	In.bOnGround = true;
	In.StateSeconds = 0.f;
	In.LandEndSeconds = 0.4f;
	TestTrue(TEXT("Down: a landing"), UVCLocomotionAnimInstance::StepState(EState::Fall, In) == EState::Land);
	In.Speed = 300.f;
	TestTrue(TEXT("Landed on the move: the landing holds"), UVCLocomotionAnimInstance::StepState(EState::Land, In) == EState::Land);
	In.StateSeconds = 0.2f;
	TestTrue(TEXT("then the cycle"), UVCLocomotionAnimInstance::StepState(EState::Land, In) == EState::Cycle);
	In.Speed = 0.f;
	In.StateSeconds = 0.5f;
	TestTrue(TEXT("Landed standing: idle at its end"), UVCLocomotionAnimInstance::StepState(EState::Land, In) == EState::Idle);
	In.bSwimming = true;
	TestTrue(TEXT("Swimming from anywhere"), UVCLocomotionAnimInstance::StepState(EState::Start, In) == EState::Swim);
	In.bSwimming = false;
	TestTrue(TEXT("and out"), UVCLocomotionAnimInstance::StepState(EState::Swim, In) == EState::Idle);

	// Every step from every state stays on the graph's transitions, so its state machine can follow.
	TArray<FName> From;
	TArray<FName> To;
	UVCLocomotionAnimInstance::GetStateTransitions(From, To);
	TestTrue(TEXT("The script's list: one name pair per transition"), From.Num() == To.Num() && From.Num() > 40);
	FRandomStream Random(404);
	int32 Strays = 0;
	for (int32 Trial = 0; Trial < 20000; ++Trial)
	{
		FVCLocomotionStepInput R;
		R.bOnGround = Random.FRand() < 0.7f;
		R.bFalling = !R.bOnGround && Random.FRand() < 0.8f;
		R.bSwimming = Random.FRand() < 0.05f;
		R.bHasInput = Random.FRand() < 0.5f;
		R.Speed = Random.FRandRange(0.f, 800.f) * (Random.FRand() < 0.2f ? 0.f : 1.f);
		R.UpSpeed = Random.FRandRange(-500.f, 500.f);
		R.StateSeconds = Random.FRandRange(0.f, 1.f);
		R.bHasStart = Random.FRand() < 0.5f;
		R.bHasStop = Random.FRand() < 0.5f;
		R.bWantsTurn = Random.FRand() < 0.3f;
		R.bWantsSprint = Random.FRand() < 0.3f;
		R.bStartDone = Random.FRand() < 0.5f;
		R.bStopDone = Random.FRand() < 0.5f;
		R.bTurnDone = Random.FRand() < 0.5f;
		R.bWantsCrouchChange = Random.FRand() < 0.3f;
		R.bCrouchChangeDone = Random.FRand() < 0.5f;
		R.bReversal = Random.FRand() < 0.2f;
		R.bReversalStop = Random.FRand() < 0.5f;
		R.JumpEndSeconds = 0.5f;
		R.LandEndSeconds = 0.5f;
		const EState Current = static_cast<EState>(Random.RandRange(0, static_cast<int32>(EState::Swim)));
		Strays += UVCLocomotionAnimInstance::IsStateTransition(Current, UVCLocomotionAnimInstance::StepState(Current, R)) ? 0 : 1;
	}
	TestEqual(TEXT("No step outside the graph's transitions"), Strays, 0);

	// The landing: the moving one when the character touches down on the move, the standing one otherwise.
	UVCLocomotionClipSet* Base = NewSet(TEXT("Unarmed"));
	UVCLocomotionClipSet* Bow = NewSet(TEXT("Bow"));
	Bow->LandMoving = NewClip(TEXT("Bow_Land_Jog"));
	UVCLocomotionAnimInstance* Anim = NewAnimInstance();
	FVCAnimInput Air = Standing(Bow, Base);
	Air.bOnGround = false;
	Air.bFalling = true;
	Air.Velocity = FVector(300.0, 0.0, -400.0);
	Update(Anim, Air);
	TestTrue(TEXT("Falling: the fall"), Anim->bInFall && !Anim->bInLand);
	FVCAnimInput Touch = Standing(Bow, Base);
	Touch.Velocity = FVector(300.0, 0.0, 0.0);
	Update(Anim, Touch);
	TestTrue(TEXT("Landing on the move plays the moving landing"), Anim->LandClip == Bow->LandMoving);
	TestTrue(TEXT("and lands"), Anim->bInLand && Anim->GetLocomotionState() == EState::Land);
	Update(Anim, Air);
	Update(Anim, Standing(Bow, Base));
	TestTrue(TEXT("Landing standing plays the standing one"), Anim->LandClip == Bow->Land);
	return true;
}

// ---------------------------------------------------------------------------
// The paces (D91): each gait's loops whole at its cap, at the cap over their authored speed.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCLocomotion_Pace, "VoxelCharacter.Locomotion.Pace", VC_LOCO_FLAGS)
bool FVCLocomotion_Pace::RunTest(const FString& Parameters)
{
	using namespace VCLocomotionTestHelpers;

	const auto Near = [](float A, float B) { return FMath::IsNearlyEqual(A, B, 0.01f); };
	UVCLocomotionClipSet* Bow = NewSet(TEXT("Bow")); // walk 92 at 120, jog 302 at 400
	FVCLocomotionPace Pace = Bow->ComputeCyclePace(120.f, false);
	TestTrue(TEXT("At the walk's cap: the walk whole at 120 / 92"), Near(Pace.BlendSpeed, 92.f) && Near(Pace.PlayRate, 120.f / 92.f));
	Pace = Bow->ComputeCyclePace(400.f, false);
	TestTrue(TEXT("At the run's cap: the jog whole at 400 / 302"), Near(Pace.BlendSpeed, 302.f) && Near(Pace.PlayRate, 400.f / 302.f));
	Pace = Bow->ComputeCyclePace(260.f, false);
	TestTrue(TEXT("Halfway: halfway between, the feet on the ground"), Near(Pace.BlendSpeed, 197.f) && Near(Pace.PlayRate * Pace.BlendSpeed, 260.f));
	Pace = Bow->ComputeCyclePace(60.f, false);
	TestTrue(TEXT("Below the walk: toward the idle at the walk's rate"), Near(Pace.BlendSpeed, 46.f) && Near(Pace.PlayRate, 120.f / 92.f));
	Pace = Bow->ComputeCyclePace(700.f, false);
	TestTrue(TEXT("Past the run with no sprint: the jog quickens"), Near(Pace.BlendSpeed, 302.f) && Near(Pace.PlayRate, 700.f / 302.f));

	UVCLocomotionClipSet* Unarmed = NewSet(TEXT("Unarmed"));
	Unarmed->Walk.AuthoredSpeed = 300.f;
	Unarmed->Jog.AuthoredSpeed = 600.f;
	Unarmed->WalkSpeed = 200.f;
	Unarmed->RunSpeed = 500.f;
	Pace = Unarmed->ComputeCyclePace(200.f, false);
	TestTrue(TEXT("The template's walk, authored faster than its cap, plays slower"), Near(Pace.BlendSpeed, 300.f) && Near(Pace.PlayRate, 2.f / 3.f));

	Bow->AimWalk = SixWay(TEXT("Bow_C_Walk"), 80.f);
	Pace = Bow->ComputeCyclePace(120.f, true);
	TestTrue(TEXT("The aim cycle's walk is the aim walk's"), Near(Pace.BlendSpeed, 80.f) && Near(Pace.PlayRate, 1.5f));

	// The sprint: Run_01 to Run_02 by speed, at 1 between them.
	Bow->Run.Forward = NewClip(TEXT("Bow_Run_01"));
	Bow->Run.AuthoredSpeed = 446.f;
	Bow->RunFast.Forward = NewClip(TEXT("Bow_Run_02"));
	Bow->RunFast.AuthoredSpeed = 920.f;
	Pace = Bow->ComputeSprintPace(700.f);
	TestTrue(TEXT("The sprint's cap between the runs: a blend at 1"), Near(Pace.BlendSpeed, 700.f) && Near(Pace.PlayRate, 1.f));
	Pace = Bow->ComputeSprintPace(400.f);
	TestTrue(TEXT("Below Run_01: Run_01 slower"), Near(Pace.BlendSpeed, 446.f) && Near(Pace.PlayRate, 400.f / 446.f));
	TArray<UAnimSequence*> Clips;
	TArray<float> Speeds;
	Bow->GetSprintSamples(Clips, Speeds);
	TestTrue(TEXT("The sprint's samples: the runs at their speeds"), Clips.Num() == 2 && Clips[0] == Bow->Run.Forward && Speeds[1] == 920.f);
	TestFalse(TEXT("No sprint blend space yet: no sprint"), Bow->HasSprint());
	TestTrue(TEXT("The sprint's rate without one: the jog's past the run"),
		Near(Bow->GetGaitPlayRate(EVCLocomotionGait::Run, false), 700.f / 302.f));

	FVCLocomotionPace None = UVCLocomotionClipSet::ComputePace(250.f, {}, true);
	TestTrue(TEXT("No points: the speed as it is"), Near(None.BlendSpeed, 250.f) && Near(None.PlayRate, 1.f));
	return true;
}

// ---------------------------------------------------------------------------
// The starts, the stops and the turns (§3.3's missing-clip rules), and the stop's entry by distance.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCLocomotion_Transitions, "VoxelCharacter.Locomotion.Transitions", VC_LOCO_FLAGS)
bool FVCLocomotion_Transitions::RunTest(const FString& Parameters)
{
	using namespace VCLocomotionTestHelpers;

	const auto Near = [](float A, float B) { return FMath::IsNearlyEqual(A, B, 0.01f); };
	UVCLocomotionClipSet* Bow = NewSet(TEXT("Bow"));
	Bow->WalkStart = SixWay(TEXT("Bow_WalkStart"), 92.f);
	Bow->JogStart = SixWay(TEXT("Bow_JogStart"), 302.f);
	Bow->WalkStop = SixWay(TEXT("Bow_WalkStop"), 92.f);
	Bow->JogStop = SixWay(TEXT("Bow_JogStop"), 302.f);
	Bow->RunStop.Forward = NewClip(TEXT("Bow_RunStop"));
	Bow->RunStop.AuthoredSpeed = 446.f;
	Bow->AimWalkStart.Right = NewClip(TEXT("Bow_C_WalkStart_R"));
	Bow->AimWalkStart.AuthoredSpeed = 92.f;

	FVCLocomotionPick Pick = Bow->ChooseStart(EVCLocomotionGait::Walk, 0.f, false);
	TestTrue(TEXT("A walk start forward, at the walk's rate"), Pick.Clip == Bow->WalkStart.Forward && Near(Pick.PlayRate, 120.f / 92.f));
	TestTrue(TEXT("To the right: the right one"), Bow->ChooseStart(EVCLocomotionGait::Walk, 95.f, false).Clip == Bow->WalkStart.Right);
	TestTrue(TEXT("Behind: the backward one"), Bow->ChooseStart(EVCLocomotionGait::Walk, -170.f, false).Clip == Bow->WalkStart.Backward);
	Pick = Bow->ChooseStart(EVCLocomotionGait::Jog, 0.f, false);
	TestTrue(TEXT("A jog start at the jog's rate"), Pick.Clip == Bow->JogStart.Forward && Near(Pick.PlayRate, 400.f / 302.f));
	TestTrue(TEXT("A sprint from a stand starts as a jog"), Bow->ChooseStart(EVCLocomotionGait::Run, 0.f, false).Clip == Bow->JogStart.Forward);
	TestTrue(TEXT("Aiming: the aim walk's start"), Bow->ChooseStart(EVCLocomotionGait::Walk, 90.f, true).Clip == Bow->AimWalkStart.Right);
	TestTrue(TEXT("Aiming where the aim walk has none: its nearest"), Bow->ChooseStart(EVCLocomotionGait::Walk, 0.f, true).Clip == Bow->AimWalkStart.Right);

	Pick = Bow->ChooseStop(EVCLocomotionGait::Walk, 0.f, false);
	TestTrue(TEXT("A walk stop"), Pick.Clip == Bow->WalkStop.Forward && Near(Pick.PlayRate, 120.f / 92.f));
	TestTrue(TEXT("A jog stop to the left"), Bow->ChooseStop(EVCLocomotionGait::Jog, -90.f, false).Clip == Bow->JogStop.Left);
	TestTrue(TEXT("The run's stop forward"), Bow->ChooseStop(EVCLocomotionGait::Run, 10.f, false).Clip == Bow->RunStop.Forward);
	TestTrue(TEXT("Off forward the run stops as the jog"), Bow->ChooseStop(EVCLocomotionGait::Run, 90.f, false).Clip == Bow->JogStop.Right);

	// The sword sets: no walk start or stop, so the jog's at the walk's rate (§3.3).
	UVCLocomotionClipSet* Sword = NewSet(TEXT("SSH"));
	Sword->JogStart = SixWay(TEXT("SSH_JogStart"), 306.f);
	Sword->JogStop = SixWay(TEXT("SSH_JogStop"), 306.f);
	Pick = Sword->ChooseStart(EVCLocomotionGait::Walk, 0.f, false);
	TestTrue(TEXT("The sword's walk starts with the jog's start, at the walk's rate"), Pick.Clip == Sword->JogStart.Forward && Near(Pick.PlayRate, 120.f / 92.f));
	Pick = Sword->ChooseStop(EVCLocomotionGait::Walk, 0.f, false);
	TestTrue(TEXT("and stops with the jog's stop, at the walk's rate"), Pick.Clip == Sword->JogStop.Forward && Near(Pick.PlayRate, 120.f / 92.f));
	TestTrue(TEXT("The sword's run stops as its jog (no run stop)"), Sword->ChooseStop(EVCLocomotionGait::Run, 0.f, false).Clip == Sword->JogStop.Forward);

	// Unarmed: none, so the graph inertializes; no turns, so the procedural turn.
	UVCLocomotionClipSet* Unarmed = NewSet(TEXT("Unarmed"));
	TestNull(TEXT("Unarmed has no start"), Unarmed->ChooseStart(EVCLocomotionGait::Jog, 0.f, false).Clip);
	TestNull(TEXT("no stop"), Unarmed->ChooseStop(EVCLocomotionGait::Jog, 0.f, false).Clip);
	TestNull(TEXT("no turn"), Unarmed->ChooseTurn(-90.f).Clip);

	Bow->Turns.Left90 = NewClip(TEXT("Bow_Turn_L90"));
	Bow->Turns.Right90 = NewClip(TEXT("Bow_Turn_R90"));
	Bow->Turns.Left180 = NewClip(TEXT("Bow_Turn_L180"));
	Pick = Bow->ChooseTurn(-70.f);
	TestTrue(TEXT("The facing 70 to the right: the right 90"), Pick.Clip == Bow->Turns.Right90 && Near(Pick.TurnDegrees, 90.f));
	Pick = Bow->ChooseTurn(150.f);
	TestTrue(TEXT("150 to the left: the left 180"), Pick.Clip == Bow->Turns.Left180 && Near(Pick.TurnDegrees, -180.f));
	Pick = Bow->ChooseTurn(-150.f);
	TestTrue(TEXT("150 to the right without a right 180: the right 90"), Pick.Clip == Bow->Turns.Right90 && Near(Pick.TurnDegrees, 90.f));
	TestNull(TEXT("On the facing: none"), Bow->ChooseTurn(0.f).Clip);

	// The stop's entry: where its travel left is the braking distance.
	const auto Linear = [](float Time) { return 50.f * Time; }; // 50 uu over a second
	TestTrue(TEXT("10 uu to brake: entered at 0.8 s"), Near(UVCLocomotionClipSet::FindStopEntryTime(Linear, 1.f, 10.f), 0.8f));
	TestEqual(TEXT("More to brake than the clip travels: from its start"), UVCLocomotionClipSet::FindStopEntryTime(Linear, 1.f, 60.f), 0.f);
	const auto EaseOut = [](float Time) { return 40.f * (1.f - (1.f - Time) * (1.f - Time)); }; // a stop: fast, then settling
	const float Entry = UVCLocomotionClipSet::FindStopEntryTime(EaseOut, 1.f, 10.f);
	TestTrue(TEXT("An easing stop: the travel left from the entry is the braking distance"), Near(EaseOut(1.f) - EaseOut(Entry), 10.f));
	TestEqual(TEXT("Without a clip no root"), UVCLocomotionClipSet::RootDistanceAt(nullptr, 0.5f), 0.f);
	return true;
}

// ---------------------------------------------------------------------------
// On the anim instance: a start, a stop, a turn and the procedural turn, the body's frame.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCLocomotion_Instance, "VoxelCharacter.Locomotion.Instance", VC_LOCO_FLAGS)
bool FVCLocomotion_Instance::RunTest(const FString& Parameters)
{
	using namespace VCLocomotionTestHelpers;

	UVCLocomotionClipSet* Base = NewSet(TEXT("Unarmed"));
	UVCLocomotionClipSet* Bow = NewSet(TEXT("Bow"));
	Bow->WalkStart = SixWay(TEXT("Bow_WalkStart"), 92.f);
	Bow->JogStart = SixWay(TEXT("Bow_JogStart"), 302.f);
	Bow->JogStop = SixWay(TEXT("Bow_JogStop"), 302.f);
	Bow->Turns.Right90 = NewClip(TEXT("Bow_Turn_R90"));
	Bow->Turns.Left90 = NewClip(TEXT("Bow_Turn_L90"));

	// A start: input from a stand at the run gait plays the jog's start, then the cycle at the cap.
	UVCLocomotionAnimInstance* Anim = NewAnimInstance();
	FVCAnimInput In = Standing(Bow, Base);
	In.Gait = EVCGait::Run;
	In.MaxSpeed = 400.f;
	Update(Anim, In);
	TestTrue(TEXT("Standing: idle"), Anim->bInIdle);
	In.Acceleration = FVector(800.0, 0.0, 0.0);
	Update(Anim, In);
	TestTrue(TEXT("Input: the jog's start"), Anim->bInStart && Anim->StartClip == Bow->JogStart.Forward && Anim->StartTime == 0.f);
	In.Velocity = FVector(380.0, 0.0, 0.0);
	Update(Anim, In);
	TestTrue(TEXT("At the cap: the cycle"), Anim->bInCycle);

	// A stop: released at the run's cap, the jog's stop.
	In.Acceleration = FVector::ZeroVector;
	Update(Anim, In);
	TestTrue(TEXT("Released: the jog's stop"), Anim->bInStop && Anim->StopClip == Bow->JogStop.Forward);

	// The body's frame: the body 90 to the left of the facing, moving along the facing, steps to the body's right.
	UVCLocomotionAnimInstance* Frame = NewAnimInstance();
	FVCAnimInput Face = Standing(Bow, Base);
	Update(Frame, Face);
	Face.ActorRotation = FRotator(0.0, 90.0, 0.0).Quaternion();
	Update(Frame, Face);
	TestTrue(TEXT("The capsule turned 90 right: the body keeps its heading"), FMath::IsNearlyEqual(Frame->RootYawOffset, -90.f, 0.5f));
	TestTrue(TEXT("and turns in place to the right"), Frame->bInTurnInPlace && Frame->TurnClip == Bow->Turns.Right90);
	Face.Velocity = FVector(0.0, 300.0, 0.0);
	Face.Acceleration = FVector(0.0, 800.0, 0.0);
	Update(Frame, Face);
	TestTrue(TEXT("Moving along the facing: the cycle steps to the body's right"), Frame->CycleDirection > 45.f);
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Update(Frame, Face);
	}
	TestTrue(TEXT("and the body eases round to the facing"), FMath::Abs(Frame->RootYawOffset) < 5.f && FMath::Abs(Frame->CycleDirection) < 5.f);

	// Aiming (the body faces the view), a quick turn of the view drags the legs: never more than BodyMaxOffset behind.
	UVCLocomotionAnimInstance* Aim = NewAnimInstance();
	FVCAnimInput Drawn = Standing(Bow, Base);
	Drawn.bOrientToMovement = false;
	Update(Aim, Drawn);
	Drawn.ActorRotation = FRotator(0.0, 40.0, 0.0).Quaternion();
	Update(Aim, Drawn);
	TestTrue(TEXT("Aiming, the view 40 right: the legs keep their heading"), FMath::IsNearlyEqual(Aim->RootYawOffset, -40.f, 0.5f));
	Drawn.ActorRotation = FRotator(0.0, 170.0, 0.0).Quaternion();
	Update(Aim, Drawn);
	TestTrue(TEXT("A flick to 170: the legs dragged to within 90 of the torso"), FMath::Abs(Aim->RootYawOffset) <= Aim->BodyMaxOffset + 0.01f);
	TestTrue(TEXT("still behind it on the same side (no wrap the other way round)"), Aim->RootYawOffset < 0.f);

	// The procedural turn: a set without turns eases the body round once it is past the angle.
	UVCLocomotionAnimInstance* Plain = NewAnimInstance();
	FVCAnimInput Still = Standing(nullptr, Base);
	Update(Plain, Still);
	Still.ActorRotation = FRotator(0.0, 30.0, 0.0).Quaternion();
	Update(Plain, Still);
	Update(Plain, Still);
	TestTrue(TEXT("30 off: the body keeps its heading"), FMath::IsNearlyEqual(Plain->RootYawOffset, -30.f, 0.5f));
	Still.ActorRotation = FRotator(0.0, 80.0, 0.0).Quaternion();
	Update(Plain, Still);
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Update(Plain, Still);
	}
	TestTrue(TEXT("80 off: the body eases round, no turn state"), FMath::Abs(Plain->RootYawOffset) < 2.f && Plain->bInIdle);
	return true;
}

// ---------------------------------------------------------------------------
// The crouch (L3): the crouch set (own or borrowed), its pace, starts and stops, and on the anim instance the crouch's
// begin and end at a stand, the crouch cycle, the borrowed crouch's upper body.
// ---------------------------------------------------------------------------

namespace VCLocomotionTestHelpers
{
	/** The bow's crouch (hold B): an idle, the begin and end, the slow crouch walk and the crouch walk, their starts and stops. */
	void AddCrouch(UVCLocomotionClipSet* Set, const TCHAR* Name)
	{
		Set->CrouchIdle = NewClip(*FString::Printf(TEXT("%s_CrouchIdle"), Name));
		Set->CrouchBegin = NewClip(*FString::Printf(TEXT("%s_CrouchBegin"), Name));
		Set->CrouchEnd = NewClip(*FString::Printf(TEXT("%s_CrouchEnd"), Name));
		Set->CrouchWalkSlow = SixWay(*FString::Printf(TEXT("%s_CrouchSlow"), Name), 114.f);
		Set->CrouchWalk = SixWay(*FString::Printf(TEXT("%s_CrouchWalk"), Name), 302.f);
		Set->CrouchWalkSlowStart = SixWay(*FString::Printf(TEXT("%s_CrouchSlowStart"), Name), 114.f);
		Set->CrouchWalkStart = SixWay(*FString::Printf(TEXT("%s_CrouchWalkStart"), Name), 302.f);
		Set->CrouchWalkSlowStop = SixWay(*FString::Printf(TEXT("%s_CrouchSlowStop"), Name), 114.f);
		Set->CrouchWalkStop = SixWay(*FString::Printf(TEXT("%s_CrouchWalkStop"), Name), 302.f);
		Set->CrouchCycle = NewObject<UBlendSpace>(GetTransientPackage());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCLocomotion_Crouch, "VoxelCharacter.Locomotion.Crouch", VC_LOCO_FLAGS)
bool FVCLocomotion_Crouch::RunTest(const FString& Parameters)
{
	using namespace VCLocomotionTestHelpers;
	using EState = EVCLocomotionState;

	const auto Near = [](float A, float B) { return FMath::IsNearlyEqual(A, B, 0.01f); };
	UVCLocomotionClipSet* Base = NewSet(TEXT("Unarmed"));
	UVCLocomotionClipSet* Bow = NewSet(TEXT("Bow"));
	AddCrouch(Bow, TEXT("Bow"));
	UVCLocomotionClipSet* Sword = NewSet(TEXT("SSH"));

	// The crouch set: the bow's own; the sword's none until it borrows the bow's (§3.3).
	TestTrue(TEXT("The bow has its own crouch"), Bow->HasCrouch() && Bow->GetCrouchSet() == Bow);
	TestNull(TEXT("The sword has none"), Sword->GetCrouchSet());
	Sword->BorrowedCrouch = Bow;
	TestTrue(TEXT("Borrowing the bow's: the bow's crouch"), Sword->GetCrouchSet() == Bow);
	TestNull(TEXT("Not when borrowing is off (vc.Anim.SM.BorrowCrouch 0)"), Sword->GetCrouchSet(false));
	FVCLocomotionClips Clips = UVCLocomotionAnimInstance::ResolveClips(Sword, Base);
	TestTrue(TEXT("Resolved: the sword's loops on the bow's crouch, borrowed"), Clips.Loops == Sword && Clips.Crouch == Bow && Clips.bBorrowedCrouch);
	Clips = UVCLocomotionAnimInstance::ResolveClips(Bow, Base);
	TestTrue(TEXT("The bow's own: not borrowed"), Clips.Crouch == Bow && !Clips.bBorrowedCrouch);
	Clips = UVCLocomotionAnimInstance::ResolveClips(nullptr, Base);
	TestNull(TEXT("Unarmed borrowing nothing: no crouch (the walk's loops)"), Clips.Crouch);

	// The pace: the slow crouch walk whole at CrouchSlowSpeed, the crouch walk at CrouchSpeed, between them a blend.
	FVCLocomotionPace Pace = Bow->ComputeCrouchPace(150.f, false);
	TestTrue(TEXT("At 150: the slow crouch walk at 150 / 114"), Near(Pace.BlendSpeed, 114.f) && Near(Pace.PlayRate, 150.f / 114.f));
	Pace = Bow->ComputeCrouchPace(250.f, false);
	TestTrue(TEXT("At the cap: the crouch walk at 250 / 302"), Near(Pace.BlendSpeed, 302.f) && Near(Pace.PlayRate, 250.f / 302.f));
	Pace = Bow->ComputeCrouchPace(200.f, false);
	TestTrue(TEXT("Between: the feet on the ground"), Near(Pace.BlendSpeed * Pace.PlayRate, 200.f));
	TArray<UAnimSequence*> Samples;
	TArray<FVector2D> Positions;
	Bow->GetCrouchCycleSamples(false, Samples, Positions);
	TestTrue(TEXT("The crouch cycle's samples: both crouch walks and the crouch idle"),
		Samples.Contains(Bow->CrouchWalkSlow.Forward.Get()) && Samples.Contains(Bow->CrouchWalk.Forward.Get()) && Samples.Contains(Bow->CrouchIdle.Get()));

	// Starts and stops: the crouch walk's (it plays at the cap), the slow one's when the cap is the slow walk's.
	FVCLocomotionPick Pick = Bow->ChooseCrouchStart(0.f, false);
	TestTrue(TEXT("A crouch start: the crouch walk's, at the crouch's rate"), Pick.Clip == Bow->CrouchWalkStart.Forward && Near(Pick.PlayRate, 250.f / 302.f));
	TestTrue(TEXT("A crouch stop to the right"), Bow->ChooseCrouchStop(90.f, false).Clip == Bow->CrouchWalkStop.Right);
	Bow->CrouchSpeed = 150.f;
	TestTrue(TEXT("A cap near the slow walk's: the slow start"), Bow->ChooseCrouchStart(0.f, false).Clip == Bow->CrouchWalkSlowStart.Forward);
	Bow->AimCrouchWalk = SixWay(TEXT("Bow_C_CrouchSlow"), 114.f);
	Bow->AimCrouchWalkStart.Right = NewClip(TEXT("Bow_C_CrouchSlowStart_R"));
	Bow->AimCrouchWalkStart.AuthoredSpeed = 114.f;
	TestTrue(TEXT("Aiming: hold C's slow crouch start"), Bow->ChooseCrouchStart(90.f, true).Clip == Bow->AimCrouchWalkStart.Right);
	Bow->CrouchSpeed = 250.f;

	// The steps: a change at a stand plays the begin or end, input leaves it, and only the listed transitions occur.
	FVCLocomotionStepInput In;
	In.bOnGround = true;
	In.bWantsCrouchChange = true;
	TestTrue(TEXT("Crouching at a stand: the change"), UVCLocomotionAnimInstance::StepState(EState::Idle, In) == EState::CrouchChange);
	TestTrue(TEXT("It holds"), UVCLocomotionAnimInstance::StepState(EState::CrouchChange, In) == EState::CrouchChange);
	In.bCrouchChangeDone = true;
	TestTrue(TEXT("then stands (crouched)"), UVCLocomotionAnimInstance::StepState(EState::CrouchChange, In) == EState::Idle);
	In.bHasInput = true;
	In.bHasStart = true;
	TestTrue(TEXT("Input during it: the start"), UVCLocomotionAnimInstance::StepState(EState::CrouchChange, In) == EState::Start);
	TestTrue(TEXT("Listed: Idle to CrouchChange and back"), UVCLocomotionAnimInstance::IsStateTransition(EState::Idle, EState::CrouchChange)
		&& UVCLocomotionAnimInstance::IsStateTransition(EState::CrouchChange, EState::Idle));

	// On the anim instance: crouching at a stand plays the begin, then the crouch idle; moving, the crouch cycle.
	UVCLocomotionAnimInstance* Anim = NewAnimInstance();
	FVCAnimInput Stand = Standing(Bow, Base);
	Update(Anim, Stand);
	TestFalse(TEXT("Standing: not the crouch's pose"), Anim->bCrouchPose);
	FVCAnimInput Crouched = Stand;
	Crouched.bCrouching = true;
	Update(Anim, Crouched);
	TestTrue(TEXT("Crouched at a stand: the begin"), Anim->bInCrouchChange && Anim->CrouchChangeClip == Bow->CrouchBegin && Anim->bCrouchPose);
	TestTrue(TEXT("The idle plays the crouch idle"), Anim->CrouchIdleClip == Bow->CrouchIdle);
	Update(Anim, Stand);
	TestTrue(TEXT("Up again mid-way: the end"), Anim->CrouchChangeClip == Bow->CrouchEnd && !Anim->bCrouchPose);
	Update(Anim, Crouched);
	FVCAnimInput Walking = Crouched;
	Walking.Velocity = FVector(200.0, 0.0, 0.0);
	Walking.Acceleration = FVector(800.0, 0.0, 0.0);
	Walking.MaxSpeed = 250.f;
	Update(Anim, Walking);
	Update(Anim, Walking);
	TestTrue(TEXT("Crouched on the move: the crouch cycle"), Anim->CycleChoice == 2 && Anim->CrouchCycleClip == Bow->CrouchCycle);
	TestTrue(TEXT("at the crouch's pace"), Near(Anim->CrouchCycleBlendSpeed * Anim->CrouchCyclePlayRate, 200.f));
	TestEqual(TEXT("Its own crouch: no borrowed upper body"), Anim->BorrowedCrouchWeight, 0.f);

	// The borrowed crouch: the sword's idle over the upper body, eased in.
	UVCLocomotionAnimInstance* Borrow = NewAnimInstance();
	FVCAnimInput SwordCrouched = Standing(Sword, Base);
	SwordCrouched.bCrouching = true;
	for (int32 Step = 0; Step < 30; ++Step)
	{
		Update(Borrow, SwordCrouched);
	}
	TestTrue(TEXT("The sword crouched: the bow's crouch idle"), Borrow->bCrouchPose && Borrow->CrouchIdleClip == Bow->CrouchIdle);
	TestTrue(TEXT("under the sword's own idle upper body"), Borrow->BorrowedUpperClip == Sword->Idle && Near(Borrow->BorrowedCrouchWeight, 1.f));
	return true;
}

// ---------------------------------------------------------------------------
// The tunables (L3: vc.Anim.SM.* over the properties) and the reversal (a stop, then a start the other way).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCLocomotion_Tuning, "VoxelCharacter.Locomotion.Tuning", VC_LOCO_FLAGS)
bool FVCLocomotion_Tuning::RunTest(const FString& Parameters)
{
	using namespace VCLocomotionTestHelpers;
	using EState = EVCLocomotionState;

	TestEqual(TEXT("No override: the property"), UVCLocomotionAnimInstance::ResolveTunable(-1.f, 60.f), 60.f);
	TestEqual(TEXT("An override of 0 counts"), UVCLocomotionAnimInstance::ResolveTunable(0.f, 60.f), 0.f);
	TestEqual(TEXT("An override"), UVCLocomotionAnimInstance::ResolveTunable(45.f, 60.f), 45.f);

	TestTrue(TEXT("Input against the velocity: 180"),
		FMath::IsNearlyEqual(UVCLocomotionAnimInstance::InputAngleToVelocity(FVector(-1, 0, 0), FVector(300, 0, 0)), 180.f, 0.01f));
	TestTrue(TEXT("Across it: 90"), FMath::IsNearlyEqual(UVCLocomotionAnimInstance::InputAngleToVelocity(FVector(0, 1, 0), FVector(300, 0, 0)), 90.f, 0.01f));
	TestEqual(TEXT("Standing: 0"), UVCLocomotionAnimInstance::InputAngleToVelocity(FVector(1, 0, 0), FVector::ZeroVector), 0.f);

	// A reversal from 400 uu/s at friction 5 and 800 uu/s^2: v' = -10 v - 800 stops it in about 26 uu.
	const float Reversal = UVCLocomotionAnimInstance::ReversalStopDistance(400.f, 800.f, 5.f);
	TestTrue(TEXT("A reversal's stop: about 26 uu"), FMath::IsNearlyEqual(Reversal, 25.7f, 0.5f));
	TestTrue(TEXT("shorter than the braking stop (40 uu at 2000)"), Reversal < 400.f * 400.f / (2.f * 2000.f));
	TestTrue(TEXT("No friction: the acceleration alone"), FMath::IsNearlyEqual(UVCLocomotionAnimInstance::ReversalStopDistance(400.f, 800.f, 0.f), 100.f, 0.01f));

	FVCLocomotionStepInput In;
	In.bOnGround = true;
	In.bHasInput = true;
	In.Speed = 400.f;
	In.bReversal = true;
	In.bHasStop = true;
	TestTrue(TEXT("A reversal in the cycle: the stop"), UVCLocomotionAnimInstance::StepState(EState::Cycle, In) == EState::Stop);
	TestTrue(TEXT("in the sprint too"), UVCLocomotionAnimInstance::StepState(EState::Sprint, In) == EState::Stop);
	TestTrue(TEXT("The stop holds while the input is against the velocity"), UVCLocomotionAnimInstance::StepState(EState::Stop, In) == EState::Stop);
	In.bReversal = false;
	In.bReversalStop = true;
	In.bHasStart = true;
	In.Speed = 40.f;
	TestTrue(TEXT("The velocity turned: a start the new way"), UVCLocomotionAnimInstance::StepState(EState::Stop, In) == EState::Start);
	In.bReversalStop = false;
	TestTrue(TEXT("Not a reversal's stop, still moving: the cycle"), UVCLocomotionAnimInstance::StepState(EState::Stop, In) == EState::Cycle);

	// On the anim instance, with the reversal on: the stop the way the body went, the body held through it.
	UVCLocomotionClipSet* Base = NewSet(TEXT("Unarmed"));
	UVCLocomotionClipSet* Bow = NewSet(TEXT("Bow"));
	Bow->JogStop = SixWay(TEXT("Bow_JogStop"), 302.f);
	UVCLocomotionAnimInstance* Anim = NewAnimInstance();
	Anim->ReversalAngle = 135.f;
	FVCAnimInput Run = Standing(Bow, Base);
	Run.bOrientToMovement = true;
	Run.GroundFriction = 5.f;
	Run.MaxSpeed = 400.f;
	Run.Velocity = FVector(400.0, 0.0, 0.0);
	Run.Acceleration = FVector(800.0, 0.0, 0.0);
	for (int32 Step = 0; Step < 10; ++Step)
	{
		Update(Anim, Run);
	}
	TestTrue(TEXT("Running: the cycle"), Anim->bInCycle);
	FVCAnimInput Back = Run;
	Back.ActorRotation = FRotator(0.0, 170.0, 0.0).Quaternion(); // the capsule turned to the input at once
	Back.Acceleration = Back.ActorRotation.GetForwardVector() * 800.0;
	Update(Anim, Back);
	TestTrue(TEXT("Reversed: the stop forward of the body"), Anim->bInStop && Anim->StopClip == Bow->JogStop.Forward);
	const float Held = Anim->RootYawOffset;
	Update(Anim, Back);
	TestTrue(TEXT("The body held through it"), FMath::Abs(Held) > 120.f && FMath::IsNearlyEqual(Anim->RootYawOffset, Held, 0.5f));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
