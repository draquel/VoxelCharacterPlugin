// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCGameModeBase.h"
#include "Core/VCPlayerState.h"
#include "Core/VCCharacterBase.h"
#include "Core/VCPlayerController.h"
#include "VoxelCharacterPlugin.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/SpectatorPawn.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Movement/VCTerrainQueryHelper.h"
#include "VoxelChunkManager.h"
#if WITH_INTERACTION_PLUGIN
#include "Subsystems/WorldItemPoolSubsystem.h"
#endif

AVCGameModeBase::AVCGameModeBase()
{
}

void AVCGameModeBase::StartPlay()
{
	Super::StartPlay();
	VoxelWorldShapePolls = 0;
	GetWorldTimerManager().SetTimer(VoxelWorldShapeTimer, this, &AVCGameModeBase::ApplyVoxelWorldShape, 0.5f, /*bLoop=*/true, 0.0f);
	GetWorldTimerManager().SetTimer(PlayerPawnCheckTimer, this, &AVCGameModeBase::CheckPlayersHavePawns, 5.f, /*bLoop=*/false);
}

FString AVCGameModeBase::DescribeSpawnState(AController* Controller) const
{
	if (!Controller)
	{
		return TEXT("no controller");
	}
	APlayerController* PC = Cast<APlayerController>(Controller);
	const APlayerState* PS = Controller->PlayerState;
	const UWorld* World = GetWorld();
	return FString::Printf(
		TEXT("%s (%s, local %d, net mode %d, state %s): player state %s (only spectator %d, spectator %d); can restart %d (world loaded %d), must spectate %d, start as spectators %d, match started %d, world begun %d; pawn %s, spectator %s, start spot %s; pawn class %s, controller class %s"),
		*Controller->GetName(), *GetNameSafe(Controller->GetClass()), Controller->IsLocalController() ? 1 : 0,
		World ? static_cast<int32>(World->GetNetMode()) : -1, *Controller->GetStateName().ToString(),
		*GetNameSafe(PS), PS && PS->IsOnlyASpectator() ? 1 : 0, PS && PS->IsSpectator() ? 1 : 0,
		PC && PC->CanRestartPlayer() ? 1 : 0, PC && PC->HasClientLoadedCurrentWorld() ? 1 : 0,
		PC && MustSpectate(PC) ? 1 : 0, bStartPlayersAsSpectators ? 1 : 0, HasMatchStarted() ? 1 : 0,
		World && World->HasBegunPlay() ? 1 : 0, *GetNameSafe(Controller->GetPawn()),
		PC ? *GetNameSafe(PC->GetSpectatorPawn()) : TEXT("-"), *GetNameSafe(Controller->StartSpot.Get()),
		*GetNameSafe(DefaultPawnClass.Get()), *GetNameSafe(PlayerControllerClass.Get()));
}

void AVCGameModeBase::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("Starting player: %s"), *DescribeSpawnState(NewPlayer));
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);

	const bool bShouldSpawn = NewPlayer && !bStartPlayersAsSpectators && !MustSpectate(NewPlayer);
	if (bShouldSpawn && !NewPlayer->GetPawn())
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("A starting player has no pawn: %s"), *DescribeSpawnState(NewPlayer));
	}
	else
	{
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("Started player: %s"), *DescribeSpawnState(NewPlayer));
	}
}

void AVCGameModeBase::RestartPlayer(AController* NewPlayer)
{
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("RestartPlayer: %s"), *DescribeSpawnState(NewPlayer));
	Super::RestartPlayer(NewPlayer);
}

void AVCGameModeBase::FailedToRestartPlayer(AController* NewPlayer)
{
	UE_LOG(LogVoxelCharacter, Warning, TEXT("Failed to restart a player: %s"), *DescribeSpawnState(NewPlayer));
	Super::FailedToRestartPlayer(NewPlayer);
}

void AVCGameModeBase::CheckPlayersHavePawns()
{
	int32 Players = 0;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		++Players;
		if (!PC->GetPawn() && !bStartPlayersAsSpectators && !MustSpectate(PC) && !PendingRespawns.Contains(PC))
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("A player has no pawn %.1f s into play: %s"),
				GetWorld()->GetTimeSeconds(), *DescribeSpawnState(PC));
		}
	}
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("Pawn check: %d player controller(s)."), Players);
}

