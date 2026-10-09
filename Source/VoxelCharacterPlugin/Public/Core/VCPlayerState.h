// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "Core/VCTypes.h"
#include "VCPlayerState.generated.h"

class UAbilitySystemComponent;
class UVCCharacterAttributeSet;
class UVCCombatAttributeSet;
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

	/** Direct access to the combat attribute set (AttackPower, Defense). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|GAS")
	UVCCombatAttributeSet* GetCombatAttributes() const { return CombatAttributes; }

	/**
	 * Melee attack ability granted alongside DefaultAbilities. Defaults to UVCMeleeAttackAbility
	 * in C++ so the demo needs no Blueprint edit; None disables the native melee attack.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|GAS")
	TSubclassOf<UGameplayAbility> MeleeAttackAbilityClass;

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

	// --- Items across death ---

	/** Items the dying avatar was carrying; consumed by the next avatar's PossessedBy. Server only. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Items")
	FVCItemSnapshot PendingItemSnapshot;

	/** True while PendingItemSnapshot waits for a new avatar. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Items")
	bool bHasPendingItemSnapshot = false;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|GAS")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UVCCharacterAttributeSet> CharacterAttributes;

	UPROPERTY()
	TObjectPtr<UVCCombatAttributeSet> CombatAttributes;
};
