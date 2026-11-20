// Copyright Daniel Raquel. All Rights Reserved.

#include "Animation/VCActionMontageComponent.h"
#include "Core/VCAnimatedCharacter.h"
#include "Core/VCAnimInstance.h"
#include "Core/VCGameplayTags.h"
#include "Combat/VCCombatComponent.h"
#include "Interfaces/CGFDamageableInterface.h"
#include "Movement/VCGravityFrame.h"
#include "Movement/VCMovementComponent.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Tags/CGFGameplayTags.h"

namespace
{
	TAutoConsoleVariable<int32> CVarAnimAttackBreak(
		TEXT("vc.Anim.AttackBreak"), 1,
		TEXT("Movement breaks an attack off once its follow-through ends (the sword attack flow plan, D103): 1 on, 0 off (the recovery always plays out; A/B)."),
		ECVF_Default);

	TAutoConsoleVariable<int32> CVarAnimAttackCharge(
		TEXT("vc.Anim.AttackCharge"), 1,
		TEXT("A primary press from rest charges the sword (the sword moves plan, D167): 1 on (the first attack at the release, the charged strike after a hold), 0 off (the first attack at the press, as before; A/B)."),
		ECVF_Default);
}

UVCActionMontageComponent::UVCActionMontageComponent()
{
	// Ticks only while an attack plays (D103: the recovery's break, D104: the kept press).
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void UVCActionMontageComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	float Position = 0.f;
	UAnimMontage* Montage = GetPlayingAttackMontage(Position);
	// A dodge or a roll breaks its recovery off on the move as an attack does (D170 after D103).
	if (!Montage)
	{
		Montage = GetPlayingMoveMontage(Position);
	}
	if (!Montage)
	{
		// Nothing to break; a kept press still waits for its recovery (the owner's character takes it).
		SetComponentTickEnabled(bAttackBuffered);
		return;
	}
	// Read on this machine's own montage, so a simulated proxy, which keeps no attack of its own, breaks it too. A kept
	// press wins: the next attack plays from the recovery instead.
	const float Recovery = UVCActionAnimSet::GetRecoveryTime(Montage);
	if (CVarAnimAttackBreak.GetValueOnGameThread() != 0 && !bAttackBuffered
		&& ShouldBreakRecovery(Position, Recovery, Montage->GetPlayLength(), WantsToMove()))
	{
		const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
		if (UAnimInstance* AnimInstance = GetBodyAnimInstance())
		{
			AnimInstance->Montage_Stop(Set ? Set->RecoveryBlendOut : 0.2f, Montage);
		}
	}
}

EVCAttackPress UVCActionMontageComponent::ClassifyAttackPress(double SinceStart, float HitTime, float RecoveryTime, float EndTime, float Window)
{
	if (SinceStart < 0.0 || SinceStart > EndTime + Window)
	{
		return EVCAttackPress::NewCombo;
	}
	if (SinceStart < HitTime)
	{
		return EVCAttackPress::Ignore;
	}
	return SinceStart < RecoveryTime ? EVCAttackPress::Buffer : EVCAttackPress::Continue;
}

float UVCActionMontageComponent::GetRequestTolerance(const AActor* Character)
{
	const APawn* Pawn = Cast<APawn>(Character);
	return Pawn && Pawn->HasAuthority() && Pawn->IsPlayerControlled() && !Pawn->IsLocallyControlled() ? AttackRequestTolerance : 0.f;
}

bool UVCActionMontageComponent::IsBeforeRecovery(double SinceStart, float RecoveryTime, float Tolerance)
{
	return SinceStart >= 0.0 && SinceStart < RecoveryTime - FMath::Max(Tolerance, 0.f);
}

void UVCActionMontageComponent::RecordMove(EVCMoveKind Kind, int32 Index)
{
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	const EVCAnimAction Action = Kind == EVCMoveKind::Roll ? EVCAnimAction::Roll : EVCAnimAction::Dodge;
	const UAnimMontage* Montage = Set ? Set->GetMontage(Action, Index) : nullptr;
	const UWorld* World = GetWorld();
	MoveStartTime = World ? World->GetTimeSeconds() : 0.0;
	MoveRecoveryTime = UVCActionAnimSet::GetRecoveryTime(Montage);
	// The move cancels the attack (the server ends the melee ability, so its hit does not land) and the combo starts over.
	ComboIndex = -1;
	AttackStartTime = -1.0;
	AttackRecoveryTime = 0.f;
	bAttackBuffered = false;
	bGuardBuffered = false;
}

