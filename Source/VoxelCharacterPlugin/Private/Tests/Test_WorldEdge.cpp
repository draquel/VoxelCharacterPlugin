// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Testing/VCTestWorld.h"

#if WITH_AUTOMATION_TESTS

#include "Core/VCCharacterBase.h"
#include "Movement/VCMovementComponent.h"
#include "UI/VCVitalsWidget.h"
#include "Blueprint/UserWidget.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/WorldSettings.h"

// ---------------------------------------------------------------------------
// Leaving a bounded world (VoxelWorlds WORLD_MODES_REVIEW_PLAN.md §14.2 WM-I4, WM-I5; decision D9): the current before
// an island's edge, the HUD hint that names it, and what falling out of the world does to a body nobody plays.
// ---------------------------------------------------------------------------

namespace VCWorldEdgeTestHelpers
{
	/** A game world that has begun play (VCTesting::FPlayWorld), its actors destroyed before it. */
	struct FWorldHarness : VCTesting::FPlayWorld
	{
		FWorldHarness() : VCTesting::FPlayWorld(TEXT("VCWorldEdgeTestWorld"), /*bDestroyActorsFirst=*/true) {}
	};

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

	/** Island S's footprint: R + F = 40,000 around the origin. */
	FVoxelWorldBounds Island()
	{
		FVoxelWorldBounds Bounds;
		Bounds.Shape = EVoxelWorldBoundsShape::Circle;
		Bounds.OuterRadius = 40000.f;
		Bounds.HalfExtent = FVector2D(40000.0, 40000.0);
		return Bounds;
	}
}

#define VC_WORLDEDGE_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// The current: no effect inside the island or in an unbounded world; across the band the outward speed is capped, from
// the full speed at its inner side to an inward drift at the edge; motion along the coast, inward or vertical is kept.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCWorldEdge_PushBack, "VoxelCharacter.WorldEdge.PushBack", VC_WORLDEDGE_FLAGS)
bool FVCWorldEdge_PushBack::RunTest(const FString& Parameters)
{
	const FVoxelWorldBounds Bounds = VCWorldEdgeTestHelpers::Island();
	constexpr float Band = 4000.f, Push = 250.f, MaxSpeed = 360.f;
	const FVector Out(360.0, 0.0, 50.0); // swimming straight out (+X) and a little up
	float Proximity = -1.f;

	auto At = [&](double X, const FVector& Velocity)
	{
		return UVCMovementComponent::ApplyWorldEdgePushBack(Bounds, FVector(X, 0.0, -1500.0), Velocity, MaxSpeed, Band, Push, Proximity);
	};

	TestTrue(TEXT("Inside the island: unchanged"), At(30000.0, Out).Equals(Out));
	TestEqual(TEXT("Inside the island: proximity 0"), Proximity, 0.f);

	const FVector Mid = At(38000.0, Out); // half way into the band
	TestTrue(TEXT("Half way: proximity 0.5"), FMath::IsNearlyEqual(Proximity, 0.5f, 1.0e-4f));
	TestTrue(TEXT("Half way: outward speed capped to the midpoint of max and -push"), FMath::IsNearlyEqual(Mid.X, (MaxSpeed - Push) * 0.5, 0.01));
	TestTrue(TEXT("Half way: vertical speed kept"), FMath::IsNearlyEqual(Mid.Z, 50.0));

	const FVector Edge = At(39999.0, Out);
	TestTrue(TEXT("At the edge: carried back inward"), Edge.X < -Push * 0.99);
	TestTrue(TEXT("At the edge: proximity ~1"), Proximity > 0.99f);

	const FVector In(-200.0, 0.0, 0.0);
	TestTrue(TEXT("Swimming back in fast enough is not slowed"), At(39000.0, In).Equals(In));
	const FVector Along(0.0, 300.0, 0.0);
	const FVector AlongResult = At(39999.0, Along);
	TestTrue(TEXT("Along the coast at the edge: the sideways speed is kept"), FMath::IsNearlyEqual(AlongResult.Y, 300.0));

	// The equilibrium: pushing straight out at full speed, the swimmer stalls about a third of the band before the edge.
	const double StallDistance = Band * (1.0 - MaxSpeed / (MaxSpeed + Push));
	TestTrue(TEXT("Stalls inside the band"), FMath::IsNearlyEqual(At(40000.0 - StallDistance, Out).X, 0.0, 0.01));

	// No edge, no current: a plane, a planet, or the band turned off.
	TestTrue(TEXT("Unbounded: unchanged"), UVCMovementComponent::ApplyWorldEdgePushBack(FVoxelWorldBounds(), FVector(1.0e7, 0.0, 0.0), Out, MaxSpeed, Band, Push, Proximity).Equals(Out));
	FVoxelWorldBounds Shell = Bounds;
	Shell.Shape = EVoxelWorldBoundsShape::Shell;
	TestTrue(TEXT("Planet: unchanged"), UVCMovementComponent::ApplyWorldEdgePushBack(Shell, FVector(39999.0, 0.0, 0.0), Out, MaxSpeed, Band, Push, Proximity).Equals(Out));
	TestTrue(TEXT("Band 0: unchanged"), UVCMovementComponent::ApplyWorldEdgePushBack(Bounds, FVector(39999.0, 0.0, 0.0), Out, MaxSpeed, 0.f, Push, Proximity).Equals(Out));

	// A rectangular island pushes back across its nearest side.
	FVoxelWorldBounds Rect;
	Rect.Shape = EVoxelWorldBoundsShape::Rectangle;
	Rect.HalfExtent = FVector2D(40000.0, 30000.0);
	const FVector North(0.0, 360.0, 0.0);
	TestTrue(TEXT("Rectangle: pushed back across the near side"),
		UVCMovementComponent::ApplyWorldEdgePushBack(Rect, FVector(0.0, 29999.0, 0.0), North, MaxSpeed, Band, Push, Proximity).Y < 0.0);
	return true;
}

