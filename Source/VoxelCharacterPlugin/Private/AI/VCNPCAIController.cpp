// Copyright Daniel Raquel. All Rights Reserved.

#include "AI/VCNPCAIController.h"
#include "AI/VCPathProvider.h"
#include "AI/VCVoxelPathProvider.h"
#include "Core/VCNPCCharacterBase.h"
#include "Tags/CGFGameplayTags.h"
#include "Interfaces/CGFLightBearerInterface.h"
#include "Utilities/CGFCombatStatics.h"
#include "VoxelCharacterPlugin.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarAIEnabled(
		TEXT("vc.AI.Enabled"), 1,
		TEXT("0 freezes every AVCNPCAIController (NPCs stand still, no attacks); 1 resumes. QA switch."),
		ECVF_Default);

	FVector2D Flat(const FVector& V) { return FVector2D(V.X, V.Y); }
}

AVCNPCAIController::AVCNPCAIController()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bAttachToPawn = false;
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void AVCNPCAIController::SetPatrolRoute(const TArray<FVector>& Points)
{
	PatrolRoute = Points;
	PatrolIndex = 0;
	CurrentPath.Reset();
	if (PatrolRoute.Num() > 0)
	{
		LeashOrigin = PatrolRoute[0];
	}
}

void AVCNPCAIController::SetPathProvider(TScriptInterface<IVCPathProvider> Provider)
{
	PathProvider = Provider;
}

void AVCNPCAIController::SetLeashOrigin(const FVector& Origin)
{
	LeashOrigin = Origin;
}

void AVCNPCAIController::SetAIEnabled(bool bEnabled)
{
	bAIEnabledLocal = bEnabled;
}

void AVCNPCAIController::SetBehavior(EVCNPCBehavior InBehavior)
{
	Behavior = InBehavior;
}

void AVCNPCAIController::AlertToTarget(AActor* NewTarget)
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!NewTarget || !MyPawn || !World || Behavior == EVCNPCBehavior::Prey || Target.IsValid() || !IsAIEnabled())
	{
		return;
	}
	Target = NewTarget;
	LastSeenTime = World->GetTimeSeconds();
	CurrentPath.Reset();
	State = EVCNPCAIState::Chase;
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s AI: alerted to %s by the pack."), *MyPawn->GetName(), *NewTarget->GetName());
}

UVCVoxelPathProvider* AVCNPCAIController::GetVoxelProvider() const
{
	return Cast<UVCVoxelPathProvider>(PathProvider.GetObject());
}

bool AVCNPCAIController::IsAIEnabled() const
{
	return bAIEnabledLocal && CVarAIEnabled.GetValueOnGameThread() != 0;
}

// ---------------------------------------------------------------------------
// Decision (pure)
// ---------------------------------------------------------------------------

