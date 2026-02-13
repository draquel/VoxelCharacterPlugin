// Copyright Daniel Raquel. All Rights Reserved.

#include "Movement/VCVoxelNavigationHelper.h"
#include "Movement/VCMovementComponent.h"
#include "VoxelChunkManager.h"
#include "VoxelWorldConfiguration.h"
#include "VoxelCoordinates.h"
#include "VoxelData.h"
#include "IVoxelWorldMode.h"
#include "VoxelCharacterPlugin.h"
#include "EngineUtils.h"

TWeakObjectPtr<UVoxelChunkManager> FVCVoxelNavigationHelper::CachedChunkManager;

// ---------------------------------------------------------------------------
// Find Chunk Manager
// ---------------------------------------------------------------------------

UVoxelChunkManager* FVCVoxelNavigationHelper::FindChunkManager(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	// Validate cached manager belongs to the same world (critical for PIE)
	if (CachedChunkManager.IsValid() && CachedChunkManager->GetWorld() == World)
	{
		return CachedChunkManager.Get();
	}

	// Cache miss or wrong world — clear and re-search
	CachedChunkManager.Reset();

	// Search all actors for one with a VoxelChunkManager component
	for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (UVoxelChunkManager* ChunkMgr = It->FindComponentByClass<UVoxelChunkManager>())
		{
			if (ChunkMgr->IsInitialized())
			{
				CachedChunkManager = ChunkMgr;
				return ChunkMgr;
			}
		}
	}

	return nullptr;
}

// ---------------------------------------------------------------------------
// Query Terrain Context
// ---------------------------------------------------------------------------

FVoxelTerrainContext FVCVoxelNavigationHelper::QueryTerrainContext(const UWorld* World, const FVector& Location)
{
	FVoxelTerrainContext Context;

	UVoxelChunkManager* ChunkMgr = FindChunkManager(World);
	if (!ChunkMgr)
	{
		return Context;
	}

	const UVoxelWorldConfiguration* Config = ChunkMgr->GetConfiguration();
	if (!Config)
	{
		return Context;
	}

	// Get voxel data at feet position (sample slightly below to catch surface)
	const FVector SamplePos = Location - FVector(0.f, 0.f, 10.f);
	const FVoxelData VoxelAtFeet = ChunkMgr->GetVoxelAtWorldPosition(SamplePos);

	// Material and surface type
	Context.VoxelMaterialID = VoxelAtFeet.MaterialID;
	Context.SurfaceType = UVCMovementComponent::MaterialIDToSurfaceType(VoxelAtFeet.MaterialID);
	Context.FrictionMultiplier = UVCMovementComponent::GetSurfaceFriction(Context.SurfaceType);

	// Surface hardness based on material
	switch (Context.SurfaceType)
	{
	case EVoxelSurfaceType::Stone:
	case EVoxelSurfaceType::Metal:
		Context.SurfaceHardness = 1.0f;
		break;
	case EVoxelSurfaceType::Dirt:
	case EVoxelSurfaceType::Grass:
		Context.SurfaceHardness = 0.5f;
		break;
	case EVoxelSurfaceType::Sand:
	case EVoxelSurfaceType::Snow:
		Context.SurfaceHardness = 0.3f;
		break;
	case EVoxelSurfaceType::Mud:
		Context.SurfaceHardness = 0.2f;
		break;
	default:
		Context.SurfaceHardness = 1.0f;
		break;
	}

	// Water state
	if (Config->bEnableWaterLevel)
	{
		const float WaterSurface = Config->WaterLevel + Config->WorldOrigin.Z;
		if (Location.Z < WaterSurface)
		{
			Context.bIsUnderwater = true;
			Context.WaterDepth = WaterSurface - Location.Z;
		}
	}

	// Chunk coordinate
	const FVector RelativePos = Location - Config->WorldOrigin;
	Context.CurrentChunkCoord = FVoxelCoordinates::WorldToChunk(
		RelativePos, Config->ChunkSize, Config->VoxelSize);

	return Context;
}

// ---------------------------------------------------------------------------
// Material Lookup
// ---------------------------------------------------------------------------

uint8 FVCVoxelNavigationHelper::GetVoxelMaterialAtLocation(const UWorld* World, const FVector& Location)
{
	UVoxelChunkManager* ChunkMgr = FindChunkManager(World);
	if (!ChunkMgr)
	{
		return 0;
	}

	return ChunkMgr->GetVoxelAtWorldPosition(Location).MaterialID;
}

// ---------------------------------------------------------------------------
// Water Check
// ---------------------------------------------------------------------------

