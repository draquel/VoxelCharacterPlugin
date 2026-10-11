// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCCharacterAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "AbilitySystemComponent.h"
#include "VoxelCharacterPlugin.h"

UVCCharacterAttributeSet::UVCCharacterAttributeSet()
{
	InitHealth(100.f);
	InitMaxHealth(100.f);
	InitStamina(100.f);
	InitMaxStamina(100.f);
	InitBreath(20.f);
	InitMaxBreath(20.f);
	InitMoveSpeedMultiplier(1.f);
	InitMiningSpeed(1.f);
	InitInteractionRange(300.f);
	InitIncomingDamage(0.f);
}

// ---------------------------------------------------------------------------
// Replication
// ---------------------------------------------------------------------------

void UVCCharacterAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UVCCharacterAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVCCharacterAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVCCharacterAttributeSet, Stamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVCCharacterAttributeSet, MaxStamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVCCharacterAttributeSet, Breath, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVCCharacterAttributeSet, MaxBreath, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVCCharacterAttributeSet, MoveSpeedMultiplier, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVCCharacterAttributeSet, MiningSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVCCharacterAttributeSet, InteractionRange, COND_None, REPNOTIFY_Always);
}

// ---------------------------------------------------------------------------
// Pre-clamp
// ---------------------------------------------------------------------------

void UVCCharacterAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);

	if (Attribute == GetHealthAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxHealth());
	}
	else if (Attribute == GetStaminaAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxStamina());
	}
	else if (Attribute == GetBreathAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, GetMaxBreath());
	}
	else if (Attribute == GetMoveSpeedMultiplierAttribute())
	{
		NewValue = FMath::Max(0.f, NewValue);
	}
}

void UVCCharacterAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);

	// PreAttributeChange clamps Health / Stamina only when THEY change. Unequipping armour lowers MaxHealth without
	// touching Health, which left e.g. 130 / 100; clamp the current value whenever its maximum shrinks below it.
	UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
	if (!ASC || !ASC->IsOwnerActorAuthoritative())
	{
		return;
	}
	if (Attribute == GetMaxHealthAttribute() && GetHealth() > NewValue)
	{
		ASC->ApplyModToAttribute(GetHealthAttribute(), EGameplayModOp::Override, NewValue);
	}
	else if (Attribute == GetMaxStaminaAttribute() && GetStamina() > NewValue)
	{
		ASC->ApplyModToAttribute(GetStaminaAttribute(), EGameplayModOp::Override, NewValue);
	}
	else if (Attribute == GetMaxBreathAttribute() && GetBreath() > NewValue)
	{
		ASC->ApplyModToAttribute(GetBreathAttribute(), EGameplayModOp::Override, NewValue);
	}
}

// ---------------------------------------------------------------------------
// Post-execute (damage meta attribute)
// ---------------------------------------------------------------------------

void UVCCharacterAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
	{
		const float DamageDone = GetIncomingDamage();
		SetIncomingDamage(0.f);

		if (DamageDone > 0.f)
		{
			const float NewHealth = FMath::Max(0.f, GetHealth() - DamageDone);
			SetHealth(NewHealth);

			// Zero health is handled by UVCCombatComponent (bound to the Health change delegate),
			// which decides between Dead and Downed; the attribute set only does the arithmetic.
		}
	}
}

// ---------------------------------------------------------------------------
// OnRep
// ---------------------------------------------------------------------------

void UVCCharacterAttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVCCharacterAttributeSet, Health, OldValue);
}

void UVCCharacterAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVCCharacterAttributeSet, MaxHealth, OldValue);
}

void UVCCharacterAttributeSet::OnRep_Stamina(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVCCharacterAttributeSet, Stamina, OldValue);
}

void UVCCharacterAttributeSet::OnRep_Breath(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVCCharacterAttributeSet, Breath, OldValue);
}

void UVCCharacterAttributeSet::OnRep_MaxBreath(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVCCharacterAttributeSet, MaxBreath, OldValue);
}

void UVCCharacterAttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVCCharacterAttributeSet, MaxStamina, OldValue);
}

void UVCCharacterAttributeSet::OnRep_MoveSpeedMultiplier(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVCCharacterAttributeSet, MoveSpeedMultiplier, OldValue);
}

void UVCCharacterAttributeSet::OnRep_MiningSpeed(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVCCharacterAttributeSet, MiningSpeed, OldValue);
}

void UVCCharacterAttributeSet::OnRep_InteractionRange(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVCCharacterAttributeSet, InteractionRange, OldValue);
}
