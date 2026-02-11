// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCPlayerState.h"
#include "Core/VCCharacterAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "VoxelCharacterPlugin.h"

AVCPlayerState::AVCPlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	CharacterAttributes = CreateDefaultSubobject<UVCCharacterAttributeSet>(TEXT("CharacterAttributes"));

	// Net update frequency for ASC replication
	SetNetUpdateFrequency(100.f);
}

UAbilitySystemComponent* AVCPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AVCPlayerState::HandleRespawnAttributeReset()
{
	if (!AbilitySystemComponent)
	{
		return;
	}

	// Remove temporary effects tagged for death cleanse
	if (DeathCleanseTags.Num() > 0)
	{
		FGameplayEffectQuery Query;
		Query.EffectTagQuery.MakeQuery_MatchAnyTags(DeathCleanseTags);
		AbilitySystemComponent->RemoveActiveEffects(Query);
	}

	// Apply respawn reset effect (sets Health = MaxHealth, Stamina = MaxStamina, etc.)
	if (RespawnResetEffect)
	{
		FGameplayEffectContextHandle Context = AbilitySystemComponent->MakeEffectContext();
		Context.AddSourceObject(this);
		const FGameplayEffectSpecHandle Spec = AbilitySystemComponent->MakeOutgoingSpec(RespawnResetEffect, 1.f, Context);
		if (Spec.IsValid())
		{
			AbilitySystemComponent->ApplyGameplayEffectSpecToSelf(*Spec.Data.Get());
		}
	}

	UE_LOG(LogVoxelCharacter, Log, TEXT("Respawn attribute reset applied for %s"), *GetPlayerName());
}
