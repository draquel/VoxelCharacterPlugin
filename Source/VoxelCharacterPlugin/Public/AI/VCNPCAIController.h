// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Core/VCTypes.h"
#include "VCNPCAIController.generated.h"

class IVCPathProvider;

/** What the NPC is doing. */
UENUM(BlueprintType)
enum class EVCNPCAIState : uint8
{
	/** No route, no target (or AI disabled / dead). */
	Idle,
	/** Walking its patrol loop. */
	Patrol,
	/** Closing on a hostile it can see (or saw recently). */
	Chase,
	/** In melee range, facing the target, swinging on cooldown. */
	Attack,
	/** Lost the target or leashed: walking back to the route. */
	Return,
	/** Roaming around home while nothing is going on (feature 10). */
	Wander,
	/** Prey: running from a threat (feature 10). Also a light-fearing hostile backing out of a torch's light. */
	Flee,
	/** Keeping its distance from a target it fears (a lit torch): faces it at the light's edge, never attacks (QA R16). */
	Hold,
};

/** Everything the state decision needs, gathered once per perception tick (pure input: testable). */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCNPCAIDecisionInput
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|AI") bool bAIEnabled = true;
	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|AI") bool bPawnDead = false;
	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|AI") bool bHasRoute = false;
	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|AI") bool bHasTarget = false;
	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|AI") bool bTargetVisible = false;
	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|AI") float DistanceToTarget = 0.f;
	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|AI") float SecondsSinceSeen = 0.f;
	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|AI") float DistanceFromRoute = 0.f;
	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|AI") bool bAtRoute = true;
	/** Prey behaviour: the target is a threat to run from, never something to attack (feature 10). */
	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|AI") bool bPrey = false;
	/** A wander radius is set, so idling turns into roaming. */
	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|AI") bool bCanWander = false;
	/** The threat is within FleeRadius (prey starts running; it keeps running while the threat is engaged). */
	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|AI") bool bThreatInFleeRange = false;
	/** Hostile that fears light, and the target carries a lit one (QA R16). */
	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|AI") bool bTargetFeared = false;
	/** How close the feared light keeps it. */
	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|AI") float FearRadius = 600.f;
	/** Band beyond FearRadius it holds in; farther out it closes in again. */
	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|AI") float FearHoldBand = 350.f;
};

/**
 * Patrol / chase / attack / return brain for AVCNPCCharacterBase pawns (feature 5). No Behavior
 * Tree and no navmesh: perception is a periodic sphere query + line of sight, movement steers
 * along waypoints from an IVCPathProvider (straight lines without one), and attacks go through
 * the pawn's own melee ability (Ability.Attack.Melee) so weapon fragments / attack power apply.
 * Server only (AI controllers are not replicated); the pawn's movement replicates as usual.
 *
 * Setup from the spawner: SetPatrolRoute (world points, walked as a loop), SetPathProvider,
 * optional SetLeashOrigin. Freeze everything with vc.AI.Enabled 0 or SetAIEnabled(false).
 */
UCLASS(BlueprintType, Blueprintable)
class VOXELCHARACTERPLUGIN_API AVCNPCAIController : public AAIController
{
	GENERATED_BODY()

public:
	AVCNPCAIController();

	// --- Tuning ---

