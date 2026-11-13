// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Effects/VCRagdollBuoyancy.h"
#include "Effects/VCWaterSurfaceBridge.h"
#include "Movement/VCTerrainQueryHelper.h"
#include "Subsystems/CGFWaterSubsystem.h"
#include "Testing/VoxelTestChunkWorld.h"
#include "VoxelChunkManager.h"
#include "VoxelData.h"
#include "VoxelEditManager.h"
#include "VoxelEditTypes.h"
#include "VoxelWaterLevels.h"
#include "VoxelWaterPropagation.h"
#include "VoxelWorldConfiguration.h"

#if WITH_INTERACTION_PLUGIN
#include "Actors/WorldItem.h"
#include "Subsystems/WorldItemPoolSubsystem.h"
#endif

// ---------------------------------------------------------------------------
// Buoyancy (the water plan's W2B; Documentation/WATER_EPIC_PLAN.md, D129 and D130).
// - The bridge (UVCWaterSurfaceBridge) answers the framework's water question from the voxel world's water levels, per
//   position: the surface over it, the up, the depth, and whether the place really holds water.
// - A dropped item (InteractionPlugin's AWorldItem) rests at that surface instead of on the bed, in each kind of world:
//   a plane, an island, a block world (the level is a world Z) and a planet (up is the radial; the level is a radius).
//   Nothing floats where the sample says there is no water: in the bed's rock, and in a dry cave dug under the sea.
// - A dead body's ragdoll (FVCRagdollBuoyancy) rises to the surface and rests awash there: the rule's acceleration, and
//   a part followed through time under gravity, the rule's lift and its drag.
// ---------------------------------------------------------------------------

#define VC_BUOYANCY_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace VCBuoyancyTestHelpers
{
	/** Tick the world until streaming and the water propagation have been idle for 90 ticks. */
	bool SettleWithWater(VoxelTesting::FTestChunkWorld& W, double TimeoutSeconds = 120.0)
	{
		const UVoxelWaterPropagation* Water = W.Manager->GetWaterPropagation();
		int32 IdleTicks = 0;
		const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
		while (IdleTicks < 90 && FPlatformTime::Seconds() < Deadline)
		{
			W.Tick();
			IdleTicks = (W.IsIdle() && (!Water || !Water->HasPendingWork())) ? IdleTicks + 1 : 0;
		}
		return IdleTicks >= 90;
	}

	/**
	 * A column of sea with a bed 5 to 12 voxels under the surface, nearest the start first: a point of the bed and the
	 * local up there. On a heightmap world the search walks a 200 uu grid from the origin; on a planet it walks
	 * directions from the pole.
	 */
	bool FindSeabed(const VoxelTesting::FTestChunkWorld& W, FVector& OutBed, FVector& OutUp)
	{
		const float VS = W.Config->VoxelSize;
		const FVoxelWaterLevels Water = W.Manager->GetWaterLevels();
		auto Try = [&](const FVector& Probe)
		{
			const FVector Bed = W.Manager->GetGeneratedSurfacePoint(Probe);
			const double Depth = Water.GetDepth(Bed);
			if (Depth < 5.0 * VS || Depth > 12.0 * VS)
			{
				return false;
			}
			OutBed = Bed;
			OutUp = W.Manager->GetUpVectorAt(Bed);
			return true;
		};
		if (Water.IsRadial())
		{
			const FVector Centre = W.Config->WorldOrigin;
			for (int32 Index = 0; Index < 4000; ++Index)
			{
				// A spiral of directions outward from the pole.
				const double Polar = 0.004 * Index;
				const double Azimuth = 2.399963 * Index;
				const FVector Direction(FMath::Sin(Polar) * FMath::Cos(Azimuth), FMath::Sin(Polar) * FMath::Sin(Azimuth), FMath::Cos(Polar));
				if (Try(Centre + Direction * W.Config->WorldRadius))
				{
					return true;
				}
			}
			return false;
		}
		for (int32 Ring = 0; Ring <= 150; ++Ring)
		{
			for (int32 j = -Ring; j <= Ring; ++j)
			{
				for (int32 i = -Ring; i <= Ring; ++i)
				{
					if (FMath::Max(FMath::Abs(i), FMath::Abs(j)) == Ring && Try(FVector(i * 200.0, j * 200.0, 0.0)))
					{
						return true;
					}
				}
			}
		}
		return false;
	}

	/** The world kinds the floating rule is checked in. */
	enum class EWorldKind : uint8 { Plane, Island, Blocks, Planet };

	void Configure(VoxelTesting::FTestChunkWorld& W, EWorldKind Kind)
	{
		if (Kind == EWorldKind::Planet)
		{
			W.ConfigureSmallPlanet();
			W.Config->bEnableWaterLevel = true;
			W.Config->WaterRadius = W.Config->WorldRadius; // a sea at the datum
			return;
		}
		W.ConfigureSmallPlane();
		W.Config->bEnableWaterLevel = true;
		W.Config->WaterLevel = 0.0f;
		if (Kind == EWorldKind::Island)
		{
			W.Config->WorldMode = EWorldMode::IslandBowl;
			W.Config->IslandRadius = 20000.0f;
			W.Config->IslandFalloffWidth = 4000.0f;
			W.Config->IslandEdgeHeight = -1000.0f;
			W.Config->bWorldBoundary = false;
			W.Config->bIslandFarOcean = false;
		}
		else if (Kind == EWorldKind::Blocks)
		{
			W.Config->MeshingMode = EMeshingMode::Cubic;
		}
	}
}

