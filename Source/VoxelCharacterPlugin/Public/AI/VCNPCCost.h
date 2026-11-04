// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"

/**
 * The NPCs' cumulative game-thread cost (G73, the NPC budget): the AI controllers' ticks, the path requests they make inside
 * them and the movement ticks of pawns no player controls. A bench reads the counters' change over a frame (the game
 * module's vox.NPCBench); nothing else reads them. Game thread only.
 */
struct VOXELCHARACTERPLUGIN_API FVCNPCCost
{
	/** Seconds in AVCNPCAIController::Tick: perception, decisions and path following, PathSeconds included. */
	double AISeconds = 0.0;

	/** AI controller ticks counted (one per possessing controller per frame). */
	int64 AITicks = 0;

	/** Seconds in the AI controllers' path requests (IVCPathProvider::FindPath: surface searches, dungeon grids). */
	double PathSeconds = 0.0;

	/** Path requests made. */
	int64 PathRequests = 0;

	/** Seconds in UVCMovementComponent::TickComponent of pawns no player controls. */
	double MoveSeconds = 0.0;

	/** Movement ticks counted. */
	int64 MoveTicks = 0;

	/**
	 * The process's counters.
	 *
	 * @return The one instance
	 */
	static FVCNPCCost& Get();
};

/** Adds the seconds between its construction and its destruction to a counter of FVCNPCCost. */
struct FVCNPCCostScope
{
	/** @param InSink The counter the scope's seconds are added to */
	explicit FVCNPCCostScope(double& InSink)
		: Sink(InSink)
		, Start(FPlatformTime::Cycles64())
	{
	}

	~FVCNPCCostScope()
	{
		Sink += FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - Start);
	}

private:
	double& Sink;
	uint64 Start;
};
