// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Testing/VCTestWorld.h"

#if WITH_AUTOMATION_TESTS

#include "Combat/VCArrowProjectile.h"
#include "Combat/VCArrowMovementComponent.h"
#include "Combat/VCCombatComponent.h"
#include "Core/VCNPCCharacterBase.h"
#include "Tags/CGFGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"

// ---------------------------------------------------------------------------
// The arrow (ranged combat plan B2): its gravity on a plane and on a planet, its hit on a pawn, sticking, and the shot's
// damage through the combat pipeline. The flights tick the arrow's movement by hand in a play world with no voxel world.
// ---------------------------------------------------------------------------
namespace VCArrowTestHelpers
{
	/** One substep of the arrow's movement (its MaxSimulationTimeStep). */
	constexpr float ArrowStep = 1.f / 60.f;

	struct FArrowWorld : VCTesting::FPlayWorld
	{
		FArrowWorld() : VCTesting::FPlayWorld(TEXT("VCArrowTestWorld"), /*bDestroyActorsFirst=*/false) {}
	};

	/** An NPC with 100 health and no attack power or defence, so a hit's damage is the shot's. */
	AVCNPCCharacterBase* SpawnArrowTarget(UWorld* World, FGameplayTag Faction, const FVector& Location)
	{
		const FTransform Transform(Location);
		AVCNPCCharacterBase* NPC = World->SpawnActorDeferred<AVCNPCCharacterBase>(AVCNPCCharacterBase::StaticClass(), Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!NPC)
		{
			return nullptr;
		}
		NPC->StartingMaxHealth = 100.f;
		NPC->StartingAttackPower = 0.f;
		NPC->StartingDefense = 0.f;
		NPC->DestroyAfterDeathDelay = 0.f;
		NPC->CombatComponent->FactionTag = Faction;
		NPC->FinishSpawning(Transform);
		return NPC;
	}

	AVCArrowProjectile* SpawnArrow(UWorld* World, APawn* Shooter, const FVector& Location)
	{
		const FTransform Transform(Location);
		AVCArrowProjectile* Arrow = World->SpawnActorDeferred<AVCArrowProjectile>(AVCArrowProjectile::StaticClass(), Transform,
			Shooter, Shooter, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Arrow)
		{
			Arrow->FinishSpawning(Transform);
			// The flights measure gravity and hits: the test world's weather would push them aside (WindRule has the wind).
			Arrow->GetArrowMovement()->WindResponse = 0.f;
		}
		return Arrow;
	}

	FVCArrowShot MakeArrowShot(float FullDamage, float DrawPower)
	{
		FVCArrowShot Shot;
		Shot.FullDamage = FullDamage;
		Shot.DrawPower = DrawPower;
		return Shot;
	}

	/** True while the arrow exists and flies. */
	bool IsArrowFlying(const AVCArrowProjectile* Arrow)
	{
		return IsValid(Arrow) && Arrow->GetArrowMovement()->UpdatedComponent != nullptr;
	}

	/** Ticks the flight up to Steps substeps, stopping when the arrow stops or goes. @return The substeps flown */
	int32 FlyArrow(AVCArrowProjectile* Arrow, int32 Steps)
	{
		int32 Flown = 0;
		for (; Flown < Steps && IsArrowFlying(Arrow); ++Flown)
		{
			Arrow->GetArrowMovement()->TickComponent(ArrowStep, LEVELTICK_All, nullptr);
		}
		return Flown;
	}

	/** A planet's bounds: a shell around Centre. */
	FVoxelWorldBounds ArrowPlanetBounds(const FVector& Centre, float Radius)
	{
		FVoxelWorldBounds Bounds;
		Bounds.Shape = EVoxelWorldBoundsShape::Shell;
		Bounds.Center = Centre;
		Bounds.InnerRadius = Radius * 0.9f;
		Bounds.OuterRadius = Radius * 1.1f;
		return Bounds;
	}

