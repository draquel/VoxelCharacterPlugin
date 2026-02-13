// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "VCInputConfig.generated.h"

/**
 * DataAsset that holds references to all Enhanced Input actions and mapping
 * contexts used by the character controller.
 *
 * Create an instance in the editor, assign your input action and mapping
 * context assets, then reference it from AVCPlayerController::InputConfig.
 */
UCLASS(BlueprintType)
class VOXELCHARACTERPLUGIN_API UVCInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	// ==================== Input Actions ====================

	/** 2D axis — WASD / left stick. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Input|Actions")
	TObjectPtr<UInputAction> IA_Move;

	/** 2D axis — mouse delta / right stick. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Input|Actions")
	TObjectPtr<UInputAction> IA_Look;

	/** Digital — spacebar. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Input|Actions")
	TObjectPtr<UInputAction> IA_Jump;

	/** Digital — E key / gamepad face button. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Input|Actions")
	TObjectPtr<UInputAction> IA_Interact;

	/** Digital — LMB — attack / mine / use. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Input|Actions")
	TObjectPtr<UInputAction> IA_PrimaryAction;

	/** Digital — RMB — place / aim / alt-use. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Input|Actions")
	TObjectPtr<UInputAction> IA_SecondaryAction;

	/** Digital — V key — switch FP / TP. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Input|Actions")
	TObjectPtr<UInputAction> IA_ToggleView;

	/** Digital — Tab / I — open inventory UI. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Input|Actions")
	TObjectPtr<UInputAction> IA_OpenInventory;

	/** Digital — 1-9 number keys — select hotbar slot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Input|Actions")
	TObjectPtr<UInputAction> IA_HotbarSlot;

	/** 1D axis — mouse wheel — cycle hotbar. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Input|Actions")
	TObjectPtr<UInputAction> IA_ScrollHotbar;

	/** Digital — Q key — drop active hotbar item. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Input|Actions")
	TObjectPtr<UInputAction> IA_Drop;

	// ==================== Mapping Contexts ====================

	/** Base gameplay context — always active at priority 0. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Input|Contexts")
	TObjectPtr<UInputMappingContext> IMC_Gameplay;

	/** UI overlay context — active when inventory / menus are open (priority 1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Input|Contexts")
	TObjectPtr<UInputMappingContext> IMC_UI;

	/** Vehicle context — active when possessing a vehicle (priority 2, future). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Input|Contexts")
	TObjectPtr<UInputMappingContext> IMC_Vehicle;
};
