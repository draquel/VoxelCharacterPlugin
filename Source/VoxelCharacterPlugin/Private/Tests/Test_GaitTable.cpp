// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Camera/VCThirdPersonCameraMode.h"
#include "Core/VCCharacterBase.h"
#include "Core/VCNPCCharacterBase.h"
#include "Movement/VCMovementComponent.h"
#include "Testing/VCTestWorld.h"
#include "AIController.h"

// The gait table (the character refinement plan's C4, D75): the row per gait and direction, the sprint's ramps, the
// presets, their application to the engine's values on a player and not on an NPC; and the eased face-aim flip.

#define VC_GAIT_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace VCGaitTests
{
	AVCCharacterBase* SpawnPlayer(UWorld* World, const FRotator& Facing)
	{
		const FTransform Stand(Facing, FVector(0, 0, 1000));
		AVCCharacterBase* Player = World->SpawnActorDeferred<AVCCharacterBase>(AVCCharacterBase::StaticClass(), Stand, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Player)
		{
			Player->bWaitForTerrain = false;
			Player->FinishSpawning(Stand);
		}
		return Player;
	}
}

// ---------------------------------------------------------------------------
// The rows: the strafe speed map, the sprint's ramps, the presets.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCGaitTableRowsTest, "VoxelCharacter.Movement.GaitTable", VC_GAIT_TEST_FLAGS)

