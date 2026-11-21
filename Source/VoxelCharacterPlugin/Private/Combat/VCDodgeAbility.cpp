// Copyright Daniel Raquel. All Rights Reserved.

#include "Combat/VCDodgeAbility.h"
#include "Animation/VCActionAnimSet.h"
#include "Animation/VCActionMontageComponent.h"
#include "Core/VCGameplayTags.h"
#include "Movement/VCGravityFrame.h"
#include "Movement/VCMovementComponent.h"
#include "Tags/CGFGameplayTags.h"
#include "VoxelCharacterPlugin.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/Character.h"

UVCDodgeAbility::UVCDodgeAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(VCGameplayTags::Ability_Dodge);
	SetAssetTags(AssetTags);

	ActivationBlockedTags.AddTag(CGFGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(CGFGameplayTags::State_Downed);
}

float UVCDodgeAbility::GetStaminaCost(EVCMoveKind Kind) const
{
	switch (Kind)
	{
	case EVCMoveKind::Dodge: return DodgeStaminaCost;
	case EVCMoveKind::Roll:  return RollStaminaCost;
	default:                 return 0.f;
	}
}

FVCMoveRequest UVCDodgeAbility::ChooseMove(const FVector& Input, const FVector& Forward, const FVector& Up, bool bFacingLocked, bool bHasRoll)
{
	const FVector Across = Input - Up * FVector::DotProduct(Input, Up);
	// A step runs along the facing: the server turns to it (its capsule may face elsewhere, e.g. in first person).
	const FVector Facing = (Forward - Up * FVector::DotProduct(Forward, Up)).GetSafeNormal();
	if (Across.SizeSquared() < FMath::Square(0.01))
	{
		return FVCMoveRequest(EVCMoveKind::Dodge, static_cast<int32>(EVCHitDirection::Back), Facing);
	}
	if (bFacingLocked || !bHasRoll)
	{
		// The side of the facing the input points to, as a hit's side is measured.
		return FVCMoveRequest(EVCMoveKind::Dodge, static_cast<int32>(UVCActionAnimSet::DirectionOfHit(Forward, Up, Across)), Facing);
	}
	return FVCMoveRequest(EVCMoveKind::Roll, 0, Across.GetSafeNormal());
}

bool UVCDodgeAbility::CanStartMove(const AActor* Avatar, EVCMoveKind Kind, int32 Index, float Tolerance) const
{
	if (Kind != EVCMoveKind::Dodge && Kind != EVCMoveKind::Roll)
	{
		return false;
	}
	const ACharacter* Character = Cast<ACharacter>(Avatar);
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	const UVCActionMontageComponent* Montages = Avatar ? Avatar->FindComponentByClass<UVCActionMontageComponent>() : nullptr;
	const TCHAR* Refused = nullptr;
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Avatar, false);
	const EVCAnimAction Action = Kind == EVCMoveKind::Roll ? EVCAnimAction::Roll : EVCAnimAction::Dodge;
	// The replicated stamina: the owner refuses what the server would.
	const UVCMovementComponent* VoxelMovement = Cast<UVCMovementComponent>(Movement);
	const float Stamina = VoxelMovement ? VoxelMovement->GetStamina() : -1.f;
	if (!Movement || !Montages || !ASC)
	{
		Refused = TEXT("no movement, montage component or ability system");
	}
	else if (!Movement->IsMovingOnGround())
	{
		Refused = TEXT("not on the ground");
	}
	else if (ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Dead) || ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Downed))
	{
		Refused = TEXT("dead or down");
	}
	else if (!Montages->GetMontage(Action, Index))
	{
		Refused = TEXT("the held set has no montage for it");
	}
	else if (Montages->IsMoveBeforeRecovery(Tolerance))
	{
		Refused = TEXT("a dodge or a roll before its recovery");
	}
	else if (Stamina >= 0.f && Stamina < GetStaminaCost(Kind))
	{
		Refused = TEXT("short of stamina");
	}
	if (Refused)
	{
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s: no %s (%d): %s (stamina %.1f)"), *GetNameSafe(Avatar),
			Kind == EVCMoveKind::Roll ? TEXT("roll") : TEXT("dodge"), Index, Refused, Stamina);
		return false;
	}
	return true;
}

void UVCDodgeAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	ACharacter* Character = ActorInfo ? Cast<ACharacter>(ActorInfo->AvatarActor.Get()) : nullptr;
	UVCActionMontageComponent* Montages = Character ? Character->FindComponentByClass<UVCActionMontageComponent>() : nullptr;
	UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!Character || !Montages || !ASC || !Character->HasAuthority() || !CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// The owner's pick, else (an NPC's own activation) a step back.
	FVCMoveRequest Request;
	if (!Montages->TakeRequestedMove(Request))
	{
		Request = FVCMoveRequest(EVCMoveKind::Dodge, static_cast<int32>(EVCHitDirection::Back));
	}
	const int32 Index = Request.Kind == EVCMoveKind::Dodge ? FMath::Clamp<int32>(Request.Index, 0, 3) : 0;
	// A remote owner played it a trip ago: its move before this one may end that much later here.
	const bool bRemoteOwner = Character->IsPlayerControlled() && !Character->IsLocallyControlled();
	const float Tolerance = UVCActionMontageComponent::GetRequestTolerance(Character);
	UVCMovementComponent* Movement = Cast<UVCMovementComponent>(Character->GetCharacterMovement());
	if (!CanStartMove(Character, Request.Kind, Index, Tolerance) || (Movement && !Movement->SpendStamina(GetStaminaCost(Request.Kind))))
	{
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s: the %s was refused"), *Character->GetName(), Request.Kind == EVCMoveKind::Roll ? TEXT("roll") : TEXT("dodge"));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// The capsule faces as the owner's did at its press: a roll's input, a dodge's facing (in first person the owner's
	// follows the view, this one where the body moved). The root motion runs along it on every machine.
	{
		const FVector Up = FVCGravityFrame::GetUp(Character->GetCharacterMovement());
		const FVector Across = FVector(Request.Direction) - Up * FVector::DotProduct(Request.Direction, Up);
		if (!Across.IsNearlyZero())
		{
			Character->SetActorRotation(FVCGravityFrame::FacingRotation(Character->GetCharacterMovement(), Across));
		}
	}

	// From an attack at any time: the swing ends, so its hit does not land (its wait ends with it). The guard comes down.
	// Cancelled here, once the move is taken, not by CancelAbilitiesWithTag (that runs before the checks above).
	FGameplayTagContainer Cancelled;
	Cancelled.AddTag(CGFGameplayTags::Ability_Attack_Melee);
	Cancelled.AddTag(VCGameplayTags::Ability_Block);
	ASC->CancelAbilities(&Cancelled, nullptr, this);

	const EVCAnimAction Action = Request.Kind == EVCMoveKind::Roll ? EVCAnimAction::Roll : EVCAnimAction::Dodge;
	Montages->RecordMove(Request.Kind, Index);
	Montages->PlayActionForEveryone(Action, Index, 1.f, bRemoteOwner);

	// Immune on the server's own timeline, from now to 90 % of the travel.
	ASC->AddLooseGameplayTag(CGFGameplayTags::State_Invulnerable);
	bHoldsImmunity = true;
	const float Travelled = UVCActionAnimSet::GetTravelledTime(Montages->GetMontage(Action, Index));
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s: %s %d, immune for %.2f s"), *Character->GetName(),
		Request.Kind == EVCMoveKind::Roll ? TEXT("roll") : TEXT("dodge"), Index, Travelled);
	if (Travelled <= UE_KINDA_SMALL_NUMBER)
	{
		OnTravelled();
		return;
	}
	UAbilityTask_WaitDelay* Wait = UAbilityTask_WaitDelay::WaitDelay(this, Travelled);
	Wait->OnFinish.AddDynamic(this, &UVCDodgeAbility::OnTravelled);
	Wait->ReadyForActivation();
}

void UVCDodgeAbility::OnTravelled()
{
	ReleaseImmunity();
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

void UVCDodgeAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	ReleaseImmunity();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UVCDodgeAbility::ReleaseImmunity()
{
	if (!bHoldsImmunity)
	{
		return;
	}
	bHoldsImmunity = false;
	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo())
	{
		ASC->RemoveLooseGameplayTag(CGFGameplayTags::State_Invulnerable);
	}
}
