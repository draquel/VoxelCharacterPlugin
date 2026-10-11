// Copyright Daniel Raquel. All Rights Reserved.

#include "AI/VCVoxelPathProvider.h"
#include "VoxelSurfaceNavigationSubsystem.h"

bool UVCVoxelPathProvider::FindPath_Implementation(const FVector& From, const FVector& To, TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();
	UVoxelSurfaceNavigationSubsystem* Nav = UVoxelSurfaceNavigationSubsystem::Get(this);
	return Nav && Nav->FindPath(From, To, OutPoints);
}

bool UVCVoxelPathProvider::IsAvailable(const UObject* WorldContextObject)
{
	return UVoxelSurfaceNavigationSubsystem::Get(WorldContextObject) != nullptr;
}

bool UVCVoxelPathProvider::IsWalkable(const FVector& Location) const
{
	UVoxelSurfaceNavigationSubsystem* Nav = UVoxelSurfaceNavigationSubsystem::Get(this);
	return Nav && Nav->IsWalkableAt(Location);
}

bool UVCVoxelPathProvider::ProjectToSurface(const FVector& Location, FVector& OutOnSurface) const
{
	OutOnSurface = Location;
	UVoxelSurfaceNavigationSubsystem* Nav = UVoxelSurfaceNavigationSubsystem::Get(this);
	return Nav && Nav->ProjectToSurface(Location, OutOnSurface);
}

void UVCVoxelPathProvider::ReportBlocked(const FVector& Location, float Radius, float Seconds) const
{
	if (UVoxelSurfaceNavigationSubsystem* Nav = UVoxelSurfaceNavigationSubsystem::Get(this))
	{
		Nav->BlockCellsTemporarily(Location, Radius, Seconds);
	}
}
