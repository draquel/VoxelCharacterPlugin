// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Core/VCTypes.h"
#include "VCGameModeBase.generated.h"

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

protected:
	virtual void Logout(AController* Exiting) override;

	struct FPendingRespawn
	{
		FTimerHandle Timer;
		FTransform DeathTransform;
	};

	TMap<TWeakObjectPtr<AController>, FPendingRespawn> PendingRespawns;
};