bool UVCActionMontageComponent::IsMoveBeforeRecovery(float Tolerance) const
{
	const UWorld* World = GetWorld();
	return World && MoveStartTime >= 0.0 && IsBeforeRecovery(World->GetTimeSeconds() - MoveStartTime, MoveRecoveryTime, Tolerance);
}

bool UVCActionMontageComponent::IsAttackBeforeRecovery(float Tolerance) const
{
	return ComboIndex >= 0 && AttackStartTime >= 0.0 && IsBeforeRecovery(GetSecondsSinceAttack(), AttackRecoveryTime, Tolerance);
}

bool UVCActionMontageComponent::ShouldBreakRecovery(double SinceStart, float RecoveryTime, float EndTime, bool bWantsToMove)
{
	return bWantsToMove && SinceStart >= RecoveryTime && SinceStart < EndTime;
}

float UVCActionMontageComponent::GetAttackPlayRate(float AttackSpeed)
{
	return FMath::Clamp(AttackSpeed, 0.5f, 2.f);
}

bool UVCActionMontageComponent::CanChargeFrom(bool bSetCharges, EVCAttackPress Press, bool bSprinting, bool bFalling)
{
	return bSetCharges && Press == EVCAttackPress::NewCombo && !bSprinting && !bFalling;
}

EVCChargeRelease UVCActionMontageComponent::ClassifyChargeRelease(double HeldSeconds, float ChargeTime)
{
	return HeldSeconds >= ChargeTime ? EVCChargeRelease::Charged : EVCChargeRelease::FirstAttack;
}

bool UVCActionMontageComponent::CanStartCharge() const
{
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	const bool bSetCharges = Set && Set->HasCharge() && CVarAnimAttackCharge.GetValueOnGameThread() != 0;
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	const UVCMovementComponent* VCMovement = Cast<UVCMovementComponent>(Movement);
	return !IsMoveBeforeRecovery()
		&& CanChargeFrom(bSetCharges, ClassifyAttackPress(), VCMovement && VCMovement->IsSprinting(), Movement && Movement->IsFalling());
}

bool UVCActionMontageComponent::CanRunAttackFrom(bool bSetHasRunAttack, bool bSprinting, float Speed, float MinSpeed)
{
	return bSetHasRunAttack && bSprinting && Speed > MinSpeed;
}

bool UVCActionMontageComponent::CanRunAttack(float SpeedTolerance) const
{
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UVCMovementComponent* Movement = Character ? Cast<UVCMovementComponent>(Character->GetCharacterMovement()) : nullptr;
	if (!Set || !Movement)
	{
		return false;
	}
	const FVector Up = FVCGravityFrame::GetUp(Movement);
	const FVector Velocity = Movement->Velocity;
	const float Speed = (Velocity - Up * FVector::DotProduct(Velocity, Up)).Size();
	return CanRunAttackFrom(Set->HasRunAttack(), Movement->IsSprinting(), Speed, Set->RunAttackSpeed - FMath::Max(SpeedTolerance, 0.f));
}

bool UVCActionMontageComponent::CanAirAttackFrom(bool bSetHasIt, bool bUsedThisFall, bool bFalling, double SinceLanded, float Tolerance)
{
	return bSetHasIt && !bUsedThisFall && (bFalling || (SinceLanded >= 0.0 && SinceLanded < Tolerance));
}

bool UVCActionMontageComponent::IsFalling() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	return Movement && Movement->IsFalling();
}

bool UVCActionMontageComponent::HasAirAttack(bool bSecondary) const
{
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	return Set && Set->GetAirAttack(bSecondary) >= 0;
}

bool UVCActionMontageComponent::CanAirAttack(bool bSecondary, float Tolerance) const
{
	const UWorld* World = GetWorld();
	const double SinceLanded = World && LastLandedTime >= 0.0 ? World->GetTimeSeconds() - LastLandedTime : -1.0;
	return CanAirAttackFrom(HasAirAttack(bSecondary), bAirAttackThisFall, IsFalling(), SinceLanded, Tolerance);
}

int32 UVCActionMontageComponent::ChooseAirAttack(bool bSecondary, float AttackSpeed, float& OutPlayRate)
{
	OutPlayRate = GetAttackPlayRate(AttackSpeed);
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	const int32 Index = Set ? Set->GetAirAttack(bSecondary) : -1;
	if (Index < 0)
	{
		return -1;
	}
	RecordAttack(Index, OutPlayRate);
	bAirAttackThisFall = true;
	return Index;
}

