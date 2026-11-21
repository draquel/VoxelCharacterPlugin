// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "AIController.h"
#include "Camera/VCNearClipOverride.h"
#include "Core/VCCharacterBase.h"
#include "Engine/World.h"
#include "RenderCore.h"
#include "Testing/VCTestWorld.h"

// ---------------------------------------------------------------------------
// G192: the near clip plane is the process's (GNearClippingPlane, from the project's NearClipPlane at start), and the
// blend into first person lowers it for the camera's pass through the body. Every way out of the blend puts back the
// plane the blend found: it used to write the engine's default of 10 uu, so a session that had entered first person
// rendered with another near plane than the project's (1 uu here) from then on.
// ---------------------------------------------------------------------------

#define VC_NEARCLIP_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace VCNearClipTests
{
	/** The process's near plane set for a case, and put back as the test found it when the case ends. */
	struct FScopedNearPlane
	{
		const float Found = GNearClippingPlane;

		explicit FScopedNearPlane(float Plane) { SetNearClipPlaneGlobals(Plane); }
		~FScopedNearPlane() { SetNearClipPlaneGlobals(Found); }

		FScopedNearPlane(const FScopedNearPlane&) = delete;
		FScopedNearPlane& operator=(const FScopedNearPlane&) = delete;
	};

	/** A player character with no terrain wait; possessed by an AI controller it counts as locally controlled. */
	AVCCharacterBase* SpawnPlayer(UWorld* World, bool bPossessed)
	{
		const FTransform Stand(FVector::ZeroVector);
		AVCCharacterBase* Character = World->SpawnActorDeferred<AVCCharacterBase>(AVCCharacterBase::StaticClass(), Stand, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Character)
		{
			return nullptr;
		}
		Character->bWaitForTerrain = false;
		Character->FinishSpawning(Stand);
		if (bPossessed)
		{
			if (AAIController* Controller = World->SpawnActor<AAIController>())
			{
				Controller->Possess(Character);
			}
		}
		return Character;
	}

	/** Tick the character alone (its camera blend runs in its Tick) until no one holds the near plane, or Seconds pass. */
	float TickUntilReleased(AVCCharacterBase* Character, float Seconds)
	{
		const float Dt = 1.f / 60.f;
		float Elapsed = 0.f;
		while (FVCNearClipOverride::NumHolders() > 0 && Elapsed < Seconds)
		{
			Character->TickActor(Dt, LEVELTICK_All, Character->PrimaryActorTick);
			Elapsed += Dt;
		}
		return Elapsed;
	}
}

