// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "VCPathProvider.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UVCPathProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Something that can answer "how do I walk from A to B" for an NPC (feature 5: dungeon enemies).
 * The AI controller asks its provider instead of the engine navmesh, so the dungeon layer can
 * path over its own grid and the surface epic can plug in a voxel pathfinder later. Without a
 * provider the controller walks straight lines.
 */
class VOXELCHARACTERPLUGIN_API IVCPathProvider
{
	GENERATED_BODY()

public:
	/**
	 * Find a walkable route.
	 * @param From      Start (world).
	 * @param To        Goal (world).
	 * @param OutPoints Waypoints to walk in order, ending at (or near) To. Empty on failure.
	 * @return True when a route exists.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VoxelCharacter|AI")
	bool FindPath(const FVector& From, const FVector& To, TArray<FVector>& OutPoints) const;
};
