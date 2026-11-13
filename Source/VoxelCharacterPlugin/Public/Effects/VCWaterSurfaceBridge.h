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

protected:
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
};
