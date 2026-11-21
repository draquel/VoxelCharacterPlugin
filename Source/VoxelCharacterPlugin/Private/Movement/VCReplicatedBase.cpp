// Copyright Daniel Raquel. All Rights Reserved.

#include "Movement/VCReplicatedBase.h"

#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/MovementBaseInterface.h"
#include "VoxelCharacterPlugin.h"

namespace
{
	TAutoConsoleVariable<int32> CVarClearUnresolvableBase(
		TEXT("vc.Net.ClearUnresolvableBase"),
		1,
		TEXT("1 (default): a character standing on a base no client can resolve (the voxel terrain's collision, which each\n")
		TEXT("machine cooks for itself) replicates no base to its simulated proxies, and the server's correction of a\n")
		TEXT("player's move names none (G221). 0: the engine's, the comparison: the server names the component, logs\n")
		TEXT("FNetGUIDCache::SupportsObject ... NOT Supported for it, and a client skips simulating the character between\n")
		TEXT("updates while it cannot resolve it."),
		ECVF_Default);
}

bool FVCReplicatedBase::ApplyBeforeReplication(const ACharacter& Character, FBasedMovementInfo& Info)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	const UObject* Base = Info.MovementBaseInterfaceData.PhysicsObjectOwner.Get();
	const UObject* Component = Info.MovementBase.Get();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	if (IsResolvableByClients(Base) && IsResolvableByClients(Component))
	{
		return false;
	}
	const bool bClear = IsEnabled();
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s stands on %s, which no client can resolve: %s"), *Character.GetName(),
		*GetFullNameSafe(Base ? Base : Component), bClear ? TEXT("replicated with no base") : TEXT("replicated as is (vc.Net.ClearUnresolvableBase 0)"));
	return bClear && ClearUnresolvableBase(Info);
}

bool FVCReplicatedBase::IsEnabled()
{
	return CVarClearUnresolvableBase.GetValueOnAnyThread() != 0;
}

bool FVCReplicatedBase::IsResolvableByClients(const UObject* Object)
{
	// FNetGUIDCache::SupportsObject's tests, without its cache of ids already handed out: an object it has given an id
	// is one it replicates, which IsSupportedForNetworking already says.
	return !Object || Object->IsFullNameStableForNetworking() || Object->IsSupportedForNetworking();
}

bool FVCReplicatedBase::ClearUnresolvableBase(FBasedMovementInfo& Info)
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// The engine fills both: the base's owner in the interface data, and the deprecated component pointer it still
	// replicates beside it. Either one unresolvable makes the server log the reference it cannot send.
	const UObject* Owner = Info.MovementBaseInterfaceData.PhysicsObjectOwner.Get();
	const UObject* Component = Info.MovementBase.Get();
	if (IsResolvableByClients(Owner) && IsResolvableByClients(Component))
	{
		return false;
	}
	Info.MovementBase = nullptr;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	Info.MovementBaseInterfaceData = FMovementBaseInterfaceData();
	Info.BoneName = NAME_None;
	Info.bRelativeRotation = false;
	Info.bServerHasBaseComponent = false;
	return true;
}
