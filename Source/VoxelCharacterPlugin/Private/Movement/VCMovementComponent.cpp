// Copyright Daniel Raquel. All Rights Reserved.

#include "Movement/VCMovementComponent.h"
#include "Movement/VCVoxelNavigationHelper.h"
#include "Movement/VCGravityFrame.h"
#include "GameFramework/Character.h"
#include "GameFramework/PhysicsVolume.h"
#include "VoxelChunkManager.h"
#include "VoxelCollisionManager.h"
#include "VoxelWorldConfiguration.h"
#include "VoxelMaterialRegistry.h"
#include "VoxelEditManager.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Core/VCCharacterAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Combat/VCCombatStatics.h"
#include "Tags/CGFGameplayTags.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarStepUpBlocks(
	TEXT("vc.Move.StepUpBlocks"),
	1,
	TEXT("Cubic worlds: players and NPCs step up one block without a jump (1, world modes review WM-C7, D29), or keep the ")
	TEXT("smooth step so every block is a jump (0, as before). Read when a walker first sees the voxel world."),
	ECVF_Default);

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
	if (UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld()))
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
		RefreshWorldBounds();
		RefreshBlockStepHeight();
	}

	// World edge band (WM-I5): every machine, from the position, for the HUD hint.
	if (bHasWorldBounds && UpdatedComponent)
	{
		ApplyWorldEdgePushBack(CachedWorldBounds, UpdatedComponent->GetComponentLocation(), FVector::ZeroVector, 0.f,
			WorldEdgePushBackDistance, WorldEdgePushBackSpeed, WorldEdgeProximity);
	}
	else
	{
		WorldEdgeProximity = 0.f;
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

// ---------------------------------------------------------------------------
// World edge (world modes review WM-I5, decision D9)
// ---------------------------------------------------------------------------

void UVCMovementComponent::RefreshWorldBounds()
{
	if (bHasWorldBounds)
	{
		return;
	}
	if (const UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld()))
	{
		CachedWorldBounds = ChunkMgr->GetWorldBounds();
		bHasWorldBounds = true;
		bPlanetGravity = CachedWorldBounds.Shape == EVoxelWorldBoundsShape::Shell;
	}
}

// ---------------------------------------------------------------------------
// Block steps (world modes review WM-C7, decision D29)
// ---------------------------------------------------------------------------

void UVCMovementComponent::RefreshBlockStepHeight()
{
	if (bBlockStepResolved)
	{
		return;
	}
	const UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld());
	const UVoxelWorldConfiguration* Config = ChunkMgr ? ChunkMgr->GetConfiguration() : nullptr;
	if (!ChunkMgr || !ChunkMgr->IsInitialized() || !Config)
	{
		return; // the voxel world is not up yet: try again at the next terrain refresh
	}
	bBlockStepResolved = true;
	// A cubic world's ground is blocks a voxel high: step one, as the surface navigation does, and leave two to be a wall.
	if (bStepUpOneBlock && CVarStepUpBlocks.GetValueOnGameThread() != 0 && ChunkMgr->IsBlockSurface())
	{
		MaxStepHeight = GetBlockStepHeight(Config->VoxelSize, BlockStepMargin);
		bSteppingBlocks = true;
		UE_LOG(LogVoxelCharacter, Log, TEXT("%s: cubic world, stepping one block (MaxStepHeight %.0f)."), *GetNameSafe(GetOwner()), MaxStepHeight);
	}
}

// ---------------------------------------------------------------------------
// Planet gravity (spherical SP-F1; WORLD_MODES_REVIEW_PLAN.md §17, WM-W1)
// ---------------------------------------------------------------------------

FVector UVCMovementComponent::ComputePlanetGravityDirection(const FVector& Location, const FVector& Centre, const FVector& Fallback)
{
	const FVector ToCentre = Centre - Location;
	return ToCentre.IsNearlyZero() ? Fallback : ToCentre.GetSafeNormal();
}

