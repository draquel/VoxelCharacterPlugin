// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Core/VCTypes.h"
#include "VoxelWorldModeTypes.h"
#include "VCMovementComponent.generated.h"

struct FGameplayAttributeData;
struct FOnAttributeChangeData;
enum class EEditSource : uint8;

/**
 * Extended CharacterMovementComponent with voxel terrain awareness.
 *
 * Caches terrain context (surface type, friction, water state) and adjusts
 * movement parameters accordingly. Queries voxel terrain data via
 * FVCVoxelNavigationHelper and automatically transitions to swimming
 * mode when water is detected.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UVCMovementComponent();

	// --- Voxel-Aware Movement ---

	/** Re-query voxel terrain data beneath the character. */
	void UpdateVoxelTerrainContext();

	/** Current cached terrain context (read by animation, audio, etc.). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement")
	const FVoxelTerrainContext& GetTerrainContext() const { return CachedTerrainContext; }

	/** Map a voxel MaterialID to a logical surface type. */
	static EVoxelSurfaceType MaterialIDToSurfaceType(uint8 MaterialID);

	/** Get friction multiplier for a given surface type. */
	static float GetSurfaceFriction(EVoxelSurfaceType Surface);

	// --- Custom Movement Mode Properties ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Voxel")
	float VoxelClimbingSpeed = 200.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Voxel")
	float VoxelSwimmingSpeedMultiplier = 0.6f;

	/** Water depth (world units) at character feet before entering swimming mode. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Voxel", meta = (ClampMin = "0.0"))
	float SwimmingEntryDepth = 40.f;

	/**
	 * Water depth below which the character exits swimming onto a walkable floor. Kept under SwimmingEntryDepth so the
	 * two do not flicker; 30 lets a swimmer stand up on a shelving beach instead of crawling until nearly dry (QA R11).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Voxel", meta = (ClampMin = "0.0"))
	float SwimmingExitDepth = 30.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Voxel")
	float VoxelSurfaceGripMultiplier = 1.0f;

	// --- Swimming Tuning ---

	/** Buoyancy while swimming. 1.0 = float at surface, <1.0 = sink slowly, >1.0 = rise faster. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Swimming", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float SwimmingBuoyancy = 1.0f;

	/** Braking deceleration while swimming. Higher = stop faster when not pressing input. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Swimming", meta = (ClampMin = "0.0"))
	float SwimmingBrakingDeceleration = 600.f;

	/** Max acceleration while swimming. Lower values give a sluggish underwater feel. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Swimming", meta = (ClampMin = "0.0"))
	float SwimmingMaxAcceleration = 256.f;

	/** Vertical acceleration when ascending (jump held) while swimming. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Swimming", meta = (ClampMin = "0.0"))
	float DiveAscendAcceleration = 300.f;

	/** Vertical acceleration when descending (crouch held) while swimming. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Swimming", meta = (ClampMin = "0.0"))
	float DiveDescendAcceleration = 250.f;

	/** Fluid drag coefficient for CalcVelocity while swimming. Higher = more resistance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Swimming", meta = (ClampMin = "0.0"))
	float SwimmingDragCoefficient = 1.5f;

	/** Passive upward force when idle/no vertical input, pushing character toward surface. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Swimming", meta = (ClampMin = "0.0"))
	float SurfaceBuoyancyStrength = 150.f;

	/**
	 * Swimming into a bank near the surface climbs onto it when a walkable ledge sits within the capsule half height
	 * plus this much above the feet (QA R11: swimmers slid along the shore instead of getting out).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Swimming", meta = (ClampMin = "0.0"))
	float ShoreClimbHeight = 50.f;

	/** Dampens upward velocity near water surface to prevent breaching. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Swimming", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SwimmingSurfaceDamping = 0.85f;

	/** Current logical surface type (derived from voxel material). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Movement|Voxel")
	EVoxelSurfaceType CurrentSurfaceType = EVoxelSurfaceType::Default;

	// --- World edge (world modes review WM-I5, decision D9) ---
	// A bounded world (an island) ends at invisible walls on its edge (R + falloff). Before them a current pushes the
	// pawn back: within WorldEdgePushBackDistance of the edge the pawn's outward speed is capped, from its full speed at
	// the band's inner side down to moving inward at WorldEdgePushBackSpeed at the edge. A swimmer heading out stalls
	// about a third of the band before the wall and drifts back when it lets go. Flying (debug, benches) is exempt.

	/** Width of the band before the world's edge where the current pushes back (world units). 0 turns it off. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|WorldEdge", meta = (ClampMin = "0.0"))
	float WorldEdgePushBackDistance = 4000.f;

	/** Speed the current carries the pawn back toward the world at the edge itself (it fades to 0 across the band). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|WorldEdge", meta = (ClampMin = "0.0"))
	float WorldEdgePushBackSpeed = 250.f;

	/**
	 * How deep the pawn is in the push-back band: 0 outside it (or in an unbounded world), 1 at the world's edge.
	 * Refreshed every tick from the pawn's position; drives the HUD hint.
	 * @return Proximity in [0, 1]
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|WorldEdge")
	float GetWorldEdgeProximity() const { return WorldEdgeProximity; }

	/** @return True while the pawn is in the push-back band before the world's edge */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|WorldEdge")
	bool IsNearWorldEdge() const { return WorldEdgeProximity > 0.f; }

	/**
	 * The voxel world's bounds as this component cached them (unbounded until the world is found).
	 * @return The bounds
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|WorldEdge")
	FVoxelWorldBounds GetWorldBounds() const { return CachedWorldBounds; }

	/**
	 * Pure: the push-back for one velocity (see WorldEdgePushBackDistance). Caps the velocity's outward component in X
	 * and Y; vertical speed and motion along or into the world are untouched.
	 * @param Bounds       The world's bounds
	 * @param Location     The pawn's position
	 * @param Velocity     Velocity to adjust
	 * @param MaxSpeed     The pawn's current max speed (the cap at the band's inner side)
	 * @param BandWidth    WorldEdgePushBackDistance
	 * @param PushSpeed    WorldEdgePushBackSpeed
	 * @param OutProximity Receives the proximity in [0, 1] (0 outside the band)
	 * @return The adjusted velocity
	 */
	static FVector ApplyWorldEdgePushBack(const FVoxelWorldBounds& Bounds, const FVector& Location, const FVector& Velocity,
		float MaxSpeed, float BandWidth, float PushSpeed, float& OutProximity);

	// --- Planet gravity (VoxelWorlds SPHERICAL_PLANET_REQUIREMENTS.md SP-F1; WORLD_MODES_REVIEW_PLAN.md §17, WM-W1) ---
	// On a planet (the voxel world's bounds are a shell) gravity points at the centre: the engine's custom gravity
	// direction (UE 5.4+), set from the position at the start of every move (PerformMovement), so the server's replay,
	// the client's prediction and NPCs all use the same function of the position. Walking, falling, floors, steps, jumps
	// and staying upright then run in the engine's gravity space; this component's own vertical code (swimming, the floor
	// line traces, the shore climb, breath, the embed resolve) uses the same frame. Heightmap worlds never set it.

	/** Local up: minus the gravity direction (radial on a planet, +Z elsewhere). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|Gravity")
	FVector GetLocalUp() const { return -GetGravityDirection(); }

	/** True when the voxel world is a planet, so gravity points at its centre. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|Gravity")
	bool HasPlanetGravity() const { return bPlanetGravity; }

	/**
	 * Point gravity at the planet centre from the current position now (every move does it too). Call after placing the
	 * pawn (spawn, teleport) so its first move and its upright pose use the new gravity. Does nothing off a planet.
	 * @return True on a planet
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Movement|Gravity")
	bool UpdatePlanetGravity();

	/**
	 * Stand the pawn upright in its gravity frame, keeping its gravity-relative yaw (after a placement on a planet). Does
	 * nothing without custom gravity.
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Movement|Gravity")
	void SnapUprightToGravity();

	/**
	 * Pure: gravity toward a planet centre.
	 * @param Location The pawn's position
	 * @param Centre The planet centre
	 * @param Fallback Returned when the two coincide
	 * @return Unit direction from Location to Centre
	 */
	static FVector ComputePlanetGravityDirection(const FVector& Location, const FVector& Centre, const FVector& Fallback);

	// --- GAS Attribute Callbacks ---

	/** Called when MoveSpeedMultiplier attribute changes. */
	void OnMoveSpeedAttributeChanged(const FOnAttributeChangeData& Data);

	// --- Overrides ---
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual void PhysSwimming(float DeltaTime, int32 Iterations) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void FindFloor(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult, bool bCanUseCachedLocation, const FHitResult* DownwardSweepResult = nullptr) const override;
	virtual bool IsValidLandingSpot(const FVector& CapsuleLocation, const FHitResult& Hit) const override;

	/** Override to use voxel water depth instead of physics water volumes. */
	virtual float ImmersionDepth() const override;

	/**
	 * Lift the pawn out of terrain that closed around it -- a build brush or stamp filled the space it stands in,
	 * so its capsule is inside solid voxels and would drop through the inside of the new geometry. Scans the
	 * edit-merged voxels upward for headroom and snaps onto the new surface. Runs on the authority only.
	 * Called automatically when new collision lands near the pawn shortly after an edit near it.
	 * @return True if the pawn was embedded and has been moved
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Movement|Voxel")
	bool ResolveTerrainEmbedding();

	/**
	 * True while an edit near the pawn keeps the embed check armed: new collision landing near the pawn then runs
	 * ResolveTerrainEmbedding. A brush arms it when its sphere comes near the capsule, an edit with no centre (a POI's
	 * terraform, a dungeon stamp, undo, a clear, a load) when the voxels it changed do. Authority only.
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|Voxel")
	bool IsEmbedCheckArmed() const;

	/**
	 * Pure: the world box of chunk-local voxels (the bounds UVoxelEditManager::GetBroadcastEditBounds gives). Chunks are
	 * world-aligned boxes on every world mode, so a planet's is the same box.
	 * @param ChunkCoord  The chunk
	 * @param LocalMin    Lowest chunk-local voxel on each axis (inclusive)
	 * @param LocalMax    Highest chunk-local voxel on each axis (inclusive)
	 * @param WorldOrigin The voxel world's origin
	 * @param ChunkSize   Voxels per chunk edge
	 * @param VoxelSize   World units per voxel
	 * @return The box from LocalMin's lower corner to LocalMax's upper corner
	 */
	static FBox GetEditedVoxelWorldBox(const FIntVector& ChunkCoord, const FIntVector& LocalMin, const FIntVector& LocalMax,
		const FVector& WorldOrigin, int32 ChunkSize, float VoxelSize);

	/** Max voxels scanned upward for headroom when lifting an embedded pawn. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Movement|Voxel", meta = (ClampMin = "1"))
	int32 EmbedResolveMaxScanVoxels = 64;

	// --- Sprint (QA R10) ---

	/** Walking speed multiplier while sprinting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Sprint", meta = (ClampMin = "1.0"))
	float SprintSpeedMultiplier = 1.6f;

	/** Stamina spent per second of sprinting (server). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Sprint", meta = (ClampMin = "0.0"))
	float SprintStaminaPerSecond = 15.f;

	/** Stamina regained per second once not sprinting for StaminaRegenDelay (server). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Sprint", meta = (ClampMin = "0.0"))
	float StaminaRegenPerSecond = 12.f;

	/** Seconds after sprinting before stamina starts to come back. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Sprint", meta = (ClampMin = "0.0"))
	float StaminaRegenDelay = 1.f;

	/** After running out, sprinting stays off until stamina refills to this much (no stutter at zero). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Sprint", meta = (ClampMin = "0.0"))
	float SprintResumeStamina = 15.f;

	/**
	 * Ask to sprint (the held sprint key). Travels with the saved move, so clients predict it.
	 * @param bWantsSprint True while the key is held
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Movement|Sprint")
	void SetSprinting(bool bWantsSprint);

	/**
	 * Sprinting right now: wanted, walking on the ground, trying to move, not crouched and not out of stamina.
	 * @return True while the sprint speed applies
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|Sprint")
	bool IsSprinting() const;

	/** Sprint input state (saved-move flag FLAG_Custom_0). */
	bool bWantsToSprint = false;

	/**
	 * Hold the swim-rise key (the jump key while swimming). ACharacter drops bPressedJump after one frame, so a held
	 * key needs its own state. Travels with the saved move.
	 * @param bWantsAscend True while the key is held
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Movement|Swimming")
	void SetWantsToAscend(bool bWantsAscend);

	/** Swim-rise input state (saved-move flag FLAG_Custom_1). */
	bool bWantsToAscend = false;

	// --- Debug flight (spherical Phase 6, WM-BP2, decision D20: QA tooling, not gameplay) ---

	/**
	 * QA flight (vox.Fly, vox.TPLatLon ... hold=1): MOVE_Flying steered in the gravity frame. Movement input moves across
	 * local up as on the ground (Input_Move and vox.AutoRun give no other); the climb (SetFlightClimb, or the jump / crouch
	 * keys held) moves along local up at up to MaxFlySpeed; with no climb the distance from the planet centre (Z on a
	 * heightmap world) is held, so a flight across a planet keeps its altitude. The body stays upright in its gravity and
	 * water never turns it into a swimmer. Stopping drops it into MOVE_Falling; any other mode change ends it. Not
	 * replicated (standalone and listen-server QA).
	 * @param bEnable Fly, or stop flying
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Movement|Debug")
	void SetDebugFlight(bool bEnable);

	/** @return True while in debug flight */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|Debug")
	bool IsDebugFlying() const { return bDebugFlight; }

	/**
	 * Hands-free climb in debug flight: -1 (down) to 1 (up) of MaxFlySpeed along local up; 0 holds the altitude reached.
	 * @param Climb The climb rate as a fraction of MaxFlySpeed (clamped to [-1, 1])
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Movement|Debug")
	void SetFlightClimb(float Climb);

	/** @return The hands-free climb (-1..1) */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|Debug")
	float GetFlightClimb() const { return FlightClimb; }

	/** @return The level debug flight holds while not climbing: distance from the planet centre (Z on a heightmap world) */
	double GetFlightHoldLevel() const { return FlightHoldLevel; }

	/**
	 * Debug flight: hold this level instead (no climb), reached at up to MaxFlySpeed along local up; vox.FlyTo moves it each
	 * frame along its altitude profile (spherical Phase 6, WM-BP10).
	 * @param Level Distance from the planet centre (Z on a heightmap world)
	 */
	void SetFlightHoldLevel(double Level)
	{
		FlightClimb = 0.f;
		FlightHoldLevel = Level;
	}

	// --- Breath (QA R11) ---

	/** Breath lost per second while the head is underwater (Breath is in seconds of air). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Breath", meta = (ClampMin = "0.0"))
	float BreathLossPerSecond = 1.f;

	/** Breath regained per second with the head above water. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Breath", meta = (ClampMin = "0.0"))
	float BreathRegenPerSecond = 10.f;

	/** Pure damage per second once out of breath underwater. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Breath", meta = (ClampMin = "0.0"))
	float DrowningDamagePerSecond = 10.f;

	/** Head height above the capsule centre, as a fraction of the half height, used for the underwater check. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Breath", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeadHeightFraction = 0.8f;

	/** True while the head is underwater (refreshed ten times a second on the authority). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|Breath")
	bool IsHeadUnderwater() const { return bHeadUnderwater; }

	virtual float GetMaxSpeed() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

protected:
	/** Cached terrain data, refreshed every TerrainCacheDuration seconds. */
	FVoxelTerrainContext CachedTerrainContext;

	/** Time accumulator for terrain cache refresh. */
	float TerrainContextCacheTimer = 0.f;

	/** How often to re-query voxel terrain (seconds). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Movement|Voxel")
	float TerrainCacheDuration = 0.1f;

	/** Base MaxWalkSpeed before GAS multiplier. Captured on BeginPlay. */
	float BaseMaxWalkSpeed = 0.f;

	/** The voxel world's bounds (WM-I5), cached once the chunk manager is found; unbounded until then. */
	FVoxelWorldBounds CachedWorldBounds;
	bool bHasWorldBounds = false;

	/** Push-back band proximity, 0..1 (see GetWorldEdgeProximity). */
	float WorldEdgeProximity = 0.f;

	/** Cache the world's bounds from the chunk manager if not done yet (and whether it is a planet). */
	void RefreshWorldBounds();

	/** The voxel world is a planet (its bounds are a shell): gravity points at CachedWorldBounds.Center (WM-W1). */
	bool bPlanetGravity = false;

	/** Planets: gravity from the position before the move (WM-W1), then the engine's movement. */
	virtual void PerformMovement(float DeltaTime) override;

	/**
	 * Planets: a swimmer stays upright in its gravity as well as a walker or a faller. The engine turns pitch and roll at
	 * RotationRate's zero rates unless the pawn should remain vertical, so as gravity turns under a swimmer nothing would
	 * re-upright it (it lay on its side after a quarter of the planet). Heightmap worlds: the engine's rule.
	 */
	virtual bool ShouldRemainVertical() const override;

	/** Debug flight (SetDebugFlight): the gravity-frame flight with its climb and altitude hold; else the engine's. */
	virtual void PhysFlying(float DeltaTime, int32 Iterations) override;

	/** Ends debug flight when another mode takes over (a landing, a teleport's settle, death). */
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

	/** Debug flight state (SetDebugFlight). */
	bool bDebugFlight = false;

	/** Hands-free climb, -1..1 of MaxFlySpeed (SetFlightClimb). */
	float FlightClimb = 0.f;

	/** The level debug flight holds with no climb: distance from the planet centre, or Z on a heightmap world. */
	double FlightHoldLevel = 0.0;

	/** The flight level of a position (see FlightHoldLevel). */
	double GetFlightLevel(const FVector& Location) const;

	/** Speed across local up, squared (Velocity.SizeSquared2D() for the default gravity). */
	double GetHorizontalSpeedSquared(const FVector& InVelocity) const;

	/** A vector's unit direction across local up (GetSafeNormal2D() for the default gravity). */
	FVector GetHorizontalDirection(const FVector& InVector) const;

	/** Base MaxAcceleration before swimming override. Captured on BeginPlay. */
	float BaseMaxAcceleration = 0.f;

	/** Current GAS speed multiplier (default 1.0). */
	float GASSpeedMultiplier = 1.f;

	/** Was the character grounded last frame? Used for floor grace logic. */
	bool bWasGroundedLastFrame = false;

	/** Grace period remaining when floor temporarily disappears. */
	mutable float FloorGraceTimer = 0.f;

	/** Time since a real (non-synthesized) floor was last detected. Used to allow
	 *  grace to re-trigger on cubic terrain where floor contact is intermittent. */
	mutable float TimeSinceLastRealFloor = 0.f;

	/** Max grace period (seconds) to maintain grounded state when floor vanishes.
	 *  Higher values prevent falling thrash on cubic terrain. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Movement|Voxel")
	float FloorGraceDuration = 0.35f;

	/** Window (seconds) after last real floor contact during which grace can re-trigger.
	 *  Prevents the one-shot problem on cubic terrain where floor rapidly alternates. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Movement|Voxel")
	float RecentGroundedWindow = 1.0f;

	/** Max distance below capsule bottom to check for floor when granting grace.
	 *  If no floor is found within this distance, it's a real ledge (not a trimesh
	 *  artifact) and the character should fall naturally instead of getting grace. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Movement|Voxel")
	float GraceHeightThreshold = 30.f;

	virtual void BeginPlay() override;

	/** Delegate handler for chunk modification (invalidates terrain cache, arms the embed check). */
	void OnVoxelChunkModified(const FIntVector& ChunkCoord, EEditSource Source, const FVector& EditCenter, float EditRadius);

	/** Bind OnCollisionReady once the collision manager exists (it can be created after this component). */
	void TryBindCollisionReady();

	/** New collision landed for a chunk: if an edit near the pawn armed the check, lift it out if embedded. */
	void OnVoxelCollisionReady(const FIntVector& ChunkCoord);

	/** World time until which an edit near the pawn keeps the embed check armed (negative = disarmed). */
	double EmbedCheckArmedUntil = -1.0;

	/** OnCollisionReady is bound. */
	bool bBoundCollisionReady = false;

	/** Seconds until the next attempt to bind OnCollisionReady. */
	float CollisionBindRetryTimer = 0.f;

	/** Ran out of stamina: sprint stays off until stamina refills to SprintResumeStamina (every machine). */
	bool bSprintExhausted = false;

	/** Seconds since the last sprinting tick (regeneration delay; authority). */
	float TimeSinceSprint = 0.f;

	/** Stamina change accumulated between attribute writes (written ~10 times a second; authority). */
	float PendingStaminaDelta = 0.f;
	float StaminaWriteTimer = 0.f;

	/** Spend stamina while sprinting, regenerate after a delay (authority). */
	void TickStamina(float DeltaTime);

	/**
	 * Swimming into a bank (Hit): if the swimmer is near the surface and a walkable ledge is within reach ahead, move onto
	 * it and switch to walking.
	 * @return True if the swimmer climbed out
	 */
	bool TryClimbOutOfWater(const FHitResult& Hit);

	/** Drain breath with the head underwater, refill above, drown at zero (authority, player pawns). */
	void TickBreath(float DeltaTime);

	bool bHeadUnderwater = false;
	float BreathTimer = 0.f;
	float DrowningTimer = 0.f;

	/** Owner's current stamina, or a negative value when it has no stamina attribute. */
	float GetOwnerStamina() const;
};
