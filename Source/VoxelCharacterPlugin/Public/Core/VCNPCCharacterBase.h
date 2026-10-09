// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GameplayTagContainer.h"
#include "Types/CGFCombatTypes.h"
#include "VCNPCCharacterBase.generated.h"

class UAbilitySystemComponent;
class UVCCharacterAttributeSet;
class UVCCombatAttributeSet;
class UVCCombatComponent;
class UGameplayAbility;

/**
 * Base pawn for anything that fights but is not a player: dungeon enemies,
 * bosses, training dummies, later surface NPCs.
 *
 * Unlike AVCCharacterBase (ASC on the player state so attributes survive
 * respawn), an NPC owns its ability system component and attribute sets
 * directly — there is no player state and death is final for the pawn.
 *
 * Provides health, combat attributes, faction and the death flow. Movement is
 * the stock character movement component; AI controllers, abilities and
 * voxel-terrain awareness are added by subclasses / later features.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API AVCNPCCharacterBase : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AVCNPCCharacterBase(const FObjectInitializer& ObjectInitializer);

	// --- IAbilitySystemInterface ---
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	// =================================================================
	// Components
	// =================================================================

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|GAS")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Combat")
	TObjectPtr<UVCCombatComponent> CombatComponent;

	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|GAS")
	UVCCharacterAttributeSet* GetCharacterAttributes() const { return CharacterAttributes; }

	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|GAS")
	UVCCombatAttributeSet* GetCombatAttributes() const { return CombatAttributes; }

	// =================================================================
	// Configuration
	// =================================================================

	/** Health and MaxHealth at spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "1"))
	float StartingMaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float StartingAttackPower = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float StartingDefense = 0.f;

	/** Abilities granted on the server at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|GAS")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

	/** Seconds after death before the actor is destroyed. 0 = leave the corpse. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float DestroyAfterDeathDelay = 10.f;

	/** True after the death flow ran on this machine (authority or via replication). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	bool IsDead() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Death visuals + cleanup; runs on every machine from the combat component's OnDied. */
	UFUNCTION()
	virtual void HandleDied(const FCGFDamageContext& Context);

	/** Blueprint hook for death presentation (ragdoll, VFX, loot). Called after the C++ cleanup. */
	UFUNCTION(BlueprintImplementableEvent, Category = "VoxelCharacter|Combat", meta = (DisplayName = "On Died"))
	void BP_OnDied(const FCGFDamageContext& Context);

	UPROPERTY()
	TObjectPtr<UVCCharacterAttributeSet> CharacterAttributes;

	UPROPERTY()
	TObjectPtr<UVCCombatAttributeSet> CombatAttributes;

	FTimerHandle DestroyTimerHandle;
};
