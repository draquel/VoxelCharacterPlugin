// Copyright Daniel Raquel. All Rights Reserved.

// AVCPlayerController's click-to-move item management (a file of the class, code review CR4).

#include "Core/VCPlayerController.h"
#include "Core/VCPlayerState.h"
#include "UI/VCVitalsWidget.h"
#include "Core/VCCharacterBase.h"
#include "Gathering/VCGatherTable.h"
#include "Editing/VCBlockInteractionComponent.h"
#include "VoxelScatterManager.h"
#include "UI/VCCampsiteWidget.h"
#include "UI/VCCraftingPanelWidget.h"
#include "UI/VCPauseMenuWidget.h"
#include "Core/VCGameModeBase.h"
#include "Kismet/KismetSystemLibrary.h"
#include "AbilitySystemComponent.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Input/VCInputConfig.h"
#include "Movement/VCTerrainQueryHelper.h"
#include "Movement/VCGravityFrame.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "VoxelChunkManager.h"
#include "VoxelEditManager.h"
#include "VoxelEditTypes.h"
#include "VoxelWorldConfiguration.h"
#include "VoxelCoordinates.h"
#include "VoxelCharacterPlugin.h"

#if WITH_INVENTORY_PLUGIN
#include "Components/InventoryComponent.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Data/ItemDefinition.h"
#include "UI/HotbarWidget.h"
#include "UI/InventoryPanelWidget.h"
#include "UI/ItemCursorWidget.h"
#endif
#if WITH_EQUIPMENT_PLUGIN
#include "Components/EquipmentManagerComponent.h"
#include "UI/EquipmentPanelWidget.h"
#include "UI/EquipmentSlotWidget.h"
#endif

// ---------------------------------------------------------------------------
// Click-to-Move Item Management
// ---------------------------------------------------------------------------

void AVCPlayerController::BindSlotClickDelegates()
{
#if WITH_INVENTORY_PLUGIN
	if (bSlotDelegatesBound)
	{
		return;
	}

	if (UHotbarWidget* Hotbar = Cast<UHotbarWidget>(HotbarWidget))
	{
		Hotbar->OnSlotClicked.AddDynamic(this, &AVCPlayerController::OnSlotClickedFromUI);
		Hotbar->OnSlotRightClicked.AddDynamic(this, &AVCPlayerController::OnSlotRightClickedFromUI);
		Hotbar->OnSlotDoubleClicked.AddDynamic(this, &AVCPlayerController::OnSlotDoubleClickedFromUI);
	}

	if (UInventoryPanelWidget* Panel = Cast<UInventoryPanelWidget>(InventoryPanelWidget))
	{
		Panel->OnSlotClicked.AddDynamic(this, &AVCPlayerController::OnSlotClickedFromUI);
		Panel->OnSlotRightClicked.AddDynamic(this, &AVCPlayerController::OnSlotRightClickedFromUI);
		Panel->OnSlotDoubleClicked.AddDynamic(this, &AVCPlayerController::OnSlotDoubleClickedFromUI);
	}

#if WITH_EQUIPMENT_PLUGIN
	if (UEquipmentPanelWidget* EqPanel = Cast<UEquipmentPanelWidget>(EquipmentPanelWidget))
	{
		EqPanel->OnSlotClicked.AddDynamic(this, &AVCPlayerController::OnEquipmentSlotClickedFromUI);
		EqPanel->OnSlotRightClicked.AddDynamic(this, &AVCPlayerController::OnEquipmentSlotRightClickedFromUI);
		EqPanel->OnSlotDoubleClicked.AddDynamic(this, &AVCPlayerController::OnEquipmentSlotDoubleClickedFromUI);
	}
#endif

	bSlotDelegatesBound = true;
#endif
}

