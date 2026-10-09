// Copyright Daniel Raquel. All Rights Reserved.

#include "Combat/VCMeleeAttackAbility.h"
#include "Combat/VCCombatStatics.h"
#include "Utilities/CGFCombatStatics.h"
#include "Tags/CGFGameplayTags.h"
#include "VoxelCharacterPlugin.h"
#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"

#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
#include "Components/EquipmentManagerComponent.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Data/ItemDefinition.h"
#include "Data/Fragments/ItemFragment_Weapon.h"
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

void UVCMeleeAttackAbility::ResolveAttackParameters(const AActor* Avatar, float& OutDamage, FGameplayTag& OutDamageType,
	float& OutAttackSpeed, FGuid& OutItemInstanceId) const
{
	OutDamage = UnarmedDamage;
	OutDamageType = CGFGameplayTags::Damage_Type_Physical;
	OutAttackSpeed = UnarmedAttackSpeed;
	OutItemInstanceId.Invalidate();

#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
	const UEquipmentManagerComponent* Equipment = Avatar ? Avatar->FindComponentByClass<UEquipmentManagerComponent>() : nullptr;
	if (!Equipment)
	{
		return;
	}

	const FItemInstance MainHand = Equipment->GetEquippedItem(CGFGameplayTags::Equipment_Slot_MainHand);
	if (!MainHand.IsValid())
	{
		return;
	}

	const UWorld* World = Avatar->GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UItemDatabaseSubsystem* ItemDB = GameInstance ? GameInstance->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	const UItemDefinition* Definition = ItemDB ? ItemDB->GetDefinition(MainHand.ItemDefinitionId) : nullptr;
	const UItemFragment_Weapon* Weapon = Definition ? Definition->FindFragment<UItemFragment_Weapon>() : nullptr;
	if (!Weapon)
	{
		// A tool or shield in the main hand still swings, at unarmed numbers.
		OutItemInstanceId = MainHand.InstanceId;
		return;
	}

	OutDamage = Weapon->BaseDamage;
	if (Weapon->DamageType.IsValid())
	{
		OutDamageType = Weapon->DamageType;
	}
	if (Weapon->AttackSpeed > 0.f)
	{
		OutAttackSpeed = Weapon->AttackSpeed;
	}
	OutItemInstanceId = MainHand.InstanceId;
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

	float Damage = 0.f;
	FGameplayTag DamageType;
	float AttackSpeed = 1.f;
	FGuid ItemInstanceId;
	ResolveAttackParameters(Avatar, Damage, DamageType, AttackSpeed, ItemInstanceId);

	// Rate limit: one swing per 1/AttackSpeed seconds. Cheaper and simpler than a cooldown effect
	// until attack animations give the swing a real duration.
	const double Now = World->GetTimeSeconds();
	const double Interval = 1.0 / FMath::Max(0.01f, AttackSpeed);
	if (LastAttackTimeSeconds >= 0.0 && (Now - LastAttackTimeSeconds) < Interval)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}
	LastAttackTimeSeconds = Now;

	// Server-side view: eye location + control rotation (replicated to the server), so the sweep
	// does not depend on a client-only camera.
	FVector ViewLocation;
	FRotator ViewRotation;
	Avatar->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	const FVector End = ViewLocation + ViewRotation.Vector() * MeleeRange;

	// ECC_Pawn: the engine's Pawn / CharacterMesh profiles ignore Visibility, so a visibility sweep
	// would pass through every character. Terrain and props still block the Pawn channel.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VCMeleeAttack), /*bTraceComplex*/ false, Avatar);
	FHitResult Hit;
	const bool bHit = World->SweepSingleByChannel(Hit, ViewLocation, End, FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeSphere(MeleeRadius), Params);

	AActor* HitActor = bHit ? Hit.GetActor() : nullptr;
	if (!HitActor || !UCGFCombatStatics::FindDamageable(HitActor).GetObject() || !UCGFCombatStatics::AreHostile(Avatar, HitActor))
	{
		// Swung at nothing hostile. Still a valid activation (the swing happened), just no hit.
		EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
		return;
	}

	FCGFDamageContext Context = UVCCombatStatics::MakeDamageContext(Avatar, Avatar, Damage, DamageType);
	Context.HitLocation = Hit.ImpactPoint;
	Context.HitNormal = Hit.ImpactNormal;
	Context.SourceItemInstanceId = ItemInstanceId;

	const ECGFDamageResult Result = UVCCombatStatics::ApplyDamageToActor(HitActor, Context);
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s melee → %s: %.1f %s (%s)"), *GetNameSafe(Avatar), *GetNameSafe(HitActor),
		Damage, *DamageType.ToString(), *UEnum::GetValueAsString(Result));

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
