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
/** A player asked to save the world (the pause menu's Save; QA R20). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVCSaveRequested, AController*, RequestedBy);

class UVCCharacterAppearance;

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

	/** A player asked to save (pause menu). The save system binds it; nothing bound = saving unavailable. */
	UPROPERTY(BlueprintAssignable, Category = "VoxelCharacter|Save")
	FOnVCSaveRequested OnSaveRequested;

	/**
	 * Server: a player asked to save the world. Broadcasts OnSaveRequested; tells the player when nothing can save.
	 * @param RequestedBy The asking player's controller
	 * @return True if a save system received the request
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "VoxelCharacter|Save")
	bool RequestSave(AController* RequestedBy);

	/**
	 * Keep the dead player's body as a corpse at respawn instead of destroying it (QA R19): one per player, the
	 * previous one goes when the next is kept.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Respawn")
	bool bKeepCorpse = true;

	/**
	 * The body every player wears in this game mode's maps, over the project settings' (character animation P6); a
	 * player's own choice still comes first. Empty: the settings decide.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Appearance")
	TSoftObjectPtr<UVCCharacterAppearance> PlayerAppearance;

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

	/**
	 * The distance between two players' spawn places (G244). A player who starts where another player's pawn already
	 * stands is given another player start, or a place this far from the taken one on rings around the start; a pawn the
	 * terrain wait holds has its collision off, so the engine's own occupancy test never sees it. 0: every player at the
	 * start itself (two pawns released inside each other push one out, down the slope).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Spawn", meta = (ClampMin = "0"))
	float PlayerSpawnSpacing = 250.f;

	/** Rings of places around a taken start (6, 12, 18 places...); past the last the player spawns at the start itself. */
	static constexpr int32 PlayerSpawnRings = 3;

	/**
	 * Pure: whether another pawn stands at a spawn place: within half the spacing of it across local up, and within
	 * 5,000 uu along it (a pawn in a dungeon under the start, or across a planet, does not take it).
	 * @param Place   The spawn place.
	 * @param Up      Local up there (+Z, or the radius on a planet).
	 * @param Taken   The other players' pawns.
	 * @param Spacing The distance between two places.
	 * @return True when one of Taken stands there.
	 */
	static bool IsSpawnPlaceTaken(const FVector& Place, const FVector& Up, TConstArrayView<FVector> Taken, float Spacing);

	/**
	 * Pure: the first free spawn place at a start (G244): the start itself, then rings of 6 * ring places, ring * Spacing
	 * from it in the plane across local up, the first of each ring along Forward.
	 * @param Start       The player start's location.
	 * @param Up          Local up there.
	 * @param Forward     The start's facing (the first place of each ring); any direction across Up will do.
	 * @param Taken       The other players' pawns.
	 * @param Spacing     The distance between two places (PlayerSpawnSpacing).
	 * @param OutLocation The place found; the start itself when every place is taken.
	 * @return The place's index: 0 the start, 1 to 6 the first ring, ...; INDEX_NONE when every place is taken.
	 */
	static int32 FindFreeSpawnPlace(const FVector& Start, const FVector& Up, const FVector& Forward, TConstArrayView<FVector> Taken,
		float Spacing, FVector& OutLocation);

protected:
	/**
	 * In a play-in-editor world, a player whose login names its PIE instance (UVCLocalPlayer::LoginOption) gets
	 * "PIE<n>" on its save key, before PostLogin restores the player from the save (G271).
	 */
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options,
		const FString& Portal = TEXT("")) override;

	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void StartPlay() override;

	/** Traces the start of each player (LogVoxelCharacter Verbose) and warns when one that should spawn has no pawn (G110). */
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	/** Warns with the controller's spawn state, then lets the engine park it (inactive). */
	virtual void FailedToRestartPlayer(AController* NewPlayer) override;

	/**
	 * One line of what decides a controller's spawn: its class and state, the player state's spectator flags, the
	 * engine's restart checks, the pawn or spectator it holds and the classes the game mode would use (G110).
	 * @param Controller The controller to describe
	 * @return The description, for a log line
	 */
	FString DescribeSpawnState(AController* Controller) const;

	/**
	 * A few seconds into play: warn for every player controller that holds no pawn although it should (not a spectator,
	 * no respawn pending), whatever path created it (G110).
	 */
	void CheckPlayersHavePawns();

	FTimerHandle PlayerPawnCheckTimer;

	/**
	 * Once the voxel world is up, settle what depends on its shape: on a planet the world-item pool parks inside its
	 * core, at the centre, instead of the default spot under a flat world's ground, which is a small planet's south pole
	 * (spherical SP-F5). Polled from StartPlay until the chunk manager is initialised (at most a minute).
	 */
	void ApplyVoxelWorldShape();

	FTimerHandle VoxelWorldShapeTimer;
	int32 VoxelWorldShapePolls = 0;

	/**
	 * A player state carrying a pending spawn transform (a loaded save) spawns there instead of a player start; otherwise
	 * the first free place at the start (FindFreeSpawnPlace, G244): the start itself while no other player stands there.
	 */
	virtual APawn* SpawnDefaultPawnFor_Implementation(AController* NewPlayer, AActor* StartSpot) override;

	/**
	 * The engine's choice, unless another player's pawn stands at it and a player start no one stands at exists: then that
	 * one (G244). A "Play from Here" start is kept (every player shares it, apart by FindFreeSpawnPlace).
	 */
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

	/**
	 * Where the other players' pawns stand (alive, player-controlled, not Player's own), for the spawn places.
	 * @param Player   The player about to spawn.
	 * @param OutTaken The locations.
	 */
	void GatherOtherPlayerPawns(const AController* Player, TArray<FVector>& OutTaken) const;

	/**
	 * Local up at a spawn place: the radius on a planet (a shell world), +Z otherwise.
	 * @param Location The place.
	 * @return The unit up.
	 */
	FVector GetSpawnUp(const FVector& Location) const;

	/** Respawns adjust out of small overlaps (a bench, a slope) instead of failing to spawn. */
	virtual APawn* SpawnDefaultPawnAtTransform_Implementation(AController* NewPlayer, const FTransform& SpawnTransform) override;

	struct FPendingRespawn
	{
		FTimerHandle Timer;
		FTransform DeathTransform;
	};

	TMap<TWeakObjectPtr<AController>, FPendingRespawn> PendingRespawns;

	/** Each player's kept corpse (bKeepCorpse). */
	TMap<TWeakObjectPtr<AController>, TWeakObjectPtr<APawn>> PlayerCorpses;
};
