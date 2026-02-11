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
				"CoreUObject",
				"Engine",
				"InputCore",
				"EnhancedInput",
				"GameplayAbilities",
				"GameplayTags",
				"GameplayTasks",
				"CommonGameFramework",
				"NetCore",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Slate",
				"SlateCore",
				"UMG",
				"VoxelCore",
				"VoxelStreaming",
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

		PublicDefinitions.Add("WITH_INVENTORY_PLUGIN=" + (bHasInventory ? "1" : "0"));
		PublicDefinitions.Add("WITH_INTERACTION_PLUGIN=" + (bHasInteraction ? "1" : "0"));
		PublicDefinitions.Add("WITH_EQUIPMENT_PLUGIN=" + (bHasEquipment ? "1" : "0"));

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
			PrivateDependencyModuleNames.Add("EquipmentGASIntegration");
		}
	}
}
