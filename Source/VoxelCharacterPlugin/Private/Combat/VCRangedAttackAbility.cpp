// Copyright Daniel Raquel. All Rights Reserved.

#include "Combat/VCRangedAttackAbility.h"
#include "Combat/VCArrowProjectile.h"
#include "Animation/VCActionMontageComponent.h"
#include "Core/VCGameplayTags.h"
#include "Tags/CGFGameplayTags.h"
#include "VoxelCharacterPlugin.h"
#include "AbilitySystemComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
#include "Components/EquipmentManagerComponent.h"
#include "Components/InventoryComponent.h"
#include "Interfaces/CGFInventoryInterface.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Data/ItemDefinition.h"
#include "Data/Fragments/ItemFragment_Durability.h"
#include "Data/Fragments/ItemFragment_RangedWeapon.h"
#include "Data/Fragments/ItemFragment_Weapon.h"
#include "Data/Fragments/ItemFragment_WorldDisplay.h"
#include "Types/ItemInstanceFragments.h"
#include "Engine/GameInstance.h"
#endif

#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
namespace
{
	/** What the main hand holds, when it is a ranged weapon: the item, its definition and the fragments a shot reads. */
	struct FVCMainHandRangedWeapon
	{
		UEquipmentManagerComponent* Equipment = nullptr;
		FItemInstance Item;
		const UItemDefinition* Definition = nullptr;
		const UItemFragment_RangedWeapon* Ranged = nullptr;
		/** The damage, its type and the crits; null fires for no damage. */
		const UItemFragment_Weapon* Weapon = nullptr;
	};

	const UItemDatabaseSubsystem* FindItemDatabase(const AActor* Actor)
	{
		const UWorld* World = Actor ? Actor->GetWorld() : nullptr;
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		return GameInstance ? GameInstance->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	}

	bool FindMainHandRangedWeapon(const AActor* Avatar, FVCMainHandRangedWeapon& Out)
	{
		Out.Equipment = Avatar ? Avatar->FindComponentByClass<UEquipmentManagerComponent>() : nullptr;
		if (!Out.Equipment)
		{
			return false;
		}
		Out.Item = Out.Equipment->GetEquippedItem(CGFGameplayTags::Equipment_Slot_MainHand);
		const UItemDatabaseSubsystem* ItemDB = Out.Item.IsValid() ? FindItemDatabase(Avatar) : nullptr;
		Out.Definition = ItemDB ? ItemDB->GetDefinition(Out.Item.ItemDefinitionId) : nullptr;
		Out.Ranged = Out.Definition ? Out.Definition->FindFragment<UItemFragment_RangedWeapon>() : nullptr;
		Out.Weapon = Out.Definition ? Out.Definition->FindFragment<UItemFragment_Weapon>() : nullptr;
		return Out.Ranged != nullptr;
	}

	/** The avatar's inventory: through ICGFInventoryInterface, else its first inventory component. */
	UInventoryComponent* FindInventory(const AActor* Avatar)
	{
		if (Avatar && Avatar->Implements<UCGFInventoryInterface>())
		{
			if (UInventoryComponent* Inventory = Cast<UInventoryComponent>(ICGFInventoryInterface::Execute_GetInventoryComponent(Avatar)))
			{
				return Inventory;
			}
		}
		return Avatar ? Avatar->FindComponentByClass<UInventoryComponent>() : nullptr;
	}

	/** The first stack in the avatar's inventory carrying the ammo tag. */
	bool FindAmmo(const AActor* Avatar, const FGameplayTag& AmmoTag, UInventoryComponent*& OutInventory, FItemInstance& OutAmmo)
	{
		OutInventory = FindInventory(Avatar);
		if (!OutInventory)
		{
			return false;
		}
		for (const FItemInstance& Item : OutInventory->FindItemsByTag(FGameplayTagContainer(AmmoTag)))
		{
			if (Item.IsValid() && Item.StackCount > 0)
			{
				OutAmmo = Item;
				return true;
			}
		}
		return false;
	}
}
#endif

UVCRangedAttackAbility::UVCRangedAttackAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;

	FGameplayTagContainer AssetTags;
	AssetTags.AddTag(VCGameplayTags::Ability_Attack_Ranged);
	SetAssetTags(AssetTags);

	ActivationBlockedTags.AddTag(CGFGameplayTags::State_Dead);
	ActivationBlockedTags.AddTag(CGFGameplayTags::State_Downed);
}

bool UVCRangedAttackAbility::HoldsRangedWeapon(const AActor* Avatar)
{
#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
	FVCMainHandRangedWeapon Weapon;
	return FindMainHandRangedWeapon(Avatar, Weapon);
#else
	return false;
#endif
}

