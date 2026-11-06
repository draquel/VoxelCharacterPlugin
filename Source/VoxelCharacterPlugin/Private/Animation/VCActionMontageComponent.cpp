// Copyright Daniel Raquel. All Rights Reserved.

#include "Animation/VCActionMontageComponent.h"
#include "Core/VCAnimatedCharacter.h"
#include "Core/VCGameplayTags.h"
#include "Combat/VCCombatComponent.h"
#include "Interfaces/CGFDamageableInterface.h"
#include "Movement/VCGravityFrame.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Engine/World.h"

UVCActionMontageComponent::UVCActionMontageComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
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

int32 UVCActionMontageComponent::ChooseAttack(float AttackSpeed, float& OutPlayRate)
{
	OutPlayRate = 1.f;
	const UVCActionAnimSet* Set = GetCurrentActionAnimSet();
	const int32 Count = Set ? Set->AttackCombo.Num() : 0;
	if (Count == 0)
	{
		return 0;
	}
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	ComboIndex = (ComboIndex >= 0 && Now <= ComboWindowEnd) ? (ComboIndex + 1) % Count : 0;

	// A swing fits one attack's interval: a montage longer than 1 / AttackSpeed plays faster, a shorter one at its own pace.
	const UAnimMontage* Montage = Set->AttackCombo[ComboIndex];
	const float Length = Montage ? Montage->GetPlayLength() : 0.f;
	OutPlayRate = FMath::Clamp(Length * FMath::Max(AttackSpeed, 0.01f), 1.f, 3.f);
	ComboWindowEnd = Now + Length / OutPlayRate + Set->ComboWindow;
	return ComboIndex;
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
	return AnimInstance->Montage_Play(Montage, FMath::Max(PlayRate, 0.01f));
}

void UVCActionMontageComponent::PlayActionForEveryone(EVCAnimAction Action, int32 Variant, float PlayRate, bool bOwnerPlayedIt)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}
	Multicast_PlayAction(Action, static_cast<uint8>(FMath::Clamp(Variant, 0, 255)), PlayRate, bOwnerPlayedIt);
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
	// A dedicated server draws nothing; the owner already played what it started at input.
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (bOwnerPlayedIt && Pawn && Pawn->IsLocallyControlled())
	{
		return;
	}
	PlayActionLocal(Action, Variant, PlayRate);
}
