// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN

#include "Animation/VCActionAnimSet.h"
#include "Animation/VCActionMontageComponent.h"
#include "Animation/AnimMontage.h"
#include "Combat/VCCombatComponent.h"
#include "Combat/VCRangedAttackAbility.h"
#include "Core/VCCharacterBase.h"
#include "Core/VCGameplayTags.h"
#include "Components/EquipmentManagerComponent.h"
#include "Components/InventoryComponent.h"
#include "Data/Fragments/ItemFragment_Equipment.h"
#include "Data/Fragments/ItemFragment_RangedWeapon.h"
#include "Data/ItemDefinition.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Tags/CGFGameplayTags.h"
#include "Testing/CGFTestGameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"

// ---------------------------------------------------------------------------
// The bow's reload (the bow reload plan, D98-D101) on a player character: the bow drawn from the back loads itself once the
// unsheathe has played, a draw needs the nock, a shot empties the string and the next arrow comes after the loose, no
// arrow means no reload, sheathing empties the string, a hit breaks a reload off and the quiver picks where the arrow
// comes from. The player is spawned in a game instance's world that has begun play, standalone (the authority); its
// action sets are replaced by one Bow set with a 1 s reload (nock at 0.45 s), and the world is ticked frame by frame.
// ---------------------------------------------------------------------------
namespace VCBowReloadTestHelpers
{
	FGameplayTag AmmoTag() { return FGameplayTag::RequestGameplayTag(TEXT("Item.Ammo.Arrow")); }
	FGameplayTag BowSet() { return FGameplayTag::RequestGameplayTag(TEXT("Equipment.AnimSet.Bow")); }

	struct FReloadWorld
	{
		CGFTesting::FTestGameInstance Game;
		UWorld* World = nullptr;
		UItemDatabaseSubsystem* Database = nullptr;
		TArray<UObject*> Rooted;

		FReloadWorld()
		{
			World = Game.World;
			World->InitializeActorsForPlay(FURL());
			if (AWorldSettings* Settings = World->GetWorldSettings())
			{
				Settings->NotifyBeginPlay();
			}
			Database = Game.GetSubsystem<UItemDatabaseSubsystem>();
		}

		~FReloadWorld()
		{
			TArray<AActor*> Actors;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				Actors.Add(*It);
			}
			for (AActor* Actor : Actors)
			{
				World->DestroyActor(Actor);
			}
			for (UObject* Object : Rooted)
			{
				Object->RemoveFromRoot();
			}
		}

		UItemDefinition* Define(const TCHAR* Name, int32 Stack, const FGameplayTagContainer& Tags)
		{
			UItemDefinition* Definition = NewObject<UItemDefinition>(GetTransientPackage(), Name);
			Definition->AddToRoot();
			Rooted.Add(Definition);
			Definition->MaxStackSize = Stack;
			Definition->ItemTags = Tags;
			Database->RegisterDefinition(Definition->GetPrimaryAssetId(), Definition);
			return Definition;
		}

		/** A bow carried on the back, drawn into the main hand, shooting arrows, animated by the Bow set. */
		UItemDefinition* DefineBow()
		{
			FGameplayTagContainer Tags(CGFGameplayTags::Item_Category_Weapon);
			Tags.AddTag(CGFGameplayTags::Item_Type_Bow);
			UItemDefinition* Bow = Define(TEXT("ReloadTestBow"), 1, Tags);
			UItemFragment_Equipment* Equip = NewObject<UItemFragment_Equipment>(Bow);
			Equip->EquipmentSlotTag = CGFGameplayTags::Equipment_Slot_MainHand;
			Equip->SheathSlotTag = CGFGameplayTags::Equipment_Slot_Sheath_Back;
			Equip->ActionSetTag = BowSet();
			UItemFragment_RangedWeapon* Ranged = NewObject<UItemFragment_RangedWeapon>(Bow);
			Ranged->AmmoTag = AmmoTag();
			Bow->Fragments = { Equip, Ranged };
			return Bow;
		}

