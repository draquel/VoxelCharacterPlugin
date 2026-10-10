// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCPlayerState.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Combat/VCCombatAttributeSet.h"
#include "Combat/VCMeleeAttackAbility.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "VoxelCharacterPlugin.h"
#include "Net/UnrealNetwork.h"

AVCPlayerState::AVCPlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	CharacterAttributes = CreateDefaultSubobject<UVCCharacterAttributeSet>(TEXT("CharacterAttributes"));
	CombatAttributes = CreateDefaultSubobject<UVCCombatAttributeSet>(TEXT("CombatAttributes"));

	MeleeAttackAbilityClass = UVCMeleeAttackAbility::StaticClass();

	// Net update frequency for ASC replication
	SetNetUpdateFrequency(100.f);
}

UAbilitySystemComponent* AVCPlayerState::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

void AVCPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVCPlayerState, Progression);
}

void AVCPlayerState::AddDungeonCleared(bool bBossKilled)
{
	if (!HasAuthority())
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("AddDungeonCleared called without authority on %s — ignored."), *GetName());
		return;
	}
	++Progression.DungeonsCleared;
	if (bBossKilled)
	{
		++Progression.BossesKilled;
	}
	UE_LOG(LogVoxelCharacter, Log, TEXT("%s progression: dungeons cleared %d, bosses killed %d."),
		*GetPlayerName(), Progression.DungeonsCleared, Progression.BossesKilled);
	// OnRep only fires on clients — broadcast locally for the server / standalone.
	OnProgressionChanged.Broadcast(Progression);
}

void AVCPlayerState::OnRep_Progression()
{
	OnProgressionChanged.Broadcast(Progression);
}

void AVCPlayerState::SetRespawnPoint(const FTransform& Point)
{
	if (!HasAuthority())
	{
		return;
	}
	RespawnPoint = Point;
	bHasRespawnPoint = true;
}

void AVCPlayerState::ClearRespawnPoint()
{
	if (HasAuthority())
	{
		bHasRespawnPoint = false;
	}
}

bool AVCPlayerState::GetRespawnPoint(FTransform& OutPoint) const
{
	OutPoint = RespawnPoint;
	return bHasRespawnPoint;
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
	else
	{
		// No effect configured: restore vitals directly so a fresh avatar never starts at 0 health.
		const float MaxHealth = AbilitySystemComponent->GetNumericAttribute(UVCCharacterAttributeSet::GetMaxHealthAttribute());
		const float MaxStamina = AbilitySystemComponent->GetNumericAttribute(UVCCharacterAttributeSet::GetMaxStaminaAttribute());
		AbilitySystemComponent->SetNumericAttributeBase(UVCCharacterAttributeSet::GetHealthAttribute(), MaxHealth);
		AbilitySystemComponent->SetNumericAttributeBase(UVCCharacterAttributeSet::GetStaminaAttribute(), MaxStamina);
	}

	UE_LOG(LogVoxelCharacter, Log, TEXT("Respawn attribute reset applied for %s"), *GetPlayerName());
}
