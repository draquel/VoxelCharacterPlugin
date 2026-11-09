// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Core/VCAnimInstance.h"
#include "Core/VCAnimLayerInstance.h"
#include "Core/VCCharacterBase.h"
#include "Core/VCNPCCharacterBase.h"
#include "Core/VCGameplayTags.h"
#include "Movement/VCGravityFrame.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

// ---------------------------------------------------------------------------
// The anim instance of every character (character animation P2, CHARACTER_ANIMATION_PLAN.md):
// - On a plane (the default gravity) the values are the Z-up expressions the anim instance used before P2.
// - On a planet the values equal the plane's for the same motion turned into the local frame, at the poles, the
//   equator and between, whatever the frame's own yaw (it spins near the south pole).
// - The turn-in-place offset holds while standing, clamps, returns to zero once moving, and the south-pole twist a kept
//   gravity-relative yaw would see never reaches it.
// - The action set: Unarmed with nothing held, Tool for an item without a set, the item's own set otherwise.
// - The gather from a spawned character on a tilted frame: gravity, velocity, the ground below while falling.
// - The aiming stance layer reads the body's aim (ranged combat plan B4).
// - The IK and pace values (character animation P4), and the foot-bone placement's weight (P6).
// ---------------------------------------------------------------------------

namespace VCAnimTestHelpers
{
	constexpr float Dt = 1.0f / 60.0f;

	/** Direction for a latitude and longitude (degrees; +Z north, longitude from +X toward +Y). */
	FVector LatLon(double Lat, double Lon)
	{
		const double La = FMath::DegreesToRadians(Lat);
		const double Lo = FMath::DegreesToRadians(Lon);
		return FVector(FMath::Cos(La) * FMath::Cos(Lo), FMath::Cos(La) * FMath::Sin(Lo), FMath::Sin(La));
	}

	/** The smallest angle between two angles (degrees). */
	double AngleGap(double A, double B)
	{
		return FMath::Abs(FMath::FindDeltaAngleDegrees(A, B));
	}

	/** A plane input: the default gravity, an upright actor at a yaw, a random velocity, acceleration and look. */
	FVCAnimInput RandomPlaneInput(FRandomStream& Random, double ActorYaw)
	{
		FVCAnimInput In;
		In.DeltaSeconds = Dt;
		In.ActorRotation = FRotator(0.0, ActorYaw, 0.0).Quaternion();
		In.Velocity = FVector(Random.FRandRange(-600.0, 600.0), Random.FRandRange(-600.0, 600.0), Random.FRandRange(-300.0, 300.0));
		In.Acceleration = FVector(Random.FRandRange(-2048.0, 2048.0), Random.FRandRange(-2048.0, 2048.0), 0.0);
		In.MaxAcceleration = 2048.f;
		In.MaxBrakingDeceleration = 1500.f;
		In.ControlRotation = FRotator(Random.FRandRange(-85.0, 85.0), Random.FRandRange(-180.0, 180.0), 0.0);
		In.bFalling = Random.FRand() < 0.3f;
		In.bOnGround = !In.bFalling;
		In.GroundDistance = In.bFalling ? (Random.FRand() < 0.8f ? Random.FRandRange(0.0, 900.0) : -1.f) : 0.f;
		if (!In.bFalling)
		{
			// A floor tilted up to 40 degrees in any direction.
			In.FloorNormal = FRotator(Random.FRandRange(-40.0, 40.0), Random.FRandRange(-180.0, 180.0), 0.0).RotateVector(FVector::UpVector);
		}
		return In;
	}

	/** The same motion in a frame whose up is Q * Z. */
	FVCAnimInput Rotated(const FVCAnimInput& In, const FQuat& Q)
	{
		FVCAnimInput Out = In;
		Out.GravityDirection = Q.RotateVector(In.GravityDirection);
		Out.Velocity = Q.RotateVector(In.Velocity);
		Out.Acceleration = Q.RotateVector(In.Acceleration);
		Out.ActorRotation = Q * In.ActorRotation;
		Out.ControlRotation = (Q * In.ControlRotation.Quaternion()).Rotator();
		Out.FloorNormal = Q.RotateVector(In.FloorNormal);
		return Out;
	}

	/** An anim instance on a bare mesh component (an anim instance lives within one); ComputeValues needs no character. */
	UVCAnimInstance* NewAnimInstance()
	{
		USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(GetTransientPackage());
		return NewObject<UVCAnimInstance>(Mesh);
	}