void AVCPlayerController::OnSlotClickedFromUI(int32 ClickedSlotIndex, UInventoryComponent* Inventory)
{
#if WITH_INVENTORY_PLUGIN
	if (HeldSourceType == EVCHeldSource::None)
	{
		// Nothing held — check if clicked slot has an item
		if (Inventory)
		{
			const FItemInstance Item = Inventory->GetItemInSlot(ClickedSlotIndex);
			if (Item.IsValid())
			{
				EnterHeldState(ClickedSlotIndex, Inventory);
			}
		}
	}
	else if (HeldSourceType == EVCHeldSource::Inventory)
	{
		if (ClickedSlotIndex == HeldSlotIndex && Inventory == HeldInventory)
		{
			// Clicked same slot — cancel
			CancelHeldState();
		}
		else
		{
			// Held inventory + clicked different inventory slot — swap
			ExecuteSwapAndClearHeld(ClickedSlotIndex);
		}
	}
	else if (HeldSourceType == EVCHeldSource::Equipment)
	{
		// Held from equipment, clicked inventory slot — unequip
#if WITH_EQUIPMENT_PLUGIN
		if (HeldEquipmentManager)
		{
			UInventoryComponent* TargetInventory = nullptr;
			if (APawn* ControlledPawn = GetPawn())
			{
				TargetInventory = ControlledPawn->FindComponentByClass<UInventoryComponent>();
			}

			if (TargetInventory)
			{
				// Save item ID so we can find where it lands
				const FGuid ItemId = HeldEquipmentManager->GetEquippedItem(HeldEquipmentSlotTag).InstanceId;

				const EEquipmentResult Result = HeldEquipmentManager->TryUnequipToInventory(HeldEquipmentSlotTag, TargetInventory);

				// Swap from the auto-assigned slot to the clicked slot
				if (Result == EEquipmentResult::Success && ItemId.IsValid())
				{
					const int32 LandedSlot = TargetInventory->FindSlotIndexByInstanceId(ItemId);
					if (LandedSlot != INDEX_NONE && LandedSlot != ClickedSlotIndex)
					{
						TargetInventory->TrySwapSlots(LandedSlot, ClickedSlotIndex);
					}
				}
			}
		}
#endif
		CancelHeldState();
	}
#endif
}

void AVCPlayerController::OnSlotDoubleClickedFromUI(int32 ClickedSlotIndex, UInventoryComponent* Inventory)
{
#if WITH_INVENTORY_PLUGIN
	// The first press of the double-click picked the item up; drop that and act on the slot instead.
	if (HeldSourceType != EVCHeldSource::None)
	{
		CancelHeldState();
	}
	AVCCharacterBase* VCChar = Cast<AVCCharacterBase>(GetPawn());
	if (VCChar && Inventory && Inventory == VCChar->FindComponentByClass<UInventoryComponent>())
	{
		VCChar->RequestUseItemInSlot(ClickedSlotIndex);
	}
#endif
}

void AVCPlayerController::OnEquipmentSlotDoubleClickedFromUI(FGameplayTag SlotTag, UEquipmentManagerComponent* EquipmentManager)
{
#if WITH_EQUIPMENT_PLUGIN
	if (HeldSourceType != EVCHeldSource::None)
	{
		CancelHeldState();
	}
	if (AVCCharacterBase* VCChar = Cast<AVCCharacterBase>(GetPawn()))
	{
		VCChar->UnequipSlotToInventory(SlotTag);
	}
#endif
}

void AVCPlayerController::OnSlotRightClickedFromUI(int32 ClickedSlotIndex, UInventoryComponent* Inventory)
{
#if WITH_INVENTORY_PLUGIN
	if (HeldSourceType != EVCHeldSource::None)
	{
		CancelHeldState();
	}
#endif
}

void AVCPlayerController::OnEquipmentSlotClickedFromUI(FGameplayTag SlotTag, UEquipmentManagerComponent* EquipmentManager)
{
#if WITH_EQUIPMENT_PLUGIN
	if (HeldSourceType == EVCHeldSource::None)
	{
		// Nothing held — check if equipment slot is occupied
		if (EquipmentManager)
		{
			const FItemInstance Item = EquipmentManager->GetEquippedItem(SlotTag);
			if (Item.IsValid())
			{
				EnterHeldStateFromEquipment(SlotTag, EquipmentManager);
			}
		}
	}
	else if (HeldSourceType == EVCHeldSource::Inventory)
	{
		// Held from inventory, clicked equipment slot — equip
#if WITH_INVENTORY_PLUGIN
		if (HeldInventory && EquipmentManager)
		{
			const FItemInstance Item = HeldInventory->GetItemInSlot(HeldSlotIndex);
			if (Item.IsValid())
			{
				EquipmentManager->TryEquipFromInventory(Item.InstanceId, HeldInventory, SlotTag);
			}
		}
#endif
		CancelHeldState();
	}
	else if (HeldSourceType == EVCHeldSource::Equipment)
	{
		if (SlotTag == HeldEquipmentSlotTag && EquipmentManager == HeldEquipmentManager)
		{
			// Clicked same equipment slot — cancel
			CancelHeldState();
		}
		else
		{
			// Equipment-to-equipment swap not supported — cancel
			CancelHeldState();
		}
	}
#endif
}