void AVCGameModeBase::ApplyVoxelWorldShape()
{
	const UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld());
	if (!ChunkMgr)
	{
		// No voxel world (yet): keep looking for a minute, then give up quietly (a map without one).
		if (++VoxelWorldShapePolls >= 120)
		{
			GetWorldTimerManager().ClearTimer(VoxelWorldShapeTimer);
		}
		return;
	}
	GetWorldTimerManager().ClearTimer(VoxelWorldShapeTimer);

	const FVoxelWorldBounds Bounds = ChunkMgr->GetWorldBounds();
	if (Bounds.Shape != EVoxelWorldBoundsShape::Shell)
	{
		return; // heightmap worlds keep every default
	}
#if WITH_INTERACTION_PLUGIN
	if (UWorldItemPoolSubsystem* Pool = GetWorld() ? GetWorld()->GetSubsystem<UWorldItemPoolSubsystem>() : nullptr)
	{
		// The centre is inside the core, which never streams: hidden, collisionless items wait there, out of every
		// player's way and relevance.
		Pool->SetParkingLocation(Bounds.Center);
		// ... and stand and snap along the direction from it (spherical Phase 4, SP-F7).
		Pool->SetGravityCenter(Bounds.Center);
		UE_LOG(LogVoxelCharacter, Log, TEXT("Planet: world items park at the planet centre %s and snap along its gravity."), *Bounds.Center.ToCompactString());
	}
#endif
}

void AVCGameModeBase::HandlePlayerDied(AController* Controller, const FTransform& DeathTransform)
{
	if (!Controller || !HasAuthority())
	{
		return;
	}

	FPendingRespawn& Pending = PendingRespawns.FindOrAdd(Controller);
	Pending.DeathTransform = DeathTransform;
	GetWorldTimerManager().ClearTimer(Pending.Timer);

	if (RespawnDelay <= 0.f)
	{
		RespawnPlayer(Controller);
		return;
	}

	TWeakObjectPtr<AController> WeakController = Controller;
	GetWorldTimerManager().SetTimer(Pending.Timer, FTimerDelegate::CreateWeakLambda(this, [this, WeakController]()
	{
		RespawnPlayer(WeakController.Get());
	}), RespawnDelay, false);

	UE_LOG(LogVoxelCharacter, Log, TEXT("%s died; respawn in %.1fs."), *GetNameSafe(Controller), RespawnDelay);
}

bool AVCGameModeBase::RequestSave(AController* RequestedBy)
{
	if (!HasAuthority())
	{
		return false;
	}
	if (!OnSaveRequested.IsBound())
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("%s asked to save, but no save system is listening."), *GetNameSafe(RequestedBy));
		if (AVCPlayerController* PC = Cast<AVCPlayerController>(RequestedBy))
		{
			PC->Client_ShowToast(NSLOCTEXT("VCGameMode", "NoSave", "Saving is not available here"));
		}
		return false;
	}
	OnSaveRequested.Broadcast(RequestedBy);
	return true;
}

void AVCGameModeBase::RespawnPlayer(AController* Controller)
{
	if (!Controller || !HasAuthority())
	{
		return;
	}

	FTransform DeathTransform = FTransform::Identity;
	if (FPendingRespawn* Pending = PendingRespawns.Find(Controller))
	{
		GetWorldTimerManager().ClearTimer(Pending->Timer);
		DeathTransform = Pending->DeathTransform;
		PendingRespawns.Remove(Controller);
	}

	if (APawn* OldPawn = Controller->GetPawn())
	{
		Controller->UnPossess();
		AVCCharacterBase* Body = Cast<AVCCharacterBase>(OldPawn);
		if (bKeepCorpse && Body)
		{
			// One corpse per player: the previous one goes as this one stays (QA R19).
			if (APawn* Previous = PlayerCorpses.FindRef(Controller).Get())
			{
				Previous->Destroy();
			}
			Body->BecomeCorpse();
			PlayerCorpses.Add(Controller, Body);
		}
		else
		{
			OldPawn->Destroy();
		}
	}

	bool bUsePlayerStart = false;
	const FTransform SpawnTransform = ChooseRespawnTransform(Controller, DeathTransform, bUsePlayerStart);

	if (bUsePlayerStart)
	{
		RestartPlayer(Controller);
	}
	else
	{
		RestartPlayerAtTransform(Controller, SpawnTransform);
	}
}

