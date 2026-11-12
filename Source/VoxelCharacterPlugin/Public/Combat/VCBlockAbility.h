// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "VCBlockAbility.generated.h"

class UAbilitySystemComponent;
struct FCGFDamageContext;

/** A shield's numbers (ItemInventoryPlugin's Shield fragment), read from the item in the off hand. */
struct VOXELCHARACTERPLUGIN_API FVCShieldStats
{
	/** The share of a blocked hit's damage the guard takes. */
	float DamageReduction = 0.f;

	/** How far to either side of the facing a blocked hit's source may be (degrees). */
	float HalfAngle = 0.f;

	/** Stamina per point of damage the guard takes. */
	float StaminaPerDamage = 0.f;
};

/**
 * The shield's guard (D94): held while the secondary action is, with a shield in the off hand. Net policy ServerOnly, as
 * the attacks: the owner plays its guard at once and asks for the activation; the server holds State.Blocking while the
 * ability runs (its activation-owned tag) and plays the guard for everyone else. A hit taken while the tag is held from
 * within the shield's arc loses the shield's share of its damage and costs stamina (ApplyGuard, from
 * UVCCombatComponent::ApplyDamage); a hit the stamina cannot pay for breaks the guard (BreakGuard).
 *
 * Granted to players through AVCPlayerState::BlockAbilityClass.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCBlockAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UVCBlockAbility();

	/** Seconds no guard can be raised after one breaks (State.GuardBroken). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float GuardBreakSeconds = 1.5f;

	/**
	 * The shield in an avatar's off hand.
	 * @param Avatar   The character
	 * @param OutStats Its numbers, when it holds one
	 * @return True when the off hand holds an item with a Shield fragment
	 */
	static bool GetHeldShield(const AActor* Avatar, FVCShieldStats& OutStats);

	/**
	 * Whether an avatar can raise its guard now: a shield in the off hand, the guard not broken, not dead or downed. The
	 * owner's check before it plays its guard; the server's activation runs the same one.
	 * @param Avatar The character
	 * @return True when a block may start
	 */
	static bool CanStartBlock(const AActor* Avatar);

	/**
	 * Lowers the guard: the secondary action's release, sent to the server on a client (as the bow's draw is released).
	 * @param ASC The avatar's ability system
	 * @return True when the ability is granted
	 */
	static bool ReleaseBlock(UAbilitySystemComponent* ASC);

	/**
	 * Whether a hit's source lies within a guard's arc.
	 * @param Forward          The holder's facing
	 * @param Up               The holder's local up (the gravity frame's)
	 * @param ToSource         From the holder toward the hit's source
	 * @param HalfAngleDegrees The shield's half angle
	 * @return True within the arc, measured about Up; false for a source straight above or below
	 */
	static bool IsInBlockArc(const FVector& Forward, const FVector& Up, const FVector& ToSource, float HalfAngleDegrees);

	/**
	 * How a guard meets a hit: the shield takes its share of the damage while the stamina pays for it; what the stamina
	 * cannot pay for lands, and then the guard breaks.
	 * @param Damage         The hit's damage
	 * @param Stamina        The holder's stamina
	 * @param Shield         The shield
	 * @param OutDamage      The damage that lands
	 * @param OutStaminaCost The stamina the guard spends
	 * @return True when the guard breaks
	 */
	static bool ResolveBlockedHit(float Damage, float Stamina, const FVCShieldStats& Shield, float& OutDamage, float& OutStaminaCost);

	/**
	 * Authority, from the target's damage path: a hit on a guarding target from within its shield's arc loses the shield's
	 * share (Damage.Blocked joins the context's tags) and costs stamina, and a hit the stamina cannot pay for breaks the
	 * guard. Pure damage and a hit with no source (a fall, a hazard) are not blocked.
	 * @param Target         The character hit
	 * @param TargetASC      Its ability system
	 * @param InOutContext   The hit; its BaseDamage and ContextTags change when the guard takes it
	 * @return True when the guard took the hit
	 */
	static bool ApplyGuard(AActor* Target, UAbilitySystemComponent* TargetASC, FCGFDamageContext& InOutContext);

	/**
	 * Authority: the guard breaks. The block ends, State.GuardBroken holds for GuardBreakSeconds, and everyone sees the
	 * stagger.
	 * @param Target    The character
	 * @param TargetASC Its ability system
	 */
	static void BreakGuard(AActor* Target, UAbilitySystemComponent* TargetASC);

protected:
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;
};