	/** A wall: a box blocking everything, static or movable. */
	AActor* SpawnArrowWall(UWorld* World, const FVector& Location, const FVector& Extent, EComponentMobility::Type Mobility)
	{
		AActor* Wall = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location));
		if (!Wall)
		{
			return nullptr;
		}
		UBoxComponent* Box = NewObject<UBoxComponent>(Wall, TEXT("Box"));
		Box->SetMobility(Mobility);
		Box->SetBoxExtent(Extent);
		Box->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Wall->SetRootComponent(Box);
		Box->SetWorldLocation(Location);
		Box->RegisterComponent();
		return Wall;
	}
}

using namespace VCArrowTestHelpers;

#define VC_ARROW_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// ===========================================================================
// Pure rules
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCArrow_GravityRule, "VoxelCharacter.Combat.Arrow.GravityRule", VC_ARROW_FLAGS)
bool FVCArrow_GravityRule::RunTest(const FString& Parameters)
{
	const float G = -980.f;
	const FVoxelWorldBounds Plane;
	TestEqual(TEXT("A plane: the engine's -Z"), UVCArrowMovementComponent::ComputeGravityAcceleration(Plane, FVector(5000, -300, 900), G), FVector(0, 0, G));

	const FVoxelWorldBounds Planet = ArrowPlanetBounds(FVector(100, 200, 300), 250000.f);
	const FVector C = Planet.Center;
	TestTrue(TEXT("On the planet's +X side: toward -X"), UVCArrowMovementComponent::ComputeGravityAcceleration(Planet, C + FVector(250000, 0, 0), G).Equals(FVector(-980, 0, 0), 1e-3));
	TestTrue(TEXT("Under its south pole: toward +Z"), UVCArrowMovementComponent::ComputeGravityAcceleration(Planet, C + FVector(0, 0, -250000), G).Equals(FVector(0, 0, 980), 1e-3));
	TestTrue(TEXT("Anywhere: the pull's size is the engine's"), FMath::IsNearlyEqual(UVCArrowMovementComponent::ComputeGravityAcceleration(Planet, C + FVector(123456, -98765, 54321), G).Size(), 980.0, 1e-3));
	TestEqual(TEXT("At the centre itself: the engine's -Z"), UVCArrowMovementComponent::ComputeGravityAcceleration(Planet, C, G), FVector(0, 0, G));

	const FVector Up(0, 0, 1);
	TestTrue(TEXT("Below a surface point"), UVCArrowMovementComponent::IsBelowSurface(FVector(10, 10, 99), FVector(0, 0, 100), Up));
	TestFalse(TEXT("Above it"), UVCArrowMovementComponent::IsBelowSurface(FVector(10, 10, 101), FVector(0, 0, 100), Up));
	TestTrue(TEXT("Below a planet's surface, along its up"), UVCArrowMovementComponent::IsBelowSurface(FVector(-249000, 0, 0), FVector(-250000, 0, 0), FVector(-1, 0, 0)));

	const FVCArrowShot Half = MakeArrowShot(20.f, 0.5f);
	TestEqual(TEXT("A shot's damage scales by the draw"), Half.GetDamage(), 10.f);
	TestEqual(TEXT("A full draw: the bow's damage"), MakeArrowShot(20.f, 1.f).GetDamage(), 20.f);
	return true;
}

