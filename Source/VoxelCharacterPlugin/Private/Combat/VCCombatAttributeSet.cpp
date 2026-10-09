// Copyright Daniel Raquel. All Rights Reserved.

#include "Combat/VCCombatAttributeSet.h"
#include "Net/UnrealNetwork.h"

UVCCombatAttributeSet::UVCCombatAttributeSet()
{
	InitAttackPower(0.f);
	InitDefense(0.f);
}

void UVCCombatAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UVCCombatAttributeSet, AttackPower, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVCCombatAttributeSet, Defense, COND_None, REPNOTIFY_Always);
}

void UVCCombatAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	// Negative attack power or defense has no meaning in the damage formula.
	if (Attribute == GetAttackPowerAttribute() || Attribute == GetDefenseAttribute())
	{
		NewValue = FMath::Max(0.f, NewValue);
	}
}

void UVCCombatAttributeSet::OnRep_AttackPower(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVCCombatAttributeSet, AttackPower, OldValue);
}

void UVCCombatAttributeSet::OnRep_Defense(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVCCombatAttributeSet, Defense, OldValue);
}
