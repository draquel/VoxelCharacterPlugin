// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AIController.h"
#include "Camera/VCUnderwaterPostProcess.h"
#include "Components/PostProcessComponent.h"
#include "Core/VCCharacterBase.h"
#include "Core/VCPlayerController.h"
#include "Effects/VCWaterEffectsComponent.h"
#include "Movement/VCTerrainQueryHelper.h"
#include "VoxelChunkManager.h"
#include "VoxelCoordinates.h"
#include "VoxelData.h"
#include "VoxelEditManager.h"
#include "VoxelEditTypes.h"
#include "VoxelWaterPropagation.h"
#include "VoxelWorldConfiguration.h"
#include "Testing/VCTestWorld.h"
#include "Testing/VoxelTestChunkWorld.h"
#include "UObject/UnrealType.h"

// ---------------------------------------------------------------------------
// The character's terrain reads and the view mode (Documentation/FINDINGS.md):
// - G86: the water reads (FVCTerrainQueryHelper::IsPositionUnderwater, QueryTerrainContext) read the voxel with its edits,
//   so a dig under the sea that flooded (the flood's flags live in the edit layer, G58/G59) is water to the pawn.
// - G84: the terrain wait covers the chunk under the feet in every kind of world, not on a planet only.
// - G87: the view mode is local to the machine that set it: not replicated, no RepNotify.
// - G133: the underwater view tint (UVCUnderwaterPostProcess, an unbound post-process volume) exists only on the pawn a
//   local player controller views through, following possession; never on an unpossessed or AI-controlled pawn.
// - W1 (the water plan): the water over a position (FVCTerrainQueryHelper::SampleWaterSurface) is the position's own voxel
//   under the surface and the voxel under the surface a little above it; the view is under water only below the sea's
//   level; the crossing effects (UVCWaterEffectsComponent) exist on every machine but a dedicated server.
// ---------------------------------------------------------------------------

namespace VCTerrainQueriesTestHelpers
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
	 * A flat seabed 5-10 voxels under the sea near the origin (the ground within 75 uu 150 uu either way), nearest first, on
	 * a 200 uu grid; the terrain decides where one lies, so the search widens in steps.
	 */
	bool FindSeabed(const VoxelTesting::FTestChunkWorld& W, FVector& OutSite)
	{
		const float VS = W.Config->VoxelSize;
		auto Depth = [&W](double X, double Y)
		{
			return W.Config->WaterLevel - W.Manager->GetGeneratedSurfacePoint(FVector(X, Y, 0.0)).Z;
		};
		for (int32 Ring = 0; Ring <= 100; ++Ring)
		{
			for (int32 j = -Ring; j <= Ring; ++j)
			{
				for (int32 i = -Ring; i <= Ring; ++i)
				{
					if (FMath::Max(FMath::Abs(i), FMath::Abs(j)) != Ring)
					{
						continue;
					}
					const double X = i * 200.0, Y = j * 200.0;
					const double D = Depth(X, Y);
					if (D < 5.0 * VS || D > 10.0 * VS)
					{
						continue;
					}
					bool bFlat = true;
					for (const FVector2D& Offset : { FVector2D(150, 0), FVector2D(-150, 0), FVector2D(0, 150), FVector2D(0, -150) })
					{
						bFlat &= FMath::Abs(Depth(X + Offset.X, Y + Offset.Y) - D) <= 75.0;
					}
					if (bFlat)
					{
						OutSite = W.Manager->GetGeneratedSurfacePoint(FVector(X, Y, 0.0));
						return true;
					}
				}
			}
		}
		return false;
	}
}