// ---------------------------------------------------------------------------
// The override itself: the first holder remembers the plane it finds, the last puts it back; a plane already near
// enough is not touched; a plane something else set meanwhile is kept.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCNearClip_OverrideRestoresWhatItFound, "VoxelCharacter.NearClip.OverrideRestoresWhatItFound", VC_NEARCLIP_FLAGS)
bool FVCNearClip_OverrideRestoresWhatItFound::RunTest(const FString& Parameters)
{
	using namespace VCNearClipTests;
	if (!TestEqual(TEXT("No holder before the test"), FVCNearClipOverride::NumHolders(), 0))
	{
		return false;
	}

	{
		// The engine's default: lowered for the hold, back after it.
		FScopedNearPlane Plane(10.f);
		FVCNearClipOverride::Acquire(1.f);
		TestEqual(TEXT("At 10: lowered to 1 while held"), GNearClippingPlane, 1.f);
		TestEqual(TEXT("At 10: one holder"), FVCNearClipOverride::NumHolders(), 1);
		FVCNearClipOverride::Release();
		TestEqual(TEXT("At 10: back to 10"), GNearClippingPlane, 10.f);
		TestEqual(TEXT("At 10: no holder"), FVCNearClipOverride::NumHolders(), 0);
	}
	{
		// This project's plane: the hold changes nothing, and neither does its end (the bug wrote 10 here).
		FScopedNearPlane Plane(1.f);
		FVCNearClipOverride::Acquire(1.f);
		TestEqual(TEXT("At 1: still 1 while held"), GNearClippingPlane, 1.f);
		FVCNearClipOverride::Release();
		TestEqual(TEXT("At 1: still 1 after"), GNearClippingPlane, 1.f);
	}
	{
		// Nearer than the hold needs: never raised.
		FScopedNearPlane Plane(0.5f);
		FVCNearClipOverride::Acquire(1.f);
		TestEqual(TEXT("At 0.5: still 0.5 while held"), GNearClippingPlane, 0.5f);
		FVCNearClipOverride::Release();
		TestEqual(TEXT("At 0.5: still 0.5 after"), GNearClippingPlane, 0.5f);
	}
	{
		// Two holders (two local pawns blending at once): the plane goes back with the last.
		FScopedNearPlane Plane(10.f);
		FVCNearClipOverride::Acquire(1.f);
		FVCNearClipOverride::Acquire(1.f);
		TestEqual(TEXT("Two holders"), FVCNearClipOverride::NumHolders(), 2);
		FVCNearClipOverride::Release();
		TestEqual(TEXT("Two holders, one done: still 1"), GNearClippingPlane, 1.f);
		FVCNearClipOverride::Release();
		TestEqual(TEXT("Two holders, both done: back to 10"), GNearClippingPlane, 10.f);
	}
	{
		// Something else set the plane during the hold (r.SetNearClipPlane): its value is kept.
		FScopedNearPlane Plane(10.f);
		FVCNearClipOverride::Acquire(1.f);
		SetNearClipPlaneGlobals(20.f);
		FVCNearClipOverride::Release();
		TestEqual(TEXT("Set from outside during the hold: kept"), GNearClippingPlane, 20.f);
	}
	{
		// ... and between two holds: the second holder takes the outside value as the one to put back.
		FScopedNearPlane Plane(10.f);
		FVCNearClipOverride::Acquire(1.f);
		SetNearClipPlaneGlobals(5.f);
		FVCNearClipOverride::Acquire(1.f);
		TestEqual(TEXT("Set from outside, a second hold: lowered again"), GNearClippingPlane, 1.f);
		FVCNearClipOverride::Release();
		FVCNearClipOverride::Release();
		TestEqual(TEXT("Set from outside, both done: the outside value"), GNearClippingPlane, 5.f);
	}
	{
		// A release with no holder, and a hold that needs nothing: the plane is untouched and the count stays right.
		FScopedNearPlane Plane(10.f);
		FVCNearClipOverride::Release();
		TestEqual(TEXT("Release with no holder: no holder"), FVCNearClipOverride::NumHolders(), 0);
		TestEqual(TEXT("Release with no holder: untouched"), GNearClippingPlane, 10.f);
		FVCNearClipOverride::Acquire(0.f);
		TestEqual(TEXT("A hold that needs nothing: counted"), FVCNearClipOverride::NumHolders(), 1);
		TestEqual(TEXT("A hold that needs nothing: untouched"), GNearClippingPlane, 10.f);
		FVCNearClipOverride::Release();
		TestEqual(TEXT("A hold that needs nothing: untouched after"), GNearClippingPlane, 10.f);
	}
	TestEqual(TEXT("No holder after the test"), FVCNearClipOverride::NumHolders(), 0);
	return true;
}

