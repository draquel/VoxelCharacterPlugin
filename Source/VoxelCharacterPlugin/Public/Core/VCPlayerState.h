// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "VCPlayerState.generated.h"

class UAbilitySystemComponent;
class UVCCharacterAttributeSet;
class UGameplayEffect;
class UGameplayAbility;

/**
 * Player state that owns the Ability System Component.
 *
 * Hosting the ASC here means attributes, cooldowns, and persistent
 * gameplay effects survive character death and respawn.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API AVCPlayerState : public APlayerState, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AVCPlayerState();

	// --- IAbilitySystemInterface ---
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	/** Direct access to the character attribute set. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|GAS")
	UVCCharacterAttributeSet* GetCharacterAttributes() const { return CharacterAttributes; }

	// --- Death / Respawn ---

	/** GameplayEffect applied on respawn to reset vitals to max. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|GAS|Respawn")
	TSubclassOf<UGameplayEffect> RespawnResetEffect;

	/** GEs tagged with any of these are removed on death (temporary buffs). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|GAS|Respawn")
	FGameplayTagContainer DeathCleanseTags;

	/** Strip death-cleansable effects and apply the respawn reset GE. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|GAS|Respawn")
	void HandleRespawnAttributeReset();

	/** Default abilities granted once on first possession. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|GAS")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

	/** True after default abilities have been granted (prevents re-grant). */
	bool bAbilitiesGranted = false;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|GAS")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UVCCharacterAttributeSet> CharacterAttributes;
};
