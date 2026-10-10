// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "VCMeleeAttackAbility.generated.h"

/**
 * Minimal melee attack: a server-side sweep along the avatar's view, one hit on
 * the first hostile damageable, damage from the main-hand weapon fragment or
 * bare hands. No animation, no hit reaction — those are later features.
 *
 * Net policy is ServerOnly: the client requests activation (TryActivateAbilitiesByTag
 * with Ability.Attack.Melee from the primary-action input chain) and the server
 * traces and applies, so a client can never fabricate a hit.
 *
 * Granted to players natively through AVCPlayerState::MeleeAttackAbilityClass.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCMeleeAttackAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UVCMeleeAttackAbility();

	/** How far from the eyes the attack reaches (uu). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float MeleeRange = 250.f;

	/** Sweep radius so near-misses still connect (uu). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float MeleeRadius = 30.f;

	/** Damage when no weapon fragment is equipped in the main hand. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float UnarmedDamage = 5.f;

	/** Attacks per second when unarmed. A weapon fragment's AttackSpeed overrides it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0.01"))
	float UnarmedAttackSpeed = 1.f;

	/** Damage multiplier for a weapon at zero durability that was not destroyed (feature 5b). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0", ClampMax = "1"))
	float WornOutDamageMultiplier = 0.5f;

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	/** Everything one swing needs, resolved from the main-hand weapon fragment or unarmed defaults. */
	struct FAttackParameters
	{
		float Damage = 0.f;
		FGameplayTag DamageType;
		float AttackSpeed = 1.f;
		float CritChance = 0.f;
		float CritMultiplier = 1.f;
		FGuid ItemInstanceId;
		bool bWornOut = false;
	};

	/** After a landed hit: wear the main-hand weapon by its Durability fragment's DegradeRate (authority). */
	void ApplyWeaponWear(AActor* Avatar) const;

	/** Resolve damage, type, attack speed, crit and item id from the equipped main-hand item, or unarmed defaults. */
	FAttackParameters ResolveAttackParameters(const AActor* Avatar) const;

	/** World time of the last activation that got past the rate limit. */
	double LastAttackTimeSeconds = -1.0;
};
