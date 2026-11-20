// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Movement/VCReplicatedBase.h"
#include "Core/VCNPCCharacterBase.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Interfaces/MovementBaseInterface.h"
#include "UObject/Package.h"
#include "VoxelCollisionManager.h"

// ---------------------------------------------------------------------------
// A character's replicated movement base (G221): what a server sends a client about the base a character stands on is
// kept to objects a client can resolve. The terrain's collision component, which each machine cooks for itself, is
// cleared from it, with the server's word that it has a base; a component clients resolve (a default subobject, a
// replicated component) is kept as the engine filled it; `vc.Net.ClearUnresolvableBase 0` leaves the engine's.
// ---------------------------------------------------------------------------

namespace VCReplicatedBaseTestHelpers
{
	/** A replicated based movement as ACharacter::PreReplication fills it for a character standing on a component. */
	FBasedMovementInfo On(UPrimitiveComponent* Base)
	{
		FBasedMovementInfo Info;
		Info.MovementBaseInterfaceData.Set(Base);
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		Info.MovementBase = Base;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		Info.bServerHasBaseComponent = true;
		Info.bRelativeRotation = true;
		Info.BoneName = TEXT("Root");
		Info.Location = FVector(10.0, 20.0, 30.0);
		Info.Rotation = FRotator(0.f, 45.f, 0.f);
		return Info;
	}

	bool HasBase(const FBasedMovementInfo& Info)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return Info.MovementBaseInterfaceData.PhysicsObjectOwner.Get() != nullptr || Info.MovementBase != nullptr;
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}

#define VC_REPLICATEDBASE_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCReplicatedBase_TerrainCleared, "VoxelCharacter.Net.ReplicatedBase", VC_REPLICATEDBASE_FLAGS)
bool FVCReplicatedBase_TerrainCleared::RunTest(const FString& Parameters)
{
	using namespace VCReplicatedBaseTestHelpers;

	// The terrain's collision: made at runtime on a container no machine replicates, so it has no network id.
	UVoxelCollisionComponent* Terrain = NewObject<UVoxelCollisionComponent>(GetTransientPackage());
	TestFalse(TEXT("The terrain's collision component is not resolvable by clients"), FVCReplicatedBase::IsResolvableByClients(Terrain));
	TestTrue(TEXT("Null is resolvable"), FVCReplicatedBase::IsResolvableByClients(nullptr));

	FBasedMovementInfo OnTerrain = On(Terrain);
	TestTrue(TEXT("On the terrain: the base is cleared"), FVCReplicatedBase::ClearUnresolvableBase(OnTerrain));
	TestFalse(TEXT("On the terrain: no base is replicated"), HasBase(OnTerrain));
	TestFalse(TEXT("On the terrain: the server no longer says it has a base (a client simulates the character)"), OnTerrain.bServerHasBaseComponent != 0);
	TestFalse(TEXT("On the terrain: no relative rotation"), OnTerrain.HasRelativeRotation());
	TestEqual(TEXT("On the terrain: no bone"), OnTerrain.BoneName, FName(NAME_None));
	TestTrue(TEXT("On the terrain: the rotation is kept"), OnTerrain.Rotation.Equals(FRotator(0.f, 45.f, 0.f)));
	TestFalse(TEXT("Cleared once: nothing left to clear"), FVCReplicatedBase::ClearUnresolvableBase(OnTerrain));

	// A default subobject (a character's capsule) is named by its path on every machine.
	UCapsuleComponent* Capsule = AVCNPCCharacterBase::StaticClass()->GetDefaultObject<ACharacter>()->GetCapsuleComponent();
	TestTrue(TEXT("A default subobject is resolvable"), FVCReplicatedBase::IsResolvableByClients(Capsule));
	FBasedMovementInfo OnCapsule = On(Capsule);
	TestFalse(TEXT("On a default subobject: kept"), FVCReplicatedBase::ClearUnresolvableBase(OnCapsule));
	TestTrue(TEXT("On a default subobject: the base is still replicated"), HasBase(OnCapsule) && OnCapsule.bServerHasBaseComponent);
	TestEqual(TEXT("On a default subobject: the bone is kept"), OnCapsule.BoneName, FName(TEXT("Root")));

	// A component the server replicates gets an id the client resolves.
	UStaticMeshComponent* Replicated = NewObject<UStaticMeshComponent>(GetTransientPackage());
	Replicated->SetIsReplicated(true);
	TestTrue(TEXT("A replicated component is resolvable"), FVCReplicatedBase::IsResolvableByClients(Replicated));
	FBasedMovementInfo OnReplicated = On(Replicated);
	TestFalse(TEXT("On a replicated component: kept"), FVCReplicatedBase::ClearUnresolvableBase(OnReplicated));

	// The comparison: vc.Net.ClearUnresolvableBase 0 leaves the engine's.
	const ACharacter& Character = *AVCNPCCharacterBase::StaticClass()->GetDefaultObject<ACharacter>();
	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("vc.Net.ClearUnresolvableBase"));
	if (TestNotNull(TEXT("vc.Net.ClearUnresolvableBase exists"), CVar))
	{
		// At the priority it was last set with, at least code's: one typed at the console outranks code (G248).
		const EConsoleVariableFlags Priority = static_cast<EConsoleVariableFlags>(FMath::Max(static_cast<uint32>(CVar->GetFlags() & ECVF_SetByMask),
			static_cast<uint32>(ECVF_SetByCode)));
		const int32 Was = CVar->GetInt();
		CVar->Set(0, Priority);
		FBasedMovementInfo Off = On(Terrain);
		TestFalse(TEXT("With the cvar 0: the terrain's base is replicated as the engine filled it"), FVCReplicatedBase::ApplyBeforeReplication(Character, Off));
		TestTrue(TEXT("With the cvar 0: the base is still there"), HasBase(Off) && Off.bServerHasBaseComponent);
		CVar->Set(1, Priority);
		FBasedMovementInfo Onn = On(Terrain);
		TestTrue(TEXT("With the cvar 1: the terrain's base is cleared"), FVCReplicatedBase::ApplyBeforeReplication(Character, Onn) && !HasBase(Onn));
		FBasedMovementInfo Kept = On(Capsule);
		TestFalse(TEXT("With the cvar 1: a resolvable base is kept"), FVCReplicatedBase::ApplyBeforeReplication(Character, Kept) || !HasBase(Kept));
		CVar->Set(Was, Priority);
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
