// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
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
	 * Pure state decision (one tick): what the NPC should do next given what it perceives.
	 * @param In      Perception summary.
	 * @param Current The current state.
	 * @param InMeleeRange Attack distance.
	 * @param InLoseSightSeconds How long a lost target is still chased.
	 * @param InLeashDistance How far from the route it may stray.
	 */
	static EVCNPCAIState DecideState(const FVCNPCAIDecisionInput& In, EVCNPCAIState Current,
		float InMeleeRange, float InLoseSightSeconds, float InLeashDistance);

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
};