// The wind on an arrow (D83): across the launch heading in the local horizontal, the arrow's speed takes the cross wind's
// at WindResponse a second; head and tail winds and the vertical are left alone. A full draw over 30 m in the weather's
// strong wind drifts about 28 uu, and the drift never outruns the wind.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCArrow_WindRule, "VoxelCharacter.Combat.Arrow.WindRule", VC_ARROW_FLAGS)
bool FVCArrow_WindRule::RunTest(const FString& Parameters)
{
	const FVector V(6000, 0, 0);
	const FVector Heading(1, 0, 0);
	const FVector Up(0, 0, 1);
	const FVector Cross(0, 600, 0);
	const float K = GetDefault<UVCArrowMovementComponent>()->WindResponse;
	TestTrue(TEXT("Still air: no push"), UVCArrowMovementComponent::ComputeWindAcceleration(V, FVector::ZeroVector, Heading, Up, K).IsNearlyZero(1e-6));
	TestTrue(TEXT("A tail wind: no push"), UVCArrowMovementComponent::ComputeWindAcceleration(V, FVector(600, 0, 0), Heading, Up, K).IsNearlyZero(1e-6));
	TestTrue(TEXT("A cross wind: the response times the wind, across"),
		UVCArrowMovementComponent::ComputeWindAcceleration(V, Cross, Heading, Up, K).Equals(FVector(0, 600.0 * K, 0), 1e-3));
	TestTrue(TEXT("Falling: the drop is gravity's alone"),
		UVCArrowMovementComponent::ComputeWindAcceleration(FVector(6000, 0, -2000), FVector::ZeroVector, Heading, Up, K).IsNearlyZero(1e-6));
	TestTrue(TEXT("No response: no push"), UVCArrowMovementComponent::ComputeWindAcceleration(V, Cross, Heading, Up, 0.f).IsZero());
	TestTrue(TEXT("No heading yet: no push"), UVCArrowMovementComponent::ComputeWindAcceleration(V, Cross, FVector::ZeroVector, Up, K).IsZero());
	TestTrue(TEXT("Moving across with the wind: no push"),
		UVCArrowMovementComponent::ComputeWindAcceleration(FVector(6000, 600, 0), Cross, Heading, Up, K).IsNearlyZero(1e-6));

	// 30 m at a full draw, in 1/60 s steps as the arrow flies; then on for 10 s.
	FVector Location = FVector::ZeroVector, Velocity = V;
	while (Location.X < 3000.0)
	{
		Velocity += UVCArrowMovementComponent::ComputeWindAcceleration(Velocity, Cross, Heading, Up, K) * ArrowStep;
		Location += Velocity * ArrowStep;
	}
	TestTrue(*FString::Printf(TEXT("About 28 uu aside at 30 m (%.1f)"), Location.Y), FMath::IsNearlyEqual(Location.Y, 28.0, 3.0));
	TestTrue(*FString::Printf(TEXT("The bow's speed along the heading kept (%.1f)"), Velocity.X), FMath::IsNearlyEqual(Velocity.X, 6000.0, 1e-3));
	for (int32 Step = 0; Step < 600; ++Step)
	{
		Velocity += UVCArrowMovementComponent::ComputeWindAcceleration(Velocity, Cross, Heading, Up, K) * ArrowStep;
	}
	TestTrue(*FString::Printf(TEXT("Never faster across than the wind (%.1f)"), Velocity.Y), Velocity.Y > 500.0 && Velocity.Y <= 600.0);
	return true;
}

// ===========================================================================
// Flight (world)
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCArrow_PlaneArc, "VoxelCharacter.Combat.Arrow.PlaneArc", VC_ARROW_FLAGS)
bool FVCArrow_PlaneArc::RunTest(const FString& Parameters)
{
	FArrowWorld H;
	const FVector Start(0, 0, 10000);
	AVCArrowProjectile* Arrow = SpawnArrow(H.World, nullptr, Start);
	if (!Arrow) { AddError(TEXT("Spawn failed")); return false; }

	const FVector V0(3000, 1000, 3000);
	Arrow->Launch(V0, MakeArrowShot(15.f, 1.f));
	const float GravityZ = Arrow->GetArrowMovement()->GetGravityZ();
	TestTrue(TEXT("The world has gravity"), GravityZ < 0.f);
	TestFalse(TEXT("No voxel world: no planet gravity"), Arrow->GetArrowMovement()->HasPlanetGravity());

	TestEqual(TEXT("Flies the whole second"), FlyArrow(Arrow, 60), 60);
	const double T = 60 * ArrowStep;
	const FVector Expected = Start + V0 * T + FVector(0, 0, 0.5 * GravityZ * T * T);
	const FVector Actual = Arrow->GetActorLocation();
	TestTrue(*FString::Printf(TEXT("The ballistic arc: %s, expected %s"), *Actual.ToString(), *Expected.ToString()), Actual.Equals(Expected, 0.5));
	const FVector Velocity = Arrow->GetArrowMovement()->Velocity;
	TestTrue(TEXT("Only its vertical speed changed"), Velocity.Equals(V0 + FVector(0, 0, GravityZ * T), 0.5));
	TestTrue(TEXT("It points along its flight"), FVector::DotProduct(Arrow->GetActorForwardVector(), Velocity.GetSafeNormal()) > 0.9999);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCArrow_PlanetFall, "VoxelCharacter.Combat.Arrow.PlanetFall", VC_ARROW_FLAGS)
