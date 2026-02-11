#include "VoxelCharacterPlugin.h"

DEFINE_LOG_CATEGORY(LogVoxelCharacter);

#define LOCTEXT_NAMESPACE "FVoxelCharacterPluginModule"

void FVoxelCharacterPluginModule::StartupModule()
{
}

void FVoxelCharacterPluginModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVoxelCharacterPluginModule, VoxelCharacterPlugin)