	/** The largest difference between two instances' values (degrees, uu, seconds alike), and which value it is. */
	double WorstDifference(const UVCAnimInstance& A, const UVCAnimInstance& B, FString& OutWhich)
	{
		double Worst = 0.0;
		const auto Check = [&Worst, &OutWhich](const TCHAR* Name, double Difference)
		{
			if (Difference > Worst)
			{
				Worst = Difference;
				OutWhich = Name;
			}
		};
		Check(TEXT("Speed"), FMath::Abs(A.Speed - B.Speed));
		Check(TEXT("Direction"), AngleGap(A.Direction, B.Direction));
		Check(TEXT("LocalVelocity"), (A.LocalVelocity - B.LocalVelocity).GetAbsMax());
		Check(TEXT("LocalAcceleration"), (A.LocalAcceleration - B.LocalAcceleration).GetAbsMax());
		Check(TEXT("RelativeAcceleration"), (A.RelativeAcceleration - B.RelativeAcceleration).GetAbsMax());
		Check(TEXT("YawRate"), FMath::Abs(A.YawRate - B.YawRate));
		Check(TEXT("RootYawOffset"), AngleGap(A.RootYawOffset, B.RootYawOffset));
		Check(TEXT("AimPitch"), FMath::Abs(A.AimPitch - B.AimPitch));
		Check(TEXT("AimYaw"), AngleGap(A.AimYaw, B.AimYaw));
		Check(TEXT("SlopePitch"), FMath::Abs(A.SlopePitch - B.SlopePitch));
		Check(TEXT("SlopeRoll"), FMath::Abs(A.SlopeRoll - B.SlopeRoll));
		Check(TEXT("GroundDistance"), FMath::Abs(A.GroundDistance - B.GroundDistance));
		Check(TEXT("TimeToLand"), FMath::Abs(A.TimeToLand - B.TimeToLand));
		Check(TEXT("bIsFalling"), A.bIsFalling != B.bIsFalling ? 1.0e6 : 0.0);
		Check(TEXT("bJustLanded"), A.bJustLanded != B.bJustLanded ? 1.0e6 : 0.0);
		return Worst;
	}

	struct FSite
	{
		const TCHAR* Name;
		double Lat;
		double Lon;
	};

	const FSite Sites[] = {
		{ TEXT("north pole"), 90.0, 0.0 },
		{ TEXT("south pole"), -90.0, 0.0 },
		{ TEXT("near the south pole"), -89.999, 37.0 },
		{ TEXT("latitude 45"), 45.0, 20.0 },
		{ TEXT("equator"), 0.0, 100.0 },
		{ TEXT("cube corner"), 35.264, 45.0 },
		{ TEXT("latitude -60"), -60.0, 200.0 },
	};
}

#define VC_ANIM_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// The default gravity: speed, direction, aim and the local velocity are the expressions the anim instance used before
// P2 (Size2D, the yaw difference, NormalizedDeltaRotator); slope and time to land meet their definitions.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_PlaneValues, "VoxelCharacter.Anim.PlaneValues", VC_ANIM_FLAGS)
bool FVCAnim_PlaneValues::RunTest(const FString& Parameters)
{
	using namespace VCAnimTestHelpers;
	UVCAnimInstance* Anim = NewAnimInstance();
	FRandomStream Random(2029);

	int32 SpeedMismatches = 0;
	double WorstDirection = 0.0, WorstAim = 0.0, WorstLocal = 0.0, WorstLandResidual = 0.0;
	for (int32 i = 0; i < 500; ++i)
	{
		const double Yaw = Random.FRandRange(-180.0, 180.0);
		const FVCAnimInput In = RandomPlaneInput(Random, Yaw);
		Anim->ResetKeptState();
		Anim->ComputeValues(In);

		SpeedMismatches += Anim->Speed != float(In.Velocity.Size2D()) ? 1 : 0;
		if (In.Velocity.Size2D() > 1.0)
		{
			WorstDirection = FMath::Max(WorstDirection, AngleGap(Anim->Direction, FMath::FindDeltaAngleDegrees(Yaw, In.Velocity.Rotation().Yaw)));
		}
		const FRotator Delta = UKismetMathLibrary::NormalizedDeltaRotator(In.ControlRotation, FRotator(0.0, Yaw, 0.0));
		WorstAim = FMath::Max(WorstAim, FMath::Abs(Anim->AimPitch - FMath::ClampAngle(Delta.Pitch, -90.0, 90.0)));
		WorstAim = FMath::Max(WorstAim, AngleGap(Anim->AimYaw, FMath::ClampAngle(Delta.Yaw, -90.0, 90.0)));
		WorstLocal = FMath::Max(WorstLocal, (Anim->LocalVelocity - FRotator(0.0, Yaw, 0.0).UnrotateVector(In.Velocity)).GetAbsMax());

		if (In.bFalling && In.GroundDistance >= 0.f)
		{
			// Height along up at the landing time: v t - g t^2 / 2 = -h.
			const double T = Anim->TimeToLand;
			WorstLandResidual = FMath::Max(WorstLandResidual,
				FMath::Abs(In.Velocity.Z * T - 0.5 * In.GravityAcceleration * T * T + In.GroundDistance));
			TestTrue(TEXT("Time to land is never negative"), T >= 0.0);
		}
		else
		{
			TestEqual(TEXT("No time to land off the air or out of range"), Anim->TimeToLand, -1.f);
		}
	}
	AddInfo(FString::Printf(TEXT("Plane: speed mismatches %d of 500, direction %.2e deg, aim %.2e deg, local velocity %.2e, landing residual %.2e uu"),
		SpeedMismatches, WorstDirection, WorstAim, WorstLocal, WorstLandResidual));
	TestEqual(TEXT("Speed is Size2D bit for bit"), SpeedMismatches, 0);
	TestTrue(TEXT("Direction is the yaw difference"), WorstDirection < 1.0e-3);
	TestTrue(TEXT("Aim is NormalizedDeltaRotator, clamped"), WorstAim < 1.0e-3);
	TestTrue(TEXT("Local velocity is the actor-unrotated velocity"), WorstLocal < 1.0e-3);
	TestTrue(TEXT("Time to land solves the fall"), WorstLandResidual < 1.0e-2);

	// Slopes: a ramp rising ahead of the facing, then one rising to its right.
	for (const double Yaw : { 0.0, 73.0, -150.0 })
	{
		const FRotator Facing(0.0, Yaw, 0.0);
		FVCAnimInput In;
		In.DeltaSeconds = Dt;
		In.ActorRotation = Facing.Quaternion();
		const double A = FMath::DegreesToRadians(25.0);
		In.FloorNormal = Facing.RotateVector(FVector(-FMath::Sin(A), 0.0, FMath::Cos(A)));
		Anim->ComputeValues(In);
		TestTrue(FString::Printf(TEXT("Yaw %.0f: uphill ahead is +25 pitch"), Yaw),
			FMath::IsNearlyEqual(Anim->SlopePitch, 25.f, 1.0e-3f) && FMath::IsNearlyZero(Anim->SlopeRoll, 1.0e-3f));
		In.FloorNormal = Facing.RotateVector(FVector(0.0, -FMath::Sin(A), FMath::Cos(A)));
		Anim->ComputeValues(In);
		TestTrue(FString::Printf(TEXT("Yaw %.0f: higher on the right is +25 roll"), Yaw),
			FMath::IsNearlyEqual(Anim->SlopeRoll, 25.f, 1.0e-3f) && FMath::IsNearlyZero(Anim->SlopePitch, 1.0e-3f));
	}

	// Falling, then landing: one update of bJustLanded.
	FVCAnimInput Air;
	Air.DeltaSeconds = Dt;
	Air.bFalling = true;
	Air.GroundDistance = -1.f;
	FVCAnimInput Ground = Air;
	Ground.bFalling = false;
	Ground.GroundDistance = 0.f;
	Anim->ComputeValues(Air);
	TestTrue(TEXT("Falling"), Anim->bIsFalling && !Anim->bJustLanded);
	Anim->ComputeValues(Ground);
	TestTrue(TEXT("Just landed"), !Anim->bIsFalling && Anim->bJustLanded);
	Anim->ComputeValues(Ground);
	TestFalse(TEXT("Just landed lasts one update"), Anim->bJustLanded);
	return true;
}

