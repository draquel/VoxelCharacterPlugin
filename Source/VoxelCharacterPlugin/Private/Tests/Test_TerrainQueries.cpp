// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AIController.h"
#include "Camera/VCUnderwaterPostProcess.h"
#include "Components/PostProcessComponent.h"
#include "Core/VCCharacterBase.h"
#include "Core/VCPlayerController.h"
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
	TestEqual(TEXT("Player: one unbound volume"), Volumes(Player), 1);
	Link(Player, nullptr);
	TestNull(TEXT("Player, unpossessed: the tint is gone"), Tint(Player));
	TestEqual(TEXT("Player, unpossessed: the volume went with it"), Volumes(Player), 0);
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
		TestEqual(TEXT("Possessed before BeginPlay: one volume"), Volumes(Early), 1);
		Link(Early, nullptr);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