void UVCActionMontageComponent::CountAirAttackFromLanding()
{
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	const UWorld* World = GetWorld();
	if (!Set || !World || !Set->IsAirAttack(ComboIndex) || AttackStartTime < 0.0)
	{
		return;
	}
	// The recorded times are the montage's, from its start: from the landing, its position is the landing section's.
	const float Rate = AttackEndTime > 0.f && Set->AttackCombo[ComboIndex] ? Set->AttackCombo[ComboIndex]->GetPlayLength() / AttackEndTime : 1.f;
	AttackStartTime = World->GetTimeSeconds() - Set->GetLandingTime(ComboIndex) / FMath::Max(Rate, 0.01f);
}

void UVCActionMontageComponent::HandleFallEnded(bool bOnGround)
{
	const UWorld* World = GetWorld();
	LastLandedTime = World ? World->GetTimeSeconds() : 0.0;
	bAirAttackThisFall = false;
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	if (Set && Set->IsAirAttack(ComboIndex) && AttackStartTime >= 0.0)
	{
		if (bOnGround)
		{
			CountAirAttackFromLanding();
		}
		else
		{
			// Into the water: no landing to play, and the combo starts over.
			ComboIndex = -1;
			AttackStartTime = -1.0;
		}
	}
	// This machine's own montage, whoever started it (a simulated proxy keeps no attack of its own).
	if (UAnimInstance* AnimInstance = Set ? GetBodyAnimInstance() : nullptr)
	{
		for (const int32 Index : { Set->GetAirAttack(false), Set->GetAirAttack(true) })
		{
			UAnimMontage* Montage = Index >= 0 ? Set->AttackCombo[Index].Get() : nullptr;
			const FAnimMontageInstance* Instance = Montage ? AnimInstance->GetActiveInstanceForMontage(Montage) : nullptr;
			if (!Instance || !Instance->IsActive() || Instance->IsStopped())
			{
				continue;
			}
			if (bOnGround && Montage->GetSectionIndex(UVCActionAnimSet::LandingSection) != INDEX_NONE)
			{
				AnimInstance->Montage_JumpToSection(UVCActionAnimSet::LandingSection, Montage);
			}
			else
			{
				AnimInstance->Montage_Stop(Set->RecoveryBlendOut, Montage);
			}
		}
	}
	OnFallEnded.Broadcast(bOnGround);
}

void UVCActionMontageComponent::BeginCharge()
{
	const UWorld* World = GetWorld();
	ChargeStartTime = World ? World->GetTimeSeconds() : 0.0;
}

double UVCActionMontageComponent::GetChargeSeconds() const
{
	const UWorld* World = GetWorld();
	return World && ChargeStartTime >= 0.0 ? World->GetTimeSeconds() - ChargeStartTime : -1.0;
}

float UVCActionMontageComponent::GetChargeTime() const
{
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	return Set ? Set->GetChargeTime() : 0.f;
}

bool UVCActionMontageComponent::IsChargePlaying() const
{
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	const UAnimInstance* AnimInstance = Set && Set->Charge ? GetBodyAnimInstance() : nullptr;
	const FAnimMontageInstance* Instance = AnimInstance ? AnimInstance->GetActiveInstanceForMontage(Set->Charge) : nullptr;
	return Instance && Instance->IsActive() && !Instance->IsStopped();
}

int32 UVCActionMontageComponent::ChooseChargedAttack(float AttackSpeed, float& OutPlayRate)
{
	OutPlayRate = GetAttackPlayRate(AttackSpeed);
	EndCharge();
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	if (!Set || !Set->HasCharge())
	{
		return -1;
	}
	RecordAttack(Set->ChargedAttack, OutPlayRate);
	return Set->ChargedAttack;
}

double UVCActionMontageComponent::GetSecondsSinceAttack() const
{
	const UWorld* World = GetWorld();
	return World && AttackStartTime >= 0.0 ? World->GetTimeSeconds() - AttackStartTime : -1.0;
}

EVCAttackPress UVCActionMontageComponent::ClassifyAttackPress() const
{
	return ClassifyAttackPress(GetSecondsSinceAttack(), AttackHitTime, AttackRecoveryTime, AttackEndTime, AttackComboWindow);
}

void UVCActionMontageComponent::RecordAttack(int32 Index, float PlayRate)
{
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	const UAnimMontage* Montage = Set ? Set->GetMontage(EVCAnimAction::Attack, Index) : nullptr;
	const float Rate = FMath::Max(PlayRate, 0.01f);
	const UWorld* World = GetWorld();
	ComboIndex = Index;
	AttackStartTime = World ? World->GetTimeSeconds() : 0.0;
	AttackHitTime = UVCActionAnimSet::GetHitTime(Montage) / Rate;
	AttackRecoveryTime = UVCActionAnimSet::GetRecoveryTime(Montage) / Rate;
	AttackEndTime = Montage ? Montage->GetPlayLength() / Rate : 0.f;
	AttackComboWindow = Set ? Set->ComboWindow : 0.f;
	AttackComboCount = Set ? Set->AttackCombo.Num() : 0;
	bAttackBuffered = false;
	// An attack after a dodge or a roll ends the move's record.
	MoveStartTime = -1.0;
}

