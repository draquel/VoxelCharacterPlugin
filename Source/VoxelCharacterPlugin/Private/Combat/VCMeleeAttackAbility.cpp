// Copyright Daniel Raquel. All Rights Reserved.

#include "Combat/VCMeleeAttackAbility.h"
#include "Combat/VCCombatStatics.h"
#include "Animation/VCActionMontageComponent.h"
#include "Abilities/Tasks/AbilityTask_WaitDelay.h"
#include "Utilities/CGFCombatStatics.h"
#include "Tags/CGFGameplayTags.h"
#include "VoxelCharacterPlugin.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/OverlapResult.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Movement/VCGravityFrame.h"

#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
#include "Components/EquipmentManagerComponent.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Data/ItemDefinition.h"
#include "Data/Fragments/ItemFragment_Weapon.h"
#include "Data/Fragments/ItemFragment_RangedWeapon.h"
#include "Data/Fragments/ItemFragment_Durability.h"
#include "Types/ItemInstanceFragments.h"
#include "Engine/GameInstance.h"
#endif

UVCMeleeAttackAbility::UVCMeleeAttackAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(CGFGameplayTags::Ability_Attack_Melee);
	SetAssetTags(AssetTags);

	ActivationBlockedTags.AddTag(CGFGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(CGFGameplayTags::State_Downed);
}

UVCMeleeAttackAbility::FAttackParameters UVCMeleeAttackAbility::ResolveAttackParameters(const AActor* Avatar) const
{
	FAttackParameters Out;
	Out.Damage = UnarmedDamage;
	Out.DamageType = CGFGameplayTags::Damage_Type_Physical;
	Out.AttackSpeed = UnarmedAttackSpeed;

#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
	const UEquipmentManagerComponent* Equipment = Avatar ? Avatar->FindComponentByClass<UEquipmentManagerComponent>() : nullptr;
	if (!Equipment)
	{
		return Out;
	}

	const FItemInstance MainHand = Equipment->GetHeldItem(CGFGameplayTags::Equipment_Slot_MainHand); // its own slot's or drawn from a sheath
	if (!MainHand.IsValid())
	{
		return Out;
	}

	const UWorld* World = Avatar->GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UItemDatabaseSubsystem* ItemDB = GameInstance ? GameInstance->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	const UItemDefinition* Definition = ItemDB ? ItemDB->GetDefinition(MainHand.ItemDefinitionId) : nullptr;
	const UItemFragment_Weapon* Weapon = Definition ? Definition->FindFragment<UItemFragment_Weapon>() : nullptr;
	if (!Weapon)
	{
		// A tool or shield in the main hand still swings, at unarmed numbers.
		Out.ItemInstanceId = MainHand.InstanceId;
		return Out;
	}

	Out.Damage = Weapon->BaseDamage;
	if (Weapon->DamageType.IsValid())
	{
		Out.DamageType = Weapon->DamageType;
	}
	if (Weapon->AttackSpeed > 0.f)
	{
		Out.AttackSpeed = Weapon->AttackSpeed;
	}
	Out.CritChance = FMath::Clamp(Weapon->CritChance, 0.f, 1.f);
	Out.CritMultiplier = FMath::Max(1.f, Weapon->CritMultiplier);
	Out.ItemInstanceId = MainHand.InstanceId;

	// Feature 5b: a weapon worn down to zero (without DestroyAtZero) still swings, at half damage.
	if (Definition->FindFragment<UItemFragment_Durability>())
	{
		if (const UInstanceFragment_DurabilityState* State = MainHand.FindFragment<UInstanceFragment_DurabilityState>())
		{
			if (State->CurrentDurability <= 0.f)
			{
				Out.Damage *= WornOutDamageMultiplier;
				Out.bWornOut = true;
			}
		}
	}
#endif
	return Out;
}

float UVCMeleeAttackAbility::GetAttackSpeed(const AActor* Avatar) const
{
	return ResolveAttackParameters(Avatar).AttackSpeed;
}

