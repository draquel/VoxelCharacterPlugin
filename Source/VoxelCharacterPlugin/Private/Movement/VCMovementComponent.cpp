// Copyright Daniel Raquel. All Rights Reserved.

#include "Movement/VCMovementComponent.h"
#include "Movement/VCVoxelNavigationHelper.h"
#include "VoxelChunkManager.h"
#include "VoxelEditManager.h"
#include "VoxelEditTypes.h"
#include "VoxelCharacterPlugin.h"
#include "GameplayEffectTypes.h"

UVCMovementComponent::UVCMovementComponent()
{
	SetIsReplicatedByDefault(true);

	// Third-person defaults: character faces movement direction
	bOrientRotationToMovement = true;
	RotationRate = FRotator(0.f, 500.f, 0.f);
}

void UVCMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	BaseMaxWalkSpeed = MaxWalkSpeed;

	// Subscribe to chunk edit events for terrain cache invalidation
	if (UVoxelChunkManager* ChunkMgr = FVCVoxelNavigationHelper::FindChunkManager(GetWorld()))
	{
		if (UVoxelEditManager* EditMgr = ChunkMgr->GetEditManager())
		{
			EditMgr->OnChunkEdited.AddUObject(this, &UVCMovementComponent::OnVoxelChunkModified);
		}
	}
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void UVCMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// Track grounded state before movement update (for floor grace logic)
	bWasGroundedLastFrame = IsMovingOnGround();

	// Decay floor grace timer
	if (FloorGraceTimer > 0.f)
	{
		FloorGraceTimer -= DeltaTime;
	}

	// Refresh terrain cache periodically
	TerrainContextCacheTimer += DeltaTime;
	if (TerrainContextCacheTimer >= TerrainCacheDuration)
	{
		TerrainContextCacheTimer = 0.f;
		UpdateVoxelTerrainContext();
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

// ---------------------------------------------------------------------------
// Voxel Terrain Context
// ---------------------------------------------------------------------------

void UVCMovementComponent::UpdateVoxelTerrainContext()
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// Query voxel terrain at the character's feet position
	const FVector FeetPos = Owner->GetActorLocation() - FVector(0.f, 0.f, GetOwner()->GetSimpleCollisionHalfHeight());
	CachedTerrainContext = FVCVoxelNavigationHelper::QueryTerrainContext(GetWorld(), FeetPos);

	CurrentSurfaceType = CachedTerrainContext.SurfaceType;

	// Apply surface friction to ground friction
	GroundFriction = 8.f * CachedTerrainContext.FrictionMultiplier * VoxelSurfaceGripMultiplier;

	// --- Swimming mode transition ---
	if (CachedTerrainContext.bIsUnderwater && CachedTerrainContext.WaterDepth >= SwimmingEntryDepth)
	{
		if (!IsSwimming())
		{
			SetMovementMode(MOVE_Swimming);
			MaxSwimSpeed = BaseMaxWalkSpeed * VoxelSwimmingSpeedMultiplier * GASSpeedMultiplier;
		}
	}
	else if (IsSwimming() && CachedTerrainContext.WaterDepth < SwimmingExitDepth)
	{
		SetMovementMode(MOVE_Walking);
	}
}

// ---------------------------------------------------------------------------
// Floor Finding (async mesh rebuild tolerance)
// ---------------------------------------------------------------------------

void UVCMovementComponent::FindFloor(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult, bool bCanUseCachedLocation, const FHitResult* DownwardSweepResult) const
{
	Super::FindFloor(CapsuleLocation, OutFloorResult, bCanUseCachedLocation, DownwardSweepResult);

	// Real floor found — reset grace so it's available for the next gap
	if (OutFloorResult.bWalkableFloor)
	{
		bFloorGraceUsed = false;
		return;
	}

	// No floor found. If we were grounded last frame, this may be a transient
	// gap caused by async voxel mesh rebuilds. Grant a ONE-SHOT grace period.
	// bFloorGraceUsed prevents infinite re-triggering (must land on real floor to reset).
	if (bWasGroundedLastFrame && !bFloorGraceUsed && FloorGraceTimer <= 0.f)
	{
		FloorGraceTimer = FloorGraceDuration;
		bFloorGraceUsed = true;
	}

	if (FloorGraceTimer > 0.f)
	{
		// Synthesize a walkable floor result to keep the character grounded
		OutFloorResult.bWalkableFloor = true;
		OutFloorResult.bBlockingHit = true;
		OutFloorResult.FloorDist = 0.f;
	}
}