int32 UVCActionMontageComponent::ChooseAttack(float AttackSpeed, float& OutPlayRate, bool bSecondary, bool bAllowRunAttack)
{
	OutPlayRate = GetAttackPlayRate(AttackSpeed);
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	const int32 Count = Set ? Set->AttackCombo.Num() : 0;
	if (Count == 0)
	{
		return bSecondary ? -1 : 0;
	}
	// The button's next of the combo when a press now continues it (one kept for the recovery does by then), else the
	// button's first (D166).
	const EVCAttackPress Press = ClassifyAttackPress();
	// A fresh primary press while sprinting fast plays the run attack instead of the combo (D168).
	if (!bSecondary && bAllowRunAttack && CanRunAttack())
	{
		RecordAttack(Set->RunAttack, OutPlayRate);
		return Set->RunAttack;
	}
	const bool bContinues = ComboIndex >= 0 && AttackComboCount == Count
		&& (Press == EVCAttackPress::Continue || Press == EVCAttackPress::Buffer);
	const int32 Index = bContinues ? Set->GetNextAttack(ComboIndex, bSecondary) : Set->GetFirstAttack(bSecondary);
	if (Index < 0)
	{
		return -1;
	}
	RecordAttack(Index, OutPlayRate);
	return Index;
}

bool UVCActionMontageComponent::AcceptAttack(int32 RequestedIndex, float AttackSpeed, int32& OutIndex, float& OutPlayRate)
{
	OutPlayRate = GetAttackPlayRate(AttackSpeed);
	OutIndex = 0;
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	const int32 Count = Set ? Set->AttackCombo.Num() : 0;
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	// No attack before a dodge's or a roll's recovery (D170), less the request's allowance.
	if (IsMoveBeforeRecovery(AttackRequestTolerance))
	{
		return false;
	}
	// The owner picked it on this machine this very moment (a listen server's or a standalone player): it is the attack
	// already kept, not a second pick (G179).
	if (RequestedIndex >= 0 && RequestedIndex == ComboIndex && AttackStartTime == Now)
	{
		OutIndex = ComboIndex;
		return true;
	}
	// The attack before must have reached its recovery; the request's trip makes the server see it late, never early,
	// so the allowance only takes up the jitter.
	const double Since = GetSecondsSinceAttack();
	if (ComboIndex >= 0 && Since >= 0.0 && Since < AttackRecoveryTime - AttackRequestTolerance)
	{
		return false;
	}
	if (Count == 0)
	{
		RecordAttack(0, OutPlayRate);
		return true;
	}
	// The charged strike (D167): taken once this machine's charge has run its time less the request's allowance, else
	// the release's first attack. Any attack ends the charge.
	const double ChargeSeconds = GetChargeSeconds();
	EndCharge();
	if (Set->HasCharge() && RequestedIndex == Set->ChargedAttack)
	{
		OutIndex = ChargeSeconds >= 0.0 && ChargeSeconds >= Set->GetChargeTime() - AttackRequestTolerance ? Set->ChargedAttack : 0;
		RecordAttack(OutIndex, OutPlayRate);
		return true;
	}
	// An air attack or a plunge (D169): taken as asked while this machine's character falls, or landed within the
	// request's allowance, and its fall had none; else refused (the owner's montage plays on alone).
	if (Set->IsAirAttack(RequestedIndex))
	{
		if (!CanAirAttack(RequestedIndex == Set->GetAirAttack(true), AttackRequestTolerance))
		{
			return false;
		}
		OutIndex = RequestedIndex;
		RecordAttack(OutIndex, OutPlayRate);
		// Still falling here: this fall has had its air attack. Landed already (within the allowance): the attack is at its
		// landing, and the next fall is free.
		bAirAttackThisFall = IsFalling();
		if (!bAirAttackThisFall)
		{
			CountAirAttackFromLanding();
		}
		return true;
	}
	// The run attack (D168): taken while this machine's character sprints fast enough, the request's allowance taken off
	// the speed; else the server's pick for the primary below.
	if (Set->HasRunAttack() && RequestedIndex == Set->RunAttack
		&& CanRunAttack(GetRequestTolerance(GetOwner()) > 0.f ? RunAttackSpeedTolerance : 0.f))
	{
		OutIndex = Set->RunAttack;
		RecordAttack(OutIndex, OutPlayRate);
		return true;
	}
	const bool bInWindow = ComboIndex >= 0 && AttackComboCount == Count && Since >= 0.0
		&& Since <= AttackEndTime + AttackComboWindow + AttackRequestTolerance;
	// The button the owner pressed, read from its index (D166: each attack of the table is one button's).
	const bool bSecondary = Set->IsSecondaryAttack(RequestedIndex);
	const int32 Next = bInWindow ? Set->GetNextAttack(ComboIndex, bSecondary) : Set->GetFirstAttack(bSecondary);
	const bool bFirst = RequestedIndex >= 0 && (RequestedIndex == Set->GetFirstAttack(false) || RequestedIndex == Set->GetFirstAttack(true));
	// The owner's index when the server's rule allows it (a button's first, or its next within the window), else the
	// server's for that button.
	OutIndex = (bFirst || RequestedIndex == Next) ? RequestedIndex : Next;
	if (!Set->AttackCombo.IsValidIndex(OutIndex))
	{
		OutIndex = 0;
	}
	RecordAttack(OutIndex, OutPlayRate);
	return true;
}

