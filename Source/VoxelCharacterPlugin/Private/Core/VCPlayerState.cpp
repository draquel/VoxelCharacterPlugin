// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCPlayerState.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Core/VCCharacterBase.h"
#include "VoxelCharacterPlugin.h"
#include "GameFramework/Pawn.h"
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

FString AVCPlayerState::GetSaveKey() const
{
	const FUniqueNetIdRepl& NetId = GetUniqueId();
	if (NetId.IsValid())
	{
		FString Key = NetId.ToString();
		// The Null online subsystem mints "<ComputerName>-<32 hex>" with a fresh GUID per session; keep
		// the stable machine part so a standalone / PIE player finds their entry next time.
		if (NetId.GetType() == FName(TEXT("NULL")))
		{
			int32 Dash = INDEX_NONE;
			if (Key.FindLastChar(TEXT('-'), Dash) && Key.Len() - Dash - 1 == 32)
			{
				Key.LeftInline(Dash);
			}
		}
		return Key;
	}
	return TEXT("Local0");
}

void AVCPlayerState::ExportSaveState(FVCPlayerSaveState& OutState) const
{
	OutState = FVCPlayerSaveState();
	OutState.PlayerKey = GetSaveKey();
	OutState.Progression = Progression;
	OutState.bHasRespawnPoint = bHasRespawnPoint;
	OutState.RespawnPoint = RespawnPoint;

	if (const AVCCharacterBase* Avatar = Cast<AVCCharacterBase>(GetPawn()))
	{
		FVCItemSnapshot Snapshot;
		Avatar->BuildItemSnapshot(Snapshot);
		OutState.Inventory = Snapshot.Inventory;
		OutState.Equipment = Snapshot.Equipment;
		OutState.bHasLastTransform = true;
		OutState.LastTransform = Avatar->GetActorTransform();
		if (AbilitySystemComponent)
		{
			OutState.bHasVitals = true;
			OutState.Health = AbilitySystemComponent->GetNumericAttribute(UVCCharacterAttributeSet::GetHealthAttribute());
			OutState.Stamina = AbilitySystemComponent->GetNumericAttribute(UVCCharacterAttributeSet::GetStaminaAttribute());
		}
	}
	else
	{
		// Between avatars (dead, left, or not spawned yet): the pending snapshot and the last known
		// transform / vitals are the truth.
		if (bHasPendingItemSnapshot)
		{
			OutState.Inventory = PendingItemSnapshot.Inventory;
			OutState.Equipment = PendingItemSnapshot.Equipment;
		}
		OutState.bHasLastTransform = bHasLastKnownTransform;
		OutState.LastTransform = LastKnownTransform;
		OutState.bHasVitals = bHasLastKnownVitals;
		OutState.Health = LastKnownHealth;
		OutState.Stamina = LastKnownStamina;
	}
}

void AVCPlayerState::CaptureAvatarState()
{
	const AVCCharacterBase* Avatar = Cast<AVCCharacterBase>(GetPawn());
	if (!HasAuthority() || !Avatar)
	{
		return;
	}
	Avatar->BuildItemSnapshot(PendingItemSnapshot);
	bHasPendingItemSnapshot = !PendingItemSnapshot.IsEmpty();
	LastKnownTransform = Avatar->GetActorTransform();
	bHasLastKnownTransform = true;
	// A dead avatar (death also unpossesses) must not pin the next spawn to 1 hp: no vitals = full.
	bHasLastKnownVitals = false;
	if (AbilitySystemComponent)
	{
		const float Health = AbilitySystemComponent->GetNumericAttribute(UVCCharacterAttributeSet::GetHealthAttribute());
		if (Health > 0.f)
		{
			LastKnownHealth = Health;
			LastKnownStamina = AbilitySystemComponent->GetNumericAttribute(UVCCharacterAttributeSet::GetStaminaAttribute());
			bHasLastKnownVitals = true;
		}
	}
	UE_LOG(LogVoxelCharacter, Log, TEXT("%s: captured avatar state (%d items, %d equipped, at %s, hp %.0f)"), *GetName(),
		PendingItemSnapshot.Inventory.Num(), PendingItemSnapshot.Equipment.Num(), *LastKnownTransform.GetLocation().ToCompactString(), LastKnownHealth);
}

void AVCPlayerState::ImportSaveState(const FVCPlayerSaveState& State)
{
	if (!HasAuthority())
	{
		return;
	}
	PendingItemSnapshot.Reset();
	PendingItemSnapshot.Inventory = State.Inventory;
	PendingItemSnapshot.Equipment = State.Equipment;
	bHasPendingItemSnapshot = !PendingItemSnapshot.IsEmpty();

	Progression = State.Progression;
	OnProgressionChanged.Broadcast(Progression);

	bHasRespawnPoint = State.bHasRespawnPoint;
	RespawnPoint = State.RespawnPoint;

	bHasPendingSpawnTransform = State.bHasLastTransform;
	PendingSpawnTransform = State.LastTransform;

	bHasPendingVitals = State.bHasVitals;
	PendingHealth = State.Health;
	PendingStamina = State.Stamina;

	// A save taken before the avatar spawns must not lose these.
	bHasLastKnownTransform = State.bHasLastTransform;
	LastKnownTransform = State.LastTransform;
	bHasLastKnownVitals = State.bHasVitals;
	LastKnownHealth = State.Health;
	LastKnownStamina = State.Stamina;

	UE_LOG(LogVoxelCharacter, Log, TEXT("%s: imported save state (%d items, %d equipped, cleared %d, spawn %s)"), *GetName(),
		State.Inventory.Num(), State.Equipment.Num(), State.Progression.DungeonsCleared,
		State.bHasLastTransform ? *State.LastTransform.GetLocation().ToCompactString() : TEXT("default"));

	// Already playing (the world save loads after the first spawn; mid-session loads): apply it now.
	if (AVCCharacterBase* Avatar = Cast<AVCCharacterBase>(GetPawn()))
	{
		Avatar->ApplyPendingSaveState();
	}
}

void AVCPlayerState::SetPendingSpawnTransform(const FTransform& Transform)
{
	if (HasAuthority())
	{
		PendingSpawnTransform = Transform;
		bHasPendingSpawnTransform = true;
	}
}

bool AVCPlayerState::ConsumePendingSpawnTransform(FTransform& OutTransform)
{
	if (!bHasPendingSpawnTransform)
	{
		return false;
	}
	OutTransform = PendingSpawnTransform;
	bHasPendingSpawnTransform = false;
	return true;
}

void AVCPlayerState::ApplyPendingVitals()
{
	if (!HasAuthority() || !bHasPendingVitals || !AbilitySystemComponent)
	{
		return;
	}
	bHasPendingVitals = false;
	AbilitySystemComponent->SetNumericAttributeBase(UVCCharacterAttributeSet::GetHealthAttribute(), FMath::Max(PendingHealth, 1.f));
	AbilitySystemComponent->SetNumericAttributeBase(UVCCharacterAttributeSet::GetStaminaAttribute(), FMath::Max(PendingStamina, 0.f));
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
