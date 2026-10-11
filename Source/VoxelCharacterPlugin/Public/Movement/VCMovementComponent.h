// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Core/VCTypes.h"
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

	// --- GAS Attribute Callbacks ---

	/** Called when MoveSpeedMultiplier attribute changes. */
	void OnMoveSpeedAttributeChanged(const FOnAttributeChangeData& Data);

	// --- Overrides ---
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual void PhysSwimming(float DeltaTime, int32 Iterations) override;
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
