// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCGameModeBase.h"
#include "Core/VCPlayerState.h"
#include "VoxelCharacterPlugin.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"

AVCGameModeBase::AVCGameModeBase()
{
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
		OldPawn->Destroy();
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

void AVCGameModeBase::Logout(AController* Exiting)
{
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
