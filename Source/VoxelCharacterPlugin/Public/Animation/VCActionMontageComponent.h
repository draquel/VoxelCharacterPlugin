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

/** What a press of the attack does at a moment of the current attack (the sword attack flow plan, D104). */
UENUM()
enum class EVCAttackPress : uint8
{
	/** No attack, or past the combo's window: the first attack of a new combo, at once. */
	NewCombo,
	/** Before the strike: nothing (a press in the wind-up is not kept). */
	Ignore,
	/** From the strike to the recovery: kept, and the next attack plays at the recovery time. */
	Buffer,
	/** In the recovery or the window after it: the next attack of the combo, at once. */
	Continue,
};

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
 * The attack combo (the sword attack flow plan, D102-D104): each machine keeps the attack it plays (its combo index,
 * its start and its hit, recovery and end times at its play rate). A press before the strike does nothing, one from the
 * strike to the recovery is kept until the recovery, one in the recovery or just after continues the combo; the owner
 * picks the index and the server accepts it when the same rule allows it. While an attack's recovery plays, the
 * component ticks and breaks it off with a blend-out when the character wants to move.
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
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** The server's allowance for a request's trip when it checks an owner's attack against its own clock (seconds). */
	static constexpr float AttackRequestTolerance = 0.2f;

	/** Above this speed a simulated proxy counts as moving (uu/s): it has no input to read (D103). */
	static constexpr float MovingSpeed = 50.f;

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
	 * What a press does at a moment of an attack (D104), the rule the owner and the server share.
	 * @param SinceStart   Seconds since the attack started, negative for no attack
	 * @param HitTime      Its hit time, seconds at its play rate
	 * @param RecoveryTime Its recovery time, seconds at its play rate
	 * @param EndTime      Its length, seconds at its play rate
	 * @param Window       Seconds past its end within which the combo continues
	 * @return NewCombo, Ignore, Buffer or Continue
	 */
	static EVCAttackPress ClassifyAttackPress(double SinceStart, float HitTime, float RecoveryTime, float EndTime, float Window);

	/**
	 * Whether movement breaks an attack off now (D103): in its recovery, before its end, while the character wants to move.
	 * @param SinceStart    Seconds since the attack started
	 * @param RecoveryTime  Its recovery time, seconds at its play rate
	 * @param EndTime       Its length, seconds at its play rate
	 * @param bWantsToMove  The character wants to move (WantsToMove)
	 * @return True to stop its montage
	 */
	static bool ShouldBreakRecovery(double SinceStart, float RecoveryTime, float EndTime, bool bWantsToMove);

	/**
	 * The play rate of an attack for a weapon (D107): its attacks per second as a rate, 1 as the clips were authored.
	 * @param AttackSpeed The weapon's attacks per second
	 * @return The rate, from 0.5 to 2
	 */
	static float GetAttackPlayRate(float AttackSpeed);

	/** What a press of the attack does now on this machine, from the attack it plays (ClassifyAttackPress). */
	EVCAttackPress ClassifyAttackPress() const;

	/**
	 * Picks the attack to play now and keeps it as this machine's current attack: the next of the combo when a press now
	 * continues it, else the first.
	 * @param AttackSpeed  Attacks per second (the weapon's)
	 * @param OutPlayRate  The play rate (GetAttackPlayRate)
	 * @return The combo index (0 with no combo)
	 */
	int32 ChooseAttack(float AttackSpeed, float& OutPlayRate);

	/**
	 * Server: whether an attack may start now and which (D104, G179). An attack the owner picked on this very machine at
	 * this moment (a listen server's or a standalone player's) is taken as it is. Otherwise the attack before must have
	 * reached its recovery (less AttackRequestTolerance); the owner's index is taken when it is the first or the next of
	 * the combo within the window (plus the tolerance), else the server picks its own. The attack is kept as current.
	 * @param RequestedIndex The owner's combo index, -1 for none (an NPC)
	 * @param AttackSpeed    Attacks per second (the weapon's)
	 * @param OutIndex       The combo index to play
	 * @param OutPlayRate    The play rate
	 * @return False when it is too early for another attack
	 */
	bool AcceptAttack(int32 RequestedIndex, float AttackSpeed, int32& OutIndex, float& OutPlayRate);

	/**
	 * Server: the combo index the owner asked for with its swing (AVCCharacterBase::Server_RequestMeleeSwing), for the
	 * melee ability's activation that follows; -1 clears it.
	 */
	void SetRequestedAttack(int32 Index) { RequestedAttackIndex = Index; }

	/** Server: the owner's requested combo index, cleared; -1 for none. */
	int32 TakeRequestedAttack() { const int32 Index = RequestedAttackIndex; RequestedAttackIndex = -1; return Index; }

	/** The owner: keep a press made between the strike and the recovery until the recovery (D104). */
	void BufferAttack();

	/** The owner: whether a kept press is due (the current attack reached its recovery); it is consumed. */
	bool TakeDueBufferedAttack();

	/** Whether a press is kept for the recovery. */
	bool HasBufferedAttack() const { return bAttackBuffered; }

	/** The combo index of the current or last attack, -1 before any. */
	int32 GetLastAttackIndex() const { return ComboIndex; }

	/**
	 * Whether the character wants to move (D103): its input's acceleration on the owner and on the server (from the
	 * owner's moves), its speed across local up over MovingSpeed on a simulated proxy.
	 */
	bool WantsToMove() const;

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
	 * Authority: plays an action on every machine that draws the character, through one unreliable multicast. A dedicated
	 * server draws nothing but plays a montage with root motion itself (an attack's lunge, D106), so the movement takes
	 * the lunge there as the owner's does.
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
	 * The action's montage playing on the body and how far in (any variant of a Reload).
	 * @param Action      The action
	 * @param OutPosition Seconds into the montage
	 * @return The playing montage, or null
	 */
	const UAnimMontage* GetPlayingActionMontage(EVCAnimAction Action, float& OutPosition) const;

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

	/** Keeps an attack as this machine's current one, starting now. */
	void RecordAttack(int32 Index, float PlayRate);

	/** Seconds since the current attack started, negative before any. */
	double GetSecondsSinceAttack() const;

	/** The attack montage of the current set playing on the body and its position, or null. */
	UAnimMontage* GetPlayingAttackMontage(float& OutPosition) const;

	/** The current attack (D104): its combo index, its start (world seconds) and its times at its play rate. */
	int32 ComboIndex = -1;
	double AttackStartTime = -1.0;
	float AttackHitTime = 0.f;
	float AttackRecoveryTime = 0.f;
	float AttackEndTime = 0.f;
	float AttackComboWindow = 0.f;
	int32 AttackComboCount = 0;

	/** A press kept for the current attack's recovery (the owner's). */
	bool bAttackBuffered = false;

	/** The combo index the owner asked for, until the melee ability takes it (server). */
	int32 RequestedAttackIndex = -1;

	int32 ActionsPlayed = 0;
	EVCAnimAction LastAction = EVCAnimAction::Attack;
};
