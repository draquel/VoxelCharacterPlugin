// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Core/VCTypes.h"
#include "VCGameModeBase.generated.h"

class AVCPlayerState;

/** A player is logging in and their player state exists, before their first pawn spawns (save systems fill it here). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVCPlayerLoggingIn, AVCPlayerState*, PlayerState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVCPlayerLoggingOut, AVCPlayerState*, PlayerState);

/**
 * Game mode base that owns player respawn for the voxel character.
 *
 * AVCCharacterBase reports its death here; after RespawnDelay the old pawn is
 * destroyed and the controller restarted at a transform chosen by
 * ChooseRespawnTransform (death location by default, a player start otherwise).
 * The new pawn's PossessedBy revives the persistent ASC on the player state.
 *
 * Intended parent for the project's game mode Blueprint.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API AVCGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	AVCGameModeBase();

	/** Seconds between death and respawn. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Respawn", meta = (ClampMin = "0"))
	float RespawnDelay = 5.f;

	/** Where the player comes back. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Respawn")
	EVCRespawnPolicy RespawnPolicy = EVCRespawnPolicy::AtDeathLocation;

	/**
	 * Fires in PostLogin before the first pawn spawns: the player state is ready, so a save system can
	 * restore items (PendingItemSnapshot), progression, the rest point and a spawn transform (feature 9).
	 */
	UPROPERTY(BlueprintAssignable, Category = "VoxelCharacter|Save")
	FOnVCPlayerLoggingIn OnPlayerLoggingIn;

	/**
	 * Fires in Logout while the player state is still valid (the pawn is already gone, but the state
	 * captured it in PawnLeavingGame): a save system records the departing player here (feature 9).
	 */
	UPROPERTY(BlueprintAssignable, Category = "VoxelCharacter|Save")
	FOnVCPlayerLoggingOut OnPlayerLoggingOut;

	/**
	 * Called by the dying pawn on authority. Schedules the respawn.
	 * @param Controller     The player's controller.
	 * @param DeathTransform Where the pawn died.
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Respawn")
	void HandlePlayerDied(AController* Controller, const FTransform& DeathTransform);

	/** Respawn now, ignoring any pending delay. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Respawn")
	void RespawnPlayer(AController* Controller);

	/**
	 * Pick the respawn transform. Default honours RespawnPolicy; override for POI or
	 * checkpoint respawns later.
	 * @param Controller     Player being respawned.
	 * @param DeathTransform Where they died.
	 * @param bOutUsePlayerStart True to let the engine pick a player start instead of the returned transform.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "VoxelCharacter|Respawn")
	FTransform ChooseRespawnTransform(AController* Controller, const FTransform& DeathTransform, bool& bOutUsePlayerStart) const;

	/**
	 * Pure: the transform a policy resolves to (upright, unit scale).
	 * @param Policy          The game mode's policy.
	 * @param bHasRestPoint   The player has rested somewhere.
	 * @param RestPoint       Where (valid when bHasRestPoint).
	 * @param DeathTransform  Where they died.
	 * @param bOutUsePlayerStart True to let the engine pick a player start instead.
	 */
	static FTransform ResolveRespawnTransform(EVCRespawnPolicy Policy, bool bHasRestPoint, const FTransform& RestPoint,
		const FTransform& DeathTransform, bool& bOutUsePlayerStart);

protected:
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	/** A player state carrying a pending spawn transform (a loaded save) spawns there instead of a player start. */
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

	/** Respawns adjust out of small overlaps (a bench, a slope) instead of failing to spawn. */
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;

	struct FPendingRespawn
	{
		FTimerHandle Timer;
		FTransform DeathTransform;
	};

	TMap<TWeakObjectPtr<AController>, FPendingRespawn> PendingRespawns;
};
