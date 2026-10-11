// Copyright Daniel Raquel. All Rights Reserved.

#include "Movement/VCMovementComponent.h"
#include "Movement/VCVoxelNavigationHelper.h"
#include "GameFramework/Character.h"
#include "VoxelChunkManager.h"
#include "VoxelCollisionManager.h"
#include "VoxelWorldConfiguration.h"
#include "VoxelEditManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Core/VCCharacterAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Combat/VCCombatStatics.h"
#include "Tags/CGFGameplayTags.h"

namespace
{
	/** Saved move carrying the sprint and swim-rise keys, so a client's predicted moves replay the same way. */
	class FSavedMove_VC : public FSavedMove_Character
	{
	public:
		typedef FSavedMove_Character Super;

		uint8 bSavedWantsToSprint : 1;
		uint8 bSavedWantsToAscend : 1;

		virtual void Clear() override
		{
			Super::Clear();
			bSavedWantsToSprint = 0;
			bSavedWantsToAscend = 0;
		}

		virtual uint8 GetCompressedFlags() const override
		{
			uint8 Flags = Super::GetCompressedFlags();
			if (bSavedWantsToSprint)
			{
				Flags |= FLAG_Custom_0;
			}
			if (bSavedWantsToAscend)
			{
				Flags |= FLAG_Custom_1;
			}
			return Flags;
		}

		virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
		{
			const FSavedMove_VC* Other = static_cast<const FSavedMove_VC*>(NewMove.Get());
			if (bSavedWantsToSprint != Other->bSavedWantsToSprint || bSavedWantsToAscend != Other->bSavedWantsToAscend)
			{
				return false;
			}
			return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
		}

		virtual void SetMoveFor(ACharacter* InCharacter, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
		{
			Super::SetMoveFor(InCharacter, InDeltaTime, NewAccel, ClientData);
			if (const UVCMovementComponent* Movement = Cast<UVCMovementComponent>(InCharacter->GetCharacterMovement()))
			{
				bSavedWantsToSprint = Movement->bWantsToSprint;
				bSavedWantsToAscend = Movement->bWantsToAscend;
			}
		}

		virtual void PrepMoveFor(ACharacter* InCharacter) override
		{
			Super::PrepMoveFor(InCharacter);
			if (UVCMovementComponent* Movement = Cast<UVCMovementComponent>(InCharacter->GetCharacterMovement()))
			{
				Movement->bWantsToSprint = bSavedWantsToSprint;
				Movement->bWantsToAscend = bSavedWantsToAscend;
			}
		}
	};

	class FNetworkPredictionData_Client_VC : public FNetworkPredictionData_Client_Character
	{
	public:
		explicit FNetworkPredictionData_Client_VC(const UCharacterMovementComponent& ClientMovement)
			: FNetworkPredictionData_Client_Character(ClientMovement)
		{
		}

		virtual FSavedMovePtr AllocateNewMove() override
		{
			return FSavedMovePtr(new FSavedMove_VC());
		}
	};
}
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

	// --- Crouch (QA R10): a slow, low walk; later the hook for being harder to spot ---
	NavAgentProps.bCanCrouch = true;
	MaxWalkSpeedCrouched = 150.f;
	bCanWalkOffLedgesWhenCrouching = true;

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
	TryBindCollisionReady();
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

void UVCMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// Track grounded state before movement update (for floor grace logic)
	bWasGroundedLastFrame = IsMovingOnGround();

	// The voxel world can finish initialising after this pawn's BeginPlay: keep trying to bind, once a second.
	if (!bBoundCollisionReady)
	{
		CollisionBindRetryTimer -= DeltaTime;
		if (CollisionBindRetryTimer <= 0.f)
		{
			CollisionBindRetryTimer = 1.f;
			TryBindCollisionReady();
		}
	}

	// Sprint gate (every machine, from the replicated stamina): out of breath at zero until it refills a little.
	const float Stamina = GetOwnerStamina();
	if (Stamina >= 0.f)
	{
		if (Stamina <= 0.5f)
		{
			bSprintExhausted = true;
		}
		else if (Stamina >= SprintResumeStamina)
		{
			bSprintExhausted = false;
		}
	}
	if (CharacterOwner && CharacterOwner->HasAuthority())
	{
		TickStamina(DeltaTime);
		TickBreath(DeltaTime);
	}

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
}

// ---------------------------------------------------------------------------
// PhysSwimming — Custom 3D swimming physics (no physics water volume checks)
// ---------------------------------------------------------------------------

