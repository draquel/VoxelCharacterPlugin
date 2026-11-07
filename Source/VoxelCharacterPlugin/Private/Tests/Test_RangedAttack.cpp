// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN

#include "Combat/VCArrowMovementComponent.h"
#include "Combat/VCArrowProjectile.h"
#include "Combat/VCCombatComponent.h"
#include "Combat/VCRangedAttackAbility.h"
#include "Core/VCGameplayTags.h"
#include "Core/VCNPCCharacterBase.h"
#include "Components/EquipmentManagerComponent.h"
#include "Components/InventoryComponent.h"
#include "Data/Fragments/ItemFragment_Durability.h"
#include "Data/Fragments/ItemFragment_Equipment.h"
#include "Data/Fragments/ItemFragment_RangedWeapon.h"
#include "Data/Fragments/ItemFragment_Weapon.h"
#include "Data/ItemDefinition.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Tags/CGFGameplayTags.h"
#include "Testing/CGFTestGameInstance.h"
#include "Types/CGFEquipmentTypes.h"
#include "Types/ItemInstanceFragments.h"
#include "AbilitySystemComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"

// ---------------------------------------------------------------------------
// The bow's attack (ranged combat plan B3): the draw's power and its minimum, no draw without an arrow, one arrow a shot
// (and a bow's wear), a release refused when the server finds no arrow left. The shooter is an NPC (it owns its ability
// system) given a main hand and a bag; a game instance holds the definitions, and its world begins play here.
// ---------------------------------------------------------------------------
namespace VCRangedTestHelpers
{
	/** A game instance whose world has begun play: the item database for the definitions, BeginPlay for the NPC. */
	struct FRangedWorld
	{
		CGFTesting::FTestGameInstance Game;
		UWorld* World = nullptr;
		UItemDatabaseSubsystem* Database = nullptr;
		TArray<UItemDefinition*> Definitions;

		FRangedWorld()
		{
			World = Game.World;
			World->InitializeActorsForPlay(FURL());
			if (AWorldSettings* Settings = World->GetWorldSettings())
			{
				Settings->NotifyBeginPlay();
			}
			Database = Game.GetSubsystem<UItemDatabaseSubsystem>();
		}

		~FRangedWorld()
		{
			// EndPlay while the world still exists (VCTesting::FPlayWorld's reason).
			TArray<AActor*> Actors;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				Actors.Add(*It);
			}
			for (AActor* Actor : Actors)
			{
				World->DestroyActor(Actor);
			}
			for (UItemDefinition* Definition : Definitions)
			{
				Definition->RemoveFromRoot();
			}
		}

