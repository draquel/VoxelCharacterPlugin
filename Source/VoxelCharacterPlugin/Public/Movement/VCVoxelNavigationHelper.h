// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/VCTypes.h"

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
	 * Populates surface type, friction, water state, and chunk coordinate.
	 *
	 * @param World World context
	 * @param Location World-space position to query (typically character feet)
	 * @return Terrain context with all fields populated
	 */
	static FVoxelTerrainContext QueryTerrainContext(const UWorld* World, const FVector& Location);

	/**
	 * Get the raw voxel material ID at a world position.
	 *
	 * @param World World context
	 * @param Location World-space position
	 * @return Material ID (0 if unloaded or air)
	 */
	static uint8 GetVoxelMaterialAtLocation(const UWorld* World, const FVector& Location);

	/**
	 * Check if a world position is underwater based on voxel world water level.
	 *
	 * @param World World context
	 * @param Location World-space position
	 * @param OutWaterDepth Depth below water surface (0 if above)
	 * @return True if position is below water level
	 */
	static bool IsPositionUnderwater(const UWorld* World, const FVector& Location, float& OutWaterDepth);

	/** Clear the cached chunk manager reference (call on world teardown). */
	static void ClearCache();

private:
	/** Cached chunk manager (weak to avoid preventing GC). */
	static TWeakObjectPtr<UVoxelChunkManager> CachedChunkManager;
};