void UVCActionMontageComponent::BufferAttack(bool bSecondary)
{
	bAttackBuffered = true;
	bBufferedSecondary = bSecondary;
	SetComponentTickEnabled(true);
}

bool UVCActionMontageComponent::TakeDueBufferedAttack()
{
	// A press kept during a dodge or a roll waits for its recovery (D170), one during an attack for the attack's.
	if (!bAttackBuffered || IsMoveBeforeRecovery() || (AttackStartTime >= 0.0 && GetSecondsSinceAttack() < AttackRecoveryTime))
	{
		return false;
	}
	bAttackBuffered = false;
	return true;
}

bool UVCActionMontageComponent::StopAttackForGuard()
{
	float Position = 0.f;
	UAnimMontage* Attack = GetPlayingAttackMontage(Position);
	UAnimInstance* AnimInstance = Attack ? GetBodyAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return false;
	}
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	AnimInstance->Montage_Stop(Set ? Set->RecoveryBlendOut : 0.2f, Attack);
	return true;
}

void UVCActionMontageComponent::BufferGuard()
{
	bGuardBuffered = true;
	bAttackBuffered = false;
}

bool UVCActionMontageComponent::TakeDueBufferedGuard()
{
	// The guard rises at the attack's recovery (D172); a dodge or a roll drops it (RecordMove), so none is before its own.
	if (!bGuardBuffered || IsMoveBeforeRecovery() || IsAttackBeforeRecovery())
	{
		return false;
	}
	bGuardBuffered = false;
	return true;
}

bool UVCActionMontageComponent::WantsToMove() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return false;
	}
	if (Character->GetLocalRole() == ROLE_SimulatedProxy)
	{
		const FVector Up = FVCGravityFrame::GetUp(Movement);
		const FVector Velocity = Character->GetVelocity();
		return (Velocity - Up * FVector::DotProduct(Velocity, Up)).SizeSquared() > FMath::Square(MovingSpeed);
	}
	return !Movement->GetCurrentAcceleration().IsNearlyZero();
}

UAnimMontage* UVCActionMontageComponent::GetPlayingAttackMontage(float& OutPosition) const
{
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	const UAnimInstance* AnimInstance = Set ? GetBodyAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return nullptr;
	}
	for (const TObjectPtr<UAnimMontage>& Montage : Set->AttackCombo)
	{
		// Playing and not already on its way out (a break or the next attack blending over it).
		const FAnimMontageInstance* Instance = Montage ? AnimInstance->GetActiveInstanceForMontage(Montage) : nullptr;
		if (Instance && Instance->IsActive() && !Instance->IsStopped())
		{
			OutPosition = Instance->GetPosition();
			return Montage;
		}
	}
	return nullptr;
}

UAnimMontage* UVCActionMontageComponent::GetPlayingMoveMontage(float& OutPosition) const
{
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	const UAnimInstance* AnimInstance = Set ? GetBodyAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return nullptr;
	}
	UAnimMontage* Moves[] = { Set->Roll.Get(), Set->Dodge.Front.Get(), Set->Dodge.Back.Get(), Set->Dodge.Left.Get(), Set->Dodge.Right.Get() };
	for (UAnimMontage* Montage : Moves)
	{
		const FAnimMontageInstance* Instance = Montage ? AnimInstance->GetActiveInstanceForMontage(Montage) : nullptr;
		if (Instance && Instance->IsActive() && !Instance->IsStopped())
		{
			OutPosition = Instance->GetPosition();
			return Montage;
		}
	}
	return nullptr;
}