// ---------------------------------------------------------------------------
// The rule for a body: the acceleration the water gives a part, and a part followed through time.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCBuoyancy_BodyRule, "VoxelCharacter.Water.BodyFloats.Rule", VC_BUOYANCY_FLAGS)
bool FVCBuoyancy_BodyRule::RunTest(const FString& Parameters)
{
	const FVCRagdollBuoyancy Rules;
	const float Gravity = 980.0f;
	const FVector Up = FVector::UpVector;

	// At or above the surface the water does nothing.
	TestTrue(TEXT("Above the surface: nothing"), Rules.ComputeAcceleration(-10.0, Up, FVector(50.0, 0.0, -300.0), FVector(100.0, 0.0, 0.0), Gravity).IsZero());
	TestTrue(TEXT("At the surface: nothing"), Rules.ComputeAcceleration(0.0, Up, FVector::ZeroVector, FVector::ZeroVector, Gravity).IsZero());

	// Deep and still: more lift than gravity, straight up; at the rest depth the lift is gravity's.
	const FVector Deep = Rules.ComputeAcceleration(500.0, Up, FVector::ZeroVector, FVector::ZeroVector, Gravity);
	TestTrue(TEXT("Deep and still: lifted by more than gravity"), Deep.Z > Gravity && FMath::IsNearlyZero(Deep.X) && FMath::IsNearlyZero(Deep.Y));
	const FVector AtRest = Rules.ComputeAcceleration(Rules.GetRestDepth(), Up, FVector::ZeroVector, FVector::ZeroVector, Gravity);
	TestTrue(TEXT("At the rest depth: the lift is gravity's (it hangs)"), FMath::Abs(AtRest.Z - Gravity) < 0.5);
	TestTrue(TEXT("The rest depth is awash: under the surface, within the submerge depth"), Rules.GetRestDepth() > 0.0 && Rules.GetRestDepth() < Rules.SubmergeDepth);

	// Drag opposes the velocity; the pull is toward the anchor and capped.
	const FVector Moving = Rules.ComputeAcceleration(500.0, Up, FVector(200.0, 0.0, 0.0), FVector::ZeroVector, Gravity);
	TestTrue(TEXT("Drag opposes the velocity"), Moving.X < 0.0 && FMath::IsNearlyEqual(Moving.X, -200.0 * Rules.LinearDrag, 0.01));
	const FVector Pulled = Rules.ComputeAcceleration(500.0, Up, FVector::ZeroVector, FVector(0.0, 20.0, 0.0), Gravity);
	TestTrue(TEXT("The pull is toward the anchor"), FMath::IsNearlyEqual(Pulled.Y, 20.0 * Rules.AnchorPull, 0.01));
	const FVector PulledFar = Rules.ComputeAcceleration(500.0, Up, FVector::ZeroVector, FVector(0.0, 5000.0, 0.0), Gravity);
	TestTrue(TEXT("The pull is capped"), FMath::IsNearlyEqual(PulledFar.Y, Rules.MaxAnchorAcceleration, 0.01));

	// Any up: on a planet the lift is along the radial.
	const FVector Radial = FVector(1.0, 2.0, -2.0).GetSafeNormal();
	const FVector OnPlanet = Rules.ComputeAcceleration(500.0, Radial, FVector::ZeroVector, FVector::ZeroVector, Gravity);
	TestTrue(TEXT("A radial up: the lift is along it"), FVector::CrossProduct(OnPlanet.GetSafeNormal(), Radial).Size() < 1.0e-6 && FVector::DotProduct(OnPlanet, Radial) > Gravity);

	// A part followed through time: gravity, the rule's lift and drag, 60 steps a second. From deep and still it rises
	// and comes to rest at the rest depth; dropped into the water from above it does the same.
	auto Follow = [&Rules, Gravity](double Depth, double VelocityUp, double Seconds, double& OutDeepest, double& OutSecondsToSurface)
	{
		const double Step = 1.0 / 60.0;
		OutDeepest = Depth;
		OutSecondsToSurface = -1.0;
		for (double Time = 0.0; Time < Seconds; Time += Step)
		{
			const FVector Water = Rules.ComputeAcceleration(Depth, FVector::UpVector, FVector(0.0, 0.0, VelocityUp), FVector::ZeroVector, Gravity);
			VelocityUp += (Water.Z - Gravity) * Step;
			Depth -= VelocityUp * Step;
			OutDeepest = FMath::Max(OutDeepest, Depth);
			if (OutSecondsToSurface < 0.0 && Depth <= Rules.SubmergeDepth)
			{
				OutSecondsToSurface = Time;
			}
		}
		return FVector2D(Depth, VelocityUp);
	};
	double Deepest = 0.0;
	double ToSurface = 0.0;
	const FVector2D FromDeep = Follow(2000.0, 0.0, 30.0, Deepest, ToSurface);
	AddInfo(FString::Printf(TEXT("From 2,000 uu deep: at the surface after %.1f s; after 30 s at depth %.2f (rest depth %.2f), speed %.3f"),
		ToSurface, FromDeep.X, Rules.GetRestDepth(), FromDeep.Y));
	TestTrue(TEXT("From deep: rests at the rest depth"), FMath::Abs(FromDeep.X - Rules.GetRestDepth()) < 1.0 && FMath::Abs(FromDeep.Y) < 1.0);
	TestTrue(TEXT("From 2,000 uu deep: at the surface within 10 s"), ToSurface > 0.0 && ToSurface < 10.0);
	const FVector2D FromAbove = Follow(-300.0, -800.0, 30.0, Deepest, ToSurface);
	AddInfo(FString::Printf(TEXT("Falling in at 8 m/s: deepest %.1f uu; after 30 s at depth %.2f, speed %.3f"), Deepest, FromAbove.X, FromAbove.Y));
	TestTrue(TEXT("Falling in: rests at the rest depth"), FMath::Abs(FromAbove.X - Rules.GetRestDepth()) < 1.0 && FMath::Abs(FromAbove.Y) < 1.0);
	TestTrue(TEXT("Falling in: the water stops it within a few metres"), Deepest < 600.0);

	// Nothing to push: no body.
	FVCRagdollBuoyancyState State;
	TestFalse(TEXT("No body: nothing"), Rules.Apply(nullptr, State) || State.bInWater);
	return true;
}

