// Copyright Daniel Raquel. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/VCAnimNode_FootBoneIK.h"
#include "EdGraph/EdGraph.h"
#include "Interfaces/IPluginManager.h"
#include "UObject/CoreRedirects.h"
#include "VCAnimGraphNode_FootBoneIK.h"

// ---------------------------------------------------------------------------
// G284: the Foot Bone IK's graph node is in a module that every process loading uncooked content loads. In the Editor
// module VoxelCharacterEditor it was missing from a -game or -server process of UnrealEditor.exe, which compiles an
// animation Blueprint as it loads it: the graphs compiled without the node, and the bodies stood in their reference pose.
// What a test in the editor can hold: the module's type in the plugin's descriptor (UncookedOnly, so loaded with
// uncooked content and never built for a cooked target), the class under the module's path, the old path redirected to
// it, and the two animation Blueprints that hold the node finding it and compiling it into their classes. That a -game
// process now loads it and animates the bodies only a -game run shows (Scripts/qa/game_anim.py).
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnimGraphModuleTest,
	"VoxelCharacter.Config.AnimGraphNodeModule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVCAnimGraphModuleTest::RunTest(const FString& Parameters)
{
	// The descriptor: the graph node's module loads with uncooked content; the authoring libraries' only in the editor.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("VoxelCharacterPlugin"));
	if (!TestTrue(TEXT("VoxelCharacterPlugin is mounted"), Plugin.IsValid()))
	{
		return false;
	}
	const FModuleDescriptor* AnimGraph = Plugin->GetDescriptor().Modules.FindByPredicate(
		[](const FModuleDescriptor& Module) { return Module.Name == TEXT("VoxelCharacterAnimGraph"); });
	const FModuleDescriptor* Editor = Plugin->GetDescriptor().Modules.FindByPredicate(
		[](const FModuleDescriptor& Module) { return Module.Name == TEXT("VoxelCharacterEditor"); });
	if (TestNotNull(TEXT("the descriptor lists VoxelCharacterAnimGraph"), AnimGraph))
	{
		TestEqual(TEXT("VoxelCharacterAnimGraph is UncookedOnly"), static_cast<int32>(AnimGraph->Type),
			static_cast<int32>(EHostType::UncookedOnly));
	}
	if (TestNotNull(TEXT("the descriptor lists VoxelCharacterEditor"), Editor))
	{
		TestEqual(TEXT("VoxelCharacterEditor stays an Editor module"), static_cast<int32>(Editor->Type),
			static_cast<int32>(EHostType::Editor));
	}

	// The class under the new module's path, and the old path redirected to it.
	const FString NewPath = TEXT("/Script/VoxelCharacterAnimGraph.VCAnimGraphNode_FootBoneIK");
	TestEqual(TEXT("the graph node's class path"), UVCAnimGraphNode_FootBoneIK::StaticClass()->GetPathName(), NewPath);
	const FCoreRedirectObjectName Redirected = FCoreRedirects::GetRedirectedName(ECoreRedirectFlags::Type_Class,
		FCoreRedirectObjectName(TEXT("/Script/VoxelCharacterEditor.VCAnimGraphNode_FootBoneIK")));
	TestEqual(TEXT("the old path redirects to the new one"), Redirected.ToString(), NewPath);
	TestTrue(TEXT("the redirected name finds the class"),
		FindObject<UClass>(nullptr, *Redirected.ToString()) == UVCAnimGraphNode_FootBoneIK::StaticClass());

	// The animation Blueprints saved with the old path: each graph holds the node, and the class compiled from it holds
	// the runtime node (in a -game process before G284 it held none, and the graph's nodes before it were dropped).
	const TCHAR* Blueprints[] = {
		TEXT("/Game/PluginTesting/Blueprints/ABP_VoxelCharacter.ABP_VoxelCharacter"),
		TEXT("/Game/CharacterAnimation/Locomotion/ABP_VCPlayerLocomotion.ABP_VCPlayerLocomotion"),
	};
	for (const TCHAR* Path : Blueprints)
	{
		const UAnimBlueprint* Blueprint = LoadObject<UAnimBlueprint>(nullptr, Path);
		if (!TestNotNull(FString::Printf(TEXT("%s loads"), Path), Blueprint))
		{
			continue;
		}
		int32 GraphNodes = 0;
		for (const UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (!Graph)
			{
				continue;
			}
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				GraphNodes += Node && Node->IsA<UVCAnimGraphNode_FootBoneIK>() ? 1 : 0;
			}
		}
		TestTrue(FString::Printf(TEXT("%s: its anim graph holds a Foot Bone IK node (found %d)"), Path, GraphNodes), GraphNodes > 0);

		int32 RuntimeNodes = 0;
		if (const IAnimClassInterface* Class = IAnimClassInterface::GetFromClass(Blueprint->GeneratedClass))
		{
			for (const FStructProperty* Property : Class->GetAnimNodeProperties())
			{
				RuntimeNodes += Property && Property->Struct && Property->Struct->IsChildOf(FVCAnimNode_FootBoneIK::StaticStruct()) ? 1 : 0;
			}
		}
		TestTrue(FString::Printf(TEXT("%s: its compiled class holds FVCAnimNode_FootBoneIK (found %d)"), Path, RuntimeNodes), RuntimeNodes > 0);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
