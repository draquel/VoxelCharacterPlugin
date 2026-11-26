// Copyright Daniel Raquel. All Rights Reserved.

using UnrealBuildTool;

/**
 * Editor-only helpers for the character's animation content (character animation P3): anim layers in an interface,
 * implementing an interface, adding and wiring graph nodes and setting their properties, so a script builds the
 * content. Type "Editor" in VoxelCharacterPlugin.uplugin: compiled only for editor targets, loaded only by the editor
 * (not by a -game or -server process of UnrealEditor.exe), never cooked. The graph node of FVCAnimNode_FootBoneIK (P6)
 * is in VoxelCharacterAnimGraph (UncookedOnly), which those processes load too (G284).
 */
public class VoxelCharacterEditor : ModuleRules
{
	public VoxelCharacterEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",       // FBlueprintEditorUtils
			"BlueprintGraph", // UK2Node, UEdGraphSchema_K2
			"AnimGraph",      // UAnimationGraph, UAnimationGraphSchema
			"AssetTools",     // CopyAssetsWithReferences (character animation P5)
			"Niagara",        // UVCEffectsAuthoringLibrary: the water effects' systems (the water plan's W1)
			"NiagaraEditor",  // FNiagaraEditorUtilities::AddEmitterToSystem
		});
	}
}
