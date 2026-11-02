// Copyright Daniel Raquel. All Rights Reserved.

// AVCCharacterBase's terrain-ready spawn and leaving the world (the world-safety recovery) (a file of the class, code review CR4).

#include "Core/VCCharacterBase.h"
#include "VoxelWorldModeTypes.h"
#include "HAL/IConsoleManager.h"
#include "Core/VCPlayerState.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Core/VCPlayerController.h"
#include "Core/VCGameModeBase.h"
#include "Combat/VCCombatComponent.h"
#include "Editing/VCBlockInteractionComponent.h"
#include "Utilities/CGFCombatStatics.h"
#include "Tags/CGFGameplayTags.h"
#include "Interfaces/CGFRestPointInterface.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/VCCameraManager.h"
#include "Camera/VCFirstPersonCameraMode.h"
#include "Camera/VCThirdPersonCameraMode.h"
#include "Movement/VCMovementComponent.h"
#include "Movement/VCTerrainQueryHelper.h"
#include "Movement/VCGravityFrame.h"
#include "Camera/VCUnderwaterPostProcess.h"
#include "Input/VCInputConfig.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "TimerManager.h"
#include "AbilitySystemComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Net/UnrealNetwork.h"
#include "VoxelCoordinates.h"
#include "VoxelWorldConfiguration.h"
#include "VoxelChunkManager.h"
#include "VoxelSeaSurface.h"
#include "VoxelScatterManager.h"
#include "VoxelCollisionManager.h"
#include "VoxelCharacterPlugin.h"
#include "Engine/Engine.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/GameModeBase.h"
#include "Combat/VCCombatStatics.h"

#if WITH_INTERACTION_PLUGIN
#include "Components/InteractionComponent.h"
#include "Components/InteractableComponent.h"
#include "Detection/SphereOverlapDetection.h"
#include "Subsystems/WorldItemPoolSubsystem.h"
#include "Actors/WorldItem.h"
#endif

// ---------------------------------------------------------------------------
// Terrain Ready Spawn
// ---------------------------------------------------------------------------

void AVCCharacterBase::FreezeForTerrainWait()
{
	GetCharacterMovement()->DisableMovement();
	SetActorEnableCollision(false);
	bIsWaitingForTerrain = true;
	TerrainWaitElapsed = 0.f;
	UE_LOG(LogVoxelCharacter, Log, TEXT("Waiting for terrain collision before placing character..."));
}