void AVCPlayerController::OnEquipmentSlotRightClickedFromUI(FGameplayTag SlotTag, UEquipmentManagerComponent* EquipmentManager)
{
#if WITH_EQUIPMENT_PLUGIN
	if (HeldSourceType != EVCHeldSource::None)
	{
		CancelHeldState();
	}
#endif
}

void AVCPlayerController::EnterHeldState(int32 InSlotIndex, UInventoryComponent* Inventory)
{
#if WITH_INVENTORY_PLUGIN
	HeldSourceType = EVCHeldSource::Inventory;
	HeldSlotIndex = InSlotIndex;
	HeldInventory = Inventory;

	SetSlotHeldVisual(InSlotIndex, true);
	ShowItemCursor(InSlotIndex, Inventory);

	UE_LOG(LogVoxelCharacter, Verbose, TEXT("EnterHeldState: Inventory Slot %d"), InSlotIndex);
#endif
}

void AVCPlayerController::EnterHeldStateFromEquipment(FGameplayTag InSlotTag, UEquipmentManagerComponent* EquipMgr)
{
#if WITH_EQUIPMENT_PLUGIN
	HeldSourceType = EVCHeldSource::Equipment;
	HeldEquipmentSlotTag = InSlotTag;
	HeldEquipmentManager = EquipMgr;

	SetEquipmentSlotHeldVisual(InSlotTag, true);
	ShowItemCursorForEquipment(InSlotTag, EquipMgr);

	UE_LOG(LogVoxelCharacter, Verbose, TEXT("EnterHeldState: Equipment Slot %s"), *InSlotTag.ToString());
#endif
}

void AVCPlayerController::ExecuteSwapAndClearHeld(int32 TargetSlotIndex)
{
#if WITH_INVENTORY_PLUGIN
	if (!HeldInventory || HeldSlotIndex == INDEX_NONE)
	{
		return;
	}

	// Clear visuals first
	SetSlotHeldVisual(HeldSlotIndex, false);
	HideItemCursor();

	const int32 SourceSlot = HeldSlotIndex;

	// Reset state before swap (in case swap triggers delegate callbacks)
	HeldSourceType = EVCHeldSource::None;
	HeldSlotIndex = INDEX_NONE;
	HeldInventory = nullptr;

	// Find the pawn's inventory for the swap
	UInventoryComponent* Inventory = nullptr;
	if (APawn* ControlledPawn = GetPawn())
	{
		Inventory = ControlledPawn->FindComponentByClass<UInventoryComponent>();
	}

	if (Inventory)
	{
		const EInventoryOperationResult Result = Inventory->TrySwapSlots(SourceSlot, TargetSlotIndex);
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("ExecuteSwap: %d <-> %d = %s"),
			SourceSlot, TargetSlotIndex,
			Result == EInventoryOperationResult::Success ? TEXT("Success") : TEXT("Failed"));
	}
#endif
}

void AVCPlayerController::CancelHeldState()
{
	if (HeldSourceType == EVCHeldSource::None)
	{
		return;
	}

#if WITH_INVENTORY_PLUGIN
	if (HeldSourceType == EVCHeldSource::Inventory)
	{
		SetSlotHeldVisual(HeldSlotIndex, false);
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("CancelHeldState: Inventory Slot %d"), HeldSlotIndex);
	}
#endif

#if WITH_EQUIPMENT_PLUGIN
	if (HeldSourceType == EVCHeldSource::Equipment)
	{
		SetEquipmentSlotHeldVisual(HeldEquipmentSlotTag, false);
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("CancelHeldState: Equipment Slot %s"), *HeldEquipmentSlotTag.ToString());
	}
#endif

	HideItemCursor();

	HeldSourceType = EVCHeldSource::None;
	HeldSlotIndex = INDEX_NONE;
	HeldInventory = nullptr;
	HeldEquipmentSlotTag = FGameplayTag();
	HeldEquipmentManager = nullptr;
}

