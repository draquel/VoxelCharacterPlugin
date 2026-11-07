// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "VCRangedAttackAbility.generated.h"

class AVCArrowProjectile;
class UAbilitySystemComponent;

/**
 * The bow's attack (ranged combat plan B3, D56): hold to draw, release to shoot, for whatever ranged weapon the main
 * hand holds (UItemFragment_RangedWeapon).
 *
 * Net policy is ServerOnly, as the melee attack's: the owner's press asks the server to activate it
 * (TryActivateAbilitiesByTag with Ability.Attack.Ranged from the primary-action chain) and its release reaches the server
 * as the ability system component's input release (ReleaseDraw), which travels on the same channel as the activation
 * request and cannot overtake it. The server times the draw between the two, so a client cannot shoot faster than its
 * draw. At the release a draw shorter than the weapon's minimum is no shot; otherwise the server uses up one arrow from
 * the inventory, spawns it (AVCArrowProjectile::SpawnArrow) at the bow toward what the view meets, wears the bow by its
 * Durability fragment's DegradeRate, and sends the release to everyone else. Without an arrow the draw does not start
 * (CanStartDraw: the owner's check and the server's), and a release that finds none left is refused.
 *
 * The draw and the release have montages when the avatar's UVCActionMontageComponent has them (EVCAnimAction::Draw and
 * ::Release): the owning player plays its own at input and the server sends them to everyone else (D49). Granted to players
 * through AVCPlayerState::RangedAttackAbilityClass.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCRangedAttackAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UVCRangedAttackAbility();

	/** Where the arrow starts, from the eyes in the view's frame (X ahead, Y right, Z up): ahead of the face, by the bow's hand. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Combat")
	FVector ArrowStartOffset = FVector(50.f, -10.f, -20.f);

	/** How far the aim reaches (uu): the arrow flies toward what the view meets within it, else toward the view's point there. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "100"))
	float AimRange = 20000.f;

	/** Damage multiplier for a bow worn to zero durability that was not destroyed (as the melee attack's). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0", ClampMax = "1"))
	float WornOutDamageMultiplier = 0.5f;

	/**
	 * Whether an avatar's main hand holds a ranged weapon: the primary action's bow step.
	 * @param Avatar The character
	 * @return True for a main-hand item with a Ranged Weapon fragment
	 */
	static bool HoldsRangedWeapon(const AActor* Avatar);

	/**
	 * Whether an avatar can start a draw: a ranged weapon in its main hand and, when the weapon shoots ammo, some in its
	 * inventory. One predicate for the owner's input and the server's activation (D-CR16).
	 * @param Avatar The character
	 * @return True when a draw may start
	 */
	static bool CanStartDraw(const AActor* Avatar);

	/**
	 * The power a draw of this long gives the avatar's ranged weapon (UItemFragment_RangedWeapon::GetDrawPower).
	 * @param Avatar The character
	 * @param DrawSeconds How long the weapon was drawn
	 * @return 0 to 1; 0 below the shortest draw or without a ranged weapon
	 */
	static float GetDrawPower(const AActor* Avatar, float DrawSeconds);

	/**
	 * Releases an avatar's draw: at once on the server, from a client through the ability system component's input-release
	 * RPC. Nothing happens when no draw is active there.
	 * @param ASC The avatar's ability system component
	 * @return True when the component has a ranged attack to release
	 */
	static bool ReleaseDraw(UAbilitySystemComponent* ASC);

	/**
	 * Where an avatar's arrow starts and which way it flies: from ArrowStartOffset toward the point its view meets within
	 * AimRange (a thin sphere along the eyes' view on the Pawn channel, the avatar ignored), so the arrow converges on what
	 * the crosshair shows.
	 * @param Avatar The shooter
	 * @param OutStart The arrow's start
	 * @param OutDirection Its unit direction
	 * @return False without a world
	 */
	bool ComputeShot(const AActor* Avatar, FVector& OutStart, FVector& OutDirection) const;

	/** @return True between an activation and its release */
	bool IsDrawing() const { return bDrawing; }

	/** @return The arrow of the last shot, if it still flies or sticks */
	AVCArrowProjectile* GetLastArrow() const { return LastArrow.Get(); }

protected:
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/**
	 * Authority: the shot at the release.
	 * @param Avatar The shooter
	 * @param DrawSeconds How long the draw lasted on the server
	 * @return True when an arrow flew; false for a short draw, no ranged weapon or no arrow left (nothing used up)
	 */
	bool Shoot(AActor* Avatar, float DrawSeconds);

	/** Server time the current draw started. */
	double DrawStartTime = -1.0;

	/** Between an activation and its release. */
	bool bDrawing = false;

	TWeakObjectPtr<AVCArrowProjectile> LastArrow;
};
