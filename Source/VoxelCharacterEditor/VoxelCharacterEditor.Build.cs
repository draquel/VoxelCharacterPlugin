// Copyright Daniel Raquel. All Rights Reserved.

using UnrealBuildTool;

/**
 * Editor-only helpers for the character's animation content (character animation P3): anim layers in an interface,
 * implementing an interface, adding and wiring graph nodes and setting their properties, so a script builds the
 * content. Type "Editor" in VoxelCharacterPlugin.uplugin: compiled only for editor targets, never cooked.
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
		});
	}
}
