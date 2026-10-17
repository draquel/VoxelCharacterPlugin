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
class UWidgetComponent;
class UVCNPCHealthBarWidget;

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

	/** How readily it engages (hostiles). The surface spawner pushes the entry's day / night value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI")
	EVCNPCTemperament Temperament = EVCNPCTemperament::Aggressive;

	/** Wary temperament: a hostile this close is engaged anyway. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "0"))
	float WaryEngageRadius = 400.f;

	/**
	 * Keeps away from a target carrying a lit light (a torch, ICGFLightBearerInterface): holds at the light's edge,
	 * backs off inside it, never attacks it, and gets no sight bonus from it (QA R16).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI")
	bool bFearsLight = false;

	/** How close a carried light keeps it (bFearsLight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "0"))
	float LightFearRadius = 600.f;

	/** Group id shared by NPCs spawned together (0 = none): a hit on one alerts the whole pack. Set by the spawner. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "VoxelCharacter|AI")
	int32 PackId = 0;

	/**
	 * Change the temperament at runtime (forwarded to the AI controller).
	 * @param InTemperament New temperament
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AI")
	void SetTemperament(EVCNPCTemperament InTemperament);

	/**
	 * Death visual when there is no physics asset to ragdoll (static-mesh placeholders): the body
	 * topples over ToppleSeconds and sinks a little. 0 disables.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float ToppleSeconds = 0.4f;

	/** True after the death flow ran on this machine (authority or via replication). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	bool IsDead() const;

	/**
	 * Show a floating health bar over this NPC on screens that render it (not on dedicated servers): hidden until its
	 * health first changes, then shown for HealthBarVisibleSeconds after each change, removed on death. Off by default
	 * (a per-NPC choice, maybe a skill later); the training dummy turns it on. vc.UI.NPCHealthBars: 0 = never,
	 * 1 = this flag (default), 2 = every NPC.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|UI")
	bool bShowHealthBar = false;

	/** Seconds the bar stays up after the last health change. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|UI", meta = (ClampMin = "0.5"))
	float HealthBarVisibleSeconds = 5.f;

	/**
	 * Turn the floating health bar on or off at runtime (creates it when needed).
	 * @param bShow True to show the bar on health changes
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void SetShowHealthBar(bool bShow);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * The eyes: BaseEyeHeight along local up (spherical Phase 4, WM-SY12): sight traces and the melee sweep start at the
	 * head on a planet too. Without custom gravity this is the engine's location.
	 */
	virtual FVector GetPawnViewLocation() const override;

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

	/** Screen-space host of the floating health bar (created at runtime while a bar is wanted). */
	UPROPERTY(Transient)
	TObjectPtr<UWidgetComponent> HealthBarComponent;

	/** Hides the bar HealthBarVisibleSeconds after the last change. */
	FTimerHandle HealthBarHideTimer;

	/** Create / remove the bar to match bShowHealthBar and vc.UI.NPCHealthBars. */
	void UpdateHealthBar();

	/** Health changed: update the bar, show it and restart the hide timer. */
	UFUNCTION()
	void HandleHealthBarHealthChanged(float NewHealth, float OldHealth, float MaxHealth);

	/** Hide the bar (timer). */
	void HideHealthBar();

	// Topple animation state (every machine; starts in HandleDied when no physics asset exists).
	bool bToppling = false;
	float ToppleElapsed = 0.f;
	FRotator ToppleStartRotation = FRotator::ZeroRotator;
	FVector ToppleStartLocation = FVector::ZeroVector;
};
