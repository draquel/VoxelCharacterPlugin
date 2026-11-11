// Copyright Daniel Raquel. All Rights Reserved.

#include "Combat/VCBlockAbility.h"
#include "Animation/VCActionMontageComponent.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Core/VCGameplayTags.h"
#include "Movement/VCGravityFrame.h"
#include "Tags/CGFGameplayTags.h"
#include "Types/CGFCombatTypes.h"
#include "VoxelCharacterPlugin.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
#include "Components/EquipmentManagerComponent.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Data/ItemDefinition.h"
#include "Data/Fragments/ItemFragment_Shield.h"
#include "Engine/GameInstance.h"
#endif

UVCBlockAbility::UVCBlockAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(VCGameplayTags::Ability_Block);
	SetAssetTags(AssetTags);

	// The guard is up exactly while the ability runs: the damage path reads this tag.
	ActivationOwnedTags.AddTag(CGFGameplayTags::State_Blocking);
	ActivationBlockedTags.AddTag(CGFGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(CGFGameplayTags::State_Downed);
	ActivationBlockedTags.AddTag(CGFGameplayTags::State_GuardBroken);
}

bool UVCBlockAbility::GetHeldShield(const AActor* Avatar, FVCShieldStats& OutStats)
{
#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
	const UEquipmentManagerComponent* Equipment = Avatar ? Avatar->FindComponentByClass<UEquipmentManagerComponent>() : nullptr;
	const FItemInstance OffHand = Equipment ? Equipment->GetHeldItem(CGFGameplayTags::Equipment_Slot_OffHand) : FItemInstance();
	if (!OffHand.IsValid())
	{
		return false;
	}
	const UWorld* World = Avatar->GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UItemDatabaseSubsystem* ItemDB = GameInstance ? GameInstance->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	const UItemDefinition* Definition = ItemDB ? ItemDB->GetDefinition(OffHand.ItemDefinitionId) : nullptr;
	const UItemFragment_Shield* Shield = Definition ? Definition->FindFragment<UItemFragment_Shield>() : nullptr;
	if (!Shield)
	{
		return false;
	}
	OutStats.DamageReduction = FMath::Clamp(Shield->BlockDamageReduction, 0.f, 1.f);
	OutStats.HalfAngle = FMath::Clamp(Shield->BlockHalfAngle, 0.f, 180.f);
	OutStats.StaminaPerDamage = FMath::Max(0.f, Shield->StaminaPerBlockedDamage);
	return true;
#else
	return false;
#endif
}

bool UVCBlockAbility::CanStartBlock(const AActor* Avatar)
{
	FVCShieldStats Shield;
	if (!GetHeldShield(Avatar, Shield))
	{
		return false;
	}
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Avatar, false);
	return ASC && !ASC->HasMatchingGameplayTag(CGFGameplayTags::State_GuardBroken) && !ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Dead)
		&& !ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Downed);
}

bool UVCBlockAbility::ReleaseBlock(UAbilitySystemComponent* ASC)
{
	if (!ASC)
	{
		return false;
	}
	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->IsA<UVCBlockAbility>())
		{
			if (ASC->IsOwnerActorAuthoritative())
			{
				ASC->AbilitySpecInputReleased(Spec);
			}
			else
			{
				// The release rides the component's channel, after the activation request it follows (as the bow's draw).
				ASC->ServerSetInputReleased(Spec.Handle);
			}
			return true;
		}
	}
	return false;
}

bool UVCBlockAbility::IsInBlockArc(const FVector& Forward, const FVector& Up, const FVector& ToSource, float HalfAngleDegrees)
{
	const FVector Normal = Up.GetSafeNormal();
	const FVector Facing = FVector::VectorPlaneProject(Forward, Normal).GetSafeNormal();
	const FVector Toward = FVector::VectorPlaneProject(ToSource, Normal).GetSafeNormal();
	if (Facing.IsNearlyZero() || Toward.IsNearlyZero())
	{
		return false;
	}
	return FVector::DotProduct(Facing, Toward) >= FMath::Cos(FMath::DegreesToRadians(HalfAngleDegrees)) - UE_KINDA_SMALL_NUMBER;
}

bool UVCBlockAbility::ResolveBlockedHit(float Damage, float Stamina, const FVCShieldStats& Shield, float& OutDamage, float& OutStaminaCost)
{
	Damage = FMath::Max(0.f, Damage);
	Stamina = FMath::Max(0.f, Stamina);
	const float Taken = Damage * Shield.DamageReduction;
	const float Cost = Taken * Shield.StaminaPerDamage;
	if (Shield.StaminaPerDamage <= 0.f || Cost <= Stamina)
	{
		OutDamage = Damage - Taken;
		OutStaminaCost = Cost;
		return false;
	}
	// The stamina pays for part of the share; the rest lands, and the guard gives.
	OutDamage = Damage - Stamina / Shield.StaminaPerDamage;
	OutStaminaCost = Stamina;
	return true;
}