bool UVCMovementComponent::UpdatePlanetGravity()
{
	RefreshWorldBounds();
	if (!bPlanetGravity || !UpdatedComponent)
	{
		return false;
	}
	// The world mode's up is the direction from the centre (FSphericalPlanetWorldMode::GetUpVector); gravity is its
	// opposite. The engine ignores changes under about 1e-4 (25 uu of walking on a 250,000 uu planet).
	SetGravityDirection(ComputePlanetGravityDirection(UpdatedComponent->GetComponentLocation(), CachedWorldBounds.Center,
		GetGravityDirection()));
	return true;
}

void UVCMovementComponent::SnapUprightToGravity()
{
	// On a planet also where its gravity is the engine's default (exactly -Z, over the north pole): a body brought there
	// tilted (a teleport from elsewhere on the planet) is levelled too (spherical Phase 6). GetYawRotation keeps only the
	// yaw for the default gravity.
	if (!CharacterOwner || !(HasCustomGravity() || bPlanetGravity))
	{
		return;
	}
	CharacterOwner->SetActorRotation(FVCGravityFrame::GetYawRotation(this, CharacterOwner->GetActorRotation()), ETeleportType::TeleportPhysics);
}

void UVCMovementComponent::PerformMovement(float DeltaTime)
{
	// Every simulated move (the server's replay of a client move, the client's prediction and its replays, standalone,
	// NPCs) starts from the gravity at its own position, so prediction and authority agree with no new replicated state;
	// simulated proxies take the engine's replicated gravity direction.
	UpdatePlanetGravity();
	Super::PerformMovement(DeltaTime);
}

bool UVCMovementComponent::ShouldRemainVertical() const
{
	// A swimmer and a flyer (debug flight) as well as the engine's walker and faller: nothing else re-uprights them as
	// gravity turns under them.
	return Super::ShouldRemainVertical() || (HasCustomGravity() && (IsSwimming() || IsFlying()));
}

// ---------------------------------------------------------------------------
// Debug flight (spherical Phase 6, WM-BP2, D20)
// ---------------------------------------------------------------------------

void UVCMovementComponent::SetDebugFlight(bool bEnable)
{
	if (bEnable)
	{
		RefreshWorldBounds();
		UpdatePlanetGravity();
		FlightClimb = 0.f;
		if (UpdatedComponent)
		{
			FlightHoldLevel = GetFlightLevel(UpdatedComponent->GetComponentLocation());
		}
		bDebugFlight = true;
		SetMovementMode(MOVE_Flying);
		Velocity = FVector::ZeroVector;
		SnapUprightToGravity();
	}
	else if (bDebugFlight)
	{
		bDebugFlight = false;
		FlightClimb = 0.f;
		if (MovementMode == MOVE_Flying)
		{
			SetMovementMode(MOVE_Falling);
		}
	}
}

void UVCMovementComponent::SetFlightClimb(float Climb)
{
	FlightClimb = FMath::Clamp(Climb, -1.f, 1.f);
	if (FlightClimb == 0.f && UpdatedComponent)
	{
		FlightHoldLevel = GetFlightLevel(UpdatedComponent->GetComponentLocation());
	}
}

double UVCMovementComponent::GetFlightLevel(const FVector& Location) const
{
	return bPlanetGravity ? FVector::Dist(Location, CachedWorldBounds.Center) : Location.Z;
}

void UVCMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
	// MOVE_None is a freeze or a teleport's hold; any mode but flying ends debug flight.
	if (bDebugFlight && MovementMode != MOVE_Flying && MovementMode != MOVE_None)
	{
		bDebugFlight = false;
		FlightClimb = 0.f;
	}
}

