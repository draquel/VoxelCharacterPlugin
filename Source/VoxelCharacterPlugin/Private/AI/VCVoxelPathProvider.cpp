// Copyright Daniel Raquel. All Rights Reserved.

#include "AI/VCVoxelPathProvider.h"
#include "VoxelSurfaceNavigationSubsystem.h"

bool UVCVoxelPathProvider::FindPath_Implementation(const FVector& From, const FVector& To, TArray<FVector>& OutPoints) const
{
	OutPoints.Reset();
	UVoxelSurfaceNavigationSubsystem* Nav = UVoxelSurfaceNavigationSubsystem::Get(this);
	FVoxelSurfacePathStats Stats;
	const bool bFound = Nav && Nav->FindPathWithStats(From, To, OutPoints, Stats);
	bLastPathPartial = bFound && Stats.bPartial;
	return bFound;
}

int32 UVCVoxelPathProvider::StartPath(const FVector& From, const FVector& To) const
{
	UVoxelSurfaceNavigationSubsystem* Nav = UVoxelSurfaceNavigationSubsystem::Get(this);
	return Nav ? Nav->StartPathSearch(From, To) : 0;
}

bool UVCVoxelPathProvider::PollPath(int32 Handle, TArray<FVector>& OutPoints, bool& bOutFound) const
{
	OutPoints.Reset();
	bOutFound = false;
	UVoxelSurfaceNavigationSubsystem* Nav = UVoxelSurfaceNavigationSubsystem::Get(this);
	if (!Nav || Handle == 0)
	{
		bLastPathPartial = false;
		return true;
	}
	FVoxelSurfacePathStats Stats;
	switch (Nav->PollPathSearch(Handle, OutPoints, Stats, bOutFound))
	{
	case EVoxelPathSearchState::Running:
		return false;
	case EVoxelPathSearchState::Done:
		bLastPathPartial = bOutFound && Stats.bPartial;
		return true;
	default:
		bLastPathPartial = false;
		return true;
	}
}

void UVCVoxelPathProvider::CancelPath(int32 Handle) const
{
	if (UVoxelSurfaceNavigationSubsystem* Nav = UVoxelSurfaceNavigationSubsystem::Get(this))
	{
		Nav->CancelPathSearch(Handle);
	}
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
