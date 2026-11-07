// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Core/VCCharacterAttributeSet.h"
#include "Core/VCCharacterBase.h"
#include "Core/VCWeatherEffects.h"
#include "Movement/VCMovementComponent.h"
#include "UI/VCVitalsWidget.h"

// ---------------------------------------------------------------------------
// The wind's drift on the pawn (the weather plan's P4): a fraction of the wind on the pawn's own motion (slower against
// it, faster with it, in proportion to its speed) and the full fraction on a fall; a standing pawn idles in any wind;
// nothing sheltered or in a calm. CalcVelocity's bookkeeping (the previous drift off before the braking, the new one on
// after it) is checked here as the arithmetic it is.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCWindPushTest, "VoxelCharacter.Movement.WindPush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVCWindPushTest::RunTest(const FString& Parameters)
{
	const FVector East(1.0, 0.0, 0.0);
	const FVector Wind = East * 900.0; // a storm
	const float Factor = 0.1f;        // a drift of 90 uu/s at full motion
	const float Max = 600.f;          // the pawn's max speed

	// Standing: nothing. Walking at the max speed: the full drift; at half: half. In the air: full whatever the speed.
	TestTrue(TEXT("Standing: no drift"), UVCMovementComponent::ComputeWindDrift(Wind, Factor, 1.0f, 0.f, Max, false).IsZero());
	const FVector Drift = UVCMovementComponent::ComputeWindDrift(Wind, Factor, 1.0f, Max, Max, false);
	TestTrue(TEXT("At the max speed: a tenth of the wind, along it"), Drift.Equals(East * 90.0, 1.0e-3));
	TestTrue(TEXT("At half speed: half"), UVCMovementComponent::ComputeWindDrift(Wind, Factor, 1.0f, 300.f, Max, false).Equals(East * 45.0, 1.0e-3));
	TestTrue(TEXT("Over the max speed: capped at the full drift"), UVCMovementComponent::ComputeWindDrift(Wind, Factor, 1.0f, 2000.f, Max, false).Equals(East * 90.0, 1.0e-3));
	TestTrue(TEXT("Falling from rest: the full drift"), UVCMovementComponent::ComputeWindDrift(Wind, Factor, 1.0f, 0.f, Max, true).Equals(East * 90.0, 1.0e-3));
	TestTrue(TEXT("Half exposure: half the drift"), UVCMovementComponent::ComputeWindDrift(Wind, Factor, 0.5f, Max, Max, false).Equals(East * 45.0, 1.0e-3));
	TestTrue(TEXT("Sheltered: nothing"), UVCMovementComponent::ComputeWindDrift(Wind, Factor, 0.0f, Max, Max, false).IsZero());
	TestTrue(TEXT("Calm: nothing"), UVCMovementComponent::ComputeWindDrift(FVector::ZeroVector, Factor, 1.0f, Max, Max, false).IsZero());
	TestTrue(TEXT("No factor: nothing"), UVCMovementComponent::ComputeWindDrift(Wind, 0.0f, 1.0f, Max, Max, false).IsZero());
	TestTrue(TEXT("Exposure over 1 clamps"), UVCMovementComponent::ComputeWindDrift(Wind, Factor, 3.0f, Max, Max, false).Equals(East * 90.0, 1.0e-3));
	TestTrue(TEXT("A zero max speed does not divide"), UVCMovementComponent::ComputeWindDrift(Wind, Factor, 1.0f, 100.f, 0.f, false).Equals(East * 90.0, 1.0e-3));

	// A vertical component in the wind (a planet's tangent wind seen in world axes) drifts along it, the vertical included.
	const FVector Tangent = FVector(0.6, 0.0, 0.8) * 500.0;
	TestTrue(TEXT("Along the wind's own direction"), UVCMovementComponent::ComputeWindDrift(Tangent, Factor, 1.0f, Max, Max, false).Equals(FVector(0.6, 0.0, 0.8) * 50.0, 1.0e-3));

	// CalcVelocity's bookkeeping, as arithmetic: a standing pawn (its own velocity braked to zero each tick) does not
	// move; a pawn walking against the wind at the max speed nets 510; across it, the cross speed stays.
	FVector Velocity = FVector::ZeroVector;
	FVector Last = FVector::ZeroVector;
	for (int32 Tick = 0; Tick < 3; ++Tick)
	{
		Velocity -= Last;              // the previous drift off
		Velocity = FVector::ZeroVector; // the braking of a standing pawn
		Last = UVCMovementComponent::ComputeWindDrift(Wind, Factor, 1.0f, static_cast<float>(Velocity.Size()), Max, false);
		Velocity += Last;
		TestTrue(TEXT("Standing: still standing"), Velocity.IsZero());
	}
	Velocity = East * -600.0;          // the pawn's own walk, against the wind, at the max speed
	Velocity += UVCMovementComponent::ComputeWindDrift(Wind, Factor, 1.0f, static_cast<float>(Velocity.Size()), Max, false);
	TestTrue(TEXT("Against the wind: slowed by the drift"), Velocity.Equals(East * -510.0, 1.0e-3));
	Velocity = FVector(0.0, 600.0, 0.0);
	Velocity += UVCMovementComponent::ComputeWindDrift(Wind, Factor, 1.0f, static_cast<float>(Velocity.Size()), Max, false);
	TestTrue(TEXT("Across: the cross speed kept, the drift added"), Velocity.Equals(FVector(90.0, 600.0, 0.0), 1.0e-3));
	return true;
}