bool FVCArrow_PlanetFall::RunTest(const FString& Parameters)
{
	FArrowWorld H;
	const FVector Centre(0, 0, 0);
	const double R = 250000.0;

	// Fired level on the equator (+X side, along Y) and under the south pole (along X): each falls toward the centre,
	// which is -X for the first and +Z for the second; the engine's -Z would pull the first down and the second away.
	struct FCase { const TCHAR* Name; FVector Start; FVector Velocity; };
	const FCase Cases[] = {
		{ TEXT("equator"), Centre + FVector(R, 0, 0), FVector(0, 3000, 0) },
		{ TEXT("south pole"), Centre + FVector(0, 0, -R), FVector(3000, 0, 0) },
	};
	for (const FCase& Case : Cases)
	{
		AVCArrowProjectile* Arrow = SpawnArrow(H.World, nullptr, Case.Start);
		if (!Arrow) { AddError(TEXT("Spawn failed")); return false; }
		UVCArrowMovementComponent* Movement = Arrow->GetArrowMovement();
		Movement->SetWorldBounds(ArrowPlanetBounds(Centre, R));
		Arrow->Launch(Case.Velocity, MakeArrowShot(15.f, 1.f));
		TestTrue(TEXT("Planet gravity"), Movement->HasPlanetGravity());
		const double G = -Movement->GetGravityZ();

		int32 OffCentre = 0;
		for (int32 i = 0; i < 60; ++i)
		{
			const FVector From = Arrow->GetActorLocation();
			const FVector V0 = Movement->Velocity;
			FlyArrow(Arrow, 1);
			// The step's acceleration, from its change of velocity: toward the centre from where the step began.
			const FVector Accel = (Movement->Velocity - V0) / ArrowStep;
			const FVector ToCentre = (Centre - From).GetSafeNormal();
			if (FVector::DotProduct(Accel.GetSafeNormal(), ToCentre) < 0.99999 || !FMath::IsNearlyEqual(Accel.Size(), G, G * 1e-3))
			{
				++OffCentre;
			}
		}
		TestEqual(*FString::Printf(TEXT("%s: every step accelerates toward the centre at the engine's gravity"), Case.Name), OffCentre, 0);

		const FVector End = Arrow->GetActorLocation();
		const FVector Up = (End - Centre).GetSafeNormal();
		const double Drop = R - FVector::Dist(End, Centre);
		// Half g t^2 = 490 in a second, less the curve's 18 (v^2 t^2 / 2R) as the arrow moves round the planet.
		TestTrue(*FString::Printf(TEXT("%s: it fell toward the centre (%.1f uu)"), Case.Name, Drop), Drop > 440.0 && Drop < 500.0);
		TestTrue(*FString::Printf(TEXT("%s: and is falling inward"), Case.Name), FVector::DotProduct(Movement->Velocity, Up) < 0.0);
		const double Sideways = FMath::Abs(FVector::DotProduct(End - Case.Start, FVector::CrossProduct(Case.Start - Centre, Case.Velocity).GetSafeNormal()));
		TestTrue(*FString::Printf(TEXT("%s: no pull out of its plane of flight (%.3f uu)"), Case.Name, Sideways), Sideways < 0.5);
	}
	return true;
}

