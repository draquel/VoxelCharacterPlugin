// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Core/VCPlayerState.h"
#include "Map/VCMapExplorationComponent.h"
#include "VoxelMapExploredMask.h"
#include "VoxelMapSubsystem.h"

// ---------------------------------------------------------------------------
// Each player's map exploration in a multiplayer save (G271). The owning client sends its explored mask to the server in
// parts; the server joins them and keeps a blob only when it decodes to a mask; a save's mask goes back the same way.
// - Parts: a blob cut into parts and joined comes back whole, from one part and from several; a part out of order, a
//   count that changes and a blob past the cap are dropped, and a part 0 starts over.
// - Validate: an encoded mask is kept with its own tile count; garbage, the coordinate pairs saves held before the mask
//   and a blob past the cap are not.
// - PIEKey: a Null id keeps its machine part and takes the PIE instance after it; any other id stays as it is; no id is
//   Local0; with no suffix the key is what it was before G271.
// ---------------------------------------------------------------------------

#define VC_MAP_SYNC_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace VCMapExplorationSyncTests
{
	/** Join every part of a blob through an assembler. */
	bool JoinAll(FVCExploredBlobAssembler& Assembler, const TArray<FString>& Parts, FString& OutBlob)
	{
		bool bComplete = false;
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			bComplete = Assembler.AddPart(Index, Parts.Num(), Parts[Index], UVCMapExplorationComponent::MaxBlobChars, OutBlob);
		}
		return bComplete;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCMapExplorationSync_Parts, "VoxelCharacter.Map.ExploredSync.Parts", VC_MAP_SYNC_FLAGS)
bool FVCMapExplorationSync_Parts::RunTest(const FString& Parameters)
{
	using namespace VCMapExplorationSyncTests;
	const int32 PartChars = UVCMapExplorationComponent::PartChars;

	TestEqual(TEXT("No parts for an empty blob"), UVCMapExplorationComponent::SplitBlob(FString()).Num(), 0);

	const FString Small = TEXT("VMXMabc");
	FVCExploredBlobAssembler Assembler;
	FString Out;
	TestTrue(TEXT("One part"), JoinAll(Assembler, UVCMapExplorationComponent::SplitBlob(Small), Out) && Out == Small);

	FString Large;
	for (int32 I = 0; I < PartChars * 2 + 17; ++I)
	{
		Large.AppendChar(TCHAR('A' + I % 26));
	}
	const TArray<FString> Parts = UVCMapExplorationComponent::SplitBlob(Large);
	TestEqual(TEXT("Three parts"), Parts.Num(), 3);
	TestEqual(TEXT("The last part holds the rest"), Parts.Last().Len(), 17);
	TestTrue(TEXT("Several parts join"), JoinAll(Assembler, Parts, Out) && Out == Large);

	// Out of order: part 1 before part 0 completes nothing, and part 2 after a dropped 1 does not either.
	TestFalse(TEXT("Part 1 first"), Assembler.AddPart(1, 3, Parts[1], UVCMapExplorationComponent::MaxBlobChars, Out));
	TestFalse(TEXT("Part 0"), Assembler.AddPart(0, 3, Parts[0], UVCMapExplorationComponent::MaxBlobChars, Out));
	TestFalse(TEXT("Part 2 skipping 1"), Assembler.AddPart(2, 3, Parts[2], UVCMapExplorationComponent::MaxBlobChars, Out));
	TestFalse(TEXT("... and the blob is dropped: part 1 now completes nothing"), Assembler.AddPart(1, 3, Parts[1], UVCMapExplorationComponent::MaxBlobChars, Out));

	// A count that changes mid-blob drops it.
	TestFalse(TEXT("Part 0 of 3"), Assembler.AddPart(0, 3, Parts[0], UVCMapExplorationComponent::MaxBlobChars, Out));
	TestFalse(TEXT("Part 1 of 2"), Assembler.AddPart(1, 2, Parts[1], UVCMapExplorationComponent::MaxBlobChars, Out));

	// A part 0 starts over: a half-sent blob followed by a whole one gives the whole one.
	TestFalse(TEXT("Half a blob"), Assembler.AddPart(0, 3, Parts[0], UVCMapExplorationComponent::MaxBlobChars, Out));
	TestTrue(TEXT("A new blob after half of one"), JoinAll(Assembler, UVCMapExplorationComponent::SplitBlob(Small), Out) && Out == Small);

	// Past the cap: dropped.
	TestFalse(TEXT("Over the cap"), Assembler.AddPart(0, 1, Large, PartChars, Out));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCMapExplorationSync_Validate, "VoxelCharacter.Map.ExploredSync.Validate", VC_MAP_SYNC_FLAGS)