void UVCActionMontageComponent::BeginPlay()
{
	Super::BeginPlay();
	// The server decides what the combat events look like and tells every machine; clients only play.
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		if (UVCCombatComponent* Combat = GetOwner()->FindComponentByClass<UVCCombatComponent>())
		{
			Combat->OnDamaged.AddUniqueDynamic(this, &UVCActionMontageComponent::HandleDamaged);
			Combat->OnDied.AddUniqueDynamic(this, &UVCActionMontageComponent::HandleDied);
			Combat->OnRevived.AddUniqueDynamic(this, &UVCActionMontageComponent::HandleRevived);
		}
	}
}

void UVCActionMontageComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UVCCombatComponent* Combat = GetOwner() ? GetOwner()->FindComponentByClass<UVCCombatComponent>() : nullptr)
	{
		Combat->OnDamaged.RemoveDynamic(this, &UVCActionMontageComponent::HandleDamaged);
		Combat->OnDied.RemoveDynamic(this, &UVCActionMontageComponent::HandleDied);
		Combat->OnRevived.RemoveDynamic(this, &UVCActionMontageComponent::HandleRevived);
	}
	Super::EndPlay(EndPlayReason);
}

const UVCActionAnimSet* UVCActionMontageComponent::FindActionAnimSet(const FGameplayTag& ActionSet) const
{
	const UVCActionAnimSet* Unarmed = nullptr;
	for (const UVCActionAnimSet* Set : ActionAnimSets)
	{
		if (!Set)
		{
			continue;
		}
		if (Set->ActionSet == ActionSet)
		{
			return Set;
		}
		if (Set->ActionSet == VCGameplayTags::Equipment_AnimSet_Unarmed)
		{
			Unarmed = Set;
		}
	}
	return Unarmed;
}

const UVCActionAnimSet* UVCActionMontageComponent::GetCurrentActionAnimSet() const
{
	const IVCAnimatedCharacter* Animated = Cast<IVCAnimatedCharacter>(GetOwner());
	return FindActionAnimSet(Animated ? Animated->GetAnimActionSet() : VCGameplayTags::Equipment_AnimSet_Unarmed.GetTag());
}

UAnimMontage* UVCActionMontageComponent::GetMontage(EVCAnimAction Action, int32 Variant) const
{
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	return Set ? Set->GetMontage(Action, Variant) : nullptr;
}

UAnimInstance* UVCActionMontageComponent::GetBodyAnimInstance() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	return Body ? Body->GetAnimInstance() : nullptr;
}

float UVCActionMontageComponent::PlayActionLocal(EVCAnimAction Action, int32 Variant, float PlayRate)
{
	++ActionsPlayed;
	LastAction = Action;

	UAnimMontage* Montage = GetMontage(Action, Variant);
	UAnimInstance* AnimInstance = Montage ? GetBodyAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return 0.f;
	}
	// Clicking again while digging or chopping keeps the swing going rather than restarting it every click.
	const bool bRepeatable = Action == EVCAnimAction::Dig || Action == EVCAnimAction::Chop
		|| Action == EVCAnimAction::Place || Action == EVCAnimAction::Use;
	if (bRepeatable && AnimInstance->Montage_IsPlaying(Montage))
	{
		return 0.f;
	}
	const float Length = AnimInstance->Montage_Play(Montage, FMath::Max(PlayRate, 0.01f));
	if ((Action == EVCAnimAction::Attack || Action == EVCAnimAction::Dodge || Action == EVCAnimAction::Roll) && Length > 0.f)
	{
		// The recovery's break (D103) is watched while the attack, the dodge or the roll plays.
		SetComponentTickEnabled(true);
	}
	// An air attack or a plunge played on the ground (taken within the allowance after a landing, or reaching a machine
	// that already landed) starts at its landing (D169).
	const UVCActionAnimSet* PlayedSet = Action == EVCAnimAction::Attack && Length > 0.f ? GetCurrentActionAnimSet() : nullptr;
	if (PlayedSet && PlayedSet->IsAirAttack(Variant) && !IsFalling() && Montage->GetSectionIndex(UVCActionAnimSet::LandingSection) != INDEX_NONE)
	{
		AnimInstance->Montage_JumpToSection(UVCActionAnimSet::LandingSection, Montage);
	}
	// The charge's variant 1 is its lowering (D167), the End section.
	if (Action == EVCAnimAction::Charge && Variant == 1 && Length > 0.f && Montage->GetSectionIndex(UVCActionAnimSet::ChargeEndSection) != INDEX_NONE)
	{
		AnimInstance->Montage_JumpToSection(UVCActionAnimSet::ChargeEndSection, Montage);
	}
	// Raised by the aim already: the draw pulls the string from there (its first section is the raise).
	const UVCAnimInstance* Body = Cast<UVCAnimInstance>(AnimInstance);
	if (Action == EVCAnimAction::Draw && Length > 0.f && Body && Body->AimRaiseWeight > 0.5f && Montage->CompositeSections.Num() >= 3)
	{
		AnimInstance->Montage_JumpToSection(Montage->GetSectionName(1), Montage);
	}
	return Length;
}

