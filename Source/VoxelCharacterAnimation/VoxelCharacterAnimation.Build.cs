// Copyright Daniel Raquel. All Rights Reserved.

using UnrealBuildTool;

/**
 * The player's motion-matching locomotion (character animation P5, D45): a gravity-frame trajectory predictor and the
 * anim instance Game Animation Sample's CMC AnimBP runs on. A module of its own, so VoxelCharacterPlugin's runtime
 * module and a project without motion matching carry none of PoseSearch.
 */
public class VoxelCharacterAnimation : ModuleRules
{
	public VoxelCharacterAnimation(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"PoseSearch",           // IPoseSearchTrajectoryPredictorInterface, UPoseSearchTrajectoryLibrary
			"VoxelCharacterPlugin", // UVCAnimInstance
		});
	}
}
