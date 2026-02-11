// Copyright Daniel Raquel. All Rights Reserved.

#include "Movement/VCVoxelNavigationHelper.h"
#include "Movement/VCMovementComponent.h"
#include "VoxelChunkManager.h"
#include "VoxelWorldConfiguration.h"
#include "VoxelCoordinates.h"
#include "VoxelData.h"
#include "VoxelCharacterPlugin.h"
#include "EngineUtils.h"

TWeakObjectPtr<UVoxelChunkManager> FVCVoxelNavigationHelper::CachedChunkManager;

// ---------------------------------------------------------------------------
// Find Chunk Manager
// ---------------------------------------------------------------------------

UVoxelChunkManager* FVCVoxelNavigationHelper::FindChunkManager(const UWorld* World)
{
	if (CachedChunkManager.IsValid())
	{
		return CachedChunkManager.Get();
	}

	if (!World)
	{
		return nullptr;
	}

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
// Cache Management
// ---------------------------------------------------------------------------

void FVCVoxelNavigationHelper::ClearCache()
{
	CachedChunkManager.Reset();
}
