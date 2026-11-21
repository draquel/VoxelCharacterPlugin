// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Interface.h"
#include "Core/VCTypes.h"
#include "VCAnimatedCharacter.generated.h"

class UVCCombatComponent;
class UVCLocomotionAnimSet;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UVCAnimatedCharacter : public UInterface
{
	GENERATED_BODY()
};

/**
 * What UVCAnimInstance reads from a character besides its movement: the view mode it is drawn for, the action set of
 * what it holds and its combat component. AVCCharacterBase and AVCNPCCharacterBase implement it, so one anim instance
 * class serves the player and the NPCs (character animation P2, CHARACTER_ANIMATION_PLAN.md).
 */
class VOXELCHARACTERPLUGIN_API IVCAnimatedCharacter
{
	GENERATED_BODY()

public:
	/**
	 * The view mode the character's mesh is drawn for on this machine.
	 * @return The local player's mode for its own pawn; third person for everyone else and for NPCs
	 */
	virtual EVCViewMode GetAnimViewMode() const = 0;

	/**
	 * The action set of what the character holds: which layer and montages animate it (Equipment.AnimSet.*).
	 * @return Equipment.AnimSet.Unarmed when it holds nothing
	 */
	virtual FGameplayTag GetAnimActionSet() const = 0;

	/**
	 * The combat component, for the dead and downed states.
	 * @return The component, or null for a character without one
	 */
	virtual const UVCCombatComponent* GetAnimCombatComponent() const = 0;

	/**
	 * The body's own locomotion, for the creature template AnimBP (character animation P7).
	 * @return The set, or null for a body that plays the Mannequin's graph
	 */
	virtual const UVCLocomotionAnimSet* GetAnimLocomotionSet() const { return nullptr; }

	/**
	 * Whether the character aims a ranged weapon (the bow raised, AVCCharacterBase::IsAiming).
	 * @return False for a character that never aims
	 */
	virtual bool IsAnimAiming() const { return false; }
};