FTransform AVCGameModeBase::ResolveRespawnTransform(EVCRespawnPolicy Policy, bool bHasRestPoint, const FTransform& RestPoint,
	const FTransform& DeathTransform, bool& bOutUsePlayerStart)
{
	bOutUsePlayerStart = (Policy == EVCRespawnPolicy::AtPlayerStart);
	const bool bUseRest = Policy == EVCRespawnPolicy::AtLastRest && bHasRestPoint;
	const FTransform& Source = bUseRest ? RestPoint : DeathTransform;

	// Spawn upright regardless of how the pawn ended up; the terrain-ready spawn settles the height.
	FTransform Result = Source;
	Result.SetRotation(FRotator(0.f, Source.Rotator().Yaw, 0.f).Quaternion());
	Result.SetScale3D(FVector::OneVector);
	return Result;
}

FTransform AVCGameModeBase::ChooseRespawnTransform_Implementation(AController* Controller, const FTransform& DeathTransform, bool& bOutUsePlayerStart) const
{
	FTransform RestPoint;
	bool bHasRestPoint = false;
	if (const AVCPlayerState* PS = Controller ? Controller->GetPlayerState<AVCPlayerState>() : nullptr)
	{
		bHasRestPoint = PS->GetRespawnPoint(RestPoint);
	}
	return ResolveRespawnTransform(RespawnPolicy, bHasRestPoint, RestPoint, DeathTransform, bOutUsePlayerStart);
}

APawn* AVCGameModeBase::SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform)
{
	// The engine default inherits the pawn class's collision handling, which refuses to spawn into a
	// bench or a slope; a respawn must always produce a pawn.
	FActorSpawnParameters SpawnInfo;
	SpawnInfo.Instigator = GetInstigator();
	SpawnInfo.ObjectFlags |= RF_Transient;
	SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	UClass* PawnClass = GetDefaultPawnClassForController(NewPlayer);
	APawn* ResultPawn = PawnClass ? GetWorld()->SpawnActor<APawn>(PawnClass, SpawnTransform, SpawnInfo) : nullptr;
	if (!ResultPawn)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("SpawnDefaultPawnAtTransform: could not spawn %s at %s"),
			*GetNameSafe(PawnClass), *SpawnTransform.GetLocation().ToCompactString());
	}
	return ResultPawn;
}

void AVCGameModeBase::PostLogin(APlayerController* NewPlayer)
{
	// The player state exists; the pawn does not yet: the save system restores here.
	if (AVCPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<AVCPlayerState>() : nullptr)
	{
		OnPlayerLoggingIn.Broadcast(PS);
	}
	Super::PostLogin(NewPlayer);
}

APawn* AVCGameModeBase::SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot)
{
	FTransform Saved;
	if (AVCPlayerState* PS = NewPlayer ? NewPlayer->GetPlayerState<AVCPlayerState>() : nullptr)
	{
		if (PS->ConsumePendingSpawnTransform(Saved))
		{
			UE_LOG(LogVoxelCharacter, Log, TEXT("%s spawns at its saved transform %s"), *GetNameSafe(NewPlayer), *Saved.GetLocation().ToCompactString());
			return SpawnDefaultPawnAtTransform(NewPlayer, Saved);
		}
	}
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s spawns %s at the start %s %s"), *GetNameSafe(NewPlayer),
		*GetNameSafe(GetDefaultPawnClassForController(NewPlayer)), *GetNameSafe(StartSpot),
		StartSpot ? *StartSpot->GetActorLocation().ToCompactString() : TEXT("-"));
	return Super::SpawnDefaultPawnFor_Implementation(NewPlayer, StartSpot);
}

void AVCGameModeBase::Logout(AController* Exiting)
{
	if (AVCPlayerState* PS = Exiting ? Exiting->GetPlayerState<AVCPlayerState>() : nullptr)
	{
		OnPlayerLoggingOut.Broadcast(PS);
	}
	if (Exiting)
	{
		if (FPendingRespawn* Pending = PendingRespawns.Find(Exiting))
		{
			GetWorldTimerManager().ClearTimer(Pending->Timer);
			PendingRespawns.Remove(Exiting);
		}
	}
	Super::Logout(Exiting);
}