void UVCMovementComponent::PhysSwimming(float DeltaTime, int32 Iterations)
{
	if (DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

	// --- Restore pre-additive root motion velocity ---
	RestorePreAdditiveRootMotionVelocity();

	// --- Dive controls: Jump = ascend, Crouch = descend ---
	// Fed in as acceleration before CalcVelocity: with no movement input CalcVelocity brakes the swimmer to a stop
	// every frame, which ate a per-frame velocity nudge (QA R10: holding LeftCtrl sank about 5 uu/s). As acceleration
	// the fluid drag sets the dive speed instead (about DiveDescendAcceleration / SwimmingDragCoefficient). The dive
	// key counts as full input: CalcVelocity caps speed by the analog input scale, which is 0 with no movement keys.
	const bool bWantsAscend = CharacterOwner && (CharacterOwner->bPressedJump || bWantsToAscend);
	// A swimmer cannot crouch (CanCrouchInCurrentState is ground / air only), so the held key arrives as
	// bWantsToCrouch alone.
	const bool bWantsDescent = CharacterOwner && !bWantsAscend && (CharacterOwner->bIsCrouched || IsCrouching() || bWantsToCrouch);
	if (bWantsAscend)
	{
		Acceleration.Z += DiveAscendAcceleration;
		AnalogInputModifier = 1.f;
	}
	else if (bWantsDescent)
	{
		// Check if the ocean floor is directly below — don't descend into it
		const float HalfHeight = CharacterOwner->GetSimpleCollisionHalfHeight();
		const FVector FeetPos = CharacterOwner->GetActorLocation() - FVector(0.f, 0.f, HalfHeight);
		FHitResult GroundHit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DiveFloorCheck), false, CharacterOwner);
		const bool bHitFloor = GetWorld()->LineTraceSingleByChannel(
			GroundHit, FeetPos, FeetPos - FVector(0.f, 0.f, 50.f),
			ECC_WorldStatic, Params);

		if (!bHitFloor)
		{
			Acceleration.Z -= DiveDescendAcceleration;
			AnalogInputModifier = 1.f;
		}
	}

	// --- Calculate velocity with fluid drag ---
	// bFluid=true enables UE's fluid friction model (drag proportional to speed)
	CalcVelocity(DeltaTime, SwimmingDragCoefficient, true, BrakingDecelerationSwimming);

	// --- Apply root motion ---
	ApplyRootMotionToVelocity(DeltaTime);

	if (CharacterOwner)
	{

		// --- Passive buoyancy: gently push toward surface when idle ---
		// Only applies when no significant vertical input from pitch-based movement or dive keys.
		const bool bHasVerticalInput = bWantsAscend || bWantsDescent || FMath::Abs(Acceleration.Z) > 10.f;
		if (!bHasVerticalInput && SurfaceBuoyancyStrength > 0.f && CachedTerrainContext.bIsUnderwater)
		{
			// Scale buoyancy with depth: strongest deep, zero at surface
			const float DepthRatio = FMath::Clamp(CachedTerrainContext.WaterDepth / FMath::Max(SwimmingEntryDepth, 1.f), 0.f, 1.f);
			Velocity.Z += SurfaceBuoyancyStrength * DepthRatio * DeltaTime;
		}

		// --- Surface damping: decelerate upward motion near water surface ---
		if (CachedTerrainContext.WaterDepth < SwimmingEntryDepth && Velocity.Z > 0.f)
		{
			const float SurfaceRatio = FMath::Clamp(CachedTerrainContext.WaterDepth / FMath::Max(SwimmingEntryDepth, 1.f), 0.f, 1.f);
			Velocity.Z *= FMath::Lerp(SwimmingSurfaceDamping, 1.f, SurfaceRatio);
		}

		// --- Surface cap: hard-stop upward velocity at the water line ---
		// Prevents the character from breaching the surface and falling back in.
		// Shore exit is handled by horizontal movement into shallow water, which
		// triggers the mode transition in UpdateVoxelTerrainContext.
		const float HalfHeight = CharacterOwner->GetSimpleCollisionHalfHeight();
		if (CachedTerrainContext.WaterDepth < HalfHeight && Velocity.Z > 0.f)
		{
			Velocity.Z = 0.f;
		}
	}

	// --- Clamp vertical speed ---
	Velocity.Z = FMath::Clamp(Velocity.Z, -MaxSwimSpeed, MaxSwimSpeed);

	// --- Move and handle collisions ---
	Iterations++;
	bJustTeleported = false;

	FVector OldLocation = UpdatedComponent->GetComponentLocation();
	const FVector Adjusted = Velocity * DeltaTime;
	FHitResult Hit(1.f);
	const FQuat Rotation = UpdatedComponent->GetComponentQuat();

	SafeMoveUpdatedComponent(Adjusted, Rotation, true, Hit);

	if (Hit.bBlockingHit && UpdatedComponent)
	{
		// A bank at the surface: climb out instead of sliding along it (the stock step-up on wall hits is gone in
		// this custom swim mode).
		if (TryClimbOutOfWater(Hit))
		{
			return;
		}
		// Slide along collision surfaces (seabed, cave walls)
		SlideAlongSurface(Adjusted, (1.f - Hit.Time), Hit.Normal, Hit, true);
	}

	if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / DeltaTime;
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
	else if (IsSwimming() && (!CachedTerrainContext.bIsUnderwater || CachedTerrainContext.WaterDepth < SwimmingExitDepth))
	{
		// Anti-flicker guard for the seabed case: when bIsUnderwater goes false
		// (feet/body in solid voxel), verify with an upper-body water check before
		// exiting. Skip this when water IS detected but shallow (genuine shore exit).
		if (!CachedTerrainContext.bIsUnderwater)
		{
			const FVector UpperBodyPos = Owner->GetActorLocation() + FVector(0.f, 0.f, HalfHeight * 0.5f);
			float UpperBodyWaterDepth = 0.f;
			if (FVCVoxelNavigationHelper::IsPositionUnderwater(GetWorld(), UpperBodyPos, UpperBodyWaterDepth))
			{
				// Upper body still in water — seabed contact, stay swimming
				return;
			}
		}

		// Near the water surface or at shore — check for walkable floor
		FFindFloorResult FloorResult;
		FindFloor(UpdatedComponent->GetComponentLocation(), FloorResult, false);

		if (FloorResult.bWalkableFloor)
		{
			SetMovementMode(MOVE_Walking);
		}
		else
		{
			// No floor — let gravity pull back to water or down to ground
			SetMovementMode(MOVE_Falling);
		}
		MaxAcceleration = BaseMaxAcceleration;
	}
}