bool FVCVoxelNavigationHelper::IsPositionUnderwater(const UWorld* World, const FVector& Location, float& OutWaterDepth)
{
	OutWaterDepth = 0.f;

	UVoxelChunkManager* ChunkMgr = FindChunkManager(World);
	if (!ChunkMgr)
	{
		return false;
	}

	const UVoxelWorldConfiguration* Config = ChunkMgr->GetConfiguration();
	if (!Config || !Config->bEnableWaterLevel)
	{
		return false;
	}

	const float WaterSurface = Config->WaterLevel + Config->WorldOrigin.Z;
	if (Location.Z < WaterSurface)
	{
		OutWaterDepth = WaterSurface - Location.Z;
		return true;
	}

	return false;
}

// ---------------------------------------------------------------------------
// Find Spawnable Position
// ---------------------------------------------------------------------------

bool FVCVoxelNavigationHelper::FindSpawnablePosition(
	const UWorld* World,
	const FVector& NearPosition,
	FVector& OutPosition,
	float MaxSearchRadius)
{
	UVoxelChunkManager* ChunkMgr = FindChunkManager(World);
	if (!ChunkMgr)
	{
		return false;
	}

	const IVoxelWorldMode* WorldMode = ChunkMgr->GetWorldMode();
	const UVoxelWorldConfiguration* Config = ChunkMgr->GetConfiguration();
	if (!WorldMode || !Config)
	{
		return false;
	}

	const float ChunkWorldSize = Config->ChunkSize * Config->VoxelSize;
	const float WaterLevel = Config->WaterLevel;
	const bool bHasWater = Config->bEnableWaterLevel;

	// Helper: query terrain height and check if above water
	auto IsAboveWater = [&](float X, float Y, float& OutTerrainHeight) -> bool
	{
		OutTerrainHeight = WorldMode->GetTerrainHeightAt(X, Y, Config->NoiseParams);
		return !bHasWater || OutTerrainHeight > WaterLevel;
	};

	// Try the requested position first
	float TerrainHeight = 0.f;
	if (IsAboveWater(NearPosition.X, NearPosition.Y, TerrainHeight))
	{
		OutPosition = FVector(NearPosition.X, NearPosition.Y, TerrainHeight);
		UE_LOG(LogVoxelCharacter, Log,
			TEXT("FindSpawnablePosition: Position (%.0f, %.0f) is above water at Z=%.0f"),
			NearPosition.X, NearPosition.Y, TerrainHeight);
		return true;
	}

	UE_LOG(LogVoxelCharacter, Log,
		TEXT("FindSpawnablePosition: Position (%.0f, %.0f) is underwater (terrain Z=%.0f, water=%.0f). Searching outward..."),
		NearPosition.X, NearPosition.Y, TerrainHeight, WaterLevel);

	// Spiral search outward at ChunkWorldSize intervals in 8 directions
	static const FVector2D Directions[] =
	{
		FVector2D( 1,  0), FVector2D( 1,  1), FVector2D( 0,  1), FVector2D(-1,  1),
		FVector2D(-1,  0), FVector2D(-1, -1), FVector2D( 0, -1), FVector2D( 1, -1)
	};

	const float Step = ChunkWorldSize;
	const int32 MaxRings = FMath::CeilToInt(MaxSearchRadius / Step);

	for (int32 Ring = 1; Ring <= MaxRings; ++Ring)
	{
		const float Radius = Ring * Step;

		for (const FVector2D& Dir : Directions)
		{
			const float SampleX = NearPosition.X + Dir.X * Radius;
			const float SampleY = NearPosition.Y + Dir.Y * Radius;

			if (IsAboveWater(SampleX, SampleY, TerrainHeight))
			{
				OutPosition = FVector(SampleX, SampleY, TerrainHeight);
				UE_LOG(LogVoxelCharacter, Log,
					TEXT("FindSpawnablePosition: Found land at (%.0f, %.0f) Z=%.0f, ring %d (%.0f units away)"),
					SampleX, SampleY, TerrainHeight, Ring, Radius);
				return true;
			}
		}
	}

	UE_LOG(LogVoxelCharacter, Warning,
		TEXT("FindSpawnablePosition: No land found within %.0f units of (%.0f, %.0f)"),
		MaxSearchRadius, NearPosition.X, NearPosition.Y);
	return false;
}

// ---------------------------------------------------------------------------
// Cache Management
// ---------------------------------------------------------------------------

void FVCVoxelNavigationHelper::ClearCache()
{
	CachedChunkManager.Reset();
}
