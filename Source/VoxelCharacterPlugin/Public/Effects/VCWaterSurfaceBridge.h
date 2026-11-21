// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Interfaces/CGFWaterSurfaceInterface.h"
#include "VCWaterSurfaceBridge.generated.h"

/**
 * The voxel world's water, given to the framework (the water plan's W2B, D129): the framework plugins know no voxel
 * plugin, so this game-layer subsystem answers CommonGameFramework's water question (ICGFWaterSurfaceInterface) from
 * the water levels' sample (FVCTerrainQueryHelper::SampleWaterSurface) and registers itself with UCGFWaterSubsystem
 * when the world begins play. A dropped item (InteractionPlugin's AWorldItem) rests at the surface through it.
 *
 * It reads the water per position at each call and holds no level: a world with several bodies, or a body whose level
 * changes, needs nothing here. Without a voxel world in the level it answers "no water".
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCWaterSurfaceBridge : public UWorldSubsystem, public ICGFWaterSurfaceInterface
{
	GENERATED_BODY()

public:
	/**
	 * Register with the world's UCGFWaterSubsystem (OnWorldBeginPlay does it; a test world that never begins play calls
	 * it itself).
	 * @return True when registered
	 */
	bool Register();

	// ICGFWaterSurfaceInterface
	virtual bool SampleWaterSurface_Implementation(const FVector& Location, float ReachAbove, FCGFWaterSurfaceSample& OutSample) const override;

	/**
	 * Pass a change of the voxel world's lakes or river reaches on to the framework's water subsystem
	 * (UCGFWaterSubsystem::NotifyWaterChanged), so what rests at a surface there settles again: a body added, or one a
	 * source worked out late (UVoxelChunkManager::OnWaterBodiesChanged). Bound to the world's chunk manager when it
	 * starts.
	 * @param Region Where the bodies changed
	 */
	void HandleWaterBodiesChanged(const struct FVoxelConditioningRegion& Region);

	/** @return How many changes of the voxel world's bodies this bridge passed on to the framework's water subsystem */
	int32 GetNumChangesPassed() const { return NumChangesPassed; }

	/** @return The bounds of the last change passed on (an invalid box: everywhere) */
	const FBox& GetLastChangeBounds() const { return LastChangeBounds; }

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	/** A chunk manager started: bind the one of this world. */
	void HandleChunkManagerInitialized(class UVoxelChunkManager& Manager);

	/** Bind OnWaterBodiesChanged on a chunk manager of this world (once). */
	void BindChunkManager(class UVoxelChunkManager& Manager);

	FDelegateHandle InitializedHandle;
	FDelegateHandle WaterBodiesChangedHandle;
	TWeakObjectPtr<class UVoxelChunkManager> BoundManager;
	int32 NumChangesPassed = 0;
	FBox LastChangeBounds = FBox(ForceInit);
};