bool FVCGaitTableRowsTest::RunTest(const FString& Parameters)
{
	const FVCMovementTable GASP = FVCMovementTable::Preset(EVCMovePreset::GASP);
	// GASP's rows (SandboxCharacter_CMC): walk 200 / 180 / 150, run 500 / 350 / 300, sprint 700, crouch 225
	TestEqual(TEXT("Walk forward"), GASP.Walk.MaxSpeedForward, 200.f);
	TestEqual(TEXT("Walk strafe"), GASP.Walk.MaxSpeedStrafe, 180.f);
	TestEqual(TEXT("Walk back"), GASP.Walk.MaxSpeedBackward, 150.f);
	TestEqual(TEXT("Run forward"), GASP.Run.MaxSpeedForward, 500.f);
	TestEqual(TEXT("Run strafe"), GASP.Run.MaxSpeedStrafe, 350.f);
	TestEqual(TEXT("Run back"), GASP.Run.MaxSpeedBackward, 300.f);
	TestEqual(TEXT("Sprint"), GASP.Sprint.MaxSpeedForward, 700.f);
	TestEqual(TEXT("Crouch"), GASP.Crouch.MaxSpeedForward, 225.f);
	TestEqual(TEXT("Acceleration 800"), GASP.Run.MaxAcceleration, 800.f);
	TestEqual(TEXT("Braking with input 500"), GASP.Run.BrakingDecelerationMoving, 500.f);
	TestEqual(TEXT("Braking stopping 2000"), GASP.Run.BrakingDecelerationStopping, 2000.f);
	TestEqual(TEXT("Friction 5"), GASP.Run.GroundFriction, 5.f);
	TestEqual(TEXT("Instant yaw on the ground"), GASP.GroundOrientRotationRate, -1.f);
	TestEqual(TEXT("200 in the air"), GASP.AirRotationRate, 200.f);
	TestTrue(TEXT("The table's defaults are GASP's"), FVCMovementTable().Run == GASP.Run && FVCMovementTable().Walk == GASP.Walk);

	// The strafe speed map (GASP's curve): forward within 45 degrees, the strafe speed at 90, backward from 135, linear between, either sign
	TestEqual(TEXT("Ahead: forward"), GASP.Run.MaxSpeedAtAngle(0.f), 500.f);
	TestEqual(TEXT("Within 45 degrees: still forward"), GASP.Run.MaxSpeedAtAngle(45.f), 500.f);
	TestEqual(TEXT("From 135 degrees: backward"), GASP.Run.MaxSpeedAtAngle(-135.f), 300.f);
	TestEqual(TEXT("Sideways: strafe"), GASP.Run.MaxSpeedAtAngle(90.f), 350.f);
	TestEqual(TEXT("Sideways left: strafe"), GASP.Run.MaxSpeedAtAngle(-90.f), 350.f);
	TestEqual(TEXT("Back: backward"), GASP.Run.MaxSpeedAtAngle(180.f), 300.f);
	TestEqual(TEXT("Back the other way"), GASP.Run.MaxSpeedAtAngle(-180.f), 300.f);
	TestEqual(TEXT("67.5 degrees: halfway to the strafe"), GASP.Run.MaxSpeedAtAngle(67.5f), 425.f);
	TestEqual(TEXT("112.5 degrees: halfway to backward"), GASP.Run.MaxSpeedAtAngle(112.5f), 325.f);
	TestEqual(TEXT("Past 180 clamps"), GASP.Run.MaxSpeedAtAngle(270.f), 300.f);

	// The sprint's ramps: the acceleration 800 to 300 over 300..700 uu/s, the friction 5 to 3 over 0..500
	TestEqual(TEXT("Sprint acceleration from rest"), GASP.SprintAccelerationAtSpeed(0.f), 800.f);
	TestEqual(TEXT("Sprint acceleration at 300"), GASP.SprintAccelerationAtSpeed(300.f), 800.f);
	TestEqual(TEXT("Sprint acceleration at 500"), GASP.SprintAccelerationAtSpeed(500.f), 550.f);
	TestEqual(TEXT("Sprint acceleration at the top"), GASP.SprintAccelerationAtSpeed(700.f), 300.f);
	TestEqual(TEXT("Sprint acceleration past the top"), GASP.SprintAccelerationAtSpeed(900.f), 300.f);
	TestEqual(TEXT("Sprint friction from rest"), GASP.SprintFrictionAtSpeed(0.f), 5.f);
	TestEqual(TEXT("Sprint friction at 250"), GASP.SprintFrictionAtSpeed(250.f), 4.f);
	TestEqual(TEXT("Sprint friction at 500 and over"), GASP.SprintFrictionAtSpeed(600.f), 3.f);

	// Legacy: what the player ran on before C4
	const FVCMovementTable Legacy = FVCMovementTable::Preset(EVCMovePreset::Legacy);
	TestEqual(TEXT("Legacy: 600 every direction"), Legacy.Run.MaxSpeedAtAngle(112.5f), 600.f);
	TestEqual(TEXT("Legacy: sprint 960"), Legacy.Sprint.MaxSpeedForward, 960.f);
	TestEqual(TEXT("Legacy: crouch 150"), Legacy.Crouch.MaxSpeedForward, 150.f);
	TestEqual(TEXT("Legacy: acceleration 2048"), Legacy.Run.MaxAcceleration, 2048.f);
	TestEqual(TEXT("Legacy: braking 4096"), Legacy.Run.BrakingDecelerationStopping, 4096.f);
	TestEqual(TEXT("Legacy: friction 8"), Legacy.Run.GroundFriction, 8.f);
	TestEqual(TEXT("Legacy: braking friction factor 3"), Legacy.Run.BrakingFrictionFactor, 3.f);
	TestEqual(TEXT("Legacy: 500 degrees a second"), Legacy.GroundOrientRotationRate, 500.f);
	TestEqual(TEXT("Legacy: no sprint ramp"), Legacy.SprintAccelerationAtSpeed(700.f), 2048.f);
	TestEqual(TEXT("Legacy: no friction ramp"), Legacy.SprintFrictionAtSpeed(700.f), 8.f);

	// Row access and the printed initializer
	TestTrue(TEXT("Row(Sprint)"), GASP.Row(EVCGait::Sprint) == GASP.Sprint);
	TestTrue(TEXT("Row(Crouch)"), GASP.Row(EVCGait::Crouch) == GASP.Crouch);
	TestEqual(TEXT("ToString is the C++ initializer"), GASP.Run.ToString(), FString(TEXT("{ 500, 350, 300, 800, 500, 2000, 5, 0 }")));

	// The angle about up: positive to the right, 0 along, 180 against, 0 for a direction along up
	const FVector Up = FVector::UpVector;
	TestEqual(TEXT("Ahead"), UVCMovementComponent::AngleAboutUp(FVector::ForwardVector, FVector::ForwardVector, Up), 0.f);
	TestTrue(TEXT("Right is positive 90"), FMath::IsNearlyEqual(UVCMovementComponent::AngleAboutUp(FVector::ForwardVector, FVector::RightVector, Up), 90.f, 1e-3f));
	TestTrue(TEXT("Left is negative 90"), FMath::IsNearlyEqual(UVCMovementComponent::AngleAboutUp(FVector::ForwardVector, -FVector::RightVector, Up), -90.f, 1e-3f));
	TestTrue(TEXT("Behind is 180"), FMath::IsNearlyEqual(FMath::Abs(UVCMovementComponent::AngleAboutUp(FVector::ForwardVector, -FVector::ForwardVector, Up)), 180.f, 1e-3f));
	TestEqual(TEXT("Along up: nothing"), UVCMovementComponent::AngleAboutUp(FVector::ForwardVector, FVector::UpVector, Up), 0.f);
	// On a planet's side (up along +X, the body facing +Z): the right hand is cross(up, forward) = -Y, so +Y is the left
	const FVector SideUp = FVector::ForwardVector;
	TestTrue(TEXT("Planet side: +Y is 90 to the left of a body facing +Z"), FMath::IsNearlyEqual(UVCMovementComponent::AngleAboutUp(FVector::UpVector, FVector::RightVector, SideUp), -90.f, 1e-3f));
	TestTrue(TEXT("Planet side: -Y is 90 to the right"), FMath::IsNearlyEqual(UVCMovementComponent::AngleAboutUp(FVector::UpVector, -FVector::RightVector, SideUp), 90.f, 1e-3f));

	// The camera's lag cap (C4): the pivot never trails farther than the cap
	bool bSnapped = false;
	const FVector Old(1000, 2000, 300);
	const FVector Far = Old + FVector(1000.0, 0.0, 0.0);
	const FVector Capped = UVCThirdPersonCameraMode::ComputeLaggedPivot(Old, Far, Old, 1.f / 60.f, 10.f, 5000.f, bSnapped, 60.f);
	TestTrue(TEXT("Capped lag: 60 uu behind"), FMath::IsNearlyEqual(FVector::Dist(Capped, Far), 60.0, 1e-6));
	TestTrue(TEXT("Capped lag: on the line"), FMath::IsNearlyEqual(Capped.Y, Far.Y, 1e-6) && FMath::IsNearlyEqual(Capped.Z, Far.Z, 1e-6));
	const FVector Uncapped = UVCThirdPersonCameraMode::ComputeLaggedPivot(Old, Far, Old, 1.f / 60.f, 10.f, 5000.f, bSnapped, 0.f);
	TestTrue(TEXT("No cap: the interpolation alone"), FVector::Dist(Uncapped, Far) > 60.0);
	const FVector Near = UVCThirdPersonCameraMode::ComputeLaggedPivot(Old, Old + FVector(10.0, 0.0, 0.0), Old, 1.f / 60.f, 10.f, 5000.f, bSnapped, 60.f);
	TestTrue(TEXT("Within the cap: untouched"), Near.Equals(FMath::VInterpTo(Old, Old + FVector(10.0, 0.0, 0.0), 1.f / 60.f, 10.f), 1e-6));
	return true;
}

