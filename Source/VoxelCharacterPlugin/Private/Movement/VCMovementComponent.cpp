// Copyright Daniel Raquel. All Rights Reserved.

#include "Movement/VCMovementComponent.h"
#include "Movement/VCVoxelNavigationHelper.h"
#include "GameFramework/Character.h"
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

	// --- Voxel terrain movement defaults ---
	// These compensate for trimesh collision characteristics:
	// - Triangle edges at voxel boundaries create small geometric lips
	// - Double-sided trimesh normals can be slightly off at seams
	// - Cubic mode has full-voxel-height steps (VoxelSize, default 100)
	// - LOD 1 collision produces coarser geometry with larger lips
	MaxStepHeight = 50.f;   // Handles trimesh edge artifacts; full voxel steps require jumping
	SetWalkableFloorAngle(55.f);
	bUseFlatBaseForFloorChecks = true;
	bMaintainHorizontalGroundVelocity = true;
	bAlwaysCheckFloor = true;  // Force floor checks every frame (no caching)
	PerchRadiusThreshold = 0.f;
	PerchAdditionalHeight = 0.f;

	// --- Braking / friction defaults for voxel terrain ---
	// Default BrakingDecelerationWalking (2048) is too low — character slides.
	// Combined with a dedicated braking friction, this gives snappy stops on terrain.
	BrakingDecelerationWalking = 4096.f;
	BrakingFrictionFactor = 3.f;
	bUseSeparateBrakingFriction = true;
	BrakingFriction = 1.f;

	// --- Swimming defaults ---
	BrakingDecelerationSwimming = 600.f;
	Buoyancy = 1.0f;
	NavAgentProps.bCanSwim = true;
}

void UVCMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	BaseMaxWalkSpeed = MaxWalkSpeed;
	BaseMaxAcceleration = MaxAcceleration;

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

	// Track how long since a real floor was found (incremented each frame,
	// reset to 0 in FindFloor when actual floor detected)
	TimeSinceLastRealFloor += DeltaTime;

	// Refresh terrain cache periodically
	TerrainContextCacheTimer += DeltaTime;
	if (TerrainContextCacheTimer >= TerrainCacheDuration)
	{
		TerrainContextCacheTimer = 0.f;
		UpdateVoxelTerrainContext();
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// UE's PhysSwimming checks GetPhysicsVolume()->bWaterVolume and exits to
	// MOVE_Falling when no water volume is found. We use voxel water flags instead
	// of physics volumes, so re-enter swimming if UE kicked us out while still underwater.
	if (!IsSwimming() && CachedTerrainContext.bIsUnderwater
		&& CachedTerrainContext.WaterDepth >= SwimmingExitDepth)
	{
		SetMovementMode(MOVE_Swimming);
	}

	// --- Dive controls: vertical movement while swimming ---
	// Jump = ascend, Crouch = descend. Applied as direct velocity modification
	// post-Super so it doesn't interfere with UE's swimming physics.
	if (IsSwimming() && CharacterOwner)
	{
		const bool bWantsAscend = CharacterOwner->bPressedJump;
		const bool bWantsDescent = CharacterOwner->bIsCrouched || IsCrouching();

		if (bWantsAscend)
		{
			Velocity.Z += DiveAscendAcceleration * DeltaTime;
		}
		else if (bWantsDescent)
		{
			// Check if the voxel below the character is solid — if so, stop descending
			// to prevent sinking through the ocean floor.
			const float HalfHeight = CharacterOwner->GetSimpleCollisionHalfHeight();
			const FVector FeetPos = CharacterOwner->GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
			FHitResult GroundHit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(DiveFloorCheck), false, CharacterOwner);
			const bool bHitFloor = GetWorld()->LineTraceSingleByChannel(
				GroundHit, FeetPos, FeetPos - FVector(0.f, 0.f, 50.f),
				ECC_WorldStatic, Params);

			if (!bHitFloor)
			{
				Velocity.Z -= DiveDescendAcceleration * DeltaTime;
			}
		}

		// Clamp vertical speed to swim speed limits
		const float MaxVerticalSpeed = MaxSwimSpeed;
		Velocity.Z = FMath::Clamp(Velocity.Z, -MaxVerticalSpeed, MaxVerticalSpeed);
	}
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
	const float HalfHeight = Owner->GetSimpleCollisionHalfHeight();
	const FVector FeetPos = Owner->GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
	CachedTerrainContext = FVCVoxelNavigationHelper::QueryTerrainContext(GetWorld(), FeetPos);

	// If feet-level water check missed (feet on solid ocean floor), check at body center.
	// When standing on the seabed, feet are in a solid voxel (no water flag) but the
	// character's body is submerged in water-flagged air voxels above.
	if (!CachedTerrainContext.bIsUnderwater)
	{
		float BodyWaterDepth = 0.f;
		if (FVCVoxelNavigationHelper::IsPositionUnderwater(GetWorld(), Owner->GetActorLocation(), BodyWaterDepth))
		{
			CachedTerrainContext.bIsUnderwater = true;
			CachedTerrainContext.WaterDepth = BodyWaterDepth;
		}
	}

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
			Buoyancy = SwimmingBuoyancy;
			BrakingDecelerationSwimming = SwimmingBrakingDeceleration;
			MaxAcceleration = SwimmingMaxAcceleration;
		}
	}
	else if (IsSwimming() && CachedTerrainContext.WaterDepth < SwimmingExitDepth)
	{
		SetMovementMode(MOVE_Walking);
		MaxAcceleration = BaseMaxAcceleration;
	}
}

