// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Core/VCGameModeBase.h"
#include "Gathering/VCGatherTable.h"

#define VC_SURFACE_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// Gathering (feature 8): material -> item mapping and the tool multiplier are pure.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSurface_GatherTable, "VoxelCharacter.Gather.TableYield", VC_SURFACE_FLAGS)
bool FVCSurface_GatherTable::RunTest(const FString& Parameters)
{
	UVCGatherTable* Table = NewObject<UVCGatherTable>();
	Table->ToolMiningSpeedThreshold = 1.5f;
	FVCGatherEntry Stone;
	Stone.MaterialId = 2;
	Stone.ItemId = FPrimaryAssetId(TEXT("ItemDefinition"), TEXT("ID_Material_Stone"));
	Stone.BaseYield = 1;
	Stone.ToolYield = 2;
	Table->Entries.Add(Stone);
	FVCGatherEntry Wood;
	Wood.MaterialId = 20;
	Wood.ItemId = FPrimaryAssetId(TEXT("ItemDefinition"), TEXT("ID_Material_Wood"));
	Table->Entries.Add(Wood);

	TestNotNull(TEXT("Stone is diggable"), Table->Find(2));
	TestNotNull(TEXT("Wood is diggable"), Table->Find(20));
	TestNull(TEXT("Dirt yields nothing"), Table->Find(1));

	TestEqual(TEXT("Bare hands: base yield"), UVCGatherTable::YieldFor(Stone, 1.0f, 1.5f), 1);
	TestEqual(TEXT("Stone pickaxe (1.5): tool yield"), UVCGatherTable::YieldFor(Stone, 1.5f, 1.5f), 2);
	TestEqual(TEXT("Iron pickaxe (2.0): tool yield"), UVCGatherTable::YieldFor(Stone, 2.0f, 1.5f), 2);
	TestEqual(TEXT("Wood is one either way"), UVCGatherTable::YieldFor(Wood, 2.0f, 1.5f), 1);
	return true;
}

// ---------------------------------------------------------------------------
// Respawn (feature 8): AtLastRest picks the rest point when there is one.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSurface_RespawnAtLastRest, "VoxelCharacter.Respawn.AtLastRest", VC_SURFACE_FLAGS)
bool FVCSurface_RespawnAtLastRest::RunTest(const FString& Parameters)
{
	const FTransform Death(FRotator(30.f, 90.f, 10.f), FVector(100.f, 200.f, 300.f), FVector(2.f));
	const FTransform Rest(FRotator(0.f, 45.f, 0.f), FVector(-5000.f, 0.f, 50.f));
	bool bPlayerStart = true;

	FTransform Out = AVCGameModeBase::ResolveRespawnTransform(EVCRespawnPolicy::AtLastRest, true, Rest, Death, bPlayerStart);
	TestFalse(TEXT("Rest point: no player start"), bPlayerStart);
	TestTrue(TEXT("Rest point location"), Out.GetLocation().Equals(Rest.GetLocation()));

	Out = AVCGameModeBase::ResolveRespawnTransform(EVCRespawnPolicy::AtLastRest, false, Rest, Death, bPlayerStart);
	TestTrue(TEXT("No rest point yet: death location"), Out.GetLocation().Equals(Death.GetLocation()));
	TestTrue(TEXT("Upright"), FMath::IsNearlyZero(Out.Rotator().Pitch) && FMath::IsNearlyZero(Out.Rotator().Roll));
	TestTrue(TEXT("Unit scale"), Out.GetScale3D().Equals(FVector::OneVector));

	Out = AVCGameModeBase::ResolveRespawnTransform(EVCRespawnPolicy::AtDeathLocation, true, Rest, Death, bPlayerStart);
	TestTrue(TEXT("Death policy ignores the rest point"), Out.GetLocation().Equals(Death.GetLocation()));

	AVCGameModeBase::ResolveRespawnTransform(EVCRespawnPolicy::AtPlayerStart, true, Rest, Death, bPlayerStart);
	TestTrue(TEXT("Player start policy asks for one"), bPlayerStart);
	return true;
}

#undef VC_SURFACE_FLAGS

#endif // WITH_AUTOMATION_TESTS
