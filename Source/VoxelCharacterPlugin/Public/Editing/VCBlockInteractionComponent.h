// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VCBlockInteractionComponent.generated.h"

class AVCCharacterBase;
class ULineBatchComponent;
class UVoxelChunkManager;
class UVoxelWorldConfiguration;

/** Server: a timed break finished and dug its block (VoxelCoord, the block's material). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVCBlockBroken, FIntVector, VoxelCoord, uint8, MaterialId);

/**
 * What the player's edit actions aim at this frame, as the block feedback shows it (world modes review WM-C12, WM-C13).
 * The voxels are FindEditTarget's, i.e. exactly what a dig and a place would edit.
 */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCBlockTargets
{
	GENERATED_BODY()

	/** The primary action would dig DigVoxel: a solid voxel in reach, and edit mode on or a digging tool equipped. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Voxel")
	bool bHasDig = false;

	/** Global voxel coordinate a dig would empty. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Voxel")
	FIntVector DigVoxel = FIntVector::ZeroValue;

	/** Material of the dig voxel. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Voxel")
	uint8 DigMaterial = 0;

	/** The secondary action would place PlaceVoxel: an air voxel in reach, edit mode on. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Voxel")
	bool bHasPlace = false;

	/** Global voxel coordinate a place would fill. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Voxel")
	FIntVector PlaceVoxel = FIntVector::ZeroValue;

	/** The server would refuse the place: a pawn is in the block's cell. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Voxel")
	bool bPlaceBlocked = false;
};

/**
 * Block feedback and timed breaking for the player character in a cubic world (world modes review §22: WM-C13, WM-C15;
 * decisions D31-D34).
 *
 * On the locally controlled player it outlines the block the primary action would dig and shows a translucent ghost of
 * the block the secondary action would place (hidden while a break runs), both from AVCCharacterBase::FindEditTarget, so
 * exactly the voxels the edits change, within AVCPlayerController::IsVoxelInEditReach, and drawn by its own line batcher
 * (no assets). While the primary action is held on a block (StartHold / StopHold), the block breaks after its material's
 * hardness divided by the breaker's MiningSpeed (FVoxelMaterialRegistry::GetHardness): the owning client asks the server
 * to begin a break on its target, again on every retarget, and to stop on release, and draws cracks from its own
 * prediction of the progress; the server advances the one break it keeps per player, checks it again when the time is up
 * and digs through the same path an instant dig takes (AVCPlayerController::DigVoxelAt: one block, the gather table once
 * per voxel). Smooth worlds keep the instant dent per click and show nothing (D32).
 */
UCLASS(ClassGroup = (VoxelCharacter), meta = (BlueprintSpawnableComponent))
class VOXELCHARACTERPLUGIN_API UVCBlockInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVCBlockInteractionComponent();

	// =================================================================
	// Rules (pure)
	// =================================================================

	/**
	 * Whether digs in this world are timed breaks: a cubic world, with vc.Dig.BreakTimes on.
	 * @param Config The world's configuration
	 * @return True when a block breaks only by holding the primary action on it
	 */
	static bool UsesTimedBreaks(const UVoxelWorldConfiguration* Config);

	/**
	 * Whether the block feedback (outline, ghost, cracks) applies in this world: a cubic world.
	 * @param Config The world's configuration
	 * @return True in a cubic world
	 */
	static bool ShowsBlockFeedback(const UVoxelWorldConfiguration* Config);

	/**
	 * Seconds to break a block (D31, D34): the material's hardness divided by the breaker's mining speed.
	 * @param MaterialId  The block's voxel material
	 * @param MiningSpeed The breaker's MiningSpeed attribute (1 bare-handed, 1.5 / 2 with the stone / iron pickaxe)
	 * @return Seconds; 0 breaks at once
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Voxel")
	static float GetBreakSeconds(uint8 MaterialId, float MiningSpeed);

	/**
	 * The crack lines on a block at a break's progress (D33): on each exposed face, a few branches spreading from near its
	 * centre, from a pattern fixed by Seed (the same at every progress), more and longer as the break advances.
	 *
	 * @param Block         The block's cell
	 * @param ExposedFaces  Bit per face (+X, -X, +Y, -Y, +Z, -Z) whose neighbour is air
	 * @param Progress      0 to 1
	 * @param Seed          Per block (HashVoxel)
	 * @param SurfaceOffset How far off the face the lines lie (above it, so the face does not hide them)
	 * @param OutSegments   Receives pairs of points, one pair per line
	 */
	static void BuildCrackLines(const FBox& Block, uint8 ExposedFaces, float Progress, uint32 Seed, float SurfaceOffset, TArray<FVector>& OutSegments);

	/** The crack pattern's seed for a block. */
	static uint32 HashVoxel(const FIntVector& VoxelCoord);

	// =================================================================
	// The owning client
	// =================================================================

	/** The primary action is pressed on a dig in a timed world: break what is under the crosshair while it stays held. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Voxel")
	void StartHold();

	/** The primary action is released: stop the break (its progress is lost). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Voxel")
	void StopHold();

	/** @return True while the primary action is held for a break */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Voxel")
	bool IsHolding() const { return bHolding; }

	/**
	 * The break this client is showing.
	 * @param OutVoxel    The block being broken
	 * @param OutProgress This client's prediction, 0 to 1
	 * @return False when no break is running here
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Voxel")
	bool GetClientBreak(FIntVector& OutVoxel, float& OutProgress) const;

	/** What the edits aim at, as last shown (the locally controlled player only). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Voxel")
	const FVCBlockTargets& GetTargets() const { return Targets; }

	/**
	 * Find what the edits would aim at now: FindEditTarget's voxels, with the reach, edit-mode, tool and voxel checks the
	 * actions make. Empty outside a cubic world.
	 * @return The targets
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Voxel")
	FVCBlockTargets ComputeTargets() const;

	/**
	 * One frame of the owning client: targets, the hold (begin, retarget, predicted progress) and the drawing. Ticked by
	 * the component; tests call it with their own frame time.
	 * @param DeltaSeconds Frame time
	 */
	void TickLocal(float DeltaSeconds);

	/** What the line batcher shows (tests and debug): the outline's, the ghost's cell and the crack segments drawn. */
	bool IsOutlineShown(FBox& OutBox) const;
	bool IsGhostShown(FBox& OutBox, bool& bOutBlocked) const;
	int32 GetCrackSegmentCount() const { return CrackSegmentCount; }

	// =================================================================
	// The server
	// =================================================================

	/** Owning client -> server: begin (or move) the break onto a block. Replaces a running break, whose progress is lost. */
	UFUNCTION(Server, Reliable)
	void Server_BeginBlockBreak(FIntVector VoxelCoord);

	/** Owning client -> server: stop the break (the primary action was released). */
	UFUNCTION(Server, Reliable)
	void Server_StopBlockBreak();

	/** Server -> owning client: the break on a block ended: dug (bBroken) or refused / cancelled. */
	UFUNCTION(Client, Reliable)
	void Client_BlockBreakFinished(FIntVector VoxelCoord, bool bBroken);

	/**
	 * Authority: begin a break on a block, checked as a dig: a living player pawn, a world with timed breaks, the block in
	 * reach, solid, and editable (UVoxelEditManager::CanEditAt; a protected block is reported as a blocked edit).
	 * @param VoxelCoord Global voxel coordinate
	 * @return True when the break started
	 */
	bool BeginBreakOnServer(const FIntVector& VoxelCoord);

	/** Authority: stop the running break. */
	void StopBreakOnServer();

	/**
	 * Authority: advance the running break by DeltaSeconds at the breaker's current MiningSpeed; when it is complete, check
	 * it again and dig the block. A break whose block left reach or changed is cancelled.
	 * @param DeltaSeconds Time to advance
	 */
	void AdvanceServerBreak(float DeltaSeconds);

	/**
	 * The break the server is running.
	 * @param OutVoxel    The block
	 * @param OutProgress 0 to 1
	 * @return False when none is
	 */
	bool GetServerBreak(FIntVector& OutVoxel, float& OutProgress) const;

	/** Server: a timed break dug its block. */
	UPROPERTY(BlueprintAssignable, Category = "VoxelCharacter|Voxel")
	FOnVCBlockBroken OnBlockBroken;

	// =================================================================
	// Look
	// =================================================================

	/** Outline colour of the block a dig would break. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|Voxel")
	FLinearColor OutlineColor = FLinearColor(0.02f, 0.02f, 0.02f);

	/** Outline line width (world units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|Voxel", meta = (ClampMin = "0"))
	float OutlineThickness = 2.0f;

	/** The ghost of the block a place would add (alpha = how solid it looks). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|Voxel")
	FColor GhostColor = FColor(255, 255, 255, 56);

	/** The ghost where the server would refuse the block (a pawn in its cell). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|Voxel")
	FColor GhostBlockedColor = FColor(255, 40, 40, 80);

	/** Crack line colour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|Voxel")
	FLinearColor CrackColor = FLinearColor(0.01f, 0.01f, 0.01f);

	/** Crack line width (world units). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|Voxel", meta = (ClampMin = "0"))
	float CrackThickness = 1.8f;

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	AVCCharacterBase* GetCharacter() const;

	/** The client side of a hold this frame: begin on the target, retarget, advance the prediction. */
	void UpdateHold(float DeltaSeconds);

	/** Send a begin / stop to the server (directly on the authority). */
	void SendBegin(const FIntVector& VoxelCoord);
	void SendStop();

	/** Draw the outline, the ghost and the cracks (only when they changed). */
	void Draw(const UVoxelWorldConfiguration* Config, const UVoxelChunkManager* ChunkMgr);
	void ClearDrawing();

	/** Faces of a block whose neighbour is air (bit per face, +X, -X, +Y, -Y, +Z, -Z). */
	static uint8 FindExposedFaces(const UVoxelChunkManager* ChunkMgr, const FIntVector& VoxelCoord);

	// --- Client ---
	FVCBlockTargets Targets;
	bool bHolding = false;
	bool bClientBreaking = false;
	FIntVector ClientVoxel = FIntVector::ZeroValue;
	uint8 ClientMaterial = 0;
	float ClientProgress = 0.f;
	/** A break that ended here (dug or refused) is not begun again on the same block until the target moves off it. */
	bool bSuppressVoxel = false;
	FIntVector SuppressedVoxel = FIntVector::ZeroValue;

	UPROPERTY(Transient)
	TObjectPtr<ULineBatchComponent> Lines;

	/** What was drawn last (a redraw only when it changes). */
	bool bOutlineShown = false;
	bool bGhostShown = false;
	bool bGhostBlockedShown = false;
	FBox OutlineBox = FBox(ForceInit);
	FBox GhostBox = FBox(ForceInit);
	float DrawnProgress = -1.f;
	uint8 DrawnExposedFaces = 0;
	int32 CrackSegmentCount = 0;

	// --- Server ---
	bool bServerBreaking = false;
	FIntVector ServerVoxel = FIntVector::ZeroValue;
	uint8 ServerMaterial = 0;
	float ServerProgress = 0.f;
};
