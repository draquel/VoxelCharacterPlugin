// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Core/VCLocomotionAnimInstance.h"
#include "Animation/VCLocomotionClipSet.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Components/SkeletalMeshComponent.h"

// ---------------------------------------------------------------------------
// The locomotion state machine's choices (LOCOMOTION_STATE_MACHINE_PLAN.md L1, D87-D89), pure and on the anim instance:
// - The clip choice by set, gait and direction: the nearest direction a pack has (six or eight), a missing gait from the
//   nearest the set has, the Cycle blend space's samples; each family from the held set, else the base set's.
// - The stance weight: 0 while the held set plays its own locomotion, the other graphs' weight otherwise.
// - A set change: the new clips go into the slot not playing and it becomes the playing one.
// - The transition rules.
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
	Set->GetCycleSamples(Clips, Positions);
	TestEqual(TEXT("Two gaits of 7 samples, 7 idles"), Clips.Num(), 21);
	TestEqual(TEXT("A position per clip"), Positions.Num(), Clips.Num());
	TestTrue(TEXT("The walk forward at its speed"), Positions.Contains(FVector2D(0.0, 92.0)) && Clips[Positions.IndexOfByKey(FVector2D(0.0, 92.0))] == Set->Walk.Forward);
	TestTrue(TEXT("The jog backward at 180"), Clips[Positions.IndexOfByKey(FVector2D(180.0, 302.0))] == Set->Jog.Backward);
	TestTrue(TEXT("and at -180"), Clips[Positions.IndexOfByKey(FVector2D(-180.0, 302.0))] == Set->Jog.Backward);
	TestTrue(TEXT("The idle at -180, speed 0"), Clips[Positions.IndexOfByKey(FVector2D(-180.0, 0.0))] == Set->Idle);
	TestFalse(TEXT("No run sample"), Clips.Contains(Set->Run.Forward.Get()));
	TestEqual(TEXT("The top speed is the jog's"), Set->GetCycleTopSpeed(), 302.f);

	// Each family from the held set, else the base set's.
	UVCLocomotionClipSet* Base = NewSet(TEXT("Unarmed"));
	FVCLocomotionClips Resolved = UVCLocomotionAnimInstance::ResolveClips(Set, Base);
	TestTrue(TEXT("A set with its own locomotion"), Resolved.bOwnLocomotion);
	TestTrue(TEXT("plays its idle and cycle"), Resolved.Idle == Set->Idle && Resolved.Cycle == Set->Cycle);
	TestTrue(TEXT("and its jump"), Resolved.JumpStart == Set->JumpStart && Resolved.FallLoop == Set->FallLoop);
	TestTrue(TEXT("No moving landing: its standing one"), Resolved.LandMoving == Set->Land);
	TestEqual(TEXT("The cycle's top speed"), Resolved.CycleTopSpeed, 302.f);

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
	Moving.Velocity = FVector(300.0, 0.0, 0.0);
	Update(Anim, Moving);
	TestTrue(TEXT("The bow in B, playing"), Anim->bSlotB && Anim->IdleB == Bow->Idle && Anim->CycleB == Bow->Cycle);
	TestTrue(TEXT("The bow's jump"), Anim->JumpStartClip == Bow->JumpStart && Anim->FallLoopClip == Bow->FallLoop);
	TestEqual(TEXT("At the jog's speed the cycle plays as authored"), Anim->CyclePlayRate, 1.f);
	Moving.Velocity = FVector(604.0, 0.0, 0.0);
	Update(Anim, Moving);
	TestTrue(TEXT("At twice the jog's speed it plays twice as fast"), FMath::IsNearlyEqual(Anim->CyclePlayRate, 2.f, 1.0e-3f));
	return true;
}

// ---------------------------------------------------------------------------
// The transition rules, and the landing chosen at the touch-down.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCLocomotion_Rules, "VoxelCharacter.Locomotion.Rules", VC_LOCO_FLAGS)
bool FVCLocomotion_Rules::RunTest(const FString& Parameters)
{
	using namespace VCLocomotionTestHelpers;

	FVCLocomotionRuleInput In;
	In.bOnGround = true;
	FVCLocomotionRules Rules = UVCLocomotionAnimInstance::ComputeRules(In, 10.f, 0.15f);
	TestTrue(TEXT("Standing: to idle"), Rules.bCycleToIdle && !Rules.bIdleToCycle);
	TestTrue(TEXT("and land"), Rules.bAirToLand && !Rules.bToJump && !Rules.bToFall && !Rules.bToSwim && Rules.bSwimToIdle);
	In.bAccelerating = true;
	Rules = UVCLocomotionAnimInstance::ComputeRules(In, 10.f, 0.15f);
	TestTrue(TEXT("Input from a stand: to the cycle"), Rules.bIdleToCycle && !Rules.bCycleToIdle);
	In.bAccelerating = false;
	In.Speed = 50.f;
	Rules = UVCLocomotionAnimInstance::ComputeRules(In, 10.f, 0.15f);
	TestTrue(TEXT("Coasting: still the cycle"), Rules.bIdleToCycle);
	TestFalse(TEXT("Just landed on the move: the landing holds"), Rules.bLandToCycle);
	In.SecondsSinceLanded = 0.2f;
	TestTrue(TEXT("then the cycle"), UVCLocomotionAnimInstance::ComputeRules(In, 10.f, 0.15f).bLandToCycle);

	In = FVCLocomotionRuleInput();
	In.bFalling = true;
	In.UpSpeed = 400.f;
	Rules = UVCLocomotionAnimInstance::ComputeRules(In, 10.f, 0.15f);
	TestTrue(TEXT("Rising: a jump"), Rules.bToJump && !Rules.bToFall && !Rules.bAirToLand && !Rules.bIdleToCycle && !Rules.bCycleToIdle);
	In.UpSpeed = -10.f;
	Rules = UVCLocomotionAnimInstance::ComputeRules(In, 10.f, 0.15f);
	TestTrue(TEXT("Off a ledge: a fall"), Rules.bToFall && !Rules.bToJump);

	In = FVCLocomotionRuleInput();
	In.bSwimming = true;
	In.Speed = 200.f;
	Rules = UVCLocomotionAnimInstance::ComputeRules(In, 10.f, 0.15f);
	TestTrue(TEXT("Swimming: to swim, never the cycle"), Rules.bToSwim && !Rules.bSwimToIdle && !Rules.bIdleToCycle && !Rules.bAirToLand);

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
	TestTrue(TEXT("Falling: the fall rule"), Anim->bRuleToFall && !Anim->bRuleAirToLand);
	FVCAnimInput Touch = Standing(Bow, Base);
	Touch.Velocity = FVector(300.0, 0.0, 0.0);
	Update(Anim, Touch);
	TestTrue(TEXT("Landing on the move plays the moving landing"), Anim->LandClip == Bow->LandMoving);
	TestTrue(TEXT("and lands"), Anim->bRuleAirToLand && !Anim->bRuleLandToCycle);
	Update(Anim, Air);
	Update(Anim, Standing(Bow, Base));
	TestTrue(TEXT("Landing standing plays the standing one"), Anim->LandClip == Bow->Land);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
