// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "VCGravityTrajectoryPredictor.h"
#include "VCMotionMatchingAnimInstance.h"

// Character animation P5: the gravity-frame trajectory prediction against the engine's arithmetic, and the frame math the
// motion-matching AnimBP's world-Z nodes are swapped for. Pure, so they run headless.

#define VC_MM_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace VCMotionMatchingTests
{
	constexpr int32 NumHistory = 4;
	constexpr int32 NumPrediction = 15;
	constexpr float SecondsPerSample = 0.1f;
	constexpr float CurrentTime = 1.f / 60.f;

	/** The mannequins' mesh offset: -90 yaw. */
	const FQuat MeshRelative = FQuat::MakeFromEuler(FVector(0.0, 0.0, -90.0));

	/** A point in a planet's southern half and a frame there whose up is the planet's (its yaw arbitrary). */
	const FVector PlanetUp = FVector(0.6, -0.48, -0.64).GetSafeNormal();
	FQuat PlanetFrame()
	{
		return FRotationMatrix::MakeFromZX(PlanetUp, FVector(0.3, 0.9, 0.2)).ToQuat();
	}

	/**
	 * A walker in a frame whose up is Frame's Z: moving along the frame's X at 300 uu/s, accelerating toward its Y,
	 * facing its X, the camera turning at 30 degrees a second.
	 */
	FVCTrajectoryPredictionState MakeState(const FQuat& Frame, bool bOrient)
	{
		FVCTrajectoryPredictionState State;
		State.Up = Frame.GetUpVector();
		State.Position = Frame.RotateVector(FVector(0.0, 0.0, 100.0));
		State.MeshRelativeRotation = MeshRelative;
		State.Facing = Frame * MeshRelative;
		State.Velocity = Frame.RotateVector(FVector(300.0, 0.0, 0.0));
		State.Acceleration = Frame.RotateVector(FVector(0.0, 1000.0, 0.0));
		State.GravityAccel = -State.Up * 980.0;
		State.ControllerYawRateDegrees = 30.f;
		State.bOrientRotationToMovement = bOrient;
		State.bGrounded = true;
		State.Derived.ControllerYawRate = 30.f;
		State.Derived.MaxSpeed = 500.f;
		State.Derived.BrakingDeceleration = 2048.f;
		State.Derived.BrakingSubStepTime = 1.f / 30.f;
		State.Derived.Friction = 8.f;
		return State;
	}

	FTransformTrajectory Predict(const FVCTrajectoryPredictionState& State, bool bGravityFrame)
	{
		FTransformTrajectory Trajectory;
		Trajectory.Samples.SetNum(NumHistory + 1 + NumPrediction);
		FTransformTrajectorySample& Current = Trajectory.Samples[NumHistory];
		Current.Position = State.Position;
		Current.Facing = State.Facing;
		Current.TimeInSeconds = CurrentTime;
		UVCGravityTrajectoryPredictor::PredictSamples(State, FPoseSearchTrajectoryData(), bGravityFrame, Trajectory,
			NumPrediction, SecondsPerSample, NumHistory);
		return Trajectory;
	}

	/** A sample's up in actor space (its facing without the mesh offset). */
	FVector SampleUp(const FTransformTrajectorySample& Sample)
	{
		return (Sample.Facing * MeshRelative.Inverse()).GetUpVector();
	}

	bool RotatorsMatch(const FRotator& A, const FRotator& B, double ToleranceDegrees)
	{
		return A.Quaternion().AngularDistance(B.Quaternion()) < FMath::DegreesToRadians(ToleranceDegrees);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_TrajectoryMatchesEngineOnPlane, "VoxelCharacter.Anim.Trajectory.MatchesEngineOnPlane", VC_MM_TEST_FLAGS)
bool FVCAnim_TrajectoryMatchesEngineOnPlane::RunTest(const FString& Parameters)
{
	using namespace VCMotionMatchingTests;

	// Up = world Z: the gravity-frame prediction is the engine's, facing the movement or the camera
	for (const bool bOrient : { true, false })
	{
		const FVCTrajectoryPredictionState State = MakeState(FQuat::Identity, bOrient);
		const FTransformTrajectory Gravity = Predict(State, true);
		const FTransformTrajectory Engine = Predict(State, false);
		for (int32 Index = NumHistory + 1; Index < Gravity.Samples.Num(); ++Index)
		{
			const FTransformTrajectorySample& G = Gravity.Samples[Index];
			const FTransformTrajectorySample& E = Engine.Samples[Index];
			TestTrue(FString::Printf(TEXT("orient %d sample %d position (%s vs %s)"), bOrient, Index, *G.Position.ToString(), *E.Position.ToString()),
				G.Position.Equals(E.Position, 0.01));
			TestTrue(FString::Printf(TEXT("orient %d sample %d facing (%.5f rad apart)"), bOrient, Index, G.Facing.AngularDistance(E.Facing)),
				G.Facing.AngularDistance(E.Facing) < 1e-3);
			TestEqual(FString::Printf(TEXT("sample %d time"), Index), G.TimeInSeconds, E.TimeInSeconds, 1e-5f);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_TrajectoryPlanetFrame, "VoxelCharacter.Anim.Trajectory.PlanetFrame", VC_MM_TEST_FLAGS)
bool FVCAnim_TrajectoryPlanetFrame::RunTest(const FString& Parameters)
{
	using namespace VCMotionMatchingTests;

	const FQuat Frame = PlanetFrame();
	TestTrue(TEXT("the frame's up is the planet's"), Frame.GetUpVector().Equals(PlanetUp, 1e-6));

	for (const bool bOrient : { true, false })
	{
		const FVCTrajectoryPredictionState Plane = MakeState(FQuat::Identity, bOrient);
		const FVCTrajectoryPredictionState Planet = MakeState(Frame, bOrient);
		const FTransformTrajectory OnPlane = Predict(Plane, true);
		const FTransformTrajectory OnPlanet = Predict(Planet, true);
		const FTransformTrajectory EngineOnPlanet = Predict(Planet, false);

		double WorstEngineUp = 1.0;
		for (int32 Index = NumHistory + 1; Index < OnPlanet.Samples.Num(); ++Index)
		{
			const FTransformTrajectorySample& S = OnPlanet.Samples[Index];
			// On the ground plane through the start, upright in the frame
			TestTrue(FString::Printf(TEXT("orient %d sample %d on the tangent plane (%.4f off)"), bOrient, Index, (S.Position - Planet.Position) | PlanetUp),
				FMath::Abs((S.Position - Planet.Position) | PlanetUp) < 0.01);
			TestTrue(FString::Printf(TEXT("orient %d sample %d upright (up . up = %.6f)"), bOrient, Index, SampleUp(S) | PlanetUp),
				(SampleUp(S) | PlanetUp) > 0.9999);

			// The plane's prediction carried into the frame: it turns about local up, whichever way up is
			const FTransformTrajectorySample& P = OnPlane.Samples[Index];
			const FVector Expected = Planet.Position + Frame.RotateVector(P.Position - Plane.Position);
			TestTrue(FString::Printf(TEXT("orient %d sample %d is the plane's, turned (%s vs %s)"), bOrient, Index, *S.Position.ToString(), *Expected.ToString()),
				S.Position.Equals(Expected, 0.05));
			TestTrue(FString::Printf(TEXT("orient %d sample %d facing is the plane's, turned (%.5f rad)"), bOrient, Index, S.Facing.AngularDistance(Frame * P.Facing)),
				S.Facing.AngularDistance(Frame * P.Facing) < 1e-3);

			WorstEngineUp = FMath::Min(WorstEngineUp, SampleUp(EngineOnPlanet.Samples[Index]) | PlanetUp);
		}
		// The engine's arithmetic turns about world Z and orients against world up: off the frame's up here
		AddInfo(FString::Printf(TEXT("orient %d: the engine's arithmetic, worst sample up . local up = %.4f"), bOrient, WorstEngineUp));
		TestTrue(FString::Printf(TEXT("orient %d: the engine's arithmetic leaves the frame (worst %.4f)"), bOrient, WorstEngineUp), WorstEngineUp < 0.99);
	}

	// The yaw sense: X toward Y about Z is +90, as FQuat(Z, +90) maps X onto Y; components along the axis do not count
	TestEqual(TEXT("X to Y about Z"), UVCGravityTrajectoryPredictor::SignedAngleAboutAxisDegrees(FVector::XAxisVector, FVector::YAxisVector, FVector::ZAxisVector), 90.f, 1e-3f);
	TestEqual(TEXT("X to Y about -Z"), UVCGravityTrajectoryPredictor::SignedAngleAboutAxisDegrees(FVector::XAxisVector, FVector::YAxisVector, -FVector::ZAxisVector), -90.f, 1e-3f);
	TestEqual(TEXT("pitched vectors"), UVCGravityTrajectoryPredictor::SignedAngleAboutAxisDegrees(FVector(1, 0, 5), FVector(0, 1, -3), FVector::ZAxisVector), 90.f, 1e-3f);
	TestEqual(TEXT("degenerate"), UVCGravityTrajectoryPredictor::SignedAngleAboutAxisDegrees(FVector::ZAxisVector, FVector::XAxisVector, FVector::ZAxisVector), 0.f, 1e-6f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_MotionMatchingFrame, "VoxelCharacter.Anim.MotionMatchingFrame", VC_MM_TEST_FLAGS)
bool FVCAnim_MotionMatchingFrame::RunTest(const FString& Parameters)
{
	using namespace VCMotionMatchingTests;
	using Instance = UVCMotionMatchingAnimInstance;
	const FVector Z = FVector::UpVector;

	// On world Z each is the engine's arithmetic, bit for bit
	const FVector Velocity(300.0, -200.0, 50.0);
	const FRotator Facing(10.0, 35.0, -5.0);
	const FRotator Aim(-20.0, 170.0, 0.0);
	TestEqual(TEXT("plane: VSizeXY"), Instance::GroundSpeed(Velocity, Z), Velocity.Size2D());
	FRotator EngineDelta = Aim - Facing;
	EngineDelta.Normalize();
	TestTrue(TEXT("plane: NormalizedDeltaRotator"), Instance::DeltaRotatorInFrame(Aim, Facing, Z) == EngineDelta);
	TestTrue(TEXT("plane: the yaw sum"), Instance::AddYawInFrame(Facing, 90.f, Z) == FRotator(Facing.Pitch, Facing.Yaw + 90.0, Facing.Roll));
	TestTrue(TEXT("plane: ToFrame"), Instance::ToFrame(Velocity, Z) == Velocity);
	TestEqual(TEXT("plane: CalculateDirection, right"), Instance::DirectionInFrame(FVector(0, 100, 0), FRotator::ZeroRotator, Z), 90.f, 1e-3f);
	TestEqual(TEXT("plane: CalculateDirection, left"), Instance::DirectionInFrame(FVector(0, -100, 0), FRotator::ZeroRotator, Z), -90.f, 1e-3f);
	TestEqual(TEXT("plane: CalculateDirection, behind"), FMath::Abs(Instance::DirectionInFrame(FVector(-100, 0, 0), FRotator::ZeroRotator, Z)), 180.f, 1e-2f);
	TestEqual(TEXT("plane: CalculateDirection, none"), Instance::DirectionInFrame(FVector::ZeroVector, Facing, Z), 0.f);

	// In a tilted frame each gives what the plane gives for the same motion turned into it, and is right about local up
	// where the world-Z arithmetic is not
	for (const FVector& Up : { PlanetUp, FVector(1.0, 0.0, 0.0), FVector(0.0, 0.0, -1.0) })
	{
		const FQuat Frame = FRotationMatrix::MakeFromZX(Up, FVector(0.3, 0.9, 0.2)).ToQuat();
		const FString Where = Up.ToString();
		const FVector LocalVelocity(300.0, -200.0, 50.0);
		const FVector WorldVelocity = Frame.RotateVector(LocalVelocity);
		TestEqual(*(Where + TEXT(": the speed across up")), Instance::GroundSpeed(WorldVelocity, Up), LocalVelocity.Size2D(), 1e-6);
		TestEqual(*(Where + TEXT(": the velocity's Z in the frame")), Instance::ToFrame(WorldVelocity, Up) | FVector::UpVector, 50.0, 1e-6);

		const FRotator WorldFacing = (Frame * Facing.Quaternion()).Rotator();
		TestEqual(*(Where + TEXT(": direction")), Instance::DirectionInFrame(WorldVelocity, WorldFacing, Up),
			Instance::DirectionInFrame(LocalVelocity, Facing, Z), 1e-2f);

		// Deltas and yaw sums do not depend on the frame's yaw: compare against the plane's in this frame
		const FRotator WorldAim = (Frame * Aim.Quaternion()).Rotator();
		const FRotator Delta = Instance::DeltaRotatorInFrame(WorldAim, WorldFacing, Up);
		TestTrue(*FString::Printf(TEXT("%s: the delta's yaw (%.3f vs %.3f)"), *Where, Delta.Yaw, EngineDelta.Yaw), FMath::IsNearlyEqual(Delta.Yaw, EngineDelta.Yaw, 1e-2));
		TestTrue(*FString::Printf(TEXT("%s: the delta's pitch (%.3f vs %.3f)"), *Where, Delta.Pitch, EngineDelta.Pitch), FMath::IsNearlyEqual(Delta.Pitch, EngineDelta.Pitch, 1e-2));

		const FRotator Turned = Instance::AddYawInFrame(WorldFacing, 90.f, Up);
		const FRotator Expected = (Frame * FRotator(Facing.Pitch, Facing.Yaw + 90.0, Facing.Roll).Quaternion()).Rotator();
		TestTrue(*(Where + TEXT(": the yaw sum turns about up")), RotatorsMatch(Turned, Expected, 1e-3));
		TestTrue(*(Where + TEXT(": the yaw sum keeps the up")),
			FMath::IsNearlyEqual(Turned.Quaternion().GetUpVector() | Up, WorldFacing.Quaternion().GetUpVector() | Up, 1e-6));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
