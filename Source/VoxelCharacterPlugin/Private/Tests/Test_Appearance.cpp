// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Animation/VCAnimNode_FootBoneIK.h"
#include "Animation/VCAppearanceComponent.h"
#include "Animation/VCCharacterAppearance.h"
#include "Core/VCTypes.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "JsonObjectConverter.h"

// ---------------------------------------------------------------------------
// The character's appearance (character animation P6, CHARACTER_ANIMATION_PLAN.md):
// - The order a player's appearance is picked in: their own choice, the game mode's, the settings' for the world's
//   meshing mode, the settings' default.
// - The player's choice survives the save document.
// - An appearance with a body mesh dresses the character's mesh, and none gives the class's own body back.
// - The foot-bone placement's pure parts: the pelvis drops to the lowest foot, within its limit; a planted foot tilts
//   to the ground, within its limit, and a foot in its swing does not.
// ---------------------------------------------------------------------------

#define VC_APPEARANCE_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace VCAppearanceTestHelpers
{
	TSoftObjectPtr<UVCCharacterAppearance> Named(const TCHAR* Path)
	{
		return TSoftObjectPtr<UVCCharacterAppearance>(FSoftObjectPath(Path));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAppearance_PickOrder, "VoxelCharacter.Appearance.PickOrder", VC_APPEARANCE_FLAGS)
bool FVCAppearance_PickOrder::RunTest(const FString& Parameters)
{
	using namespace VCAppearanceTestHelpers;
	const TSoftObjectPtr<UVCCharacterAppearance> None;
	const TSoftObjectPtr<UVCCharacterAppearance> Chosen = Named(TEXT("/Game/Test/DA_Chosen.DA_Chosen"));
	const TSoftObjectPtr<UVCCharacterAppearance> ForMode = Named(TEXT("/Game/Test/DA_GameMode.DA_GameMode"));
	const TSoftObjectPtr<UVCCharacterAppearance> Blocks = Named(TEXT("/Game/Test/DA_Blocks.DA_Blocks"));
	const TSoftObjectPtr<UVCCharacterAppearance> Smooth = Named(TEXT("/Game/Test/DA_Smooth.DA_Smooth"));
	const TSoftObjectPtr<UVCCharacterAppearance> Default = Named(TEXT("/Game/Test/DA_Default.DA_Default"));
	TMap<EMeshingMode, TSoftObjectPtr<UVCCharacterAppearance>> ByMeshing;
	ByMeshing.Add(EMeshingMode::Cubic, Blocks);
	ByMeshing.Add(EMeshingMode::MarchingCubes, Smooth);
	ByMeshing.Add(EMeshingMode::DualContouring, None); // a mode the map names without an appearance

	const auto Pick = [&](const TSoftObjectPtr<UVCCharacterAppearance>& InChosen, const TSoftObjectPtr<UVCCharacterAppearance>& InMode,
		bool bHasWorld, EMeshingMode Meshing)
	{
		return UVCAppearanceComponent::PickAppearance(InChosen, InMode, ByMeshing, Default, bHasWorld, Meshing);
	};
	TestTrue(TEXT("The player's choice comes first"), Pick(Chosen, ForMode, true, EMeshingMode::Cubic) == Chosen);
	TestTrue(TEXT("Then the game mode's"), Pick(None, ForMode, true, EMeshingMode::Cubic) == ForMode);
	TestTrue(TEXT("Then the cubic world's"), Pick(None, None, true, EMeshingMode::Cubic) == Blocks);
	TestTrue(TEXT("A smooth world's"), Pick(None, None, true, EMeshingMode::MarchingCubes) == Smooth);
	TestTrue(TEXT("A mode named without an appearance: the default"), Pick(None, None, true, EMeshingMode::DualContouring) == Default);
	TestTrue(TEXT("No voxel world: the default"), Pick(None, None, false, EMeshingMode::Cubic) == Default);
	TestTrue(TEXT("Nothing named anywhere: null, the class's own body"),
		UVCAppearanceComponent::PickAppearance(None, None, {}, None, true, EMeshingMode::Cubic).IsNull());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAppearance_SaveRoundTrip, "VoxelCharacter.Appearance.SaveRoundTrip", VC_APPEARANCE_FLAGS)
bool FVCAppearance_SaveRoundTrip::RunTest(const FString& Parameters)
{
	FVCPlayerSaveState State;
	State.PlayerKey = TEXT("Local0");
	State.ChosenAppearance = FSoftObjectPath(TEXT("/Game/CharacterAnimation/Appearances/DA_VCAppearance_Kellan.DA_VCAppearance_Kellan"));
	FString Json;
	TestTrue(TEXT("Serialize"), FJsonObjectConverter::UStructToJsonObjectString(State, Json));
	FVCPlayerSaveState Back;
	TestTrue(TEXT("Parse"), FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Back));
	TestEqual(TEXT("The chosen appearance"), Back.ChosenAppearance.ToString(), State.ChosenAppearance.ToString());

	// A save from before P6 has no choice: the rule decides.
	FVCPlayerSaveState Old;
	TestTrue(TEXT("Parse a save without the field"), FJsonObjectConverter::JsonObjectStringToUStruct(TEXT("{\"PlayerKey\":\"Local0\"}"), &Old));
	TestTrue(TEXT("No choice"), Old.ChosenAppearance.IsNull());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAppearance_DressAndUndress, "VoxelCharacter.Appearance.DressAndUndress", VC_APPEARANCE_FLAGS)
