// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Movement/VCMovementComponent.h"
#include "VoxelMaterialAtlas.h"
#include "VoxelMaterialRegistry.h"

// ---------------------------------------------------------------------------
// The surface under a pawn's feet (G247; code review CR2, VCP-14): the voxel material's surface class in the world's
// material table (FVoxelMaterialDefinition::SurfaceClass), mapped to the movement's surface type:
// - the registry's defaults give every named material the surface the movement's table by ID gave it before the class
//   was data, and a reserved or unknown ID none (Default);
// - an atlas's table gives the materials it adds their own: the demo's road (7) Stone and its trail (8) Dirt, with their
//   friction, where the defaults have none;
// - every class but Unset is a surface of its own.
// The classes in the tables: VoxelWorlds.MaterialRegistry.SurfaceClasses; the world's table: VoxelWorlds.Streaming.Cubic.
// TwoWorldsOwnAtlases.
// ---------------------------------------------------------------------------

namespace VCSurfaceTypesTestHelpers
{
	int32 AsInt(EVCSurfaceType Surface) { return static_cast<int32>(Surface); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSurfaceClassTest, "VoxelCharacter.Movement.SurfaceClass",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVCSurfaceClassTest::RunTest(const FString& Parameters)
{
	using namespace VCSurfaceTypesTestHelpers;

	// The defaults: what the movement's switch over EVoxelMaterial returned.
	const TPair<uint8, EVCSurfaceType> Before[] = {
		{ EVoxelMaterial::Grass, EVCSurfaceType::Grass }, { EVoxelMaterial::Dirt, EVCSurfaceType::Dirt },
		{ EVoxelMaterial::Stone, EVCSurfaceType::Stone }, { EVoxelMaterial::Sand, EVCSurfaceType::Sand },
		{ EVoxelMaterial::Snow, EVCSurfaceType::Snow }, { EVoxelMaterial::Sandstone, EVCSurfaceType::Sand },
		{ EVoxelMaterial::FrozenDirt, EVCSurfaceType::Ice }, { EVoxelMaterial::Coal, EVCSurfaceType::Stone },
		{ EVoxelMaterial::Iron, EVCSurfaceType::Metal }, { EVoxelMaterial::Gold, EVCSurfaceType::Metal },
		{ EVoxelMaterial::Copper, EVCSurfaceType::Metal }, { EVoxelMaterial::Diamond, EVCSurfaceType::Stone },
		{ EVoxelMaterial::Wood, EVCSurfaceType::Wood }, { EVoxelMaterial::Leaves, EVCSurfaceType::Grass } };
	TSet<uint8> Named;
	for (const TPair<uint8, EVCSurfaceType>& Entry : Before)
	{
		Named.Add(Entry.Key);
		TestEqual(*FString::Printf(TEXT("Default material %d keeps its surface"), Entry.Key),
			AsInt(UVCMovementComponent::MaterialIDToSurfaceType(Entry.Key)), AsInt(Entry.Value));
		TestEqual(*FString::Printf(TEXT("and reads it from the defaults' table (%d)"), Entry.Key),
			AsInt(UVCMovementComponent::MaterialIDToSurfaceType(Entry.Key, FVoxelMaterialTable::Defaults())), AsInt(Entry.Value));
	}
	for (int32 ID = 0; ID < 256; ++ID)
	{
		if (!Named.Contains(static_cast<uint8>(ID)))
		{
			const EVCSurfaceType Surface = UVCMovementComponent::MaterialIDToSurfaceType(static_cast<uint8>(ID));
			if (Surface != EVCSurfaceType::Default)
			{
				AddError(FString::Printf(TEXT("Material %d has no class in the defaults but reads surface %d"), ID, AsInt(Surface)));
			}
		}
	}

	// The demo atlas's road and trail over the reserved slots 7 and 8, and an added material with no class.
	TArray<FVoxelMaterialTextureConfig> Configs;
	for (const FVoxelMaterialDefinition& Mat : FVoxelMaterialTable::Defaults().GetAll())
	{
		FVoxelMaterialTextureConfig& Config = Configs.AddDefaulted_GetRef();
		Config.MaterialID = Mat.MaterialID;
		Config.MaterialName = Mat.Name;
	}
	Configs[7].MaterialName = TEXT("Road");
	Configs[7].SurfaceClass = EVoxelSurfaceClass::Stone;
	Configs[8].MaterialName = TEXT("Trail");
	Configs[8].SurfaceClass = EVoxelSurfaceClass::Dirt;
	FVoxelMaterialTextureConfig& Added = Configs.AddDefaulted_GetRef();
	Added.MaterialID = 30;
	Added.MaterialName = TEXT("Added");
	const TSharedRef<const FVoxelMaterialTable> Atlas = FVoxelMaterialTable::FromAtlas(Configs, 9, 10);

	TestEqual(TEXT("The defaults' material 7 has no surface"), AsInt(UVCMovementComponent::MaterialIDToSurfaceType(7)), AsInt(EVCSurfaceType::Default));
	TestEqual(TEXT("The atlas's road (7) is stone"), AsInt(UVCMovementComponent::MaterialIDToSurfaceType(7, *Atlas)), AsInt(EVCSurfaceType::Stone));
	TestEqual(TEXT("The atlas's trail (8) is dirt"), AsInt(UVCMovementComponent::MaterialIDToSurfaceType(8, *Atlas)), AsInt(EVCSurfaceType::Dirt));
	TestEqual(TEXT("The trail's friction is dirt's"), UVCMovementComponent::GetSurfaceFriction(UVCMovementComponent::MaterialIDToSurfaceType(8, *Atlas)), 0.9f);
	TestEqual(TEXT("The atlas keeps the defaults' grass"), AsInt(UVCMovementComponent::MaterialIDToSurfaceType(EVoxelMaterial::Grass, *Atlas)), AsInt(EVCSurfaceType::Grass));
	TestEqual(TEXT("An added material with no class has no surface"), AsInt(UVCMovementComponent::MaterialIDToSurfaceType(30, *Atlas)), AsInt(EVCSurfaceType::Default));
	TestEqual(TEXT("An ID past the atlas's table has no surface"), AsInt(UVCMovementComponent::MaterialIDToSurfaceType(200, *Atlas)), AsInt(EVCSurfaceType::Default));

	// Every class but Unset is a surface of its own.
	TestEqual(TEXT("Unset is Default"), AsInt(UVCMovementComponent::SurfaceClassToSurfaceType(EVoxelSurfaceClass::Unset)), AsInt(EVCSurfaceType::Default));
	TSet<EVCSurfaceType> Seen;
	const UEnum* Classes = StaticEnum<EVoxelSurfaceClass>();
	for (int32 Index = 0; Classes && Index < Classes->NumEnums() - 1; ++Index)
	{
		const EVoxelSurfaceClass Class = static_cast<EVoxelSurfaceClass>(Classes->GetValueByIndex(Index));
		if (Class == EVoxelSurfaceClass::Unset)
		{
			continue;
		}
		const EVCSurfaceType Surface = UVCMovementComponent::SurfaceClassToSurfaceType(Class);
		TestNotEqual(*FString::Printf(TEXT("Class %s has a surface"), *Classes->GetNameStringByIndex(Index)), AsInt(Surface), AsInt(EVCSurfaceType::Default));
		TestFalse(*FString::Printf(TEXT("Class %s's surface is its own"), *Classes->GetNameStringByIndex(Index)), Seen.Contains(Surface));
		Seen.Add(Surface);
	}
	TestEqual(TEXT("Nine classes, nine surfaces"), Seen.Num(), 9);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