bool UVCMeleeAttackAbility::HoldsMeleeWeapon(const AActor* Avatar)
{
#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
	const UEquipmentManagerComponent* Equipment = Avatar ? Avatar->FindComponentByClass<UEquipmentManagerComponent>() : nullptr;
	const FItemInstance MainHand = Equipment ? Equipment->GetHeldItem(CGFGameplayTags::Equipment_Slot_MainHand) : FItemInstance();
	if (!MainHand.IsValid())
	{
		return false;
	}
	const UWorld* World = Avatar->GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UItemDatabaseSubsystem* ItemDB = GameInstance ? GameInstance->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	const UItemDefinition* Definition = ItemDB ? ItemDB->GetDefinition(MainHand.ItemDefinitionId) : nullptr;
	return Definition && Definition->FindFragment<UItemFragment_Weapon>() && !Definition->FindFragment<UItemFragment_RangedWeapon>();
#else
	return false;
#endif
}

void UVCMeleeAttackAbility::ApplyWeaponWear(AActor* Avatar) const
{
#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
	UEquipmentManagerComponent* Equipment = Avatar ? Avatar->FindComponentByClass<UEquipmentManagerComponent>() : nullptr;
	if (!Equipment)
	{
		return;
	}
	const FItemInstance MainHand = Equipment->GetHeldItem(CGFGameplayTags::Equipment_Slot_MainHand); // its own slot's or drawn from a sheath
	if (!MainHand.IsValid())
	{
		return;
	}
	const UWorld* World = Avatar->GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UItemDatabaseSubsystem* ItemDB = GameInstance ? GameInstance->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	const UItemDefinition* Definition = ItemDB ? ItemDB->GetDefinition(MainHand.ItemDefinitionId) : nullptr;
	const UItemFragment_Durability* Durability = Definition ? Definition->FindFragment<UItemFragment_Durability>() : nullptr;
	if (!Durability || Durability->DegradeRate <= 0.f)
	{
		return;
	}
	const float Remaining = Equipment->ApplyDurabilityLoss(Equipment->GetHeldSlotTag(CGFGameplayTags::Equipment_Slot_MainHand), Durability->DegradeRate);
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s weapon wear: -%.1f -> %.1f"), *GetNameSafe(Avatar), Durability->DegradeRate, Remaining);
#endif
}

void UVCMeleeAttackAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEndAbility*/ true, /*bWasCancelled*/ true);
		return;
	}

	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!Avatar || !World || !Avatar->HasAuthority())
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	FAttackParameters Attack = ResolveAttackParameters(Avatar);
	const double Now = World->GetTimeSeconds();
	UVCActionMontageComponent* Montages = Avatar->FindComponentByClass<UVCActionMontageComponent>();
	const UAnimMontage* AttackMontage = Montages ? Montages->GetMontage(EVCAnimAction::Attack, 0) : nullptr;

	// The swing's montage: everyone but the owning player (who played its own at input) sees it, and the hit waits
	// for the montage's hit time. No montage, no wait.
	float HitDelay = 0.f;
	if (AttackMontage)
	{
		// The combo's own rule (D104): the attack before must have reached its recovery, and the owner's pick is taken
		// when the rule allows it (G179: on a machine that is owner and server, it is the pick already made).
		float PlayRate = 1.f;
		int32 Variant = 0;
		if (!Montages->AcceptAttack(Montages->TakeRequestedAttack(), Attack.AttackSpeed, Variant, PlayRate))
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		LastAttackTimeSeconds = Now;
		// The attack's own share of the weapon's damage (its combo step's: the charged strike's twice, D167).
		if (const UVCActionAnimSet* Set = Montages->GetCurrentActionAnimSet())
		{
			Attack.Damage *= Set->GetAttackDamageMultiplier(Variant);
		}
		if (const UAnimMontage* Montage = Montages->GetMontage(EVCAnimAction::Attack, Variant))
		{
			const APawn* Pawn = Cast<APawn>(Avatar);
			Montages->PlayActionForEveryone(EVCAnimAction::Attack, Variant, PlayRate, Pawn && Pawn->IsPlayerControlled());
			HitDelay = UVCActionAnimSet::GetHitTime(Montage) / PlayRate;
		}
		// The plunge (D169): its hit is the slam at this machine's landing, however long the fall; taken within the
		// allowance after a landing, it slams now.
		const UVCActionAnimSet* Set = Montages->GetCurrentActionAnimSet();
		if (Set && Variant == Set->GetAirAttack(/*bSecondary*/ true))
		{
			PendingAttack = Attack;
			PlungeRadius = Set->PlungeRadius;
			if (!Montages->IsFalling())
			{
				ApplySlam(Avatar, Attack, PlungeRadius);
				EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
				return;
			}
			bPlungePending = true;
			PlungeMontages = Montages;
			PlungeLandedHandle = Montages->OnFallEnded.AddUObject(this, &UVCMeleeAttackAbility::OnPlungeLanded);
			return;
		}
	}
	else
	{
		// Without a montage: one swing per 1/AttackSpeed seconds.
		if (Montages)
		{
			Montages->TakeRequestedAttack();
		}
		const double Interval = 1.0 / FMath::Max(0.01f, Attack.AttackSpeed);
		if (LastAttackTimeSeconds >= 0.0 && (Now - LastAttackTimeSeconds) < Interval)
		{
			EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
			return;
		}
		LastAttackTimeSeconds = Now;
	}

	if (HitDelay <= UE_KINDA_SMALL_NUMBER)
	{
		ApplySwing(Avatar, Attack);
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}
	PendingAttack = Attack;
	UAbilityTask_WaitDelay* Wait = UAbilityTask_WaitDelay::WaitDelay(this, HitDelay);
	Wait->OnFinish.AddDynamic(this, &UVCMeleeAttackAbility::OnHitTime);
	Wait->ReadyForActivation();
}