void UVCActionMontageComponent::PlayActionForEveryone(EVCAnimAction Action, int32 Variant, float PlayRate, bool bOwnerPlayedIt)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	Multicast_PlayAction(Action, static_cast<uint8>(FMath::Clamp(Variant, 0, 255)), PlayRate, bOwnerPlayedIt);
}

bool UVCActionMontageComponent::StopActionLocal(EVCAnimAction Action)
{
	float Position = 0.f;
	UAnimMontage* Montage = const_cast<UAnimMontage*>(GetPlayingActionMontage(Action, Position));
	UAnimInstance* AnimInstance = Montage ? GetBodyAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return false;
	}
	AnimInstance->Montage_Stop(Montage->GetDefaultBlendOutTime(), Montage);
	return true;
}

bool UVCActionMontageComponent::IsActionPlaying(EVCAnimAction Action) const
{
	float Position = 0.f;
	return GetPlayingActionMontage(Action, Position) != nullptr;
}

const UAnimMontage* UVCActionMontageComponent::GetPlayingActionMontage(EVCAnimAction Action, float& OutPosition) const
{
	const UAnimInstance* AnimInstance = GetBodyAnimInstance();
	if (!AnimInstance)
	{
		return nullptr;
	}
	// A reload has one montage per place the arrow comes from; the other actions stop and ask by their first.
	const int32 Variants = Action == EVCAnimAction::Reload ? 2 : 1;
	for (int32 Variant = 0; Variant < Variants; ++Variant)
	{
		const UAnimMontage* Montage = GetMontage(Action, Variant);
		if (Montage && AnimInstance->Montage_IsPlaying(Montage))
		{
			OutPosition = AnimInstance->Montage_GetPosition(Montage);
			return Montage;
		}
	}
	return nullptr;
}

void UVCActionMontageComponent::StopActionForEveryone(EVCAnimAction Action, bool bOwnerStoppedIt)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	Multicast_StopAction(Action, bOwnerStoppedIt);
}

float UVCActionMontageComponent::PlaySheathActionLocal(EVCAnimAction Action, const FGameplayTag& ActionSet, const FGameplayTag& Sheath)
{
	++ActionsPlayed;
	LastAction = Action;

	float HandOff = 0.f;
	UAnimMontage* Montage = GetSheathMontage(Action, ActionSet, Sheath, HandOff);
	UAnimInstance* AnimInstance = Montage ? GetBodyAnimInstance() : nullptr;
	if (!AnimInstance)
	{
		return 0.f;
	}
	SheathAction = Action;
	SheathSlot = Sheath;
	SheathMontage = Montage;
	return AnimInstance->Montage_Play(Montage, 1.f);
}

void UVCActionMontageComponent::PlaySheathActionForEveryone(EVCAnimAction Action, const FGameplayTag& ActionSet, const FGameplayTag& Sheath, bool bOwnerPlayedIt)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	Multicast_PlaySheathAction(Action, ActionSet, Sheath, bOwnerPlayedIt);
}

UAnimMontage* UVCActionMontageComponent::GetSheathMontage(EVCAnimAction Action, const FGameplayTag& ActionSet, const FGameplayTag& Sheath, float& OutHandOff) const
{
	const UVCActionAnimSet* Set = FindActionAnimSet(ActionSet);
	UAnimMontage* Montage = Set ? Set->GetSheathMontage(Action, Sheath) : nullptr;
	OutHandOff = UVCActionAnimSet::GetHandOffTime(Montage);
	return Montage;
}

bool UVCActionMontageComponent::IsSheathHandOffPending(const FGameplayTag& Sheath, EVCAnimAction& OutAction) const
{
	const UAnimMontage* Montage = SheathMontage.Get();
	const UAnimInstance* AnimInstance = Montage && SheathSlot == Sheath ? GetBodyAnimInstance() : nullptr;
	if (!AnimInstance || !AnimInstance->Montage_IsPlaying(Montage))
	{
		return false;
	}
	OutAction = SheathAction;
	return AnimInstance->Montage_GetPosition(Montage) < UVCActionAnimSet::GetHandOffTime(Montage);
}

