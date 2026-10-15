// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VoxelData.h"
#include "VCGatherTable.generated.h"

/** One diggable voxel material and what digging it yields. */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCGatherEntry
{
	GENERATED_BODY()

	/** Voxel material id (EVoxelMaterial: Stone 2, Sandstone 5, Iron 11, Wood 20, ...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gather")
	uint8 MaterialId = 0;

	/** Item definition given per voxel dug out. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gather", meta = (AllowedTypes = "ItemDefinition"))
	FPrimaryAssetId ItemId;

	/** Items per voxel dug out without a tool (bare hands, or a tool that does not reach the threshold). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gather", meta = (ClampMin = "0"))
	int32 BaseYield = 1;

	/** Items per voxel dug out with a tool (MiningSpeed at or above the table's threshold). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gather", meta = (ClampMin = "0"))
	int32 ToolYield = 1;
};

/** One harvestable scatter category (FScatterDefinition::HarvestCategory) and what chopping it yields. */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCHarvestEntry
{
	GENERATED_BODY()

	/** Scatter harvest category ("Tree"). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Harvest")
	FName Category;

	/** Item definition given per hit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Harvest", meta = (AllowedTypes = "ItemDefinition"))
	FPrimaryAssetId ItemId;

	/** Items per hit bare-handed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Harvest", meta = (ClampMin = "0"))
	int32 BaseYield = 1;

	/** Items per hit with a tool (MiningSpeed at or above the table's threshold). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Harvest", meta = (ClampMin = "0"))
	int32 ToolYield = 2;

	/** Hits before the instance is removed from the world. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Harvest", meta = (ClampMin = "1"))
	int32 HitsToRemove = 3;
};

/**
 * Voxel resource gathering (feature 8): which dug voxel materials become which items, and how many.
 * On the authority the player controller reads the voxels a dig can reach before and after the brush runs,
 * and a dig pays out only when it turns a solid voxel to air (FindDugVoxel), so each voxel yields once:
 * a dig used to pay on every swing that changed any density, without limit on one voxel (G3).
 * Materials not listed (dirt, grass, sand, snow, ...) yield nothing.
 */
UCLASS(BlueprintType)
class VOXELCHARACTERPLUGIN_API UVCGatherTable : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gather")
	TArray<FVCGatherEntry> Entries;

	/** Chopping scatter (trees): category → item per hit, hits to fell. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gather")
	TArray<FVCHarvestEntry> HarvestEntries;

	/** The harvest entry for a scatter category, or null when chopping it yields nothing. */
	const FVCHarvestEntry* FindHarvest(FName Category) const
	{
		return HarvestEntries.FindByPredicate([Category](const FVCHarvestEntry& E) { return E.Category == Category; });
	}

	/** Pure: items a chop yields for an entry at a mining speed. */
	static int32 HarvestYieldFor(const FVCHarvestEntry& Entry, float MiningSpeed, float Threshold)
	{
		return MiningSpeed + KINDA_SMALL_NUMBER >= Threshold ? Entry.ToolYield : Entry.BaseYield;
	}

	/** MiningSpeed (attribute) at or above which a dig counts as tool-assisted (pickaxes add +0.5 / +1.0). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gather", meta = (ClampMin = "1.0"))
	float ToolMiningSpeedThreshold = 1.5f;

	/** The entry for a material, or null when digging it yields nothing. */
	const FVCGatherEntry* Find(uint8 MaterialId) const
	{
		return Entries.FindByPredicate([MaterialId](const FVCGatherEntry& E) { return E.MaterialId == MaterialId; });
	}

	/** Pure: items a dug-out voxel yields for an entry at a mining speed. */
	static int32 YieldFor(const FVCGatherEntry& Entry, float MiningSpeed, float Threshold)
	{
		return MiningSpeed + KINDA_SMALL_NUMBER >= Threshold ? Entry.ToolYield : Entry.BaseYield;
	}

	/**
	 * Pure: the voxel a dig pays out for. Of the voxels the dig turned from solid to air, the one nearest the
	 * brush centre; none when it emptied no voxel (a dig that only lowered densities, or hit air).
	 * @param Before Voxels the brush could reach, before the edit.
	 * @param After  The same voxels, in the same order, after the edit.
	 * @param DistSq Each voxel's squared distance from the brush centre.
	 * @return Index into the arrays, or INDEX_NONE.
	 */
	static int32 FindDugVoxel(TConstArrayView<FVoxelData> Before, TConstArrayView<FVoxelData> After, TConstArrayView<float> DistSq)
	{
		int32 Best = INDEX_NONE;
		const int32 Num = FMath::Min3(Before.Num(), After.Num(), DistSq.Num());
		for (int32 i = 0; i < Num; ++i)
		{
			if (Before[i].IsSolid() && After[i].IsAir() && (Best == INDEX_NONE || DistSq[i] < DistSq[Best]))
			{
				Best = i;
			}
		}
		return Best;
	}
};