		UItemDefinition* Define(const TCHAR* Name, int32 StackSize, const FGameplayTagContainer& Tags)
		{
			UItemDefinition* Definition = NewObject<UItemDefinition>(GetTransientPackage(), Name);
			Definition->AddToRoot();
			Definition->MaxStackSize = StackSize;
			Definition->ItemTags = Tags;
			Database->RegisterDefinition(Definition->GetPrimaryAssetId(), Definition);
			Definitions.Add(Definition);
			return Definition;
		}
	};

	/** The ammo's tag in these tests: any tag will do, so a native one. */
	FGameplayTag AmmoTag()
	{
		return CGFGameplayTags::Item_Category_Misc;
	}

	/** A bow as the demo's (B1): 15 damage at full draw, 6000 uu/s, the draw from 0.25 s (30%) to 1 s, 150 shots of wear. */
	UItemDefinition* DefineBow(FRangedWorld& W)
	{
		UItemDefinition* Bow = W.Define(TEXT("RangedTestBow"), 1, FGameplayTagContainer(CGFGameplayTags::Item_Category_Weapon));
		UItemFragment_Equipment* Equip = NewObject<UItemFragment_Equipment>(Bow);
		Equip->EquipmentSlotTag = CGFGameplayTags::Equipment_Slot_MainHand;
		UItemFragment_Weapon* Weapon = NewObject<UItemFragment_Weapon>(Bow);
		Weapon->BaseDamage = 15.f;
		Weapon->CritChance = 0.f;
		UItemFragment_RangedWeapon* Ranged = NewObject<UItemFragment_RangedWeapon>(Bow);
		Ranged->AmmoTag = AmmoTag();
		UItemFragment_Durability* Durability = NewObject<UItemFragment_Durability>(Bow);
		Durability->MaxDurability = 150.f;
		Durability->DegradeRate = 1.f;
		UInstanceFragment_DurabilityState* State = NewObject<UInstanceFragment_DurabilityState>(Bow);
		State->CurrentDurability = 150.f;
		Bow->Fragments = { Equip, Weapon, Ranged, Durability };
		Bow->DefaultInstanceFragments.Add(State);
		return Bow;
	}

	/** A shooter: an NPC that stands still, with the ranged attack, a main hand and a four-slot bag. */
	struct FShooter
	{
		AVCNPCCharacterBase* NPC = nullptr;
		UEquipmentManagerComponent* Equipment = nullptr;
		UInventoryComponent* Bag = nullptr;
		UAbilitySystemComponent* ASC = nullptr;
	};

	FShooter SpawnShooter(UWorld* World, const FVector& Location)
	{
		FShooter Out;
		const FTransform Transform(Location);
		Out.NPC = World->SpawnActorDeferred<AVCNPCCharacterBase>(AVCNPCCharacterBase::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Out.NPC)
		{
			return Out;
		}
		Out.NPC->bAIEnabled = false;
		Out.NPC->CombatComponent->FactionTag = CGFGameplayTags::Faction_Player;
		Out.NPC->DefaultAbilities.Add(UVCRangedAttackAbility::StaticClass());
		Out.NPC->FinishSpawning(Transform);
		if (UCharacterMovementComponent* Move = Out.NPC->GetCharacterMovement())
		{
			Move->DisableMovement();
		}
		// Registered after BeginPlay, so each begins play at once: the main hand from its definition, the bag's slots.
		Out.Equipment = NewObject<UEquipmentManagerComponent>(Out.NPC, TEXT("RangedTestEquipment"));
		FEquipmentSlotDefinition MainHand;
		MainHand.SlotTag = CGFGameplayTags::Equipment_Slot_MainHand;
		Out.Equipment->AvailableSlots.Add(MainHand);
		Out.Equipment->RegisterComponent();
		Out.Bag = NewObject<UInventoryComponent>(Out.NPC, TEXT("RangedTestBag"));
		Out.Bag->MaxSlots = 4;
		Out.Bag->RegisterComponent();
		Out.ASC = Out.NPC->GetAbilitySystemComponent();
		return Out;
	}

	/** One world tick as its own engine frame (the timer manager ticks once a frame). */
	void TickFor(UWorld* World, float Seconds)
	{
		for (float Elapsed = 0.f; Elapsed < Seconds - UE_KINDA_SMALL_NUMBER; Elapsed += 0.05f)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, 0.05f);
		}
	}

	bool StartDraw(const FShooter& Shooter)
	{
		return Shooter.ASC && Shooter.ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(VCGameplayTags::Ability_Attack_Ranged));
	}

	UVCRangedAttackAbility* Instance(const FShooter& Shooter)
	{
		FGameplayAbilitySpec* Spec = Shooter.ASC ? Shooter.ASC->FindAbilitySpecFromClass(UVCRangedAttackAbility::StaticClass()) : nullptr;
		return Spec ? Cast<UVCRangedAttackAbility>(Spec->GetPrimaryInstance()) : nullptr;
	}

	int32 CountArrows(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AVCArrowProjectile> It(World); It; ++It)
		{
			Count += IsValid(*It) ? 1 : 0;
		}
		return Count;
	}

	float BowDurability(const FShooter& Shooter)
	{
		const FItemInstance Bow = Shooter.Equipment->GetEquippedItem(CGFGameplayTags::Equipment_Slot_MainHand);
		const UInstanceFragment_DurabilityState* State = Bow.FindFragment<UInstanceFragment_DurabilityState>();
		return State ? State->CurrentDurability : -1.f;
	}
}

using namespace VCRangedTestHelpers;

