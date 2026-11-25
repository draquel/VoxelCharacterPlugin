// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AI/VCPathProvider.h"
#include "UObject/Object.h"
#include "VCVoxelPathProvider.generated.h"

/**
 * IVCPathProvider over the voxel surface (feature 10): routes come from VoxelWorlds'
 * UVoxelSurfaceNavigationSubsystem (A* over the generated / edit-merged terrain). AVCNPCAIController creates
 * one for itself on possess when no provider was set and the subsystem exists, so surface NPCs path over
 * hills, around lakes and through dug ground while dungeon enemies keep their grid provider.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCVoxelPathProvider : public UObject, public IVCPathProvider
{
	GENERATED_BODY()

public:
	virtual bool FindPath_Implementation(const FVector& From, const FVector& To, TArray<FVector>& OutPoints) const override;

	/** True when the world has a surface navigation subsystem (it may still be waiting for the voxel world). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|AI", meta = (WorldContext = "WorldContextObject"))
	static bool IsAvailable(const UObject* WorldContextObject);

	/** Is the surface cell at a location walkable (above water, not blocked)? */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AI")
	bool IsWalkable(const FVector& Location) const;

	/** Put a point onto the surface (edit-merged when loaded, analytic otherwise). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AI")
	bool ProjectToSurface(const FVector& Location, FVector& OutOnSurface) const;

	/** A walker got stuck here: block the cells around it for a while so the next search routes elsewhere. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AI")
	void ReportBlocked(const FVector& Location, float Radius = 120.f, float Seconds = 8.f) const;

	/**
	 * Whether the last route FindPath gave ends short of its goal (the search's budget, a goal out of reach, its sampling
	 * cap): the walker follows it, and the next search toward the same goal is worth waiting for (G73).
	 *
	 * @return True when the last route was partial
	 */
	bool WasLastPathPartial() const { return bLastPathPartial; }

	/**
	 * Begin a search that may go on over the next frames (G281: the surface navigation's per-frame search budget,
	 * voxel.Nav.SearchBudgetMs). With the budget 0, or a search that ends within it, PollPath answers at once.
	 *
	 * @param From Start (world)
	 * @param To   Goal (world)
	 * @return The search's handle; 0 when none can run (no voxel world yet), which PollPath answers as a failed search
	 */
	int32 StartPath(const FVector& From, const FVector& To) const;

	/**
	 * Where a search StartPath began stands; once it has ended, its route (WasLastPathPartial then says whether it is
	 * partial) and the handle is freed.
	 *
	 * @param Handle    StartPath's handle
	 * @param OutPoints The route when it has ended and one was found (as FindPath's)
	 * @param bOutFound Whether a route was found (when it has ended)
	 * @return True when the search has ended (or its handle is unknown: then not found), false while it is running
	 */
	bool PollPath(int32 Handle, TArray<FVector>& OutPoints, bool& bOutFound) const;

	/** Drop a search StartPath began. @param Handle StartPath's handle */
	void CancelPath(int32 Handle) const;

private:
	mutable bool bLastPathPartial = false;
};
