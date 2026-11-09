// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN

#include "Animation/VCActionAnimSet.h"
#include "Animation/VCActionMontageComponent.h"
#include "Animation/AnimMontage.h"
#include "Core/VCCharacterBase.h"
#include "Core/VCGameplayTags.h"
#include "Core/VCTypes.h"
#include "Components/EquipmentManagerComponent.h"
#include "Data/Fragments/ItemFragment_Equipment.h"
#include "Data/ItemDefinition.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Tags/CGFGameplayTags.h"
#include "Testing/CGFTestGameInstance.h"
#include "UI/VCCarriedWeaponsWidget.h"
#include "Types/EquipmentSystemTypes.h"
#include "EngineUtils.h"
#include "JsonObjectConverter.h"
#include "GameFramework/WorldSettings.h"

// ---------------------------------------------------------------------------
// The character's sheaths (the equipment sheathing plan, S2): its three sheaths on the body drawn into the main hand,
// the action set following the drawn weapon (Unarmed with all sheathed), R and X (toggle and cycle), the save keeping the
// drawn state, the hand-off time read from a montage. The player is spawned in a game instance's world that has begun
// play, standalone (the authority: a draw is the server's sequence); its montage sets are emptied so a draw lands at once
// (no montage, no hand-off to wait for).
// ---------------------------------------------------------------------------
namespace VCSheathTestHelpers
{
	struct FSheathWorld
	{
		CGFTesting::FTestGameInstance Game;
		UWorld* World = nullptr;
		UItemDatabaseSubsystem* Database = nullptr;
		TArray<UItemDefinition*> Definitions;

		FSheathWorld()
		{
			World = Game.World;
			World->InitializeActorsForPlay(FURL());
			if (AWorldSettings* Settings = World->GetWorldSettings())
			{
				Settings->NotifyBeginPlay();
			}
			Database = Game.GetSubsystem<UItemDatabaseSubsystem>();
		}

		~FSheathWorld()
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

		/** A main-hand weapon carried in Sheath, animating with ActionSet. */
		UItemDefinition* DefineWeapon(const TCHAR* Name, FGameplayTag Sheath, FGameplayTag ActionSet)
		{
			UItemDefinition* Definition = NewObject<UItemDefinition>(GetTransientPackage(), Name);
			Definition->AddToRoot();
			Definition->ItemTags.AddTag(CGFGameplayTags::Item_Category_Weapon);
			UItemFragment_Equipment* Equip = NewObject<UItemFragment_Equipment>(Definition);
			Equip->EquipmentSlotTag = CGFGameplayTags::Equipment_Slot_MainHand;
			Equip->SheathSlotTag = Sheath;
			Equip->ActionSetTag = ActionSet;
			Definition->Fragments.Add(Equip);
			Database->RegisterDefinition(Definition->GetPrimaryAssetId(), Definition);
			Definitions.Add(Definition);
			return Definition;
		}

		AVCCharacterBase* SpawnPlayer()
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
			}
			return Player;
		}
	};

	/** A transient montage of a length, with hand-off notifies at the given times. */
	UAnimMontage* MakeMontage(float Length, const TArray<float>& HandOffTimes = TArray<float>())
	{
		UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage());
		Montage->SetCompositeLength(Length);
		for (const float Time : HandOffTimes)
		{
			FAnimNotifyEvent Event;
			Event.Notify = NewObject<UVCAnimNotify_ItemHandOff>(Montage);
			Event.Link(Montage, Time);
			Event.TriggerTimeOffset = 0.f;
			Montage->Notifies.Add(Event);
		}
		return Montage;
	}
}

