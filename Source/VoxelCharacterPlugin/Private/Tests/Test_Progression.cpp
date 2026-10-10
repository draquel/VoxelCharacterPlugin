// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Core/VCPlayerState.h"
#include "UI/VCVitalsWidget.h"
#include "Tests/VCProgressionTestListener.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"

namespace VCProgressionTestHelpers
{
	struct FWorldHarness
	{
		UWorld* World = nullptr;

		FWorldHarness()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("VCProgressionTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			if (AWorldSettings* WorldSettings = World->GetWorldSettings())
			{
				WorldSettings->NotifyBeginPlay();
			}
		}

		~FWorldHarness()
		{
			if (World)
			{
				// Route EndPlay while the world still exists (see Test_EditMode.cpp for why).
				TArray<AActor*> Actors;
				for (TActorIterator<AActor> It(World); It; ++It)
				{
					Actors.Add(*It);
				}
				for (AActor* Actor : Actors)
				{
					World->DestroyActor(Actor);
				}
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};
}

#define VC_PROGRESSION_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// Counters increment on authority and broadcast once per write.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCProgression_CountersAndDelegate, "VoxelCharacter.Progression.CountersAndDelegate", VC_PROGRESSION_FLAGS)
bool FVCProgression_CountersAndDelegate::RunTest(const FString& Parameters)
{
	VCProgressionTestHelpers::FWorldHarness H;
	AVCPlayerState* PS = H.World->SpawnActor<AVCPlayerState>();
	if (!PS) { AddError(TEXT("Spawn failed")); return false; }

	UVCProgressionTestListener* Listener = NewObject<UVCProgressionTestListener>(H.World);
	PS->OnProgressionChanged.AddDynamic(Listener, &UVCProgressionTestListener::HandleProgressionChanged);

	TestEqual(TEXT("Starts at zero dungeons"), PS->GetProgression().DungeonsCleared, 0);
	TestEqual(TEXT("Starts at zero bosses"), PS->GetProgression().BossesKilled, 0);

	PS->AddDungeonCleared(true);
	TestEqual(TEXT("One dungeon cleared"), PS->GetProgression().DungeonsCleared, 1);
	TestEqual(TEXT("One boss killed"), PS->GetProgression().BossesKilled, 1);
	TestEqual(TEXT("Delegate fired once"), Listener->BroadcastCount, 1);
	TestEqual(TEXT("Delegate carried the counters"), Listener->Last.DungeonsCleared, 1);

	// A clear without the killing blow counts the dungeon but not the boss.
	PS->AddDungeonCleared(false);
	TestEqual(TEXT("Two dungeons cleared"), PS->GetProgression().DungeonsCleared, 2);
	TestEqual(TEXT("Still one boss"), PS->GetProgression().BossesKilled, 1);
	TestEqual(TEXT("Delegate fired twice"), Listener->BroadcastCount, 2);

	PS->OnProgressionChanged.RemoveDynamic(Listener, &UVCProgressionTestListener::HandleProgressionChanged);
	PS->AddDungeonCleared(true);
	TestEqual(TEXT("Unbound listener no longer hears it"), Listener->BroadcastCount, 2);
	return true;
}

// ---------------------------------------------------------------------------
// The vitals HUD objective line and toast show, replace and clear.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCProgression_VitalsObjectiveAndToast, "VoxelCharacter.Progression.VitalsObjectiveAndToast", VC_PROGRESSION_FLAGS)
bool FVCProgression_VitalsObjectiveAndToast::RunTest(const FString& Parameters)
{
	VCProgressionTestHelpers::FWorldHarness H;
	UVCVitalsWidget* Vitals = CreateWidget<UVCVitalsWidget>(H.World, UVCVitalsWidget::StaticClass());
	if (!Vitals) { AddError(TEXT("CreateWidget failed")); return false; }

	TestTrue(TEXT("No objective before any call"), Vitals->GetObjectiveText().IsEmpty());
	TestFalse(TEXT("No toast before any call"), Vitals->IsToastShown());

	Vitals->SetObjective(FText::FromString(TEXT("Objective: defeat the boss")));
	TestEqual(TEXT("Objective text shown"), Vitals->GetObjectiveText().ToString(), FString(TEXT("Objective: defeat the boss")));

	Vitals->SetObjective(FText::FromString(TEXT("Dungeon cleared")));
	TestEqual(TEXT("Objective text replaced"), Vitals->GetObjectiveText().ToString(), FString(TEXT("Dungeon cleared")));

	Vitals->ClearObjective();
	TestTrue(TEXT("Objective cleared"), Vitals->GetObjectiveText().IsEmpty());

	// Duration 0 keeps the toast until cleared, so the test needs no timer ticks.
	Vitals->ShowToast(FText::FromString(TEXT("Dungeon cleared!")), 0.0f);
	TestTrue(TEXT("Toast shown"), Vitals->IsToastShown());

	Vitals->ClearToast();
	TestFalse(TEXT("Toast cleared"), Vitals->IsToastShown());

	Vitals->ShowToast(FText::GetEmpty(), 0.0f);
	TestFalse(TEXT("Empty toast stays hidden"), Vitals->IsToastShown());
	return true;
}

#undef VC_PROGRESSION_FLAGS

#endif // WITH_AUTOMATION_TESTS