	/** Hostiles within this radius (uu) with line of sight become the target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "0"))
	float SightRadius = 1200.f;

	/**
	 * A hostile carrying a lit light (ICGFLightBearerInterface, feature 7) is noticed from this many times
	 * SightRadius: a torch is a trade-off, not a free buff. 1 = no bonus.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "1.0"))
	float LitTargetSightMultiplier = 1.5f;

	/** Seconds without line of sight before the target is dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "0"))
	float LoseSightSeconds = 4.f;

	/** Distance (uu, pawn to pawn) at which the NPC stops and swings. Keep under the ability's MeleeRange. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "0"))
	float MeleeRange = 200.f;

	/** Seconds between swing attempts (the ability rate-limits on top). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "0.05"))
	float AttackCooldown = 1.2f;

	/** How far from the route (or leash origin) the NPC will chase before returning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "0"))
	float LeashDistance = 2500.f;

	/** Perception / decision cadence (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "0.05"))
	float PerceptionInterval = 0.25f;

	/** A waypoint counts as reached within this distance (uu, horizontal). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "10"))
	float AcceptanceRadius = 70.f;

	/** Pause at each patrol point (seconds). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "0"))
	float PatrolWaitSeconds = 1.0f;

	/** Re-request a chase path when the target moved this far from the last path goal (uu). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "0"))
	float RepathDistance = 200.f;

	/** Seconds without progress toward the current waypoint before it is skipped (blocked by a prop / corner). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "0.5"))
	float StuckSeconds = 2.0f;

	/**
	 * A walker stalled this long against a low obstacle on its path jumps it (QA R13: a one-voxel edit used to stop
	 * deer and wolves until the stuck handler routed around). 0 disables auto-jumping.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "0"))
	float AutoJumpAfterSeconds = 0.4f;

	/** Tallest obstacle (above the feet) an auto-jump tries; taller ones are left to the stuck handler. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI", meta = (ClampMin = "0"))
	float MaxAutoJumpHeight = 110.f;

	// --- Surface behaviours (feature 10) ---

	/** Roam this far around home when idle (0 = never wander). Set from AVCNPCCharacterBase::WanderRadius on possess. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI|Surface", meta = (ClampMin = "0"))
	float WanderRadius = 0.f;

	/** Pause between wander legs (seconds, random in range). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI|Surface", meta = (ClampMin = "0"))
	float WanderWaitMin = 2.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI|Surface", meta = (ClampMin = "0"))
	float WanderWaitMax = 6.f;

	/** Prey starts fleeing when a threat it can see is this close. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI|Surface", meta = (ClampMin = "0"))
	float FleeRadius = 900.f;

	/** Length of each flee leg away from the threat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI|Surface", meta = (ClampMin = "100"))
	float FleeDistance = 2000.f;

	/** Create a UVCVoxelPathProvider on possess when no provider was set and the world has surface navigation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI|Surface")
	bool bUseVoxelNavigation = true;

	/** Hostiles: when this one starts chasing, idle NPCs of the same class within this radius join (0 = none). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI|Surface", meta = (ClampMin = "0"))
	float PackRadius = 0.f;

	/** When one is hit, pack mates (same pack id, or same class without one) this close come to its aid / flee too. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI|Surface", meta = (ClampMin = "0"))
	float RetaliationAlertRadius = 2500.f;

	/** Light fear: hold this far beyond the light's radius before closing in again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|AI|Surface", meta = (ClampMin = "0"))
	float FearHoldBand = 350.f;

	/**
	 * Wary NPCs engage only hostiles within their wary radius (or attackers); aggressive ones engage what they see.
	 * @param InTemperament New temperament
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AI")
	void SetTemperament(EVCNPCTemperament InTemperament);

	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|AI")
	EVCNPCTemperament GetTemperament() const { return Temperament; }

	/**
	 * Whether this NPC keeps away from Other right now: it fears light and Other carries a lit one.
	 * @param Other Candidate target
	 * @return True while Other's light keeps this NPC off
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|AI")
	bool IsTargetFeared(const AActor* Other) const;

	/** Hostile hunts, Prey flees. Read from the possessed AVCNPCCharacterBase; settable for other pawns. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AI")
	void SetBehavior(EVCNPCBehavior InBehavior);

	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|AI")
	EVCNPCBehavior GetBehavior() const { return Behavior; }

	/** Home = where wandering is centred and the leash is measured from (the spawn point by default). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|AI")
	FVector GetHomeLocation() const { return LeashOrigin; }

	/**
	 * A pack mate saw or was hit by something: hostiles chase it, prey flee from it. Ignored while already engaged.
	 * @param NewTarget The threat
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AI")
	void AlertToTarget(AActor* NewTarget);

	// --- Setup ---

	/** World points walked as a loop. Empty = stand guard at the spawn point. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AI")
	void SetPatrolRoute(const TArray<FVector>& Points);

	/** Who answers FindPath. Null = straight lines. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AI")
	void SetPathProvider(TScriptInterface<IVCPathProvider> Provider);

	/** Where the leash is measured from when there is no route (defaults to the possess location). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AI")
	void SetLeashOrigin(const FVector& Origin);

	/** Freeze (false) / resume (true) this NPC. The global vc.AI.Enabled cvar overrides to frozen. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AI")
	void SetAIEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|AI")
	bool IsAIEnabled() const;

	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|AI")
	EVCNPCAIState GetAIState() const { return State; }

	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|AI")
	AActor* GetTarget() const { return Target.Get(); }

	/**
	 * The direction an NPC steers toward its waypoint: across local up (spherical Phase 4, WM-SY12); the old
	 * FVector(To.X, To.Y, 0).GetSafeNormal() without custom gravity.
	 * @param Movement The pawn's movement component (its gravity; null = default)
	 * @param ToWaypoint Waypoint minus the pawn's location
	 * @return Unit direction (zero when the waypoint lies along up)
	 */
	static FVector SteerDirection(const class UCharacterMovementComponent* Movement, const FVector& ToWaypoint);