#define VC_TERRAIN_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// G86: a pawn in a flooded dig is under water.
// A plane with a sea at Z 0 streams around a flat seabed; a player's brush digs into it and the water propagation floods the
// dig. The dug voxels read dry rock from the generated data (what the reads used before) and water with their edits:
// IsPositionUnderwater and QueryTerrainContext say under water, with the sea's depth.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCTerrain_FloodedDigIsWater, "VoxelCharacter.Terrain.FloodedDigIsWater", VC_TERRAIN_FLAGS)
bool FVCTerrain_FloodedDigIsWater::RunTest(const FString& Parameters)
{
	using namespace VCTerrainQueriesTestHelpers;
	VoxelTesting::FTestChunkWorld W;
	W.Create(TEXT("VCFloodedDigWorld"));
	W.ConfigureSmallPlane();
	W.Config->bEnableWaterLevel = true;
	W.Config->WaterLevel = 0.0f;
	W.StartManager();

	FVector Site;
	if (!FindSeabed(W, Site))
	{
		AddError(TEXT("No flat seabed 5-10 voxels under the sea within 20,000 uu of the origin"));
		return true;
	}
	const float VS = W.Config->VoxelSize;
	W.SetViewer(Site + FVector(0.0, 0.0, 300.0));
	TestTrue(TEXT("The world settles"), SettleWithWater(W));
	if (!TestNotNull(TEXT("Water propagation runs with bEnableWaterLevel"), W.Manager->GetWaterPropagation()))
	{
		return true;
	}

	// A player's dig on the seabed (a sphere brush of 2.5 voxels, full strength): it opens dry air under the sea, and the
	// water propagation floods it.
	UVoxelEditManager* Edits = W.Manager->GetEditManager();
	Edits->SetEditSource(EEditSource::Player);
	FVoxelBrushParams Brush;
	Brush.Shape = EVoxelBrushShape::Sphere;
	Brush.Radius = 2.5f * VS;
	Brush.Strength = 1.0f;
	Brush.FalloffType = EVoxelBrushFalloff::Sharp;
	Brush.DensityDelta = 255;
	TestTrue(TEXT("The dig edits voxels"), Edits->ApplyBrushEdit(Site, Brush, EEditMode::Subtract) > 0);
	TestTrue(TEXT("The world settles after the dig"), SettleWithWater(W));

	// Every voxel the dig turned from rock to air: flooded in the edit layer, dry rock in the generated data.
	int32 Dug = 0, Flooded = 0, ReadsWater = 0, ContextWater = 0, GeneratedDry = 0;
	FVector Sample = Site;
	const FIntVector Centre = FVoxelCoordinates::WorldToVoxel(Site - W.Config->WorldOrigin, VS);
	for (int32 Z = -3; Z <= 3; ++Z)
	for (int32 Y = -3; Y <= 3; ++Y)
	for (int32 X = -3; X <= 3; ++X)
	{
		const FVector P = W.Manager->GlobalVoxelToWorldCentre(Centre + FIntVector(X, Y, Z));
		const FVoxelData Generated = W.Manager->GetGeneratedVoxelAt(P);
		const FVoxelData Merged = W.Manager->GetVoxelAt(P);
		if (!Generated.IsSolid() || !Merged.IsAir())
		{
			continue;
		}
		++Dug;
		if (!Merged.HasWaterFlag())
		{
			continue;
		}
		++Flooded;
		Sample = P;
		GeneratedDry += Generated.HasWaterFlag() ? 0 : 1;
		float Depth = 0.f;
		ReadsWater += (FVCTerrainQueryHelper::IsPositionUnderwater(W.World, P, Depth) && Depth > 0.f) ? 1 : 0;
		const FVCTerrainContext Context = FVCTerrainQueryHelper::QueryTerrainContext(W.World, P, FVector::UpVector);
		ContextWater += (Context.bIsUnderwater && Context.WaterDepth > 0.f) ? 1 : 0;
	}
	TestTrue(FString::Printf(TEXT("The dig opened rock under the sea (%d voxels)"), Dug), Dug > 0);
	TestEqual(TEXT("Every dug voxel flooded"), Flooded, Dug);
	TestEqual(TEXT("The generated data reads each flooded voxel dry (the read before G86)"), GeneratedDry, Flooded);
	TestEqual(TEXT("IsPositionUnderwater: each flooded voxel is under water, with a depth"), ReadsWater, Flooded);
	TestEqual(TEXT("QueryTerrainContext: each flooded voxel is under water, with a depth"), ContextWater, Flooded);
	AddInfo(FString::Printf(TEXT("Seabed (%.0f, %.0f, %.0f): %d voxels dug, %d flooded; sampled (%.0f, %.0f, %.0f)"),
		Site.X, Site.Y, Site.Z, Dug, Flooded, Sample.X, Sample.Y, Sample.Z));
	return true;
}