#define VC_SHEATH_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSheath_Slots, "VoxelCharacter.EquipmentLoop.SheathSlots", VC_SHEATH_FLAGS)
bool FVCSheath_Slots::RunTest(const FString& Parameters)
{
	using namespace VCSheathTestHelpers;
	FSheathWorld W;
	AVCCharacterBase* Player = W.SpawnPlayer();
	UEquipmentManagerComponent* Equipment = Player ? Player->FindComponentByClass<UEquipmentManagerComponent>() : nullptr;
	if (!TestNotNull(TEXT("The player and its equipment"), Equipment))
	{
		return false;
	}
	// The three sheaths, drawn into the main hand, on their bones, in the cycle's order (D78).
	const FGameplayTag Expected[] = { CGFGameplayTags::Equipment_Slot_Sheath_Belt, CGFGameplayTags::Equipment_Slot_Sheath_HipLeft,
		CGFGameplayTags::Equipment_Slot_Sheath_Back };
	const FName Bones[] = { TEXT("pelvis"), TEXT("pelvis"), TEXT("spine_05") };
	TArray<FGameplayTag> Sheaths;
	for (const FEquipmentSlot& Slot : Equipment->EquipmentSlots)
	{
		if (Slot.DrawSlotTag.IsValid())
		{
			TestTrue(*FString::Printf(TEXT("%s draws into the main hand"), *Slot.SlotTag.ToString()), Slot.DrawSlotTag == CGFGameplayTags::Equipment_Slot_MainHand.GetTag());
			Sheaths.Add(Slot.SlotTag);
		}
	}
	if (TestEqual(TEXT("Three sheaths"), Sheaths.Num(), 3))
	{
		for (int32 i = 0; i < 3; ++i)
		{
			TestTrue(*FString::Printf(TEXT("Sheath %d is %s"), i, *Expected[i].ToString()), Sheaths[i] == Expected[i]);
			TestTrue(*FString::Printf(TEXT("%s hangs on %s"), *Expected[i].ToString(), *Bones[i].ToString()),
				Equipment->GetVisualAttachSocket(Expected[i]) == Bones[i]);
		}
	}
	TestTrue(TEXT("The hand slot is still there"), Equipment->EquipmentSlots.ContainsByPredicate([](const FEquipmentSlot& S)
	{
		return S.SlotTag == CGFGameplayTags::Equipment_Slot_MainHand.GetTag() && !S.DrawSlotTag.IsValid();
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSheath_ActionSetFollowsDrawn, "VoxelCharacter.EquipmentLoop.SheathActionSet", VC_SHEATH_FLAGS)
bool FVCSheath_ActionSetFollowsDrawn::RunTest(const FString& Parameters)
{
	using namespace VCSheathTestHelpers;
	FSheathWorld W;
	const FGameplayTag HipLeft = CGFGameplayTags::Equipment_Slot_Sheath_HipLeft;
	const FGameplayTag Back = CGFGameplayTags::Equipment_Slot_Sheath_Back;
	const FGameplayTag Belt = CGFGameplayTags::Equipment_Slot_Sheath_Belt;
	UItemDefinition* Sword = W.DefineWeapon(TEXT("SheathLoopSword"), HipLeft, VCGameplayTags::Equipment_AnimSet_SwordShield);
	UItemDefinition* Bow = W.DefineWeapon(TEXT("SheathLoopBow"), Back, VCGameplayTags::Equipment_AnimSet_Bow);
	AVCCharacterBase* Player = W.SpawnPlayer();
	UEquipmentManagerComponent* Equipment = Player ? Player->FindComponentByClass<UEquipmentManagerComponent>() : nullptr;
	if (!TestNotNull(TEXT("The player and its equipment"), Equipment))
	{
		return false;
	}
	const FGameplayTag Unarmed = VCGameplayTags::Equipment_AnimSet_Unarmed;

	Equipment->TryEquip(W.Database->CreateItemInstance(Sword->GetPrimaryAssetId()));
	Equipment->TryEquip(W.Database->CreateItemInstance(Bow->GetPrimaryAssetId()));
	TestTrue(TEXT("Carried, both sheathed: Unarmed"), Player->GetAnimActionSet() == Unarmed);
	TestEqual(TEXT("Two carried weapons"), Player->GetCarriedWeaponSlots().Num(), 2);
	TestFalse(TEXT("An empty sheath cannot be drawn"), Player->RequestDrawWeapon(Belt));
	TestFalse(TEXT("Nothing drawn: nothing to sheathe"), Player->RequestSheatheWeapon());

	// R with nothing drawn and nothing drawn before: the first carried (the hip, before the back).
	TestTrue(TEXT("R draws"), Player->RequestToggleSheathe());
	TestTrue(TEXT("The sword is drawn"), Player->GetDrawnSheath() == HipLeft);
	TestTrue(TEXT("The set is the sword's"), Player->GetAnimActionSet() == VCGameplayTags::Equipment_AnimSet_SwordShield.GetTag());

	// X: the next carried, the sword back to the hip.
	TestTrue(TEXT("X cycles"), Player->RequestCycleWeapon());
	TestTrue(TEXT("The bow is drawn"), Player->GetDrawnSheath() == Back);
	TestFalse(TEXT("The sword is sheathed"), Equipment->IsDrawn(HipLeft));
	TestTrue(TEXT("The set is the bow's"), Player->GetAnimActionSet() == VCGameplayTags::Equipment_AnimSet_Bow.GetTag());

	// R sheathes; R again draws the last one drawn.
	TestTrue(TEXT("R sheathes"), Player->RequestToggleSheathe());
	TestFalse(TEXT("Nothing drawn"), Player->GetDrawnSheath().IsValid());
	TestTrue(TEXT("All sheathed: Unarmed"), Player->GetAnimActionSet() == Unarmed);
	TestTrue(TEXT("R draws again"), Player->RequestToggleSheathe());
	TestTrue(TEXT("The last drawn, the bow"), Player->GetDrawnSheath() == Back);

	// The save keeps which one is in the hand.
	FVCItemSnapshot Snapshot;
	Player->BuildItemSnapshot(Snapshot);
	int32 Found = 0;
	for (const FVCSnapshotEquipmentEntry& Entry : Snapshot.Equipment)
	{
		if (Entry.SlotTag == Back)
		{
			TestTrue(TEXT("The snapshot's bow is drawn"), Entry.bDrawn);
			++Found;
		}
		else if (Entry.SlotTag == HipLeft)
		{
			TestFalse(TEXT("The snapshot's sword is sheathed"), Entry.bDrawn);
			++Found;
		}
	}
	TestEqual(TEXT("Both weapons are in the snapshot"), Found, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSheath_CarriedStrip, "VoxelCharacter.EquipmentLoop.SheathCarriedStrip", VC_SHEATH_FLAGS)
bool FVCSheath_CarriedStrip::RunTest(const FString& Parameters)
{
	// The HUD's strip shows each carried weapon in the sheaths' order and lights the drawn one, following the equipment.
	using namespace VCSheathTestHelpers;
	FSheathWorld W;
	const FGameplayTag HipLeft = CGFGameplayTags::Equipment_Slot_Sheath_HipLeft;
	const FGameplayTag Back = CGFGameplayTags::Equipment_Slot_Sheath_Back;
	UItemDefinition* Sword = W.DefineWeapon(TEXT("SheathStripSword"), HipLeft, VCGameplayTags::Equipment_AnimSet_SwordShield);
	UItemDefinition* Bow = W.DefineWeapon(TEXT("SheathStripBow"), Back, VCGameplayTags::Equipment_AnimSet_Bow);
	AVCCharacterBase* Player = W.SpawnPlayer();
	UEquipmentManagerComponent* Equipment = Player ? Player->FindComponentByClass<UEquipmentManagerComponent>() : nullptr;
	UVCCarriedWeaponsWidget* Strip = CreateWidget<UVCCarriedWeaponsWidget>(W.World, UVCCarriedWeaponsWidget::StaticClass());
	if (!TestNotNull(TEXT("The player and its equipment"), Equipment) || !TestNotNull(TEXT("The strip"), Strip))
	{
		return false;
	}
	Strip->InitWithCharacter(Player);
	TestEqual(TEXT("Nothing carried, nothing shown"), Strip->GetShownSheaths().Num(), 0);
	Equipment->TryEquip(W.Database->CreateItemInstance(Bow->GetPrimaryAssetId()));
	Equipment->TryEquip(W.Database->CreateItemInstance(Sword->GetPrimaryAssetId()));
	const TArray<FGameplayTag> Shown = Strip->GetShownSheaths();
	if (TestEqual(TEXT("Two shown"), Shown.Num(), 2))
	{
		TestTrue(TEXT("In the sheaths' order: the hip, then the back"), Shown[0] == HipLeft && Shown[1] == Back);
	}
	TestFalse(TEXT("None lit while all are sheathed"), Strip->GetLitSheath().IsValid());
	Equipment->TryDraw(Back);
	TestTrue(TEXT("The drawn bow is lit"), Strip->GetLitSheath() == Back);
	Equipment->TrySheathe(Back);
	TestFalse(TEXT("Sheathed again: none lit"), Strip->GetLitSheath().IsValid());
	Strip->InitWithCharacter(nullptr);
	TestEqual(TEXT("Unbound: nothing shown"), Strip->GetShownSheaths().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSheath_SaveEntry, "VoxelCharacter.EquipmentLoop.SheathSaveEntry", VC_SHEATH_FLAGS)
bool FVCSheath_SaveEntry::RunTest(const FString& Parameters)
{
	// The drawn state survives the save's JSON; an entry written before sheaths reads as sheathed (the manager then puts
	// an old main-hand weapon into its sheath, drawn: Equipment.Sheath.TargetSlot).
	FVCSnapshotEquipmentEntry Entry;
	Entry.SlotTag = CGFGameplayTags::Equipment_Slot_Sheath_Back;
	Entry.ItemJson = TEXT("{\"def\":\"ID_Weapon_Bow\"}");
	Entry.bDrawn = true;
	FString Json;
	TestTrue(TEXT("Serialize"), FJsonObjectConverter::UStructToJsonObjectString(Entry, Json));
	FVCSnapshotEquipmentEntry Back;
	TestTrue(TEXT("Parse"), FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Back));
	TestTrue(TEXT("The slot comes back"), Back.SlotTag == Entry.SlotTag);
	TestTrue(TEXT("The drawn state comes back"), Back.bDrawn);
	FVCSnapshotEquipmentEntry Old;
	TestTrue(TEXT("An old entry parses"), FJsonObjectConverter::JsonObjectStringToUStruct(
		FString(TEXT("{\"itemJson\":\"{}\"}")), &Old));
	TestFalse(TEXT("An old entry is not drawn"), Old.bDrawn);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSheath_HandOffTime, "VoxelCharacter.EquipmentLoop.SheathHandOffTime", VC_SHEATH_FLAGS)
bool FVCSheath_HandOffTime::RunTest(const FString& Parameters)
{
	using namespace VCSheathTestHelpers;
	TestEqual(TEXT("No montage: 0"), UVCActionAnimSet::GetHandOffTime(nullptr), 0.f);
	TestTrue(TEXT("The notify's time"), FMath::IsNearlyEqual(UVCActionAnimSet::GetHandOffTime(MakeMontage(1.5f, { 0.55f })), 0.55f, 1.0e-4f));
	TestTrue(TEXT("Without a notify, 40% of the length"), FMath::IsNearlyEqual(UVCActionAnimSet::GetHandOffTime(MakeMontage(1.5f)), 0.6f, 1.0e-4f));

	// A set's montages by sheath, the first entry for a sheath it has none for.
	UVCActionAnimSet* Set = NewObject<UVCActionAnimSet>(GetTransientPackage());
	UAnimMontage* HipOut = MakeMontage(1.f);
	UAnimMontage* HipIn = MakeMontage(1.f);
	UAnimMontage* BackOut = MakeMontage(1.f);
	FVCSheathMontages& Hip = Set->SheathMontages.AddDefaulted_GetRef();
	Hip.Sheath = CGFGameplayTags::Equipment_Slot_Sheath_HipLeft;
	Hip.Unsheathe = HipOut;
	Hip.Sheathe = HipIn;
	FVCSheathMontages& OnBack = Set->SheathMontages.AddDefaulted_GetRef();
	OnBack.Sheath = CGFGameplayTags::Equipment_Slot_Sheath_Back;
	OnBack.Unsheathe = BackOut;
	TestTrue(TEXT("The hip's unsheathe"), Set->GetSheathMontage(EVCAnimAction::Unsheathe, CGFGameplayTags::Equipment_Slot_Sheath_HipLeft) == HipOut);
	TestTrue(TEXT("The hip's sheathe"), Set->GetSheathMontage(EVCAnimAction::Sheathe, CGFGameplayTags::Equipment_Slot_Sheath_HipLeft) == HipIn);
	TestTrue(TEXT("The back's unsheathe"), Set->GetSheathMontage(EVCAnimAction::Unsheathe, CGFGameplayTags::Equipment_Slot_Sheath_Back) == BackOut);
	TestTrue(TEXT("The belt falls back to the first entry"), Set->GetSheathMontage(EVCAnimAction::Unsheathe, CGFGameplayTags::Equipment_Slot_Sheath_Belt) == HipOut);
	TestTrue(TEXT("Another action has no sheath montage"), Set->GetSheathMontage(EVCAnimAction::Attack, CGFGameplayTags::Equipment_Slot_Sheath_HipLeft) == nullptr);
	return true;
}

#endif // WITH_AUTOMATION_TESTS && WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
