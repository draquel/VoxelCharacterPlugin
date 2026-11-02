// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VCSettings.generated.h"

/**
 * The character plugin's project-wide settings (Project Settings > Plugins > Voxel Character; code review CR4, the plan's
 * §10.4). Each has a console variable of the same meaning that overrides it once something sets the variable.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "Voxel Character"))
class VOXELCHARACTERPLUGIN_API UVCSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	/** @return The settings (the class default object) */
	static const UVCSettings& Get() { return *GetDefault<UVCSettings>(); }

	/** Floating NPC health bars: 0 = never, 1 = NPCs with bShowHealthBar, 2 = every NPC (vc.UI.NPCHealthBars) */
	UPROPERTY(config, EditAnywhere, Category = "UI", meta = (ClampMin = "0", ClampMax = "2"))
	int32 NPCHealthBars = 1;
};