bool UVCBlockAbility::ApplyGuard(AActor* Target, UAbilitySystemComponent* TargetASC, FCGFDamageContext& InOutContext)
{
	if (!Target || !TargetASC || !TargetASC->HasMatchingGameplayTag(CGFGameplayTags::State_Blocking)
		|| InOutContext.DamageType == CGFGameplayTags::Damage_Type_Pure)
	{
		return false;
	}
	FVCShieldStats Shield;
	if (!GetHeldShield(Target, Shield))
	{
		return false;
	}

	// Where the hit came from: its causer (an arrow), else its instigator, else the hit's location.
	const AActor* Source = InOutContext.CauserActor.IsValid() ? InOutContext.CauserActor.Get() : InOutContext.InstigatorActor.Get();
	const FVector From = Source && Source != Target ? Source->GetActorLocation() : InOutContext.HitLocation;
	if (From.IsZero())
	{
		return false;
	}
	const ACharacter* Character = Cast<ACharacter>(Target);
	const FVector Up = Character ? FVCGravityFrame::GetUp(Character->GetCharacterMovement()) : FVector::UpVector;
	if (!IsInBlockArc(Target->GetActorForwardVector(), Up, From - Target->GetActorLocation(), Shield.HalfAngle))
	{
		return false;
	}

	// A body with no stamina attribute (an NPC's) holds its guard whatever it takes.
	const FGameplayAttribute StaminaAttribute = UVCCharacterAttributeSet::GetStaminaAttribute();
	const bool bHasStamina = TargetASC->HasAttributeSetForAttribute(StaminaAttribute);
	const float Stamina = bHasStamina ? TargetASC->GetNumericAttribute(StaminaAttribute) : TNumericLimits<float>::Max();
	float Damage = 0.f;
	float StaminaCost = 0.f;
	const bool bBreaks = ResolveBlockedHit(InOutContext.BaseDamage, Stamina, Shield, Damage, StaminaCost);
	UE_LOG(LogVoxelCharacter, Log, TEXT("%s blocks: %.1f -> %.1f, stamina -%.1f%s"), *GetNameSafe(Target), InOutContext.BaseDamage, Damage,
		bHasStamina ? StaminaCost : 0.f, bBreaks ? TEXT(", the guard breaks") : TEXT(""));
	InOutContext.BaseDamage = Damage;
	InOutContext.ContextTags.AddTag(CGFGameplayTags::Damage_Blocked);
	if (bHasStamina && StaminaCost > 0.f)
	{
		TargetASC->ApplyModToAttribute(StaminaAttribute, EGameplayModOp::Additive, -StaminaCost);
	}
	if (bBreaks)
	{
		BreakGuard(Target, TargetASC);
	}
	return true;
}

void UVCBlockAbility::BreakGuard(AActor* Target, UAbilitySystemComponent* TargetASC)
{
	if (!Target || !TargetASC || !Target->HasAuthority())
	{
		return;
	}
	const FGameplayTagContainer BlockTags(VCGameplayTags::Ability_Block);
	TargetASC->CancelAbilities(&BlockTags);

	float Seconds = GetDefault<UVCBlockAbility>()->GuardBreakSeconds;
	for (const FGameplayAbilitySpec& Spec : TargetASC->GetActivatableAbilities())
	{
		if (const UVCBlockAbility* Block = Cast<UVCBlockAbility>(Spec.Ability))
		{
			Seconds = Block->GuardBreakSeconds;
			break;
		}
	}
	// Replicated, so the owner's own check (CanStartBlock) refuses a guard the server would refuse.
	TargetASC->SetLooseGameplayTagCount(CGFGameplayTags::State_GuardBroken, 1, EGameplayTagReplicationState::TagOnly);
	if (UWorld* World = Target->GetWorld())
	{
		FTimerHandle Handle;
		TWeakObjectPtr<UAbilitySystemComponent> WeakASC(TargetASC);
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(TargetASC, [WeakASC]()
		{
			if (UAbilitySystemComponent* ASC = WeakASC.Get())
			{
				ASC->SetLooseGameplayTagCount(CGFGameplayTags::State_GuardBroken, 0, EGameplayTagReplicationState::TagOnly);
			}
		}), FMath::Max(Seconds, 0.01f), false);
	}
	if (UVCActionMontageComponent* Montages = Target->FindComponentByClass<UVCActionMontageComponent>())
	{
		Montages->PlayActionForEveryone(EVCAnimAction::GuardBreak);
	}
}

bool UVCBlockAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	return CanStartBlock(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
}

void UVCBlockAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility*/ true, /*bWasCancelled*/ true);
		return;
	}
	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (!Avatar || !Avatar->HasAuthority())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	// The owning player raised its own guard at input; everyone else follows the server.
	if (UVCActionMontageComponent* Montages = Avatar->FindComponentByClass<UVCActionMontageComponent>())
	{
		const APawn* Pawn = Cast<APawn>(Avatar);
		Montages->PlayActionForEveryone(EVCAnimAction::Block, 0, 1.f, Pawn && Pawn->IsPlayerControlled());
	}
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s raises its guard"), *GetNameSafe(Avatar));
}

void UVCBlockAbility::InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	if (UVCActionMontageComponent* Montages = Avatar ? Avatar->FindComponentByClass<UVCActionMontageComponent>() : nullptr)
	{
		const APawn* Pawn = Cast<APawn>(Avatar);
		Montages->PlayActionForEveryone(EVCAnimAction::BlockEnd, 0, 1.f, Pawn && Pawn->IsPlayerControlled());
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, /*bWasCancelled*/ false);
}