// ---------------------------------------------------------------------------
// Floor Finding (async mesh rebuild tolerance)
// ---------------------------------------------------------------------------

void UVCMovementComponent::FindFloor(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult, bool bCanUseCachedLocation, const FHitResult* DownwardSweepResult) const
{
	Super::FindFloor(CapsuleLocation, OutFloorResult, bCanUseCachedLocation, DownwardSweepResult);

	// The voxel collision trimesh used to be wound backwards, so top surfaces reported downward normals
	// and this override flipped them back. The collision manager now winds triangles the way Chaos
	// expects (VoxelCollisionManager.cpp), so a downward normal under the feet is a real underside.

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
// Landing Spot Validation (edge normal fix for voxel trimesh)
// ---------------------------------------------------------------------------

bool UVCMovementComponent::IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit) const
{
	if (!Hit.bBlockingHit)
	{
		return Super::IsValidLandingSpot(CapsuleLocation, Hit);
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

void UVCMovementComponent::OnVoxelChunkModified(const FIntVector& ChunkCoord, EEditSource /*Source*/, const FVector& EditCenter, float EditRadius)
{
	// If the modified chunk is the one we're standing on, invalidate cache immediately
	if (ChunkCoord == CachedTerrainContext.CurrentChunkCoord)
	{
		TerrainContextCacheTimer = TerrainCacheDuration; // Force refresh next tick
	}

	// An edit that reaches the pawn may have closed terrain around it. The pawn still stands on the OLD
	// collision until the chunk re-cooks, so arm a check that runs when the new collision lands.
	const ACharacter* Character = GetCharacterOwner();
	const UWorld* World = GetWorld();
	if (Character && World && Character->HasAuthority())
	{
		const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
		const float Reach = EditRadius + (Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 100.f) + 200.f;
		if (FVector::DistSquared(EditCenter, Character->GetActorLocation()) <= FMath::Square(Reach))
		{
			EmbedCheckArmedUntil = World->GetTimeSeconds() + 10.0;
		}
	}
}

void UVCMovementComponent::SetSprinting(bool bWantsSprint)
{
	bWantsToSprint = bWantsSprint;
}

void UVCMovementComponent::SetWantsToAscend(bool bWantsAscend)
{
	bWantsToAscend = bWantsAscend;
}

bool UVCMovementComponent::IsSprinting() const
{
	// Intent, not speed: acceleration is the input direction, so a sprint starts with the first step.
	return bWantsToSprint && !bSprintExhausted && IsMovingOnGround() && !IsCrouching()
		&& GetCurrentAcceleration().SizeSquared() > 1.f;
}

float UVCMovementComponent::GetMaxSpeed() const
{
	const float Speed = Super::GetMaxSpeed();
	return (MovementMode == MOVE_Walking && IsSprinting()) ? Speed * SprintSpeedMultiplier : Speed;
}

void UVCMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bWantsToAscend = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
}

FNetworkPredictionData_Client* UVCMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UVCMovementComponent* MutableThis = const_cast<UVCMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FNetworkPredictionData_Client_VC(*this);
	}
	return ClientPredictionData;
}

