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

/** The gaits the gait table has a row for (the character refinement plan's C4, D75). */
UENUM(BlueprintType)
enum class EVCGait : uint8
{
	Walk,
	Run,
	Sprint,
	Crouch
};

/** The capsule's rotation mode: toward where it moves, or toward the view (the bow drawn, first person). */
UENUM(BlueprintType)
enum class EVCRotationMode : uint8
{
	OrientToMovement,
	Strafe
};

/** The movement presets vc.Move.Preset swaps between (C4's A/B). */
UENUM()
enum class EVCMovePreset : uint8
{
	/** The engine defaults the player ran on before C4: 600 every direction, 2048, 4096, friction 8, 500 degrees a second. */
	Legacy,
	/** The Game Animation Sample's per-frame tuning (SandboxCharacter_CMC), the clips' own numbers. */
	GASP,
	/** GASP's numbers tuned on the character test lane (the plan's C4 result: GASP's verbatim so far; the next pass's changes go here). */
	Tuned
};

/**
 * One gait's movement numbers (D75): the speed caps by direction, the acceleration, the braking and the friction.
 * The movement component applies the row of the gait in force every tick, inside the predicted movement.
 */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCGaitSettings
{
	GENERATED_BODY()

	/** Max speed moving forward (uu/s): the only cap in orient mode, where the body faces where it goes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gait", meta = (ClampMin = "0.0"))
	float MaxSpeedForward = 500.f;

	/** Max speed sideways in strafe mode (uu/s); the input's angle to the facing picks between forward, this and backward. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gait", meta = (ClampMin = "0.0"))
	float MaxSpeedStrafe = 350.f;

	/** Max speed backward in strafe mode (uu/s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gait", meta = (ClampMin = "0.0"))
	float MaxSpeedBackward = 300.f;

	/** Max acceleration (uu/s^2); the sprint's falls toward FVCMovementTable::SprintTopSpeedAcceleration with speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gait", meta = (ClampMin = "0.0"))
	float MaxAcceleration = 800.f;

	/** Braking deceleration while there is input (over the cap: a sprint released, a strafe's lower cap), uu/s^2. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gait", meta = (ClampMin = "0.0"))
	float BrakingDecelerationMoving = 500.f;

	/** Braking deceleration with no input (stopping), uu/s^2. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gait", meta = (ClampMin = "0.0"))
	float BrakingDecelerationStopping = 2000.f;

	/** Ground friction on dry, plain ground; the surface and the wetness scale it as they scaled the constant before. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gait", meta = (ClampMin = "0.0"))
	float GroundFriction = 5.f;

	/**
	 * The engine's braking friction factor (its braking friction is the ground friction times this). GASP's is 0: a stop
	 * is the braking deceleration alone, so its length is the same on every surface.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gait", meta = (ClampMin = "0.0"))
	float BrakingFrictionFactor = 0.f;

	FVCGaitSettings() = default;
	FVCGaitSettings(float Forward, float Strafe, float Backward, float Acceleration, float BrakingMoving, float BrakingStopping,
		float Friction, float BrakingFactor)
		: MaxSpeedForward(Forward), MaxSpeedStrafe(Strafe), MaxSpeedBackward(Backward), MaxAcceleration(Acceleration)
		, BrakingDecelerationMoving(BrakingMoving), BrakingDecelerationStopping(BrakingStopping), GroundFriction(Friction)
		, BrakingFrictionFactor(BrakingFactor)
	{
	}

	/**
	 * The speed cap for an input at an angle to the facing: forward within 45 degrees, the strafe speed at 90, backward
	 * from 135, linear between (GASP's Curve_StrafeSpeedMap).
	 * @param AngleDegrees The input's (or the velocity's) angle to the body's forward about local up, any sign
	 * @return The cap, uu/s
	 */
	float MaxSpeedAtAngle(float AngleDegrees) const;

	/** @return The row as a C++ initializer, "{ 500, 350, 300, 800, 500, 2000, 5, 1 }" (vc.Move.Print, to commit) */
	FString ToString() const;

	bool operator==(const FVCGaitSettings& Other) const;
	bool operator!=(const FVCGaitSettings& Other) const { return !(*this == Other); }
};

