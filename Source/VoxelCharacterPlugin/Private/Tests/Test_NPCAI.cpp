// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AI/VCNPCAIController.h"

#define VC_NPCAI_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace VCNPCAITestHelpers
{
	constexpr float Melee = 200.f;
	constexpr float LoseSight = 4.f;
	constexpr float Leash = 2500.f;

	FVCNPCAIDecisionInput Patrolling()
	{
		FVCNPCAIDecisionInput In;
		In.bHasRoute = true;
		return In;
	}

	EVCNPCAIState Decide(const FVCNPCAIDecisionInput& In, EVCNPCAIState Current)
	{
		return AVCNPCAIController::DecideState(In, Current, Melee, LoseSight, Leash);
	}
}

// ---------------------------------------------------------------------------
// The state decision is a pure function of what the NPC perceives.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCNPCAI_StateDecision, "VoxelCharacter.AI.StateDecision", VC_NPCAI_FLAGS)
bool FVCNPCAI_StateDecision::RunTest(const FString& Parameters)
{
	using namespace VCNPCAITestHelpers;

	// Nothing around: patrol the route; stand guard without one.
	TestEqual(TEXT("Route, no target -> Patrol"), Decide(Patrolling(), EVCNPCAIState::Idle), EVCNPCAIState::Patrol);
	{
		FVCNPCAIDecisionInput In;
		TestEqual(TEXT("No route, no target -> Idle"), Decide(In, EVCNPCAIState::Patrol), EVCNPCAIState::Idle);
	}

	// A visible hostile far away: chase; within melee range: attack.
	{
		FVCNPCAIDecisionInput In = Patrolling();
		In.bHasTarget = true;
		In.bTargetVisible = true;
		In.DistanceToTarget = 800.f;
		TestEqual(TEXT("Visible far target -> Chase"), Decide(In, EVCNPCAIState::Patrol), EVCNPCAIState::Chase);
		In.DistanceToTarget = 150.f;
		TestEqual(TEXT("Visible near target -> Attack"), Decide(In, EVCNPCAIState::Chase), EVCNPCAIState::Attack);
	}

	// Lost sight: keep chasing for LoseSightSeconds, then return to the route.
	{
		FVCNPCAIDecisionInput In = Patrolling();
		In.bHasTarget = true;
		In.bTargetVisible = false;
		In.DistanceToTarget = 900.f;
		In.SecondsSinceSeen = 2.f;
		In.bAtRoute = false;
		In.DistanceFromRoute = 600.f;
		TestEqual(TEXT("Recently seen -> still Chase"), Decide(In, EVCNPCAIState::Chase), EVCNPCAIState::Chase);
		In.SecondsSinceSeen = 6.f;
		TestEqual(TEXT("Lost for long -> Return"), Decide(In, EVCNPCAIState::Chase), EVCNPCAIState::Return);
		In.bHasTarget = false;
		In.bAtRoute = true;
		In.DistanceFromRoute = 0.f;
		TestEqual(TEXT("Back on the route -> Patrol"), Decide(In, EVCNPCAIState::Return), EVCNPCAIState::Patrol);
	}

	// Leash: a visible target too far from the route is abandoned.
	{
		FVCNPCAIDecisionInput In = Patrolling();
		In.bHasTarget = true;
		In.bTargetVisible = true;
		In.DistanceToTarget = 300.f;
		In.DistanceFromRoute = 3000.f;
		In.bAtRoute = false;
		TestEqual(TEXT("Beyond the leash -> Return"), Decide(In, EVCNPCAIState::Chase), EVCNPCAIState::Return);
	}

	// Disabled or dead: always Idle, whatever else is true.
	{
		FVCNPCAIDecisionInput In = Patrolling();
		In.bHasTarget = true;
		In.bTargetVisible = true;
		In.DistanceToTarget = 100.f;
		In.bAIEnabled = false;
		TestEqual(TEXT("AI disabled -> Idle"), Decide(In, EVCNPCAIState::Attack), EVCNPCAIState::Idle);
		In.bAIEnabled = true;
		In.bPawnDead = true;
		TestEqual(TEXT("Dead -> Idle"), Decide(In, EVCNPCAIState::Attack), EVCNPCAIState::Idle);
	}
	return true;
}

#undef VC_NPCAI_FLAGS

#endif // WITH_AUTOMATION_TESTS
