// Copyright Daniel Raquel. All Rights Reserved.

#include "Effects/VCWaterSurfaceBridge.h"
#include "Movement/VCTerrainQueryHelper.h"
#include "Subsystems/CGFWaterSubsystem.h"
#include "Engine/World.h"

bool UVCWaterSurfaceBridge::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UVCWaterSurfaceBridge::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	Register();
}

void UVCWaterSurfaceBridge::Deinitialize()
{
	if (const UWorld* World = GetWorld())
	{
		if (UCGFWaterSubsystem* Water = World->GetSubsystem<UCGFWaterSubsystem>())
		{
			Water->ClearProvider(this);
		}
	}
	Super::Deinitialize();
}

bool UVCWaterSurfaceBridge::Register()
{
	const UWorld* World = GetWorld();
	UCGFWaterSubsystem* Water = World ? World->GetSubsystem<UCGFWaterSubsystem>() : nullptr;
	if (!Water)
	{
		return false;
	}
	Water->SetProvider(this);
	return true;
}

bool UVCWaterSurfaceBridge::SampleWaterSurface_Implementation(const FVector& Location, float ReachAbove, FCGFWaterSurfaceSample& OutSample) const
{
	OutSample = FCGFWaterSurfaceSample();
	// The water levels' sample at the position, with the voxel's water flag: under the surface the position's own voxel
	// must be water (not rock, not a dry cave below the level); above it, the voxel just under the surface below.
	FVCWaterSurfaceSample Sample;
	if (!FVCTerrainQueryHelper::SampleWaterSurface(GetWorld(), Location, ReachAbove, Sample))
	{
		return false;
	}
	OutSample.bWater = Sample.bWater;
	OutSample.Depth = Sample.Depth;
	OutSample.Up = Sample.Up;
	OutSample.SurfacePoint = Sample.SurfacePoint(Location);
	return true;
}
