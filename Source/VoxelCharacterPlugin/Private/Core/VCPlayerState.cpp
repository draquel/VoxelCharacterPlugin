// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCPlayerState.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Core/VCCharacterBase.h"
#include "VoxelCharacterPlugin.h"
#include "GameFramework/Pawn.h"
#include "Combat/VCCombatAttributeSet.h"
#include "Combat/VCMeleeAttackAbility.h"
#include "Combat/VCRangedAttackAbility.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "VoxelCharacterPlugin.h"
#include "Net/UnrealNetwork.h"
#include "Animation/VCAppearanceComponent.h"
#include "Animation/VCCharacterAppearance.h"
#include "Core/VCGameModeBase.h"
#include "Core/VCSettings.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "VoxelChunkManager.h"
#include "VoxelWorldConfiguration.h"

namespace
{
	/**
	 * vc.Appearance: no argument lists what each player wears and the appearances the settings name; a name (part of an
	 * appearance asset's name among those, or an asset path) dresses every player in it as their own choice, which is
	 * saved; "rule" hands the choice back to the game mode and the settings. Acts on the authority's players.
	 */
	void OnAppearanceCommand(const TArray<FString>& Args)
	{
		const UVCSettings& Settings = UVCSettings::Get();
		TArray<TSoftObjectPtr<UVCCharacterAppearance>> Known;
		Known.AddUnique(Settings.DefaultPlayerAppearance);
		for (const TPair<EMeshingMode, TSoftObjectPtr<UVCCharacterAppearance>>& Pair : Settings.PlayerAppearanceByMeshing)
		{
			Known.AddUnique(Pair.Value);
		}
		Known.RemoveAll([](const TSoftObjectPtr<UVCCharacterAppearance>& Each) { return Each.IsNull(); });

		TSoftObjectPtr<UVCCharacterAppearance> Choice;
		const bool bList = Args.Num() == 0;
		if (!bList && !Args[0].Equals(TEXT("rule"), ESearchCase::IgnoreCase))
		{
			for (const TSoftObjectPtr<UVCCharacterAppearance>& Each : Known)
			{
				if (Each.GetAssetName().Contains(Args[0]))
				{
					Choice = Each;
					break;
				}
			}
			if (Choice.IsNull())
			{
				Choice = TSoftObjectPtr<UVCCharacterAppearance>(FSoftObjectPath(Args[0]));
			}
			if (!Choice.LoadSynchronous())
			{
				UE_LOG(LogVoxelCharacter, Warning, TEXT("vc.Appearance: no appearance %s"), *Args[0]);
				return;
			}
		}
		if (!GEngine)
		{
			return;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_Client)
			{
				continue;
			}
			for (TActorIterator<AVCPlayerState> It(World); It; ++It)
			{
				if (!bList)
				{
					It->SetChosenAppearance(Choice);
				}
				UE_LOG(LogVoxelCharacter, Display, TEXT("vc.Appearance: %s wears %s"), *It->GetPlayerName(),
					It->GetAppearance().IsNull() ? TEXT("its own body") : *It->GetAppearance().GetAssetName());
			}
		}
		if (bList)
		{
			for (const TSoftObjectPtr<UVCCharacterAppearance>& Each : Known)
			{
				UE_LOG(LogVoxelCharacter, Display, TEXT("vc.Appearance: available %s"), *Each.ToString());
			}
		}
	}

	FAutoConsoleCommand CmdAppearance(
		TEXT("vc.Appearance"),
		TEXT("[name|path|rule] The players' body (character animation P6): none lists; a name or path dresses every player in it as their own, saved choice; rule hands it back to the game mode and the settings."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&OnAppearanceCommand));
}

AVCPlayerState::AVCPlayerState()
{
	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	CharacterAttributes = CreateDefaultSubobject<UVCCharacterAttributeSet>(TEXT("CharacterAttributes"));
	CombatAttributes = CreateDefaultSubobject<UVCCombatAttributeSet>(TEXT("CombatAttributes"));

	MeleeAttackAbilityClass = UVCMeleeAttackAbility::StaticClass();
	RangedAttackAbilityClass = UVCRangedAttackAbility::StaticClass();

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
	DOREPLIFETIME(AVCPlayerState, Appearance);
}

void AVCPlayerState::OnRep_Appearance()
{
	OnAppearanceChanged.Broadcast();
}

void AVCPlayerState::SetChosenAppearance(const TSoftObjectPtr<UVCCharacterAppearance>& Choice)
{
	if (!HasAuthority())
	{
		return;
	}
	ChosenAppearance = Choice;
	ResolveAppearance();
}

void AVCPlayerState::ResolveAppearance()
{
	if (!HasAuthority())
	{
		return;
	}
	const UWorld* World = GetWorld();
	const AVCGameModeBase* GameMode = World ? World->GetAuthGameMode<AVCGameModeBase>() : nullptr;
	const UVoxelChunkManager* ChunkManager = World ? UVoxelChunkManager::Find(World) : nullptr;
	const UVoxelWorldConfiguration* Config = ChunkManager ? ChunkManager->GetConfiguration() : nullptr;
	const UVCSettings& Settings = UVCSettings::Get();
	const TSoftObjectPtr<UVCCharacterAppearance> Resolved = UVCAppearanceComponent::PickAppearance(ChosenAppearance,
		GameMode ? GameMode->PlayerAppearance : TSoftObjectPtr<UVCCharacterAppearance>(), Settings.PlayerAppearanceByMeshing,
		Settings.DefaultPlayerAppearance, Config != nullptr, Config ? Config->MeshingMode : EMeshingMode::Cubic);
	if (Resolved != Appearance)
	{
		Appearance = Resolved;
		ForceNetUpdate();
		OnAppearanceChanged.Broadcast();
	}
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
	OutState.ChosenAppearance = ChosenAppearance.ToSoftObjectPath();

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

	ChosenAppearance = TSoftObjectPtr<UVCCharacterAppearance>(State.ChosenAppearance);
	ResolveAppearance();

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

	// Breath is not part of the respawn effect asset: refill it here either way (a drowned player respawns breathing).
	AbilitySystemComponent->SetNumericAttributeBase(UVCCharacterAttributeSet::GetBreathAttribute(),
		AbilitySystemComponent->GetNumericAttribute(UVCCharacterAttributeSet::GetMaxBreathAttribute()));

	UE_LOG(LogVoxelCharacter, Log, TEXT("Respawn attribute reset applied for %s"), *GetPlayerName());
}