// ===========================================================================
// What it meets (world)
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCArrow_HitsPawn, "VoxelCharacter.Combat.Arrow.HitsPawn", VC_ARROW_FLAGS)
bool FVCArrow_HitsPawn::RunTest(const FString& Parameters)
{
	FArrowWorld H;
	AVCNPCCharacterBase* Shooter = SpawnArrowTarget(H.World, CGFGameplayTags::Faction_Player, FVector(0, 0, 0));
	AVCNPCCharacterBase* Target = SpawnArrowTarget(H.World, CGFGameplayTags::Faction_Monster, FVector(800, 0, 0));
	if (!Shooter || !Target) { AddError(TEXT("Spawn failed")); return false; }

	// Spawned the bow's way, from inside the shooter's capsule: the sweep ignores the shooter and meets the target.
	AVCArrowProjectile* Arrow = AVCArrowProjectile::SpawnArrow(Shooter, nullptr, FVector(0, 0, 40), FVector(6000, 0, 0),
		MakeArrowShot(20.f, 0.5f), nullptr);
	if (!Arrow) { AddError(TEXT("Spawn failed")); return false; }
	TestTrue(TEXT("Its shooter is its instigator"), Arrow->GetInstigator() == Shooter);
	const int32 Flown = FlyArrow(Arrow, 60);

	TestTrue(*FString::Printf(TEXT("It stopped within its flight (%d steps)"), Flown), Flown < 60);
	TestEqual(TEXT("The shooter is untouched"), Shooter->CombatComponent->GetHealth(), 100.f);
	TestEqual(TEXT("The target took the half draw's damage"), Target->CombatComponent->GetHealth(), 90.f);
	TestFalse(TEXT("The arrow went (it does not stay in a body)"), IsValid(Arrow));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCArrow_Sticks, "VoxelCharacter.Combat.Arrow.Sticks", VC_ARROW_FLAGS)
