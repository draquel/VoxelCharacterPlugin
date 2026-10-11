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
// Surface behaviours (feature 10): wandering replaces idling when a radius is set; prey flees instead of
// fighting and settles once the threat is gone; hostiles without a route roam.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCNPCAI_WanderFlee, "VoxelCharacter.AI.WanderFlee", VC_NPCAI_FLAGS)
bool FVCNPCAI_WanderFlee::RunTest(const FString& Parameters)
{
	using namespace VCNPCAITestHelpers;

	// Hostile with a wander radius and no route: roams; with a route: patrols; a target: chases.
	{
		FVCNPCAIDecisionInput In;
		In.bCanWander = true;
		TestEqual(TEXT("Hostile, no route, can wander -> Wander"), Decide(In, EVCNPCAIState::Idle), EVCNPCAIState::Wander);
		In.bHasRoute = true;
		TestEqual(TEXT("Route wins over wander -> Patrol"), Decide(In, EVCNPCAIState::Wander), EVCNPCAIState::Patrol);
		In.bHasRoute = false;
		In.bHasTarget = true;
		In.bTargetVisible = true;
		In.DistanceToTarget = 700.f;
		TestEqual(TEXT("Hostile sees a target -> Chase"), Decide(In, EVCNPCAIState::Wander), EVCNPCAIState::Chase);
		In.bHasTarget = false;
		In.bTargetVisible = false;
		In.bAtRoute = false;
		In.DistanceFromRoute = 900.f;
		TestEqual(TEXT("Lost target away from home -> Return"), Decide(In, EVCNPCAIState::Chase), EVCNPCAIState::Return);
	}

	// Prey: a visible threat inside the flee radius -> Flee; outside it -> keep wandering; once fleeing it keeps
	// fleeing while the threat is engaged, then wanders (never Return, never Attack).
	{
		FVCNPCAIDecisionInput In;
		In.bPrey = true;
		In.bCanWander = true;
		TestEqual(TEXT("Prey alone -> Wander"), Decide(In, EVCNPCAIState::Idle), EVCNPCAIState::Wander);
		In.bHasTarget = true;
		In.bTargetVisible = true;
		In.DistanceToTarget = 1100.f;
		In.bThreatInFleeRange = false;
		TestEqual(TEXT("Threat far -> still Wander"), Decide(In, EVCNPCAIState::Wander), EVCNPCAIState::Wander);
		In.DistanceToTarget = 500.f;
		In.bThreatInFleeRange = true;
		TestEqual(TEXT("Threat close -> Flee"), Decide(In, EVCNPCAIState::Wander), EVCNPCAIState::Flee);
		In.DistanceToTarget = 150.f;
		TestEqual(TEXT("Prey never attacks"), Decide(In, EVCNPCAIState::Flee), EVCNPCAIState::Flee);
		In.bThreatInFleeRange = false;
		In.DistanceToTarget = 1500.f;
		In.bTargetVisible = false;
		In.SecondsSinceSeen = 1.f;
		TestEqual(TEXT("Recently seen -> keep fleeing"), Decide(In, EVCNPCAIState::Flee), EVCNPCAIState::Flee);
		In.SecondsSinceSeen = 10.f;
		In.bAtRoute = false;
		In.DistanceFromRoute = 2600.f;
		TestEqual(TEXT("Threat gone -> Wander (no Return for prey)"), Decide(In, EVCNPCAIState::Flee), EVCNPCAIState::Wander);
		In.bAIEnabled = false;
		TestEqual(TEXT("Disabled prey -> Idle"), Decide(In, EVCNPCAIState::Flee), EVCNPCAIState::Idle);
	}
	return true;
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

// ---------------------------------------------------------------------------
// A lit light bearer is noticed from farther (feature 7); unlit targets keep the base radius.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCNPCAI_LitTargetSight, "VoxelCharacter.AI.LitTargetSight", VC_NPCAI_FLAGS)
bool FVCNPCAI_LitTargetSight::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Unlit inside the base radius"), AVCNPCAIController::IsWithinSight(1000.f, 1200.f, false, 1.5f));
	TestFalse(TEXT("Unlit beyond the base radius"), AVCNPCAIController::IsWithinSight(1400.f, 1200.f, false, 1.5f));
	TestTrue(TEXT("Lit at 1.4x is noticed"), AVCNPCAIController::IsWithinSight(1680.f, 1200.f, true, 1.5f));
	TestFalse(TEXT("Lit beyond 1.5x is not"), AVCNPCAIController::IsWithinSight(1900.f, 1200.f, true, 1.5f));
	TestTrue(TEXT("Multiplier never shrinks the reach"), AVCNPCAIController::IsWithinSight(1100.f, 1200.f, true, 0.5f));
	return true;
}

#undef VC_NPCAI_FLAGS

#endif // WITH_AUTOMATION_TESTS
