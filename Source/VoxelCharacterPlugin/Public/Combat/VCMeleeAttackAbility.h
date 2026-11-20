// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "VCMeleeAttackAbility.generated.h"

class UVCActionMontageComponent;

/**
 * Melee attack: a server-side sweep along the avatar's view, one hit on the first hostile damageable, damage from the
 * main-hand weapon fragment or bare hands.
 *
 * Net policy is ServerOnly: the client requests activation (TryActivateAbilitiesByTag with Ability.Attack.Melee from
 * the primary-action input chain) and the server traces and applies, so a client can never fabricate a hit.
 *
 * The swing has a montage when the avatar's UVCActionMontageComponent has one for what it holds (character animation
 * P3, D49): the owning player plays it at once on input, the server sends it to everyone else, and the sweep and the
 * damage wait for the montage's hit time, read from the asset (UVCActionAnimSet::GetHitTime). Without a montage the
 * hit is immediate.
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

	/**
	 * Attacks per second for what the avatar holds: the main-hand weapon fragment's AttackSpeed, else UnarmedAttackSpeed.
	 * The owner's swing uses it to pick the same play rate and rate limit the server applies.
	 * @param Avatar The attacker
	 * @return Attacks per second
	 */
	float GetAttackSpeed(const AActor* Avatar) const;

	/**
	 * Whether an avatar's main hand holds a melee weapon: the primary action's swing at the air when nothing hostile is
	 * under the crosshair (G174).
	 * @param Avatar The character
	 * @return True for a main-hand item (its own slot's or drawn from a sheath) with a Weapon fragment and no Ranged
	 *         Weapon fragment
	 */
	static bool HoldsMeleeWeapon(const AActor* Avatar);

	/**
	 * Whether the plunge's slam reaches a target (the sword moves plan, D169): within the radius of the feet across local
	 * up, and within it along local up.
	 * @param Feet   The attacker's feet (the capsule's bottom)
	 * @param Up     Local up there (the gravity frame's)
	 * @param Target The target's location
	 * @param Radius The set's PlungeRadius
	 * @return True when the slam hits it
	 */
	static bool IsInSlam(const FVector& Feet, const FVector& Up, const FVector& Target, float Radius);

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

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

	/** The sweep and the damage of one swing (authority), at its hit time. */
	void ApplySwing(AActor* Avatar, const FAttackParameters& Attack) const;

	/** The swing's hit time arrived: apply it unless the attacker went down meanwhile, then end. */
	UFUNCTION()
	void OnHitTime();

	/** The plunge's slam (authority, D169): every hostile damageable within the radius of the feet takes the attack. */
	void ApplySlam(AActor* Avatar, const FAttackParameters& Attack, float Radius) const;

	/** The server's fall ended during a plunge: the slam on the ground (not in the water) unless the attacker went down, then end. */
	void OnPlungeLanded(bool bOnGround);

	/** Stops waiting for the plunge's landing. */
	void StopWaitingForLanding();

	/** The plunge waiting for the server's landing: its montage component, the binding and the slam's radius. */
	TWeakObjectPtr<UVCActionMontageComponent> PlungeMontages;
	FDelegateHandle PlungeLandedHandle;
	float PlungeRadius = 0.f;
	bool bPlungePending = false;

	/** World time of the last activation that got past the rate limit. */
	double LastAttackTimeSeconds = -1.0;

	/** The swing waiting for its hit time. */
	FAttackParameters PendingAttack;
};
