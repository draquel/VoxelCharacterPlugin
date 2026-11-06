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