bool FVCArrow_Sticks::RunTest(const FString& Parameters)
{
	FArrowWorld H;
	const FVector Extent(50, 300, 300);
	AActor* StaticWall = SpawnArrowWall(H.World, FVector(1000, 0, 1000), Extent, EComponentMobility::Static);
	AActor* MovingWall = SpawnArrowWall(H.World, FVector(1000, 2000, 1000), Extent, EComponentMobility::Movable);
	// Movable but known to this machine only, as the terrain's per-chunk collision is.
	AActor* LocalWall = SpawnArrowWall(H.World, FVector(1000, 4000, 1000), Extent, EComponentMobility::Movable);
	if (!StaticWall || !MovingWall || !LocalWall) { AddError(TEXT("Spawn failed")); return false; }
	MovingWall->SetReplicates(true);

	AVCArrowProjectile* Arrow = SpawnArrow(H.World, nullptr, FVector(0, 0, 1000));
	if (!Arrow) { AddError(TEXT("Spawn failed")); return false; }
	Arrow->Launch(FVector(6000, 0, 0), MakeArrowShot(15.f, 1.f));
	FlyArrow(Arrow, 60);
	TestTrue(TEXT("It stuck in the wall"), IsValid(Arrow) && Arrow->IsStuck());
	if (!IsValid(Arrow))
	{
		return false;
	}
	TestTrue(*FString::Printf(TEXT("At the wall's face (x %.1f)"), Arrow->GetActorLocation().X), FMath::IsNearlyEqual(Arrow->GetActorLocation().X, 950.0 - 4.0, 2.0));
	TestFalse(TEXT("Stuck without collision"), CastChecked<UPrimitiveComponent>(Arrow->GetRootComponent())->IsCollisionEnabled());
	TestTrue(TEXT("Not riding a static wall"), Arrow->GetAttachParentActor() == nullptr);
	TestTrue(TEXT("Staying a while"), Arrow->GetLifeSpan() > Arrow->FlightLifetime);

	AVCArrowProjectile* Second = SpawnArrow(H.World, nullptr, FVector(0, 2000, 1000));
	if (!Second) { AddError(TEXT("Spawn failed")); return false; }
	Second->Launch(FVector(6000, 0, 0), MakeArrowShot(15.f, 1.f));
	FlyArrow(Second, 60);
	TestTrue(TEXT("It rides a movable, replicated wall"), IsValid(Second) && Second->IsStuck() && Second->GetAttachParentActor() == MovingWall);

	AVCArrowProjectile* Third = SpawnArrow(H.World, nullptr, FVector(0, 4000, 1000));
	if (!Third) { AddError(TEXT("Spawn failed")); return false; }
	Third->Launch(FVector(6000, 0, 0), MakeArrowShot(15.f, 1.f));
	FlyArrow(Third, 60);
	TestTrue(TEXT("A movable wall only this machine knows keeps it where it stuck"), IsValid(Third) && Third->IsStuck() && Third->GetAttachParentActor() == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCArrow_DamageAndFactions, "VoxelCharacter.Combat.Arrow.DamageAndFactions", VC_ARROW_FLAGS)
bool FVCArrow_DamageAndFactions::RunTest(const FString& Parameters)
{
	FArrowWorld H;
	AVCNPCCharacterBase* Shooter = SpawnArrowTarget(H.World, CGFGameplayTags::Faction_Player, FVector(0, 0, 0));
	AVCNPCCharacterBase* Monster = SpawnArrowTarget(H.World, CGFGameplayTags::Faction_Monster, FVector(800, 0, 0));
	AVCNPCCharacterBase* Friend = SpawnArrowTarget(H.World, CGFGameplayTags::Faction_Player, FVector(0, 800, 0));
	if (!Shooter || !Monster || !Friend) { AddError(TEXT("Spawn failed")); return false; }

	AVCArrowProjectile* Weak = SpawnArrow(H.World, Shooter, FVector(0, 0, 40));
	AVCArrowProjectile* Full = SpawnArrow(H.World, Shooter, FVector(0, 0, 40));
	if (!Weak || !Full) { AddError(TEXT("Spawn failed")); return false; }
	Weak->Launch(FVector(1800, 0, 0), MakeArrowShot(15.f, 0.3f));
	Full->Launch(FVector(6000, 0, 0), MakeArrowShot(15.f, 1.f));

	const FHitResult Hit;
	TestEqual(TEXT("A short draw hits"), Weak->ApplyHit(Monster, Hit), ECGFDamageResult::Applied);
	TestEqual(TEXT("for the bow's damage times the draw"), Monster->CombatComponent->GetHealth(), 100.f - 4.5f);
	TestEqual(TEXT("A full draw hits"), Full->ApplyHit(Monster, Hit), ECGFDamageResult::Applied);
	TestEqual(TEXT("for the bow's damage"), Monster->CombatComponent->GetHealth(), 100.f - 4.5f - 15.f);

	TestEqual(TEXT("A friend is not hurt"), Full->ApplyHit(Friend, Hit), ECGFDamageResult::Rejected_Friendly);
	TestEqual(TEXT("Its health untouched"), Friend->CombatComponent->GetHealth(), 100.f);

	// The shooter is gone with the arrow in the air: its faction from the launch still decides.
	H.World->DestroyActor(Shooter);
	TestEqual(TEXT("Shooter gone: a friend is still not hurt"), Full->ApplyHit(Friend, Hit), ECGFDamageResult::Rejected_Friendly);
	TestEqual(TEXT("Shooter gone: an enemy still is"), Full->ApplyHit(Monster, Hit), ECGFDamageResult::Applied);
	TestEqual(TEXT("for the bow's damage"), Monster->CombatComponent->GetHealth(), 100.f - 4.5f - 15.f - 15.f);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