// ---------------------------------------------------------------------------
// G84: the terrain wait covers the chunk under the feet on a heightmap world.
// A pawn in the lower half of its chunk (its ground may be in the layer below) waits for that chunk too; in the upper half
// the chunk under its feet is its own and the wait is the 3 x 3 grid. A planet waits for the chunk under the feet as before.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCTerrain_WaitChunkUnderFeet, "VoxelCharacter.Terrain.WaitChunkUnderFeet", VC_TERRAIN_FLAGS)
bool FVCTerrain_WaitChunkUnderFeet::RunTest(const FString& Parameters)
{
	{
		VoxelTesting::FTestChunkWorld Plane;
		Plane.Create(TEXT("VCWaitChunksPlane"));
		Plane.ConfigureSmallPlane();
		Plane.Config->WorldOrigin = FVector(3112.5, -1937.5, -2187.5); // off the chunk grid, so the layers are the origin's
		Plane.StartManager();
		const float ChunkWorldSize = Plane.Config->GetChunkWorldSize();
		const FVector Origin = Plane.Config->WorldOrigin;

		// A pawn 200 uu above the bottom of chunk layer 1 (lower half).
		const FVector Low = Origin + FVector(700.0, 900.0, ChunkWorldSize + 200.0);
		const FIntVector LowChunk = Plane.Manager->WorldToChunkCoord(Low);
		const TArray<FIntVector> LowWait = FVCTerrainQueryHelper::GetTerrainWaitChunks(*Plane.Manager, Low, 1);
		TestEqual(TEXT("Plane, lower half: the pawn is in layer 1"), LowChunk.Z, 1);
		TestEqual(TEXT("Plane, lower half: 9 chunks of the grid and the one under the feet"), LowWait.Num(), 10);
		TestTrue(TEXT("Plane, lower half: the chunk under the feet (layer 0) is waited for"), LowWait.Contains(LowChunk - FIntVector(0, 0, 1)));
		int32 OwnLayer = 0;
		for (const FIntVector& Chunk : LowWait)
		{
			OwnLayer += (Chunk.Z == LowChunk.Z && FMath::Abs(Chunk.X - LowChunk.X) <= 1 && FMath::Abs(Chunk.Y - LowChunk.Y) <= 1) ? 1 : 0;
		}
		TestEqual(TEXT("Plane, lower half: the 3 x 3 grid at the pawn's layer"), OwnLayer, 9);

		// 1,200 uu above the bottom (upper half): the chunk under the feet is the pawn's own.
		const FVector High = Origin + FVector(700.0, 900.0, ChunkWorldSize + 1200.0);
		const TArray<FIntVector> HighWait = FVCTerrainQueryHelper::GetTerrainWaitChunks(*Plane.Manager, High, 1);
		TestEqual(TEXT("Plane, upper half: the 3 x 3 grid only"), HighWait.Num(), 9);
		TestFalse(TEXT("Plane, upper half: no chunk of the layer below"), HighWait.Contains(Plane.Manager->WorldToChunkCoord(High) - FIntVector(0, 0, 1)));
	}
	{
		VoxelTesting::FTestChunkWorld Planet;
		Planet.Create(TEXT("VCWaitChunksPlanet"));
		Planet.ConfigureSmallPlanet();
		Planet.StartManager();
		// On the +Z pole (up is +Z there) a pawn in the lower half of its chunk: the chunk below along the radius is waited for.
		const float ChunkWorldSize = Planet.Config->GetChunkWorldSize();
		const double Bottom = FMath::FloorToDouble(Planet.Config->WorldRadius / ChunkWorldSize) * ChunkWorldSize;
		const FVector Pawn(10.0, 10.0, Bottom + 200.0);
		const TArray<FIntVector> Wait = FVCTerrainQueryHelper::GetTerrainWaitChunks(*Planet.Manager, Pawn, 1);
		TestTrue(TEXT("Planet: the chunk under the feet is waited for"),
			Wait.Contains(Planet.Manager->WorldToChunkCoord(Pawn - FVector(0.0, 0.0, 0.5 * ChunkWorldSize))));
		TestEqual(TEXT("Planet, at the pole: 9 chunks of the grid and the one under the feet"), Wait.Num(), 10);
	}
	return true;
}

