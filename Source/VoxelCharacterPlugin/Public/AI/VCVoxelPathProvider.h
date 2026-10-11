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
};
