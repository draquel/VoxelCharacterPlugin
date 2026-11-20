// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "Animation/VCActionMontageComponent.h"
#include "VCDodgeAbility.generated.h"

/**
 * The dodge and the roll (the sword moves plan, phase F, D170): a step or a roll that moves the capsule by its montage's
 * root motion whatever the input (D168), costs stamina, and holds State.Invulnerable on the server for most of its
 * travel.
 *
 * The owner picks the move at its press (ChooseMove: a roll toward the movement input, a step back without input, a
 * step toward the input relative to the facing while the facing is locked to the view), checks it (CanStartMove),
 * plays its montage at once and asks the server (AVCCharacterBase::Server_RequestMove, D165). This ability is
 * ServerOnly: the server checks the same predicate with the request's allowance, spends the stamina, turns its capsule
 * to the facing the owner's move starts with (a roll's input, a dodge's facing), plays the montage for everyone else (a dedicated server plays it too, for its root
 * motion: D106), cancels a swing in progress (its hit does not land) and a raised guard, and holds the immunity from the
 * start to the montage's UVCAnimNotify_ActionTravelled (a dodge about 0.3 s, a roll 0.44 s).
 *
 * Granted to players through AVCPlayerState::DodgeAbilityClass. An NPC granted it dodges back when it activates it.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCDodgeAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UVCDodgeAbility();

	/** Stamina a dodge step costs (D170). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float DodgeStaminaCost = 15.f;

	/** Stamina a roll costs (D170). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float RollStaminaCost = 20.f;

	/**
	 * The stamina a move costs.
	 * @param Kind Dodge or Roll
	 * @return Its cost, 0 for any other kind
	 */
	float GetStaminaCost(EVCMoveKind Kind) const;

	/**
	 * Pure: which move a press makes (D170). With no movement input, a step back; while the facing is locked to the view
	 * (a bow drawn, the first-person view) or without a roll in the set, a step toward the input's side of the facing;
	 * else a roll toward the input.
	 * @param Input         The movement input or acceleration (world space)
	 * @param Forward       The character's facing
	 * @param Up            Its local up
	 * @param bFacingLocked The facing follows the view
	 * @param bHasRoll      The held set has a roll
	 * @return The request: Dodge with its side (EVCHitDirection) and the facing's horizontal direction, or Roll with the
	 *         input's horizontal direction
	 */
	static FVCMoveRequest ChooseMove(const FVector& Input, const FVector& Forward, const FVector& Up, bool bFacingLocked, bool bHasRoll);

	/**
	 * The check the owner makes at its press and the server at its activation (D-CR16): alive and up, on the ground, the
	 * held set has the move's montage, no dodge or roll before its recovery (less the tolerance), and the stamina for it.
	 * A dodge or a roll may start during an attack at any time.
	 * @param Avatar    The character
	 * @param Kind      Dodge or Roll
	 * @param Index     The dodge's side
	 * @param Tolerance The server's allowance for a remote owner's request (seconds); 0 on the owner
	 * @return True when the move may start
	 */
	bool CanStartMove(const AActor* Avatar, EVCMoveKind Kind, int32 Index, float Tolerance) const;

	/** Whether this ability holds State.Invulnerable on its avatar now (server; for tests). */
	bool HoldsImmunity() const { return bHoldsImmunity; }

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	/** The move's travel is 90 % done: the immunity ends, and the ability with it. */
	UFUNCTION()
	void OnTravelled();

	/** Drops State.Invulnerable if this ability holds it. */
	void ReleaseImmunity();

	bool bHoldsImmunity = false;
};