bool UVCRangedAttackAbility::CanStartDraw(const AActor* Avatar)
{
#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
	FVCMainHandRangedWeapon Weapon;
	if (!FindMainHandRangedWeapon(Avatar, Weapon))
	{
		return false;
	}
	if (!Weapon.Ranged->AmmoTag.IsValid())
	{
		return true; // it needs no ammo
	}
	UInventoryComponent* Inventory = nullptr;
	FItemInstance Ammo;
	return FindAmmo(Avatar, Weapon.Ranged->AmmoTag, Inventory, Ammo);
#else
	return false;
#endif
}

float UVCRangedAttackAbility::GetDrawPower(const AActor* Avatar, float DrawSeconds)
{
#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
	FVCMainHandRangedWeapon Weapon;
	return FindMainHandRangedWeapon(Avatar, Weapon) ? Weapon.Ranged->GetDrawPower(DrawSeconds) : 0.f;
#else
	return 0.f;
#endif
}

bool UVCRangedAttackAbility::ReleaseDraw(UAbilitySystemComponent* ASC)
{
	if (!ASC)
	{
		return false;
	}
	for (FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->IsA<UVCRangedAttackAbility>())
		{
			if (ASC->IsOwnerActorAuthoritative())
			{
				ASC->AbilitySpecInputReleased(Spec);
			}
			else
			{
				// The input release rides the component's channel, after the activation request it follows.
				ASC->ServerSetInputReleased(Spec.Handle);
			}
			return true;
		}
	}
	return false;
}

bool UVCRangedAttackAbility::ComputeShot(const AActor* Avatar, FVector& OutStart, FVector& OutDirection) const
{
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!World)
	{
		return false;
	}
	FVector Eyes;
	FRotator View;
	Avatar->GetActorEyesViewPoint(Eyes, View);
	const FVector ViewDirection = View.Vector();
	OutStart = Eyes + View.RotateVector(ArrowStartOffset);

	// What the crosshair is on: terrain and props block the Pawn channel, and so do the capsules a visibility trace passes.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VCRangedAim), /*bTraceComplex*/ false, Avatar);
	FHitResult Hit;
	const FVector Far = Eyes + ViewDirection * AimRange;
	const FVector AimPoint = World->SweepSingleByChannel(Hit, Eyes, Far, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(4.f), Params)
		? Hit.ImpactPoint : Far;

	// An aim point at the bow or behind it (a wall at the face) gives no direction: fly along the view.
	const FVector ToAim = AimPoint - OutStart;
	OutDirection = (ToAim.Size() > 2.0 * ArrowStartOffset.Size() && FVector::DotProduct(ToAim.GetSafeNormal(), ViewDirection) > 0.5)
		? ToAim.GetSafeNormal() : ViewDirection;
	return true;
}

bool UVCRangedAttackAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	return CanStartDraw(ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr);
}

void UVCRangedAttackAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
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

	// The draw is timed here, on the server, from now to the input release (InputReleased).
	DrawStartTime = World->GetTimeSeconds();
	bDrawing = true;
	if (UVCActionMontageComponent* Montages = Avatar->FindComponentByClass<UVCActionMontageComponent>())
	{
		const APawn* Pawn = Cast<APawn>(Avatar);
		Montages->PlayActionForEveryone(EVCAnimAction::Draw, 0, 1.f, Pawn && Pawn->IsPlayerControlled());
	}
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s draws"), *GetNameSafe(Avatar));
}

void UVCRangedAttackAbility::InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo)
{
	AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	UWorld* World = Avatar ? Avatar->GetWorld() : nullptr;
	if (!bDrawing || !World)
	{
		return;
	}
	bDrawing = false;
	const float DrawSeconds = static_cast<float>(World->GetTimeSeconds() - DrawStartTime);
	const bool bShot = Shoot(Avatar, DrawSeconds);

	// The owning player played its own release or stopped its own draw at input; everyone else follows the server.
	if (UVCActionMontageComponent* Montages = Avatar->FindComponentByClass<UVCActionMontageComponent>())
	{
		const APawn* Pawn = Cast<APawn>(Avatar);
		const bool bOwnerDidIt = Pawn && Pawn->IsPlayerControlled();
		if (bShot)
		{
			Montages->PlayActionForEveryone(EVCAnimAction::Release, 0, 1.f, bOwnerDidIt);
		}
		else
		{
			Montages->StopActionForEveryone(EVCAnimAction::Draw, bOwnerDidIt);
		}
	}
	EndAbility(Handle, ActorInfo, ActivationInfo, true, /*bWasCancelled*/ !bShot);
}

void UVCRangedAttackAbility::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// Cancelled mid-draw (killed, knocked out): the draw stops everywhere, the owner's too.
	if (bDrawing)
	{
		bDrawing = false;
		AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
		if (UVCActionMontageComponent* Montages = Avatar ? Avatar->FindComponentByClass<UVCActionMontageComponent>() : nullptr)
		{
			Montages->StopActionForEveryone(EVCAnimAction::Draw, /*bOwnerStoppedIt*/ false);
		}
	}
	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

