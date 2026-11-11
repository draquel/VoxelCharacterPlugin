// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VoxelCoreTypes.h"
#include "VCSettings.generated.h"

class UAnimInstance;
class UVCActionAnimSet;
class UVCCharacterAppearance;

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

	/**
	 * The player's action montage sets (character animation P3): what an AVCCharacterBase whose UVCActionMontageComponent
	 * lists none plays, one set per Equipment.AnimSet tag. Scripts/setup_character_layers.py builds them.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Animation")
	TArray<TSoftObjectPtr<UVCActionAnimSet>> PlayerActionAnimSets;

	/**
	 * The player's motion-matching AnimBP (character animation P5): the Game Animation Sample's CMC AnimBP on
	 * UVCMotionMatchingAnimInstance, built by Scripts/setup_character_locomotion.py. The player plays it while
	 * vc.Anim.Locomotion is 1 and it loads; otherwise, and on a dedicated server, its Blueprint's own anim class.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Animation")
	TSoftClassPtr<UAnimInstance> PlayerLocomotionAnimClass;

	/**
	 * The player's state-machine AnimBP (the locomotion state machine plan, D86): ABP_VCPlayerLocomotion on
	 * UVCLocomotionAnimInstance, the packs' clips by the held action set, built by Scripts/setup_character_state_machine.py.
	 * The player plays it while vc.Anim.Locomotion is 2 and it loads; otherwise, and on a dedicated server, its Blueprint's own.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Animation")
	TSoftClassPtr<UAnimInstance> PlayerStateMachineAnimClass;

	/**
	 * The player's body where nothing more particular names one (character animation P6): the project's choice, by its
	 * style. Empty: the player Blueprint's own body. A game mode's PlayerAppearance and the player's own choice come first.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Appearance")
	TSoftObjectPtr<UVCCharacterAppearance> DefaultPlayerAppearance;

	/**
	 * The player's body by the voxel world's meshing mode, over DefaultPlayerAppearance (this project: a MetaHuman in
	 * smooth worlds, the low-poly body in cubic ones). A mode not listed takes the default.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Appearance")
	TMap<EMeshingMode, TSoftObjectPtr<UVCCharacterAppearance>> PlayerAppearanceByMeshing;
};