void UVCMeleeAttackAbility::OnHitTime()
{
	AActor* Avatar = GetAvatarActorFromActorInfo();
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	// Killed or knocked out during the wind-up: the swing never lands.
	const bool bDown = ASC && (ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Dead) || ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Downed));
	if (Avatar && !bDown)
	{
		ApplySwing(Avatar, PendingAttack);
	}
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

void UVCMeleeAttackAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	StopWaitingForLanding();
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UVCMeleeAttackAbility::StopWaitingForLanding()
{
	if (UVCActionMontageComponent* Montages = PlungeMontages.Get())
	{
		Montages->OnFallEnded.Remove(PlungeLandedHandle);
	}
	PlungeMontages.Reset();
	PlungeLandedHandle.Reset();
	bPlungePending = false;
}

void UVCMeleeAttackAbility::OnPlungeLanded(bool bOnGround)
{
	if (!bPlungePending)
	{
		return;
	}
	StopWaitingForLanding();
	AActor* Avatar = GetAvatarActorFromActorInfo();
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	const bool bDown = ASC && (ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Dead) || ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Downed));
	// Into the water the plunge ends without a slam.
	if (Avatar && bOnGround && !bDown)
	{
		ApplySlam(Avatar, PendingAttack, PlungeRadius);
	}
	EndAbility(GetCurrentAbilitySpecHandle(), GetCurrentActorInfo(), GetCurrentActivationInfo(), true, false);
}

bool UVCMeleeAttackAbility::IsInSlam(const FVector& Feet, const FVector& Up, const FVector& Target, float Radius)
{
	const FVector Offset = Target - Feet;
	const double Along = FVector::DotProduct(Offset, Up);
	return FMath::Abs(Along) <= Radius && (Offset - Up * Along).SizeSquared() <= FMath::Square(static_cast<double>(Radius));
}

void UVCMeleeAttackAbility::ApplySlam(AActor* Avatar, const FAttackParameters& Attack, float Radius) const
{
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	const ACharacter* Character = Cast<ACharacter>(Avatar);
	if (!World || !Character)
	{
		return;
	}
	const FVector Up = FVCGravityFrame::GetUp(Character->GetCharacterMovement());
	const FVector Feet = Character->GetActorLocation() - Up * Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	// Candidates by the bodies' collision near the feet, then the slam's own reach from the feet to each one's place.
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VCPlungeSlam), /*bTraceComplex*/ false, Avatar);
	World->OverlapMultiByObjectType(Overlaps, Feet, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius * 1.5f), Params);
	TSet<AActor*> Hit;
	int32 Landed = 0;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Target = Overlap.GetActor();
		if (!Target || Target == Avatar || Hit.Contains(Target) || !UCGFCombatStatics::FindDamageable(Target).GetObject()
			|| !UCGFCombatStatics::AreHostile(Avatar, Target) || !IsInSlam(Feet, Up, Target->GetActorLocation(), Radius))
		{
			continue;
		}
		Hit.Add(Target);
		float Damage = Attack.Damage;
		const bool bCritical = Attack.CritChance > 0.f && FMath::FRand() < Attack.CritChance;
		if (bCritical)
		{
			Damage *= Attack.CritMultiplier;
		}
		FCGFDamageContext Context = UVCCombatStatics::MakeDamageContext(Avatar, Avatar, Damage, Attack.DamageType);
		Context.HitLocation = Target->GetActorLocation();
		Context.HitNormal = Up;
		Context.SourceItemInstanceId = Attack.ItemInstanceId;
		if (bCritical)
		{
			Context.ContextTags.AddTag(CGFGameplayTags::Damage_Critical);
		}
		const ECGFDamageResult Result = UVCCombatStatics::ApplyDamageToActor(Target, Context);
		UE_LOG(LogVoxelCharacter, Log, TEXT("%s plunge slam -> %s: %.1f %s%s (%s)"), *GetNameSafe(Avatar), *GetNameSafe(Target), Damage,
			*Attack.DamageType.ToString(), bCritical ? TEXT(" CRIT") : TEXT(""), *UEnum::GetValueAsString(Result));
		Landed += Result == ECGFDamageResult::Applied ? 1 : 0;
	}
	UE_LOG(LogVoxelCharacter, Log, TEXT("%s plunge slam at (%.0f, %.0f, %.0f): %d hit, radius %.0f"), *GetNameSafe(Avatar), Feet.X, Feet.Y, Feet.Z, Hit.Num(), Radius);
	if (Landed > 0)
	{
		// One slam wears the weapon once, as a swing that lands does.
		ApplyWeaponWear(Avatar);
	}
}

