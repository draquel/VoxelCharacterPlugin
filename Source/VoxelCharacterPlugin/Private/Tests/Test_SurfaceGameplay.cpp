// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Core/VCGameModeBase.h"
#include "Gathering/VCGatherTable.h"
#include "Core/VCTypes.h"
#include "JsonObjectConverter.h"

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

	// Chopping scatter trees: category lookup and the tool multiplier.
	FVCHarvestEntry Tree;
	Tree.Category = TEXT("Tree");
	Tree.ItemId = Wood.ItemId;
	Tree.BaseYield = 1;
	Tree.ToolYield = 2;
	Tree.HitsToRemove = 3;
	Table->HarvestEntries.Add(Tree);
	TestNotNull(TEXT("Trees are harvestable"), Table->FindHarvest(TEXT("Tree")));
	TestNull(TEXT("Rocks are not"), Table->FindHarvest(TEXT("Rock")));
	TestEqual(TEXT("Bare-hand chop"), UVCGatherTable::HarvestYieldFor(Tree, 1.0f, 1.5f), 1);
	TestEqual(TEXT("Tool chop"), UVCGatherTable::HarvestYieldFor(Tree, 1.5f, 1.5f), 2);
	return true;
}

// ---------------------------------------------------------------------------
// Gathering yields once per voxel (WORLD_MODES_REVIEW_PLAN.md G3): a dig pays out only for a voxel it turned
// from solid to air, the one nearest the brush centre. Digs that only lower densities pay nothing.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSurface_GatherOncePerVoxel, "VoxelCharacter.Gather.OncePerVoxel", VC_SURFACE_FLAGS)
bool FVCSurface_GatherOncePerVoxel::RunTest(const FString& Parameters)
{
	const FVoxelData Wood(20, 255);
	const FVoxelData Stone(2, 255);
	const TArray<float> DistSq = { 0.0f, 5625.0f, 11250.0f };

	// The aimed voxel loses 80 a dig: 255 -> 175 -> 95. Its neighbours lose 21.
	TArray<FVoxelData> Before = { Wood, Stone, Stone };
	TArray<FVoxelData> After = { FVoxelData(20, 175), FVoxelData(2, 234), FVoxelData(2, 234) };
	TestEqual(TEXT("First dig empties nothing: no pay"), UVCGatherTable::FindDugVoxel(Before, After, DistSq), INDEX_NONE);

	Before = After;
	After = { FVoxelData(20, 95), FVoxelData(2, 213), FVoxelData(2, 213) };
	TestEqual(TEXT("Second dig empties the aimed voxel: it pays"), UVCGatherTable::FindDugVoxel(Before, After, DistSq), 0);

	Before = After;
	After = { FVoxelData(20, 15), FVoxelData(2, 192), FVoxelData(2, 192) };
	TestEqual(TEXT("Digging the emptied voxel again pays nothing"), UVCGatherTable::FindDugVoxel(Before, After, DistSq), INDEX_NONE);

	// Two voxels emptied by one dig: the one nearest the centre decides the material.
	Before = { FVoxelData(0, 0), FVoxelData(2, 130), FVoxelData(20, 130) };
	After = { FVoxelData(0, 0), FVoxelData(2, 60), FVoxelData(20, 60) };
	TestEqual(TEXT("Nearest emptied voxel"), UVCGatherTable::FindDugVoxel(Before, After, DistSq), 1);

	TestEqual(TEXT("Mismatched arrays are clamped, not overrun"),
		UVCGatherTable::FindDugVoxel(Before, TArray<FVoxelData>{ FVoxelData(0, 0) }, DistSq), INDEX_NONE);
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

// ---------------------------------------------------------------------------
// Persistence (feature 9): the per-player save state survives a JSON round trip.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSurface_PlayerSaveRoundTrip, "VoxelCharacter.Save.PlayerStateRoundTrip", VC_SURFACE_FLAGS)
bool FVCSurface_PlayerSaveRoundTrip::RunTest(const FString& Parameters)
{
	FVCPlayerSaveState State;
	State.PlayerKey = TEXT("Local0");
	FVCSnapshotInventoryEntry Entry;
	Entry.SlotIndex = 3;
	Entry.ItemJson = TEXT("{\"def\":\"ID_Tool_Torch\",\"fuel\":512}");
	State.Inventory.Add(Entry);
	FVCSnapshotEquipmentEntry Equip;
	Equip.SlotTag = FGameplayTag::RequestGameplayTag(TEXT("Equipment.Slot.MainHand"), false);
	Equip.ItemJson = TEXT("{\"def\":\"ID_Weapon_IronSword\"}");
	State.Equipment.Add(Equip);
	State.Progression.DungeonsCleared = 2;
	State.Progression.BossesKilled = 1;
	State.bHasRespawnPoint = true;
	State.RespawnPoint = FTransform(FRotator(0.f, 45.f, 0.f), FVector(100.f, 200.f, 300.f));
	State.bHasLastTransform = true;
	State.LastTransform = FTransform(FRotator(0.f, -90.f, 0.f), FVector(-5.f, 6.f, 7.f));
	State.bHasVitals = true;
	State.Health = 73.5f;
	State.Stamina = 12.f;

	FString Json;
	TestTrue(TEXT("Serialize"), FJsonObjectConverter::UStructToJsonObjectString(State, Json));
	FVCPlayerSaveState Back;
	TestTrue(TEXT("Parse"), FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Back));
	TestEqual(TEXT("Key"), Back.PlayerKey, State.PlayerKey);
	TestEqual(TEXT("Inventory entries"), Back.Inventory.Num(), 1);
	TestEqual(TEXT("Inventory slot"), Back.Inventory[0].SlotIndex, 3);
	TestEqual(TEXT("Inventory json"), Back.Inventory[0].ItemJson, Entry.ItemJson);
	TestEqual(TEXT("Equipment slot tag"), Back.Equipment[0].SlotTag.ToString(), Equip.SlotTag.ToString());
	TestEqual(TEXT("Cleared"), Back.Progression.DungeonsCleared, 2);
	TestTrue(TEXT("Respawn point"), Back.bHasRespawnPoint && Back.RespawnPoint.GetLocation().Equals(State.RespawnPoint.GetLocation()));
	TestTrue(TEXT("Last transform"), Back.bHasLastTransform && Back.LastTransform.GetLocation().Equals(State.LastTransform.GetLocation()));
	TestEqual(TEXT("Health"), Back.Health, 73.5f, 0.001f);
	return true;
}

#undef VC_SURFACE_FLAGS

#endif // WITH_AUTOMATION_TESTS
