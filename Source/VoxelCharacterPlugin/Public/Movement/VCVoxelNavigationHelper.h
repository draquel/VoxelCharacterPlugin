// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/VCTypes.h"
#include "VoxelSeaSurface.h"
#include "VoxelWorldModeTypes.h"

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
	 * With bSearchWholeWorld the search goes on past MaxSearchRadius: on a planet to its far side
	 * (FindPlanetSpawnPointAtScale, spherical Phase 6, WM-BP11, G36: a big planet's open sea can lie hundreds of km
	 * across), on a heightmap world in widening rings to the island's bounds or a few hundred chunks on a plane
	 * (FindPlaneSpawnPointAtScale, world modes review WM-MP10: a seed whose spawn is open sea, an island whose radius
	 * shrank). A spawn there would otherwise wait for land that never comes.
	 *
	 * @param World World context
	 * @param NearPosition Desired spawn position (world space)
	 * @param OutPosition Output: valid terrain position with safe Z margin
	 * @param MaxSearchRadius Maximum search radius in world units (default 50000)
	 * @param bSearchWholeWorld Search the rest of the world when there is no land within MaxSearchRadius
	 * @return True if a valid position was found
	 */
	static bool FindSpawnablePosition(
		const UWorld* World,
		const FVector& NearPosition,
		FVector& OutPosition,
		float MaxSearchRadius = 50000.f,
		bool bSearchWholeWorld = false);

	/**
	 * Pure: the spawn search on a heightmap world past the ring search (world modes review WM-MP10; the plane's
	 * FindPlanetSpawnPointAtScale). Rings around Start from FirstRadius outward, each a quarter of its radius past the
	 * last (at least StepSize), with as many bearings as fit at that spacing (8 to 128), out to MaxRadius or MaxSamples
	 * surface samples: the nearest land is found to within about a quarter of its distance. A sample outside Bounds is
	 * skipped, and a ring with no sample inside them ends the search (an island fully covered).
	 *
	 * @param Start The start, in world XY
	 * @param FirstRadius The first ring's radius (where the ring search's rings ended)
	 * @param StepSize The least ring spacing (a chunk)
	 * @param MaxRadius The farthest ring
	 * @param Bounds The world's bounds (an unbounded plane contains everything)
	 * @param SurfaceHeightAt The generated surface height at a world (X, Y) (UVoxelChunkManager::GetGeneratedSurfaceHeight)
	 * @param Sea The sea (disabled: everything is dry)
	 * @param OutPosition Receives the surface point found
	 * @param OutDistance Receives its distance from Start
	 * @param MaxSamples Most surface samples to take
	 * @return True if dry land was found
	 */
	static bool FindPlaneSpawnPointAtScale(const FVector2D& Start, double FirstRadius, double StepSize, double MaxRadius,
		const FVoxelWorldBounds& Bounds, TFunctionRef<float(double X, double Y)> SurfaceHeightAt, const FVoxelSeaSurface& Sea,
		FVector& OutPosition, double& OutDistance, int32 MaxSamples = 4096);

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

	/**
	 * Pure: the spawn search on a planet of any size (spherical Phase 6, WM-BP11, G36). Past FirstArc rings of directions
	 * a quarter of their angle apart, at most 3.5 degrees, each ring's bearings as far apart (8 to 128), out to the
	 * antipode: the nearest land is found to within about a quarter of its distance, and land wider than about 5 degrees
	 * (85 km on a 1,000 km planet) is never missed, in at most MaxSamples surface samples (4,090 to the antipode of a
	 * 1,000 km planet). FindPlanetSpawnPoint covers the rings up to FirstArc.
	 *
	 * @param Centre Planet centre
	 * @param Radius Datum radius (arc lengths are measured on it)
	 * @param StartDirection Unit direction from the centre to start from
	 * @param FirstArc The first ring's arc (world units of arc), where FindPlanetSpawnPoint's rings ended
	 * @param SurfaceAt The terrain surface point along a unit direction
	 * @param Sea The sea (disabled: every direction is dry)
	 * @param OutPosition Receives the surface point found
	 * @param OutArc Receives its arc from the start (world units, on the datum)
	 * @param MaxSamples Most surface samples to take
	 * @return True if dry land was found
	 */
	static bool FindPlanetSpawnPointAtScale(const FVector& Centre, double Radius, const FVector& StartDirection, double FirstArc,
		TFunctionRef<FVector(const FVector& Direction)> SurfaceAt, const FVoxelSeaSurface& Sea, FVector& OutPosition, double& OutArc,
		int32 MaxSamples = 4096);

	/** Clear the cached chunk manager reference (call on world teardown). */
	static void ClearCache();

private:
	/** Cached chunk manager (weak to avoid preventing GC). */
	static TWeakObjectPtr<UVoxelChunkManager> CachedChunkManager;
};
