// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCCharacterAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "VoxelCharacterPlugin.h"

UVCCharacterAttributeSet::UVCCharacterAttributeSet()
{
	InitHealth(100.f);
	InitMaxHealth(100.f);
	InitStamina(100.f);
	InitMaxStamina(100.f);
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
	else if (Attribute == GetMoveSpeedMultiplierAttribute())
	{
		NewValue = FMath::Max(0.f, NewValue);
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

			if (NewHealth <= 0.f)
			{
				// Death handling — broadcast via GAS tag or delegate.
				// Full death flow wired in Gate 3 character/playerstate.
				UE_LOG(LogVoxelCharacter, Log, TEXT("Character health reached zero"));
			}
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