void AVCCharacterBase::InitiateChunkBasedWait()
{
	// Find the collision manager via the chunk manager
	UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld());
	if (!ChunkMgr)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("InitiateChunkBasedWait: No VoxelChunkManager found — placing immediately."));
		PlaceOnTerrainAndResume();
		return;
	}

	UVoxelCollisionManager* ColMgr = ChunkMgr->GetCollisionManager();
	if (!ColMgr)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("InitiateChunkBasedWait: No CollisionManager — placing immediately."));
		PlaceOnTerrainAndResume();
		return;
	}

	const UVoxelWorldConfiguration* Config = ChunkMgr->GetConfiguration();
	if (!Config)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("InitiateChunkBasedWait: No VoxelWorldConfiguration — placing immediately."));
		PlaceOnTerrainAndResume();
		return;
	}

	CachedCollisionManager = ColMgr;

	// Relocate to valid terrain if current position is over water or invalid.
	// Place at terrain surface height so chunk Z calculation is correct.
	// Movement/collision are disabled during wait, so the character won't fall.
	// PlaceOnTerrainAndResume() raycast (±50000u) handles precise final placement.
	// (Not for an exact-location resume: a saved spot inside a dungeon must stay under the surface.)
	// On a planet the search covers the whole planet (spherical Phase 6, WM-BP11, G36): a big planet's pole can be open
	// sea far past the near search (4.7 km of arc on the 1,000 km test planet), and a spawn there waited in the planet's
	// core for land that never streamed. A planet with no land at all spawns at the sea's surface, swimming.
	FVector ValidSpawn;
	bSpawnAtSea = false;
	if (!bResumeAtExactLocation && FVCTerrainQueryHelper::FindSpawnablePosition(GetWorld(), GetActorLocation(), ValidSpawn, 50000.f, /*bSearchWholeWorld=*/true))
	{
		SetActorLocation(ValidSpawn);
	}
	else if (!bResumeAtExactLocation && ChunkMgr->GetWorldBounds().Shape == EVoxelWorldBoundsShape::Shell)
	{
		const FVoxelSeaSurface Sea = ChunkMgr->GetSeaSurface();
		const FVector Direction = (GetActorLocation() - Sea.Center).GetSafeNormal();
		if (Sea.bEnabled && Sea.bRadial && !Direction.IsNearlyZero())
		{
			const float HalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
			SetActorLocation(Sea.Center + Direction * (Sea.Radius + HalfHeight));
			bSpawnAtSea = true;
			UE_LOG(LogVoxelCharacter, Warning, TEXT("InitiateChunkBasedWait: no land on the planet; spawning at the sea's surface."));
		}
	}
	// A planet: gravity at the new spot and the body upright in it before anything moves (spherical SP-F1, SP-F4).
	if (UVCMovementComponent* VCMove = Cast<UVCMovementComponent>(GetCharacterMovement()); VCMove && VCMove->UpdatePlanetGravity())
	{
		VCMove->SnapUprightToGravity();
	}

	UE_LOG(LogVoxelCharacter, Log,
		TEXT("InitiateChunkBasedWait: Bound to CollisionManager %p in world '%s' (PIE=%d)"),
		ColMgr, *GetWorld()->GetName(), GetWorld()->IsPlayInEditor());

	// Convert character world position to chunk coordinate
	const FVector CharPos = GetActorLocation();
	const FVector RelPos = CharPos - Config->WorldOrigin;
	const FIntVector CenterChunk = FVoxelCoordinates::WorldToChunk(RelPos, Config->ChunkSize, Config->VoxelSize);

	// The chunks to wait for: the grid across local up at the pawn's chunk layer and the chunk under its feet, in every kind
	// of world (G84).
	const FVoxelWorldBounds Bounds = ChunkMgr->GetWorldBounds();
	const TArray<FIntVector> WaitChunks = FVCTerrainQueryHelper::GetTerrainWaitChunks(*ChunkMgr, CharPos, TerrainWaitChunkRadius);
	TerrainWaitTotalChunks = WaitChunks.Num();
	PendingTerrainChunks.Empty();
	for (const FIntVector& ChunkCoord : WaitChunks)
	{
		if (ColMgr->HasCollision(ChunkCoord))
		{
			// Already ready — skip
			continue;
		}

		PendingTerrainChunks.Add(ChunkCoord);

		// Request collision with high priority so it's processed ASAP
		ColMgr->RequestCollision(ChunkCoord, 2000.f);
	}

	UE_LOG(LogVoxelCharacter, Log,
		TEXT("InitiateChunkBasedWait: Center chunk (%d,%d,%d), waiting for %d of %d chunks (a %dx%d grid and the chunk under the feet)%s"),
		CenterChunk.X, CenterChunk.Y, CenterChunk.Z,
		PendingTerrainChunks.Num(), TerrainWaitTotalChunks,
		2 * TerrainWaitChunkRadius + 1, 2 * TerrainWaitChunkRadius + 1,
		Bounds.Shape == EVoxelWorldBoundsShape::Shell ? TEXT(" (tangent plane)") : TEXT(""));

	if (PendingTerrainChunks.Num() == 0)
	{
		// All chunks already have collision — place immediately
		PlaceOnTerrainAndResume();
		return;
	}

	// Bind to the collision ready delegate
	CollisionReadyDelegateHandle = ColMgr->OnCollisionReady.AddUObject(
		this, &AVCCharacterBase::OnChunkCollisionReady);
}