bool UVCRangedAttackAbility::Shoot(AActor* Avatar, float DrawSeconds)
{
#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponentFromActorInfo();
	if (ASC && (ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Dead) || ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Downed)))
	{
		return false; // went down holding the draw
	}
	FVCMainHandRangedWeapon Weapon;
	if (!FindMainHandRangedWeapon(Avatar, Weapon))
	{
		UE_LOG(LogVoxelCharacter, Log, TEXT("%s's release: no ranged weapon in the main hand any more, no shot"), *GetNameSafe(Avatar));
		return false;
	}
	const UItemFragment_RangedWeapon* Ranged = Weapon.Ranged;
	const float Power = Ranged->GetDrawPower(DrawSeconds);
	if (Power <= 0.f)
	{
		UE_LOG(LogVoxelCharacter, Log, TEXT("%s's release after %.2f s: shorter than the %.2f s draw, no shot"), *GetNameSafe(Avatar),
			DrawSeconds, Ranged->MinDrawSeconds);
		return false;
	}

	UInventoryComponent* Inventory = nullptr;
	FItemInstance Ammo;
	if (Ranged->AmmoTag.IsValid() && !FindAmmo(Avatar, Ranged->AmmoTag, Inventory, Ammo))
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("%s's release: no %s left on the server, refused"), *GetNameSafe(Avatar), *Ranged->AmmoTag.ToString());
		return false;
	}

	APawn* Shooter = Cast<APawn>(Avatar);
	FVector Start, Direction;
	if (!Shooter || !ComputeShot(Avatar, Start, Direction))
	{
		return false;
	}

	// The shot's damage: the Weapon fragment's at full draw (halved when worn out, as a melee swing's), scaled by the draw.
	FVCArrowShot Shot;
	Shot.DrawPower = Power;
	Shot.SourceItemInstanceId = Weapon.Item.InstanceId;
	if (const UItemFragment_Weapon* Damage = Weapon.Weapon)
	{
		Shot.FullDamage = Damage->BaseDamage;
		Shot.DamageType = Damage->DamageType;
		Shot.CritChance = FMath::Clamp(Damage->CritChance, 0.f, 1.f);
		Shot.CritMultiplier = FMath::Max(1.f, Damage->CritMultiplier);
	}
	const UItemFragment_Durability* Durability = Weapon.Definition->FindFragment<UItemFragment_Durability>();
	if (Durability)
	{
		const UInstanceFragment_DurabilityState* State = Weapon.Item.FindFragment<UInstanceFragment_DurabilityState>();
		if (State && State->CurrentDurability <= 0.f)
		{
			Shot.FullDamage *= WornOutDamageMultiplier;
		}
	}

	// The ammo's look and the arrow's class (the native arrow unless the weapon names a subclass).
	UStaticMesh* Mesh = nullptr;
	const UItemDatabaseSubsystem* ItemDB = FindItemDatabase(Avatar);
	const UItemDefinition* AmmoDefinition = Ammo.IsValid() && ItemDB ? ItemDB->GetDefinition(Ammo.ItemDefinitionId) : nullptr;
	if (const UItemFragment_WorldDisplay* Display = AmmoDefinition ? AmmoDefinition->FindFragment<UItemFragment_WorldDisplay>() : nullptr)
	{
		Mesh = Display->WorldMesh.LoadSynchronous();
	}
	TSubclassOf<AVCArrowProjectile> ArrowClass;
	if (Ranged->ProjectileClass)
	{
		if (Ranged->ProjectileClass->IsChildOf(AVCArrowProjectile::StaticClass()))
		{
			ArrowClass = *Ranged->ProjectileClass;
		}
		else
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("%s: projectile class %s is not an AVCArrowProjectile; the native arrow flies"),
				*GetNameSafe(Weapon.Definition), *GetNameSafe(*Ranged->ProjectileClass));
		}
	}

	// One arrow a shot, used before it flies: an inventory that refuses leaves no shot.
	if (Ammo.IsValid() && Inventory->TryRemoveItem(Ammo.InstanceId, 1) != EInventoryOperationResult::Success)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("%s's release: the inventory kept its arrow, refused"), *GetNameSafe(Avatar));
		return false;
	}
	AVCArrowProjectile* Arrow = AVCArrowProjectile::SpawnArrow(Shooter, ArrowClass, Start, Direction * Ranged->LaunchSpeed * Power, Shot, Mesh);
	LastArrow = Arrow;

	// Every shot wears the bow by its DegradeRate.
	if (Durability && Durability->DegradeRate > 0.f && Weapon.Equipment)
	{
		Weapon.Equipment->ApplyDurabilityLoss(CGFGameplayTags::Equipment_Slot_MainHand, Durability->DegradeRate);
	}
	UE_LOG(LogVoxelCharacter, Log, TEXT("%s shoots after %.2f s: power %.2f, %.0f uu/s, %s"), *GetNameSafe(Avatar), DrawSeconds, Power,
		Ranged->LaunchSpeed * Power, *GetNameSafe(Arrow));
	return Arrow != nullptr;
#else
	return false;
#endif
}