// ---------------------------------------------------------------------------
// The wet level (the weather plan's P5): soaked by the rain over WetSoakSeconds, at once by a swim, dry again over
// WetDrySeconds; and the character's hysteresis for the wet and cold states.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCWetLevelTest, "VoxelCharacter.Movement.WetLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVCWetLevelTest::RunTest(const FString& Parameters)
{
	const float Soak = 12.f, Dry = 90.f;
	// Full rain, exposed: soaked in SoakSeconds, never over 1.
	float Level = 0.f;
	for (int32 i = 0; i < 120; ++i)
	{
		Level = UVCMovementComponent::AdvanceWetLevel(Level, 1.f, false, 0.1f, Soak, Dry);
	}
	TestEqual(TEXT("Twelve seconds of full rain soak"), Level, 1.f, 1.0e-3f);
	TestEqual(TEXT("Never over 1"), UVCMovementComponent::AdvanceWetLevel(1.f, 1.f, false, 5.f, Soak, Dry), 1.f, 1.0e-6f);
	// Half the rain (a drizzle, or half sheltered): half the pace.
	TestEqual(TEXT("Half the rain, half the pace"), UVCMovementComponent::AdvanceWetLevel(0.f, 0.5f, false, 6.f, Soak, Dry), 0.25f, 1.0e-4f);
	// Nothing falling: dry in DrySeconds; never under 0.
	TestEqual(TEXT("Half dry after 45 s"), UVCMovementComponent::AdvanceWetLevel(1.f, 0.f, false, 45.f, Soak, Dry), 0.5f, 1.0e-4f);
	TestEqual(TEXT("Never under 0"), UVCMovementComponent::AdvanceWetLevel(0.1f, 0.f, false, 60.f, Soak, Dry), 0.f, 1.0e-6f);
	// A swim soaks at once; a zero or negative step changes nothing.
	TestEqual(TEXT("Swimming: soaked"), UVCMovementComponent::AdvanceWetLevel(0.f, 0.f, true, 0.1f, Soak, Dry), 1.f, 1.0e-6f);
	TestEqual(TEXT("No step: no change"), UVCMovementComponent::AdvanceWetLevel(0.4f, 1.f, false, 0.f, Soak, Dry), 0.4f, 1.0e-6f);
	TestEqual(TEXT("A negative step: no change"), UVCMovementComponent::AdvanceWetLevel(0.4f, 0.f, false, -1.f, Soak, Dry), 0.4f, 1.0e-6f);

	// The hysteresis: wet goes on at 0.6 and off under 0.2; cold on at 0 C and off over 2 C.
	TestFalse(TEXT("Dry, 0.5: still off"), AVCCharacterBase::HysteresisOn(false, 0.5f, 0.6f, 0.2f, true));
	TestTrue(TEXT("Dry, 0.6: on"), AVCCharacterBase::HysteresisOn(false, 0.6f, 0.6f, 0.2f, true));
	TestTrue(TEXT("Wet, 0.3: still on"), AVCCharacterBase::HysteresisOn(true, 0.3f, 0.6f, 0.2f, true));
	TestFalse(TEXT("Wet, 0.2: off"), AVCCharacterBase::HysteresisOn(true, 0.2f, 0.6f, 0.2f, true));
	TestFalse(TEXT("Warm, 1 C: still off"), AVCCharacterBase::HysteresisOn(false, 1.f, 0.f, 2.f, false));
	TestTrue(TEXT("Warm, 0 C: on"), AVCCharacterBase::HysteresisOn(false, 0.f, 0.f, 2.f, false));
	TestTrue(TEXT("Cold, 1.5 C: still on"), AVCCharacterBase::HysteresisOn(true, 1.5f, 0.f, 2.f, false));
	TestFalse(TEXT("Cold, 2 C: off"), AVCCharacterBase::HysteresisOn(true, 2.f, 0.f, 2.f, false));

	// The cold's effect: one additive modifier on the move speed multiplier, the penalty's size.
	const UVCWeatherColdEffect* Cold = GetDefault<UVCWeatherColdEffect>();
	if (TestNotNull(TEXT("The cold effect's default object"), Cold) && TestEqual(TEXT("One modifier"), Cold->Modifiers.Num(), 1))
	{
		float Magnitude = 0.f;
		TestTrue(TEXT("On the move speed multiplier"), Cold->Modifiers[0].Attribute == UVCCharacterAttributeSet::GetMoveSpeedMultiplierAttribute());
		TestTrue(TEXT("A static magnitude"), Cold->Modifiers[0].ModifierMagnitude.GetStaticMagnitudeIfPossible(1.f, Magnitude));
		TestEqual(TEXT("The penalty"), Magnitude, -UVCWeatherColdEffect::SpeedPenalty, 1.0e-6f);
		TestTrue(TEXT("Infinite"), Cold->DurationPolicy == EGameplayEffectDurationType::Infinite);
	}
	TestTrue(TEXT("Both states' text"), UVCVitalsWidget::FormatWeather(true, true).ToString() == TEXT("Soaked and cold"));
	TestTrue(TEXT("Neither: empty"), UVCVitalsWidget::FormatWeather(false, false).IsEmpty());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
