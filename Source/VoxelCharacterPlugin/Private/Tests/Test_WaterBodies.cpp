// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Effects/VCWaterSurfaceBridge.h"
#include "Movement/VCTerrainQueryHelper.h"
#include "Subsystems/CGFWaterSubsystem.h"
#include "Testing/VoxelTestChunkWorld.h"
#include "VoxelChunkManager.h"
#include "VoxelWaterLevels.h"
#include "VoxelWorldConfiguration.h"

#if WITH_DEV_AUTOMATION_TESTS

// ---------------------------------------------------------------------------
// The character plugin at a lake (the hydrology plan's H1): the swimmer's sample and the water bridge read the voxel
// world's point gather, so a lake above the sea answers where the sea alone used to.
//
// - VoxelCharacter.Water.Bodies.Plane, .Planet: a world whose sea lies under its land, and a lake over a site.
//   FVCTerrainQueryHelper::SampleWaterSurface answers with the lake over its footprint (the depth under its level, the
//   local up) and with the sea outside it; the framework's question through the bridge gives the same depth; the
//   spawn search does not put a pawn in the lake; and a change of the world's bodies reaches the framework's water
//   subsystem through the bridge, with the bounds of the change. The voxels under a lake carry no water flag until
//   generation fills bodies (the plan's H2), so the sample's bWater is not asked here.
// ---------------------------------------------------------------------------

