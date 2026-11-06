// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Core/VCAnimInstance.h"
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

#endif // WITH_AUTOMATION_TESTS
