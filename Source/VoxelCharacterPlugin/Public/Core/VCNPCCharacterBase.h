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

	/** Let the possessing AVCNPCAIController think and move (feature 5). False = a training dummy. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI")
	bool bAIEnabled = true;

	/** Faction applied to the combat component at BeginPlay when set (Faction.Animal for wildlife); unset = the component's default (Monster). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (Categories = "Faction"))
	FGameplayTag Faction;

	/** Hostile hunts, Prey flees (feature 10). The AI controller reads it on possess. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI")
	EVCNPCBehavior Behavior = EVCNPCBehavior::Hostile;

	/** Roam this far around the spawn point when there is nothing to do (0 = stand still; dungeon enemies patrol instead). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "0"))
	float WanderRadius = 0.f;

	/** Hostiles: idle NPCs of this class within the radius join a chase this one starts (0 = lone hunter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "0"))
	float PackRadius = 0.f;

	/** Perception radius for this creature (0 = the controller's default, 1200). Wolves hunt from further out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "0"))
	float SightRadius = 0.f;

	/**
	 * Death visual when there is no physics asset to ragdoll (static-mesh placeholders): the body
	 * topples over ToppleSeconds and sinks a little. 0 disables.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float ToppleSeconds = 0.4f;

	/** True after the death flow ran on this machine (authority or via replication). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	bool IsDead() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

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

	// Topple animation state (every machine; starts in HandleDied when no physics asset exists).
	bool bToppling = false;
	float ToppleElapsed = 0.f;
	FRotator ToppleStartRotation = FRotator::ZeroRotator;
	FVector ToppleStartLocation = FVector::ZeroVector;
};