		UItemDefinition* DefineQuiver()
		{
			UItemDefinition* Quiver = Define(TEXT("ReloadTestQuiver"), 1, FGameplayTagContainer(CGFGameplayTags::Item_Category_Quiver));
			UItemFragment_Equipment* Equip = NewObject<UItemFragment_Equipment>(Quiver);
			Equip->EquipmentSlotTag = CGFGameplayTags::Equipment_Slot_Quiver;
			Quiver->Fragments = { Equip };
			return Quiver;
		}

		/** The Bow set with a reload of Length seconds (no notifies: the nock at 45%, the grab at 15%). */
		UVCActionAnimSet* MakeBowSet(float Length)
		{
			UVCActionAnimSet* Set = NewObject<UVCActionAnimSet>(GetTransientPackage());
			Set->AddToRoot();
			Rooted.Add(Set);
			Set->ActionSet = BowSet();
			UAnimMontage* Reload = NewObject<UAnimMontage>(Set);
			Reload->SetCompositeLength(Length);
			Set->ReloadFromQuiver = Reload;
			return Set;
		}

		AVCCharacterBase* SpawnPlayer(UVCActionAnimSet* Set)
		{
			const FTransform Stand(FVector(0, 0, 1000));
			AVCCharacterBase* Player = World->SpawnActorDeferred<AVCCharacterBase>(AVCCharacterBase::StaticClass(), Stand, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Player)
			{
				return nullptr;
			}
			Player->bWaitForTerrain = false;
			Player->FinishSpawning(Stand);
			if (UVCActionMontageComponent* Montages = Player->FindComponentByClass<UVCActionMontageComponent>())
			{
				Montages->ActionAnimSets.Reset();
				if (Set)
				{
					Montages->ActionAnimSets.Add(Set);
				}
			}
			return Player;
		}

		/** Engine frames of 0.05 s (the timer manager ticks once a frame). */
		void TickFor(float Seconds)
		{
			for (float Elapsed = 0.f; Elapsed < Seconds - UE_KINDA_SMALL_NUMBER; Elapsed += 0.05f)
			{
				++GFrameCounter;
				World->Tick(LEVELTICK_All, 0.05f);
			}
		}
	};
}

using namespace VCBowReloadTestHelpers;