EVCNPCAIState AVCNPCAIController::DecideState(const FVCNPCAIDecisionInput& In, EVCNPCAIState Current,
	float InMeleeRange, float InLoseSightSeconds, float InLeashDistance)
{
	if (!In.bAIEnabled || In.bPawnDead)
	{
		return EVCNPCAIState::Idle;
	}

	// A target we can see, or saw recently, keeps us engaged.
	const bool bEngaged = In.bHasTarget && (In.bTargetVisible || In.SecondsSinceSeen <= InLoseSightSeconds);

	// Prey (feature 10): the target is a threat. Run once it is within FleeRadius, keep running while it is
	// engaged, and settle wherever the run ended (no leash, no return home).
	if (In.bPrey)
	{
		if (bEngaged && (In.bThreatInFleeRange || Current == EVCNPCAIState::Flee))
		{
			return EVCNPCAIState::Flee;
		}
		if (In.bHasRoute)
		{
			return EVCNPCAIState::Patrol;
		}
		return In.bCanWander ? EVCNPCAIState::Wander : EVCNPCAIState::Idle;
	}

	if (bEngaged)
	{
		if (In.DistanceFromRoute > InLeashDistance)
		{
			return EVCNPCAIState::Return;
		}
		return In.DistanceToTarget <= InMeleeRange ? EVCNPCAIState::Attack : EVCNPCAIState::Chase;
	}

	// Nothing to fight: get back to the route, then patrol it, roam around home, or stand guard.
	if (!In.bAtRoute && (Current == EVCNPCAIState::Chase || Current == EVCNPCAIState::Attack || Current == EVCNPCAIState::Return))
	{
		return EVCNPCAIState::Return;
	}
	if (In.bHasRoute)
	{
		return EVCNPCAIState::Patrol;
	}
	return In.bCanWander ? EVCNPCAIState::Wander : EVCNPCAIState::Idle;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void AVCNPCAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	if (InPawn && PatrolRoute.Num() == 0)
	{
		LeashOrigin = InPawn->GetActorLocation();
	}
	if (const AVCNPCCharacterBase* NPC = Cast<AVCNPCCharacterBase>(InPawn))
	{
		Behavior = NPC->Behavior;
		WanderRadius = NPC->WanderRadius;
		PackRadius = NPC->PackRadius;
		if (NPC->SightRadius > 0.f)
		{
			SightRadius = NPC->SightRadius;
		}
	}
	// A creature that roams must be allowed to chase what it sees from the edge of its roaming area,
	// otherwise it starts beyond the leash, turns back, re-enters range and flips Chase / Return forever.
	if (WanderRadius > 0.f)
	{
		LeashDistance = FMath::Max(LeashDistance, WanderRadius + SightRadius);
	}
	// Surface NPCs path over the voxels; structures that set their own provider (dungeon grids) do so after
	// spawning and simply replace this one.
	if (bUseVoxelNavigation && !PathProvider.GetObject() && UVCVoxelPathProvider::IsAvailable(this))
	{
		UVCVoxelPathProvider* Provider = NewObject<UVCVoxelPathProvider>(this);
		PathProvider = Provider;
	}
	if (PatrolRoute.Num() > 0)
	{
		State = EVCNPCAIState::Patrol;
	}
	else
	{
		State = WanderRadius > 0.f ? EVCNPCAIState::Wander : EVCNPCAIState::Idle;
	}
	WanderWaitUntil = 0.0;
	LastThinkTime = -1.0;
}

void AVCNPCAIController::OnUnPossess()
{
	Target.Reset();
	CurrentPath.Reset();
	State = EVCNPCAIState::Idle;
	Super::OnUnPossess();
}

void AVCNPCAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !World || !HasAuthority())
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	if (LastThinkTime < 0.0 || Now - LastThinkTime >= PerceptionInterval)
	{
		LastThinkTime = Now;
		Think();
	}

	switch (State)
	{
	case EVCNPCAIState::Patrol:
		if (Now >= PatrolWaitUntil && !FollowPath(DeltaSeconds))
		{
			// Reached a point (or have no path yet): wait, then head for the next one.
			if (CurrentPath.Num() > 0)
			{
				PatrolWaitUntil = Now + PatrolWaitSeconds;
				PatrolIndex = (PatrolIndex + 1) % FMath::Max(PatrolRoute.Num(), 1);
				CurrentPath.Reset();
			}
			else if (PatrolRoute.Num() > 0)
			{
				SetPathTo(PatrolRoute[PatrolIndex]);
			}
		}
		break;
	case EVCNPCAIState::Chase:
		if (AActor* T = Target.Get())
		{
			if (CurrentPath.Num() == 0 || FVector::Dist2D(T->GetActorLocation(), LastPathGoal) > RepathDistance)
			{
				SetPathTo(T->GetActorLocation());
			}
			FollowPath(DeltaSeconds);
		}
		break;
	case EVCNPCAIState::Attack:
		TryAttack();
		break;
	case EVCNPCAIState::Wander:
		TickWander(DeltaSeconds, Now);
		break;
	case EVCNPCAIState::Flee:
		TickFlee(DeltaSeconds, Now);
		break;
	case EVCNPCAIState::Return:
		if (CurrentPath.Num() == 0)
		{
			const int32 Nearest = NearestRoutePoint();
			SetPathTo(Nearest != INDEX_NONE ? PatrolRoute[Nearest] : LeashOrigin);
			if (Nearest != INDEX_NONE)
			{
				PatrolIndex = Nearest;
			}
		}
		FollowPath(DeltaSeconds);
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Perception + decision
// ---------------------------------------------------------------------------

void AVCNPCAIController::Think()
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !World)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();

	// Keep a remembered target while it is alive and not too far; otherwise look for a fresh one.
	AActor* T = Target.Get();
	if (T && (UCGFCombatStatics::IsActorDead(T) || FVector::Dist(T->GetActorLocation(), MyPawn->GetActorLocation()) > SightRadius * 1.5f))
	{
		T = nullptr;
		Target.Reset();
	}
	bool bVisible = false;
	if (T)
	{
		bVisible = HasLineOfSight(T);
	}
	if (!T || (!bVisible && Now - LastSeenTime > LoseSightSeconds))
	{
		T = FindTarget();
		Target = T;
		bVisible = T != nullptr;
		if (T)
		{
			CurrentPath.Reset();
		}
	}
	if (bVisible)
	{
		LastSeenTime = Now;
	}

	FVCNPCAIDecisionInput In;
	In.bAIEnabled = IsAIEnabled();
	const AVCNPCCharacterBase* NPC = Cast<AVCNPCCharacterBase>(MyPawn);
	In.bPawnDead = NPC ? NPC->IsDead() : UCGFCombatStatics::IsActorDead(MyPawn);
	In.bHasRoute = PatrolRoute.Num() > 0;
	In.bHasTarget = T != nullptr;
	In.bTargetVisible = bVisible;
	In.DistanceToTarget = T ? FVector::Dist(T->GetActorLocation(), MyPawn->GetActorLocation()) : 0.f;
	In.SecondsSinceSeen = (T && LastSeenTime >= 0.0) ? static_cast<float>(Now - LastSeenTime) : 0.f;
	In.DistanceFromRoute = DistanceFromRoute();
	In.bAtRoute = In.DistanceFromRoute <= AcceptanceRadius * 2.f;
	In.bPrey = Behavior == EVCNPCBehavior::Prey;
	In.bCanWander = WanderRadius > 0.f;
	In.bThreatInFleeRange = T != nullptr && In.DistanceToTarget <= FleeRadius;

	const EVCNPCAIState Next = DecideState(In, State, MeleeRange, LoseSightSeconds, LeashDistance);
	if (Next != State)
	{
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s AI: %s -> %s (target %s, dist %.0f)"), *MyPawn->GetName(),
			*UEnum::GetValueAsString(State), *UEnum::GetValueAsString(Next), *GetNameSafe(T), In.DistanceToTarget);
		if (Next == EVCNPCAIState::Return || Next == EVCNPCAIState::Idle || Next == EVCNPCAIState::Wander)
		{
			Target.Reset();
		}
		if (State == EVCNPCAIState::Flee)
		{
			// Prey settles where the run ended: home moves with it.
			LeashOrigin = MyPawn->GetActorLocation();
			WanderWaitUntil = Now + WanderWaitMax;
		}
		if (Next == EVCNPCAIState::Chase && T)
		{
			AlertPack(T);
		}
		CurrentPath.Reset();
		State = Next;
		if (State == EVCNPCAIState::Idle || State == EVCNPCAIState::Attack)
		{
			if (UCharacterMovementComponent* Move = MyPawn->FindComponentByClass<UCharacterMovementComponent>())
			{
				Move->StopMovementKeepPathing();
			}
		}
	}
}

bool AVCNPCAIController::IsWithinSight(float Distance, float InSightRadius, bool bTargetLit, float InLitMultiplier)
{
	const float Reach = bTargetLit ? InSightRadius * FMath::Max(InLitMultiplier, 1.f) : InSightRadius;
	return Distance <= Reach;
}