void AVCCharacterBase::OnChunkCollisionReady(const FIntVector& ChunkCoord)
{
	if (!bIsWaitingForTerrain)
	{
		return;
	}

	const int32 Removed = PendingTerrainChunks.Remove(ChunkCoord);
	if (Removed > 0)
	{
		UE_LOG(LogVoxelCharacter, Log,
			TEXT("OnChunkCollisionReady: Chunk (%d,%d,%d) ready — %d remaining"),
			ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, PendingTerrainChunks.Num());
	}

	if (PendingTerrainChunks.Num() == 0)
	{
		// All required chunks are ready — place character
		UE_LOG(LogVoxelCharacter, Log, TEXT("All terrain chunks ready — placing character."));

		// Unbind delegate now that we're done waiting
		if (UVoxelCollisionManager* ColMgr = CachedCollisionManager.Get())
		{
			ColMgr->OnCollisionReady.Remove(CollisionReadyDelegateHandle);
			CollisionReadyDelegateHandle.Reset();
		}

		PlaceOnTerrainAndResume();
	}
}

/** Seconds an exact-location resume waits for a floor close under the feet before taking a deeper one. */
static constexpr float ExactFloorGraceSeconds = 15.f;

void AVCCharacterBase::PlaceOnTerrainAndResume()
{
	// Local up: +Z, or the radius on a planet (the movement component's gravity, set when the wait began).
	const FVector Up = FVCGravityFrame::GetUp(GetCharacterMovement());
	if (bResumeAtExactLocation)
	{
		// Look for a floor just below the feet; a surface trace from above would put a player saved
		// inside a dungeon on the terrain over it.
		const float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector Feet = GetActorLocation() - Up * CapsuleHalfHeight;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(this);
		FHitResult Hit;
		const bool bFloor = GetWorld()->SweepSingleByChannel(Hit, Feet + Up * 20.f, Feet - Up * FloorProbeDistance,
			FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(20.f), Params);
		// The saved spot stood ON a floor, so the real one is within a few units of the feet. A hit far
		// below is the voxel void under a dungeon whose tile floor has not streamed in yet: give the
		// tiles a grace period before accepting it.
		const float Drop = bFloor ? FVector::DotProduct(Feet - Hit.ImpactPoint, Up) : 0.f;
		const bool bFloorIsClose = bFloor && Drop <= 50.f;
		if (!bFloorIsClose && TerrainWaitElapsed < (bFloor ? ExactFloorGraceSeconds : TerrainWaitTimeout))
		{
			// Keep waiting (frozen, no collision); the tick polls again every second.
			bIsWaitingForTerrain = true;
			return;
		}
		bResumeAtExactLocation = false;
		PendingTerrainChunks.Empty();
		// No floor at all under the saved spot by the timeout: nothing will come (the spot is outside the world, or its
		// terrain never generates). Go to the respawn point instead of dropping into the void (WM-I4).
		if (!bFloor && RecoverFromMissingTerrain())
		{
			return;
		}
		SetActorEnableCollision(true);
		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
		if (bFloor)
		{
			SetActorLocation(Hit.ImpactPoint + Up * (CapsuleHalfHeight + 2.f));
			UE_LOG(LogVoxelCharacter, Log, TEXT("PlaceOnTerrainAndResume: resumed at the saved spot, floor %.0f below (%s)."),
				Drop, Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("null"));
		}
		else
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("PlaceOnTerrainAndResume: no floor under the saved spot after %.1fs — resuming anyway."), TerrainWaitElapsed);
		}
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		bIsWaitingForTerrain = false;
		return;
	}

	// Re-enable collision on the actor and explicitly on the capsule
	SetActorEnableCollision(true);
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	// Sphere sweep downward (along local down: the radius on a planet) to find terrain surface (more robust than line
	// trace at a single point)
	const FVector SpawnPos = GetActorLocation();
	const FVector TraceStart = SpawnPos + Up * 50000.f;
	const FVector TraceEnd = SpawnPos - Up * 50000.f;
	const float SweepRadius = 50.f; // Small sphere to avoid exact-point misses on trimesh seams

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);

	FHitResult Hit;
	const FCollisionShape SweepShape = FCollisionShape::MakeSphere(SweepRadius);
	bool bTraceHit = GetWorld()->SweepSingleByChannel(Hit, TraceStart, TraceEnd, FQuat::Identity, ECC_WorldStatic, SweepShape, Params);

	// Fallback: line trace without sweep
	if (!bTraceHit)
	{
		bTraceHit = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, Params);
	}

	if (bTraceHit)
	{
		const float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		SetActorLocation(Hit.ImpactPoint + Up * CapsuleHalfHeight);

		UE_LOG(LogVoxelCharacter, Log,
			TEXT("PlaceOnTerrainAndResume: Trace HIT at (%.0f, %.0f, %.0f) — Component=%s — placed at Z=%.0f"),
			Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Hit.ImpactPoint.Z,
			Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("null"),
			GetActorLocation().Z);
	}
	else
	{
		UE_LOG(LogVoxelCharacter, Warning,
			TEXT("PlaceOnTerrainAndResume: Trace MISSED — character at (%.0f, %.0f, %.0f), World=%s%s"),
			SpawnPos.X, SpawnPos.Y, SpawnPos.Z,
			*GetWorld()->GetName(), bSpawnAtSea ? TEXT(" (at the sea's surface: swimming)") : TEXT(""));
		// Nothing under the pawn after the wait: go to the respawn point and wait there (WM-I4). Without one (or after
		// two tries) it falls, and falling out of the world respawns it. A spawn at the sea's surface (a planet without
		// land, G36) drops into the sea instead and swims.
		if (!bSpawnAtSea && RecoverFromMissingTerrain())
		{
			return;
		}
	}

	// Resume normal movement (upright in the gravity at the landing spot on a planet)
	if (UVCMovementComponent* VCMove = Cast<UVCMovementComponent>(GetCharacterMovement()); VCMove && VCMove->UpdatePlanetGravity())
	{
		VCMove->SnapUprightToGravity();
	}
	// At the sea's surface the pawn drops in and swims; on ground it walks.
	GetCharacterMovement()->SetMovementMode((bSpawnAtSea && !bTraceHit) ? MOVE_Falling : MOVE_Walking);
	bIsWaitingForTerrain = false;
	bSpawnAtSea = false;
	TerrainRecoveryAttempts = 0;

	UE_LOG(LogVoxelCharacter, Log,
		TEXT("Terrain ready — ActorCollision=%s, CapsuleCollision=%s, MovementMode=%d"),
		GetActorEnableCollision() ? TEXT("Enabled") : TEXT("DISABLED"),
		GetCapsuleComponent() ? (*StaticEnum<ECollisionEnabled::Type>()->GetNameStringByValue(static_cast<int64>(GetCapsuleComponent()->GetCollisionEnabled()))) : TEXT("?"),
		static_cast<int32>(GetCharacterMovement()->MovementMode.GetValue()));
}