void UVCMovementComponent::PhysFlying(float DeltaTime, int32 Iterations)
{
	if (!bDebugFlight || !UpdatedComponent)
	{
		Super::PhysFlying(DeltaTime, Iterations);
		return;
	}
	if (DeltaTime < MIN_TICK_TIME)
	{
		return;
	}
	RestorePreAdditiveRootMotionVelocity();

	// Across local up: the engine's flying velocity from the movement input; without input it stops at once, as the
	// engine's cheat flying does (QA flight hovers where it is let go).
	const FVector Up = FVCGravityFrame::GetUp(this);
	Acceleration -= Up * FVector::DotProduct(Acceleration, Up);
	if (Acceleration.IsNearlyZero())
	{
		Velocity = FVector::ZeroVector;
	}
	else
	{
		CalcVelocity(DeltaTime, 0.5f * GetPhysicsVolume()->FluidFriction, true, GetMaxBrakingDeceleration());
	}
	const FVector Across = Velocity - Up * FVector::DotProduct(Velocity, Up);

	// Along local up: the climb (hands-free, else the jump / crouch keys held), or back to the held level. Flying straight
	// across a planet rises off it (the curvature); the hold takes that back each frame.
	float Climb = FlightClimb;
	if (Climb == 0.f && CharacterOwner)
	{
		if (CharacterOwner->bPressedJump || bWantsToAscend)
		{
			Climb = 1.f;
		}
		else if (bWantsToCrouch)
		{
			Climb = -1.f;
		}
	}
	const FVector Here = UpdatedComponent->GetComponentLocation();
	const double MaxUp = static_cast<double>(MaxFlySpeed);
	double UpSpeed = 0.0;
	if (Climb != 0.f)
	{
		UpSpeed = static_cast<double>(Climb) * MaxUp;
		FlightHoldLevel = GetFlightLevel(Here + (Across + Up * UpSpeed) * DeltaTime);
	}
	else
	{
		UpSpeed = FMath::Clamp((FlightHoldLevel - GetFlightLevel(Here)) / DeltaTime, -MaxUp, MaxUp);
	}
	Velocity = Across + Up * UpSpeed;

	Iterations++;
	bJustTeleported = false;
	const FVector Adjusted = Velocity * DeltaTime;
	FHitResult Hit(1.f);
	SafeMoveUpdatedComponent(Adjusted, UpdatedComponent->GetComponentQuat(), true, Hit);
	if (Hit.Time < 1.f)
	{
		SlideAlongSurface(Adjusted, 1.f - Hit.Time, Hit.Normal, Hit, true);
	}
	if (!bJustTeleported && !HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
	{
		Velocity = (UpdatedComponent->GetComponentLocation() - Here) / DeltaTime;
	}
}

double UVCMovementComponent::GetHorizontalSpeedSquared(const FVector& InVelocity) const
{
	return HasCustomGravity() ? ProjectToGravityFloor(InVelocity).SizeSquared() : InVelocity.SizeSquared2D();
}

FVector UVCMovementComponent::GetHorizontalDirection(const FVector& InVector) const
{
	return HasCustomGravity() ? ProjectToGravityFloor(InVector).GetSafeNormal() : InVector.GetSafeNormal2D();
}

FVector UVCMovementComponent::ApplyWorldEdgePushBack(const FVoxelWorldBounds& Bounds, const FVector& Location, const FVector& Velocity,
	float MaxSpeed, float BandWidth, float PushSpeed, float& OutProximity)
{
	OutProximity = 0.f;
	// Islands only: a plane has no edge and a planet's shell holds its pawns by gravity.
	if (BandWidth <= 0.f || (Bounds.Shape != EVoxelWorldBoundsShape::Circle && Bounds.Shape != EVoxelWorldBoundsShape::Rectangle))
	{
		return Velocity;
	}
	const double Distance = Bounds.GetDistanceToEdgeXY(Location.X, Location.Y);
	if (Distance >= BandWidth)
	{
		return Velocity;
	}
	const float T = FMath::Clamp(1.f - static_cast<float>(Distance) / BandWidth, 0.f, 1.f);
	OutProximity = T;
	const FVector2D Out2D = Bounds.GetOutwardNormalXY(Location.X, Location.Y);
	const FVector Out(Out2D.X, Out2D.Y, 0.0);
	// Allowed outward speed: the full max speed at the band's inner side, -PushSpeed (moving inward) at the edge.
	const float Allowed = FMath::Lerp(FMath::Max(MaxSpeed, 0.f), -PushSpeed, T);
	const float Outward = static_cast<float>(FVector::DotProduct(Velocity, Out));
	return Outward > Allowed ? Velocity - Out * (Outward - Allowed) : Velocity;
}

void UVCMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);

	// The current before the world's edge (WM-I5). Inside the movement simulation, so the client's prediction and the
	// server agree. Flying is exempt: debug flight and the streaming benchmark cross the edge on purpose.
	if (bHasWorldBounds && UpdatedComponent && MovementMode != MOVE_Flying && MovementMode != MOVE_None)
	{
		float Proximity = 0.f;
		Velocity = ApplyWorldEdgePushBack(CachedWorldBounds, UpdatedComponent->GetComponentLocation(), Velocity, GetMaxSpeed(),
			WorldEdgePushBackDistance, WorldEdgePushBackSpeed, Proximity);
	}
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

	// --- Dive controls: Jump = ascend, Crouch = descend (along local up: radial on a planet, WM-W2) ---
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
		Acceleration += FVCGravityFrame::GetUp(this) * DiveAscendAcceleration;
		AnalogInputModifier = 1.f;
	}
	else if (bWantsDescent)
	{
		// Check if the ocean floor is directly below — don't descend into it
		const float HalfHeight = CharacterOwner->GetSimpleCollisionHalfHeight();
		const FVector FeetPos = CharacterOwner->GetActorLocation() + GetGravityDirection() * HalfHeight;
		FHitResult GroundHit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DiveFloorCheck), false, CharacterOwner);
		const bool bHitFloor = GetWorld()->LineTraceSingleByChannel(
			GroundHit, FeetPos, FeetPos + GetGravityDirection() * 50.f,
			ECC_WorldStatic, Params);

		if (!bHitFloor)
		{
			Acceleration -= FVCGravityFrame::GetUp(this) * DiveDescendAcceleration;
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
		const bool bHasVerticalInput = bWantsAscend || bWantsDescent || FMath::Abs(GetGravitySpaceZ(Acceleration)) > 10.f;
		if (!bHasVerticalInput && SurfaceBuoyancyStrength > 0.f && CachedTerrainContext.bIsUnderwater)
		{
			// Scale buoyancy with depth: strongest deep, zero at surface
			const float DepthRatio = FMath::Clamp(CachedTerrainContext.WaterDepth / FMath::Max(SwimmingEntryDepth, 1.f), 0.f, 1.f);
			Velocity += FVCGravityFrame::GetUp(this) * (SurfaceBuoyancyStrength * DepthRatio * DeltaTime);
		}

		// --- Surface damping: decelerate upward motion near water surface ---
		if (CachedTerrainContext.WaterDepth < SwimmingEntryDepth && GetGravitySpaceZ(Velocity) > 0.f)
		{
			const float SurfaceRatio = FMath::Clamp(CachedTerrainContext.WaterDepth / FMath::Max(SwimmingEntryDepth, 1.f), 0.f, 1.f);
			SetGravitySpaceZ(Velocity, GetGravitySpaceZ(Velocity) * FMath::Lerp(SwimmingSurfaceDamping, 1.f, SurfaceRatio));
		}

		// --- Surface cap: hard-stop upward velocity at the water line ---
		// Prevents the character from breaching the surface and falling back in.
		// Shore exit is handled by horizontal movement into shallow water, which
		// triggers the mode transition in UpdateVoxelTerrainContext.
		const float HalfHeight = CharacterOwner->GetSimpleCollisionHalfHeight();
		if (CachedTerrainContext.WaterDepth < HalfHeight && GetGravitySpaceZ(Velocity) > 0.f)
		{
			SetGravitySpaceZ(Velocity, 0.0);
		}
	}

	// --- Clamp vertical speed ---
	SetGravitySpaceZ(Velocity, FMath::Clamp(GetGravitySpaceZ(Velocity), -MaxSwimSpeed, MaxSwimSpeed));

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

	// Query voxel terrain at the character's feet position (along local down: radial on a planet)
	const float HalfHeight = Owner->GetSimpleCollisionHalfHeight();
	const FVector FeetPos = Owner->GetActorLocation() + GetGravityDirection() * HalfHeight;
	CachedTerrainContext = FVCVoxelNavigationHelper::QueryTerrainContext(GetWorld(), FeetPos, FVCGravityFrame::GetUp(this));

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
	// Not while flying: debug flight (vox.Fly) and the streaming benchmark's fly pawn cross the sea on purpose. Not while
	// movement is disabled (MOVE_None: a hold, a death): switching a held pawn in water back to swimming let it sink
	// through a seabed that had streamed out under the orbit camera (spherical Phase 6, P6.5).
	if (MovementMode == MOVE_None)
	{
		return;
	}
	if (CachedTerrainContext.bIsUnderwater && CachedTerrainContext.WaterDepth >= SwimmingEntryDepth && !IsFlying())
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
			const FVector UpperBodyPos = Owner->GetActorLocation() + FVCGravityFrame::GetUp(this) * (HalfHeight * 0.5f);
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
			const FVector TraceEnd = CapsuleLocation + GetGravityDirection() * (CapsuleHalfHeight + FloorTraceMargin);

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
			const FVector TraceStart = CapsuleLocation + GetGravityDirection() * CapsuleHalfHeight;
			const FVector TraceEnd = TraceStart + GetGravityDirection() * GraceHeightThreshold;

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
		const FVector TraceEnd = CapsuleLocation + GetGravityDirection() * (CapsuleHalfHeight + 50.f);

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
	// The registry's materials (EVoxelMaterial, VoxelMaterialRegistry.h) to the movement's surface types (code review CR2,
	// VCP-14; a surface class on FVoxelMaterialDefinition would make this data).
	switch (MaterialID)
	{
	case EVoxelMaterial::Grass:      return EVoxelSurfaceType::Grass;
	case EVoxelMaterial::Dirt:       return EVoxelSurfaceType::Dirt;
	case EVoxelMaterial::Stone:      return EVoxelSurfaceType::Stone;
	case EVoxelMaterial::Sand:       return EVoxelSurfaceType::Sand;
	case EVoxelMaterial::Snow:       return EVoxelSurfaceType::Snow;
	case EVoxelMaterial::Sandstone:  return EVoxelSurfaceType::Sand;
	case EVoxelMaterial::FrozenDirt: return EVoxelSurfaceType::Ice;
	case EVoxelMaterial::Coal:       return EVoxelSurfaceType::Stone;
	case EVoxelMaterial::Iron:       return EVoxelSurfaceType::Metal;
	case EVoxelMaterial::Gold:       return EVoxelSurfaceType::Metal;
	case EVoxelMaterial::Copper:     return EVoxelSurfaceType::Metal;
	case EVoxelMaterial::Diamond:    return EVoxelSurfaceType::Stone;
	case EVoxelMaterial::Wood:       return EVoxelSurfaceType::Wood;
	case EVoxelMaterial::Leaves:     return EVoxelSurfaceType::Grass;
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
	if (!Character || !World || !Character->HasAuthority())
	{
		return;
	}
	const UCapsuleComponent* Capsule = Character->GetCapsuleComponent();
	const float Reach = (Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 100.f) + 200.f;
	const FVector PawnLocation = Character->GetActorLocation();
	bool bReachesPawn = false;
	if (EditRadius > 0.f)
	{
		// A brush: its sphere.
		bReachesPawn = FVector::DistSquared(EditCenter, PawnLocation) <= FMath::Square(EditRadius + Reach);
	}
	else
	{
		// No centre: a batch of single-voxel edits (a POI's terraform, a campsite pad, a dungeon stamp), undo, a clear or
		// a load. The box of the voxels the edit changed in this chunk, which the edit manager gives while it broadcasts;
		// without it, the whole chunk. Chunks are world-aligned boxes on every world mode, a planet's too.
		const UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(World);
		const UVoxelWorldConfiguration* Config = ChunkMgr ? ChunkMgr->GetConfiguration() : nullptr;
		if (Config)
		{
			FIntVector LocalMin(0), LocalMax(Config->ChunkSize - 1);
			if (const UVoxelEditManager* EditMgr = ChunkMgr->GetEditManager())
			{
				EditMgr->GetBroadcastEditBounds(LocalMin, LocalMax);
			}
			const FBox Edited = GetEditedVoxelWorldBox(ChunkCoord, LocalMin, LocalMax, Config->WorldOrigin, Config->ChunkSize,
				Config->VoxelSize);
			bReachesPawn = Edited.ComputeSquaredDistanceToPoint(PawnLocation) <= FMath::Square(Reach);
		}
	}
	if (bReachesPawn)
	{
		EmbedCheckArmedUntil = World->GetTimeSeconds() + 10.0;
	}
}

