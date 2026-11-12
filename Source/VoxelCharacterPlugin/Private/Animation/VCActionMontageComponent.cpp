// Copyright Daniel Raquel. All Rights Reserved.

#include "Animation/VCActionMontageComponent.h"
#include "Core/VCAnimatedCharacter.h"
#include "Core/VCAnimInstance.h"
#include "Core/VCGameplayTags.h"
#include "Combat/VCCombatComponent.h"
#include "Interfaces/CGFDamageableInterface.h"
#include "Movement/VCGravityFrame.h"
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

bool UVCActionMontageComponent::ShouldBreakRecovery(double SinceStart, float RecoveryTime, float EndTime, bool bWantsToMove)
{
	return bWantsToMove && SinceStart >= RecoveryTime && SinceStart < EndTime;
}

float UVCActionMontageComponent::GetAttackPlayRate(float AttackSpeed)
{
	return FMath::Clamp(AttackSpeed, 0.5f, 2.f);
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
}

int32 UVCActionMontageComponent::ChooseAttack(float AttackSpeed, float& OutPlayRate)
{
	OutPlayRate = GetAttackPlayRate(AttackSpeed);
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	const int32 Count = Set ? Set->AttackCombo.Num() : 0;
	if (Count == 0)
	{
		return 0;
	}
	// The next of the combo when a press now continues it (one kept for the recovery does by then), else the first.
	const EVCAttackPress Press = ClassifyAttackPress();
	const bool bContinues = ComboIndex >= 0 && AttackComboCount == Count
		&& (Press == EVCAttackPress::Continue || Press == EVCAttackPress::Buffer);
	const int32 Index = bContinues ? (ComboIndex + 1) % Count : 0;
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
	const bool bInWindow = ComboIndex >= 0 && AttackComboCount == Count && Since >= 0.0
		&& Since <= AttackEndTime + AttackComboWindow + AttackRequestTolerance;
	const int32 Next = bInWindow ? (ComboIndex + 1) % Count : 0;
	// The owner's index when the server's rule allows it (the first, or the next within the window), else the server's.
	OutIndex = (RequestedIndex == 0 || RequestedIndex == Next) ? RequestedIndex : Next;
	RecordAttack(OutIndex, OutPlayRate);
	return true;
}

void UVCActionMontageComponent::BufferAttack()
{
	bAttackBuffered = true;
	SetComponentTickEnabled(true);
}

bool UVCActionMontageComponent::TakeDueBufferedAttack()
{
	if (!bAttackBuffered || GetSecondsSinceAttack() < AttackRecoveryTime)
	{
		return false;
	}
	bAttackBuffered = false;
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
	if (Action == EVCAnimAction::Attack && Length > 0.f)
	{
		// The recovery's break (D103) is watched while the attack plays.
		SetComponentTickEnabled(true);
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
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (bOwnerStoppedIt && Pawn && Pawn->IsLocallyControlled())
	{
		return;
	}
	StopActionLocal(Action);
}