// ---------------------------------------------------------------------------
// Floor Finding (async mesh rebuild tolerance)
// ---------------------------------------------------------------------------

void UVCMovementComponent::FindFloor(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult, bool bCanUseCachedLocation, const FHitResult* DownwardSweepResult) const
{
	Super::FindFloor(CapsuleLocation, OutFloorResult, bCanUseCachedLocation, DownwardSweepResult);

	// --- Handle inverted normals from double-sided voxel trimesh collision ---
	// With bDoubleSidedGeometry = true, the Chaos trimesh returns the raw face
	// normal from triangle winding. If the mesher's winding convention differs
	// from Chaos's expectation, top-surface normals point downward (ImpactNormal.Z < 0).
	// The floor IS blocking but Super::FindFloor marks it unwalkable. Fix by
	// flipping the normal and re-evaluating walkability.
	if (!OutFloorResult.bWalkableFloor && OutFloorResult.bBlockingHit)
	{
		FHitResult& Hit = OutFloorResult.HitResult;
		if (Hit.ImpactNormal.Z < -UE_KINDA_SMALL_NUMBER)
		{
			Hit.ImpactNormal = -Hit.ImpactNormal;
			Hit.Normal = -Hit.Normal;
			OutFloorResult.bWalkableFloor = IsWalkable(Hit);
		}
	}

	// --- Line trace fallback for trimesh edge normal artifacts ---
	// Capsule sweeps against trimesh collision can return normals perpendicular to
	// triangle edges rather than face normals. On slopes, these edge normals can be
	// nearly horizontal, causing IsWalkable() to fail. A line trace hits the triangle
	// face directly, returning the correct face normal.
	if (!OutFloorResult.bWalkableFloor && OutFloorResult.bBlockingHit)
	{
		const UPrimitiveComponent* CapsulePrimitive = Cast<UPrimitiveComponent>(UpdatedComponent);
		if (CapsulePrimitive)
		{
			const ACharacter* Owner = CharacterOwner.Get();
			const float CapsuleHalfHeight = Owner ? Owner->GetSimpleCollisionHalfHeight() : 0.f;
			const FVector TraceStart = CapsuleLocation;
			// Trace down past capsule bottom + generous margin for floor detection
			constexpr float FloorTraceMargin = 50.f;
			const FVector TraceEnd = CapsuleLocation - FVector(0.f, 0.f, CapsuleHalfHeight + FloorTraceMargin);

			FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(VoxelFloorLineTrace), false, Owner);
			FHitResult LineHit;

			if (GetWorld()->LineTraceSingleByChannel(LineHit, TraceStart, TraceEnd, CapsulePrimitive->GetCollisionObjectType(), QueryParams))
			{
				// Fix inverted normals on the line trace result too
				if (LineHit.ImpactNormal.Z < -UE_KINDA_SMALL_NUMBER)
				{
					LineHit.ImpactNormal = -LineHit.ImpactNormal;
					LineHit.Normal = -LineHit.Normal;
				}

				if (IsWalkable(LineHit))
				{
					UE_LOG(LogVoxelCharacter, Verbose, TEXT("VoxelFloorLineTrace: Edge normal corrected. Sweep=(%.2f, %.2f, %.2f) LineTrace=(%.2f, %.2f, %.2f)"),
						OutFloorResult.HitResult.ImpactNormal.X, OutFloorResult.HitResult.ImpactNormal.Y, OutFloorResult.HitResult.ImpactNormal.Z,
						LineHit.ImpactNormal.X, LineHit.ImpactNormal.Y, LineHit.ImpactNormal.Z);

					// Override the floor result with the corrected face normal
					OutFloorResult.HitResult.ImpactNormal = LineHit.ImpactNormal;
					OutFloorResult.HitResult.Normal = LineHit.Normal;
					OutFloorResult.bWalkableFloor = true;
				}
			}
		}
	}

	// Real floor found — reset the "recently grounded" timer
	if (OutFloorResult.bWalkableFloor)
	{
		TimeSinceLastRealFloor = 0.f;
		return;
	}

	// No floor found. Only grant grace for SHALLOW gaps (trimesh collision artifacts),
	// not for real ledges. Check how far below the nearest floor is — if it's a big
	// drop (e.g., a cubic voxel step-down), let the character fall naturally instead
	// of synthesizing floor and teleporting down later.
	if (TimeSinceLastRealFloor < RecentGroundedWindow && FloorGraceTimer <= 0.f)
	{
		bool bShouldGrantGrace = true;

		// Trace down to see if there's floor within the grace height threshold.
		// If the nearest floor is too far below, this is a real ledge — don't grant grace.
		const ACharacter* Owner = CharacterOwner.Get();
		if (Owner)
		{
			const float CapsuleHalfHeight = Owner->GetSimpleCollisionHalfHeight();
			const FVector TraceStart = CapsuleLocation - FVector(0.f, 0.f, CapsuleHalfHeight);
			const FVector TraceEnd = TraceStart - FVector(0.f, 0.f, GraceHeightThreshold);

			FCollisionQueryParams Params(SCENE_QUERY_STAT(VoxelGraceCheck), false, Owner);
			FHitResult GraceHit;
			if (!GetWorld()->LineTraceSingleByChannel(GraceHit, TraceStart, TraceEnd,
				UpdatedComponent->GetCollisionObjectType(), Params))
			{
				// No floor within threshold — real ledge, let character fall
				bShouldGrantGrace = false;
			}
		}

		if (bShouldGrantGrace)
		{
			FloorGraceTimer = FloorGraceDuration;
		}
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
// Landing Spot Validation (inverted normal fix for voxel trimesh)
// ---------------------------------------------------------------------------

bool UVCMovementComponent::IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit) const
{
	if (!Hit.bBlockingHit)
	{
		return Super::IsValidLandingSpot(CapsuleLocation, Hit);
	}

	// Fix inverted normals from double-sided voxel trimesh.
	// The base class rejects hits with ImpactNormal.Z < 0 before FindFloor
	// gets a chance to correct them, causing the character to slide after jumps.
	if (!Hit.bStartPenetrating && Hit.ImpactNormal.Z < -UE_KINDA_SMALL_NUMBER)
	{
		FHitResult FixedHit = Hit;
		FixedHit.ImpactNormal = -FixedHit.ImpactNormal;
		FixedHit.Normal = -FixedHit.Normal;
		return Super::IsValidLandingSpot(CapsuleLocation, FixedHit);
	}

	// Fix edge normals: capsule sweep may return a nearly-horizontal edge normal
	// instead of the face normal. Line trace to get the actual surface normal.
	if (!Hit.bStartPenetrating && !IsWalkable(Hit))
	{
		const float CapsuleHalfHeight = CharacterOwner ? CharacterOwner->GetSimpleCollisionHalfHeight() : 0.f;
		const FVector TraceStart = CapsuleLocation;
		const FVector TraceEnd = CapsuleLocation - FVector(0.f, 0.f, CapsuleHalfHeight + 50.f);

		FCollisionQueryParams Params(SCENE_QUERY_STAT(VoxelLandingTrace), false, CharacterOwner);
		FHitResult LineHit;

		if (GetWorld()->LineTraceSingleByChannel(LineHit, TraceStart, TraceEnd,
			UpdatedComponent->GetCollisionObjectType(), Params))
		{
			// Fix inverted normals on line trace too
			if (LineHit.ImpactNormal.Z < -UE_KINDA_SMALL_NUMBER)
			{
				LineHit.ImpactNormal = -LineHit.ImpactNormal;
				LineHit.Normal = -LineHit.Normal;
			}

			if (IsWalkable(LineHit))
			{
				FHitResult FixedHit = Hit;
				FixedHit.ImpactNormal = LineHit.ImpactNormal;
				FixedHit.Normal = LineHit.Normal;
				return Super::IsValidLandingSpot(CapsuleLocation, FixedHit);
			}
		}
	}

	return Super::IsValidLandingSpot(CapsuleLocation, Hit);
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
// Immersion Depth (voxel-based water detection)
// ---------------------------------------------------------------------------

float UVCMovementComponent::ImmersionDepth() const
{
	// UE's default ImmersionDepth() uses physics water volumes, which we don't have.
	// Instead, use the voxel terrain context water depth to compute immersion ratio.
	if (!CachedTerrainContext.bIsUnderwater || !CharacterOwner)
	{
		return 0.f;
	}

	// Map water depth (world units) to 0-1 immersion ratio based on capsule height
	const float CapsuleHeight = CharacterOwner->GetSimpleCollisionHalfHeight() * 2.f;
	if (CapsuleHeight <= 0.f)
	{
		return 0.f;
	}

	return FMath::Clamp(CachedTerrainContext.WaterDepth / CapsuleHeight, 0.f, 1.f);
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