float UVCMovementComponent::GetOwnerStamina() const
{
	const IAbilitySystemInterface* WithASC = Cast<IAbilitySystemInterface>(GetOwner());
	const UAbilitySystemComponent* ASC = WithASC ? WithASC->GetAbilitySystemComponent() : nullptr;
	const FGameplayAttribute StaminaAttribute = UVCCharacterAttributeSet::GetStaminaAttribute();
	return (ASC && ASC->HasAttributeSetForAttribute(StaminaAttribute)) ? ASC->GetNumericAttribute(StaminaAttribute) : -1.f;
}

void UVCMovementComponent::TickStamina(float DeltaTime)
{
	const IAbilitySystemInterface* WithASC = Cast<IAbilitySystemInterface>(GetOwner());
	UAbilitySystemComponent* ASC = WithASC ? WithASC->GetAbilitySystemComponent() : nullptr;
	const FGameplayAttribute StaminaAttribute = UVCCharacterAttributeSet::GetStaminaAttribute();
	if (!ASC || !ASC->HasAttributeSetForAttribute(StaminaAttribute))
	{
		return;
	}

	// Spend only while the sprint actually carries the pawn: holding it against a wall or a ledge costs nothing.
	if (IsSprinting() && Velocity.SizeSquared2D() > FMath::Square(100.f))
	{
		TimeSinceSprint = 0.f;
		PendingStaminaDelta -= SprintStaminaPerSecond * DeltaTime;
	}
	else
	{
		TimeSinceSprint += DeltaTime;
		if (TimeSinceSprint >= StaminaRegenDelay)
		{
			PendingStaminaDelta += StaminaRegenPerSecond * DeltaTime;
		}
	}

	// Write the attribute ~10 times a second rather than every frame (it replicates on change).
	StaminaWriteTimer += DeltaTime;
	if (StaminaWriteTimer < 0.1f || FMath::IsNearlyZero(PendingStaminaDelta))
	{
		return;
	}
	StaminaWriteTimer = 0.f;
	const float Current = ASC->GetNumericAttribute(StaminaAttribute);
	const float Max = ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetMaxStaminaAttribute());
	const float Target = FMath::Clamp(Current + PendingStaminaDelta, 0.f, Max);
	PendingStaminaDelta = 0.f;
	if (!FMath::IsNearlyEqual(Target, Current))
	{
		ASC->ApplyModToAttribute(StaminaAttribute, EGameplayModOp::Override, Target);
	}
}

bool UVCMovementComponent::TryClimbOutOfWater(const FHitResult& Hit)
{
	ACharacter* Character = GetCharacterOwner();
	UWorld* World = GetWorld();
	if (!Character || !World || !UpdatedComponent)
	{
		return false;
	}
	const float HalfHeight = Character->GetSimpleCollisionHalfHeight();
	const float Radius = Character->GetSimpleCollisionRadius();

	// Only at the surface (head out or nearly), only into something steep, and only when trying to go that way.
	const FVector Push = Acceleration.GetSafeNormal2D();
	if (CachedTerrainContext.WaterDepth > HalfHeight * 1.5f || Hit.ImpactNormal.Z > 0.7f || Push.IsNearlyZero()
		|| FVector::DotProduct(Push, -Hit.ImpactNormal.GetSafeNormal2D()) < 0.3f)
	{
		return false;
	}

	// Look for a floor just past the bank edge, no higher than the reach.
	const FVector Location = UpdatedComponent->GetComponentLocation();
	const float FeetZ = Location.Z - HalfHeight;
	const float Reach = HalfHeight + ShoreClimbHeight;
	const FVector ProbeTop = Location + Push * (Radius + 25.f) + FVector(0.f, 0.f, Reach);
	const FVector ProbeBottom = ProbeTop - FVector(0.f, 0.f, Reach + HalfHeight);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShoreClimb), false, Character);
	FHitResult Floor;
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	if (!World->SweepSingleByChannel(Floor, ProbeTop, ProbeBottom, FQuat::Identity, UpdatedComponent->GetCollisionObjectType(),
		Capsule, Params) || Floor.bStartPenetrating || !IsWalkable(Floor))
	{
		return false;
	}

	// The ledge must lift the feet out of swimming depth (the water line is FeetZ + WaterDepth).
	const float LedgeFeetZ = Floor.Location.Z - HalfHeight;
	const float WaterLineZ = FeetZ + CachedTerrainContext.WaterDepth;
	if (LedgeFeetZ < WaterLineZ - SwimmingEntryDepth * 0.5f || LedgeFeetZ > FeetZ + Reach)
	{
		return false;
	}

	Character->SetActorLocation(Floor.Location + FVector(0.f, 0.f, 2.f), false, nullptr, ETeleportType::None);
	SetMovementMode(MOVE_Walking);
	MaxAcceleration = BaseMaxAcceleration;
	Velocity = Push * FMath::Min(Velocity.Size2D(), MaxWalkSpeed);
	return true;
}