bool FVCMapExplorationSync_Validate::RunTest(const FString& Parameters)
{
	FVoxelMapExploredMask Mask;
	Mask.Configure(1, 0);
	Mask.Set(0, 10, 20);
	Mask.Set(0, 11, 20);
	Mask.SetRect(0, FIntRect(-64, -64, 0, 0)); // a full block
	const FString Blob = UVoxelMapSubsystem::EncodeExploredMask(Mask);

	int32 Count = -1;
	TestTrue(TEXT("A mask is kept"), UVCMapExplorationComponent::ValidateBlob(Blob, Count));
	TestEqual(TEXT("... with the tiles it holds"), Count, static_cast<int32>(Mask.Num()));
	TestTrue(TEXT("... through parts too"), UVCMapExplorationComponent::SplitBlob(Blob).Num() >= 1);

	TestFalse(TEXT("Empty"), UVCMapExplorationComponent::ValidateBlob(FString(), Count));
	TestEqual(TEXT("... no tiles"), Count, 0);
	TestFalse(TEXT("Garbage"), UVCMapExplorationComponent::ValidateBlob(TEXT("not a mask at all"), Count));
	TestFalse(TEXT("The coordinate pairs saves held before the mask"),
		UVCMapExplorationComponent::ValidateBlob(UVoxelMapSubsystem::EncodeTileCoords({ FIntPoint(1, 2), FIntPoint(3, 4) }), Count));
	// The mask's own characters, padded past the cap: refused on its size before it is decoded.
	const FString Huge = Blob + FString::ChrN(UVCMapExplorationComponent::MaxBlobChars, TEXT('A'));
	TestFalse(TEXT("Past the cap"), UVCMapExplorationComponent::ValidateBlob(Huge, Count));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCMapExplorationSync_PIEKey, "VoxelCharacter.Save.PIEKey", VC_MAP_SYNC_FLAGS)
bool FVCMapExplorationSync_PIEKey::RunTest(const FString& Parameters)
{
	const FString NullId = TEXT("DESKTOP-HA9Q58I-0123456789ABCDEF0123456789ABCDEF");
	TestEqual(TEXT("A Null id: its machine part"), AVCPlayerState::ComposeSaveKey(NullId, true, FString()), FString(TEXT("DESKTOP-HA9Q58I")));
	TestEqual(TEXT("A Null id with a PIE instance"), AVCPlayerState::ComposeSaveKey(NullId, true, TEXT("PIE2")), FString(TEXT("DESKTOP-HA9Q58I-PIE2")));
	TestEqual(TEXT("Two instances, two keys"), AVCPlayerState::ComposeSaveKey(NullId, true, TEXT("PIE1")) != AVCPlayerState::ComposeSaveKey(NullId, true, TEXT("PIE2")), true);
	TestEqual(TEXT("A Null id without the GUID stays"), AVCPlayerState::ComposeSaveKey(TEXT("HOST"), true, FString()), FString(TEXT("HOST")));
	TestEqual(TEXT("Another subsystem's id as it is, suffix or not"), AVCPlayerState::ComposeSaveKey(TEXT("76561198000000000"), false, TEXT("PIE1")),
		FString(TEXT("76561198000000000")));
	TestEqual(TEXT("No id"), AVCPlayerState::ComposeSaveKey(FString(), false, TEXT("PIE1")), FString(TEXT("Local0")));
	return true;
}

#undef VC_MAP_SYNC_FLAGS

#endif // WITH_AUTOMATION_TESTS
