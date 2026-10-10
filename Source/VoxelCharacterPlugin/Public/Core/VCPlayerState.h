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

	// --- Rest point (feature 8: campsites) ---

	/** Authority: remember where the player last rested (the AtLastRest respawn policy uses it). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Rest")
	void SetRespawnPoint(const FTransform& Point);

	/** Authority: forget the rest point (back to the policy's fallback). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Rest")
	void ClearRespawnPoint();

	/** @return True and the point when the player has rested somewhere. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Rest")
	bool GetRespawnPoint(FTransform& OutPoint) const;

	// --- Progression (feature 4: dungeon objectives) ---

	/** @return The replicated progression counters. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Progression")
	const FVCProgressionStats& GetProgression() const { return Progression; }

	/**
	 * Authority: record a cleared dungeon objective for this player.
	 * @param bBossKilled True when this player landed the killing blow on the boss.
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Progression")
	void AddDungeonCleared(bool bBossKilled = true);

	/** Fires on the server when counters change and on every client when they replicate. */
	UPROPERTY(BlueprintAssignable, Category = "VoxelCharacter|Progression")
	FOnVCProgressionChanged OnProgressionChanged;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UFUNCTION()
	void OnRep_Progression();

	UPROPERTY(ReplicatedUsing = OnRep_Progression, BlueprintReadOnly, Category = "VoxelCharacter|Progression")
	FVCProgressionStats Progression;

	/** Last rest point (server only; survives the avatar like the item snapshot). */
	FTransform RespawnPoint;
	bool bHasRespawnPoint = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|GAS")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY()
	TObjectPtr<UVCCharacterAttributeSet> CharacterAttributes;

	UPROPERTY()
	TObjectPtr<UVCCombatAttributeSet> CombatAttributes;
};
