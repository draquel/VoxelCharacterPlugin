// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Animation/VCActionAnimSet.h"
#include "Types/CGFCombatTypes.h"
#include "VCActionMontageComponent.generated.h"

class UAnimInstance;
class UAnimMontage;

/**
 * Plays a character's action montages (character animation P3): attacks, digging, chopping, placing, using, hit reacts,
 * deaths and getting up, from the UVCActionAnimSet of what the character holds (IVCAnimatedCharacter::GetAnimActionSet).
 *
 * Montages are cosmetic. The owner plays its own actions at once on input (PlayActionLocal); the server, once it has
 * accepted the action, tells every other machine with one unreliable multicast (PlayActionForEveryone), which the owner
 * skips, so nothing plays twice (D49). Server-side effects that wait for a montage (the melee hit) read the time from the
 * asset, so a dedicated server, which plays no montage, still times them.
 *
 * On the server it also answers the owner's UVCCombatComponent: a hit react from the side the hit came from, a death
 * montage for a body with no physics asset (one with a physics asset ragdolls), getting up when revived.
 *
 * On AVCCharacterBase and AVCNPCCharacterBase. It replicates only for its multicast.
 */
UCLASS(ClassGroup = (VoxelCharacter), meta = (BlueprintSpawnableComponent))
class VOXELCHARACTERPLUGIN_API UVCActionMontageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVCActionMontageComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** The action sets this character plays, one per Equipment.AnimSet tag; the Unarmed set covers a tag with none. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TArray<TObjectPtr<UVCActionAnimSet>> ActionAnimSets;

	/**
	 * The set for an action-set tag.
	 * @param ActionSet An Equipment.AnimSet.* tag
	 * @return The set with that tag, else the Unarmed set, else null
	 */
	const UVCActionAnimSet* FindActionAnimSet(const FGameplayTag& ActionSet) const;

	/** The set for what the owner holds now. @return It, or null when the owner has none */
	const UVCActionAnimSet* GetCurrentActionAnimSet() const;

	/**
	 * The montage the owner plays now for an action.
	 * @param Action  The action
	 * @param Variant The combo index, or an EVCHitDirection for hit reacts and deaths
	 * @return The current set's montage, or null
	 */
	UAnimMontage* GetMontage(EVCAnimAction Action, int32 Variant = 0) const;

	/**
	 * The next attack of the combo on this machine: the next montage while attacks follow each other within the set's
	 * window, else the first.
	 * @param AttackSpeed  Attacks per second (the weapon's)
	 * @param OutPlayRate  The play rate that fits the montage into one attack's interval
	 * @return The combo index (0 with no combo)
	 */
	int32 ChooseAttack(float AttackSpeed, float& OutPlayRate);

	/**
	 * Plays an action's montage on this machine only. A repeated non-attack action that is still playing keeps playing.
	 * A draw (a montage of three sections or more: the raise, the draw, the hold) starts at its second section when the
	 * aim has already raised the weapon (UVCAnimInstance::AimRaiseWeight past one half), so the bow is not raised twice.
	 * @param Action   The action
	 * @param Variant  The combo index, or an EVCHitDirection
	 * @param PlayRate Play rate
	 * @return The montage's length at that rate, 0 when nothing played (no montage, no anim instance)
	 */
	float PlayActionLocal(EVCAnimAction Action, int32 Variant = 0, float PlayRate = 1.f);

	/**
	 * Authority: plays an action on every machine that draws the character, through one unreliable multicast.
	 * @param Action         The action
	 * @param Variant        The combo index, or an EVCHitDirection
	 * @param PlayRate       Play rate
	 * @param bOwnerPlayedIt True when the owning player already played it at input: its machine skips it
	 */
	void PlayActionForEveryone(EVCAnimAction Action, int32 Variant = 0, float PlayRate = 1.f, bool bOwnerPlayedIt = false);

	/**
	 * Stops an action's montage on this machine (a bow's draw let go before it can shoot).
	 * @param Action The action
	 * @return True when its montage was playing
	 */
	bool StopActionLocal(EVCAnimAction Action);

	/**
	 * Whether an action's montage plays on this machine (the current set's).
	 * @param Action The action
	 * @return True while its montage plays
	 */
	bool IsActionPlaying(EVCAnimAction Action) const;

	/**
	 * Authority: stops an action on every machine that draws the character, through one reliable multicast (a lost stop
	 * would leave a looping draw playing).
	 * @param Action          The action
	 * @param bOwnerStoppedIt True when the owning player already stopped it at input: its machine skips it
	 */
	void StopActionForEveryone(EVCAnimAction Action, bool bOwnerStoppedIt = false);

	/**
	 * Plays an unsheathe or a sheathe on this machine (the equipment sheathing plan, D79): the montage of the weapon's own
	 * set for its sheath, which need not be the set the owner holds now (an unsheathe starts with the hand empty). This
	 * machine remembers it, so the weapon's visual changes bone at the montage's hand-off (IsSheathHandOffPending).
	 * @param Action    Unsheathe or Sheathe
	 * @param ActionSet The weapon's set (Equipment.AnimSet.*)
	 * @param Sheath    The weapon's sheath slot
	 * @return The montage's length, 0 when nothing played
	 */
	float PlaySheathActionLocal(EVCAnimAction Action, const FGameplayTag& ActionSet, const FGameplayTag& Sheath);

	/**
	 * Authority: plays an unsheathe or a sheathe on every machine that draws the character (one unreliable multicast).
	 * @param bOwnerPlayedIt True when the owning player already played it at input: its machine skips it
	 */
	void PlaySheathActionForEveryone(EVCAnimAction Action, const FGameplayTag& ActionSet, const FGameplayTag& Sheath, bool bOwnerPlayedIt);

	/**
	 * The montage an unsheathe or a sheathe plays and its hand-off time, from the assets (a dedicated server times the
	 * draw with this; it plays nothing).
	 * @param OutHandOff Seconds into it at play rate 1
	 * @return The montage, or null when the set has none for that sheath
	 */
	UAnimMontage* GetSheathMontage(EVCAnimAction Action, const FGameplayTag& ActionSet, const FGameplayTag& Sheath, float& OutHandOff) const;

	/**
	 * Whether this machine plays an unsheathe or a sheathe of a sheath and has not reached its hand-off: until then the
	 * weapon's visual stays where the montage started it (at the sheath for an unsheathe, in the hand for a sheathe).
	 * @param Sheath    The sheath slot
	 * @param OutAction Unsheathe or Sheathe, when pending
	 * @return True while pending
	 */
	bool IsSheathHandOffPending(const FGameplayTag& Sheath, EVCAnimAction& OutAction) const;

	/** Whether an unsheathe or a sheathe montage plays on this machine (the owner ignores the primary action meanwhile). */
	bool IsSheathActionPlaying() const;

	/** How many actions this machine was asked to play (locally or by the multicast), for tests. */
	int32 GetActionsPlayed() const { return ActionsPlayed; }

	/** The last action this machine was asked to play, for tests. */
	EVCAnimAction GetLastAction() const { return LastAction; }

	/**
	 * The side a hit came from for the owner: its source (the causer, else the instigator, else the hit location)
	 * measured about the owner's local up from its facing.
	 * @param Context The hit
	 * @return An EVCHitDirection, as a montage variant
	 */
	int32 GetHitDirection(const FCGFDamageContext& Context) const;

