// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCGameModeBase.h"
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

FTransform AVCGameModeBase::ChooseRespawnTransform_Implementation(AController* Controller, const FTransform& DeathTransform, bool& bOutUsePlayerStart) const
{
	bOutUsePlayerStart = (RespawnPolicy == EVCRespawnPolicy::AtPlayerStart);

	// Spawn upright regardless of how the pawn ended up; the terrain-ready spawn settles the height.
	FTransform Result = DeathTransform;
	Result.SetRotation(FRotator(0.f, DeathTransform.Rotator().Yaw, 0.f).Quaternion());
	Result.SetScale3D(FVector::OneVector);
	return Result;
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