#define VC_RELOAD_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCBowReload_Cycle, "VoxelCharacter.Combat.Ranged.ReloadCycle", VC_RELOAD_FLAGS)
bool FVCBowReload_Cycle::RunTest(const FString& Parameters)
{
	FReloadWorld W;
	if (!TestNotNull(TEXT("The item database"), W.Database))
	{
		return false;
	}
	UItemDefinition* Bow = W.DefineBow();
	UItemDefinition* Arrow = W.Define(TEXT("ReloadTestArrow"), 50, FGameplayTagContainer(AmmoTag()));
	UItemDefinition* Quiver = W.DefineQuiver();
	AVCCharacterBase* Player = W.SpawnPlayer(W.MakeBowSet(1.f));
	UEquipmentManagerComponent* Equipment = Player ? Player->FindComponentByClass<UEquipmentManagerComponent>() : nullptr;
	UInventoryComponent* Bag = Player ? Player->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!TestNotNull(TEXT("The player, its equipment and its bag"), Bag) || !Equipment)
	{
		return false;
	}
	const FGameplayTag Back = CGFGameplayTags::Equipment_Slot_Sheath_Back;

	// Without a quiver the arrow comes from the bag; with one, from the quiver (D99).
	TestTrue(TEXT("No quiver: the bag"), Player->GetReloadFrom() == EVCReloadFrom::Bag);
	Equipment->TryEquip(W.Database->CreateItemInstance(Quiver->GetPrimaryAssetId(), 1));
	TestTrue(TEXT("A quiver: the quiver"), Player->GetReloadFrom() == EVCReloadFrom::Quiver);

	// The bow drawn with no arrow: nothing loads.
	Equipment->TryEquip(W.Database->CreateItemInstance(Bow->GetPrimaryAssetId(), 1));
	TestEqual(TEXT("The bow comes into the hand"), static_cast<int32>(Equipment->TryDraw(Back, Bag)), static_cast<int32>(EEquipmentResult::Success));
	W.TickFor(1.5f);
	TestFalse(TEXT("No arrow: no reload"), Player->IsReloading() || Player->IsArrowNocked());
	TestFalse(TEXT("No arrow: no draw"), UVCRangedAttackAbility::CanStartDraw(Player));

	// Arrows: the reload starts, the string is empty until its nock, then a draw may start (D98).
	Bag->TryAddItem(W.Database->CreateItemInstance(Arrow->GetPrimaryAssetId(), 2));
	W.TickFor(0.1f);
	TestTrue(TEXT("With arrows the reload starts"), Player->IsReloading());
	TestFalse(TEXT("Not nocked yet"), Player->IsArrowNocked());
	TestFalse(TEXT("No draw before the nock"), UVCRangedAttackAbility::CanStartDraw(Player));
	TestTrue(TEXT("Arrows to load"), UVCRangedAttackAbility::HasAmmoToLoad(Player));
	W.TickFor(0.5f);
	TestTrue(TEXT("Nocked at the nock"), Player->IsArrowNocked());
	TestFalse(TEXT("The reload is over"), Player->IsReloading());
	TestTrue(TEXT("A draw may start"), UVCRangedAttackAbility::CanStartDraw(Player));

	// A shot empties the string; the next reload waits for the loose.
	Player->HandleArrowLoosed();
	TestFalse(TEXT("The shot empties the string"), Player->IsArrowNocked());
	TestFalse(TEXT("No draw on an empty string"), UVCRangedAttackAbility::CanStartDraw(Player));
	W.TickFor(0.2f);
	TestFalse(TEXT("The loose plays first"), Player->IsReloading());
	W.TickFor(0.25f);
	TestTrue(TEXT("Then the next reload"), Player->IsReloading());

	// A hit breaks it off; the flinch plays before the next.
	FCGFDamageContext Hit;
	Player->CombatComponent->OnDamaged.Broadcast(Hit);
	TestFalse(TEXT("A hit breaks the reload off"), Player->IsReloading());
	W.TickFor(0.4f);
	TestFalse(TEXT("The flinch plays first"), Player->IsReloading() || Player->IsArrowNocked());
	W.TickFor(0.3f);
	TestTrue(TEXT("Then the reload again"), Player->IsReloading());
	W.TickFor(0.5f);
	TestTrue(TEXT("Nocked again"), Player->IsArrowNocked());

	// Sheathing empties the string; drawn again, the bow loads again.
	TestEqual(TEXT("The bow sheathes"), static_cast<int32>(Equipment->TrySheathe(CGFGameplayTags::Equipment_Slot_MainHand)),
		static_cast<int32>(EEquipmentResult::Success));
	TestFalse(TEXT("Sheathed: nothing nocked"), Player->IsArrowNocked());
	W.TickFor(1.f);
	TestFalse(TEXT("Sheathed: no reload"), Player->IsReloading() || Player->IsArrowNocked());
	Equipment->TryDraw(Back, Bag);
	W.TickFor(1.2f);
	TestTrue(TEXT("Drawn again: nocked"), Player->IsArrowNocked());

	// The last arrows leave the bag: the nock stays (it is on the string), no reload follows the next shot.
	Bag->TryRemoveItem(Bag->FindItemsByTag(FGameplayTagContainer(AmmoTag()))[0].InstanceId, -1);
	Player->HandleArrowLoosed();
	W.TickFor(1.5f);
	TestFalse(TEXT("No arrows left: no reload"), Player->IsReloading() || Player->IsArrowNocked());
	return true;
}

#endif
