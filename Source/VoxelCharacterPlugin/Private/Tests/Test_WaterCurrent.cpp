// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/VCCharacterBase.h"
#include "Effects/VCRagdollBuoyancy.h"
#include "Effects/VCWaterSurfaceBridge.h"
#include "Movement/VCMovementComponent.h"
#include "Movement/VCTerrainQueryHelper.h"
#include "Subsystems/CGFWaterSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Testing/VoxelTestChunkWorld.h"
#include "VoxelChunkManager.h"
#include "VoxelData.h"
#include "VoxelWaterLevels.h"
#include "VoxelWorldConfiguration.h"

#if WITH_INTERACTION_PLUGIN
#include "Actors/WorldItem.h"
#include "Subsystems/WorldItemPoolSubsystem.h"
#endif

// ---------------------------------------------------------------------------
// The current (the hydrology plan's H6; Documentation/HYDROLOGY_PLAN.md, section 5.9 and D150): a river reach carries a
// swimmer downstream at the river's own speed; a lake and the sea are still; floating items and dead bodies do not
// move.
//
// - VoxelCharacter.Water.Current.Rule: the pure rule. A river reach's sample gives its flow times the factor, under
//   the surface and up to the reach over it; a lake's, the sea's and no water give none; no factor, no current.
// - VoxelCharacter.Water.Current.Plane, .Planet: a streamed world with a river reach and a lake laid over its ground
//   (their columns hold flagged water since the plan's H2). A pawn swimming in the river with no input moves
//   downstream at the reach's speed, along the river and not across it, its own velocity still nothing and its mode
//   still swimming; with the factor at 0 it stays; in the lake it stays; the current asked at a position is the
//   reach's flow there and nothing over the lake, the sea or dry ground. A dropped item floats on the river where it
//   was dropped and is still there after it has ticked; a dead body's part, followed through time under the body's
//   rule with the river's surface for its up, comes to rest within a hand of its anchor column.
// ---------------------------------------------------------------------------

#define VC_WATER_CURRENT_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCWaterCurrent_Rule, "VoxelCharacter.Water.Current.Rule", VC_WATER_CURRENT_FLAGS)
bool FVCWaterCurrent_Rule::RunTest(const FString& Parameters)
{
	const FVector Flow(120.0, -90.0, -4.0);
	FVoxelWaterSample River;
	River.bWater = true;
	River.Kind = EVoxelWaterBodyKind::Reach;
	River.BodyId = 7;
	River.Depth = 50.0;
	River.Flow = Flow;

	TestTrue(TEXT("In a river reach: its flow"), UVCMovementComponent::ComputeSwimCurrent(River, 1.0f, 90.0f).Equals(Flow, 1.0e-6));
	TestTrue(TEXT("Half the factor: half the flow"), UVCMovementComponent::ComputeSwimCurrent(River, 0.5f, 90.0f).Equals(0.5 * Flow, 1.0e-6));
	TestTrue(TEXT("No factor: no current"), UVCMovementComponent::ComputeSwimCurrent(River, 0.0f, 90.0f).IsZero());

	// At the surface the capsule's centre is over the water by less than its half height.
	River.Depth = -60.0;
	TestTrue(TEXT("60 over the surface, reach 90: carried"), UVCMovementComponent::ComputeSwimCurrent(River, 1.0f, 90.0f).Equals(Flow, 1.0e-6));
	River.Depth = -120.0;
	TestTrue(TEXT("120 over the surface, reach 90: not carried"), UVCMovementComponent::ComputeSwimCurrent(River, 1.0f, 90.0f).IsZero());
	River.Depth = 50.0;

	FVoxelWaterSample Lake = River;
	Lake.Kind = EVoxelWaterBodyKind::Lake;
	Lake.Flow = FVector::ZeroVector;
	TestTrue(TEXT("In a lake: no current"), UVCMovementComponent::ComputeSwimCurrent(Lake, 1.0f, 90.0f).IsZero());
	Lake.Flow = Flow; // a lake has no flow; even a sample that carried one would move nobody
	TestTrue(TEXT("... whatever its sample says"), UVCMovementComponent::ComputeSwimCurrent(Lake, 1.0f, 90.0f).IsZero());
	FVoxelWaterSample Sea = River;
	Sea.Kind = EVoxelWaterBodyKind::Sea;
	Sea.BodyId = FVoxelWaterBody::SeaId;
	TestTrue(TEXT("In the sea: no current"), UVCMovementComponent::ComputeSwimCurrent(Sea, 1.0f, 90.0f).IsZero());
	FVoxelWaterSample Dry;
	Dry.Kind = EVoxelWaterBodyKind::Reach;
	Dry.Flow = Flow;
	TestTrue(TEXT("No water over the position: no current"), UVCMovementComponent::ComputeSwimCurrent(Dry, 1.0f, 90.0f).IsZero());
	return true;
}

