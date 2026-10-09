// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Core/VCCharacterAttributeSet.h"   // ATTRIBUTE_ACCESSORS
#include "VCCombatAttributeSet.generated.h"

/**
 * Offensive / defensive combat attributes, separate from the vitals in
 * UVCCharacterAttributeSet so equipment and buffs can modify them without
 * touching Health.
 *
 * Lives beside the character attribute set on whatever owns the ASC
 * (AVCPlayerState for players, AVCNPCCharacterBase for NPCs). Consumed by
 * UVCDamageExecution: damage = Base + AttackPower(source) - Defense(target).
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCCombatAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UVCCombatAttributeSet();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

	/** Flat damage added to every hit this combatant lands. */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AttackPower, Category = "VoxelCharacter|Attributes")
	FGameplayAttributeData AttackPower;
	ATTRIBUTE_ACCESSORS(UVCCombatAttributeSet, AttackPower)

	/** Flat damage removed from every mitigated hit this combatant takes (Damage.Type.Pure ignores it). */
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Defense, Category = "VoxelCharacter|Attributes")
	FGameplayAttributeData Defense;
	ATTRIBUTE_ACCESSORS(UVCCombatAttributeSet, Defense)

protected:
	UFUNCTION()
	void OnRep_AttackPower(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Defense(const FGameplayAttributeData& OldValue);
};