#define VC_RANGED_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCRanged_DrawRules, "VoxelCharacter.Combat.Ranged.DrawRules", VC_RANGED_FLAGS)
bool FVCRanged_DrawRules::RunTest(const FString& Parameters)
{
	FRangedWorld W;
	if (!TestNotNull(TEXT("The item database"), W.Database))
	{
		return false;
	}
	UItemDefinition* Bow = DefineBow(W);
	UItemDefinition* Arrow = W.Define(TEXT("RangedTestArrow"), 50, FGameplayTagContainer(AmmoTag()));
	FShooter S = SpawnShooter(W.World, FVector(0, 0, 1000));
	if (!S.NPC || !S.ASC) { AddError(TEXT("Spawn failed")); return false; }

	TestFalse(TEXT("Empty hands: no ranged weapon"), UVCRangedAttackAbility::HoldsRangedWeapon(S.NPC));
	TestFalse(TEXT("Empty hands: no draw"), UVCRangedAttackAbility::CanStartDraw(S.NPC));
	TestEqual(TEXT("Empty hands: no power"), UVCRangedAttackAbility::GetDrawPower(S.NPC, 1.f), 0.f);

	TestTrue(TEXT("The bow equips"), S.Equipment->TryEquipToSlot(W.Database->CreateItemInstance(Bow->GetPrimaryAssetId()),
		CGFGameplayTags::Equipment_Slot_MainHand) == EEquipmentResult::Success);
	TestTrue(TEXT("A bow in hand"), UVCRangedAttackAbility::HoldsRangedWeapon(S.NPC));
	TestFalse(TEXT("No arrow: no draw"), UVCRangedAttackAbility::CanStartDraw(S.NPC));
	TestFalse(TEXT("No arrow: the server does not start one"), StartDraw(S));

	TestTrue(TEXT("Arrows in the bag"), S.Bag->TryAddItem(W.Database->CreateItemInstance(Arrow->GetPrimaryAssetId(), 3)) == EInventoryOperationResult::Success);
	TestTrue(TEXT("An arrow: a draw may start"), UVCRangedAttackAbility::CanStartDraw(S.NPC));
	TestEqual(TEXT("Shorter than the shortest draw: nothing"), UVCRangedAttackAbility::GetDrawPower(S.NPC, 0.1f), 0.f);
	TestEqual(TEXT("The shortest draw: its power"), UVCRangedAttackAbility::GetDrawPower(S.NPC, 0.25f), 0.3f);
	TestEqual(TEXT("Halfway: halfway between"), UVCRangedAttackAbility::GetDrawPower(S.NPC, 0.625f), 0.65f, 1e-4f);
	TestEqual(TEXT("A full draw: full power"), UVCRangedAttackAbility::GetDrawPower(S.NPC, 1.f), 1.f);
	TestEqual(TEXT("Held longer: still full"), UVCRangedAttackAbility::GetDrawPower(S.NPC, 3.f), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCRanged_OneArrowPerShot, "VoxelCharacter.Combat.Ranged.OneArrowPerShot", VC_RANGED_FLAGS)
bool FVCRanged_OneArrowPerShot::RunTest(const FString& Parameters)
{
	FRangedWorld W;
	if (!TestNotNull(TEXT("The item database"), W.Database))
	{
		return false;
	}
	UItemDefinition* Bow = DefineBow(W);
	UItemDefinition* Arrow = W.Define(TEXT("RangedTestArrow"), 50, FGameplayTagContainer(AmmoTag()));
	FShooter S = SpawnShooter(W.World, FVector(0, 0, 1000));
	if (!S.NPC || !S.ASC) { AddError(TEXT("Spawn failed")); return false; }
	S.Equipment->TryEquipToSlot(W.Database->CreateItemInstance(Bow->GetPrimaryAssetId()), CGFGameplayTags::Equipment_Slot_MainHand);
	S.Bag->TryAddItem(W.Database->CreateItemInstance(Arrow->GetPrimaryAssetId(), 3));
	const FPrimaryAssetId ArrowId = Arrow->GetPrimaryAssetId();

	// A full draw: one arrow flies at the bow's full speed, one leaves the bag, the bow wears by one.
	TestTrue(TEXT("The draw starts"), StartDraw(S));
	UVCRangedAttackAbility* Ability = Instance(S);
	TestTrue(TEXT("It is drawing"), Ability && Ability->IsDrawing());
	TickFor(W.World, 1.f);
	TestEqual(TEXT("Nothing flies while drawn"), CountArrows(W.World), 0);
	TestTrue(TEXT("The release reaches the ability"), UVCRangedAttackAbility::ReleaseDraw(S.ASC));
	TestEqual(TEXT("One arrow flies"), CountArrows(W.World), 1);
	TestEqual(TEXT("One arrow left the bag"), S.Bag->GetItemCount(ArrowId), 2);
	TestEqual(TEXT("The bow wore by its rate"), BowDurability(S), 149.f);
	AVCArrowProjectile* Shot = Ability ? Ability->GetLastArrow() : nullptr;
	if (TestNotNull(TEXT("The shot's arrow"), Shot))
	{
		TestTrue(TEXT("Its shooter"), Shot->GetInstigator() == S.NPC);
		TestEqual(TEXT("The full draw's power"), Shot->GetShot().DrawPower, 1.f);
		TestEqual(TEXT("The bow's damage"), Shot->GetShot().GetDamage(), 15.f);
		TestTrue(*FString::Printf(TEXT("The bow's full speed (%.0f uu/s)"), Shot->GetArrowMovement()->Velocity.Size()),
			FMath::IsNearlyEqual(Shot->GetArrowMovement()->Velocity.Size(), 6000.0, 1.0));
	}
	TestFalse(TEXT("The ability ended"), Ability && Ability->IsActive());

	// Let go too soon: no arrow, none used, no wear.
	TestTrue(TEXT("A second draw starts"), StartDraw(S));
	TickFor(W.World, 0.1f);
	UVCRangedAttackAbility::ReleaseDraw(S.ASC);
	TestEqual(TEXT("A short draw shoots nothing"), CountArrows(W.World), 1);
	TestEqual(TEXT("and uses no arrow"), S.Bag->GetItemCount(ArrowId), 2);
	TestEqual(TEXT("and wears nothing"), BowDurability(S), 149.f);

	// A half draw: the arrow flies at the draw's power.
	TestTrue(TEXT("A third draw starts"), StartDraw(S));
	TickFor(W.World, 0.65f);
	UVCRangedAttackAbility::ReleaseDraw(S.ASC);
	TestEqual(TEXT("A second arrow flies"), CountArrows(W.World), 2);
	TestEqual(TEXT("One arrow left"), S.Bag->GetItemCount(ArrowId), 1);
	AVCArrowProjectile* Half = Ability ? Ability->GetLastArrow() : nullptr;
	if (TestNotNull(TEXT("The half draw's arrow"), Half))
	{
		const float Power = Half->GetShot().DrawPower;
		TestTrue(*FString::Printf(TEXT("A partial power (%.3f)"), Power), Power > 0.3f && Power < 1.f);
		TestTrue(TEXT("and the speed with it"), FMath::IsNearlyEqual(Half->GetArrowMovement()->Velocity.Size(), 6000.0 * Power, 1.0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCRanged_ReleaseWithoutArrowRefused, "VoxelCharacter.Combat.Ranged.ReleaseWithoutArrowRefused", VC_RANGED_FLAGS)
bool FVCRanged_ReleaseWithoutArrowRefused::RunTest(const FString& Parameters)
{
	FRangedWorld W;
	if (!TestNotNull(TEXT("The item database"), W.Database))
	{
		return false;
	}
	UItemDefinition* Bow = DefineBow(W);
	UItemDefinition* Arrow = W.Define(TEXT("RangedTestArrow"), 50, FGameplayTagContainer(AmmoTag()));
	FShooter S = SpawnShooter(W.World, FVector(0, 0, 1000));
	if (!S.NPC || !S.ASC) { AddError(TEXT("Spawn failed")); return false; }
	S.Equipment->TryEquipToSlot(W.Database->CreateItemInstance(Bow->GetPrimaryAssetId()), CGFGameplayTags::Equipment_Slot_MainHand);
	const FItemInstance Last = W.Database->CreateItemInstance(Arrow->GetPrimaryAssetId(), 1);
	S.Bag->TryAddItem(Last);

	// The last arrow leaves the bag while the bow is drawn (dropped, traded): the server refuses the release.
	TestTrue(TEXT("The draw starts with one arrow"), StartDraw(S));
	TickFor(W.World, 1.f);
	TestTrue(TEXT("The arrow leaves the bag"), S.Bag->TryRemoveItem(Last.InstanceId, -1) == EInventoryOperationResult::Success);
	UVCRangedAttackAbility::ReleaseDraw(S.ASC);
	TestEqual(TEXT("No arrow flies"), CountArrows(W.World), 0);
	TestEqual(TEXT("The bow did not wear"), BowDurability(S), 150.f);
	const UVCRangedAttackAbility* Ability = Instance(S);
	TestFalse(TEXT("The ability ended"), Ability && Ability->IsActive());
	TestFalse(TEXT("And no draw starts now"), StartDraw(S));
	return true;
}

#endif // WITH_AUTOMATION_TESTS && WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