// ---------------------------------------------------------------------------
// The table applied to a player's component: the row by gait and direction sets the engine's values; an NPC keeps its own.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCGaitTableAppliedTest, "VoxelCharacter.Movement.GaitTableApplied", VC_GAIT_TEST_FLAGS)

bool FVCGaitTableAppliedTest::RunTest(const FString& Parameters)
{
	VCTesting::FPlayWorld W(TEXT("VCGaitTableWorld"), /*bDestroyActorsFirst=*/true);
	AVCCharacterBase* Player = VCGaitTests::SpawnPlayer(W.World, FRotator::ZeroRotator);
	UVCMovementComponent* Move = Player ? Cast<UVCMovementComponent>(Player->GetCharacterMovement()) : nullptr;
	if (!TestNotNull(TEXT("The player has the voxel movement"), Move))
	{
		return false;
	}
	TestTrue(TEXT("A player uses the table"), Move->bUseGaitTable);
	Move->SetMovementMode(MOVE_Walking);
	TestTrue(TEXT("Walking"), Move->IsMovingOnGround());
	Move->GaitTable = FVCMovementTable::Preset(EVCMovePreset::GASP);

	// The run, standing: the forward cap, the row's acceleration, the stopping braking, the friction, instant yaw
	Move->ApplyGaitTable();
	TestTrue(TEXT("Gait: run"), Move->GetGait() == EVCGait::Run);
	TestEqual(TEXT("Run: 500"), Move->MaxWalkSpeed, 500.f);
	TestEqual(TEXT("Run: acceleration 800"), Move->MaxAcceleration, 800.f);
	TestEqual(TEXT("No input: the stopping braking"), Move->BrakingDecelerationWalking, 2000.f);
	TestEqual(TEXT("Friction 5 on plain dry ground"), Move->GroundFriction, 5.f);
	TestEqual(TEXT("Braking friction factor 0 (GASP: the deceleration alone stops)"), Move->BrakingFrictionFactor, 0.f);
	TestEqual(TEXT("Orient on the ground: instant"), Move->RotationRate.Yaw, -1.0);
	TestEqual(TEXT("GetMaxSpeed is the row's"), Move->GetMaxSpeed(), 500.f);

	// The tick's arithmetic with input (the engine keeps the acceleration to itself): the moving braking, the strafe map,
	// the sprint's ramps, the surface scale
	const FVector Fwd = FVector::ForwardVector, Up = FVector::UpVector;
	const FVCMovementTable& T = Move->GaitTable;
	FVCGaitValues V = UVCMovementComponent::ComputeGaitValues(T, EVCGait::Run, EVCRotationMode::OrientToMovement, true, false, FVector(300, 0, 0), FVector(800, 0, 0), Fwd, Up, 1.f, 1.f);
	TestEqual(TEXT("With input: the moving braking"), V.BrakingDeceleration, 500.f);
	TestEqual(TEXT("With input: the row's cap"), V.MaxSpeed, 500.f);
	V = UVCMovementComponent::ComputeGaitValues(T, EVCGait::Sprint, EVCRotationMode::OrientToMovement, true, false, FVector::ZeroVector, FVector(800, 0, 0), Fwd, Up, 1.f, 1.f);
	TestEqual(TEXT("Sprint: 700"), V.MaxSpeed, 700.f);
	TestEqual(TEXT("Sprint from rest: 800"), V.MaxAcceleration, 800.f);
	TestEqual(TEXT("Sprint from rest: friction 5"), V.GroundFriction, 5.f);
	V = UVCMovementComponent::ComputeGaitValues(T, EVCGait::Sprint, EVCRotationMode::OrientToMovement, true, false, FVector(500, 0, 0), FVector(800, 0, 0), Fwd, Up, 1.f, 1.f);
	TestEqual(TEXT("Sprint at 500: acceleration 550"), V.MaxAcceleration, 550.f);
	TestEqual(TEXT("Sprint at 500: friction 3"), V.GroundFriction, 3.f);
	// Strafe mode: the cap by the velocity's angle to the facing (the body faces +X)
	V = UVCMovementComponent::ComputeGaitValues(T, EVCGait::Run, EVCRotationMode::Strafe, true, false, FVector(0, 300, 0), FVector(0, 800, 0), Fwd, Up, 1.f, 1.f);
	TestEqual(TEXT("Strafing sideways: 350"), V.MaxSpeed, 350.f);
	V = UVCMovementComponent::ComputeGaitValues(T, EVCGait::Run, EVCRotationMode::Strafe, true, false, FVector(-300, 0, 0), FVector(-800, 0, 0), Fwd, Up, 1.f, 1.f);
	TestEqual(TEXT("Backpedalling: 300"), V.MaxSpeed, 300.f);
	V = UVCMovementComponent::ComputeGaitValues(T, EVCGait::Run, EVCRotationMode::Strafe, true, false, FVector(300, 300, 0), FVector::ZeroVector, Fwd, Up, 1.f, 1.f);
	TestTrue(TEXT("Diagonal within 45 degrees: the forward cap"), FMath::IsNearlyEqual(V.MaxSpeed, 500.f, 1e-2f));
	V = UVCMovementComponent::ComputeGaitValues(T, EVCGait::Run, EVCRotationMode::Strafe, true, false, FVector::ZeroVector, FVector(-800, 0, 0), Fwd, Up, 1.f, 1.f);
	TestEqual(TEXT("A backpedal from rest starts at 300"), V.MaxSpeed, 300.f);
	V = UVCMovementComponent::ComputeGaitValues(T, EVCGait::Run, EVCRotationMode::OrientToMovement, true, false, FVector(-300, 0, 0), FVector(-800, 0, 0), Fwd, Up, 1.f, 1.f);
	TestEqual(TEXT("Orient: the forward cap whatever the velocity"), V.MaxSpeed, 500.f);
	TestEqual(TEXT("Orient on the ground: instant"), V.RotationRate, -1.f);
	V = UVCMovementComponent::ComputeGaitValues(T, EVCGait::Run, EVCRotationMode::Strafe, true, false, FVector::ZeroVector, FVector::ZeroVector, Fwd, Up, 0.5f, 0.2f);
	TestEqual(TEXT("The GAS multiplier scales the cap"), V.MaxSpeed, 250.f);
	TestEqual(TEXT("The surface scales the friction"), V.GroundFriction, 1.f);
	TestEqual(TEXT("Strafe on the ground: the strafe rate"), V.RotationRate, -1.f);
	V = UVCMovementComponent::ComputeGaitValues(T, EVCGait::Run, EVCRotationMode::OrientToMovement, false, false, FVector::ZeroVector, FVector::ZeroVector, Fwd, Up, 1.f, 1.f);
	TestFalse(TEXT("Swimming: the row does not apply"), V.bRowApplies);
	TestEqual(TEXT("Swimming: the swim's yaw rate"), V.RotationRate, 500.f);
	// On a planet's side (up along +X, the body facing +Z): a velocity along -Y is a strafe to the left
	V = UVCMovementComponent::ComputeGaitValues(T, EVCGait::Run, EVCRotationMode::Strafe, true, false, FVector(0, -300, 0), FVector::ZeroVector, FVector::UpVector, FVector::ForwardVector, 1.f, 1.f);
	TestEqual(TEXT("Planet side: sideways is the strafe cap"), V.MaxSpeed, 350.f);

	// The sprint through the component
	Move->SetSprinting(true);
	Move->Velocity = FVector::ZeroVector;
	Move->ApplyGaitTable();
	TestTrue(TEXT("Gait: sprint"), Move->GetGait() == EVCGait::Sprint);
	TestEqual(TEXT("Sprint: 700 on the component"), Move->MaxWalkSpeed, 700.f);
	Move->SetSprinting(false);

	// Strafe mode through the component (the body faces +X, moving sideways)
	Move->bOrientRotationToMovement = false;
	Move->bUseControllerDesiredRotation = true;
	Move->Velocity = FVector(0, 300, 0);
	Move->ApplyGaitTable();
	TestTrue(TEXT("Rotation mode: strafe"), Move->GetRotationMode() == EVCRotationMode::Strafe);
	TestEqual(TEXT("Strafing sideways: 350 on the component"), Move->MaxWalkSpeed, 350.f);
	Move->bOrientRotationToMovement = true;
	Move->bUseControllerDesiredRotation = false;
	Move->Velocity = FVector::ZeroVector;

	// The walk and the crouch
	Move->SetWalking(true);
	Move->ApplyGaitTable();
	TestTrue(TEXT("Gait: walk"), Move->GetGait() == EVCGait::Walk);
	TestEqual(TEXT("Walk: 200"), Move->MaxWalkSpeed, 200.f);
	Move->SetWalking(false);
	Player->bIsCrouched = true;
	Move->ApplyGaitTable();
	TestTrue(TEXT("Gait: crouch"), Move->GetGait() == EVCGait::Crouch);
	TestEqual(TEXT("Crouch: 225"), Move->MaxWalkSpeed, 225.f);
	TestEqual(TEXT("Crouch: the crouched cap too"), Move->MaxWalkSpeedCrouched, 225.f);
	Move->SetSprinting(true);
	TestTrue(TEXT("Crouched beats the sprint"), Move->GetGait() == EVCGait::Crouch);
	Move->SetSprinting(false);
	Player->bIsCrouched = false;

	// In the air: the air yaw rate, the ground values left
	Move->SetMovementMode(MOVE_Falling);
	Move->MaxWalkSpeed = 123.f;
	Move->ApplyGaitTable();
	TestEqual(TEXT("In the air: 200 degrees a second"), Move->RotationRate.Yaw, 200.0);
	TestEqual(TEXT("In the air: the row applies too"), Move->MaxWalkSpeed, 500.f);
	// Swimming: nothing but the yaw rate
	Move->SetMovementMode(MOVE_Swimming);
	Move->MaxAcceleration = 256.f;
	Move->ApplyGaitTable();
	TestEqual(TEXT("Swimming: the swim's acceleration kept"), Move->MaxAcceleration, 256.f);
	TestEqual(TEXT("Swimming: the old yaw rate"), Move->RotationRate.Yaw, 500.0);
	// G154: out of the water by any mode change, the ground's acceleration is back
	Move->SetMovementMode(MOVE_Flying);
	TestTrue(TEXT("G154: a flight out of the water restores the ground acceleration"), Move->MaxAcceleration > 256.f);
	Move->SetMovementMode(MOVE_Walking);

	// The preset swap
	Move->GaitTable = FVCMovementTable::Preset(EVCMovePreset::Legacy);
	Move->Velocity = FVector::ZeroVector;
	Move->ApplyGaitTable();
	TestEqual(TEXT("Legacy: 600"), Move->MaxWalkSpeed, 600.f);
	TestEqual(TEXT("Legacy: 4096"), Move->BrakingDecelerationWalking, 4096.f);
	TestEqual(TEXT("Legacy: 500 a second"), Move->RotationRate.Yaw, 500.0);

	// The table off: the values stay as set (an NPC's)
	Move->bUseGaitTable = false;
	Move->MaxWalkSpeed = 300.f;
	Move->RotationRate.Yaw = 360.f;
	Move->ApplyGaitTable();
	TestEqual(TEXT("Off: the speed stays"), Move->MaxWalkSpeed, 300.f);
	TestEqual(TEXT("Off: the yaw rate stays"), Move->RotationRate.Yaw, 360.0);
	TestEqual(TEXT("Off: GetMaxSpeed is the walk without a sprint"), Move->GetMaxSpeed(), 300.f);

	// An NPC: the table off from its constructor, its own numbers
	const FTransform NPCStand(FVector(500, 0, 1000));
	AVCNPCCharacterBase* NPC = W.World->SpawnActorDeferred<AVCNPCCharacterBase>(AVCNPCCharacterBase::StaticClass(), NPCStand, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (TestNotNull(TEXT("The NPC spawns"), NPC))
	{
		NPC->bAIEnabled = false;
		NPC->FinishSpawning(NPCStand);
		UVCMovementComponent* NPCMove = Cast<UVCMovementComponent>(NPC->GetCharacterMovement());
		if (TestNotNull(TEXT("The NPC has the voxel movement"), NPCMove))
		{
			TestFalse(TEXT("An NPC does not use the table"), NPCMove->bUseGaitTable);
			NPCMove->ApplyGaitTable();
			TestEqual(TEXT("NPC: 300"), NPCMove->MaxWalkSpeed, 300.f);
			TestEqual(TEXT("NPC: 360 a second"), NPCMove->RotationRate.Yaw, 360.0);
			TestEqual(TEXT("NPC: the engine's acceleration"), NPCMove->MaxAcceleration, 2048.f);
			TestEqual(TEXT("NPC: the component's braking"), NPCMove->BrakingDecelerationWalking, 4096.f);
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// The held set's gait caps (the locomotion state machine plan's L2, D91): the walk, run and sprint rows at the clip set's
// forward caps, the strafe and backward caps in proportion; only while the state machine plays (the saved-move flag);
// and on a spawned player, the caps the character gives its movement and the row the tick applies.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCGaitClipCapsTest, "VoxelCharacter.Movement.GaitClipCaps", VC_GAIT_TEST_FLAGS)

bool FVCGaitClipCapsTest::RunTest(const FString& Parameters)
{
	const FVCMovementTable GASP = FVCMovementTable::Preset(EVCMovePreset::GASP);
	FVCGaitCaps Caps;
	Caps.Walk = 120.f;
	Caps.Run = 400.f;
	Caps.Sprint = 700.f;
	const FVCMovementTable Packs = GASP.WithForwardCaps(Caps);
	TestTrue(TEXT("The walk at 120, its strafe and back in proportion"),
		Packs.Walk.MaxSpeedForward == 120.f && FMath::IsNearlyEqual(Packs.Walk.MaxSpeedStrafe, 108.f, 0.01f) && FMath::IsNearlyEqual(Packs.Walk.MaxSpeedBackward, 90.f, 0.01f));
	TestTrue(TEXT("The run at 400"), Packs.Run.MaxSpeedForward == 400.f && FMath::IsNearlyEqual(Packs.Run.MaxSpeedStrafe, 280.f, 0.01f)
		&& FMath::IsNearlyEqual(Packs.Run.MaxSpeedBackward, 240.f, 0.01f));
	TestTrue(TEXT("The sprint at 700"), Packs.Sprint.MaxSpeedForward == 700.f && Packs.Sprint.MaxSpeedStrafe == 700.f);
	TestTrue(TEXT("No crouch cap: the crouch's row, the accelerations, the braking unchanged"), Packs.Crouch == GASP.Crouch
		&& Packs.Run.MaxAcceleration == GASP.Run.MaxAcceleration && Packs.Run.BrakingDecelerationStopping == GASP.Run.BrakingDecelerationStopping);
	FVCGaitCaps WithCrouch = Caps;
	WithCrouch.Crouch = 250.f;
	const FVCMovementTable Crouched = GASP.WithForwardCaps(WithCrouch);
	TestTrue(TEXT("The crouch set's cap (L3): the crouch at 250, its strafe and back in proportion"),
		Crouched.Crouch.MaxSpeedForward == 250.f && FMath::IsNearlyEqual(Crouched.Crouch.MaxSpeedStrafe, 200.f * 250.f / 225.f, 0.01f)
		&& FMath::IsNearlyEqual(Crouched.Crouch.MaxSpeedBackward, 180.f * 250.f / 225.f, 0.01f)
		&& Crouched.Crouch.MaxAcceleration == GASP.Crouch.MaxAcceleration);
	FVCGaitCaps RunOnly;
	RunOnly.Run = 450.f;
	const FVCMovementTable Partial = GASP.WithForwardCaps(RunOnly);
	TestTrue(TEXT("A cap of 0 keeps its row"), Partial.Walk == GASP.Walk && Partial.Sprint == GASP.Sprint && Partial.Run.MaxSpeedForward == 450.f);
	TestTrue(TEXT("Not applied (motion matching, the Blueprint's graph): the table as it is"),
		UVCMovementComponent::ResolveGaitTable(GASP, Caps, false).Run == GASP.Run);
	TestTrue(TEXT("No caps: the table as it is"), UVCMovementComponent::ResolveGaitTable(GASP, FVCGaitCaps(), true).Run == GASP.Run);

	VCTesting::FPlayWorld W(TEXT("VCGaitCapsWorld"), /*bDestroyActorsFirst=*/true);
	AVCCharacterBase* Player = VCGaitTests::SpawnPlayer(W.World, FRotator::ZeroRotator);
	UVCMovementComponent* Move = Player ? Cast<UVCMovementComponent>(Player->GetCharacterMovement()) : nullptr;
	if (!TestNotNull(TEXT("The player has the voxel movement"), Move))
	{
		return false;
	}
	Move->SetMovementMode(MOVE_Walking);
	Move->GaitTable = GASP;
	TestFalse(TEXT("The tests' player plays the Blueprint's graph: the caps do not apply"), Move->bWantsClipGaitCaps);
	Move->SetClipGaitCaps(Caps);
	Move->ApplyGaitTable();
	TestEqual(TEXT("Not applied: the run's 500"), Move->MaxWalkSpeed, 500.f);
	Move->bWantsClipGaitCaps = true;
	Move->ApplyGaitTable();
	TestEqual(TEXT("Applied: the held set's 400"), Move->MaxWalkSpeed, 400.f);
	Move->SetClipGaitCaps(WithCrouch);
	Player->bIsCrouched = true;
	Move->ApplyGaitTable();
	TestEqual(TEXT("Crouched: the crouch set's 250"), Move->MaxWalkSpeedCrouched, 250.f);
	Player->bIsCrouched = false;
	Move->bWantsClipGaitCaps = false;
	return true;
}

// ---------------------------------------------------------------------------
// The eased face-aim flip: the flags change once the body has turned to within the tolerance of the view (or the time
// is up), the capsule turning at AimTurnRate meanwhile; a release while moving turns back toward the movement; a flip
// with nothing to turn through completes at once.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAimTurnEaseTest, "VoxelCharacter.Movement.AimTurnEase", VC_GAIT_TEST_FLAGS)

bool FVCAimTurnEaseTest::RunTest(const FString& Parameters)
{
	VCTesting::FPlayWorld W(TEXT("VCAimTurnWorld"), /*bDestroyActorsFirst=*/true);
	AVCCharacterBase* Player = VCGaitTests::SpawnPlayer(W.World, FRotator::ZeroRotator);
	UVCMovementComponent* Move = Player ? Cast<UVCMovementComponent>(Player->GetCharacterMovement()) : nullptr;
	if (!TestNotNull(TEXT("The player has the voxel movement"), Move))
	{
		return false;
	}
	AAIController* Controller = W.World->SpawnActor<AAIController>();
	if (!TestNotNull(TEXT("A controller spawns"), Controller))
	{
		return false;
	}
	Controller->Possess(Player);
	Player->SetActorRotation(FRotator::ZeroRotator);
	Move->AimTurnTolerance = 15.f;
	Move->AimTurnMaxTime = 0.5f;
	Move->AimTurnRate = 540.f;
	const float Dt = 1.f / 60.f; // 9 degrees a frame

	// Already facing the view: the flip is immediate, as before C4
	Controller->SetControlRotation(FRotator(0.f, 5.f, 0.f));
	Player->SetFacesAim(true);
	TestFalse(TEXT("Within tolerance: no turn pending"), Move->IsAimTurnPending());
	TestTrue(TEXT("Within tolerance: strafe at once"), !Move->bOrientRotationToMovement && Move->bUseControllerDesiredRotation);
	Player->SetFacesAim(false);
	TestFalse(TEXT("A release standing still completes at once"), Move->IsAimTurnPending());
	TestTrue(TEXT("Back to orient"), Move->bOrientRotationToMovement && !Move->bUseControllerDesiredRotation);

	// The view 180 degrees behind: the draw starts a turn; the flags hold the old mode while it turns
	Controller->SetControlRotation(FRotator(0.f, 180.f, 0.f));
	Player->SetFacesAim(true);
	TestTrue(TEXT("Far from the view: the turn is pending"), Move->IsAimTurnPending());
	TestTrue(TEXT("Pending: still orient"), Move->bOrientRotationToMovement && !Move->bUseControllerDesiredRotation);
	TestTrue(TEXT("Pending: the body has not snapped"), FMath::Abs(Player->GetActorRotation().Yaw) < 1.f);
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		Move->PhysicsRotation(Dt);
	}
	TestTrue(TEXT("After 10 frames: still turning"), Move->IsAimTurnPending());
	const float YawAfter10 = FMath::Abs(FRotator::NormalizeAxis(Player->GetActorRotation().Yaw));
	TestTrue(TEXT("After 10 frames: about 90 degrees round"), FMath::IsNearlyEqual(YawAfter10, 90.f, 2.f));
	for (int32 Frame = 0; Frame < 10 && Move->IsAimTurnPending(); ++Frame)
	{
		Move->PhysicsRotation(Dt);
	}
	TestFalse(TEXT("Within the tolerance: the flip completes"), Move->IsAimTurnPending());
	TestTrue(TEXT("Completed: strafe"), !Move->bOrientRotationToMovement && Move->bUseControllerDesiredRotation);
	TestTrue(TEXT("Completed: the body is within the tolerance of the view"),
		FMath::Abs(FRotator::NormalizeAxis(Player->GetActorRotation().Yaw - 180.f)) <= 15.f + 1e-3f);

	// A release while moving: back toward where the body moves (-Y), at the rate, then orient
	Move->Velocity = FVector(0, -300, 0);
	Player->SetFacesAim(false);
	TestTrue(TEXT("Release while moving: the turn is pending"), Move->IsAimTurnPending());
	TestTrue(TEXT("Release pending: still strafe"), !Move->bOrientRotationToMovement && Move->bUseControllerDesiredRotation);
	for (int32 Frame = 0; Frame < 30 && Move->IsAimTurnPending(); ++Frame)
	{
		Move->PhysicsRotation(Dt);
	}
	TestFalse(TEXT("The release completes"), Move->IsAimTurnPending());
	TestTrue(TEXT("Released: orient"), Move->bOrientRotationToMovement && !Move->bUseControllerDesiredRotation);
	TestTrue(TEXT("Released: the body faces about -Y"), FMath::Abs(FRotator::NormalizeAxis(Player->GetActorRotation().Yaw + 90.f)) <= 15.f + 1e-3f);

	// The time limit: a slow turn completes at AimTurnMaxTime whatever the angle left (the engine's own rotation, which
	// runs once the flip is complete, is held still by a zero rate so only the eased turn moves the body)
	Move->Velocity = FVector::ZeroVector;
	Move->RotationRate.Yaw = 0.0;
	Move->AimTurnRate = 30.f;
	Move->AimTurnMaxTime = 0.2f;
	Player->SetActorRotation(FRotator::ZeroRotator);
	Controller->SetControlRotation(FRotator(0.f, 180.f, 0.f));
	Player->SetFacesAim(true);
	TestTrue(TEXT("Slow: pending"), Move->IsAimTurnPending());
	for (int32 Frame = 0; Frame < 11; ++Frame)
	{
		Move->PhysicsRotation(Dt);
	}
	TestTrue(TEXT("Slow: still pending just under the time"), Move->IsAimTurnPending());
	Move->PhysicsRotation(Dt);
	Move->PhysicsRotation(Dt);
	TestFalse(TEXT("Slow: completed by the time limit"), Move->IsAimTurnPending());
	TestTrue(TEXT("Slow: strafe"), !Move->bOrientRotationToMovement && Move->bUseControllerDesiredRotation);
	TestTrue(TEXT("Slow: the body turned only what the rate allowed"), FMath::Abs(FRotator::NormalizeAxis(Player->GetActorRotation().Yaw)) < 10.f);

	Controller->UnPossess();
	return true;
}

#endif // WITH_AUTOMATION_TESTS