// ---------------------------------------------------------------------------
// Leaving the world (world modes review WM-I4, KI14; WM-I5)
// ---------------------------------------------------------------------------

bool AVCCharacterBase::GetLastSafeLocation(FVector& OutLocation) const
{
	OutLocation = LastSafeLocation;
	return bHasLastSafeLocation;
}

bool AVCCharacterBase::ResolveRecoveryLocation(bool bAllowLastSafe, FVector& OutLocation) const
{
	if (bAllowLastSafe && bHasLastSafeLocation)
	{
		OutLocation = LastSafeLocation;
		return true;
	}
	const UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld());
	const FVoxelWorldBounds Bounds = ChunkMgr ? ChunkMgr->GetWorldBounds() : FVoxelWorldBounds();
	const FVector Here = GetActorLocation();
	// A candidate must be inside the world and away from here: the point is to leave a spot with no terrain. (On a planet
	// distances are 3D: two points over each other in X and Y are on different sides of it.)
	const bool bShell = Bounds.Shape == EVoxelWorldBoundsShape::Shell;
	auto Usable = [&Bounds, &Here, ChunkMgr, bShell](const FVector& Candidate)
	{
		const float MinMove = ChunkMgr && ChunkMgr->GetConfiguration() ? ChunkMgr->GetConfiguration()->GetChunkWorldSize() : 4800.f;
		return Bounds.ContainsXY(Candidate.X, Candidate.Y)
			&& (bShell ? FVector::Dist(Candidate, Here) : FVector::Dist2D(Candidate, Here)) > MinMove;
	};

	FTransform RestPoint;
	if (const AVCPlayerState* PS = GetPlayerState<AVCPlayerState>(); PS && PS->GetRespawnPoint(RestPoint) && Usable(RestPoint.GetLocation()))
	{
		OutLocation = RestPoint.GetLocation();
		return true;
	}
	if (AGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr)
	{
		if (const AActor* Start = GetController() ? GameMode->FindPlayerStart(GetController()) : nullptr; Start && Usable(Start->GetActorLocation()))
		{
			OutLocation = Start->GetActorLocation();
			return true;
		}
	}
	if (ChunkMgr && ChunkMgr->GetConfiguration())
	{
		// The world's centre: an island's middle, a plane's origin; a planet's spawn point over its terrain (its centre is
		// inside it, SP-F4).
		OutLocation = bShell ? ChunkMgr->GetSpawnPosition()
			: (Bounds.IsBounded() ? Bounds.Center : ChunkMgr->GetConfiguration()->WorldOrigin);
		return true;
	}
	return false;
}