// ---------------------------------------------------------------------------
// A planet: the same motion turned into the local frame at each site, and turned again about local up by a random
// twist (the frame's own yaw is no reference), gives the plane's values at every update of a sequence.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_PlanetFrame, "VoxelCharacter.Anim.PlanetFrame", VC_ANIM_FLAGS)
bool FVCAnim_PlanetFrame::RunTest(const FString& Parameters)
{
	using namespace VCAnimTestHelpers;
	UVCAnimInstance* Plane = NewAnimInstance();
	UVCAnimInstance* Planet = NewAnimInstance();
	FRandomStream Random(77);

	for (const FSite& Site : Sites)
	{
		const FVector Up = LatLon(Site.Lat, Site.Lon);
		double WorstSite = 0.0;
		FString WorstWhich = TEXT("none");
		for (int32 Twist = 0; Twist < 3; ++Twist)
		{
			const FQuat Q = FQuat(Up, FMath::DegreesToRadians(Random.FRandRange(-180.0, 180.0)))
				* FQuat::FindBetweenNormals(FVector::UpVector, Up);
			Plane->ResetKeptState();
			Planet->ResetKeptState();

			// A walk of 120 updates: turning, standing still for stretches (turn in place), falling now and then.
			double Yaw = Random.FRandRange(-180.0, 180.0);
			for (int32 Step = 0; Step < 120; ++Step)
			{
				Yaw += Random.FRandRange(-4.0, 4.0);
				FVCAnimInput In = RandomPlaneInput(Random, Yaw);
				if ((Step / 20) % 2 == 1)
				{
					In.Velocity = FVector::ZeroVector;
					In.bFalling = false;
					In.bOnGround = true;
					In.GroundDistance = 0.f;
				}
				Plane->ComputeValues(In);
				Planet->ComputeValues(Rotated(In, Q));
				FString Which;
				const double Difference = WorstDifference(*Plane, *Planet, Which);
				if (Difference > WorstSite)
				{
					WorstSite = Difference;
					WorstWhich = Which;
				}
			}
		}
		AddInfo(FString::Printf(TEXT("%s: worst difference from the plane %.2e (%s)"), Site.Name, WorstSite, *WorstWhich));
		TestTrue(FString::Printf(TEXT("%s: the plane's values in the local frame"), Site.Name), WorstSite < 1.0e-3);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Turn in place: the body keeps its heading while the facing turns and the character stands, up to the clamp; it
// follows once the character moves; circling the south pole (where the frame's yaw spins twice per lap) leaves both
// the offset and the yaw rate alone.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_TurnInPlace, "VoxelCharacter.Anim.TurnInPlace", VC_ANIM_FLAGS)
bool FVCAnim_TurnInPlace::RunTest(const FString& Parameters)
{
	using namespace VCAnimTestHelpers;
	UVCAnimInstance* Anim = NewAnimInstance();

	FVCAnimInput In;
	In.DeltaSeconds = Dt;
	In.bOnGround = true;
	const auto Face = [&In](double Yaw) { In.ActorRotation = FRotator(0.0, Yaw, 0.0).Quaternion(); };

	// Standing, the facing turns right one degree an update: the body stays behind.
	double Yaw = 0.0;
	Face(Yaw);
	Anim->ComputeValues(In);
	for (int32 i = 0; i < 60; ++i)
	{
		Face(++Yaw);
		Anim->ComputeValues(In);
	}
	TestTrue(TEXT("Standing: the body keeps its heading (-60)"), FMath::IsNearlyEqual(Anim->RootYawOffset, -60.f, 1.0e-3f));
	TestTrue(TEXT("Yaw rate 60 deg/s"), FMath::IsNearlyEqual(Anim->YawRate, 60.f, 1.0e-2f));

	// Past the clamp the body is dragged round; turning back leaves it where it was dragged to.
	for (int32 i = 0; i < 60; ++i)
	{
		Face(++Yaw);
		Anim->ComputeValues(In);
	}
	TestTrue(TEXT("Clamped at -TurnInPlaceMaxOffset"), FMath::IsNearlyEqual(Anim->RootYawOffset, -Anim->TurnInPlaceMaxOffset, 1.0e-3f));
	for (int32 i = 0; i < 10; ++i)
	{
		Face(--Yaw);
		Anim->ComputeValues(In);
	}
	TestTrue(TEXT("Turning back: -80"), FMath::IsNearlyEqual(Anim->RootYawOffset, -80.f, 1.0e-3f));

	// Moving: the offset returns to zero within a second.
	In.Velocity = FRotator(0.0, Yaw, 0.0).Vector() * 300.0;
	for (int32 i = 0; i < 60; ++i)
	{
		Anim->ComputeValues(In);
	}
	AddInfo(FString::Printf(TEXT("After a second of walking the offset is %.4f"), Anim->RootYawOffset));
	TestTrue(TEXT("Moving: the offset returns to zero"), FMath::Abs(Anim->RootYawOffset) < 0.01f);

	// Held off the ground (MOVE_None during the terrain wait, where a restored save turns the pawn): the body follows
	// the facing, a little behind while it turns, and settles on it.
	In.Velocity = FVector::ZeroVector;
	In.bOnGround = false;
	double WorstHeld = 0.0;
	for (int32 i = 0; i < 30; ++i)
	{
		Face(Yaw += 3.0);
		Anim->ComputeValues(In);
		WorstHeld = FMath::Max(WorstHeld, double(FMath::Abs(Anim->RootYawOffset)));
	}
	for (int32 i = 0; i < 60; ++i)
	{
		Anim->ComputeValues(In);
	}
	AddInfo(FString::Printf(TEXT("Held and turning 3 degrees an update: the body lags up to %.1f degrees"), WorstHeld));
	TestTrue(TEXT("Held: no turn in place builds"), WorstHeld < 20.0);
	TestTrue(TEXT("Held: the body settles on the facing"), FMath::Abs(Anim->RootYawOffset) < 0.01f);

	// On the ground, a rotation set by code: a 120-degree turn in one update seats the body instead of dragging it.
	In.bOnGround = true;
	Anim->ComputeValues(In);
	Face(Yaw += 120.0);
	Anim->ComputeValues(In);
	TestTrue(TEXT("A one-update turn past the clamp seats the body"), FMath::IsNearlyZero(Anim->RootYawOffset, 1.0e-3f));

	// The south-pole twist: half a degree from the pole (nearer, FindBetweenNormals treats up as exactly opposite +Z and
	// the frame stops turning), the facing fixed in the world, the body 40 degrees from it; two laps round the pole in
	// 10-degree steps. The body is carried along the lap as a heading in each tangent plane, so it turns against the
	// world-fixed facing by the solid angle the lap encloses (about 0.014 degrees a lap here): drift, not a twist.
	Anim->ResetKeptState();
	FVCAnimInput Pole;
	Pole.DeltaSeconds = Dt;
	Pole.bOnGround = true;
	const FVector WorldHeading = FVector::ForwardVector;
	const auto StandAt = [&Pole, &WorldHeading](const FVector& Up, double TurnDegrees)
	{
		Pole.GravityDirection = -Up;
		const FVector Heading = FQuat(Up, FMath::DegreesToRadians(TurnDegrees)).RotateVector(WorldHeading - (WorldHeading | Up) * Up);
		Pole.ActorRotation = FRotationMatrix::MakeFromZX(Up, Heading).ToQuat();
	};
	StandAt(LatLon(-89.5, 0.0), 40.0);
	Anim->ComputeValues(Pole);
	StandAt(LatLon(-89.5, 0.0), 0.0);
	Anim->ComputeValues(Pole);
	TestTrue(TEXT("Pole: the body 40 degrees right of the facing"), FMath::IsNearlyEqual(Anim->RootYawOffset, 40.f, 1.0e-3f));

	double WorstOffset = 0.0, WorstYawRate = 0.0, WorstFrameYawStep = 0.0;
	double LastFrameYaw = FVCGravityFrame::ToGravityRelative(Pole.GravityDirection, Pole.ActorRotation.Rotator()).Yaw;
	for (int32 Step = 1; Step <= 72; ++Step)
	{
		StandAt(LatLon(-89.5, 10.0 * Step), 0.0);
		Anim->ComputeValues(Pole);
		WorstOffset = FMath::Max(WorstOffset, FMath::Abs(Anim->RootYawOffset - 40.0));
		WorstYawRate = FMath::Max(WorstYawRate, FMath::Abs(Anim->YawRate));
		// What a yaw kept in the gravity frame would see for the same, unmoving facing.
		const double FrameYaw = FVCGravityFrame::ToGravityRelative(Pole.GravityDirection, Pole.ActorRotation.Rotator()).Yaw;
		WorstFrameYawStep = FMath::Max(WorstFrameYawStep, AngleGap(FrameYaw, LastFrameYaw));
		LastFrameYaw = FrameYaw;
	}
	AddInfo(FString::Printf(TEXT("South pole: gravity-frame yaw steps up to %.1f deg; offset drift %.2e deg, yaw rate %.2e deg/s"),
		WorstFrameYawStep, WorstOffset, WorstYawRate));
	TestTrue(TEXT("The pole twists the gravity frame's yaw (the case is exercised)"), WorstFrameYawStep > 10.0);
	TestTrue(TEXT("The offset holds across the twist"), WorstOffset < 0.1);
	TestTrue(TEXT("No yaw rate from the twist"), WorstYawRate < 0.1);
	return true;
}

// ---------------------------------------------------------------------------
// The action set: what the main hand holds picks it; the anim instance and an NPC default to Unarmed.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_ActionSet, "VoxelCharacter.Anim.ActionSet", VC_ANIM_FLAGS)
bool FVCAnim_ActionSet::RunTest(const FString& Parameters)
{
	using namespace VCAnimTestHelpers;
	const FGameplayTag Unarmed = VCGameplayTags::Equipment_AnimSet_Unarmed;
	const FGameplayTag Tool = VCGameplayTags::Equipment_AnimSet_Tool;
	const FGameplayTag SwordShield = VCGameplayTags::Equipment_AnimSet_SwordShield;

	TestEqual(TEXT("Nothing held: Unarmed"), AVCCharacterBase::PickActionSet(false, SwordShield), Unarmed);
	TestEqual(TEXT("An item without a set: Tool"), AVCCharacterBase::PickActionSet(true, FGameplayTag()), Tool);
	TestEqual(TEXT("An item's own set"), AVCCharacterBase::PickActionSet(true, SwordShield), SwordShield);
	TestEqual(TEXT("The root is no set: Tool"), AVCCharacterBase::PickActionSet(true, VCGameplayTags::Equipment_AnimSet), Tool);
	TestEqual(TEXT("A tag outside Equipment.AnimSet: Tool"),
		AVCCharacterBase::PickActionSet(true, VCGameplayTags::SetByCaller_Stat_MaxHealth), Tool);

	UVCAnimInstance* Anim = NewAnimInstance();
	FVCAnimInput In;
	In.DeltaSeconds = Dt;
	Anim->ComputeValues(In);
	TestEqual(TEXT("Anim instance: no set given is Unarmed"), Anim->ActionSet, Unarmed);
	In.ActionSet = SwordShield;
	Anim->ComputeValues(In);
	TestEqual(TEXT("Anim instance: the character's set"), Anim->ActionSet, SwordShield);

	const AVCNPCCharacterBase* NPC = GetDefault<AVCNPCCharacterBase>();
	TestEqual(TEXT("NPC: Unarmed by default"), NPC->GetAnimActionSet(), Unarmed);
	TestEqual(TEXT("NPC: third person"), NPC->GetAnimViewMode(), EVCViewMode::ThirdPerson);
	TestEqual(TEXT("Player not locally controlled: third person"), GetDefault<AVCCharacterBase>()->GetAnimViewMode(), EVCViewMode::ThirdPerson);
	return true;
}

// ---------------------------------------------------------------------------
// The gather: an NPC in a world, its gravity tilted (the south pole, 45 degrees, the plane), falling toward a slab
// 150 uu below its feet. The anim instance reads its velocity in the local frame and finds the slab along gravity.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_GatherFromCharacter, "VoxelCharacter.Anim.GatherFromCharacter", VC_ANIM_FLAGS)
bool FVCAnim_GatherFromCharacter::RunTest(const FString& Parameters)
{
	using namespace VCAnimTestHelpers;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("VCAnimGatherWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());

	const FSite GatherSites[] = { { TEXT("plane"), 90.0, 0.0 }, { TEXT("south pole"), -90.0, 0.0 }, { TEXT("latitude 45"), 45.0, 20.0 } };
	TArray<AActor*> Spawned;
	int32 Index = 0;
	for (const FSite& Site : GatherSites)
	{
		const FVector Up = LatLon(Site.Lat, Site.Lon);
		const FVector Heading = FQuat::FindBetweenNormals(FVector::UpVector, Up).RotateVector(FVector::RightVector);
		const FQuat Upright = FRotationMatrix::MakeFromZX(Up, Heading).ToQuat();
		const FVector Location = FVector(20000.0 * Index++, 0.0, 0.0) + Up * 5000.0;

		const FTransform Transform(Upright, Location);
		AVCNPCCharacterBase* NPC = World->SpawnActorDeferred<AVCNPCCharacterBase>(AVCNPCCharacterBase::StaticClass(), Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!TestNotNull(*FString::Printf(TEXT("%s: NPC spawned"), Site.Name), NPC))
		{
			continue;
		}
		NPC->FinishSpawning(Transform);
		Spawned.Add(NPC);
		const float HalfHeight = NPC->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();

		// The slab: its top 150 uu below the feet, square to local up.
		AActor* Slab = World->SpawnActor<AActor>();
		Spawned.Add(Slab);
		UBoxComponent* Box = NewObject<UBoxComponent>(Slab);
		Box->SetBoxExtent(FVector(500.0, 500.0, 50.0));
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Slab->SetRootComponent(Box);
		Box->RegisterComponent();
		Box->SetWorldLocationAndRotation(Location - Up * (HalfHeight + 150.0 + 50.0), Upright);

		UCharacterMovementComponent* Move = NPC->GetCharacterMovement();
		Move->SetGravityDirection(-Up);
		Move->SetMovementMode(MOVE_Falling);
		Move->Velocity = Heading * 300.0 - Up * 200.0;

		UVCAnimInstance* Anim = NewObject<UVCAnimInstance>(NPC->GetMesh());
		Anim->NativeUpdateAnimation(Dt);
		Anim->NativeThreadSafeUpdateAnimation(Dt);

		const double Gravity = FMath::Abs(Move->GetGravityZ());
		const double Expected = (-200.0 + FMath::Sqrt(200.0 * 200.0 + 2.0 * Gravity * 150.0)) / Gravity;
		AddInfo(FString::Printf(TEXT("%s: speed %.2f, direction %.3f, local velocity %s, ground %.2f, time to land %.4f (expected %.4f)"),
			Site.Name, Anim->Speed, Anim->Direction, *Anim->LocalVelocity.ToString(), Anim->GroundDistance, Anim->TimeToLand, Expected));
		TestTrue(FString::Printf(TEXT("%s: speed across local up"), Site.Name), FMath::IsNearlyEqual(Anim->Speed, 300.f, 1.0e-2f));
		TestTrue(FString::Printf(TEXT("%s: moving straight ahead"), Site.Name), FMath::IsNearlyZero(Anim->Direction, 1.0e-2f));
		TestTrue(FString::Printf(TEXT("%s: local velocity"), Site.Name), Anim->LocalVelocity.Equals(FVector(300.0, 0.0, -200.0), 1.0e-2));
		TestTrue(FString::Printf(TEXT("%s: falling"), Site.Name), Anim->bIsFalling);
		TestTrue(FString::Printf(TEXT("%s: the slab 150 uu below along gravity"), Site.Name), FMath::IsNearlyEqual(Anim->GroundDistance, 150.f, 1.f));
		TestTrue(FString::Printf(TEXT("%s: time to land"), Site.Name), FMath::IsNearlyEqual(double(Anim->TimeToLand), Expected, 1.0e-2));
		TestEqual(FString::Printf(TEXT("%s: Unarmed"), Site.Name), Anim->ActionSet, VCGameplayTags::Equipment_AnimSet_Unarmed.GetTag());
		TestFalse(FString::Printf(TEXT("%s: alive"), Site.Name), Anim->bIsDead || Anim->bIsDowned);

		// Out of range: nothing below.
		Anim->MaxGroundTraceDistance = 100.f;
		Anim->NativeUpdateAnimation(Dt);
		Anim->NativeThreadSafeUpdateAnimation(Dt);
		TestTrue(FString::Printf(TEXT("%s: out of range is -1"), Site.Name), Anim->GroundDistance == -1.f && Anim->TimeToLand == -1.f);

		// Walking: on the ground.
		Move->SetMovementMode(MOVE_Walking);
		Anim->NativeUpdateAnimation(Dt);
		Anim->NativeThreadSafeUpdateAnimation(Dt);
		TestTrue(FString::Printf(TEXT("%s: walking is on the ground"), Site.Name),
			!Anim->bIsFalling && Anim->bJustLanded && Anim->GroundDistance == 0.f && Anim->TimeToLand == -1.f);
	}

	for (AActor* Actor : Spawned)
	{
		World->DestroyActor(Actor);
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

// ---------------------------------------------------------------------------
// IK and pace (character animation P4): foot IK on the ground only, quicker steps above the authored top speed, the head looking at a target ahead (not behind, not in first person), the off hand's IK with a two-handed set;
// every switch off turns its alpha to 0. The look-at's ahead-or-behind is measured about local up, so a tilted frame
// (a planet's) gives the same answer.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_IKValues, "VoxelCharacter.Anim.IKValues", VC_ANIM_FLAGS)
bool FVCAnim_IKValues::RunTest(const FString& Parameters)
{
	using namespace VCAnimTestHelpers;
	const auto Settle = [](UVCAnimInstance* Anim, const FVCAnimInput& In)
	{
		for (int32 Update = 0; Update < 60; ++Update)
		{
			Anim->ComputeValues(In);
		}
	};
	FVCAnimInput Ground;
	Ground.DeltaSeconds = Dt;
	Ground.bOnGround = true;
	Ground.EyeLocation = FVector(0, 0, 160);
	Ground.LookTarget = FVector(1000, 0, 160);
	Ground.bHasLookTarget = true;

	UVCAnimInstance* Anim = NewAnimInstance();
	Settle(Anim, Ground);
	TestEqual(TEXT("On the ground: foot IK on"), Anim->FootIKAlpha, 1.f);
	TestEqual(TEXT("A body with ik_foot bones: no foot-bone placement"), Anim->FootBoneIKAlpha, 0.f);
	FVCAnimInput FootBones = Ground;
	FootBones.bFootBonesOnly = true;
	Settle(Anim, FootBones);
	TestEqual(TEXT("A body without them (a MetaHuman's): the foot-bone placement at the foot IK's weight"), Anim->FootBoneIKAlpha, 1.f);
	Settle(Anim, Ground);
	TestEqual(TEXT("A target ahead: the head looks"), Anim->LookAtAlpha, 1.f);
	TestEqual(TEXT("The look-at's location is the target"), Anim->LookAtLocation, Ground.LookTarget);
	TestEqual(TEXT("No two-handed set: no hand IK"), Anim->HandIKAlpha, 0.f);
	Anim->ComputeValues([&] { FVCAnimInput In = Ground; In.bFalling = true; In.bOnGround = false; return In; }());
	TestTrue(TEXT("The foot IK eases off, not at once"), Anim->FootIKAlpha > 0.f && Anim->FootIKAlpha < 1.f);

	for (const TPair<const TCHAR*, TFunction<void(FVCAnimInput&)>>& Case : TArray<TPair<const TCHAR*, TFunction<void(FVCAnimInput&)>>>{
		{ TEXT("falling"), [](FVCAnimInput& In) { In.bOnGround = false; In.bFalling = true; } },
		{ TEXT("swimming"), [](FVCAnimInput& In) { In.bOnGround = false; In.bSwimming = true; } },
		{ TEXT("flying"), [](FVCAnimInput& In) { In.bOnGround = false; In.bFlying = true; } },
		{ TEXT("dead"), [](FVCAnimInput& In) { In.bDead = true; } },
		{ TEXT("vc.Anim.FootIK 0"), [](FVCAnimInput& In) { In.bFootIKEnabled = false; } } })
	{
		FVCAnimInput In = Ground;
		Case.Value(In);
		Settle(Anim, In);
		TestEqual(*FString::Printf(TEXT("Foot IK off %s"), Case.Key), Anim->FootIKAlpha, 0.f);
	}

	// The pace: as authored up to the reference speed, quicker steps above it, so the steps keep up with the ground.
	TestEqual(TEXT("A jog: the clips' own pace"), UVCAnimInstance::ComputeLocomotionPlayRate(600.f, 600.f), 1.f);
	TestEqual(TEXT("A walk: the clips' own pace"), UVCAnimInstance::ComputeLocomotionPlayRate(300.f, 600.f), 1.f);
	TestEqual(TEXT("A sprint: 1.6 times quicker"), UVCAnimInstance::ComputeLocomotionPlayRate(960.f, 600.f), 1.6f, 1e-4f);
	TestEqual(TEXT("No reference: 1"), UVCAnimInstance::ComputeLocomotionPlayRate(960.f, 0.f), 1.f);
	FVCAnimInput Sprint = Ground;
	Sprint.Velocity = FVector(960, 0, 0);
	Anim->ComputeValues(Sprint);
	TestEqual(TEXT("A sprinting pawn's pace"), Anim->LocomotionPlayRate, 960.f / Anim->LocomotionReferenceSpeed, 1e-4f);
	Sprint.bSprintPaceEnabled = false;
	Anim->ComputeValues(Sprint);
	TestEqual(TEXT("vc.Anim.SprintPace 0: the clips' own pace"), Anim->LocomotionPlayRate, 1.f);
	Sprint.bSprintPaceEnabled = true;
	Sprint.bOnGround = false;
	Sprint.bFalling = true;
	Anim->ComputeValues(Sprint);
	TestEqual(TEXT("In the air: the clips' own pace"), Anim->LocomotionPlayRate, 1.f);

	// The look-at: off for a target behind, in first person, without a target, or switched off.
	for (const TPair<const TCHAR*, TFunction<void(FVCAnimInput&)>>& Case : TArray<TPair<const TCHAR*, TFunction<void(FVCAnimInput&)>>>{
		{ TEXT("a target behind"), [](FVCAnimInput& In) { In.LookTarget = FVector(-1000, 50, 160); } },
		{ TEXT("first person"), [](FVCAnimInput& In) { In.ViewMode = EVCViewMode::FirstPerson; } },
		{ TEXT("no target"), [](FVCAnimInput& In) { In.bHasLookTarget = false; } },
		{ TEXT("vc.Anim.LookAt 0"), [](FVCAnimInput& In) { In.bLookAtEnabled = false; } } })
	{
		Settle(Anim, Ground);
		FVCAnimInput In = Ground;
		Case.Value(In);
		Settle(Anim, In);
		TestEqual(*FString::Printf(TEXT("Look-at off with %s"), Case.Key), Anim->LookAtAlpha, 0.f);
	}
	FVCAnimInput Side = Ground;
	Side.LookTarget = FVector(0, 1000, 160);
	Settle(Anim, Side);
	TestEqual(TEXT("A target 90 degrees aside: the head looks"), Anim->LookAtAlpha, 1.f);

	// The same targets in a tilted frame (gravity along -X, at 45 degrees round local up).
	const FQuat Tilt = FQuat(FVector::YAxisVector, FMath::DegreesToRadians(90.0)) * FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(45.0));
	FVCAnimInput Planet = Rotated(Ground, Tilt);
	Planet.bOnGround = true;
	Planet.EyeLocation = Tilt.RotateVector(Ground.EyeLocation);
	Planet.LookTarget = Tilt.RotateVector(Ground.LookTarget);
	Settle(Anim, Planet);
	TestEqual(TEXT("Tilted: a target ahead is looked at"), Anim->LookAtAlpha, 1.f);
	Planet.LookTarget = Tilt.RotateVector(FVector(-1000, 50, 160));
	Settle(Anim, Planet);
	TestEqual(TEXT("Tilted: a target behind is not"), Anim->LookAtAlpha, 0.f);

	// The off hand: on with a two-handed set, its grip the effector.
	FVCAnimInput TwoHands = Ground;
	TwoHands.bOffHandIK = true;
	TwoHands.OffHandGrip = FVector(-3, 4, 25);
	Settle(Anim, TwoHands);
	TestEqual(TEXT("Two hands: the hand IK on"), Anim->HandIKAlpha, 1.f);
	TestEqual(TEXT("The grip is the effector"), Anim->HandIKEffector, TwoHands.OffHandGrip);
	TwoHands.bHandIKEnabled = false;
	Settle(Anim, TwoHands);
	TestEqual(TEXT("vc.Anim.HandIK 0: off"), Anim->HandIKAlpha, 0.f);
	return true;
}

// ---------------------------------------------------------------------------
// The aiming stance layer (ranged combat plan B4): it plays the body's montages in its own slots, and reads the body's
// aim and its upper-body slot's montage weight; with no body it reads nothing.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_LayerInstance, "VoxelCharacter.Anim.LayerInstance", VC_ANIM_FLAGS)
bool FVCAnim_LayerInstance::RunTest(const FString& Parameters)
{
	using namespace VCAnimTestHelpers;
	TestTrue(TEXT("The layer's slots play the body's montages"),
		GetDefault<UVCAnimLayerInstance>()->IsUsingMainInstanceMontageEvaluationData());
	TestEqual(TEXT("The upper-body slot"), GetDefault<UVCAnimLayerInstance>()->UpperBodySlot, FName(TEXT("UpperBody")));

	UVCAnimInstance* Body = NewAnimInstance();
	Body->AimPitch = 25.f;
	Body->AimYaw = -40.f;
	UVCAnimLayerInstance* Layer = NewObject<UVCAnimLayerInstance>(Body->GetOuter());
	Layer->CopyBodyValues(Body);
	TestEqual(TEXT("The body's pitch"), Layer->AimPitch, 25.f);
	TestEqual(TEXT("The body's yaw"), Layer->AimYaw, -40.f);
	TestEqual(TEXT("No montage playing: no weight"), Layer->UpperBodyMontageWeight, 0.f);

	Layer->CopyBodyValues(nullptr);
	TestEqual(TEXT("No body: pitch 0"), Layer->AimPitch, 0.f);
	TestEqual(TEXT("No body: yaw 0"), Layer->AimYaw, 0.f);
	return true;
}

// ---------------------------------------------------------------------------
// The held stance's weight (character refinement plan C2, D72): 1 on the ground, StanceSprintWeight at a sprint, 0 in the
// air, in the water, dead, downed or with vc.Anim.Stance 0; it eases at IKBlendSpeed and never snaps.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_StanceWeight, "VoxelCharacter.Anim.StanceWeight", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVCAnim_StanceWeight::RunTest(const FString& Parameters)
{
	UVCAnimInstance* Anim = VCAnimTestHelpers::NewAnimInstance();
	Anim->AddToRoot();
	const float Dt = 1.f / 60.f;
	FVCAnimInput In;
	In.DeltaSeconds = Dt;
	In.bOnGround = true;
	In.StanceMode = 2;

	Anim->ComputeValues(In);
	TestTrue(TEXT("One update eases, it does not snap"), Anim->StanceWeight > 0.f && Anim->StanceWeight < 1.f);
	TestTrue(TEXT("The step is IKBlendSpeed x dt"), FMath::IsNearlyEqual(Anim->StanceWeight, Anim->IKBlendSpeed * Dt, 1.0e-4f));
	for (int32 i = 0; i < 60; ++i) { Anim->ComputeValues(In); }
	TestTrue(TEXT("On the ground: 1"), FMath::IsNearlyEqual(Anim->StanceWeight, 1.f));
	TestEqual(TEXT("The mode is kept for the layer"), Anim->StanceMode, 2);

	In.bSprinting = true;
	for (int32 i = 0; i < 60; ++i) { Anim->ComputeValues(In); }
	TestTrue(TEXT("Sprinting: the sprint weight"), FMath::IsNearlyEqual(Anim->StanceWeight, Anim->StanceSprintWeight));
	In.bSprinting = false;

	FVCAnimInput Air = In;
	Air.bOnGround = false;
	Air.bFalling = true;
	for (int32 i = 0; i < 60; ++i) { Anim->ComputeValues(Air); }
	TestTrue(TEXT("In the air: 0"), FMath::IsNearlyZero(Anim->StanceWeight));

	for (int32 i = 0; i < 60; ++i) { Anim->ComputeValues(In); }
	FVCAnimInput Swim = In;
	Swim.bSwimming = true;
	for (int32 i = 0; i < 60; ++i) { Anim->ComputeValues(Swim); }
	TestTrue(TEXT("Swimming: 0"), FMath::IsNearlyZero(Anim->StanceWeight));

	for (int32 i = 0; i < 60; ++i) { Anim->ComputeValues(In); }
	FVCAnimInput Downed = In;
	Downed.bDowned = true;
	for (int32 i = 0; i < 60; ++i) { Anim->ComputeValues(Downed); }
	TestTrue(TEXT("Downed: 0"), FMath::IsNearlyZero(Anim->StanceWeight));

	for (int32 i = 0; i < 60; ++i) { Anim->ComputeValues(In); }
	FVCAnimInput Off = In;
	Off.StanceMode = 0;
	for (int32 i = 0; i < 60; ++i) { Anim->ComputeValues(Off); }
	TestTrue(TEXT("vc.Anim.Stance 0: 0"), FMath::IsNearlyZero(Anim->StanceWeight));
	TestEqual(TEXT("The mode 0 is kept"), Anim->StanceMode, 0);

	Anim->RemoveFromRoot();
	return true;
}

#endif // WITH_AUTOMATION_TESTS
