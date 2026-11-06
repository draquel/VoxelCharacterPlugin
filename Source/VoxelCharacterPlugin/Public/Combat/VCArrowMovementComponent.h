// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "VoxelWorldModeTypes.h"
#include "VCArrowMovementComponent.generated.h"

class UVoxelChunkManager;

/**
 * An arrow's flight (ranged combat plan B2): the engine's projectile movement with gravity along local down. On a planet
 * (the voxel world's bounds are a shell) gravity points at the centre, read again from the arrow's position at the start
 * of every substep, so an arrow fired level curves down toward the planet wherever it flies; elsewhere it is the engine's
 * -Z. Both use the world's gravity magnitude (GetGravityZ with ProjectileGravityScale). Substepping is forced so the
 * direction is read at least every MaxSimulationTimeStep, and an arrow at full speed moves about a voxel a substep.
 *
 * Past the voxel world's cooked collision (UVoxelCollisionManager::HasCollision is false for the arrow's chunk) the
 * terrain cannot stop it: an arrow that goes below the generated surface there stops at that surface, as if it had hit
 * it (OnProjectileStop with a hit on no actor), instead of falling on through the ground. Arcs through open air beyond
 * the collision keep flying.
 */
UCLASS(ClassGroup = (VoxelCharacter), meta = (BlueprintSpawnableComponent))
class VOXELCHARACTERPLUGIN_API UVCArrowMovementComponent : public UProjectileMovementComponent
{
	GENERATED_BODY()

public:
	UVCArrowMovementComponent();

	/**
	 * Fly under these world bounds instead of the voxel world's (a world without a chunk manager: tests, a plain map).
	 * @param Bounds The bounds; a shell makes gravity point at its centre
	 */
	void SetWorldBounds(const FVoxelWorldBounds& Bounds);

	/** @return True when gravity points at a planet centre */
	bool HasPlanetGravity() const { return bHasWorldBounds && CachedWorldBounds.Shape == EVoxelWorldBoundsShape::Shell; }

	/**
	 * Gravity's acceleration on the arrow at a point.
	 * @param Location Where the arrow is
	 * @return The acceleration (uu/s^2): toward the planet centre on a planet, along -Z otherwise
	 */
	FVector GetGravityAcceleration(const FVector& Location) const;

	/**
	 * Pure: gravity's acceleration at a point under a world's bounds.
	 * @param Bounds The world's bounds (a shell: a planet)
	 * @param Location The point
	 * @param GravityZ The engine's gravity along Z (negative pulls down); its magnitude is the pull's
	 * @return GravityZ along Z off a planet; on a planet the same magnitude toward the centre (along Z at the centre itself)
	 */
	static FVector ComputeGravityAcceleration(const FVoxelWorldBounds& Bounds, const FVector& Location, float GravityZ);

	/**
	 * Pure: whether a point is below a surface point, along the surface's up.
	 * @param Location The point
	 * @param SurfacePoint A point of the surface on the up-line through Location
	 * @param Up Local up there
	 * @return True when Location lies below the surface
	 */
	static bool IsBelowSurface(const FVector& Location, const FVector& SurfacePoint, const FVector& Up);

	virtual void InitializeComponent() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual FVector ComputeAcceleration(const FVector& InVelocity, float DeltaTime) const override;

private:
	/** Reads the voxel world's bounds once its chunk manager is up (not after SetWorldBounds). */
	void RefreshWorldBounds();

	/**
	 * Past the cooked collision and below the generated surface: stops the arrow at that surface.
	 * @param ChunkManager The voxel world's chunk manager
	 * @return True when it stopped
	 */
	bool StopInUncookedGround(const UVoxelChunkManager& ChunkManager);

	FVoxelWorldBounds CachedWorldBounds;
	bool bHasWorldBounds = false;
};