void UVCMovementComponent::TickBreath(float DeltaTime)
{
	ACharacter* Character = GetCharacterOwner();
	const IAbilitySystemInterface* WithASC = Cast<IAbilitySystemInterface>(Character);
	UAbilitySystemComponent* ASC = WithASC ? WithASC->GetAbilitySystemComponent() : nullptr;
	const FGameplayAttribute BreathAttribute = UVCCharacterAttributeSet::GetBreathAttribute();
	// Players only for now: wildlife avoids water and has no way to come up for air.
	if (!Character || !Character->IsPlayerControlled() || !ASC || !ASC->HasAttributeSetForAttribute(BreathAttribute))
	{
		return;
	}

	// Sample ten times a second, like the terrain context (a voxel water query each frame is wasted work).
	BreathTimer += DeltaTime;
	if (BreathTimer < 0.1f)
	{
		return;
	}
	const float Elapsed = BreathTimer;
	BreathTimer = 0.f;

	const FVector Head = Character->GetActorLocation()
		+ FVector(0.f, 0.f, Character->GetSimpleCollisionHalfHeight() * HeadHeightFraction);
	float HeadDepth = 0.f;
	// The voxel holding the water line is flagged water top to bottom, so a head floating just above the line still
	// lands in a water voxel: it is under only when it is also below the water level.
	bHeadUnderwater = FVCVoxelNavigationHelper::IsPositionUnderwater(GetWorld(), Head, HeadDepth) && HeadDepth > 0.f;

	const float Current = ASC->GetNumericAttribute(BreathAttribute);
	const float Max = ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetMaxBreathAttribute());
	const float Target = FMath::Clamp(Current + (bHeadUnderwater ? -BreathLossPerSecond : BreathRegenPerSecond) * Elapsed, 0.f, Max);
	if (!FMath::IsNearlyEqual(Target, Current))
	{
		ASC->ApplyModToAttribute(BreathAttribute, EGameplayModOp::Override, Target);
	}

	// Out of air underwater: a Pure hit every second through the combat pipeline (respawn on death as usual).
	if (bHeadUnderwater && Target <= 0.f && DrowningDamagePerSecond > 0.f)
	{
		DrowningTimer += Elapsed;
		if (DrowningTimer >= 1.f)
		{
			DrowningTimer -= 1.f;
			const FCGFDamageContext Drowning = UVCCombatStatics::MakeDamageContext(nullptr, nullptr, DrowningDamagePerSecond,
				CGFGameplayTags::Damage_Type_Pure, /*bIgnoreFaction=*/true);
			UVCCombatStatics::ApplyDamageToActor(Character, Drowning);
		}
	}
	else
	{
		DrowningTimer = 0.f;
	}
}

void UVCMovementComponent::TryBindCollisionReady()
{
	if (bBoundCollisionReady)
	{
		return;
	}
	UVoxelChunkManager* ChunkMgr = FVCVoxelNavigationHelper::FindChunkManager(GetWorld());
	UVoxelCollisionManager* ColMgr = ChunkMgr ? ChunkMgr->GetCollisionManager() : nullptr;
	if (ColMgr)
	{
		ColMgr->OnCollisionReady.AddUObject(this, &UVCMovementComponent::OnVoxelCollisionReady);
		bBoundCollisionReady = true;
	}
}

