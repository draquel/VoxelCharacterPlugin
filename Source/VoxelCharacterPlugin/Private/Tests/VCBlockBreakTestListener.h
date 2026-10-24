// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VCBlockBreakTestListener.generated.h"

/**
 * Test-only sink for AVCPlayerController::OnVoxelGathered and UVCBlockInteractionComponent::OnBlockBroken (dynamic
 * delegates need a UFUNCTION target). Lives outside the WITH_AUTOMATION_TESTS guard because UHT cannot skip a conditional
 * UCLASS.
 */
UCLASS()
class UVCBlockBreakTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleGathered(uint8 MaterialId, FPrimaryAssetId ItemId, int32 Count)
	{
		Paid += Count;
		LastMaterial = MaterialId;
	}

	UFUNCTION()
	void HandleBroken(FIntVector VoxelCoord, uint8 MaterialId)
	{
		++Broken;
	}

	int32 Paid = 0;
	uint8 LastMaterial = 255;
	int32 Broken = 0;
};