bool AVCCharacterBase::RecoverFromMissingTerrain()
{
	static constexpr int32 MaxRecoveries = 2;
	if (!HasAuthority() || TerrainRecoveryAttempts >= MaxRecoveries)
	{
		return false;
	}
	FVector Target;
	if (!ResolveRecoveryLocation(/*bAllowLastSafe=*/false, Target))
	{
		return false;
	}
	++TerrainRecoveryAttempts;
	UE_LOG(LogVoxelCharacter, Warning,
		TEXT("%s: no terrain under (%.0f, %.0f, %.0f) after %.1fs; moving to the respawn point (%.0f, %.0f, %.0f), try %d."),
		*GetName(), GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z, TerrainWaitElapsed,
		Target.X, Target.Y, Target.Z, TerrainRecoveryAttempts);
	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		PC->Client_ShowToast(NSLOCTEXT("VCCharacter", "NoGroundHere", "No ground here: moved to your respawn point"));
	}
	bResumeAtExactLocation = false;
	SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
	FreezeForTerrainWait();
	InitiateChunkBasedWait();
	return true;
}

void AVCCharacterBase::FellOutOfWorld(const UDamageType& DamageType)
{
	if (!HasAuthority())
	{
		return; // the server decides; the replicated pawn follows
	}
	const bool bDead = CombatComponent && ICGFDamageableInterface::Execute_IsDead(CombatComponent);
	if (bIsCorpse || bDead || !GetController())
	{
		// A corpse, a body already dying, or no player: the engine default (destroy).
		Super::FellOutOfWorld(DamageType);
		return;
	}
	if (bFellOutOfWorld)
	{
		return;
	}
	bFellOutOfWorld = true;

	// Stop the fall and put the body back on the last ground it stood on inside the world, so the death, the corpse
	// and an at-death-location respawn all happen there.
	FVector Back;
	const bool bHasBack = ResolveRecoveryLocation(/*bAllowLastSafe=*/true, Back);
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
	}
	if (bHasBack)
	{
		SetActorLocation(Back, false, nullptr, ETeleportType::TeleportPhysics);
	}
	UE_LOG(LogVoxelCharacter, Warning, TEXT("%s fell out of the world; dying and respawning (body back at %s)."),
		*GetName(), bHasBack ? *Back.ToCompactString() : TEXT("(nowhere known)"));
	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		PC->Client_ShowToast(NSLOCTEXT("VCCharacter", "FellOutOfWorld", "You fell out of the world"));
	}

	// Death through the combat pipeline, so the respawn, the item snapshot and the corpse follow as for any death.
	if (CombatComponent)
	{
		const FCGFDamageContext Fall = UVCCombatStatics::MakeDamageContext(nullptr, nullptr, 1.0e6f,
			CGFGameplayTags::Damage_Type_Pure, /*bIgnoreFaction=*/true);
		UVCCombatStatics::ApplyDamageToActor(this, Fall);
	}
	if (!(CombatComponent && ICGFDamageableInterface::Execute_IsDead(CombatComponent)))
	{
		// Could not die (no combat component, or immune): it is back on safe ground; let it carry on from there.
		bFellOutOfWorld = false;
		if (UCharacterMovementComponent* Move = GetCharacterMovement())
		{
			Move->SetMovementMode(MOVE_Falling);
		}
	}
}