void UVCMovementComponent::OnVoxelCollisionReady(const FIntVector& ChunkCoord)
{
	const UWorld* World = GetWorld();
	const ACharacter* Character = GetCharacterOwner();
	if (!World || !Character || !Character->HasAuthority() || EmbedCheckArmedUntil < 0.0)
	{
		return;
	}
	if (World->GetTimeSeconds() > EmbedCheckArmedUntil)
	{
		EmbedCheckArmedUntil = -1.0;
		return;
	}

	// Only the chunks the capsule can touch matter.
	UVoxelChunkManager* ChunkMgr = FVCVoxelNavigationHelper::FindChunkManager(World);
	if (!ChunkMgr)
	{
		return;
	}
	const FIntVector PawnChunk = ChunkMgr->WorldToChunkCoord(Character->GetActorLocation());
	const FIntVector Delta = ChunkCoord - PawnChunk;
	if (FMath::Abs(Delta.X) > 1 || FMath::Abs(Delta.Y) > 1 || FMath::Abs(Delta.Z) > 1)
	{
		return;
	}

	if (ResolveTerrainEmbedding())
	{
		EmbedCheckArmedUntil = -1.0;
	}
}

bool UVCMovementComponent::ResolveTerrainEmbedding()
{
	ACharacter* Character = GetCharacterOwner();
	UWorld* World = GetWorld();
	UVoxelChunkManager* ChunkMgr = World ? FVCVoxelNavigationHelper::FindChunkManager(World) : nullptr;
	const UVoxelWorldConfiguration* Config = ChunkMgr ? ChunkMgr->GetConfiguration() : nullptr;
	const UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!Character || !Character->HasAuthority() || !Config || !Capsule)
	{
		return false;
	}

	const float VoxelSize = Config->VoxelSize;
	const float HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const FVector Center = Character->GetActorLocation();

	// Nearest voxel sample (the lookup floors, so offset by half a voxel). Embedded means the capsule centre AND
	// its upper body sit in solid voxels: a pawn standing on a steep slope can have one sample in the slope
	// beside it, but never both.
	auto IsSolidAt = [ChunkMgr, VoxelSize](const FVector& P)
	{
		return ChunkMgr->GetEditMergedVoxelAtWorldPosition(P + FVector(VoxelSize * 0.5f)).IsSolid();
	};
	if (!IsSolidAt(Center) || !IsSolidAt(Center + FVector(0.f, 0.f, HalfHeight * 0.5f)))
	{
		return false;
	}

	// Scan up for the first run of air tall enough for the capsule.
	const int32 HeadroomSamples = FMath::CeilToInt((2.f * HalfHeight) / VoxelSize) + 1;
	int32 AirRun = 0;
	int32 FirstAirStep = INDEX_NONE;
	for (int32 Step = 1; Step <= EmbedResolveMaxScanVoxels; ++Step)
	{
		if (IsSolidAt(Center + FVector(0.f, 0.f, Step * VoxelSize)))
		{
			AirRun = 0;
			FirstAirStep = INDEX_NONE;
			continue;
		}
		if (AirRun++ == 0)
		{
			FirstAirStep = Step;
		}
		if (AirRun >= HeadroomSamples)
		{
			break;
		}
	}
	if (FirstAirStep == INDEX_NONE || AirRun < HeadroomSamples)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("%s is embedded in terrain at %s but no headroom was found within %d voxels."),
			*Character->GetName(), *Center.ToCompactString(), EmbedResolveMaxScanVoxels);
		return false;
	}

	// The surface sits between the last solid sample and the first air one; snap onto the collision there.
	const float SurfaceGuessZ = Center.Z + (FirstAirStep - 0.5f) * VoxelSize;
	FVector Target(Center.X, Center.Y, SurfaceGuessZ + HalfHeight + 5.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VoxelEmbedResolve), false, Character);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, FVector(Center.X, Center.Y, SurfaceGuessZ + 2.f * HalfHeight),
		FVector(Center.X, Center.Y, SurfaceGuessZ - 2.f * VoxelSize), Capsule->GetCollisionObjectType(), Params))
	{
		Target.Z = Hit.ImpactPoint.Z + HalfHeight + 5.f;
	}

	Character->SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
	Velocity = FVector::ZeroVector;
	SetMovementMode(MOVE_Falling);

	UE_LOG(LogVoxelCharacter, Log, TEXT("%s was embedded in terrain; lifted %.0f uu onto the new surface."),
		*Character->GetName(), Target.Z - Center.Z);
	return true;
}
