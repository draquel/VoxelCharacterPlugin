// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VCMapExplorationComponent.generated.h"

/**
 * Joins an explored mask sent in parts (UVCMapExplorationComponent's RPCs). Parts arrive in order on one reliable
 * channel; a part 0 starts a new blob, and anything out of order or past the cap drops the blob being joined.
 */
struct VOXELCHARACTERPLUGIN_API FVCExploredBlobAssembler
{
	/**
	 * Add a part.
	 * @param Index    The part's index, from 0
	 * @param Count    The number of parts of the blob
	 * @param Part     The part's characters
	 * @param MaxChars The most characters a whole blob may hold
	 * @param OutBlob  Receives the whole blob when this was its last part
	 * @return True when the blob is complete (OutBlob set)
	 */
	bool AddPart(int32 Index, int32 Count, const FString& Part, int32 MaxChars, FString& OutBlob);

	/** Forget the blob being joined. */
	void Reset();

private:
	FString Joined;
	int32 ExpectedCount = 0;
	int32 NextIndex = 0;
};

/**
 * The player's map exploration between its client and the server (G271). A client's map lives in the client's world, so
 * the server's save cannot read it: the owning client sends its explored mask when it has grown (at most every
 * vc.Map.ExploredPushSeconds), the server checks it and keeps it on the player state for the next save, and a mask a
 * save restores goes back to the owning client the same way. On the controller of every player; it does nothing for a
 * local player (standalone, a listen server's host), whose map is the server world's own.
 */
UCLASS(ClassGroup = (VoxelCharacter), meta = (BlueprintSpawnableComponent))
class VOXELCHARACTERPLUGIN_API UVCMapExplorationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVCMapExplorationComponent();

	/** The characters of one RPC's part. */
	static constexpr int32 PartChars = 16 * 1024;

	/** The most characters a reported blob may hold (a whole explored planet's mask is kilobytes). */
	static constexpr int32 MaxBlobChars = 4 * 1024 * 1024;

	/**
	 * Pure: cut a blob into parts of PartChars.
	 * @param Blob The blob
	 * @return The parts in order; none for an empty blob
	 */
	static TArray<FString> SplitBlob(const FString& Blob);

	/**
	 * Pure: whether a reported blob is an explored mask the server keeps: an encoded mask (not the coordinate pairs saves
	 * held before the mask) that decodes, within MaxBlobChars.
	 * @param Blob         The blob
	 * @param OutTileCount Receives the tiles the mask holds (counted here, not taken from the client)
	 * @return True when the blob is kept
	 */
	static bool ValidateBlob(const FString& Blob, int32& OutTileCount);

	/**
	 * Server: send an explored mask to the owning client, whose map merges it (a save's mask for a remote player).
	 * @param Blob      The explored mask (UVoxelMapSubsystem::ExportExploredTiles)
	 * @param TileCount The tiles it holds
	 */
	void SendToOwningClient(const FString& Blob, int32 TileCount);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	/** Owning client -> server: one part of the client's explored mask. */
	UFUNCTION(Server, Reliable)
	void Server_ReportExploredPart(int32 Index, int32 Count, const FString& Part);

	/** Server -> owning client: one part of a saved explored mask, and the tiles the whole holds. */
	UFUNCTION(Client, Reliable)
	void Client_RestoreExploredPart(int32 Index, int32 Count, const FString& Part, int32 TileCount);

private:
	/** @return True on the owning client of a remote player (a client's world), where the mask is pushed from. */
	bool IsRemoteOwningClient() const;

	/** The exploration revision last sent (the owning client); MAX_uint64: none sent. */
	uint64 LastPushedRevision = MAX_uint64;

	/** Seconds since the last push (the owning client). */
	float SecondsSincePush = 0.f;

	/** The parts being joined: on the server the reports, on the client the restore. */
	FVCExploredBlobAssembler Assembler;
};
