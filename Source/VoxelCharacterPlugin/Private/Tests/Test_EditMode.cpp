// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Core/VCCharacterBase.h"
#include "UI/VCVitalsWidget.h"
#include "Tests/VCEditModeTestListener.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

namespace VCEditModeTestHelpers
{
	struct FWorldHarness
	{
		UWorld* World = nullptr;

		FWorldHarness()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("VCEditModeTestWorld"));
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
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	/** Spawn a player character without the terrain wait (no voxel world in the test harness). */
	AVCCharacterBase* SpawnCharacter(UWorld* World)
	{
		const FTransform Transform(FVector::ZeroVector);
		AVCCharacterBase* Character = World->SpawnActorDeferred<AVCCharacterBase>(AVCCharacterBase::StaticClass(), Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Character)
		{
			return nullptr;
		}
		Character->bWaitForTerrain = false;
		Character->FinishSpawning(Transform);
		return Character;
	}
}

#define VC_EDITMODE_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// The toggle flips the flag and fires OnEditModeChanged exactly once per real change.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCEditMode_ToggleFlipsStateAndFiresDelegate, "VoxelCharacter.EditMode.ToggleFlipsStateAndFiresDelegate", VC_EDITMODE_FLAGS)
bool FVCEditMode_ToggleFlipsStateAndFiresDelegate::RunTest(const FString& Parameters)
{
	VCEditModeTestHelpers::FWorldHarness H;
	AVCCharacterBase* Character = VCEditModeTestHelpers::SpawnCharacter(H.World);
	if (!Character) { AddError(TEXT("Spawn failed")); return false; }

	UVCEditModeTestListener* Listener = NewObject<UVCEditModeTestListener>(H.World);
	Character->OnEditModeChanged.AddDynamic(Listener, &UVCEditModeTestListener::HandleEditModeChanged);

	TestFalse(TEXT("Edit mode is off by default"), Character->IsEditModeEnabled());

	Character->ToggleEditMode();
	TestTrue(TEXT("Toggle turns it on"), Character->IsEditModeEnabled());
	TestEqual(TEXT("Delegate fired once"), Listener->BroadcastCount, 1);
	TestTrue(TEXT("Delegate carried true"), Listener->bLastValue);

	Character->SetEditModeEnabled(true);
	TestEqual(TEXT("Setting the same value does not re-broadcast"), Listener->BroadcastCount, 1);

	Character->SetEditModeEnabled(false);
	TestFalse(TEXT("Set off"), Character->IsEditModeEnabled());
	TestEqual(TEXT("Delegate fired again"), Listener->BroadcastCount, 2);
	TestFalse(TEXT("Delegate carried false"), Listener->bLastValue);

	Character->ToggleEditMode();
	Character->ToggleEditMode();
	TestFalse(TEXT("Two toggles return to off"), Character->IsEditModeEnabled());
	TestEqual(TEXT("Both toggles broadcast"), Listener->BroadcastCount, 4);

	Character->OnEditModeChanged.RemoveDynamic(Listener, &UVCEditModeTestListener::HandleEditModeChanged);
	Character->ToggleEditMode();
	TestEqual(TEXT("Unbound listener no longer hears it"), Listener->BroadcastCount, 4);
	return true;
}

// ---------------------------------------------------------------------------
// The vitals HUD cue mirrors the bound character and clears on unbind.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCEditMode_VitalsCueFollowsCharacter, "VoxelCharacter.EditMode.VitalsCueFollowsCharacter", VC_EDITMODE_FLAGS)
bool FVCEditMode_VitalsCueFollowsCharacter::RunTest(const FString& Parameters)
{
	VCEditModeTestHelpers::FWorldHarness H;
	AVCCharacterBase* Character = VCEditModeTestHelpers::SpawnCharacter(H.World);
	if (!Character) { AddError(TEXT("Spawn failed")); return false; }

	UVCVitalsWidget* Vitals = CreateWidget<UVCVitalsWidget>(H.World, UVCVitalsWidget::StaticClass());
	if (!Vitals) { AddError(TEXT("CreateWidget failed")); return false; }

	TestFalse(TEXT("Cue hidden before binding"), Vitals->IsEditModeShown());

	// Binding to a character that is already in edit mode picks the state up immediately.
	Character->SetEditModeEnabled(true);
	Vitals->InitWithCharacter(Character);
	TestTrue(TEXT("Cue shown after binding to an editing character"), Vitals->IsEditModeShown());

	Character->SetEditModeEnabled(false);
	TestFalse(TEXT("Cue follows off"), Vitals->IsEditModeShown());

	Character->ToggleEditMode();
	TestTrue(TEXT("Cue follows toggle on"), Vitals->IsEditModeShown());

	Vitals->InitWithCharacter(nullptr);
	TestFalse(TEXT("Cue cleared on unbind"), Vitals->IsEditModeShown());

	Character->ToggleEditMode();
	Character->ToggleEditMode();
	TestFalse(TEXT("Unbound widget ignores later toggles"), Vitals->IsEditModeShown());
	return true;
}

#undef VC_EDITMODE_FLAGS

#endif // WITH_AUTOMATION_TESTS