	/**
	 * A wander goal before it is put on the ground: Distance from Origin at Angle in the local horizontal (the tangent plane
	 * on a planet; Origin + (cos, sin, 0) * Distance without custom gravity).
	 * @param Movement The pawn's movement component
	 * @param Origin Home (the leash origin)
	 * @param AngleRadians Angle in the gravity frame's horizontal
	 * @param Distance Distance (uu)
	 * @return The goal (not projected to the surface)
	 */
	static FVector WanderGoalAt(const class UCharacterMovementComponent* Movement, const FVector& Origin, float AngleRadians, float Distance);

	/**
	 * A flee direction: Away turned by Degrees about local up (world +Z without custom gravity).
	 * @param Movement The pawn's movement component
	 * @param Away Horizontal unit direction away from the threat
	 * @param Degrees Turn (degrees)
	 * @return Unit direction
	 */
	static FVector FleeDirection(const class UCharacterMovementComponent* Movement, const FVector& Away, float Degrees);

	/**
	 * Pure state decision (one tick): what the NPC should do next given what it perceives.
	 * @param In      Perception summary.
	 * @param Current The current state.
	 * @param InMeleeRange Attack distance.
	 * @param InLoseSightSeconds How long a lost target is still chased.
	 * @param InLeashDistance How far from the route it may stray.
	 */
	static EVCNPCAIState DecideState(const FVCNPCAIDecisionInput& In, EVCNPCAIState Current,
		float InMeleeRange, float InLoseSightSeconds, float InLeashDistance);

	/**
	 * Pure: is a candidate at Distance within sight, given whether it carries a lit light.
	 * @param InSightRadius   Base sight radius.
	 * @param bTargetLit      The candidate carries a lit light source.
	 * @param InLitMultiplier LitTargetSightMultiplier.
	 */
	static bool IsWithinSight(float Distance, float InSightRadius, bool bTargetLit, float InLitMultiplier);

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Perception + decision, every PerceptionInterval. */
	void Think();

	/** Nearest visible hostile pawn within SightRadius, or null. */
	AActor* FindTarget() const;

	/** Unobstructed line from the pawn's eyes to the target (world geometry only; pawns ignore Visibility). */
	bool HasLineOfSight(const AActor* Other) const;

	/** Build the waypoint list toward a goal through the provider (straight line without one). */
	void SetPathTo(const FVector& Goal);