AActor* AVCNPCAIController::FindTarget() const
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !World)
	{
		return nullptr;
	}
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VCNPCSight), false, MyPawn);
	// Gather out to the lit-target reach; unlit candidates are filtered back to SightRadius below.
	const float GatherRadius = SightRadius * FMath::Max(LitTargetSightMultiplier, 1.f);
	World->OverlapMultiByObjectType(Overlaps, MyPawn->GetActorLocation(), FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(GatherRadius), Params);

	AActor* Best = nullptr;
	float BestDist = TNumericLimits<float>::Max();
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Other = Overlap.GetActor();
		if (!Other || Other == MyPawn || !Other->IsA<APawn>())
		{
			continue;
		}
		if (!UCGFCombatStatics::AreHostile(MyPawn, Other) || UCGFCombatStatics::IsActorDead(Other))
		{
			continue;
		}
		const float Dist = FVector::Dist(Other->GetActorLocation(), MyPawn->GetActorLocation());
		const bool bLit = Other->Implements<UCGFLightBearerInterface>()
			&& ICGFLightBearerInterface::Execute_GetCarriedLightLevel(Other) > 0.f;
		if (!IsWithinSight(Dist, SightRadius, bLit, LitTargetSightMultiplier))
		{
			continue;
		}
		if (Dist < BestDist && HasLineOfSight(Other))
		{
			Best = Other;
			BestDist = Dist;
		}
	}
	return Best;
}

bool AVCNPCAIController::HasLineOfSight(const AActor* Other) const
{
	const APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !Other || !World)
	{
		return false;
	}
	FVector Eyes;
	FRotator EyesRot;
	MyPawn->GetActorEyesViewPoint(Eyes, EyesRot);
	FVector TargetEyes;
	FRotator TargetRot;
	Other->GetActorEyesViewPoint(TargetEyes, TargetRot);
	// Pawn collision profiles ignore Visibility, so only world geometry (walls, doors) blocks this.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VCNPCLineOfSight), false, MyPawn);
	Params.AddIgnoredActor(Other);
	FHitResult Hit;
	return !World->LineTraceSingleByChannel(Hit, Eyes, TargetEyes, ECC_Visibility, Params);
}

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

void AVCNPCAIController::SetPathTo(const FVector& Goal)
{
	const UWorld* PathWorld = GetWorld();
	const double PathNow = PathWorld ? PathWorld->GetTimeSeconds() : 0.0;
	// Over the voxels a failed search leaves no path to walk; callers retry while CurrentPath is empty, so
	// throttle instead of running A* every frame.
	if (GetVoxelProvider() && PathNow < NextPathRetryTime)
	{
		return;
	}
	APawn* MyPawn = GetPawn();
	CurrentPath.Reset();
	PathIndex = 0;
	LastPathGoal = Goal;
	BestWaypointDistance = TNumericLimits<float>::Max();
	LastProgressTime = -1.0;
	if (!MyPawn)
	{
		return;
	}
	bool bFound = false;
	if (PathProvider.GetObject())
	{
		bFound = IVCPathProvider::Execute_FindPath(PathProvider.GetObject(), MyPawn->GetActorLocation(), Goal, CurrentPath);
	}
	if (!bFound || CurrentPath.Num() == 0)
	{
		CurrentPath.Reset();
		// Over the voxels a failed search means the goal is unreachable (water, cliffs): walking a straight
		// line there would march the animal into the sea. Other providers (dungeon grids) keep the old
		// straight-line fallback for the rare miss inside a room.
		if (!GetVoxelProvider())
		{
			CurrentPath.Add(Goal);
		}
		else
		{
			NextPathRetryTime = PathNow + 0.75;
		}
	}
}