// ---------------------------------------------------------------------------
// The bridge and a dropped item, per kind of world.
// ---------------------------------------------------------------------------
namespace VCBuoyancyTestHelpers
{
	void RunItemFloats(FAutomationTestBase& Test, EWorldKind Kind, const TCHAR* WorldName)
	{
		VoxelTesting::FTestChunkWorld W;
		W.Create(WorldName);
		Configure(W, Kind);
		W.StartManager();

		FVector Bed;
		FVector Up;
		if (!FindSeabed(W, Bed, Up))
		{
			Test.AddError(TEXT("No seabed 5-12 voxels under the sea"));
			return;
		}
		const float VS = W.Config->VoxelSize;
		const FVoxelWaterLevels Levels = W.Manager->GetWaterLevels();
		const double BedDepth = Levels.GetDepth(Bed);
		// A point of the column by its depth under the surface (negative: over it).
		auto AtDepth = [&](double Depth) { return Bed + Up * (BedDepth - Depth); };
		W.SetViewer(AtDepth(-300.0));
		Test.TestTrue(TEXT("The world settles"), SettleWithWater(W));
		Test.AddInfo(FString::Printf(TEXT("Seabed %s, %.0f uu under the surface, up %s"), *Bed.ToCompactString(), BedDepth, *Up.ToCompactString()));

		// The bridge: registered, it answers the framework's question with the water levels' sample.
		UVCWaterSurfaceBridge* Bridge = W.World->GetSubsystem<UVCWaterSurfaceBridge>();
		UCGFWaterSubsystem* Water = W.World->GetSubsystem<UCGFWaterSubsystem>();
		if (!Test.TestNotNull(TEXT("The world has the bridge"), Bridge) || !Test.TestNotNull(TEXT("The world has the framework's water subsystem"), Water))
		{
			return;
		}
		Test.TestTrue(TEXT("The bridge registers"), Bridge->Register() && Water->HasProvider());

		FCGFWaterSurfaceSample Sample;
		Test.TestTrue(TEXT("In the water: an answer"), Water->SampleWaterSurface(AtDepth(1.5 * VS), 500.0f, Sample));
		Test.TestTrue(TEXT("In the water: water, with the depth"), Sample.bWater && FMath::IsNearlyEqual(Sample.Depth, 1.5 * VS, 0.05));
		Test.TestTrue(TEXT("In the water: the up is the world's"), Sample.Up.Equals(Up, 1.0e-4));
		Test.TestTrue(TEXT("In the water: the surface point is at the surface, over the position"),
			FMath::Abs(Levels.GetDepth(Sample.SurfacePoint)) < 0.05 && Sample.SurfacePoint.Equals(AtDepth(0.0), 0.05));
		Water->SampleWaterSurface(AtDepth(-200.0), 500.0f, Sample);
		Test.TestTrue(TEXT("200 over the water, reach 500: at the water"), Sample.bWater && Sample.Depth < 0.0);
		Water->SampleWaterSurface(AtDepth(-200.0), 100.0f, Sample);
		Test.TestFalse(TEXT("200 over the water, reach 100: not at the water"), Sample.bWater);
		Water->SampleWaterSurface(AtDepth(BedDepth + 1.5 * VS), 500.0f, Sample);
		Test.TestTrue(TEXT("In the bed's rock: under the level and not water"), Sample.Depth > 0.0 && !Sample.bWater);

		// What a sample costs, through the framework's subsystem and the bridge (the water levels built per call, the
		// voxel's flag read): the figure that says how many floating things a frame affords.
		{
			const int32 Calls = 20000;
			const FVector At = AtDepth(1.5 * VS);
			int32 Wet = 0;
			const double Start = FPlatformTime::Seconds();
			for (int32 Call = 0; Call < Calls; ++Call)
			{
				Water->SampleWaterSurface(At + Up * (Call & 15), 500.0f, Sample);
				Wet += Sample.bWater ? 1 : 0;
			}
			const double Microseconds = (FPlatformTime::Seconds() - Start) * 1.0e6 / Calls;
			Test.AddInfo(FString::Printf(TEXT("[Buoyancy] one water sample through the bridge: %.3f us (%d calls, %d wet)"), Microseconds, Calls, Wet));
		}

		// A dry cave under the sea: a dig closed in by the bed's rock, which the water does not reach.
		const FVector Cave = AtDepth(BedDepth + 6.0 * VS);
		UVoxelEditManager* Edits = W.Manager->GetEditManager();
		Edits->SetEditSource(EEditSource::Player);
		FVoxelBrushParams Brush;
		Brush.Shape = EVoxelBrushShape::Sphere;
		Brush.Radius = 2.5f * VS;
		Brush.Strength = 1.0f;
		Brush.FalloffType = EVoxelBrushFalloff::Sharp;
		Brush.DensityDelta = 255;
		Test.TestTrue(TEXT("The dig edits voxels"), Edits->ApplyBrushEdit(Cave, Brush, EEditMode::Subtract) > 0);
		Test.TestTrue(TEXT("The world settles after the dig"), SettleWithWater(W));
		const FVoxelData CaveVoxel = W.Manager->GetVoxelAt(Cave);
		Test.TestTrue(TEXT("The cave is air the water did not reach"), CaveVoxel.IsAir() && !CaveVoxel.HasWaterFlag());
		Water->SampleWaterSurface(Cave, 500.0f, Sample);
		Test.TestTrue(TEXT("In the dry cave: under the level and not water"), Sample.Depth > 0.0 && !Sample.bWater);

#if WITH_INTERACTION_PLUGIN
		UWorldItemPoolSubsystem* Pool = W.World->GetSubsystem<UWorldItemPoolSubsystem>();
		if (!Test.TestNotNull(TEXT("The world has an item pool"), Pool))
		{
			return;
		}
		if (Levels.IsRadial())
		{
			Pool->SetGravityCenter(W.Config->WorldOrigin); // as the game mode does on a planet
		}
		struct FDrop { const TCHAR* Name; double Depth; bool bFloats; };
		const FDrop Drops[] = {
			{ TEXT("in the water"), 1.5 * VS, true },
			{ TEXT("deep, over the bed"), BedDepth - 2.5 * VS, true },
			{ TEXT("200 over the water"), -200.0, true },
			{ TEXT("700 over the water (out of reach)"), -700.0, false },
			{ TEXT("in the bed's rock"), BedDepth + 1.5 * VS, false },
			{ TEXT("in the dry cave"), BedDepth + 6.0 * VS, false } };
		for (const FDrop& Drop : Drops)
		{
			const FVector From = AtDepth(Drop.Depth);
			AWorldItem* Item = Pool->SpawnWorldItem(FItemInstance(), From);
			if (!Test.TestNotNull(TEXT("An item from the pool"), Item))
			{
				continue;
			}
			const FVector At = Item->GetActorLocation();
			Test.AddInfo(FString::Printf(TEXT("Dropped %s: %s, depth %.2f, %.2f from the drop's column"), Drop.Name,
				Item->IsFloating() ? TEXT("floats") : TEXT("does not float"), Levels.GetDepth(At), FVector::VectorPlaneProject(At - From, Up).Size()));
			Test.TestEqual(FString::Printf(TEXT("Dropped %s: floats"), Drop.Name), Item->IsFloating(), Drop.bFloats);
			if (Drop.bFloats)
			{
				// No mesh in this world (no item database): the item's pivot is at the surface itself.
				Test.TestTrue(FString::Printf(TEXT("Dropped %s: at the surface"), Drop.Name), FMath::Abs(Levels.GetDepth(At)) < 0.05);
				Test.TestTrue(FString::Printf(TEXT("Dropped %s: straight over the drop"), Drop.Name), FVector::VectorPlaneProject(At - From, Up).Size() < 0.05);
				Test.TestTrue(FString::Printf(TEXT("Dropped %s: the rest's up is the world's"), Drop.Name), FVector(Item->Rest.Up).Equals(Up, 1.0e-3));
				Test.TestTrue(FString::Printf(TEXT("Dropped %s: the rest is the place"), Drop.Name), Item->Rest.bValid && Item->Rest.Location.Equals(At, 0.01));
			}
			else
			{
				Test.TestTrue(FString::Printf(TEXT("Dropped %s: where it was dropped"), Drop.Name), At.Equals(From, 0.05));
			}
			Pool->ReturnWorldItem(Item);
		}

		// The sea goes (the configuration's water off): the same drop no longer floats. The level is read per drop,
		// never kept.
		W.Config->bEnableWaterLevel = false;
		if (AWorldItem* Item = Pool->SpawnWorldItem(FItemInstance(), AtDepth(1.5 * VS)))
		{
			Test.TestFalse(TEXT("No sea: the drop does not float"), Item->IsFloating());
			Pool->ReturnWorldItem(Item);
		}
		W.Config->bEnableWaterLevel = true;
		Pool->ClearGravityCenter();
#endif // WITH_INTERACTION_PLUGIN
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCBuoyancy_ItemFloatsPlane, "VoxelCharacter.Water.ItemFloats.Plane", VC_BUOYANCY_FLAGS)
bool FVCBuoyancy_ItemFloatsPlane::RunTest(const FString& Parameters)
{
	VCBuoyancyTestHelpers::RunItemFloats(*this, VCBuoyancyTestHelpers::EWorldKind::Plane, TEXT("VCBuoyancyPlaneWorld"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCBuoyancy_ItemFloatsIsland, "VoxelCharacter.Water.ItemFloats.Island", VC_BUOYANCY_FLAGS)
bool FVCBuoyancy_ItemFloatsIsland::RunTest(const FString& Parameters)
{
	VCBuoyancyTestHelpers::RunItemFloats(*this, VCBuoyancyTestHelpers::EWorldKind::Island, TEXT("VCBuoyancyIslandWorld"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCBuoyancy_ItemFloatsBlocks, "VoxelCharacter.Water.ItemFloats.Blocks", VC_BUOYANCY_FLAGS)
bool FVCBuoyancy_ItemFloatsBlocks::RunTest(const FString& Parameters)
{
	VCBuoyancyTestHelpers::RunItemFloats(*this, VCBuoyancyTestHelpers::EWorldKind::Blocks, TEXT("VCBuoyancyBlocksWorld"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCBuoyancy_ItemFloatsPlanet, "VoxelCharacter.Water.ItemFloats.Planet", VC_BUOYANCY_FLAGS)
bool FVCBuoyancy_ItemFloatsPlanet::RunTest(const FString& Parameters)
{
	VCBuoyancyTestHelpers::RunItemFloats(*this, VCBuoyancyTestHelpers::EWorldKind::Planet, TEXT("VCBuoyancyPlanetWorld"));
	return true;
}

#endif // WITH_AUTOMATION_TESTS
