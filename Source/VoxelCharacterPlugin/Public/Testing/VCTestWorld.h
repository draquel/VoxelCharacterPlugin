// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"

/**
 * Test-only helpers for the character plugin and the plugins that depend on it (VoxelWorldPOI): a game world that has begun
 * play, for tests that spawn characters, controllers, claims and interactables and need their BeginPlay to run (code review
 * CR2 row 17, VCP-16 / POI-23: the five copies in this plugin and the four in the POI plugin). Compiled only with
 * WITH_DEV_AUTOMATION_TESTS.
 */
namespace VCTesting
{
	/**
	 * A transient game world with its actors initialized for play and play begun without a game mode: NotifyBeginPlay
	 * dispatches BeginPlay on the existing actors and sets bBegunPlay, so an actor spawned afterwards begins play (and
	 * registers with its subsystems) on FinishSpawning. The destructor destroys the world; with bDestroyActorsFirst it
	 * destroys every actor before the world, routing EndPlay while the world still exists (a character left for the GC
	 * runs its components' EndPlay with GetWorld() null later, where UInteractionComponent clears a timer, and crashes the
	 * editor on the next map load).
	 */
	struct FPlayWorld
	{
		UWorld* World = nullptr;

		FPlayWorld(const TCHAR* WorldName, bool bInDestroyActorsFirst)
			: bDestroyActorsFirst(bInDestroyActorsFirst)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, WorldName);
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			if (AWorldSettings* WorldSettings = World->GetWorldSettings())
			{
				WorldSettings->NotifyBeginPlay();
			}
		}

		FPlayWorld(const FPlayWorld&) = delete;
		FPlayWorld& operator=(const FPlayWorld&) = delete;

		~FPlayWorld()
		{
			if (World)
			{
				if (bDestroyActorsFirst)
				{
					TArray<AActor*> Actors;
					for (TActorIterator<AActor> It(World); It; ++It)
					{
						Actors.Add(*It);
					}
					for (AActor* Actor : Actors)
					{
						World->DestroyActor(Actor);
					}
				}
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}

	private:
		bool bDestroyActorsFirst = false;
	};
}

#endif // WITH_DEV_AUTOMATION_TESTS