// ---------------------------------------------------------------------------
// G87: the view mode is the local player's, not replicated (only the locally controlled pawn draws first person).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCViewMode_NotReplicated, "VoxelCharacter.ViewMode.NotReplicated", VC_TERRAIN_FLAGS)
bool FVCViewMode_NotReplicated::RunTest(const FString& Parameters)
{
	const UClass* Class = AVCCharacterBase::StaticClass();
	const FProperty* ViewMode = FindFProperty<FProperty>(Class, GET_MEMBER_NAME_CHECKED(AVCCharacterBase, CurrentViewMode));
	if (!TestNotNull(TEXT("AVCCharacterBase::CurrentViewMode is reflected"), ViewMode))
	{
		return true;
	}
	TestFalse(TEXT("CurrentViewMode is not replicated"), ViewMode->HasAnyPropertyFlags(CPF_Net | CPF_RepNotify));
	TestNull(TEXT("No OnRep_ViewMode"), Class->FindFunctionByName(TEXT("OnRep_ViewMode")));
	const FProperty* Corpse = FindFProperty<FProperty>(Class, TEXT("bIsCorpse"));
	TestTrue(TEXT("The corpse state still replicates (the class's replication is intact)"), Corpse && Corpse->HasAnyPropertyFlags(CPF_Net));
	return true;
}