#define VC_WATER_BODIES_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace VCWaterBodiesTest
{
	void Run(FAutomationTestBase& Test, bool bPlanet)
	{
		const FString Name = bPlanet ? TEXT("Planet") : TEXT("Plane");
		VoxelTesting::FTestChunkWorld W;
		W.Create(bPlanet ? TEXT("VCWaterBodiesPlanet") : TEXT("VCWaterBodiesPlane"));
		if (bPlanet)
		{
			W.ConfigureSmallPlanet();
			W.Config->WaterRadius = W.Config->WorldRadius - 3000.0f;
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

		UVCWaterSurfaceBridge* Bridge = W.World->GetSubsystem<UVCWaterSurfaceBridge>();
		UCGFWaterSubsystem* Water = W.World->GetSubsystem<UCGFWaterSubsystem>();
		if (!Test.TestNotNull(TEXT("The world has the bridge"), Bridge) || !Test.TestNotNull(TEXT("The world has the framework's water subsystem"), Water))
		{
			return;
		}
		Test.TestTrue(*FString::Printf(TEXT("%s: the bridge registers"), *Name), Bridge->Register() && Water->HasProvider());
		const int32 PassedBefore = Bridge->GetNumChangesPassed();

		const FVector SiteDirection = FVector(-0.3, 0.4, 0.85).GetSafeNormal();
		const FVector Site = bPlanet ? Manager->GetGeneratedSurfacePoint(Frame.Center + SiteDirection * Frame.DatumRadius)
			: Manager->GetGeneratedSurfacePoint(FVector(1500.0, 2500.0, 0.0));
		const FVector Up = Manager->GetUpVectorAt(Site);
		const FVector East = bPlanet ? FVector::CrossProduct(FVector::UpVector, Up).GetSafeNormal() : FVector::ForwardVector;
		const FVector North = FVector::CrossProduct(Up, East);
		const double SiteHeight = Frame.HeightOf(Site);
		const auto At = [&](double Along, double Across, double Over)
		{
			if (!bPlanet)
			{
				return Site + East * Along + North * Across + FVector(0.0, 0.0, Over);
			}
			return Frame.Center + (Up * Frame.DatumRadius + East * Along + North * Across).GetSafeNormal() * (SiteHeight + Over);
		};

		// The lake, added when the world starts: 10 x 10 cells of 200 uu, its level 1,200 over the site (over every hill
		// under it: the terrain's amplitude is 800).
		const double LakeLevel = SiteHeight + 1200.0;
		const TSharedRef<const FVoxelWaterLake> Cells = bPlanet
			? FVoxelWaterLake::MakeOnSphere(Up, Frame.DatumRadius, 200.0, FIntPoint(10, 10), TBitArray<>(true, 100))
			: FVoxelWaterLake::Make(FVector2D(Site.X - 1000.0, Site.Y - 1000.0), 200.0, FIntPoint(10, 10), TBitArray<>(true, 100));
		Manager->AddWaterBody(FVoxelWaterBody::MakeLake(1, LakeLevel, Cells, Frame, 3000.0));

		// The change reached the framework through the bridge, with where.
		Test.TestEqual(*FString::Printf(TEXT("%s: a body added is passed on to the framework's water subsystem"), *Name), Bridge->GetNumChangesPassed(), PassedBefore + 1);
		Test.TestTrue(*FString::Printf(TEXT("%s: ... with bounds that hold the lake"), *Name),
			Bridge->GetLastChangeBounds().IsValid && Bridge->GetLastChangeBounds().IsInsideOrOn(At(0.0, 0.0, 1200.0))
			&& Bridge->GetLastChangeBounds().IsInsideOrOn(At(900.0, -900.0, 1200.0)) && !Bridge->GetLastChangeBounds().IsInsideOrOn(At(9000.0, 0.0, 0.0)));

		// The swimmer's sample: the lake over its footprint, the sea outside it.
		FVCWaterSurfaceSample Sample;
		const FVector Under = At(300.0, 200.0, 1000.0);
		Test.TestTrue(*FString::Printf(TEXT("%s: the sample over the lake is the lake's"), *Name),
			FVCTerrainQueryHelper::SampleWaterSurface(W.World, Under, 50.0f, Sample) && FMath::IsNearlyEqual(Sample.Depth, 200.0, 0.5)
			&& Sample.Up.Equals(Manager->GetUpVectorAt(Under), 1.0e-6));
		Test.TestTrue(*FString::Printf(TEXT("%s: the surface point straight over the position lies at the lake's level"), *Name),
			FMath::IsNearlyEqual(Frame.HeightOf(Sample.SurfacePoint(Under)), LakeLevel, 0.5));
		const FVector Outside = At(3000.0, 0.0, 1000.0);
		Test.TestTrue(*FString::Printf(TEXT("%s: outside the footprint the sample is the sea's"), *Name),
			FVCTerrainQueryHelper::SampleWaterSurface(W.World, Outside, 50.0f, Sample) && FMath::IsNearlyEqual(Sample.Depth, Manager->GetWaterLevels().GetDepth(Outside), 1.0e-6)
			&& Sample.Depth < -3000.0);

		// The framework's question, through the bridge.
		FCGFWaterSurfaceSample Framework;
		Test.TestTrue(*FString::Printf(TEXT("%s: the framework's sample over the lake has the lake's depth"), *Name),
			Water->SampleWaterSurface(Under, 50.0f, Framework) && FMath::IsNearlyEqual(static_cast<double>(Framework.Depth), 200.0, 0.5));

		// The spawn search: a start in the lake ends on dry ground outside its footprint.
		FVector Spawn;
		if (Test.TestTrue(*FString::Printf(TEXT("%s: a spawn point is found from a start in the lake"), *Name),
			FVCTerrainQueryHelper::FindSpawnablePosition(W.World, Site, Spawn, 8000.0f, false)))
		{
			const bool bInFootprint = bPlanet ? Cells->CoversDirection((Spawn - Frame.Center).GetSafeNormal()) : Cells->Covers(Spawn.X, Spawn.Y);
			Test.TestFalse(*FString::Printf(TEXT("%s: the spawn point is not in the lake"), *Name), bInFootprint);
			Test.TestTrue(*FString::Printf(TEXT("%s: ... and is above the water over it"), *Name), Manager->GetWaterDepth(Spawn) < 0.0);
		}
		W.Stop();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCWaterBodies_Plane, "VoxelCharacter.Water.Bodies.Plane", VC_WATER_BODIES_FLAGS)
bool FVCWaterBodies_Plane::RunTest(const FString& Parameters)
{
	VCWaterBodiesTest::Run(*this, false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCWaterBodies_Planet, "VoxelCharacter.Water.Bodies.Planet", VC_WATER_BODIES_FLAGS)
bool FVCWaterBodies_Planet::RunTest(const FString& Parameters)
{
	VCWaterBodiesTest::Run(*this, true);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
