// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/CGFCombatTypes.h"
#include "VCVitalsWidget.generated.h"

class UProgressBar;
class UTextBlock;
class UAbilitySystemComponent;
class AVCCharacterBase;
struct FOnAttributeChangeData;

/**
 * Minimal health + stamina readout, built programmatically like the hotbar (no UMG asset).
 * Owned by AVCPlayerController; rebound to each possessed character so it survives respawn.
 *
 * Health comes from UVCCombatComponent::OnHealthChanged; stamina straight from the attribute
 * change delegate on the character's ability system. An "EDIT" cue under the bars follows
 * AVCCharacterBase::OnEditModeChanged so the player can see when the mouse actions carve terrain.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCVitalsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Bind to a character's combat component and ability system. Null unbinds. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void InitWithCharacter(AVCCharacterBase* Character);

	/** @return True while the "EDIT" cue is visible (mirrors the bound character's edit mode). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|UI")
	bool IsEditModeShown() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

	void BuildWidgetTree();
	void Unbind();
	void RefreshFromCharacter();

	UFUNCTION()
	void HandleHealthChanged(float NewHealth, float OldHealth, float MaxHealth);

	UFUNCTION()
	void HandleEditModeChanged(bool bEnabled);

	void HandleStaminaChanged(const FOnAttributeChangeData& Data);
	void HandleMaxStaminaChanged(const FOnAttributeChangeData& Data);

	void SetHealth(float Current, float Max);
	void SetStamina(float Current, float Max);
	void SetEditModeShown(bool bShown);

	UPROPERTY()
	TObjectPtr<UProgressBar> HealthBar;

	UPROPERTY()
	TObjectPtr<UTextBlock> HealthText;

	UPROPERTY()
	TObjectPtr<UProgressBar> StaminaBar;

	UPROPERTY()
	TObjectPtr<UTextBlock> StaminaText;

	/** "EDIT" cue; collapsed while edit mode is off. */
	UPROPERTY()
	TObjectPtr<UTextBlock> EditModeText;

	TWeakObjectPtr<AVCCharacterBase> BoundCharacter;
	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
	FDelegateHandle StaminaHandle;
	FDelegateHandle MaxStaminaHandle;
};
