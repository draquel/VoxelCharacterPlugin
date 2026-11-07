using UnrealBuildTool;
using System.IO;

public class VoxelCharacterPlugin : ModuleRules
{
	public VoxelCharacterPlugin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"DeveloperSettings", // UVCSettings
				"CoreUObject",
				"Engine",
				"InputCore",
				"EnhancedInput",
				"GameplayAbilities",
				"GameplayTags",
				"CommonGameFramework",
				"AIModule",
				"VoxelCore", // FVoxelWorldBounds in UVCMovementComponent's header (world edge push-back)
				"AnimGraphRuntime", // FAnimNode_SkeletalControlBase: FVCAnimNode_FootBoneIK (character animation P6)
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
			// Linked, not included (CR1, VCP-29): FReply in the world map widget (SlateCore, Slate), FUniqueNetIdWrapper
			// (CoreOnline), and the tests' box world constructing the distance-band strategy and a renderer stub.
			"Slate",
			"SlateCore",
			"CoreOnline",
			"VoxelLOD",
			"VoxelRendering",
				"UMG",
				"VoxelStreaming",
				"VoxelGeneration", // IVoxelWorldMode.h (VCTerrainQueryHelper.cpp)
				"PhysicsCore", // CollisionShape.h
				"GameplayTasks", // UAbilityTask_WaitDelay: the melee hit waits for its montage (character animation P3)
				"AnimationCore", // AnimationCore::SolveTwoBoneIK: FVCAnimNode_FootBoneIK (character animation P6)
				"VoxelNavigation", // UVCVoxelPathProvider (feature 10)
				"VoxelScatter",
				"JsonUtilities",
				"VoxelMap",
				"Json",
			}
		);

		// -----------------------------------------------------------------
		// Optional gameplay plugins — detected by directory presence.
		// Each plugin defines a WITH_*_PLUGIN macro (1 or 0) so C++ code
		// can conditionally compile integration code.
		// -----------------------------------------------------------------

		string PluginsRoot = Path.GetFullPath(Path.Combine(PluginDirectory, ".."));

		bool bHasInventory = Directory.Exists(Path.Combine(PluginsRoot, "ItemInventoryPlugin"));
		bool bHasInteraction = Directory.Exists(Path.Combine(PluginsRoot, "InteractionPlugin"));
		bool bHasEquipment = Directory.Exists(Path.Combine(PluginsRoot, "EquipmentPlugin"));
		// The weather (the weather plan's P4): the wind's push and the wet grip read UWorldWeatherSubsystem; without the
		// plugin the movement is as before.
		bool bHasWeather = Directory.Exists(Path.Combine(PluginsRoot, "WorldWeatherPlugin"));

		PublicDefinitions.Add("WITH_INVENTORY_PLUGIN=" + (bHasInventory ? "1" : "0"));
		PublicDefinitions.Add("WITH_INTERACTION_PLUGIN=" + (bHasInteraction ? "1" : "0"));
		PublicDefinitions.Add("WITH_EQUIPMENT_PLUGIN=" + (bHasEquipment ? "1" : "0"));
		PublicDefinitions.Add("WITH_WORLD_WEATHER=" + (bHasWeather ? "1" : "0"));

		if (bHasWeather)
		{
			PrivateDependencyModuleNames.Add("WorldWeather");
		}

		if (bHasInventory)
		{
			PrivateDependencyModuleNames.Add("ItemInventoryPlugin");
		}

		if (bHasInteraction)
		{
			PrivateDependencyModuleNames.Add("InteractionPlugin");
		}

		if (bHasEquipment)
		{
			PrivateDependencyModuleNames.Add("EquipmentPlugin");
		}
	}
}