	/** Steer the pawn along CurrentPath; true while moving. */
	bool FollowPath(float DeltaSeconds);

	/** Face the target and fire the melee ability on cooldown. */
	void TryAttack();

	/** The route point closest to the pawn (index), or INDEX_NONE without a route. */
	int32 NearestRoutePoint() const;

	float DistanceFromRoute() const;

	// Feature 10
	void TickWander(float DeltaSeconds, double Now);
	void TickFlee(float DeltaSeconds, double Now);
	bool PickWanderGoal(FVector& OutGoal) const;
	bool PickFleeGoal(const FVector& ThreatLocation, FVector& OutGoal) const;
	void AlertPack(AActor* NewTarget);
	class UVCVoxelPathProvider* GetVoxelProvider() const;

	/** The pawn's movement component (its gravity frame: the AI's horizontal on a planet; spherical Phase 4, WM-SY12). */
	const class UCharacterMovementComponent* GetPawnMovement() const;

	/** The pawn took a hit: turn on the attacker (prey flee from it) and alert the pack (QA R16). */
	UFUNCTION()
	void HandlePawnDamaged(const FCGFDamageContext& Context);

	/** Engage Attacker now (hostiles chase it, prey flee), replacing any current target. */
	void Provoke(AActor* Attacker);

	/** Pack mates within RetaliationAlertRadius (same PackId, or same class when unpacked) learn of Threat. */
	void AlertPackmates(AActor* Threat);

	UPROPERTY(VisibleAnywhere, Category = "VoxelCharacter|AI")
	EVCNPCAIState State = EVCNPCAIState::Idle;

	UPROPERTY()
	TArray<FVector> PatrolRoute;

	UPROPERTY()
	TScriptInterface<IVCPathProvider> PathProvider;

	TWeakObjectPtr<AActor> Target;
	TArray<FVector> CurrentPath;
	int32 PathIndex = 0;
	int32 PatrolIndex = 0;
	FVector LastPathGoal = FVector::ZeroVector;
	FVector LeashOrigin = FVector::ZeroVector;
	double LastSeenTime = -1.0;
	double LastAttackTime = -1.0;
	double LastThinkTime = -1.0;
	double PatrolWaitUntil = 0.0;
	bool bAIEnabledLocal = true;
	// Stuck detection: the best distance to the current waypoint and when it last improved.
	float BestWaypointDistance = TNumericLimits<float>::Max();
	double LastProgressTime = -1.0;
	// Auto-jump: when the last hop started, and hops since the last progress (capped so a wall falls to the stuck handler).
	double LastAutoJumpTime = -1.0;
	int32 AutoJumpsSinceProgress = 0;

	/**
	 * Hop a low obstacle between the pawn and Waypoint: something blocks at knee height just ahead and its top is above
	 * a step but within MaxAutoJumpHeight.
	 * @return True if the pawn jumped
	 */
	bool TryAutoJump(APawn* MyPawn, const FVector& Waypoint);

	UPROPERTY(VisibleAnywhere, Category = "VoxelCharacter|AI")
	EVCNPCBehavior Behavior = EVCNPCBehavior::Hostile;
	double WanderWaitUntil = 0.0;
	double LastFleePathTime = -1.0;
	/** A voxel search just failed: don't run another before this (no straight-line fallback to walk meanwhile). */
	double NextPathRetryTime = 0.0;
	FVector LastThreatLocation = FVector::ZeroVector;

	// QA R16, read from the possessed AVCNPCCharacterBase.
	UPROPERTY(VisibleAnywhere, Category = "VoxelCharacter|AI")
	EVCNPCTemperament Temperament = EVCNPCTemperament::Aggressive;
	float WaryEngageRadius = 400.f;
	bool bFearsLight = false;
	float LightFearRadius = 600.f;
	int32 PackId = 0;
	/** The pawn's combat component whose OnDamaged we listen to. */
	TWeakObjectPtr<class UVCCombatComponent> BoundCombat;
};
