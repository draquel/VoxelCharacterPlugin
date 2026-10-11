// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VCNPCHealthBarWidget.generated.h"

class UProgressBar;

/**
 * Floating health bar over an NPC (VoxelCharacterPlugin UI). Code-built; hosted by a screen-space widget component that
 * AVCNPCCharacterBase creates when bShowHealthBar is on (the training dummy, or every NPC with vc.UI.NPCHealthBars 2).
 */
UCLASS(BlueprintType, Blueprintable)
class VOXELCHARACTERPLUGIN_API UVCNPCHealthBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Show a health value.
	 * @param Health    Current health
	 * @param MaxHealth Maximum health (non-positive shows an empty bar)
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void SetHealth(float Health, float MaxHealth);

	/** Bar width in slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|UI")
	float BarWidth = 80.f;

	/** Bar height in slate units. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|UI")
	float BarHeight = 8.f;

protected:
	virtual void NativeOnInitialized() override;

	/** Build the code-built tree (idempotent). */
	void BuildWidgetTree();

	UPROPERTY()
	TObjectPtr<UProgressBar> Bar;
};