// ---------------------------------------------------------------------------
// G133: the underwater view tint is the local player's. Its volume is unbound (it tints every view in its world), so the
// character creates it only on the pawn a local player controller views through, at BeginPlay or when such a controller
// arrives, and destroys it (volume included) when the controller goes. The controller is linked and NotifyControllerChanged
// called by hand, as the engine does from PossessedBy, UnPossessed and OnRep_Controller; the full possession path runs in
// PIE (APlayerController::Possess sets up input, which has no config here).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCUnderwater_LocalPlayerOnly, "VoxelCharacter.Underwater.LocalPlayerOnly", VC_TERRAIN_FLAGS)
bool FVCUnderwater_LocalPlayerOnly::RunTest(const FString& Parameters)
{
	VCTesting::FPlayWorld W(TEXT("VCUnderwaterLocalOnlyWorld"), /*bDestroyActorsFirst=*/true);

	auto Spawn = [&W](AController* ControllerBeforeBeginPlay) -> AVCCharacterBase*
	{
		const FTransform Stand(FVector::ZeroVector);
		AVCCharacterBase* Character = W.World->SpawnActorDeferred<AVCCharacterBase>(AVCCharacterBase::StaticClass(), Stand, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Character)
		{
			Character->bWaitForTerrain = false;
			if (ControllerBeforeBeginPlay)
			{
				Character->Controller = ControllerBeforeBeginPlay;
				ControllerBeforeBeginPlay->SetPawn(Character);
			}
			Character->FinishSpawning(Stand);
		}
		return Character;
	};
	auto Link = [](AVCCharacterBase* Character, AController* Controller)
	{
		if (AController* Old = Character->GetController())
		{
			Old->SetPawn(nullptr);
		}
		Character->Controller = Controller;
		if (Controller)
		{
			Controller->SetPawn(Character);
		}
		static_cast<APawn*>(Character)->NotifyControllerChanged(); // public on APawn, protected on the character
	};
	auto Tint = [](const AActor* Character) { return Character->FindComponentByClass<UVCUnderwaterPostProcess>(); };
	auto Volumes = [](const AActor* Character)
	{
		TInlineComponentArray<UPostProcessComponent*> Found;
		Character->GetComponents(Found);
		return Found.Num();
	};

	AVCCharacterBase* Nobody = Spawn(nullptr);
	AVCCharacterBase* Bot = Spawn(nullptr);
	AVCCharacterBase* Player = Spawn(nullptr);
	AVCPlayerController* PC = W.World->SpawnActor<AVCPlayerController>();
	AAIController* AI = W.World->SpawnActor<AAIController>();
	if (!TestNotNull(TEXT("Nobody"), Nobody) || !TestNotNull(TEXT("Bot"), Bot) || !TestNotNull(TEXT("Player"), Player)
		|| !TestNotNull(TEXT("Player controller"), PC) || !TestNotNull(TEXT("AI controller"), AI))
	{
		return false;
	}
	// A world with no net driver counts a player controller as local only with a ULocalPlayer (IsLocalController, UE 5.8);
	// the PIE login marks a client's controller local the same way.
	PC->SetAsLocalPlayerController();
	TestTrue(TEXT("The player controller is local here"), PC->IsLocalController());

	// No controller (another player's character on a client, a body nobody possesses): no tint, no volume.
	TestNull(TEXT("No controller: no tint"), Tint(Nobody));
	TestEqual(TEXT("No controller: no volume"), Volumes(Nobody), 0);
	TestFalse(TEXT("No controller: not a local player view"), UVCUnderwaterPostProcess::IsLocalPlayerView(Nobody));

	// An AI controller is local on its machine (IsLocallyControlled holds), but no player views through it: no tint.
	Link(Bot, AI);
	TestTrue(TEXT("AI: the pawn counts as locally controlled"), Bot->IsLocallyControlled());
	TestFalse(TEXT("AI: not a local player view"), UVCUnderwaterPostProcess::IsLocalPlayerView(Bot));
	TestNull(TEXT("AI: no tint"), Tint(Bot));
	TestEqual(TEXT("AI: no volume"), Volumes(Bot), 0);

	// A local player controller arriving after BeginPlay (possession, or the owning client's OnRep_Controller): the tint
	// and its unbound volume appear; when it goes (unpossessed), both go.
	TestNull(TEXT("Player, before the controller: no tint"), Tint(Player));
	Link(Player, PC);
	TestTrue(TEXT("Player: a local player view"), UVCUnderwaterPostProcess::IsLocalPlayerView(Player));
	UVCUnderwaterPostProcess* Effect = Tint(Player);
	TestNotNull(TEXT("Player: the tint, on possession"), Effect);
	// The view volume, and the water volume when the project names an underwater material (W1; the plugin alone has none).
	const int32 OwnVolumes = (Effect && Effect->HasWaterView()) ? 2 : 1;
	TestEqual(TEXT("Player: the effect's unbound volume(s)"), Volumes(Player), OwnVolumes);
	Link(Player, nullptr);
	TestNull(TEXT("Player, unpossessed: the tint is gone"), Tint(Player));
	TestEqual(TEXT("Player, unpossessed: the volume(s) went with it"), Volumes(Player), 0);
	TestTrue(TEXT("Player, unpossessed: the component is destroyed, not merely detached"), !IsValid(Effect));

	// Possessed again: created again (a respawn's possession, a pawn swap); then handed to the AI: gone again.
	Link(Player, PC);
	TestNotNull(TEXT("Player, possessed again: the tint again"), Tint(Player));
	Link(Player, AI);
	TestNull(TEXT("Player, handed to an AI controller: no tint"), Tint(Player));
	TestEqual(TEXT("Player, handed to an AI controller: no volume"), Volumes(Player), 0);

	// Possessed before BeginPlay (the usual spawn: the game mode possesses the pawn it spawned): BeginPlay creates it.
	AVCCharacterBase* Early = Spawn(PC);
	if (TestNotNull(TEXT("Early"), Early))
	{
		TestNotNull(TEXT("Possessed before BeginPlay: the tint from BeginPlay"), Tint(Early));
		TestEqual(TEXT("Possessed before BeginPlay: the effect's volume(s)"), Volumes(Early), OwnVolumes);
		Link(Early, nullptr);
	}
	return true;
}

