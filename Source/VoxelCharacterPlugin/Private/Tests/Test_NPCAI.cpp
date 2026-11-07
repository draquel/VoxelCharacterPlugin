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
// The weather's hold (the weather plan's P5): prey held for a storm stands where it is instead of roaming or
// patrolling, still flees a threat, and roams again when the hold lifts; hostiles ignore the flag.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCNPCAI_WeatherHold, "VoxelCharacter.AI.WeatherHold", VC_NPCAI_FLAGS)
bool FVCNPCAI_WeatherHold::RunTest(const FString& Parameters)
{
	using namespace VCNPCAITestHelpers;
	FVCNPCAIDecisionInput In;
	In.bPrey = true;
	In.bCanWander = true;
	TestEqual(TEXT("Prey, no hold -> Wander"), Decide(In, EVCNPCAIState::Idle), EVCNPCAIState::Wander);
	In.bHoldsForWeather = true;
	TestEqual(TEXT("Prey held for the weather -> Idle"), Decide(In, EVCNPCAIState::Wander), EVCNPCAIState::Idle);
	In.bHasRoute = true;
	TestEqual(TEXT("... a route does not move it either"), Decide(In, EVCNPCAIState::Idle), EVCNPCAIState::Idle);
	In.bHasTarget = true;
	In.bTargetVisible = true;
	In.bThreatInFleeRange = true;
	In.DistanceToTarget = 300.f;
	TestEqual(TEXT("... a threat still makes it flee"), Decide(In, EVCNPCAIState::Idle), EVCNPCAIState::Flee);
	In.bHasTarget = false;
	In.bTargetVisible = false;
	In.bThreatInFleeRange = false;
	In.bHasRoute = false;
	In.bHoldsForWeather = false;
	TestEqual(TEXT("The hold lifted -> Wander"), Decide(In, EVCNPCAIState::Idle), EVCNPCAIState::Wander);
	// A hostile with the flag set by mistake behaves as ever.
	FVCNPCAIDecisionInput Hostile;
	Hostile.bCanWander = true;
	Hostile.bHoldsForWeather = true;
	TestEqual(TEXT("Hostiles ignore the hold -> Wander"), Decide(Hostile, EVCNPCAIState::Idle), EVCNPCAIState::Wander);
	return true;
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
// The movement's tick rate (G73, G125): every frame near a player and off the ground, the far rates past FarDistance and
// twice it, at least the rest interval while resting, wherever the players are; 0 switches a rate off.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCNPCAI_MovementTickInterval, "VoxelCharacter.AI.MovementTickInterval", VC_NPCAI_FLAGS)
bool FVCNPCAI_MovementTickInterval::RunTest(const FString& Parameters)
{
	auto Interval = [](double Distance, bool bOnGround, bool bResting, float Rest = 0.5f)
	{
		return AVCNPCAIController::MovementTickInterval(Distance, bOnGround, bResting, 4000.f, 0.05f, Rest);
	};
	TestEqual(TEXT("Near a player: every frame"), Interval(1000.0, true, false), 0.f);
	TestEqual(TEXT("Past the far distance: the far interval"), Interval(5000.0, true, false), 0.05f);
	TestEqual(TEXT("Past twice it: twice the far interval"), Interval(9000.0, true, false), 0.1f);
	TestEqual(TEXT("Resting near a player: the rest interval"), Interval(1000.0, true, true), 0.5f);
	TestEqual(TEXT("Resting far away: the rest interval (the longer)"), Interval(9000.0, true, true), 0.5f);
	TestEqual(TEXT("A rest interval shorter than the far one: the far one"), Interval(9000.0, true, true, 0.02f), 0.1f);
	TestEqual(TEXT("Rest rate off: the far rate"), Interval(9000.0, true, true, 0.f), 0.1f);
	TestEqual(TEXT("Off the ground: every frame, far or resting"), Interval(9000.0, false, true), 0.f);
	TestEqual(TEXT("Far rate off: every frame unless resting"),
		AVCNPCAIController::MovementTickInterval(9000.0, true, false, 4000.f, 0.f, 0.5f), 0.f);
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

// ---------------------------------------------------------------------------
// Light fear (QA R16): a hostile that fears light never attacks a torch bearer. It backs out of the light,
// holds at the edge, closes in only from farther out, and attacks again once the light goes out.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCNPCAI_LightFear, "VoxelCharacter.AI.LightFear", VC_NPCAI_FLAGS)
bool FVCNPCAI_LightFear::RunTest(const FString& Parameters)
{
	using namespace VCNPCAITestHelpers;

	FVCNPCAIDecisionInput In;
	In.bHasTarget = true;
	In.bTargetVisible = true;
	In.bTargetFeared = true;
	In.FearRadius = 600.f;
	In.FearHoldBand = 350.f;

	In.DistanceToTarget = 150.f; // inside melee range
	TestEqual(TEXT("Feared target in melee range -> backs off, never attacks"), Decide(In, EVCNPCAIState::Chase), EVCNPCAIState::Flee);
	In.DistanceToTarget = 700.f;
	TestEqual(TEXT("Just past the light -> Hold"), Decide(In, EVCNPCAIState::Chase), EVCNPCAIState::Hold);
	In.DistanceToTarget = 1200.f;
	TestEqual(TEXT("Beyond the hold band -> closes in"), Decide(In, EVCNPCAIState::Hold), EVCNPCAIState::Chase);
	In.DistanceToTarget = 700.f;
	TestEqual(TEXT("Fleeing keeps going until past the band's middle"), Decide(In, EVCNPCAIState::Flee), EVCNPCAIState::Flee);
	In.DistanceToTarget = 800.f;
	TestEqual(TEXT("...then holds"), Decide(In, EVCNPCAIState::Flee), EVCNPCAIState::Hold);

	In.bTargetFeared = false;
	In.DistanceToTarget = 150.f;
	TestEqual(TEXT("Light out -> attacks"), Decide(In, EVCNPCAIState::Hold), EVCNPCAIState::Attack);
	return true;
}

#undef VC_NPCAI_FLAGS

#endif // WITH_AUTOMATION_TESTS
