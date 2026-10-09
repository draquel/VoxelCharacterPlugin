// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Interfaces/CGFDamageableInterface.h"
#include "Types/CGFCombatTypes.h"
#include "Core/VCTypes.h"
#include "VCCombatComponent.generated.h"

class UAbilitySystemComponent;
class UGameplayEffect;
struct FOnAttributeChangeData;

/**
 * Makes its owner a combatant: faction, damage intake, death / knock-out state.
 *
 * Implements ICGFDamageableInterface (found via UCGFCombatStatics::FindDamageable)
 * so attackers never need to know the pawn class. Works for both ASC placements —
 * the player (ASC on AVCPlayerState) and NPCs (ASC on the pawn) — because it
 * resolves the ASC through IAbilitySystemInterface, not FindComponentByClass.
 *
 * Lifecycle: the owner calls InitializeWithAbilitySystem once its ASC is bound
 * (player: PossessedBy / OnRep_PlayerState; NPC: BeginPlay). From then on the
 * component watches Health and, on authority, turns zero health into Dead or
 * Downed per OutOfHealthPolicy. State replicates through bIsDead / bIsDowned
 * (for client visuals) and through replicated loose tags State.Dead / State.Downed
 * (for ability activation checks on every machine).
 *
 * Damage application is server-only: UVCCombatComponent::ApplyDamage validates,
 * builds a UVCDamageEffect spec and applies it; the attribute set and
 * UVCDamageExecution do the arithmetic.
 */
UCLASS(ClassGroup = (VoxelCharacter), meta = (BlueprintSpawnableComponent))
class VOXELCHARACTERPLUGIN_API UVCCombatComponent : public UActorComponent, public ICGFDamageableInterface
{
	GENERATED_BODY()

public:
	UVCCombatComponent();

	// =================================================================
	// Configuration
	// =================================================================

	/** Faction.* this combatant fights for. Empty = non-combatant (never a valid target). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "VoxelCharacter|Combat", meta = (Categories = "Faction"))
	FGameplayTag FactionTag;

	/** What zero health means for this combatant. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Combat")
	EVCOutOfHealthPolicy OutOfHealthPolicy = EVCOutOfHealthPolicy::Die;

	/** Seconds a Downed combatant stays recoverable before being promoted to Dead. 0 = never. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float DownedTimeout = 0.f;

	/** Effect applied per hit. Defaults to UVCDamageEffect; swap for a subclass to add modifiers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Combat")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	// =================================================================
	// Lifecycle (called by the owner)
	// =================================================================

	/**
	 * Bind to the ability system that owns this combatant's attributes. Safe to call again
	 * after a re-possess; the previous binding is dropped first.
	 * @param ASC Ability system component holding UVCCharacterAttributeSet. Null unbinds.
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Combat")
	void InitializeWithAbilitySystem(UAbilitySystemComponent* ASC);

	/** Drop the attribute binding (owner EndPlay or ASC teardown). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Combat")
	void UninitializeFromAbilitySystem();

	// =================================================================
	// Damage
	// =================================================================

	/**
	 * Server-only. Validate the hit and push it through the damage effect.
	 * @param Context The hit, as built by the attacker.
	 * @return Applied, or the specific rejection reason. Never throws, never partially applies.
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Combat")
	ECGFDamageResult ApplyDamage(const FCGFDamageContext& Context);

	/**
	 * Server-only. Force death regardless of health (downed timeout, scripted kills, debug).
	 * @param Context Recorded as the killing hit; may be empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Combat")
	void Kill(const FCGFDamageContext& Context);

	/**
	 * Server-only. Clear Dead / Downed and restore health. Also clears a State.Dead / State.Downed
	 * tag left on a persistent ASC (player state) by a previous avatar, which is why the player
	 * calls it on respawn even though the new component's own flags are already clear.
	 * @param HealthFraction Fraction of MaxHealth to restore to (0..1).
	 * @return True if any state was cleared or health was set.
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Combat")
	bool Revive(float HealthFraction = 1.f);

	// =================================================================
	// Queries
	// =================================================================

	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	bool IsDowned() const { return bIsDowned; }

	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	float GetHealth() const;

	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	float GetMaxHealth() const;

	/** Health / MaxHealth, 0 when MaxHealth is 0. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	float GetHealthNormalized() const;

	/** The ASC this combatant's attributes live on, resolved through IAbilitySystemInterface. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	UAbilitySystemComponent* GetAbilitySystemComponent() const;

	// =================================================================
	// Events
	// =================================================================

	UPROPERTY(BlueprintAssignable, Category = "VoxelCharacter|Combat")
	FOnVCHealthChanged OnHealthChanged;

	/** Died. Server: the killing hit. Clients: empty context. Fires once per death. */
	UPROPERTY(BlueprintAssignable, Category = "VoxelCharacter|Combat")
	FOnVCCombatantStateChanged OnDied;

	/** Knocked out (OutOfHealthPolicy::Downed). Same server/client context rule as OnDied. */
	UPROPERTY(BlueprintAssignable, Category = "VoxelCharacter|Combat")
	FOnVCCombatantStateChanged OnDowned;

	/** Revived from Dead / Downed. Server only. */
	UPROPERTY(BlueprintAssignable, Category = "VoxelCharacter|Combat")
	FOnVCCombatantRevived OnRevived;

	// =================================================================
	// ICGFDamageableInterface
	// =================================================================

	virtual FGameplayTag GetFactionTag_Implementation() const override { return FactionTag; }
	virtual bool IsDead_Implementation() const override { return bIsDead; }
	virtual bool IsImmuneToDamage_Implementation(const FCGFDamageContext& Context) const override { return false; }

	// =================================================================
	// UActorComponent
	// =================================================================

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	/** Replicated so clients can play death visuals without reading the ASC. */
	UPROPERTY(ReplicatedUsing = OnRep_IsDead)
	bool bIsDead = false;

	UPROPERTY(ReplicatedUsing = OnRep_IsDowned)
	bool bIsDowned = false;

	UFUNCTION()
	void OnRep_IsDead();

	UFUNCTION()
	void OnRep_IsDowned();

	/** Health attribute delegate target. */
	void HandleHealthChanged(const FOnAttributeChangeData& Data);

	/** Authority: Health hit zero while alive. Routes to EnterDowned / EnterDead per policy. */
	void HandleOutOfHealth();

	void EnterDowned();
	void EnterDead();
	void HandleDownedTimeout();

	bool HasAuthority() const;

	/** ASC the Health delegate is bound on. */
	TWeakObjectPtr<UAbilitySystemComponent> BoundASC;
	FDelegateHandle HealthChangedHandle;
	FTimerHandle DownedTimerHandle;

	/** The hit that most recently got through ApplyDamage; reported as the killing/downing hit. */
	FCGFDamageContext LastDamageContext;
};
