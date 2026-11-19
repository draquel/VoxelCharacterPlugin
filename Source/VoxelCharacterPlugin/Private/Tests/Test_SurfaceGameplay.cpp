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
// G244: a player who starts where another stands gets a place of its own; alone, the start itself.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSurface_SpawnPlaces, "VoxelCharacter.Spawn.PlayerPlaces", VC_SURFACE_FLAGS)
bool FVCSurface_SpawnPlaces::RunTest(const FString& Parameters)
{
	const FVector Start(0.0, -28.0, 1200.0);
	const FVector Up = FVector::UpVector;
	const FVector Forward = FVector::ForwardVector;
	constexpr float Spacing = 250.f;
	FVector Place;

	// Alone (single player, or the first player): the start itself.
	TestEqual(TEXT("No other player: the start"), AVCGameModeBase::FindFreeSpawnPlace(Start, Up, Forward, {}, Spacing, Place), 0);
	TestTrue(TEXT("No other player: at the start"), Place.Equals(Start));

	// The first player's pawn on the ground under the start (the wait placed it 900 uu lower): the second goes to the
	// first ring, along the start's facing, Spacing away across up.
	TArray<FVector> Taken = { Start - FVector(0.0, 0.0, 900.0) };
	TestEqual(TEXT("Start taken: the first ring's first place"), AVCGameModeBase::FindFreeSpawnPlace(Start, Up, Forward, Taken, Spacing, Place), 1);
	TestTrue(TEXT("... Spacing along the facing"), Place.Equals(Start + Forward * Spacing, 0.01));

	// A third player: the next place, Spacing from both.
	Taken.Add(Place);
	TestEqual(TEXT("Two taken: the second place"), AVCGameModeBase::FindFreeSpawnPlace(Start, Up, Forward, Taken, Spacing, Place), 2);
	for (const FVector& Other : Taken)
	{
		TestTrue(TEXT("... apart from every other player"), FVector::Dist2D(Place, Other) >= Spacing - 0.01);
	}

	// A pawn far below (a dungeon under the start) or just past half the spacing does not take the start.
	TestFalse(TEXT("A pawn 6,000 uu down does not take it"),
		AVCGameModeBase::IsSpawnPlaceTaken(Start, Up, { Start - FVector(0.0, 0.0, 6000.0) }, Spacing));
	TestFalse(TEXT("A pawn 130 uu across does not take it"),
		AVCGameModeBase::IsSpawnPlaceTaken(Start, Up, { Start + FVector(130.0, 0.0, 0.0) }, Spacing));
	TestTrue(TEXT("A pawn 100 uu across takes it"),
		AVCGameModeBase::IsSpawnPlaceTaken(Start, Up, { Start + FVector(100.0, 0.0, 0.0) }, Spacing));

	// On a planet: the places lie across the radius, at the start's distance from the centre.
	const FVector PlanetUp = FVector(1.0, 1.0, 0.0).GetSafeNormal();
	const FVector PlanetStart = PlanetUp * 100000.0;
	TestEqual(TEXT("Planet: the first ring"),
		AVCGameModeBase::FindFreeSpawnPlace(PlanetStart, PlanetUp, Forward, { PlanetStart }, Spacing, Place), 1);
	TestTrue(TEXT("Planet: across local up"), FMath::IsNearlyZero(FVector::DotProduct(Place - PlanetStart, PlanetUp), 0.01));
	TestTrue(TEXT("Planet: Spacing from the start"), FMath::IsNearlyEqual(FVector::Dist(Place, PlanetStart), static_cast<double>(Spacing), 0.01));

	// Every place taken: INDEX_NONE and the start.
	TArray<FVector> All = { Start };
	for (int32 Ring = 1; Ring <= AVCGameModeBase::PlayerSpawnRings; ++Ring)
	{
		for (int32 I = 0; I < 6 * Ring; ++I)
		{
			const double Angle = 2.0 * UE_DOUBLE_PI * I / (6 * Ring);
			All.Add(Start + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0) * (Ring * Spacing));
		}
	}
	TestEqual(TEXT("All taken: none"), AVCGameModeBase::FindFreeSpawnPlace(Start, Up, Forward, All, Spacing, Place), static_cast<int32>(INDEX_NONE));
	TestTrue(TEXT("All taken: the start"), Place.Equals(Start));
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