// ---------------------------------------------------------------------------
// W1: the water over a position. A plane with a sea at Z 0 streams around a flat seabed. Under the surface the sample is
// the position's own voxel (water over the seabed, not the rock under it); a little above the surface it is the voxel
// just under the surface below, within the reach only; the depth and the surface point are the sea's. "Submerged" needs
// both: the voxel that holds the water line is flagged top to bottom, and the flag alone called a camera above the line
// under water (the depth guard).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCWater_SurfaceSample, "VoxelCharacter.Water.SurfaceSample", VC_TERRAIN_FLAGS)
bool FVCWater_SurfaceSample::RunTest(const FString& Parameters)
{
	using namespace VCTerrainQueriesTestHelpers;
	VoxelTesting::FTestChunkWorld W;
	W.Create(TEXT("VCWaterSurfaceSampleWorld"));
	W.ConfigureSmallPlane();
	W.Config->bEnableWaterLevel = true;
	W.Config->WaterLevel = 0.0f;
	W.StartManager();

	FVector Site;
	if (!FindSeabed(W, Site))
	{
		AddError(TEXT("No flat seabed 5-10 voxels under the sea within 20,000 uu of the origin"));
		return true;
	}
	W.SetViewer(Site + FVector(0.0, 0.0, 300.0));
	TestTrue(TEXT("The world settles"), SettleWithWater(W));
	const float VS = W.Config->VoxelSize;

	auto At = [&Site](double Z) { return FVector(Site.X, Site.Y, Z); };
	FVCWaterSurfaceSample Sample;

	// Under the surface, in the water over the seabed.
	TestTrue(TEXT("The world has a sea"), FVCTerrainQueryHelper::SampleWaterSurface(W.World, At(-1.5 * VS), 40.f, Sample));
	TestTrue(TEXT("Under the surface: water"), Sample.bWater);
	TestTrue(TEXT("Under the surface: the depth is the sea's"), FMath::IsNearlyEqual(Sample.Depth, 1.5 * VS, 0.01));
	TestTrue(TEXT("Under the surface: submerged"), Sample.IsSubmerged());
	TestTrue(TEXT("The up is +Z on a plane"), Sample.Up.Equals(FVector::UpVector));
	TestTrue(TEXT("The surface point is on the sea's plane"), FMath::IsNearlyZero(Sample.SurfacePoint(At(-1.5 * VS)).Z, 0.01));

	// A little above the surface over water: at the water within the reach, never submerged; out of reach, not water.
	FVCTerrainQueryHelper::SampleWaterSurface(W.World, At(20.0), 40.f, Sample);
	TestTrue(TEXT("20 uu above, reach 40: at the water"), Sample.bWater);
	TestFalse(TEXT("20 uu above: not submerged"), Sample.IsSubmerged());
	TestTrue(TEXT("20 uu above: a negative depth"), FMath::IsNearlyEqual(Sample.Depth, -20.0, 0.01));
	FVCTerrainQueryHelper::SampleWaterSurface(W.World, At(20.0), 10.f, Sample);
	TestFalse(TEXT("20 uu above, reach 10: not at the water"), Sample.bWater);

	// Just above the line the sample is at the water and not submerged (the depth guard); just under it, submerged.
	float FlagDepth = 0.f;
	TestTrue(TEXT("Just under the line: the flag reads water"), FVCTerrainQueryHelper::IsPositionUnderwater(W.World, At(-1.0), FlagDepth));
	FVCTerrainQueryHelper::SampleWaterSurface(W.World, At(1.0), 40.f, Sample);
	TestTrue(TEXT("Just above the line: at the water"), Sample.bWater);
	TestFalse(TEXT("Just above the line: not submerged (the depth guard)"), Sample.IsSubmerged());
	FVCTerrainQueryHelper::SampleWaterSurface(W.World, At(-1.0), 40.f, Sample);
	TestTrue(TEXT("Just under the line: submerged"), Sample.IsSubmerged());

	// In the seabed's rock, under the sea's level: not water.
	FVCTerrainQueryHelper::SampleWaterSurface(W.World, Site - FVector(0.0, 0.0, 1.5 * VS), 40.f, Sample);
	TestTrue(TEXT("In the seabed: under the sea's level"), Sample.Depth > 0.0);
	TestFalse(TEXT("In the seabed: not water"), Sample.bWater);
	TestFalse(TEXT("In the seabed: not submerged"), Sample.IsSubmerged());

	// No sea: the sample says so.
	W.Config->bEnableWaterLevel = false;
	TestFalse(TEXT("No sea: no sample"), FVCTerrainQueryHelper::SampleWaterSurface(W.World, At(-1.5 * VS), 40.f, Sample));
	W.Config->bEnableWaterLevel = true;
	return true;
}

