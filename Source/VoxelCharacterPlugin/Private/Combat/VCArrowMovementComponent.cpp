// Copyright Daniel Raquel. All Rights Reserved.

#include "Combat/VCArrowMovementComponent.h"
#include "Movement/VCMovementComponent.h"
#include "VoxelCharacterPlugin.h"
#include "VoxelChunkManager.h"
#include "VoxelCollisionManager.h"
#include "Components/SceneComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"

UVCArrowMovementComponent::UVCArrowMovementComponent()
{
	bRotationFollowsVelocity = true;
	bShouldBounce = false;
	bInitialVelocityInLocalSpace = false;
	InitialSpeed = 0.f;
	MaxSpeed = 0.f; // no cap: the launch sets the speed
	ProjectileGravityScale = 1.f;
	// The gravity direction is read once a substep: 1/60 s keeps it fresh and an arrow at the bow's 6000 uu/s within
	// about one voxel a step.
	bForceSubStepping = true;
	MaxSimulationTimeStep = 1.f / 60.f;
	MaxSimulationIterations = 8;
}

void UVCArrowMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();
	RefreshWorldBounds();
}

void UVCArrowMovementComponent::SetWorldBounds(const FVoxelWorldBounds& Bounds)
{
	CachedWorldBounds = Bounds;
	bHasWorldBounds = true;
}

void UVCArrowMovementComponent::RefreshWorldBounds()
{
	if (bHasWorldBounds)
	{
		return;
	}
	if (const UVoxelChunkManager* ChunkManager = UVoxelChunkManager::Find(GetWorld()))
	{
		SetWorldBounds(ChunkManager->GetWorldBounds());
	}
}

FVector UVCArrowMovementComponent::ComputeGravityAcceleration(const FVoxelWorldBounds& Bounds, const FVector& Location, float GravityZ)
{
	if (Bounds.Shape != EVoxelWorldBoundsShape::Shell)
	{
		return FVector(0.0, 0.0, GravityZ);
	}
	// The pawns' gravity function (one rule for bodies and arrows); GravityZ is negative for a pull, so its negation is
	// the pull's size toward the centre.
	const FVector Down = UVCMovementComponent::ComputePlanetGravityDirection(Location, Bounds.Center, FVector::DownVector);
	return Down * -GravityZ;
}

FVector UVCArrowMovementComponent::GetGravityAcceleration(const FVector& Location) const
{
	return ComputeGravityAcceleration(bHasWorldBounds ? CachedWorldBounds : FVoxelWorldBounds(), Location, GetGravityZ());
}

FVector UVCArrowMovementComponent::ComputeAcceleration(const FVector& InVelocity, float DeltaTime) const
{
	FVector Acceleration = Super::ComputeAcceleration(InVelocity, DeltaTime);
	if (HasPlanetGravity() && UpdatedComponent)
	{
		// The engine adds GetGravityZ along Z; on a planet that pull points at the centre from where the arrow is.
		Acceleration.Z -= GetGravityZ();
		Acceleration += GetGravityAcceleration(UpdatedComponent->GetComponentLocation());
	}
	return Acceleration;
}

bool UVCArrowMovementComponent::IsBelowSurface(const FVector& Location, const FVector& SurfacePoint, const FVector& Up)
{
	return FVector::DotProduct(Location - SurfacePoint, Up) < 0.0;
}

void UVCArrowMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	RefreshWorldBounds();
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// A stop (a hit, the arrow sticking) clears UpdatedComponent.
	if (UpdatedComponent)
	{
		if (const UVoxelChunkManager* ChunkManager = UVoxelChunkManager::Find(GetWorld()))
		{
			StopInUncookedGround(*ChunkManager);
		}
	}
}

bool UVCArrowMovementComponent::StopInUncookedGround(const UVoxelChunkManager& ChunkManager)
{
	const UVoxelCollisionManager* Collision = ChunkManager.GetCollisionManager();
	const FVector Location = UpdatedComponent->GetComponentLocation();
	if (!Collision || Collision->HasCollision(ChunkManager.WorldToChunkCoord(Location)))
	{
		return false; // cooked (solid, air or a body): the arrow's own sweep meets what is there
	}
	const FVector Surface = ChunkManager.GetGeneratedSurfacePoint(Location);
	const FVector Up = ChunkManager.GetUpVectorAt(Location);
	if (!IsBelowSurface(Location, Surface, Up))
	{
		return false;
	}

	// Into ground with no collision cooked: it ends at the surface, as a hit on the terrain would.
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s: past the cooked collision and below the surface at (%.0f, %.0f, %.0f): stops there"),
		*GetNameSafe(GetOwner()), Surface.X, Surface.Y, Surface.Z);
	UpdatedComponent->SetWorldLocation(Surface, /*bSweep*/ false, nullptr, ETeleportType::TeleportPhysics);
	FHitResult Hit;
	Hit.bBlockingHit = true;
	Hit.Time = 1.f;
	Hit.TraceStart = Location;
	Hit.TraceEnd = Surface;
	Hit.Location = Surface;
	Hit.ImpactPoint = Surface;
	Hit.Normal = Up;
	Hit.ImpactNormal = Up;
	StopSimulating(Hit);
	return true;
}