void UVCMeleeAttackAbility::ApplySwing(AActor* Avatar, const FAttackParameters& Attack) const
{
	// The same sweep the client's target check runs (UVCCombatStatics::SweepHostileUnderView): eye location + control
	// rotation (replicated to the server), so the sweep does not depend on a client-only camera.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VCMeleeAttack), /*bTraceComplex*/ false, Avatar);
	const FVCViewSweep Sweep = UVCCombatStatics::SweepHostileUnderView(Avatar, MeleeRange, MeleeRadius, Params);
	AActor* HitActor = Sweep.Hostile;
	if (!HitActor)
	{
		// Swung at nothing hostile. Still a valid activation (the swing happened), just no hit.
		AActor* Met = Sweep.bHit ? Sweep.Hit.GetActor() : nullptr;
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s melee missed: hit %s (%s) at (%.0f, %.0f, %.0f) dist %.0f, from (%.0f, %.0f, %.0f) toward %s"), *GetNameSafe(Avatar),
			*GetNameSafe(Met), Met ? *GetNameSafe(Sweep.Hit.GetComponent()) : TEXT("-"), Sweep.Hit.ImpactPoint.X, Sweep.Hit.ImpactPoint.Y, Sweep.Hit.ImpactPoint.Z, Sweep.Hit.Distance,
			Sweep.ViewLocation.X, Sweep.ViewLocation.Y, Sweep.ViewLocation.Z, *Sweep.ViewRotation.ToCompactString());
		return;
	}

	// Crit is rolled here, on the server, from the weapon fragment's numbers; the context tag lets
	// UI / cues react and the damage execution treats it like any other base damage.
	float Damage = Attack.Damage;
	const bool bCritical = Attack.CritChance > 0.f && FMath::FRand() < Attack.CritChance;
	if (bCritical)
	{
		Damage *= Attack.CritMultiplier;
	}

	FCGFDamageContext Context = UVCCombatStatics::MakeDamageContext(Avatar, Avatar, Damage, Attack.DamageType);
	Context.HitLocation = Sweep.Hit.ImpactPoint;
	Context.HitNormal = Sweep.Hit.ImpactNormal;
	Context.SourceItemInstanceId = Attack.ItemInstanceId;
	if (bCritical)
	{
		Context.ContextTags.AddTag(CGFGameplayTags::Damage_Critical);
	}

	const ECGFDamageResult Result = UVCCombatStatics::ApplyDamageToActor(HitActor, Context);
	UE_LOG(LogVoxelCharacter, Log, TEXT("%s melee -> %s: %.1f %s%s%s (%s)"), *GetNameSafe(Avatar), *GetNameSafe(HitActor),
		Damage, *Attack.DamageType.ToString(), bCritical ? TEXT(" CRIT") : TEXT(""), Attack.bWornOut ? TEXT(" WORN") : TEXT(""), *UEnum::GetValueAsString(Result));
	if (Result == ECGFDamageResult::Applied)
	{
		// Feature 5b: every landed hit wears the main-hand weapon by its DegradeRate.
		ApplyWeaponWear(Avatar);
	}
}