// ---------------------------------------------------------------------------
// Custom Physics (climbing placeholder)
// ---------------------------------------------------------------------------

void UVCMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	Super::PhysCustom(DeltaTime, Iterations);

	// Future: climbing mode implementation
}

// ---------------------------------------------------------------------------
// GAS Attribute Callback
// ---------------------------------------------------------------------------

void UVCMovementComponent::OnMoveSpeedAttributeChanged(const FOnAttributeChangeData& Data)
{
	GASSpeedMultiplier = FMath::Max(0.f, Data.NewValue);
	MaxWalkSpeed = BaseMaxWalkSpeed * GASSpeedMultiplier;
}

// ---------------------------------------------------------------------------
// Surface Type Mapping
// ---------------------------------------------------------------------------

EVoxelSurfaceType UVCMovementComponent::MaterialIDToSurfaceType(uint8 MaterialID)
{
	// Maps EVoxelMaterial IDs to logical surface types.
	// These constants match VoxelMaterialRegistry.h in VoxelCore.
	switch (MaterialID)
	{
	case 0:  return EVoxelSurfaceType::Grass;   // Grass
	case 1:  return EVoxelSurfaceType::Dirt;    // Dirt
	case 2:  return EVoxelSurfaceType::Stone;   // Stone
	case 3:  return EVoxelSurfaceType::Sand;    // Sand
	case 4:  return EVoxelSurfaceType::Snow;    // Snow
	case 5:  return EVoxelSurfaceType::Sand;    // Sandstone
	case 6:  return EVoxelSurfaceType::Ice;     // FrozenDirt
	case 10: return EVoxelSurfaceType::Stone;   // Coal
	case 11: return EVoxelSurfaceType::Metal;   // Iron
	case 12: return EVoxelSurfaceType::Metal;   // Gold
	case 13: return EVoxelSurfaceType::Metal;   // Copper
	case 14: return EVoxelSurfaceType::Stone;   // Diamond
	case 20: return EVoxelSurfaceType::Wood;    // Wood
	case 21: return EVoxelSurfaceType::Grass;   // Leaves
	default: return EVoxelSurfaceType::Default;
	}
}

float UVCMovementComponent::GetSurfaceFriction(EVoxelSurfaceType Surface)
{
	switch (Surface)
	{
	case EVoxelSurfaceType::Ice:   return 0.2f;
	case EVoxelSurfaceType::Mud:   return 0.6f;
	case EVoxelSurfaceType::Sand:  return 0.8f;
	case EVoxelSurfaceType::Snow:  return 0.7f;
	case EVoxelSurfaceType::Grass: return 1.0f;
	case EVoxelSurfaceType::Dirt:  return 0.9f;
	case EVoxelSurfaceType::Stone: return 1.0f;
	case EVoxelSurfaceType::Wood:  return 1.0f;
	case EVoxelSurfaceType::Metal: return 0.9f;
	case EVoxelSurfaceType::Water: return 0.5f;
	default:                       return 1.0f;
	}
}

// ---------------------------------------------------------------------------
// Chunk Modification Handler
// ---------------------------------------------------------------------------

void UVCMovementComponent::OnVoxelChunkModified(const FIntVector& ChunkCoord, EEditSource /*Source*/, const FVector& /*EditCenter*/, float /*EditRadius*/)
{
	// If the modified chunk is the one we're standing on, invalidate cache immediately
	if (ChunkCoord == CachedTerrainContext.CurrentChunkCoord)
	{
		TerrainContextCacheTimer = TerrainCacheDuration; // Force refresh next tick
	}
}