namespace VCWaterCurrentTest
{
	constexpr float FlowSpeed = 150.0f;

	AVCCharacterBase* SpawnSwimmer(UWorld* World, const FVector& Location)
	{
		const FTransform Transform(Location);
		AVCCharacterBase* Character = World->SpawnActorDeferred<AVCCharacterBase>(AVCCharacterBase::StaticClass(), Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Character)
		{
			Character->bWaitForTerrain = false;
			Character->FinishSpawning(Transform);
		}
		return Character;
	}

	void Run(FAutomationTestBase& Test, bool bPlanet)
	{
		const FString Name = bPlanet ? TEXT("Planet") : TEXT("Plane");
		auto Say = [&Name](const TCHAR* What) { return FString::Printf(TEXT("%s: %s"), *Name, What); };
		VoxelTesting::FTestChunkWorld W;
		W.Create(bPlanet ? TEXT("VCWaterCurrentPlanet") : TEXT("VCWaterCurrentPlane"), /*bInitializeActorsForPlay=*/true);
		if (AWorldSettings* WorldSettings = W.World->GetWorldSettings())
		{
			WorldSettings->NotifyBeginPlay();
		}
		if (bPlanet)
		{
			W.ConfigureSmallPlanet();
			W.Config->WaterRadius = W.Config->WorldRadius - 3000.0f; // the sea under every piece of land
		}
		else
		{
			W.ConfigureSmallPlane();
			W.Config->WaterLevel = -3000.0f;
		}
		W.Config->bEnableWaterLevel = true;
		W.StartManager();
		UVoxelChunkManager* Manager = W.Manager;
		const FVoxelWaterFrame Frame = Manager->GetWaterLevels().GetFrame();

		// The site: a point of the ground, its up, and the river's way (downstream) and the way across it there.
		const FVector SiteDirection = FVector(-0.3, 0.4, 0.85).GetSafeNormal();
		const FVector Site = bPlanet ? Manager->GetGeneratedSurfacePoint(Frame.Center + SiteDirection * Frame.DatumRadius)
			: Manager->GetGeneratedSurfacePoint(FVector(2300.0, -1700.0, 0.0));
		const FVector Up = Manager->GetUpVectorAt(Site);
		const FVector Downstream = bPlanet ? FVector::CrossProduct(FVector::UpVector, Up).GetSafeNormal() : FVector::ForwardVector;
		const FVector Across = FVector::CrossProduct(Up, Downstream);
		// The column a distance along the river and across it from the site: a point of the datum (a planet) or of Z 0.
		auto ColumnAt = [&](double Along, double AcrossDistance)
		{
			if (!bPlanet)
			{
				return FVector(Site.X, Site.Y, 0.0) + Downstream * Along + Across * AcrossDistance;
			}
			return Frame.Center + (Up * Frame.DatumRadius + Downstream * Along + Across * AcrossDistance).GetSafeNormal() * Frame.DatumRadius;
		};
		auto GroundAt = [&](double Along, double AcrossDistance) { return Frame.HeightOf(Manager->GetGeneratedSurfacePoint(ColumnAt(Along, AcrossDistance))); };
		// The point of a column at a water level (a height in the frame's terms).
		auto AtLevel = [&](double Along, double AcrossDistance, double Level)
		{
			const FVector Column = ColumnAt(Along, AcrossDistance);
			return bPlanet ? Frame.Center + (Column - Frame.Center).GetSafeNormal() * Level : FVector(Column.X, Column.Y, Level);
		};

		// The river: 3 km along the site, 300 to each side, level along its length 300 over the highest ground of its
		// footprint, so every column of it holds water; it runs at 150 uu/s. The lake: 8 x 8 cells of 200 uu, 2.2 km
		// across from the river, 300 over its highest ground.
		const double HalfLength = 1500.0, HalfWidth = 300.0;
		double HighestRiverGround = -TNumericLimits<double>::Max();
		for (double Along = -HalfLength; Along <= HalfLength; Along += 150.0)
		{
			for (const double AcrossDistance : { -HalfWidth, 0.0, HalfWidth })
			{
				HighestRiverGround = FMath::Max(HighestRiverGround, GroundAt(Along, AcrossDistance));
			}
		}
		const double RiverLevel = HighestRiverGround + 300.0;
		TArray<FVoxelWaterReachVertex> Vertices;
		for (double Along = -HalfLength; Along <= HalfLength; Along += 250.0)
		{
			const FVector Column = ColumnAt(Along, 0.0);
			const FVector Position = bPlanet ? (Column - Frame.Center).GetSafeNormal() : FVector(Column.X, Column.Y, 0.0);
			Vertices.Add(FVoxelWaterReachVertex(Position, RiverLevel - 100.0, RiverLevel, HalfWidth));
		}
		Manager->AddWaterBody(FVoxelWaterBody::MakeReach(21, FVoxelWaterReach::Make(MoveTemp(Vertices), bPlanet, FlowSpeed), Frame));

		const double LakeAcross = 2200.0;
		double HighestLakeGround = -TNumericLimits<double>::Max();
		for (double Along = -800.0; Along <= 800.0; Along += 200.0)
		{
			for (double AcrossDistance = LakeAcross - 800.0; AcrossDistance <= LakeAcross + 800.0; AcrossDistance += 200.0)
			{
				HighestLakeGround = FMath::Max(HighestLakeGround, GroundAt(Along, AcrossDistance));
			}
		}
		const double LakeLevel = HighestLakeGround + 300.0;
		const FVector LakeColumn = ColumnAt(0.0, LakeAcross);
		const TSharedRef<const FVoxelWaterLake> LakeCells = bPlanet
			? FVoxelWaterLake::MakeOnSphere((LakeColumn - Frame.Center).GetSafeNormal(), Frame.DatumRadius, 200.0, FIntPoint(8, 8), TBitArray<>(true, 64))
			: FVoxelWaterLake::Make(FVector2D(LakeColumn.X - 800.0, LakeColumn.Y - 800.0), 200.0, FIntPoint(8, 8), TBitArray<>(true, 64));
		Manager->AddWaterBody(FVoxelWaterBody::MakeLake(22, LakeLevel, LakeCells, Frame, 2000.0));

		// The world streams around a point between the two.
		W.SetViewer(AtLevel(0.0, 0.5 * LakeAcross, FMath::Max(RiverLevel, LakeLevel) + 300.0));
		Test.TestTrue(*Say(TEXT("the world settles")), W.Settle());

		// Where the pawn swims: 120 under the river's surface over the site, and 120 under the lake's at its centre.
		const double SwimDepth = 120.0;
		const FVector InRiver = AtLevel(0.0, 0.0, RiverLevel - SwimDepth);
		const FVector InLake = AtLevel(0.0, LakeAcross, LakeLevel - SwimDepth);
		if (!Test.TestTrue(*Say(TEXT("the river's and the lake's columns hold flagged water (the plan's H2; non-vacuous)")),
			Manager->GetVoxelAt(InRiver).HasWaterFlag() && Manager->GetVoxelAt(InLake).HasWaterFlag()))
		{
			W.Stop();
			return;
		}

		AVCCharacterBase* Character = SpawnSwimmer(W.World, InRiver);
		UVCMovementComponent* Move = Character ? Cast<UVCMovementComponent>(Character->GetCharacterMovement()) : nullptr;
		if (!Test.TestNotNull(*Say(TEXT("a pawn spawns in the river")), Move))
		{
			W.Stop();
			return;
		}
		Move->bRunPhysicsWithNoController = true;
		Move->WindPushFactor = 0.f; // the world's weather is not this test's
		Test.TestEqual(*Say(TEXT("the current carries a swimmer at the river's own speed by default")), Move->WaterCurrentFactor, 1.0f);

		// The current asked at a position.
		const FVector Asked = Move->GetSwimCurrentAt(InRiver);
		Test.TestTrue(*Say(TEXT("in the river the current is the reach's speed, downstream")),
			FMath::IsNearlyEqual(Asked.Size(), static_cast<double>(FlowSpeed), 0.01) && FVector::DotProduct(Asked.GetSafeNormal(), Downstream) > 0.9999);
		Test.TestTrue(*Say(TEXT("... along the surface: nothing of it along the up")), FMath::Abs(FVector::DotProduct(Asked, Up)) < 0.5);
		Test.TestTrue(*Say(TEXT("in the lake there is no current")), Move->GetSwimCurrentAt(InLake).IsZero());
		Test.TestTrue(*Say(TEXT("beside the river, outside its footprint, there is none")), Move->GetSwimCurrentAt(AtLevel(0.0, 2.0 * HalfWidth, RiverLevel - SwimDepth)).IsZero());
		Test.TestTrue(*Say(TEXT("high over the river there is none")), Move->GetSwimCurrentAt(AtLevel(0.0, 0.0, RiverLevel + 500.0)).IsZero());
		Test.TestTrue(*Say(TEXT("under the sea, far below the land, there is none")), Move->GetSwimCurrentAt(AtLevel(0.0, -3000.0, RiverLevel - 6000.0)).IsZero());

		// A swim with no input: Frames ticks of a 60th.
		auto Swim = [&](const FVector& From, int32 Frames)
		{
			Character->SetActorLocation(From, false, nullptr, ETeleportType::TeleportPhysics);
			Move->Velocity = FVector::ZeroVector;
			Move->SetMovementMode(MOVE_Swimming);
			for (int32 Frame = 0; Frame < Frames; ++Frame)
			{
				Move->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
			}
			return Character->GetActorLocation() - From;
		};

		// In the river: two seconds downstream at 150 uu/s.
		{
			const FVector Moved = Swim(InRiver, 120);
			const double Along = FVector::DotProduct(Moved, Downstream);
			const double Sideways = FVector::DotProduct(Moved, Across);
			const FVector Own = Move->Velocity - Up * FVector::DotProduct(Move->Velocity, Up);
			Test.AddInfo(FString::Printf(TEXT("%s: two seconds in the river with no input: %.1f uu downstream (%.0f expected), %.2f across, %.1f along the up; its own velocity across the up %.2f uu/s; %s"),
				*Name, Along, 2.0 * FlowSpeed, Sideways, FVector::DotProduct(Moved, Up), Own.Size(), Move->IsSwimming() ? TEXT("swimming") : TEXT("NOT swimming")));
			Test.TestTrue(*Say(TEXT("a swimmer in the river moves downstream at the reach's speed")), FMath::IsNearlyEqual(Along, 2.0 * FlowSpeed, 0.02 * 2.0 * FlowSpeed));
			Test.TestTrue(*Say(TEXT("... along the river, not across it")), FMath::Abs(Sideways) < 2.0);
			Test.TestTrue(*Say(TEXT("... its own velocity still nothing: the river's is not the swimmer's")), Own.Size() < 2.0);
			Test.TestTrue(*Say(TEXT("... still swimming")), Move->IsSwimming());
		}

		// With the factor at 0 the river is still water.
		{
			Move->WaterCurrentFactor = 0.f;
			const FVector Moved = Swim(InRiver, 60);
			Test.TestTrue(*Say(TEXT("with no factor the swimmer stays where it is")), (Moved - Up * FVector::DotProduct(Moved, Up)).Size() < 1.0 && Move->IsSwimming());
			Move->WaterCurrentFactor = 1.f;
		}

		// In the lake.
		{
			const FVector Moved = Swim(InLake, 120);
			Test.AddInfo(FString::Printf(TEXT("%s: two seconds in the lake: %.2f uu across the up; %s"), *Name, (Moved - Up * FVector::DotProduct(Moved, Up)).Size(),
				Move->IsSwimming() ? TEXT("swimming") : TEXT("NOT swimming")));
			Test.TestTrue(*Say(TEXT("a swimmer in the lake does not move")), (Moved - Up * FVector::DotProduct(Moved, Up)).Size() < 1.0);
			Test.TestTrue(*Say(TEXT("... and swims")), Move->IsSwimming());
		}
		Character->Destroy();

		// ---- Floating items and dead bodies do not move (D150).
		UVCWaterSurfaceBridge* Bridge = W.World->GetSubsystem<UVCWaterSurfaceBridge>();
		UCGFWaterSubsystem* Water = W.World->GetSubsystem<UCGFWaterSubsystem>();
		if (Test.TestNotNull(*Say(TEXT("the world has the bridge")), Bridge) && Test.TestNotNull(*Say(TEXT("the world has the framework's water subsystem")), Water))
		{
			Test.TestTrue(*Say(TEXT("the bridge registers")), Bridge->Register() && Water->HasProvider());
			FCGFWaterSurfaceSample Sample;
			Test.TestTrue(*Say(TEXT("the framework's sample in the river is water at the river's surface")),
				Water->SampleWaterSurface(InRiver, 500.0f, Sample) && Sample.bWater && FMath::IsNearlyEqual(Sample.Depth, SwimDepth, 0.5));

#if WITH_INTERACTION_PLUGIN
			if (UWorldItemPoolSubsystem* Pool = W.World->GetSubsystem<UWorldItemPoolSubsystem>())
			{
				if (bPlanet)
				{
					Pool->SetGravityCenter(W.Config->WorldOrigin); // as the game mode does on a planet
				}
				if (AWorldItem* Item = Pool->SpawnWorldItem(FItemInstance(), InRiver))
				{
					const FVector Rest = Item->GetActorLocation();
					Test.TestTrue(*Say(TEXT("an item dropped in the river floats at its surface, over the drop")),
						Item->IsFloating() && FMath::Abs(Frame.HeightOf(Rest) - RiverLevel) < 0.5 && FVector::VectorPlaneProject(Rest - InRiver, Up).Size() < 0.5);
					for (int32 Frame60 = 0; Frame60 < 120; ++Frame60)
					{
						Item->Tick(1.0f / 60.0f);
					}
					// A floating item bobs along the up; across it, it does not move.
					const FVector Drift = Item->GetActorLocation() - Rest;
					Test.AddInfo(FString::Printf(TEXT("%s: an item two seconds on the river: %.3f uu across the up, %.2f along it"),
						*Name, FVector::VectorPlaneProject(Drift, Up).Size(), FVector::DotProduct(Drift, Up)));
					Test.TestTrue(*Say(TEXT("... and is over the same column two seconds on: the river carries no item")),
						FVector::VectorPlaneProject(Drift, Up).Size() < 0.05 && FMath::Abs(FVector::DotProduct(Drift, Up)) < 50.0);
					Pool->ReturnWorldItem(Item);
				}
				else
				{
					Test.AddError(Say(TEXT("no item from the pool")));
				}
				Pool->ClearGravityCenter();
			}
#endif // WITH_INTERACTION_PLUGIN

			// A dead body's part under the body's rule, with the river's own surface for its up (it leans a little along a
			// river that falls; here the river is level): followed for 30 s from 200 under the surface, pulled to the
			// column it met the water in. It comes to rest at the rest depth, within a hand of that column.
			const FVCRagdollBuoyancy Rules;
			const FVector SurfaceUp = Sample.Up;
			const float Gravity = 980.0f;
			FVector Position = InRiver - Up * (200.0 - SwimDepth);
			const FVector Anchor = Position;
			FVector Velocity = FVector::ZeroVector;
			const double Step = 1.0 / 120.0;
			for (int32 Tick = 0; Tick < 30 * 120; ++Tick)
			{
				FVoxelWaterSample Over;
				Manager->SampleWater(Position, Over);
				const FVector ToAnchor = FVector::VectorPlaneProject(Anchor - Position, Up);
				const FVector Acceleration = Rules.ComputeAcceleration(Over.Depth, SurfaceUp, Velocity, ToAnchor, Gravity) - Up * Gravity;
				Velocity += Acceleration * Step;
				Position += Velocity * Step;
			}
			FVoxelWaterSample AtRest;
			Manager->SampleWater(Position, AtRest);
			const double Off = FVector::VectorPlaneProject(Position - Anchor, Up).Size();
			Test.AddInfo(FString::Printf(TEXT("%s: a body's part after 30 s in the river: depth %.1f (rest depth %.1f), %.2f uu from its column, speed %.3f"),
				*Name, AtRest.Depth, Rules.GetRestDepth(), Off, Velocity.Size()));
			Test.TestTrue(*Say(TEXT("a dead body in the river rests at the rest depth")), FMath::Abs(AtRest.Depth - Rules.GetRestDepth()) < 2.0 && Velocity.Size() < 1.0);
			Test.TestTrue(*Say(TEXT("... in the column it met the water in: the river carries no body")), Off < 10.0);
		}
		W.Stop();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCWaterCurrent_Plane, "VoxelCharacter.Water.Current.Plane", VC_WATER_CURRENT_FLAGS)
bool FVCWaterCurrent_Plane::RunTest(const FString& Parameters)
{
	VCWaterCurrentTest::Run(*this, /*bPlanet=*/false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCWaterCurrent_Planet, "VoxelCharacter.Water.Current.Planet", VC_WATER_CURRENT_FLAGS)
bool FVCWaterCurrent_Planet::RunTest(const FString& Parameters)
{
	VCWaterCurrentTest::Run(*this, /*bPlanet=*/true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
