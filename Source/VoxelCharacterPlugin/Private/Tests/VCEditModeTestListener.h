// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VCEditModeTestListener.generated.h"

/**
 * Test-only sink for AVCCharacterBase::OnEditModeChanged (a dynamic delegate needs a UFUNCTION target).
 * Lives outside the WITH_AUTOMATION_TESTS guard because UHT cannot skip a conditional UCLASS.
 */
UCLASS()
class UVCEditModeTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void HandleEditModeChanged(bool bEnabled)
	{
		++BroadcastCount;
		bLastValue = bEnabled;
	}

	int32 BroadcastCount = 0;
	bool bLastValue = false;
};
