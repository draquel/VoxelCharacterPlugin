// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Movement/VCMovementComponent.h"

// ---------------------------------------------------------------------------
// The wind's drift on the pawn (the weather plan's P4): a fraction of the wind, a current the pawn rides whatever it does
// itself; nothing sheltered or in a calm. CalcVelocity's bookkeeping (the previous drift off before the braking, the
// new one on after it) is what lets a standing pawn drift: checked here as the arithmetic it is.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCWindPushTest, "VoxelCharacter.Movement.WindPush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVCWindPushTest::RunTest(const FString& Parameters)
{
	const FVector East(1.0, 0.0, 0.0);
	const FVector Wind = East * 900.0; // a storm
	const float Factor = 0.1f;        // a drift of 90 uu/s

	const FVector Drift = UVCMovementComponent::ComputeWindDrift(Wind, Factor, 1.0f);
	TestTrue(TEXT("A tenth of the wind, along it"), Drift.Equals(East * 90.0, 1.0e-3));
	TestTrue(TEXT("Half exposure: half the drift"), UVCMovementComponent::ComputeWindDrift(Wind, Factor, 0.5f).Equals(East * 45.0, 1.0e-3));
	TestTrue(TEXT("Sheltered: nothing"), UVCMovementComponent::ComputeWindDrift(Wind, Factor, 0.0f).IsZero());
	TestTrue(TEXT("Calm: nothing"), UVCMovementComponent::ComputeWindDrift(FVector::ZeroVector, Factor, 1.0f).IsZero());
	TestTrue(TEXT("No factor: nothing"), UVCMovementComponent::ComputeWindDrift(Wind, 0.0f, 1.0f).IsZero());
	TestTrue(TEXT("Exposure over 1 clamps"), UVCMovementComponent::ComputeWindDrift(Wind, Factor, 3.0f).Equals(East * 90.0, 1.0e-3));

	// A vertical component in the wind (a planet's tangent wind seen in world axes) drifts along it, the vertical included.
	const FVector Tangent = FVector(0.6, 0.0, 0.8) * 500.0;
	TestTrue(TEXT("Along the wind's own direction"), UVCMovementComponent::ComputeWindDrift(Tangent, Factor, 1.0f).Equals(FVector(0.6, 0.0, 0.8) * 50.0, 1.0e-3));

	// CalcVelocity's bookkeeping, as arithmetic: a standing pawn (its own velocity braked to zero each tick) moves at the
	// drift, not faster; a pawn walking against the wind at 300 nets 210; across it, the cross speed stays.
	FVector Velocity = FVector::ZeroVector;
	FVector Last = FVector::ZeroVector;
	for (int32 Tick = 0; Tick < 3; ++Tick)
	{
		Velocity -= Last;              // the previous drift off
		Velocity = FVector::ZeroVector; // the braking of a standing pawn
		Last = Drift;
		Velocity += Last;
		TestTrue(TEXT("Standing: the drift, never accumulated"), Velocity.Equals(East * 90.0, 1.0e-3));
	}
	Velocity -= Last;
	Velocity = East * -300.0;          // the pawn's own walk, against the wind
	Velocity += Drift;
	TestTrue(TEXT("Against the wind: slowed by the drift"), Velocity.Equals(East * -210.0, 1.0e-3));
	Velocity = FVector(0.0, 400.0, 0.0) + Drift;
	TestTrue(TEXT("Across: the cross speed kept, the drift added"), Velocity.Equals(FVector(90.0, 400.0, 0.0), 1.0e-3));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
