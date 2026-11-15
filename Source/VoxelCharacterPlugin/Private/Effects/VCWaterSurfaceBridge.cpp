// Copyright Daniel Raquel. All Rights Reserved.

#include "Effects/VCWaterSurfaceBridge.h"
#include "Movement/VCTerrainQueryHelper.h"
#include "Subsystems/CGFWaterSubsystem.h"
#include "VoxelChunkManager.h"
#include "VoxelWorldConfiguration.h"
#include "Engine/World.h"

bool UVCWaterSurfaceBridge::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UVCWaterSurfaceBridge::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// The world's chunk manager starts after this subsystem: bind its water bodies' changes when it does.
	InitializedHandle = UVoxelChunkManager::OnInitialized.AddUObject(this, &UVCWaterSurfaceBridge::HandleChunkManagerInitialized);
}

void UVCWaterSurfaceBridge::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	Register();
	if (UVoxelChunkManager* Manager = UVoxelChunkManager::Find(&InWorld))
	{
		BindChunkManager(*Manager);
	}
}

void UVCWaterSurfaceBridge::HandleChunkManagerInitialized(UVoxelChunkManager& Manager)
{
	if (Manager.GetWorld() == GetWorld())
	{
		BindChunkManager(Manager);
	}
}

void UVCWaterSurfaceBridge::BindChunkManager(UVoxelChunkManager& Manager)
{
	if (BoundManager.Get() == &Manager)
	{
		return;
	}
	if (UVoxelChunkManager* Previous = BoundManager.Get())
	{
		Previous->OnWaterBodiesChanged.Remove(WaterBodiesChangedHandle);
	}
	BoundManager = &Manager;
	WaterBodiesChangedHandle = Manager.OnWaterBodiesChanged.AddUObject(this, &UVCWaterSurfaceBridge::HandleWaterBodiesChanged);
}

void UVCWaterSurfaceBridge::HandleWaterBodiesChanged(const FVoxelConditioningRegion& Region)
{
	const UWorld* World = GetWorld();
	UCGFWaterSubsystem* Water = World ? World->GetSubsystem<UCGFWaterSubsystem>() : nullptr;
	if (!Water)
	{
		return;
	}
	// The framework takes world bounds, an invalid box for everywhere: a heightmap region is its columns, a planet's cap
	// a box around it on the datum sphere, with a twentieth of the radius for the terrain's height.
	FBox Bounds(ForceInit);
	if (!Region.bEverywhere)
	{
		const UVoxelChunkManager* Manager = BoundManager.Get();
		const UVoxelWorldConfiguration* Config = Manager ? Manager->GetConfiguration() : nullptr;
		if (Region.bCap && Config)
		{
			const double Radius = FMath::Max(static_cast<double>(Config->WorldRadius), 1.0);
			Bounds = FBox::BuildAABB(Config->WorldOrigin + Region.Direction * Radius,
				FVector(Radius * FMath::Min(Region.AngularRadius, UE_DOUBLE_PI) + 0.05 * Radius));
		}
		else if (!Region.bCap && Region.Box.bIsValid)
		{
			Bounds = FBox(FVector(Region.Box.Min.X, Region.Box.Min.Y, -UE_LARGE_WORLD_MAX), FVector(Region.Box.Max.X, Region.Box.Max.Y, UE_LARGE_WORLD_MAX));
		}
		else
		{
			// A region that reaches nothing in this kind of world.
			return;
		}
	}
	++NumChangesPassed;
	LastChangeBounds = Bounds;
	Water->NotifyWaterChanged(Bounds);
}

void UVCWaterSurfaceBridge::Deinitialize()
{
	if (InitializedHandle.IsValid())
	{
		UVoxelChunkManager::OnInitialized.Remove(InitializedHandle);
		InitializedHandle.Reset();
	}
	if (UVoxelChunkManager* Manager = BoundManager.Get())
	{
		Manager->OnWaterBodiesChanged.Remove(WaterBodiesChangedHandle);
	}
	WaterBodiesChangedHandle.Reset();
	BoundManager.Reset();
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
