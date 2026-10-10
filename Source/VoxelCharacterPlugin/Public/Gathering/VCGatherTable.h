// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VCGatherTable.generated.h"

/** One diggable voxel material and what digging it yields. */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCGatherEntry
{
	GENERATED_BODY()

	/** Voxel material id (EVoxelMaterial: Stone 2, Sandstone 5, Iron 11, Wood 20, ...). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gather")
	uint8 MaterialId = 0;

	/** Item definition given per dig. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gather", meta = (AllowedTypes = "ItemDefinition"))
	FPrimaryAssetId ItemId;

	/** Items per dig without a tool (bare hands, or a tool that does not reach the threshold). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Gather", meta = (ClampMin = "0"))
	int32 BaseYield = 1;

	/** Items per dig with a tool (MiningSpeed at or above the table's threshold). */
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
 * The player controller reads the voxel under the dig before the brush runs and consults this table
 * on the authority. Materials not listed (dirt, grass, sand, snow, ...) yield nothing.
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

	/** Pure: items a dig yields for an entry at a mining speed. */
	static int32 YieldFor(const FVCGatherEntry& Entry, float MiningSpeed, float Threshold)
	{
		return MiningSpeed + KINDA_SMALL_NUMBER >= Threshold ? Entry.ToolYield : Entry.BaseYield;
	}
};