bool AVCNPCAIController::FollowPath(float DeltaSeconds)
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || CurrentPath.Num() == 0)
	{
		return false;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	while (PathIndex < CurrentPath.Num() && FVector::Dist2D(MyPawn->GetActorLocation(), CurrentPath[PathIndex]) <= AcceptanceRadius)
	{
		++PathIndex;
		BestWaypointDistance = TNumericLimits<float>::Max();
		LastProgressTime = Now;
	}
	if (PathIndex >= CurrentPath.Num())
	{
		return false; // arrived
	}
	// Stuck on a prop or a corner: give up on this waypoint rather than pushing forever.
	const float Distance = FVector::Dist2D(MyPawn->GetActorLocation(), CurrentPath[PathIndex]);
	if (Distance < BestWaypointDistance - 5.f)
	{
		BestWaypointDistance = Distance;
		LastProgressTime = Now;
	}
	else if (LastProgressTime >= 0.0 && Now - LastProgressTime > StuckSeconds)
	{
		// Over the voxels: remember where it failed so the next search goes around, and ask for a new route.
		if (UVCVoxelPathProvider* Voxel = GetVoxelProvider())
		{
			// One cell ahead with a small radius: blocking the walker's own cell would strand it (and make
			// spawners that test walkability think it stands somewhere impossible).
			const FVector Ahead = MyPawn->GetActorLocation() + (CurrentPath[PathIndex] - MyPawn->GetActorLocation()).GetSafeNormal2D() * 160.f;
			UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s AI: stuck %.1fs short of waypoint %d — blocking ahead and repathing."), *MyPawn->GetName(), Distance, PathIndex);
			Voxel->ReportBlocked(Ahead, 60.f, 8.f);
			CurrentPath.Reset();
			PathIndex = 0;
			BestWaypointDistance = TNumericLimits<float>::Max();
			LastProgressTime = -1.0;
			return false;
		}
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s AI: stuck %.1fs short of waypoint %d — skipping."), *MyPawn->GetName(), Distance, PathIndex);
		++PathIndex;
		BestWaypointDistance = TNumericLimits<float>::Max();
		LastProgressTime = Now;
		return PathIndex < CurrentPath.Num();
	}
	if (LastProgressTime < 0.0)
	{
		LastProgressTime = Now;
	}
	const FVector To = CurrentPath[PathIndex] - MyPawn->GetActorLocation();
	const FVector Dir = FVector(To.X, To.Y, 0.f).GetSafeNormal();
	if (!Dir.IsNearlyZero())
	{
		MyPawn->AddMovementInput(Dir, 1.f);
	}
	return true;
}

// ---------------------------------------------------------------------------
// Surface behaviours (feature 10)
// ---------------------------------------------------------------------------

bool AVCNPCAIController::PickWanderGoal(FVector& OutGoal) const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || WanderRadius <= 0.f)
	{
		return false;
	}
	const UVCVoxelPathProvider* Voxel = GetVoxelProvider();
	for (int32 Try = 0; Try < 4; ++Try)
	{
		const float Angle = FMath::FRandRange(0.f, 2.f * UE_PI);
		const float Dist = FMath::FRandRange(WanderRadius * 0.3f, WanderRadius);
		FVector Goal = LeashOrigin + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.f);
		if (Voxel)
		{
			FVector OnSurface;
			if (!Voxel->ProjectToSurface(Goal, OnSurface) || !Voxel->IsWalkable(OnSurface))
			{
				continue;
			}
			Goal = OnSurface;
		}
		OutGoal = Goal;
		return true;
	}
	return false;
}

void AVCNPCAIController::TickWander(float DeltaSeconds, double Now)
{
	if (CurrentPath.Num() == 0)
	{
		if (Now >= WanderWaitUntil)
		{
			FVector Goal;
			if (PickWanderGoal(Goal))
			{
				SetPathTo(Goal);
			}
			else
			{
				WanderWaitUntil = Now + 1.0;
			}
		}
		return;
	}
	if (!FollowPath(DeltaSeconds))
	{
		// Arrived (or gave up): rest a while before the next leg.
		CurrentPath.Reset();
		WanderWaitUntil = Now + FMath::FRandRange(WanderWaitMin, FMath::Max(WanderWaitMin, WanderWaitMax));
	}
}

bool AVCNPCAIController::PickFleeGoal(const FVector& ThreatLocation, FVector& OutGoal) const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return false;
	}
	FVector Away = (MyPawn->GetActorLocation() - ThreatLocation).GetSafeNormal2D();
	if (Away.IsNearlyZero())
	{
		const float Angle = FMath::FRandRange(0.f, 2.f * UE_PI);
		Away = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
	}
	const UVCVoxelPathProvider* Voxel = GetVoxelProvider();
	// Straight away first, then increasingly sideways, so a cliff or lake behind the animal does not trap it.
	static const float Angles[] = { 0.f, 40.f, -40.f, 80.f, -80.f, 120.f, -120.f };
	FVector Fallback = MyPawn->GetActorLocation() + Away * FleeDistance;
	for (const float Deg : Angles)
	{
		const FVector Dir = Away.RotateAngleAxis(Deg, FVector::UpVector);
		FVector Goal = MyPawn->GetActorLocation() + Dir * FleeDistance;
		if (Voxel)
		{
			FVector OnSurface;
			if (!Voxel->ProjectToSurface(Goal, OnSurface) || !Voxel->IsWalkable(OnSurface))
			{
				continue;
			}
			Goal = OnSurface;
		}
		OutGoal = Goal;
		return true;
	}
	OutGoal = Fallback;
	return true;
}