// ---------------------------------------------------------------------------
// W1: the crossing effects' rules. Pure: where they play (not on a dedicated server), how hard a splash is by the speed
// into the water, when a swimmer leaves a wake, the view's tint by the camera's depth. And the component is on a
// character in a world that is not a dedicated server, silent until the character swims.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCWater_EffectsRules, "VoxelCharacter.Water.EffectsRules", VC_TERRAIN_FLAGS)
bool FVCWater_EffectsRules::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("A dedicated server plays no effects"), UVCWaterEffectsComponent::PlaysIn(NM_DedicatedServer));
	TestTrue(TEXT("A client plays them"), UVCWaterEffectsComponent::PlaysIn(NM_Client));
	TestTrue(TEXT("A listen server plays them"), UVCWaterEffectsComponent::PlaysIn(NM_ListenServer));
	TestTrue(TEXT("Standalone plays them"), UVCWaterEffectsComponent::PlaysIn(NM_Standalone));

	TestEqual(TEXT("Wading in: the least splash"), UVCWaterEffectsComponent::SplashStrength(0.f, 900.f, 0.35f), 0.35f);
	TestEqual(TEXT("Rising out of the water: the least splash"), UVCWaterEffectsComponent::SplashStrength(-300.f, 900.f, 0.35f), 0.35f);
	TestEqual(TEXT("At the full speed: a full splash"), UVCWaterEffectsComponent::SplashStrength(900.f, 900.f, 0.35f), 1.f);
	TestEqual(TEXT("Faster: still full"), UVCWaterEffectsComponent::SplashStrength(4000.f, 900.f, 0.35f), 1.f);
	const float Half = UVCWaterEffectsComponent::SplashStrength(450.f, 900.f, 0.35f);
	TestTrue(TEXT("Half the speed: half way"), FMath::IsNearlyEqual(Half, 0.675f, 1.0e-4f));

	TestTrue(TEXT("Swimming at the surface: a wake"), UVCWaterEffectsComponent::LeavesWake(200.f, 10.0, 60.f, 45.f));
	TestTrue(TEXT("Head above the surface: a wake"), UVCWaterEffectsComponent::LeavesWake(200.f, -30.0, 60.f, 45.f));
	TestFalse(TEXT("Treading water: no wake"), UVCWaterEffectsComponent::LeavesWake(20.f, 10.0, 60.f, 45.f));
	TestFalse(TEXT("Diving: no wake"), UVCWaterEffectsComponent::LeavesWake(200.f, 300.0, 60.f, 45.f));

	const FLinearColor Shallow(0.7f, 0.85f, 0.92f), Deep(0.35f, 0.62f, 0.8f);
	TestTrue(TEXT("At the surface: the shallow tint"), UVCUnderwaterPostProcess::TintAtDepth(0.0, Shallow, Deep, 600.f).Equals(Shallow));
	TestTrue(TEXT("Above the surface: the shallow tint"), UVCUnderwaterPostProcess::TintAtDepth(-50.0, Shallow, Deep, 600.f).Equals(Shallow));
	TestTrue(TEXT("At the full depth: the deep tint"), UVCUnderwaterPostProcess::TintAtDepth(600.0, Shallow, Deep, 600.f).Equals(Deep));
	TestTrue(TEXT("Deeper: still the deep tint"), UVCUnderwaterPostProcess::TintAtDepth(5000.0, Shallow, Deep, 600.f).Equals(Deep));

	// The component on a character: there in a standalone world, with nothing played and no wake before any swim.
	VCTesting::FPlayWorld W(TEXT("VCWaterEffectsWorld"), /*bDestroyActorsFirst=*/true);
	const FTransform Stand(FVector::ZeroVector);
	AVCCharacterBase* Character = W.World->SpawnActorDeferred<AVCCharacterBase>(AVCCharacterBase::StaticClass(), Stand, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Character"), Character))
	{
		return false;
	}
	Character->bWaitForTerrain = false;
	Character->FinishSpawning(Stand);
	TestTrue(TEXT("The test world is not a dedicated server"), UVCWaterEffectsComponent::PlaysIn(W.World->GetNetMode()));
	const UVCWaterEffectsComponent* Effects = Character->FindComponentByClass<UVCWaterEffectsComponent>();
	if (TestNotNull(TEXT("The character has the water effects"), Effects))
	{
		TestEqual(TEXT("No splash before any swim"), Effects->GetSplashCount(), 0);
		TestFalse(TEXT("No wake before any swim"), Effects->IsWakeActive());
		TestFalse(TEXT("The effects do not tick on land"), Effects->IsComponentTickEnabled());
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
