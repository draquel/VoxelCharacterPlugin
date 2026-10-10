// Copyright Daniel Raquel. All Rights Reserved.

#include "AI/VCNPCAIController.h"
#include "AI/VCPathProvider.h"
#include "Core/VCNPCCharacterBase.h"
#include "Tags/CGFGameplayTags.h"
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

	// A target we can see, or saw recently, keeps us in combat unless the leash pulls us back.
	const bool bEngaged = In.bHasTarget && (In.bTargetVisible || In.SecondsSinceSeen <= InLoseSightSeconds);
	if (bEngaged)
	{
		if (In.DistanceFromRoute > InLeashDistance)
		{
			return EVCNPCAIState::Return;
		}
		return In.DistanceToTarget <= InMeleeRange ? EVCNPCAIState::Attack : EVCNPCAIState::Chase;
	}

	// Nothing to fight: get back to the route, then patrol it (or stand guard without one).
	if (!In.bAtRoute && (Current == EVCNPCAIState::Chase || Current == EVCNPCAIState::Attack || Current == EVCNPCAIState::Return))
	{
		return EVCNPCAIState::Return;
	}
	return In.bHasRoute ? EVCNPCAIState::Patrol : EVCNPCAIState::Idle;
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
	State = PatrolRoute.Num() > 0 ? EVCNPCAIState::Patrol : EVCNPCAIState::Idle;
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

	const EVCNPCAIState Next = DecideState(In, State, MeleeRange, LoseSightSeconds, LeashDistance);
	if (Next != State)
	{
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s AI: %s -> %s (target %s, dist %.0f)"), *MyPawn->GetName(),
			*UEnum::GetValueAsString(State), *UEnum::GetValueAsString(Next), *GetNameSafe(T), In.DistanceToTarget);
		if (Next == EVCNPCAIState::Return || Next == EVCNPCAIState::Idle)
		{
			Target.Reset();
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
	World->OverlapMultiByObjectType(Overlaps, MyPawn->GetActorLocation(), FQuat::Identity,
		FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(SightRadius), Params);

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
	APawn* MyPawn = GetPawn();
	CurrentPath.Reset();
	PathIndex = 0;
	LastPathGoal = Goal;
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
		CurrentPath.Add(Goal);
	}
}

bool AVCNPCAIController::FollowPath(float DeltaSeconds)
{
	APawn* MyPawn = GetPawn();
	if (!MyPawn || CurrentPath.Num() == 0)
	{
		return false;
	}
	while (PathIndex < CurrentPath.Num() && FVector::Dist2D(MyPawn->GetActorLocation(), CurrentPath[PathIndex]) <= AcceptanceRadius)
	{
		++PathIndex;
	}
	if (PathIndex >= CurrentPath.Num())
	{
		return false; // arrived
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
