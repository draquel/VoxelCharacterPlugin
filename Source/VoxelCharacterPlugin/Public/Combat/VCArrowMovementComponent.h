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
 *
 * The wind (D83, with WorldWeatherPlugin): the weather's wind where the arrow is, times its exposure (none under shelter).
 * The arrow's motion across its launch heading, in the local horizontal, takes the cross wind's at WindResponse a second
 * (so it never drifts faster than the wind); a head or tail wind and the vertical are left alone, so the bow's speed and
 * the drop are the bow's and gravity's. Read every WindSampleInterval, the same on every machine (the weather is
 * synchronised); the heading is the first velocity the arrow flies with.
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

	/**
	 * Pure: the wind's acceleration on an arrow. Across the arrow's heading in the local horizontal, the arrow's speed
	 * relaxes toward the cross wind's at Response a second; along the heading and along Up nothing changes.
	 * @param Velocity The arrow's velocity (uu/s)
	 * @param Wind     The air's velocity (uu/s)
	 * @param Heading  The arrow's launch direction (only its horizontal part counts)
	 * @param Up       Local up (unit)
	 * @param Response How fast the arrow's cross motion takes the wind's (1/s)
	 * @return The acceleration (uu/s^2), horizontal and across the heading (zero without a horizontal heading or a response)
	 */
	static FVector ComputeWindAcceleration(const FVector& Velocity, const FVector& Wind, const FVector& Heading, const FVector& Up, float Response);

	/** @return The wind on the arrow last read (the weather's wind times the exposure; zero without the weather) */
	const FVector& GetWind() const { return CachedWind; }

	/**
	 * How fast the arrow's motion across its heading takes the cross wind's (1/s): 0.4 drifts a full draw (6000 uu/s) about
	 * 28 uu at 30 m and 105 at 60 m in a 600 uu/s cross wind (the weather's strong wind); 0 ignores the wind.
	 */
	UPROPERTY(EditAnywhere, Category = "VoxelCharacter|Arrow", meta = (ClampMin = "0"))
	float WindResponse = 0.4f;

	/** How often the wind at the arrow is read (s). */
	UPROPERTY(EditAnywhere, Category = "VoxelCharacter|Arrow", meta = (ClampMin = "0.01"))
	float WindSampleInterval = 0.1f;

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

	/** Reads the weather's wind at the arrow every WindSampleInterval (WITH_WORLD_WEATHER; zero otherwise). */
	void RefreshWind(float DeltaTime);

	FVoxelWorldBounds CachedWorldBounds;
	bool bHasWorldBounds = false;

	/** The wind on the arrow (uu/s), the weather's times the exposure. */
	FVector CachedWind = FVector::ZeroVector;

	/** Seconds until the wind is read again (read at the first tick). */
	float WindSampleRemaining = 0.f;

	/** The arrow's launch direction, the wind's reference across (zero until it first flies). */
	FVector WindHeading = FVector::ZeroVector;
};
