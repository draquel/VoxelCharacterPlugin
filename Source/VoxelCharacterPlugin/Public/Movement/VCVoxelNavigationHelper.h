// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/VCTypes.h"
#include "VoxelSeaSurface.h"

class UVoxelChunkManager;
class UVoxelWorldConfiguration;

/**
 * Static utility class for voxel world queries used by the character system.
 *
 * Provides helpers for terrain context lookups, coordinate conversion,
 * and voxel material queries. All methods are static and thread-safe
 * for game-thread use.
 */
class VOXELCHARACTERPLUGIN_API FVCVoxelNavigationHelper
{
public:
	/**
	 * Find the VoxelChunkManager component in the world.
	 * Caches the result internally for fast repeated lookups.
	 *
	 * @param World World context
	 * @return Chunk manager or nullptr if not found
	 */
	static UVoxelChunkManager* FindChunkManager(const UWorld* World);

	/**
	 * Query full terrain context at a world position.
	 * Populates surface type, friction, water state, and chunk coordinate. The water depth is the sea's
	 * (UVoxelChunkManager::GetSeaSurface): along the radius on a planet (spherical SP-E1).
	 *
	 * @param World World context
	 * @param Location World-space position to query (typically character feet)
	 * @param Up Local up at Location: the surface material is sampled a little below it along -Up (radial on a planet)
	 * @return Terrain context with all fields populated
	 */
	static FVoxelTerrainContext QueryTerrainContext(const UWorld* World, const FVector& Location, const FVector& Up = FVector::UpVector);

	/**
	 * Get the raw voxel material ID at a world position.
	 *
	 * @param World World context
	 * @param Location World-space position
	 * @return Material ID (0 if unloaded or air)
	 */
	static uint8 GetVoxelMaterialAtLocation(const UWorld* World, const FVector& Location);

	/**
	 * Check if a world position is underwater: in a water-flagged voxel, with its depth below the sea
	 * (UVoxelChunkManager::GetSeaSurface: the WaterLevel plane, or the sphere of WaterRadius on a planet, SP-E1).
	 *
	 * @param World World context
	 * @param Location World-space position
	 * @param OutWaterDepth Depth below water surface (0 if above)
	 * @return True if position is in water
	 */
	static bool IsPositionUnderwater(const UWorld* World, const FVector& Location, float& OutWaterDepth);

	/**
	 * Find a valid spawn position on terrain above water level.
	 *
	 * Uses the voxel world mode's deterministic terrain height query (pure math
	 * from noise parameters) — does not require chunks to be loaded.
	 * If the given position is over water, performs a spiral search outward
	 * at chunk-sized intervals to find the nearest above-water terrain.
	 * Only points inside the world's bounds count (an island's R + falloff): a position outside
	 * searches from the nearest point a chunk inside the edge (world modes review WM-I3).
	 * On a planet the search is by direction (spherical SP-F4, FindPlanetSpawnPoint): the terrain along the direction
	 * from the centre to NearPosition, then rings of directions around it; OutPosition is then the surface point itself.
	 *
	 * @param World World context
	 * @param NearPosition Desired spawn position (world space)
	 * @param OutPosition Output: valid terrain position with safe Z margin
	 * @param MaxSearchRadius Maximum search radius in world units (default 50000)
	 * @return True if a valid position was found
	 */
	static bool FindSpawnablePosition(
		const UWorld* World,
		const FVector& NearPosition,
		FVector& OutPosition,
		float MaxSearchRadius = 50000.f);

	/**
	 * Pure: the spawn search on a planet (spherical SP-F4). The terrain surface along StartDirection, when it is above the
	 * sea; else rings of directions around it, StepArc of arc apart along the datum sphere, in 8 bearings each (as the
	 * heightmap search walks rings in X and Y), out to MaxSearchArc.
	 *
	 * @param Centre Planet centre
	 * @param Radius Datum radius (arc lengths are measured on it)
	 * @param StartDirection Unit direction from the centre to start from
	 * @param StepArc Ring spacing (world units of arc)
	 * @param MaxSearchArc Farthest ring (world units of arc)
	 * @param SurfaceAt The terrain surface point along a unit direction (UVoxelChunkManager::GetGeneratedSurfacePoint)
	 * @param Sea The sea (disabled: every direction is dry)
	 * @param OutPosition Receives the surface point found
	 * @param OutRing Receives the ring it was found on (0 = the start)
	 * @return True if dry land was found
	 */
	static bool FindPlanetSpawnPoint(const FVector& Centre, double Radius, const FVector& StartDirection, double StepArc,
		double MaxSearchArc, TFunctionRef<FVector(const FVector& Direction)> SurfaceAt, const FVoxelSeaSurface& Sea,
		FVector& OutPosition, int32& OutRing);

	/** Clear the cached chunk manager reference (call on world teardown). */
	static void ClearCache();

private:
	/** Cached chunk manager (weak to avoid preventing GC). */
	static TWeakObjectPtr<UVoxelChunkManager> CachedChunkManager;
};