/**
 * The player's movement table (D75): a row per gait, the rotation rates by mode, and the sprint's ramps. GASP's numbers
 * are the defaults; FVCMovementTable::Preset builds each of vc.Move.Preset's tables.
 */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCMovementTable
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gaits")
	FVCGaitSettings Walk = FVCGaitSettings(200.f, 180.f, 150.f, 800.f, 500.f, 2000.f, 5.f, 0.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gaits")
	FVCGaitSettings Run = FVCGaitSettings(500.f, 350.f, 300.f, 800.f, 500.f, 2000.f, 5.f, 0.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gaits")
	FVCGaitSettings Sprint = FVCGaitSettings(700.f, 700.f, 700.f, 800.f, 500.f, 2000.f, 5.f, 0.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Gaits")
	FVCGaitSettings Crouch = FVCGaitSettings(225.f, 200.f, 180.f, 800.f, 500.f, 2000.f, 5.f, 0.f);

	/** Yaw rate on the ground in orient mode (degrees a second; negative = instant: the graph turns the body, GASP). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rotation")
	float GroundOrientRotationRate = -1.f;

	/** Yaw rate on the ground in strafe mode (negative = instant). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rotation")
	float GroundStrafeRotationRate = -1.f;

	/** Yaw rate in the air (GASP: 200). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rotation")
	float AirRotationRate = 200.f;

	/** Yaw rate swimming and in debug flight (the swim's tuning is otherwise untouched by the table). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Rotation")
	float SwimRotationRate = 500.f;

	/** The sprint's acceleration falls from its row's to this between the two speeds of SprintAccelerationRamp (GASP: 800 to 300). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sprint", meta = (ClampMin = "0.0"))
	float SprintTopSpeedAcceleration = 300.f;

	/** The speeds (uu/s) the sprint's acceleration ramps between. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sprint")
	FVector2D SprintAccelerationRamp = FVector2D(300.0, 700.0);

	/** The sprint's ground friction falls from its row's to this between the two speeds of SprintFrictionRamp (GASP: 5 to 3). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sprint", meta = (ClampMin = "0.0"))
	float SprintTopSpeedFriction = 3.f;

	/** The speeds (uu/s) the sprint's friction ramps between. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sprint")
	FVector2D SprintFrictionRamp = FVector2D(0.0, 500.0);

	/** @return The row of a gait */
	const FVCGaitSettings& Row(EVCGait Gait) const;
	FVCGaitSettings& Row(EVCGait Gait);

	/**
	 * The sprint's acceleration and friction at a speed (the ramps above).
	 * @param Speed The speed across local up, uu/s
	 * @return The acceleration, uu/s^2
	 */
	float SprintAccelerationAtSpeed(float Speed) const;
	float SprintFrictionAtSpeed(float Speed) const;

	/** @return A preset's whole table */
	static FVCMovementTable Preset(EVCMovePreset Preset);

	/** @return The table as the rows and rates vc.Move.Print logs, one line each */
	FString ToString() const;
};

/** The engine values one tick of the gait table sets (UVCMovementComponent::ComputeGaitValues). */
struct FVCGaitValues
{
	/** Whether the row applies (on the ground or in the air); swimming and flying take the yaw rate alone. */
	bool bRowApplies = false;
	float MaxSpeed = 0.f;
	float MaxAcceleration = 0.f;
	float BrakingDeceleration = 0.f;
	float GroundFriction = 0.f;
	float BrakingFrictionFactor = 0.f;
	float RotationRate = 0.f;
};

/**
 * Extended CharacterMovementComponent with voxel terrain awareness.
 *
 * Caches terrain context (surface type, friction, water state) and adjusts
 * movement parameters accordingly. Queries voxel terrain data via
 * FVCTerrainQueryHelper and automatically transitions to swimming
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
	const FVCTerrainContext& GetTerrainContext() const { return CachedTerrainContext; }

	/** Map a voxel MaterialID to a logical surface type. */
	static EVCSurfaceType MaterialIDToSurfaceType(uint8 MaterialID);

	/** Get friction multiplier for a given surface type. */
	static float GetSurfaceFriction(EVCSurfaceType Surface);

	// --- Custom Movement Mode Properties ---

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

	// --- Block steps (world modes review WM-C7, decision D29) ---

	/**
	 * In a cubic world, walk up a one-block rise without a jump: the step height becomes one block (voxel size plus
	 * BlockStepMargin), for players and NPCs alike, so two blocks stay a wall (the jump's apex is about 113 uu). The
	 * surface navigation uses the same rule (FVoxelSurfaceNavParams::bBlockSteps). Smooth worlds keep MaxStepHeight as set.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Voxel")
	bool bStepUpOneBlock = true;

	/** Added to the voxel size for the one-block step, so a block's top is reached whatever the trimesh's rounding. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Voxel", meta = (ClampMin = "0.0"))
	float BlockStepMargin = 5.f;

	/**
	 * The step height for one block of a voxel size (VoxelSize + Margin).
	 *
	 * @param VoxelSize World units per voxel
	 * @param Margin BlockStepMargin
	 * @return MaxStepHeight for a cubic world
	 */
	static float GetBlockStepHeight(float VoxelSize, float Margin) { return VoxelSize + Margin; }

	/** Whether this walker steps whole blocks (a cubic world, once its voxel world is known). The camera eases those steps. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement")
	bool IsSteppingBlocks() const { return bSteppingBlocks; }

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
	EVCSurfaceType CurrentSurfaceType = EVCSurfaceType::Default;

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

	// --- Weather (the weather plan's P4) ---
	// The wind leans on the pawn and wet ground loses grip. Both read the weather at the pawn once per terrain-context
	// refresh (UpdateVoxelTerrainContext, WorldWeatherPlugin's GetConditionsAt) and apply inside the movement simulation,
	// so the client's prediction and the server agree (the sync point is the same on both; the trace's exposure may
	// differ a frame). Without WorldWeatherPlugin (WITH_WORLD_WEATHER 0) nothing changes. Flying is exempt, as the edge is.

	/** The fraction of the wind's speed the pawn drifts at in the open (a storm's 900 uu/s carries it at 90). 0 turns the push off. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Weather", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WindPushFactor = 0.1f;

	/** How much of the ground's grip full wetness takes away (the ground friction's factor is 1 - wetness x this). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Weather", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float WetGripFactor = 0.4f;

	/**
	 * Seconds in the air before the wind's full drift takes the pawn (a jump, a real drop): a floor lost for a frame or
	 * two on rough voxel ground does not count, so an idle pawn stays put.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Weather", meta = (ClampMin = "0.0"))
	float WindAirborneDelay = 0.35f;

	/** The wind at the pawn as last read (world uu/s, tangent to the local up; zero without the weather plugin). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|Weather")
	FVector GetWeatherWind() const { return CachedWeatherWind; }

	/** The pawn's exposure to the sky as last read (1 in the open, 0 sheltered). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|Weather")
	float GetWeatherExposure() const { return CachedWeatherExposure; }

	/** The ground's wetness under the pawn as last read (0 dry, 1 soaked). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|Weather")
	float GetWeatherWetness() const { return CachedWeatherWetness; }

	/** Seconds of full rain (rate 1, exposed) that soak the pawn from dry (the wet level 0 to 1; the weather plan's P5). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Weather", meta = (ClampMin = "1.0"))
	float WetSoakSeconds = 12.f;

	/** Seconds a soaked pawn takes to dry with nothing falling on it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Weather", meta = (ClampMin = "1.0"))
	float WetDrySeconds = 90.f;

	/** The stamina's regeneration while State.Weather.Wet is on, as a fraction of the dry rate. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Weather", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WetStaminaRegenMultiplier = 0.5f;

	/** How soaked the pawn is (0 dry, 1 soaked): the rain on it over WetSoakSeconds, a swim at once, drying over WetDrySeconds. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|Weather")
	float GetWeatherWetLevel() const { return WeatherWetLevel; }

	/** The air temperature at the pawn (C) from the last weather read (15 without the weather plugin). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|Weather")
	float GetWeatherTemperatureC() const { return CachedWeatherTemperatureC; }

	/**
	 * Pure: one step of the wet level. Swimming soaks at once; otherwise the rain on the pawn (its rate times the
	 * exposure, snow at a third) raises the level over SoakSeconds and nothing falling dries it over DrySeconds.
	 * @param Level       The level before the step (0..1)
	 * @param RateOnPawn  Precipitation reaching the pawn (0..1)
	 * @param bSwimming   In the water
	 * @param DeltaTime   The step, seconds
	 * @param SoakSeconds WetSoakSeconds
	 * @param DrySeconds  WetDrySeconds
	 * @return The level after the step (0..1)
	 */
	static float AdvanceWetLevel(float Level, float RateOnPawn, bool bSwimming, float DeltaTime, float SoakSeconds, float DrySeconds);

	/**
	 * Pure: the wind's drift on the pawn, Wind x Factor x Exposure, scaled by how much the pawn moves of itself: a
	 * standing pawn is not moved (it can idle anywhere in any wind), a walking pawn is slowed against the wind and
	 * helped along it in proportion to its speed (full at MaxSpeed), and a pawn in the air is carried at the full drift.
	 * CalcVelocity takes the previous tick's drift off the velocity before the braking and the friction (which would
	 * otherwise eat it) and puts this tick's back on after them. The wind itself blends over a front's transition, so
	 * the drift needs no ramp of its own. A zero wind, factor or exposure is no drift.
	 * @param Wind      The wind at the pawn, world uu/s
	 * @param Factor    WindPushFactor
	 * @param Exposure  The pawn's exposure to the sky (0..1)
	 * @param OwnSpeed  The pawn's own speed this tick (after the braking and the friction, before the drift), uu/s
	 * @param MaxSpeed  The speed at which the drift is full (GetMaxSpeed)
	 * @param bAirborne In the air past WindAirborneDelay: the full drift whatever the speed
	 * @return The drift, world uu/s
	 */
	static FVector ComputeWindDrift(const FVector& Wind, float Factor, float Exposure, float OwnSpeed, float MaxSpeed, bool bAirborne);

	// --- Planet gravity (VoxelWorlds SPHERICAL_PLANET_REQUIREMENTS.md SP-F1; WORLD_MODES_REVIEW_PLAN.md §17, WM-W1) ---
	// On a planet (the voxel world's bounds are a shell) gravity points at the centre: the engine's custom gravity
	// direction (UE 5.4+), set from the position at the start of every move (PerformMovement), so the server's replay,
	// the client's prediction and NPCs all use the same function of the position. Walking, falling, floors, steps, jumps
	// and staying upright then run in the engine's gravity space; this component's own vertical code (swimming, the floor
	// line traces, the shore climb, breath, the embed resolve) uses the same frame. Heightmap worlds never set it.

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

	// --- The gait table (the character refinement plan's C4, decision D75) ---
	// The player plays the Game Animation Sample's motion-matching graph, whose clips were made for GASP's per-frame
	// movement tuning: a speed cap per gait and direction, a low acceleration, a soft braking with input and a firm one
	// stopping, a friction of 5, and an instant capsule yaw on the ground (the graph turns the body: offset root bone,
	// steering, orientation warping). ApplyGaitTable sets the engine's values from the table every tick at the start of
	// PerformMovement, so the server's replay and the client's prediction compute the same numbers from the same
	// saved-move state (the sprint, the walk and the crouch flags, the input) and the rotation mode both sides set.
	// NPCs keep their own numbers (AVCNPCCharacterBase turns the table off). The swim and flight tuning is untouched.

	/** Apply the gait table every tick (players). Off, the component keeps the values set on it (NPCs, G73's budget). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Gait")
	bool bUseGaitTable = true;

	/** The rows and rates in force (vc.Move.Preset swaps the whole table, vc.Move.Set one value, vc.Move.Print logs it). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Gait")
	FVCMovementTable GaitTable;

	/**
	 * The gait in force: crouched, else sprinting (wanted and not out of breath), else walking (SetWalking), else the run.
	 * @return The gait
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|Gait")
	EVCGait GetGait() const;

	/** @return The rotation mode the engine's flags say: strafe when the body turns toward the view */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|Gait")
	EVCRotationMode GetRotationMode() const { return bOrientRotationToMovement ? EVCRotationMode::OrientToMovement : EVCRotationMode::Strafe; }

	/** @return The table's row of the gait in force */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|Gait")
	const FVCGaitSettings& GetGaitRow() const { return GaitTable.Row(GetGait()); }

	/**
	 * Ask to walk (a walk gait; no input is bound to it yet, §8 of the plan: the walk row is reachable by code and the
	 * console). Travels with the saved move.
	 * @param bWantsWalk True while walking is wanted
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Movement|Gait")
	void SetWalking(bool bWantsWalk);

	/** Walk input state (saved-move flag FLAG_Custom_2). */
	bool bWantsToWalk = false;

	/**
	 * Set the engine's movement values from the gait table for this tick: the row's cap by the rotation mode and the
	 * input's angle to the facing, the sprint's acceleration and friction by speed, the braking by whether there is
	 * input, the friction scaled by the surface and the wetness, the yaw rate by the mode. PerformMovement calls it;
	 * public for the tests and the debug overlay. Does nothing with the table off, or swimming and flying.
	 */
	void ApplyGaitTable();

	/**
	 * Pure: the values one tick of the table sets (ApplyGaitTable's arithmetic). The cap is the row's forward speed in
	 * orient mode, else the row's speed at the angle between the movement and the facing (the velocity's when moving,
	 * the input's when starting); the sprint's acceleration and friction follow the table's ramps by speed; the braking
	 * is the row's moving value with input and its stopping value without; the friction is scaled by the surface and
	 * the wetness; the yaw rate is the mode's on the ground, the air's in the air, the swim's otherwise, where the row
	 * does not apply.
	 * @param Table           The table
	 * @param Gait            The gait in force
	 * @param Mode            The rotation mode in force
	 * @param bOnGround       Walking (IsMovingOnGround)
	 * @param bInAir          Falling
	 * @param Velocity        The velocity, world
	 * @param InAcceleration  The input acceleration, world (zero: no input)
	 * @param Forward         The body's forward
	 * @param Up              Local up
	 * @param SpeedMultiplier The GAS move speed multiplier
	 * @param FrictionScale   The surface and wetness scale on the friction
	 * @return The values
	 */
	static FVCGaitValues ComputeGaitValues(const FVCMovementTable& Table, EVCGait Gait, EVCRotationMode Mode, bool bOnGround, bool bInAir,
		const FVector& Velocity, const FVector& InAcceleration, const FVector& Forward, const FVector& Up, float SpeedMultiplier, float FrictionScale);

	/**
	 * Pure: the angle the strafe speed map reads: the velocity's angle to the forward about up when moving, the input's
	 * when starting from rest, 0 with neither.
	 * @param Velocity       The velocity
	 * @param InAcceleration The input acceleration
	 * @param Forward        The body's forward
	 * @param Up             Local up
	 * @return Degrees, 0 straight ahead
	 */
	static float MovementAngleToFacing(const FVector& Velocity, const FVector& InAcceleration, const FVector& Forward, const FVector& Up);

	/**
	 * Pure: the angle between two directions about an up axis (the strafe map's input).
	 * @param Forward The body's forward
	 * @param Direction The velocity or the input
	 * @param Up Local up
	 * @return Degrees in [-180, 180], positive to the right; 0 when either lies along Up
	 */
	static float AngleAboutUp(const FVector& Forward, const FVector& Direction, const FVector& Up);

	// --- The face-aim flip, eased (C4) ---
	// A bow's draw turns the body toward the view (AVCCharacterBase::SetFacesAim): the capsule's rotation mode flips from
	// orient to strafe. Flipped in one frame with an instant yaw rate, the capsule snapped up to 180 degrees under the
	// mesh. Now the flip is a short turn: the capsule turns toward the new mode's target (the view, or where it moves)
	// at AimTurnRate, and the flags change once the body is within AimTurnTolerance of it or AimTurnMaxTime has passed,
	// so the mode the graph sees changes when the body already faces the new way.

	/**
	 * Start turning the body toward the view (a draw) or back toward where it moves (the release): the eased flip above.
	 * Called by the character on the machine that draws and on the server. Nothing in first person (the yaw follows the
	 * controller there).
	 * @param bFaceAim Whether the body is to face the view
	 */
	void SetFacesAim(bool bFaceAim);

	/** @return True while the eased flip turns the body and the flags are still the old mode's */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement|Gait")
	bool IsAimTurnPending() const { return bAimTurnPending; }

	/** @return The mode the body is turning to (or has): true toward the view */
	bool WantsToFaceAim() const { return bFacesAimTarget; }

	/** Degrees from the new mode's target within which the flip completes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Gait", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float AimTurnTolerance = 15.f;

	/** Seconds after which the flip completes whatever the angle left. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Gait", meta = (ClampMin = "0.0"))
	float AimTurnMaxTime = 0.5f;

	/** The yaw rate of the eased turn (degrees a second). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Gait", meta = (ClampMin = "1.0"))
	float AimTurnRate = 540.f;

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
	 * The flight level of a position (what FlightHoldLevel holds): its distance from the planet centre, its Z on a heightmap
	 * world. The one definition (vox.FlyTo reads it; code review CR2, FW-20).
	 * @param Location World position
	 * @return The level
	 */
	double GetFlightLevel(const FVector& Location) const;

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
	FVCTerrainContext CachedTerrainContext;

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

	/** The weather at the pawn as UpdateVoxelTerrainContext last read it (P4). */
	FVector CachedWeatherWind = FVector::ZeroVector;
	float CachedWeatherExposure = 1.f;
	float CachedWeatherWetness = 0.f;
	float CachedWeatherTemperatureC = 15.f;
	float WeatherWetLevel = 0.f;
	double LastWeatherReadSeconds = -1.0;
	/** The wind drift on the velocity since the last CalcVelocity (ComputeWindDrift), taken off before the next one's braking. */
	FVector WindDrift = FVector::ZeroVector;
	/** Seconds the pawn has been falling (0 on the ground): the full drift past WindAirborneDelay. */
	float WindAirborneSeconds = 0.f;

	/** Push-back band proximity, 0..1 (see GetWorldEdgeProximity). */
	float WorldEdgeProximity = 0.f;

	/** Cache the world's bounds from the chunk manager if not done yet (and whether it is a planet). */
	void RefreshWorldBounds();

	/**
	 * Once the voxel world is initialized, set the step height for its surface: one block in a cubic world when
	 * bStepUpOneBlock is on (world modes review WM-C7, D29), else the smooth step left as it is.
	 */
	void RefreshBlockStepHeight();

	/** RefreshBlockStepHeight has seen an initialized voxel world. */
	bool bBlockStepResolved = false;

	/** The step height is one block (a cubic world). */
	bool bSteppingBlocks = false;

	/** The voxel world is a planet (its bounds are a shell): gravity points at CachedWorldBounds.Center (WM-W1). */
	bool bPlanetGravity = false;

	/** Planets: gravity from the position before the move (WM-W1), then the gait table's values (C4), then the engine's movement. */
	virtual void PerformMovement(float DeltaTime) override;

	/**
	 * Planets: a swimmer stays upright in its gravity as well as a walker or a faller. The engine turns pitch and roll at
	 * RotationRate's zero rates unless the pawn should remain vertical, so as gravity turns under a swimmer nothing would
	 * re-upright it (it lay on its side after a quarter of the planet). Heightmap worlds: the engine's rule.
	 */
	virtual bool ShouldRemainVertical() const override;

	/** Debug flight (SetDebugFlight): the gravity-frame flight with its climb and altitude hold; else the engine's. */
	virtual void PhysFlying(float DeltaTime, int32 Iterations) override;

	/**
	 * Ends debug flight when another mode takes over (a landing, a teleport's settle, death); restores the ground's
	 * acceleration on any way out of the water (G154: a flight or a teleport from the water kept the swim's 256).
	 */
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;

public:
	/** The eased face-aim flip's turn while it is pending (C4), else the engine's rotation. Public as the engine's is. */
	virtual void PhysicsRotation(float DeltaTime) override;

protected:
	/**
	 * One frame of the eased flip: turns the capsule toward the new mode's target at AimTurnRate about local up and
	 * completes the flip (the flags) within AimTurnTolerance or after AimTurnMaxTime.
	 * @return True when it turned the body this frame (the engine's rotation is skipped), false once the flip is complete
	 */
	bool AdvanceAimTurn(float DeltaTime);

	/** The mode the eased flip is heading to (SetFacesAim's argument) and whether the flags still hold the old one. */
	bool bFacesAimTarget = false;
	bool bAimTurnPending = false;
	float AimTurnElapsed = 0.f;

	/** The surface and the wetness as a factor on the table's friction (and on the legacy constant 8). */
	float GroundFrictionScale = 1.f;

	/** BeginPlay: take the table vc.Move.Preset / vc.Move.Set left for this session, so a respawned pawn keeps the A/B's. */
	void ApplySessionTableOverride();

	/** Debug flight state (SetDebugFlight). */
	bool bDebugFlight = false;

	/** Hands-free climb, -1..1 of MaxFlySpeed (SetFlightClimb). */
	float FlightClimb = 0.f;

	/** The level debug flight holds with no climb: distance from the planet centre, or Z on a heightmap world. */
	double FlightHoldLevel = 0.0;

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