protected:
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayAction(EVCAnimAction Action, uint8 Variant, float PlayRate, bool bOwnerPlayedIt);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_StopAction(EVCAnimAction Action, bool bOwnerStoppedIt);

	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlaySheathAction(EVCAnimAction Action, FGameplayTag ActionSet, FGameplayTag Sheath, bool bOwnerPlayedIt);

	/** The unsheathe or sheathe this machine played last, for the visual's hand-off. */
	EVCAnimAction SheathAction = EVCAnimAction::Unsheathe;
	FGameplayTag SheathSlot;
	TWeakObjectPtr<UAnimMontage> SheathMontage;

	/** Server: a hit landed on the owner. */
	UFUNCTION()
	void HandleDamaged(const FCGFDamageContext& Context);

	/** Server: the owner died. */
	UFUNCTION()
	void HandleDied(const FCGFDamageContext& Context);

	/** Server: the owner was revived. */
	UFUNCTION()
	void HandleRevived();

private:
	/** The owner's body's anim instance, or null. */
	UAnimInstance* GetBodyAnimInstance() const;

	/** The combo index of the last attack, and until when the next one continues the combo (world seconds). */
	int32 ComboIndex = -1;
	double ComboWindowEnd = -1.0;

	int32 ActionsPlayed = 0;
	EVCAnimAction LastAction = EVCAnimAction::Attack;
};
