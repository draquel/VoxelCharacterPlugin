// Copyright Daniel Raquel. All Rights Reserved.

using UnrealBuildTool;

/**
 * The anim graph's nodes of the plugin's runtime anim nodes: UVCAnimGraphNode_FootBoneIK, the graph node of
 * FVCAnimNode_FootBoneIK (character animation P6). Type "UncookedOnly" in VoxelCharacterPlugin.uplugin (G284), as the
 * engine's animation plugins have theirs (AnimationWarping's editor module): loaded by every process that loads
 * uncooked content, the editor and also a -game or -server process of UnrealEditor.exe, which compile an animation
 * Blueprint as they load it and need its graph's nodes to do so (in the Editor module they were missing there, so the
 * graphs compiled without the node and the bodies stood in their reference pose); never built for a cooked target, which
 * loads the compiled class and no graph. It depends on the anim graph's editor-time modules (AnimGraph, BlueprintGraph)
 * and on no editor-only module (UnrealEd, Kismet, the editors): the authoring libraries that need them stay in
 * VoxelCharacterEditor.
 */
public class VoxelCharacterAnimGraph : ModuleRules
{
	public VoxelCharacterAnimGraph(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AnimGraph",            // UAnimGraphNode_SkeletalControlBase
			"AnimGraphRuntime",     // FAnimNode_SkeletalControlBase
			"BlueprintGraph",       // UK2Node, the graph node's bases
			"VoxelCharacterPlugin", // FVCAnimNode_FootBoneIK, the runtime node the graph node wraps
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects", // IPluginManager: the module's own test reads the plugin's descriptor
		});
	}
}