void AVCNPCAIController::TickFlee(float DeltaSeconds, double Now)
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return;
	}
	if (const AActor* T = Target.Get())
	{
		LastThreatLocation = T->GetActorLocation();
	}
	const bool bArrived = CurrentPath.Num() > 0 && !FollowPath(DeltaSeconds);
	if ((CurrentPath.Num() == 0 || bArrived) && (LastFleePathTime < 0.0 || Now - LastFleePathTime > 0.5))
	{
		FVector Goal;
		if (PickFleeGoal(LastThreatLocation, Goal))
		{
			SetPathTo(Goal);
			LastFleePathTime = Now;
		}
	}
}

void AVCNPCAIController::AlertPack(AActor* NewTarget)
{
	APawn* MyPawn = GetPawn();
	UWorld* World = GetWorld();
	if (!MyPawn || !World || !NewTarget || PackRadius <= 0.f || Behavior == EVCNPCBehavior::Prey)
	{
		return;
	}
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VCNPCPack), false, MyPawn);
	World->OverlapMultiByObjectType(Overlaps, MyPawn->GetActorLocation(), FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(PackRadius), Params);
	int32 Alerted = 0;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		APawn* Other = Cast<APawn>(Overlap.GetActor());
		if (!Other || Other == MyPawn || Other->GetClass() != MyPawn->GetClass())
		{
			continue;
		}
		AVCNPCAIController* Mate = Cast<AVCNPCAIController>(Other->GetController());
		if (Mate && !Mate->GetTarget() && (Mate->GetAIState() == EVCNPCAIState::Wander || Mate->GetAIState() == EVCNPCAIState::Idle || Mate->GetAIState() == EVCNPCAIState::Patrol))
		{
			Mate->AlertToTarget(NewTarget);
			++Alerted;
		}
	}
	if (Alerted > 0)
	{
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s AI: alerted %d pack mate(s) to %s."), *MyPawn->GetName(), Alerted, *NewTarget->GetName());
	}
}

// ---------------------------------------------------------------------------
// Combat
// ---------------------------------------------------------------------------

void AVCNPCAIController::TryAttack()
{
	APawn* MyPawn = GetPawn();
	AActor* T = Target.Get();
	UWorld* World = GetWorld();
	if (!MyPawn || !T || !World)
	{
		return;
	}
	// Face the target: the melee ability sweeps from the eyes along the view rotation.
	const FVector To = T->GetActorLocation() - MyPawn->GetActorLocation();
	const FRotator Face(0.f, To.Rotation().Yaw, 0.f);
	SetControlRotation(Face);
	MyPawn->SetActorRotation(Face);

	const double Now = World->GetTimeSeconds();
	if (LastAttackTime >= 0.0 && Now - LastAttackTime < AttackCooldown)
	{
		return;
	}
	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(MyPawn))
	{
		if (ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(CGFGameplayTags::Ability_Attack_Melee)))
		{
			LastAttackTime = Now;
		}
	}
}

// ---------------------------------------------------------------------------
// Route helpers
// ---------------------------------------------------------------------------

int32 AVCNPCAIController::NearestRoutePoint() const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn || PatrolRoute.Num() == 0)
	{
		return INDEX_NONE;
	}
	int32 Best = 0;
	float BestDist = TNumericLimits<float>::Max();
	for (int32 i = 0; i < PatrolRoute.Num(); ++i)
	{
		const float D = FVector::Dist2D(PatrolRoute[i], MyPawn->GetActorLocation());
		if (D < BestDist)
		{
			BestDist = D;
			Best = i;
		}
	}
	return Best;
}

float AVCNPCAIController::DistanceFromRoute() const
{
	const APawn* MyPawn = GetPawn();
	if (!MyPawn)
	{
		return 0.f;
	}
	const int32 Nearest = NearestRoutePoint();
	const FVector Anchor = Nearest != INDEX_NONE ? PatrolRoute[Nearest] : LeashOrigin;
	return FVector::Dist2D(Anchor, MyPawn->GetActorLocation());
}
