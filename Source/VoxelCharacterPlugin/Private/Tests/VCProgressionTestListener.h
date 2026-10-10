// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/VCTypes.h"
#include "VCProgressionTestListener.generated.h"

/**
 * Test-only sink for AVCPlayerState::OnProgressionChanged (a dynamic delegate needs a UFUNCTION target).
 * Lives outside the WITH_AUTOMATION_TESTS guard because UHT cannot skip a conditional UCLASS.
 */
UCLASS()
class UVCProgressionTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleProgressionChanged(const FVCProgressionStats& Stats)
	{
		++BroadcastCount;
		Last = Stats;
	}

	int32 BroadcastCount = 0;
	FVCProgressionStats Last;
};
