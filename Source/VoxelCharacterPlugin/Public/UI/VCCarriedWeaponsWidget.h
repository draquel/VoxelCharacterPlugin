// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Blueprint/UserWidget.h"
#include "VCCarriedWeaponsWidget.generated.h"

class AVCCharacterBase;
class UHorizontalBox;
class UTextBlock;

/**
 * The weapons the player carries on the body (the equipment sheathing plan, D81): one small icon per occupied sheath,
 * in the sheaths' cycle order, the drawn one lit, and the keys' hint (R sheathe / draw, X next). Display only, built in
 * code like the vitals widget; AVCPlayerController puts it above the hotbar and rebinds it to each possessed character.
 * It follows the equipment manager's OnEquipmentChanged, which fires on every machine for an equip and a draw.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCCarriedWeaponsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Bind to a character's equipment (null unbinds) and show what it carries.
	 * @param Character The possessed character
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void InitWithCharacter(AVCCharacterBase* Character);

	/** Rebuild the icons from the bound character's sheaths. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void Refresh();

	/** @return The sheaths shown, in order (the bound character's carried weapons). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|UI")
	TArray<FGameplayTag> GetShownSheaths() const { return ShownSheaths; }

	/** @return The sheath shown lit (its weapon drawn); invalid when none is. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|UI")
	FGameplayTag GetLitSheath() const { return LitSheath; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

private:
	void BuildWidgetTree();
	void Unbind();

	UFUNCTION()
	void HandleEquipmentChanged();

	UPROPERTY()
	TObjectPtr<UHorizontalBox> IconRow;

	UPROPERTY()
	TObjectPtr<UTextBlock> HintText;

	TWeakObjectPtr<AVCCharacterBase> BoundCharacter;
	TArray<FGameplayTag> ShownSheaths;
	FGameplayTag LitSheath;
};