// ---------------------------------------------------------------------------
// The HUD hint follows the character's world edge state and clears on unbind.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCWorldEdge_VitalsHint, "VoxelCharacter.WorldEdge.VitalsHint", VC_WORLDEDGE_FLAGS)
bool FVCWorldEdge_VitalsHint::RunTest(const FString& Parameters)
{
	VCWorldEdgeTestHelpers::FWorldHarness H;
	AVCCharacterBase* Character = VCWorldEdgeTestHelpers::SpawnCharacter(H.World);
	if (!Character) { AddError(TEXT("Spawn failed")); return false; }
	UVCVitalsWidget* Vitals = CreateWidget<UVCVitalsWidget>(H.World, UVCVitalsWidget::StaticClass());
	if (!Vitals) { AddError(TEXT("CreateWidget failed")); return false; }

	Vitals->InitWithCharacter(Character);
	TestFalse(TEXT("Hidden away from the edge"), Vitals->IsWorldEdgeHintShown());
	Character->OnWorldEdgeChanged.Broadcast(true);
	TestTrue(TEXT("Shown in the band"), Vitals->IsWorldEdgeHintShown());
	Character->OnWorldEdgeChanged.Broadcast(false);
	TestFalse(TEXT("Hidden again when the pawn leaves the band"), Vitals->IsWorldEdgeHintShown());
	Character->OnWorldEdgeChanged.Broadcast(true);
	Vitals->InitWithCharacter(nullptr);
	TestFalse(TEXT("Cleared on unbind"), Vitals->IsWorldEdgeHintShown());
	return true;
}

// ---------------------------------------------------------------------------
// Falling out of the world with nobody playing the body: the engine default (it is destroyed), not a respawn. (A
// played body dies and respawns; that path needs a voxel world and a game mode and is checked live, plan §14.7.)
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCWorldEdge_UnpossessedFallsOut, "VoxelCharacter.WorldEdge.UnpossessedFallsOut", VC_WORLDEDGE_FLAGS)
bool FVCWorldEdge_UnpossessedFallsOut::RunTest(const FString& Parameters)
{
	VCWorldEdgeTestHelpers::FWorldHarness H;
	AVCCharacterBase* Character = VCWorldEdgeTestHelpers::SpawnCharacter(H.World);
	if (!Character) { AddError(TEXT("Spawn failed")); return false; }
	FVector Safe;
	TestFalse(TEXT("No safe ground known before the pawn stood anywhere"), Character->GetLastSafeLocation(Safe));
	Character->FellOutOfWorld(*GetDefault<UDamageType>());
	TestTrue(TEXT("An unpossessed body is destroyed"), !IsValid(Character) || Character->IsActorBeingDestroyed());
	return true;
}

#undef VC_WORLDEDGE_FLAGS

#endif // WITH_AUTOMATION_TESTS