void AVCCharacterBase::TickWorldSafety(float DeltaSeconds)
{
	// The world edge hint (every machine; the HUD only exists where the player is local).
	const UVCMovementComponent* Move = Cast<UVCMovementComponent>(GetCharacterMovement());
	const bool bNear = Move && Move->IsNearWorldEdge() && !bIsCorpse;
	if (bNear != bNearWorldEdge)
	{
		bNearWorldEdge = bNear;
		OnWorldEdgeChanged.Broadcast(bNear);
	}

	if (!HasAuthority() || bIsWaitingForTerrain || bIsCorpse || bFellOutOfWorld || !Move)
	{
		return;
	}

	SafeLocationTimer -= DeltaSeconds;
	if (SafeLocationTimer > 0.f)
	{
		return;
	}
	SafeLocationTimer = 0.25f;

	const UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld());
	if (!ChunkMgr)
	{
		return;
	}
	const FVector Here = GetActorLocation();
	const FVoxelWorldBounds WorldBounds = ChunkMgr->GetWorldBounds();
	if (IsOutOfWorld(WorldBounds, Here, OutOfWorldDepth) && !(CombatComponent && ICGFDamageableInterface::Execute_IsDead(CombatComponent)))
	{
		// A planet: below its lowest terrain is toward the centre, and past its orbit ceiling is out too (SP-F5); a heightmap
		// world: below its lowest terrain (IsOutOfWorld, from a real terrain range only).
		if (WorldBounds.Shape == EVoxelWorldBoundsShape::Shell)
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("%s is %.0f inside the planet's lowest terrain or past its orbit ceiling (%.0f from the centre): out of the world."),
				*GetName(), OutOfWorldDepth, FVector::Dist(Here, WorldBounds.Center));
		}
		else
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("%s is %.0f below the world's lowest terrain: out of the world."), *GetName(), OutOfWorldDepth);
		}
		FellOutOfWorld(*GetDefault<UDamageType>());
		return;
	}
	if (Move->IsMovingOnGround() && ChunkMgr->IsInsideWorld(Here.X, Here.Y))
	{
		LastSafeLocation = Here;
		bHasLastSafeLocation = true;
	}
}

bool AVCCharacterBase::IsOutOfWorld(const FVoxelWorldBounds& Bounds, const FVector& Location, float Depth)
{
	if (Depth <= 0.f)
	{
		return false;
	}
	if (Bounds.Shape == EVoxelWorldBoundsShape::Shell)
	{
		const double Dist = FVector::Dist(Location, Bounds.Center);
		// Outside the orbit ceiling is out too (spherical Phase 6): flight and the orbit camera stay inside it.
		return Dist < static_cast<double>(Bounds.InnerRadius) - Depth || (Bounds.OrbitRadius > 0.0 && Dist > Bounds.OrbitRadius);
	}
	// Only from a real terrain range (a mode without one reports about +-1e9).
	return Bounds.MinZ > -1.0e6f && Location.Z < Bounds.MinZ - Depth;
}

void AVCCharacterBase::FaceRotation(FRotator NewControlRotation, float DeltaTime)
{
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Move || !Move->HasCustomGravity())
	{
		Super::FaceRotation(NewControlRotation, DeltaTime);
		return;
	}
	if (!(bUseControllerRotationPitch || bUseControllerRotationYaw || bUseControllerRotationRoll))
	{
		return;
	}
	// In the gravity frame: take the components the flags ask for from the control rotation, keep the body upright
	// otherwise (APawn::FaceRotation does this in world space, which tilts the body on a planet).
	const FRotator Control = FVCGravityFrame::ToGravityRelative(Move, NewControlRotation);
	const FRotator Current = FVCGravityFrame::ToGravityRelative(Move, GetActorRotation());
	const FRotator Facing(
		bUseControllerRotationPitch ? Control.Pitch : 0.0,
		bUseControllerRotationYaw ? Control.Yaw : Current.Yaw,
		bUseControllerRotationRoll ? Control.Roll : 0.0);
	SetActorRotation(FVCGravityFrame::ToWorld(Move, Facing));
}
