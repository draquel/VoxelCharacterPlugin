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
 * the inventory, spawns it (AVCArrowProjectile::SpawnArrow) along the reticle's line (ComputeShot, D83), wears the bow by its
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

	/**
	 * Where the arrow is nocked, from the eyes in the view's frame (X ahead, Y right, Z up): the drawn bow's grip in third
	 * person (measured on CharacterTest: (76, 4, -6) on the low-poly body, (80, 4, -12) on Kellan, steady over +-15 degrees
	 * of pitch). The arrow starts on the aim ray abreast of this point (D83, ComputeLaunchStart).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Combat")
	FVector ArrowStartOffset = FVector(75.f, 4.f, -8.f);

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
	 * Where an avatar's arrow starts and which way it flies (D83): along the aim ray (UVCCombatStatics::ComputeAimRay, the
	 * reticle's line, the same on the owner and the server, D74) from the ray's point abreast of the nocked arrow
	 * (ArrowStartOffset from the eyes). Nothing the ray meets changes the shot, so the reticle is the launch line at every
	 * range and only gravity, wind and the shooter's own motion take the arrow off it (no aim assist: the player holds over
	 * for range and off for wind). A wall between the eyes and that point starts the arrow at the wall.
	 * @param Avatar       The shooter
	 * @param OutStart     The arrow's start
	 * @param OutDirection Its unit direction (the aim ray's)
	 * @return False without a world
	 */
	bool ComputeShot(const AActor* Avatar, FVector& OutStart, FVector& OutDirection) const;

	/**
	 * Pure: the point on the aim ray abreast of the nocked arrow, its projection onto the ray (the ray's origin when the
	 * arrow is behind it).
	 * @param RayOrigin    The aim ray's origin (the aim camera, or the eyes)
	 * @param RayDirection Its unit direction
	 * @param Nock         Where the arrow is nocked (the eyes plus ArrowStartOffset)
	 * @return The arrow's start on the ray
	 */
	static FVector ComputeLaunchStart(const FVector& RayOrigin, const FVector& RayDirection, const FVector& Nock);

	/**
	 * Pure: the arrow's launch velocity, the bow's speed along the aim plus the shooter's own velocity (the bow moves with
	 * the archer).
	 * @param Direction       The unit launch direction
	 * @param Speed           The bow's speed at this draw (uu/s)
	 * @param ShooterVelocity The shooter's velocity (uu/s)
	 * @return The arrow's velocity
	 */
	static FVector ComputeLaunchVelocity(const FVector& Direction, float Speed, const FVector& ShooterVelocity);

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