bool FVCAppearance_DressAndUndress::RunTest(const FString& Parameters)
{
	USkeletalMesh* Cube = LoadObject<USkeletalMesh>(nullptr, TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
	if (!Cube)
	{
		AddWarning(TEXT("No /Engine/EngineMeshes/SkeletalCube to dress with"));
		return true;
	}
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("VCAppearanceWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	ACharacter* Character = World->SpawnActor<ACharacter>();
	UVCAppearanceComponent* Appearance = Character ? NewObject<UVCAppearanceComponent>(Character, TEXT("Appearance")) : nullptr;
	if (TestNotNull(TEXT("A character"), Character) && Appearance)
	{
		Appearance->RegisterComponent();
		USkeletalMesh* Own = Character->GetMesh()->GetSkeletalMeshAsset();
		UVCCharacterAppearance* Data = NewObject<UVCCharacterAppearance>();
		Data->BodyMesh = Cube;
		TestTrue(TEXT("Dressed"), Appearance->ApplyAppearance(Data));
		TestTrue(TEXT("The body is the appearance's"), Character->GetMesh()->GetSkeletalMeshAsset() == Cube);
		TestTrue(TEXT("It is worn"), Appearance->GetAppearance() == Data);
		TestTrue(TEXT("Its bones are the Mannequin's unless it says not"), Appearance->HasIKFootBones());
		Data->bHasIKFootBones = false;
		TestFalse(TEXT("A body without ik_foot bones says so"), Appearance->HasIKFootBones());
		TestTrue(TEXT("Undressed"), Appearance->ApplyAppearance(nullptr));
		TestTrue(TEXT("The class's own body is back"), Character->GetMesh()->GetSkeletalMeshAsset() == Own);
		TestTrue(TEXT("The class's own body has its bones"), Appearance->HasIKFootBones());
	}
	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAppearance_FootBoneIKMath, "VoxelCharacter.Appearance.FootBoneIKMath", VC_APPEARANCE_FLAGS)
bool FVCAppearance_FootBoneIKMath::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Level ground: no drop"), FVCAnimNode_FootBoneIK::PelvisOffsetFor({ 0.0, 0.0 }, 50.0), 0.0);
	TestEqual(TEXT("One foot lower: the pelvis drops to it"), FVCAnimNode_FootBoneIK::PelvisOffsetFor({ -12.0, 6.0 }, 50.0), -12.0);
	TestEqual(TEXT("Both higher: no rise"), FVCAnimNode_FootBoneIK::PelvisOffsetFor({ 5.0, 20.0 }, 50.0), 0.0);
	TestEqual(TEXT("A deep dip: the drop's limit"), FVCAnimNode_FootBoneIK::PelvisOffsetFor({ -80.0, 0.0 }, 50.0), -50.0);
	TestEqual(TEXT("No legs: no drop"), FVCAnimNode_FootBoneIK::PelvisOffsetFor({}, 50.0), 0.0);

	const FQuat Foot = FQuat(FVector::ZAxisVector, FMath::DegreesToRadians(30.0));
	const auto Tilted = [](const FQuat& Result, const FQuat& Pose) { return FMath::RadiansToDegrees((Result * Pose.Inverse()).GetAngle()); };
	const FVector Slope20 = FQuat(FVector::XAxisVector, FMath::DegreesToRadians(20.0)).RotateVector(FVector::UpVector);
	const FVector Slope45 = FQuat(FVector::XAxisVector, FMath::DegreesToRadians(45.0)).RotateVector(FVector::UpVector);
	TestTrue(TEXT("Level ground: the pose's rotation"), FVCAnimNode_FootBoneIK::TiltFoot(Foot, FVector::UpVector, 30.0, 1.0).Equals(Foot));
	TestEqual(TEXT("A 20-degree slope: tilted 20"), Tilted(FVCAnimNode_FootBoneIK::TiltFoot(Foot, Slope20, 30.0, 1.0), Foot), 20.0, 0.01);
	TestEqual(TEXT("A 45-degree slope: the tilt's limit"), Tilted(FVCAnimNode_FootBoneIK::TiltFoot(Foot, Slope45, 30.0, 1.0), Foot), 30.0, 0.01);
	TestEqual(TEXT("Half planted: half the tilt"), Tilted(FVCAnimNode_FootBoneIK::TiltFoot(Foot, Slope20, 30.0, 0.5), Foot), 10.0, 0.01);
	TestTrue(TEXT("In its swing: the pose's rotation"), FVCAnimNode_FootBoneIK::TiltFoot(Foot, Slope20, 30.0, 0.0).Equals(Foot));
	const FVector Tipped = FVCAnimNode_FootBoneIK::TiltFoot(FQuat::Identity, Slope20, 30.0, 1.0).RotateVector(FVector::UpVector);
	TestTrue(TEXT("The foot's up goes to the ground's normal"), Tipped.Equals(Slope20, 1e-4));
	return true;
}

#undef VC_APPEARANCE_FLAGS

#endif // WITH_AUTOMATION_TESTS