void AVCPlayerController::SetSlotHeldVisual(int32 InSlotIndex, bool bHeld)
{
#if WITH_INVENTORY_PLUGIN
	// Hotbar slots are [0, 9), panel slots are [9, MaxSlots)
	if (InSlotIndex < 9)
	{
		if (UHotbarWidget* Hotbar = Cast<UHotbarWidget>(HotbarWidget))
		{
			Hotbar->SetSlotHeld(InSlotIndex, bHeld);
		}
	}
	else
	{
		if (UInventoryPanelWidget* Panel = Cast<UInventoryPanelWidget>(InventoryPanelWidget))
		{
			Panel->SetSlotHeld(InSlotIndex, bHeld);
		}
	}
#endif
}

void AVCPlayerController::SetEquipmentSlotHeldVisual(FGameplayTag InSlotTag, bool bHeld)
{
#if WITH_EQUIPMENT_PLUGIN
	if (UEquipmentPanelWidget* EqPanel = Cast<UEquipmentPanelWidget>(EquipmentPanelWidget))
	{
		EqPanel->SetSlotHeld(InSlotTag, bHeld);
	}
#endif
}

void AVCPlayerController::ShowItemCursorForEquipment(FGameplayTag InSlotTag, UEquipmentManagerComponent* EquipMgr)
{
#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
	if (!EquipMgr)
	{
		return;
	}

	// Lazy-create cursor widget
	if (!ItemCursorWidget)
	{
		TSubclassOf<UUserWidget> ClassToUse = ItemCursorWidgetClass;
		if (!ClassToUse)
		{
			ClassToUse = UItemCursorWidget::StaticClass();
		}
		ItemCursorWidget = CreateWidget<UItemCursorWidget>(this, ClassToUse);
		if (ItemCursorWidget)
		{
			ItemCursorWidget->AddToViewport(100);
		}
	}

	if (!ItemCursorWidget)
	{
		return;
	}

	// Resolve item icon from equipped item
	const FItemInstance Item = EquipMgr->GetEquippedItem(InSlotTag);
	TSoftObjectPtr<UTexture2D> IconRef;

	if (Item.IsValid())
	{
		UItemDatabaseSubsystem* ItemDB = nullptr;
		if (const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
		{
			ItemDB = GI->GetSubsystem<UItemDatabaseSubsystem>();
		}

		if (ItemDB)
		{
			if (const UItemDefinition* Def = ItemDB->GetDefinition(Item.ItemDefinitionId))
			{
				IconRef = Def->Icon;
			}
		}
	}

	ItemCursorWidget->ShowWithIcon(IconRef);
#endif
}

void AVCPlayerController::ShowItemCursor(int32 InSlotIndex, UInventoryComponent* Inventory)
{
#if WITH_INVENTORY_PLUGIN
	if (!Inventory)
	{
		return;
	}

	// Lazy-create cursor widget
	if (!ItemCursorWidget)
	{
		TSubclassOf<UUserWidget> ClassToUse = ItemCursorWidgetClass;
		if (!ClassToUse)
		{
			ClassToUse = UItemCursorWidget::StaticClass();
		}
		ItemCursorWidget = CreateWidget<UItemCursorWidget>(this, ClassToUse);
		if (ItemCursorWidget)
		{
			ItemCursorWidget->AddToViewport(100);
		}
	}

	if (!ItemCursorWidget)
	{
		return;
	}

	// Resolve item icon
	const FItemInstance Item = Inventory->GetItemInSlot(InSlotIndex);
	TSoftObjectPtr<UTexture2D> IconRef;

	if (Item.IsValid())
	{
		UItemDatabaseSubsystem* ItemDB = nullptr;
		if (const UGameInstance* GI = GetWorld() ? GetWorld()->GetGameInstance() : nullptr)
		{
			ItemDB = GI->GetSubsystem<UItemDatabaseSubsystem>();
		}

		if (ItemDB)
		{
			if (const UItemDefinition* Def = ItemDB->GetDefinition(Item.ItemDefinitionId))
			{
				IconRef = Def->Icon;
			}
		}
	}

	ItemCursorWidget->ShowWithIcon(IconRef);
#endif
}

void AVCPlayerController::HideItemCursor()
{
#if WITH_INVENTORY_PLUGIN
	if (ItemCursorWidget)
	{
		ItemCursorWidget->HideCursor();
	}
#endif
}
