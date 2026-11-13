// Copyright Daniel Raquel. All Rights Reserved.

#include "Effects/VCRagdollBuoyancy.h"
#include "Movement/VCTerrainQueryHelper.h"
#include "Components/MeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "PhysicsEngine/BodyInstance.h"
#include "VoxelChunkManager.h"
#include "VoxelWorldConfiguration.h"

static TAutoConsoleVariable<int32> CVarVCBuoyancyBodies(
	TEXT("vc.Buoyancy.Bodies"),
	1,
	TEXT("Dead bodies float: a ragdoll in water rises to the surface and rests there (1, the default); 0 = it falls through the water to the bed, as before the water plan's W2B. Read each tick on each machine."),
	ECVF_Default);

const FVCRagdollBuoyancy& FVCRagdollBuoyancy::Get()
{
	static const FVCRagdollBuoyancy Rules;
	return Rules;
}

bool FVCRagdollBuoyancy::IsEnabled()
{
	return CVarVCBuoyancyBodies.GetValueOnGameThread() != 0;
}

FVector FVCRagdollBuoyancy::ComputeAcceleration(double Depth, const FVector& Up, const FVector& Velocity, const FVector& ToAnchor, float Gravity) const
{
	if (Depth <= 0.0)
	{
		return FVector::ZeroVector;
	}
	// How much of the part the water holds: it grows over SubmergeDepth, so a body comes to rest awash (at
	// GetRestDepth) instead of bouncing on a hard surface.
	const double Wet = FMath::Clamp(Depth / FMath::Max(static_cast<double>(SubmergeDepth), 1.0), 0.0, 1.0);
	FVector Acceleration = Up * (static_cast<double>(Gravity) * Buoyancy * Wet);
	Acceleration -= Velocity * (static_cast<double>(LinearDrag) * Wet);
	Acceleration += (ToAnchor * static_cast<double>(AnchorPull)).GetClampedToMaxSize(MaxAnchorAcceleration) * Wet;
	return Acceleration;
}

bool FVCRagdollBuoyancy::Apply(USkeletalMeshComponent* Body, FVCRagdollBuoyancyState& State) const
{
	const bool bWasInWater = State.bInWater;
	const bool bInWater = ApplyForces(Body, State);
	if (bInWater != bWasInWater && Body && Body->GetNetMode() != NM_DedicatedServer)
	{
		SetCustomDepth(Body, State, bInWater);
	}
	return bInWater;
}

void FVCRagdollBuoyancy::SetCustomDepth(const USkeletalMeshComponent* Body, FVCRagdollBuoyancyState& State, bool bOn)
{
	if (!bOn)
	{
		for (const TWeakObjectPtr<UPrimitiveComponent>& Mesh : State.CustomDepthMeshes)
		{
			if (UPrimitiveComponent* Component = Mesh.Get())
			{
				Component->SetRenderCustomDepth(false);
			}
		}
		State.CustomDepthMeshes.Reset();
		return;
	}
	const AActor* Owner = Body->GetOwner();
	if (!Owner)
	{
		return;
	}
	// The character's own meshes and those of the actors attached to it (a MetaHuman's parts actor, a carried item),
	// as the swimmer's effects take them. A body lying just under the surface is not a shore.
	TArray<AActor*> Actors;
	Owner->GetAttachedActors(Actors, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/true);
	Actors.Add(const_cast<AActor*>(Owner));
	for (const AActor* Actor : Actors)
	{
		TInlineComponentArray<UMeshComponent*> Meshes(Actor);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (Mesh && Mesh->IsVisible() && !Mesh->bRenderCustomDepth)
			{
				Mesh->SetRenderCustomDepth(true);
				State.CustomDepthMeshes.Add(Mesh);
			}
		}
	}
}

bool FVCRagdollBuoyancy::ApplyForces(USkeletalMeshComponent* Body, FVCRagdollBuoyancyState& State) const
{
	State.bInWater = false;
	if (!Body || !Body->IsSimulatingPhysics() || !IsEnabled())
	{
		return false;
	}
	const UWorld* World = Body->GetWorld();
	const FBodyInstance* RootBody = Body->GetBodyInstance();
	if (!World || !RootBody)
	{
		return false;
	}

	// One sample for the body, at its root: the surface over it, the depth and the up. Each part's depth follows from
	// its height over the root (a body is short against a planet's curve).
	const FVector Root = RootBody->GetUnrealWorldTransform().GetLocation();
	FVCWaterSurfaceSample Water;
	if (!FVCTerrainQueryHelper::SampleWaterSurface(World, Root, ReachAbove, Water))
	{
		return false;
	}
	if (!Water.bWater && Water.Depth > 0.0)
	{
		// A root lying on the bed may read the bed's own voxel: look one voxel up before calling the place dry. A dry
		// cave below the sea's level is dry there too.
		const UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(World);
		const float VoxelSize = ChunkMgr && ChunkMgr->GetConfiguration() ? ChunkMgr->GetConfiguration()->VoxelSize : 100.0f;
		FVCWaterSurfaceSample Above;
		if (Water.Depth > VoxelSize && FVCTerrainQueryHelper::SampleWaterSurface(World, Root + Water.Up * VoxelSize, ReachAbove, Above))
		{
			Water.bWater = Above.bWater;
		}
	}
	if (!Water.bWater)
	{
		return false;
	}

	State.bInWater = true;
	State.RootDepth = Water.Depth;
	if (!State.bAnchored)
	{
		// The column the body met the water in: for one that died swimming, where it died, the same on every machine.
		State.bAnchored = true;
		State.Anchor = Root;
	}
	const FVector ToAnchor = FVector::VectorPlaneProject(State.Anchor - Root, Water.Up);
	const float Gravity = FMath::Abs(World->GetGravityZ());

	for (FBodyInstance* Part : Body->Bodies)
	{
		if (!Part || !Part->IsValidBodyInstance() || !Part->IsInstanceSimulatingPhysics())
		{
			continue;
		}
		const double Depth = Water.Depth + FVector::DotProduct(Root - Part->GetUnrealWorldTransform().GetLocation(), Water.Up);
		if (Depth <= 0.0)
		{
			continue;
		}
		Part->AddForce(ComputeAcceleration(Depth, Water.Up, Part->GetUnrealWorldVelocity(), ToAnchor, Gravity), /*bAllowSubstepping=*/true, /*bAccelChange=*/true);
		const double Wet = FMath::Clamp(Depth / FMath::Max(static_cast<double>(SubmergeDepth), 1.0), 0.0, 1.0);
		Part->AddTorqueInRadians(-Part->GetUnrealWorldAngularVelocityInRadians() * (static_cast<double>(AngularDrag) * Wet), /*bAllowSubstepping=*/true, /*bAccelChange=*/true);
	}
	return true;
}