bool UVCActionMontageComponent::IsSheathActionPlaying() const
{
	const UAnimMontage* Montage = SheathMontage.Get();
	const UAnimInstance* AnimInstance = Montage ? GetBodyAnimInstance() : nullptr;
	return AnimInstance && AnimInstance->Montage_IsPlaying(Montage);
}

int32 UVCActionMontageComponent::GetHitDirection(const FCGFDamageContext& Context) const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
	{
		return static_cast<int32>(EVCHitDirection::Front);
	}
	const AActor* Source = Context.CauserActor.IsValid() ? Context.CauserActor.Get() : Context.InstigatorActor.Get();
	const FVector From = Source ? Source->GetActorLocation() : Context.HitLocation;
	if (From.IsZero() || Source == Character)
	{
		return static_cast<int32>(EVCHitDirection::Front);
	}
	const FVector Up = FVCGravityFrame::GetUp(Character->GetCharacterMovement());
	return static_cast<int32>(UVCActionAnimSet::DirectionOfHit(Character->GetActorForwardVector(), Up, From - Character->GetActorLocation()));
}

void UVCActionMontageComponent::HandleDamaged(const FCGFDamageContext& Context)
{
	// A hit interrupts a charge (D167): the flinch stops its montage on every machine, and the server's clock ends with it.
	EndCharge();
	// A killing or knocking-out hit already played its death (OnDied comes first); the dead do not flinch.
	const UVCCombatComponent* Combat = GetOwner() ? GetOwner()->FindComponentByClass<UVCCombatComponent>() : nullptr;
	if (!Combat || ICGFDamageableInterface::Execute_IsDead(Combat) || Combat->IsDowned())
	{
		return;
	}
	// A hit on the guard shakes the shield instead (D94); a guard it broke staggers already (UVCBlockAbility::BreakGuard).
	if (Context.ContextTags.HasTagExact(CGFGameplayTags::Damage_Blocked))
	{
		const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner(), false);
		if (!ASC || !ASC->HasMatchingGameplayTag(CGFGameplayTags::State_GuardBroken))
		{
			PlayActionForEveryone(EVCAnimAction::BlockHit);
		}
		return;
	}
	PlayActionForEveryone(EVCAnimAction::HitReact, GetHitDirection(Context));
}

void UVCActionMontageComponent::HandleDied(const FCGFDamageContext& Context)
{
	// A body with a physics asset ragdolls on every machine (the characters' HandleDied); one without plays its death.
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* Body = Character ? Character->GetMesh() : nullptr;
	if (Body && Body->GetSkeletalMeshAsset() && !Body->GetPhysicsAsset())
	{
		PlayActionForEveryone(EVCAnimAction::Death, GetHitDirection(Context));
	}
}

void UVCActionMontageComponent::HandleRevived()
{
	PlayActionForEveryone(EVCAnimAction::Revive);
}

void UVCActionMontageComponent::Multicast_PlayAction_Implementation(EVCAnimAction Action, uint8 Variant, float PlayRate, bool bOwnerPlayedIt)
{
	// A dedicated server draws nothing, but plays a montage with root motion so its movement takes the lunge as the
	// owner's does (D106); the owner already played what it started at input.
	if (GetNetMode() == NM_DedicatedServer)
	{
		const UAnimMontage* Montage = GetMontage(Action, Variant);
		if (Montage && Montage->HasRootMotion())
		{
			PlayActionLocal(Action, Variant, PlayRate);
		}
		return;
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (bOwnerPlayedIt && Pawn && Pawn->IsLocallyControlled())
	{
		return;
	}
	PlayActionLocal(Action, Variant, PlayRate);
}

void UVCActionMontageComponent::Multicast_PlaySheathAction_Implementation(EVCAnimAction Action, FGameplayTag ActionSet, FGameplayTag Sheath, bool bOwnerPlayedIt)
{
	// As Multicast_PlayAction: a dedicated server draws nothing, the owner already played what it started at input.
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (bOwnerPlayedIt && Pawn && Pawn->IsLocallyControlled())
	{
		return;
	}
	PlaySheathActionLocal(Action, ActionSet, Sheath);
}

void UVCActionMontageComponent::Multicast_StopAction_Implementation(EVCAnimAction Action, bool bOwnerStoppedIt)
{
	// A dedicated server stops what it plays: a montage with root motion (Multicast_PlayAction), the charge's among them,
	// whose held loop would otherwise go on there (the sword moves plan, D167).
	if (GetNetMode() == NM_DedicatedServer)
	{
		StopActionLocal(Action);
		return;
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (bOwnerStoppedIt && Pawn && Pawn->IsLocallyControlled())
	{
		return;
	}
	StopActionLocal(Action);
}