FBox UVCMovementComponent::GetEditedVoxelWorldBox(const FIntVector& ChunkCoord, const FIntVector& LocalMin,
	const FIntVector& LocalMax, const FVector& WorldOrigin, int32 ChunkSize, float VoxelSize)
{
	// Voxel i of a chunk covers [i, i + 1) voxels from the chunk's minimum corner (UVoxelEditManager::WorldToLocalPos).
	const FVector ChunkMin = WorldOrigin + FVector(ChunkCoord) * (double(ChunkSize) * VoxelSize);
	return FBox(ChunkMin + FVector(LocalMin) * VoxelSize, ChunkMin + FVector(LocalMax + FIntVector(1)) * VoxelSize);
}

bool UVCMovementComponent::IsEmbedCheckArmed() const
{
	const UWorld* World = GetWorld();
	return World && EmbedCheckArmedUntil >= 0.0 && World->GetTimeSeconds() <= EmbedCheckArmedUntil;
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
	if (IsSprinting() && GetHorizontalSpeedSquared(Velocity) > FMath::Square(100.f))
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

	// Only at the surface (head out or nearly), only into something steep, and only when trying to go that way. Heights
	// and the horizontal are in the gravity frame (local up on a planet, WM-W2).
	const FVector Up = FVCGravityFrame::GetUp(this);
	const FVector Push = GetHorizontalDirection(Acceleration);
	if (CachedTerrainContext.WaterDepth > HalfHeight * 1.5f || GetGravitySpaceZ(Hit.ImpactNormal) > 0.7f || Push.IsNearlyZero()
		|| FVector::DotProduct(Push, -GetHorizontalDirection(Hit.ImpactNormal)) < 0.3f)
	{
		return false;
	}

	// Look for a floor just past the bank edge, no higher than the reach.
	const FVector Location = UpdatedComponent->GetComponentLocation();
	const float FeetZ = GetGravitySpaceZ(Location) - HalfHeight;
	const float Reach = HalfHeight + ShoreClimbHeight;
	const FVector ProbeTop = Location + Push * (Radius + 25.f) + Up * Reach;
	const FVector ProbeBottom = ProbeTop - Up * (Reach + HalfHeight);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ShoreClimb), false, Character);
	FHitResult Floor;
	const FCollisionShape Capsule = FCollisionShape::MakeCapsule(Radius, HalfHeight);
	// The capsule stands along local up (the gravity frame's rotation; identity for the default gravity).
	const FQuat CapsuleRotation = HasCustomGravity() ? GetWorldToGravityTransform() : FQuat::Identity;
	if (!World->SweepSingleByChannel(Floor, ProbeTop, ProbeBottom, CapsuleRotation, UpdatedComponent->GetCollisionObjectType(),
		Capsule, Params) || Floor.bStartPenetrating || !IsWalkable(Floor))
	{
		return false;
	}

	// The ledge must lift the feet out of swimming depth (the water line is FeetZ + WaterDepth).
	const float LedgeFeetZ = GetGravitySpaceZ(Floor.Location) - HalfHeight;
	const float WaterLineZ = FeetZ + CachedTerrainContext.WaterDepth;
	if (LedgeFeetZ < WaterLineZ - SwimmingEntryDepth * 0.5f || LedgeFeetZ > FeetZ + Reach)
	{
		return false;
	}

	Character->SetActorLocation(Floor.Location + Up * 2.f, false, nullptr, ETeleportType::None);
	SetMovementMode(MOVE_Walking);
	MaxAcceleration = BaseMaxAcceleration;
	Velocity = Push * FMath::Min(FMath::Sqrt(GetHorizontalSpeedSquared(Velocity)), MaxWalkSpeed);
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
		+ FVCGravityFrame::GetUp(this) * (Character->GetSimpleCollisionHalfHeight() * HeadHeightFraction);
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
	UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld());
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
	UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(World);
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
	UVoxelChunkManager* ChunkMgr = World ? UVoxelChunkManager::Find(World) : nullptr;
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
		return ChunkMgr->GetVoxelAt(P + FVector(VoxelSize * 0.5f)).IsSolid();
	};
	// Along local up (radial on a planet, WM-W2).
	const FVector Up = FVCGravityFrame::GetUp(this);
	if (!IsSolidAt(Center) || !IsSolidAt(Center + Up * (HalfHeight * 0.5f)))
	{
		return false;
	}

	// Scan up for the first run of air tall enough for the capsule.
	const int32 HeadroomSamples = FMath::CeilToInt((2.f * HalfHeight) / VoxelSize) + 1;
	int32 AirRun = 0;
	int32 FirstAirStep = INDEX_NONE;
	for (int32 Step = 1; Step <= EmbedResolveMaxScanVoxels; ++Step)
	{
		if (IsSolidAt(Center + Up * (Step * VoxelSize)))
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
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VoxelEmbedResolve), false, Character);
	FHitResult Hit;
	FVector Target;
	if (!HasCustomGravity())
	{
		const float SurfaceGuessZ = Center.Z + (FirstAirStep - 0.5f) * VoxelSize;
		Target = FVector(Center.X, Center.Y, SurfaceGuessZ + HalfHeight + 5.f);
		if (World->LineTraceSingleByChannel(Hit, FVector(Center.X, Center.Y, SurfaceGuessZ + 2.f * HalfHeight),
			FVector(Center.X, Center.Y, SurfaceGuessZ - 2.f * VoxelSize), Capsule->GetCollisionObjectType(), Params))
		{
			Target.Z = Hit.ImpactPoint.Z + HalfHeight + 5.f;
		}
	}
	else
	{
		// A planet: the same along local up.
		const FVector SurfaceGuess = Center + Up * ((FirstAirStep - 0.5f) * VoxelSize);
		Target = SurfaceGuess + Up * (HalfHeight + 5.f);
		if (World->LineTraceSingleByChannel(Hit, SurfaceGuess + Up * (2.f * HalfHeight), SurfaceGuess - Up * (2.f * VoxelSize),
			Capsule->GetCollisionObjectType(), Params))
		{
			Target = Hit.ImpactPoint + Up * (HalfHeight + 5.f);
		}
	}

	Character->SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
	Velocity = FVector::ZeroVector;
	SetMovementMode(MOVE_Falling);

	UE_LOG(LogVoxelCharacter, Log, TEXT("%s was embedded in terrain; lifted %.0f uu onto the new surface."),
		*Character->GetName(), FVector::DotProduct(Target - Center, Up));
	return true;
}