// ---------------------------------------------------------------------------
// The character: after a switch to first person the near plane is what it was before, on every way out of the blend
// (the body hidden, the switch reversed, the pawn destroyed, the world torn down as a PIE stop does), at this
// project's 1 uu and at the engine's 10 uu.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCViewMode_NearClipRestored, "VoxelCharacter.ViewMode.NearClipRestored", VC_NEARCLIP_FLAGS)
bool FVCViewMode_NearClipRestored::RunTest(const FString& Parameters)
{
	using namespace VCNearClipTests;
	if (!TestEqual(TEXT("No holder before the test"), FVCNearClipOverride::NumHolders(), 0))
	{
		return false;
	}

	for (const float Project : { 1.f, 10.f })
	{
		const FString At = FString::Printf(TEXT("Project plane %.0f: "), Project);
		const float Blend = FMath::Min(Project, AVCCharacterBase::FirstPersonBlendNearPlane);
		FScopedNearPlane Plane(Project);

		{
			VCTesting::FPlayWorld W(TEXT("VCNearClipWorld"), /*bDestroyActorsFirst=*/true);

			// The blend runs to its end: the body is hidden and the plane is back.
			AVCCharacterBase* Player = SpawnPlayer(W.World, /*bPossessed=*/true);
			if (!TestNotNull(*(At + TEXT("the player spawns")), Player) || !TestTrue(*(At + TEXT("locally controlled")), Player->IsLocallyControlled()))
			{
				return false;
			}
			Player->SetViewMode(EVCViewMode::FirstPerson);
			TestEqual(*(At + TEXT("into first person: one holder")), FVCNearClipOverride::NumHolders(), 1);
			TestEqual(*(At + TEXT("into first person: the blend's plane")), GNearClippingPlane, Blend);
			const float BlendSeconds = TickUntilReleased(Player, 5.f);
			TestTrue(*(At + TEXT("the blend takes some frames and ends")), BlendSeconds > 0.f && BlendSeconds < 5.f);
			TestEqual(*(At + TEXT("body hidden: no holder")), FVCNearClipOverride::NumHolders(), 0);
			TestEqual(*(At + TEXT("body hidden: the plane as before the switch")), GNearClippingPlane, Project);

			// Back to third person and in again, each left to finish: still the project's.
			Player->SetViewMode(EVCViewMode::ThirdPerson);
			TestEqual(*(At + TEXT("back to third person: the plane as before")), GNearClippingPlane, Project);
			Player->SetViewMode(EVCViewMode::FirstPerson);
			TickUntilReleased(Player, 5.f);
			TestEqual(*(At + TEXT("a second switch: the plane as before")), GNearClippingPlane, Project);
			Player->SetViewMode(EVCViewMode::ThirdPerson);

			// The switch reversed during the blend.
			Player->SetViewMode(EVCViewMode::FirstPerson);
			Player->TickActor(1.f / 60.f, LEVELTICK_All, Player->PrimaryActorTick);
			TestEqual(*(At + TEXT("mid-blend: held")), FVCNearClipOverride::NumHolders(), 1);
			Player->SetViewMode(EVCViewMode::ThirdPerson);
			TestEqual(*(At + TEXT("reversed mid-blend: no holder")), FVCNearClipOverride::NumHolders(), 0);
			TestEqual(*(At + TEXT("reversed mid-blend: the plane as before")), GNearClippingPlane, Project);

			// The pawn destroyed during the blend (a death and respawn, a pawn swap).
			Player->SetViewMode(EVCViewMode::FirstPerson);
			TestEqual(*(At + TEXT("before the destroy: held")), FVCNearClipOverride::NumHolders(), 1);
			Player->Destroy();
			TestEqual(*(At + TEXT("destroyed mid-blend: no holder")), FVCNearClipOverride::NumHolders(), 0);
			TestEqual(*(At + TEXT("destroyed mid-blend: the plane as before")), GNearClippingPlane, Project);

			// A pawn nobody controls here (another player's on a client) draws third person: it holds nothing.
			AVCCharacterBase* Remote = SpawnPlayer(W.World, /*bPossessed=*/false);
			if (TestNotNull(*(At + TEXT("the remote pawn spawns")), Remote))
			{
				Remote->SetViewMode(EVCViewMode::FirstPerson);
				TestEqual(*(At + TEXT("not locally controlled: no holder")), FVCNearClipOverride::NumHolders(), 0);
				TestEqual(*(At + TEXT("not locally controlled: the plane untouched")), GNearClippingPlane, Project);
			}

			// The world torn down during the blend (a PIE stopped): the harness destroys the world at the scope's end.
			AVCCharacterBase* Last = SpawnPlayer(W.World, /*bPossessed=*/true);
			if (TestNotNull(*(At + TEXT("the last player spawns")), Last))
			{
				Last->SetViewMode(EVCViewMode::FirstPerson);
				TestEqual(*(At + TEXT("before the teardown: held")), FVCNearClipOverride::NumHolders(), 1);
			}
		}
		TestEqual(*(At + TEXT("world torn down mid-blend: no holder")), FVCNearClipOverride::NumHolders(), 0);
		TestEqual(*(At + TEXT("world torn down mid-blend: the plane as before")), GNearClippingPlane, Project);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
