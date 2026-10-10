// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCPlayerController.h"
#include "Core/VCPlayerState.h"
#include "UI/VCVitalsWidget.h"
#include "Core/VCCharacterBase.h"
#include "Gathering/VCGatherTable.h"
#include "UI/VCCampsiteWidget.h"
#include "UI/VCCraftingPanelWidget.h"
#include "AbilitySystemComponent.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Input/VCInputConfig.h"
#include "Movement/VCVoxelNavigationHelper.h"
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

#if WITH_INTERACTION_PLUGIN
#include "Subsystems/WorldItemPoolSubsystem.h"
#include "Actors/WorldItem.h"
#include "UI/InteractionPromptWidget.h"
#endif

#if WITH_EQUIPMENT_PLUGIN
#include "Components/EquipmentManagerComponent.h"
#include "UI/EquipmentPanelWidget.h"
#include "UI/EquipmentSlotWidget.h"
#endif

#include "Map/VCMinimapWidget.h"
#include "Map/VCWorldMapWidget.h"

AVCPlayerController::AVCPlayerController()
{
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void AVCPlayerController::BeginPlay()
{
	Super::BeginPlay();
	SetGameInputMode();

	if (IsLocalController())
	{
		CreatePersistentWidgets();
	}
}

void AVCPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Add the default gameplay mapping context
	if (InputConfig && InputConfig->IMC_Gameplay)
	{
		AddInputMappingContext(InputConfig->IMC_Gameplay, 0);
	}

	BindVitalsToPawn(InPawn);

	// Initialize hotbar with the possessed pawn's inventory
#if WITH_INVENTORY_PLUGIN
	if (HotbarWidget && InPawn)
	{
		if (UInventoryComponent* Inventory = InPawn->FindComponentByClass<UInventoryComponent>())
		{
			if (UHotbarWidget* Hotbar = Cast<UHotbarWidget>(HotbarWidget))
			{
				Hotbar->InitHotbar(Inventory, 9);
			}
		}
	}
#endif
}

void AVCPlayerController::OnUnPossess()
{
	// Remove mapping contexts when we lose our pawn
	if (InputConfig && InputConfig->IMC_Gameplay)
	{
		RemoveInputMappingContext(InputConfig->IMC_Gameplay);
	}
	if (InputConfig && InputConfig->IMC_UI)
	{
		RemoveInputMappingContext(InputConfig->IMC_UI);
	}

	Super::OnUnPossess();
}

// ---------------------------------------------------------------------------
// Input Mode
// ---------------------------------------------------------------------------

void AVCPlayerController::SetGameInputMode()
{
	FInputModeGameOnly InputMode;
	SetInputMode(InputMode);
	SetShowMouseCursor(false);
	bIsInUIMode = false;

	// Remove UI context, ensure gameplay context is active
	if (InputConfig)
	{
		if (InputConfig->IMC_UI)
		{
			RemoveInputMappingContext(InputConfig->IMC_UI);
		}
	}
}

void AVCPlayerController::SetUIInputMode(UUserWidget* FocusWidget)
{
	FInputModeGameAndUI InputMode;
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	if (FocusWidget)
	{
		InputMode.SetWidgetToFocus(FocusWidget->TakeWidget());
	}
	SetInputMode(InputMode);
	SetShowMouseCursor(true);
	bIsInUIMode = true;

	// Add UI context on top of gameplay
	if (InputConfig && InputConfig->IMC_UI)
	{
		AddInputMappingContext(InputConfig->IMC_UI, 1);
	}
}

// ---------------------------------------------------------------------------
// UI
// ---------------------------------------------------------------------------

void AVCPlayerController::ToggleInventoryUI()
{
	bInventoryOpen = !bInventoryOpen;

	if (bInventoryOpen)
	{
		ShowInventoryPanels();
		SetUIInputMode();
	}
	else
	{
		HideInventoryPanels();
		SetGameInputMode();
	}

	UE_LOG(LogVoxelCharacter, Verbose, TEXT("ToggleInventoryUI: %s"), bInventoryOpen ? TEXT("Open") : TEXT("Closed"));
}

void AVCPlayerController::ToggleWorldMapUI()
{
	bWorldMapOpen = !bWorldMapOpen;

	if (bWorldMapOpen)
	{
		// Lazy-create world map widget
		if (!WorldMapWidget)
		{
			TSubclassOf<UUserWidget> ClassToUse = WorldMapWidgetClass;
			if (!ClassToUse)
			{
				ClassToUse = UVCWorldMapWidget::StaticClass();
			}
			WorldMapWidget = CreateWidget<UUserWidget>(this, ClassToUse);
		}

		if (WorldMapWidget)
		{
			if (!WorldMapWidget->IsInViewport())
			{
				WorldMapWidget->AddToViewport(10);
			}
			WorldMapWidget->SetVisibility(ESlateVisibility::Visible);

			// Refresh the map data
			if (UVCWorldMapWidget* WMap = Cast<UVCWorldMapWidget>(WorldMapWidget))
			{
				WMap->RefreshMap();
			}
		}

		SetUIInputMode(WorldMapWidget);
	}
	else
	{
		if (WorldMapWidget && WorldMapWidget->IsInViewport())
		{
			WorldMapWidget->RemoveFromParent();
		}
		SetGameInputMode();
	}

	UE_LOG(LogVoxelCharacter, Verbose, TEXT("ToggleWorldMapUI: %s"), bWorldMapOpen ? TEXT("Open") : TEXT("Closed"));
}

void AVCPlayerController::ShowInteractionPrompt(AActor* InteractableActor)
{
#if WITH_INTERACTION_PLUGIN
	if (UInteractionPromptWidget* Prompt = Cast<UInteractionPromptWidget>(InteractionPromptWidget))
	{
		Prompt->ShowPromptForActor(InteractableActor);
	}
#endif
}

void AVCPlayerController::HideInteractionPrompt()
{
#if WITH_INTERACTION_PLUGIN
	if (UInteractionPromptWidget* Prompt = Cast<UInteractionPromptWidget>(InteractionPromptWidget))
	{
		Prompt->HidePrompt();
	}
#endif
}

void AVCPlayerController::UpdateHotbarSelection(int32 SlotIndex)
{
#if WITH_INVENTORY_PLUGIN
	if (UHotbarWidget* Hotbar = Cast<UHotbarWidget>(HotbarWidget))
	{
		Hotbar->SetActiveSlot(SlotIndex);
	}
#endif
}

void AVCPlayerController::SetObjectiveText(const FText& Text)
{
	if (UVCVitalsWidget* Vitals = Cast<UVCVitalsWidget>(VitalsWidget))
	{
		Vitals->SetObjective(Text);
	}
}

void AVCPlayerController::ClearObjectiveText()
{
	if (UVCVitalsWidget* Vitals = Cast<UVCVitalsWidget>(VitalsWidget))
	{
		Vitals->ClearObjective();
	}
}

void AVCPlayerController::ShowToast(const FText& Text, float Duration)
{
	if (UVCVitalsWidget* Vitals = Cast<UVCVitalsWidget>(VitalsWidget))
	{
		Vitals->ShowToast(Text, Duration);
	}
}

FText AVCPlayerController::GetObjectiveText() const
{
	const UVCVitalsWidget* Vitals = Cast<UVCVitalsWidget>(VitalsWidget);
	return Vitals ? Vitals->GetObjectiveText() : FText::GetEmpty();
}

void AVCPlayerController::InitPlayerState()
{
	Super::InitPlayerState();
	BindProgression();
}

void AVCPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	BindProgression();
}

void AVCPlayerController::BindProgression()
{
	AVCPlayerState* PS = GetPlayerState<AVCPlayerState>();
	if (!PS || !IsLocalController())
	{
		return;
	}
	if (!PS->OnProgressionChanged.IsAlreadyBound(this, &AVCPlayerController::HandleProgressionChanged))
	{
		PS->OnProgressionChanged.AddDynamic(this, &AVCPlayerController::HandleProgressionChanged);
	}
	LastProgression = PS->GetProgression();
}

void AVCPlayerController::HandleProgressionChanged(const FVCProgressionStats& Stats)
{
	if (Stats.DungeonsCleared > LastProgression.DungeonsCleared)
	{
		ShowToast(FText::Format(NSLOCTEXT("VCHud", "DungeonClearedToast", "Dungeon cleared! ({0})"), Stats.DungeonsCleared), 4.0f);
	}
	LastProgression = Stats;
}

void AVCPlayerController::BindVitalsToPawn(APawn* InPawn)
{
	if (UVCVitalsWidget* Vitals = Cast<UVCVitalsWidget>(VitalsWidget))
	{
		Vitals->InitWithCharacter(Cast<AVCCharacterBase>(InPawn));
	}
}

void AVCPlayerController::CreatePersistentWidgets()
{
	UE_LOG(LogVoxelCharacter, Log, TEXT("CreatePersistentWidgets: IsLocal=%s, Pawn=%s"),
		IsLocalController() ? TEXT("true") : TEXT("false"),
		GetPawn() ? *GetPawn()->GetName() : TEXT("null"));

#if WITH_INVENTORY_PLUGIN
	{
		TSubclassOf<UUserWidget> ClassToUse = HotbarWidgetClass;
		if (!ClassToUse)
		{
			ClassToUse = UHotbarWidget::StaticClass();
		}
		HotbarWidget = CreateWidget<UUserWidget>(this, ClassToUse);
		UE_LOG(LogVoxelCharacter, Log, TEXT("CreatePersistentWidgets: HotbarWidget=%s, Class=%s"),
			HotbarWidget ? TEXT("created") : TEXT("FAILED"),
			*ClassToUse->GetName());

		if (HotbarWidget)
		{
			HotbarWidget->AddToViewport(0);
			HotbarWidget->SetAnchorsInViewport(FAnchors(0.5f, 0.95f, 0.5f, 0.95f));
			HotbarWidget->SetAlignmentInViewport(FVector2D(0.5f, 1.f));
			HotbarWidget->SetVisibility(ESlateVisibility::HitTestInvisible);

			// OnPossess fires before BeginPlay, so the pawn may already be possessed
			if (APawn* CurrentPawn = GetPawn())
			{
				UInventoryComponent* Inventory = CurrentPawn->FindComponentByClass<UInventoryComponent>();
				UE_LOG(LogVoxelCharacter, Log, TEXT("CreatePersistentWidgets: Pawn=%s, Inventory=%s"),
					*CurrentPawn->GetName(),
					Inventory ? TEXT("found") : TEXT("NOT FOUND"));

				if (Inventory)
				{
					if (UHotbarWidget* Hotbar = Cast<UHotbarWidget>(HotbarWidget))
					{
						Hotbar->InitHotbar(Inventory, 9);
					}
					else
					{
						UE_LOG(LogVoxelCharacter, Error, TEXT("CreatePersistentWidgets: Cast to UHotbarWidget FAILED"));
					}
				}
			}
		}
	}
#endif

#if WITH_INTERACTION_PLUGIN
	{
		TSubclassOf<UUserWidget> ClassToUse = InteractionPromptWidgetClass;
		if (!ClassToUse)
		{
			ClassToUse = UInteractionPromptWidget::StaticClass();
		}
		InteractionPromptWidget = CreateWidget<UUserWidget>(this, ClassToUse);
		if (InteractionPromptWidget)
		{
			InteractionPromptWidget->AddToViewport(2);
			InteractionPromptWidget->SetAnchorsInViewport(FAnchors(0.5f, 0.7f, 0.5f, 0.7f));
			InteractionPromptWidget->SetAlignmentInViewport(FVector2D(0.5f, 0.5f));
			// Starts collapsed (NativeConstruct sets Collapsed)
		}
	}
#endif

	// Vitals (bottom-left, always visible, Z-order 1)
	{
		TSubclassOf<UUserWidget> ClassToUse = VitalsWidgetClass;
		if (!ClassToUse)
		{
			ClassToUse = UVCVitalsWidget::StaticClass();
		}
		VitalsWidget = CreateWidget<UUserWidget>(this, ClassToUse);
		if (VitalsWidget)
		{
			VitalsWidget->AddToViewport(1);
			// Anchor + alignment + padding, like the hotbar and minimap. SetPositionInViewport on a
			// bottom-anchored widget pushed it off-screen (the HUD was invisible until feature 5b).
			VitalsWidget->SetAnchorsInViewport(FAnchors(0.0f, 1.0f, 0.0f, 1.0f));
			VitalsWidget->SetAlignmentInViewport(FVector2D(0.0f, 1.0f));
			VitalsWidget->SetPadding(FMargin(24.f, 0.f, 0.f, 24.f));
			VitalsWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
			BindVitalsToPawn(GetPawn());
		}
	}

	// Minimap (top-right corner, always visible, Z-order 1)
	{
		TSubclassOf<UUserWidget> ClassToUse = MinimapWidgetClass;
		if (!ClassToUse)
		{
			ClassToUse = UVCMinimapWidget::StaticClass();
		}
		MinimapWidget = CreateWidget<UUserWidget>(this, ClassToUse);
		if (MinimapWidget)
		{
			MinimapWidget->AddToViewport(1);
			MinimapWidget->SetAnchorsInViewport(FAnchors(1.0f, 0.0f, 1.0f, 0.0f));
			MinimapWidget->SetAlignmentInViewport(FVector2D(1.0f, 0.0f));
			MinimapWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
		}

		UE_LOG(LogVoxelCharacter, Log, TEXT("CreatePersistentWidgets: MinimapWidget=%s"),
			MinimapWidget ? TEXT("created") : TEXT("FAILED"));
	}

	// Optional clock (feature 6): whatever widget the game assigns, anchored top-centre.
	if (ClockWidgetClass && !ClockWidget)
	{
		ClockWidget = CreateWidget<UUserWidget>(this, ClockWidgetClass);
		if (ClockWidget)
		{
			ClockWidget->AddToViewport(1);
			ClockWidget->SetAnchorsInViewport(FAnchors(0.5f, 0.0f, 0.5f, 0.0f));
			ClockWidget->SetAlignmentInViewport(FVector2D(0.5f, 0.0f));
			ClockWidget->SetPadding(FMargin(0.f, 12.f, 0.f, 0.f));
			ClockWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	}
}

void AVCPlayerController::ShowInventoryPanels()
{
#if WITH_INVENTORY_PLUGIN
	if (!InventoryPanelWidget)
	{
		TSubclassOf<UUserWidget> ClassToUse = InventoryPanelWidgetClass;
		if (!ClassToUse)
		{
			ClassToUse = UInventoryPanelWidget::StaticClass();
		}
		InventoryPanelWidget = CreateWidget<UUserWidget>(this, ClassToUse);
	}
	if (InventoryPanelWidget)
	{
		if (!InventoryPanelWidget->IsInViewport())
		{
			InventoryPanelWidget->AddToViewport(1);
			InventoryPanelWidget->SetAnchorsInViewport(FAnchors(0.65f, 0.3f, 0.65f, 0.3f));
			InventoryPanelWidget->SetAlignmentInViewport(FVector2D(0.5f, 0.f));
		}

		// Init with pawn's inventory
		if (APawn* ControlledPawn = GetPawn())
		{
			if (UInventoryComponent* Inventory = ControlledPawn->FindComponentByClass<UInventoryComponent>())
			{
				if (UInventoryPanelWidget* Panel = Cast<UInventoryPanelWidget>(InventoryPanelWidget))
				{
					Panel->InitPanel(Inventory, 9);
				}
			}
		}

		InventoryPanelWidget->SetVisibility(ESlateVisibility::Visible);
	}

	// Make hotbar clickable while inventory is open
	if (HotbarWidget)
	{
		HotbarWidget->SetVisibility(ESlateVisibility::Visible);
	}
#endif

#if WITH_EQUIPMENT_PLUGIN
	if (!EquipmentPanelWidget)
	{
		TSubclassOf<UUserWidget> ClassToUse = EquipmentPanelWidgetClass;
		if (!ClassToUse)
		{
			ClassToUse = UEquipmentPanelWidget::StaticClass();
		}
		EquipmentPanelWidget = CreateWidget<UUserWidget>(this, ClassToUse);
	}
	if (EquipmentPanelWidget)
	{
		if (!EquipmentPanelWidget->IsInViewport())
		{
			EquipmentPanelWidget->AddToViewport(1);
			EquipmentPanelWidget->SetAnchorsInViewport(FAnchors(0.35f, 0.3f, 0.35f, 0.3f));
			EquipmentPanelWidget->SetAlignmentInViewport(FVector2D(0.5f, 0.f));
		}

		// Init with pawn's equipment manager
		if (APawn* ControlledPawn = GetPawn())
		{
			if (UEquipmentManagerComponent* EquipMgr = ControlledPawn->FindComponentByClass<UEquipmentManagerComponent>())
			{
				if (UEquipmentPanelWidget* Panel = Cast<UEquipmentPanelWidget>(EquipmentPanelWidget))
				{
					Panel->InitPanel(EquipMgr);
				}
			}
		}

		EquipmentPanelWidget->SetVisibility(ESlateVisibility::Visible);
	}
#endif

	BindSlotClickDelegates();
}

void AVCPlayerController::HideInventoryPanels()
{
#if WITH_INVENTORY_PLUGIN
	CancelHeldState();

	// Revert hotbar to display-only
	if (HotbarWidget)
	{
		HotbarWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
#endif

	if (InventoryPanelWidget && InventoryPanelWidget->IsInViewport())
	{
		InventoryPanelWidget->RemoveFromParent();
	}

	if (EquipmentPanelWidget && EquipmentPanelWidget->IsInViewport())
	{
		EquipmentPanelWidget->RemoveFromParent();
	}
}

// ---------------------------------------------------------------------------
// Mapping Context Helpers
// ---------------------------------------------------------------------------

void AVCPlayerController::AddInputMappingContext(const UInputMappingContext* Context, int32 Priority)
{
	if (!Context)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		FModifyContextOptions Options;
		Options.bIgnoreAllPressedKeysUntilRelease = false;
		Subsystem->AddMappingContext(Context, Priority, Options);
	}
}

void AVCPlayerController::RemoveInputMappingContext(const UInputMappingContext* Context)
{
	if (!Context)
	{
		return;
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->RemoveMappingContext(Context);
	}
}

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
	}

	if (UInventoryPanelWidget* Panel = Cast<UInventoryPanelWidget>(InventoryPanelWidget))
	{
		Panel->OnSlotClicked.AddDynamic(this, &AVCPlayerController::OnSlotClickedFromUI);
		Panel->OnSlotRightClicked.AddDynamic(this, &AVCPlayerController::OnSlotRightClickedFromUI);
	}

#if WITH_EQUIPMENT_PLUGIN
	if (UEquipmentPanelWidget* EqPanel = Cast<UEquipmentPanelWidget>(EquipmentPanelWidget))
	{
		EqPanel->OnSlotClicked.AddDynamic(this, &AVCPlayerController::OnEquipmentSlotClickedFromUI);
		EqPanel->OnSlotRightClicked.AddDynamic(this, &AVCPlayerController::OnEquipmentSlotRightClickedFromUI);
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

// ---------------------------------------------------------------------------
// Debug Commands
// ---------------------------------------------------------------------------

void AVCPlayerController::GiveItem(FString AssetName, int32 Count)
{
#if WITH_INVENTORY_PLUGIN
	if (Count <= 0)
	{
		Count = 1;
	}

	UItemDatabaseSubsystem* ItemDB = GetGameInstance()->GetSubsystem<UItemDatabaseSubsystem>();
	if (!ItemDB)
	{
		UE_LOG(LogVoxelCharacter, Error, TEXT("GiveItem: ItemDatabaseSubsystem not found"));
		return;
	}

	// Resolve asset name by substring match against all registered definitions
	FPrimaryAssetId FoundId;
	const TArray<FPrimaryAssetId> AllIds = ItemDB->GetAllDefinitionIds();

	for (const FPrimaryAssetId& Id : AllIds)
	{
		if (Id.PrimaryAssetName.ToString().Contains(AssetName))
		{
			FoundId = Id;
			break;
		}
	}

	if (!FoundId.IsValid())
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("GiveItem: No item found matching '%s'. Available items:"), *AssetName);
		for (const FPrimaryAssetId& Id : AllIds)
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("  - %s"), *Id.PrimaryAssetName.ToString());
		}
		return;
	}

	FItemInstance Instance = ItemDB->CreateItemInstance(FoundId, Count);
	if (!Instance.IsValid())
	{
		UE_LOG(LogVoxelCharacter, Error, TEXT("GiveItem: Failed to create item instance for '%s'"), *FoundId.ToString());
		return;
	}

	// Find inventory on the possessed pawn
	UInventoryComponent* Inventory = nullptr;
	if (APawn* ControlledPawn = GetPawn())
	{
		Inventory = ControlledPawn->FindComponentByClass<UInventoryComponent>();
	}

	if (!Inventory)
	{
		UE_LOG(LogVoxelCharacter, Error, TEXT("GiveItem: No InventoryComponent found on possessed pawn"));
		return;
	}

	const EInventoryOperationResult Result = Inventory->TryAddItem(Instance);
	UE_LOG(LogVoxelCharacter, Log, TEXT("GiveItem: %s x%d -> %s"),
		*FoundId.PrimaryAssetName.ToString(), Count,
		Result == EInventoryOperationResult::Success ? TEXT("Success") : TEXT("Failed"));
#else
	UE_LOG(LogVoxelCharacter, Warning, TEXT("GiveItem: ItemInventoryPlugin not enabled"));
#endif
}

void AVCPlayerController::SpawnWorldItem(FString AssetName, int32 Count)
{
#if WITH_INVENTORY_PLUGIN && WITH_INTERACTION_PLUGIN
	if (Count <= 0)
	{
		Count = 1;
	}

	UItemDatabaseSubsystem* ItemDB = GetGameInstance()->GetSubsystem<UItemDatabaseSubsystem>();
	if (!ItemDB)
	{
		UE_LOG(LogVoxelCharacter, Error, TEXT("SpawnWorldItem: ItemDatabaseSubsystem not found"));
		return;
	}

	// Resolve asset name by substring match
	FPrimaryAssetId FoundId;
	const TArray<FPrimaryAssetId> AllIds = ItemDB->GetAllDefinitionIds();

	for (const FPrimaryAssetId& Id : AllIds)
	{
		if (Id.PrimaryAssetName.ToString().Contains(AssetName))
		{
			FoundId = Id;
			break;
		}
	}

	if (!FoundId.IsValid())
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("SpawnWorldItem: No item found matching '%s'. Available items:"), *AssetName);
		for (const FPrimaryAssetId& Id : AllIds)
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("  - %s"), *Id.PrimaryAssetName.ToString());
		}
		return;
	}

	FItemInstance Instance = ItemDB->CreateItemInstance(FoundId, Count);
	if (!Instance.IsValid())
	{
		UE_LOG(LogVoxelCharacter, Error, TEXT("SpawnWorldItem: Failed to create item instance for '%s'"), *FoundId.ToString());
		return;
	}

	// Spawn in front of the player
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		UE_LOG(LogVoxelCharacter, Error, TEXT("SpawnWorldItem: No possessed pawn"));
		return;
	}

	const FVector DropLoc = ControlledPawn->GetActorLocation() + ControlledPawn->GetActorForwardVector() * 200.f;

	UWorldItemPoolSubsystem* Pool = GetWorld()->GetSubsystem<UWorldItemPoolSubsystem>();
	if (!Pool)
	{
		UE_LOG(LogVoxelCharacter, Error, TEXT("SpawnWorldItem: WorldItemPoolSubsystem not found"));
		return;
	}

	AWorldItem* Spawned = Pool->SpawnWorldItem(Instance, DropLoc);
	UE_LOG(LogVoxelCharacter, Log, TEXT("SpawnWorldItem: %s x%d at (%.0f, %.0f, %.0f) -> %s"),
		*FoundId.PrimaryAssetName.ToString(), Count,
		DropLoc.X, DropLoc.Y, DropLoc.Z,
		Spawned ? TEXT("Success") : TEXT("Failed"));
#else
	UE_LOG(LogVoxelCharacter, Warning, TEXT("SpawnWorldItem: ItemInventoryPlugin/InteractionPlugin not enabled"));
#endif
}

// ---------------------------------------------------------------------------
// Server RPC — Voxel Modification
// ---------------------------------------------------------------------------

void AVCPlayerController::Server_RequestVoxelModification_Implementation(const FIntVector& VoxelCoord, EVoxelModificationType ModType, uint8 MaterialID)
{
	// --- Validation ---
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("Server_RequestVoxelModification: No pawn"));
		return;
	}

	UVoxelChunkManager* ChunkMgr = FVCVoxelNavigationHelper::FindChunkManager(GetWorld());
	if (!ChunkMgr || !ChunkMgr->IsInitialized())
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("Server_RequestVoxelModification: No chunk manager"));
		return;
	}

	const UVoxelWorldConfiguration* Config = ChunkMgr->GetConfiguration();
	if (!Config)
	{
		return;
	}

	// Convert voxel coordinate back to world position for distance validation
	const FVector VoxelWorldPos = FVector(VoxelCoord) * Config->VoxelSize + Config->WorldOrigin;
	const float DistToVoxel = FVector::Dist(ControlledPawn->GetActorLocation(), VoxelWorldPos);

	// Distance check: reject modifications beyond max interaction range
	constexpr float MaxModificationRange = 800.f;
	if (DistToVoxel > MaxModificationRange)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("Server_RequestVoxelModification: Out of range (%.0f > %.0f)"),
			DistToVoxel, MaxModificationRange);
		return;
	}

	// --- Apply Edit ---
	UVoxelEditManager* EditMgr = ChunkMgr->GetEditManager();
	if (!EditMgr)
	{
		return;
	}

	// Set edit source to Player so scatter is permanently removed
	EditMgr->SetEditSource(EEditSource::Player);

	FVoxelBrushParams Brush;
	Brush.Shape = EVoxelBrushShape::Sphere;

	switch (ModType)
	{
	case EVoxelModificationType::Destroy:
	{
		Brush.Radius = Config->VoxelSize * 1.5f;
		Brush.Strength = 1.f;
		Brush.FalloffType = EVoxelBrushFalloff::Smooth;
		Brush.DensityDelta = 80;

		// What is being dug, read before the brush empties it (feature 8: gathering).
		const uint8 DugMaterial = ChunkMgr->GetEditMergedVoxelAtWorldPosition(VoxelWorldPos).MaterialID;

		EditMgr->BeginEditOperation(TEXT("Player dig"));
		const int32 Modified = EditMgr->ApplyBrushEdit(VoxelWorldPos, Brush, EEditMode::Subtract);
		EditMgr->EndEditOperation();
		if (Modified > 0)
		{
			GatherFromVoxel(DugMaterial);
		}

		UE_LOG(LogVoxelCharacter, Verbose, TEXT("Voxel destroyed at [%d,%d,%d]"),
			VoxelCoord.X, VoxelCoord.Y, VoxelCoord.Z);
		break;
	}

	case EVoxelModificationType::Place:
	{
		// Reject placement if the voxel overlaps the character's capsule
		if (const ACharacter* PawnCharacter = Cast<ACharacter>(ControlledPawn))
		{
			if (const UCapsuleComponent* Capsule = PawnCharacter->GetCapsuleComponent())
			{
				const float VoxelSize = Config->VoxelSize;
				const FVector PawnPos = PawnCharacter->GetActorLocation();
				const float R = Capsule->GetScaledCapsuleRadius();
				const float HH = Capsule->GetScaledCapsuleHalfHeight();

				const FVector VoxelMax = VoxelWorldPos + FVector(VoxelSize);
				const bool bOverlaps =
					VoxelWorldPos.X < PawnPos.X + R && VoxelMax.X > PawnPos.X - R &&
					VoxelWorldPos.Y < PawnPos.Y + R && VoxelMax.Y > PawnPos.Y - R &&
					VoxelWorldPos.Z < PawnPos.Z + HH && VoxelMax.Z > PawnPos.Z - HH;

				if (bOverlaps)
				{
					UE_LOG(LogVoxelCharacter, Verbose,
						TEXT("Server_RequestVoxelModification: Rejected place at [%d,%d,%d] — overlaps pawn capsule"),
						VoxelCoord.X, VoxelCoord.Y, VoxelCoord.Z);
					return;
				}
			}
		}

		Brush.Radius = Config->VoxelSize * 0.8f;
		Brush.Strength = 1.f;
		Brush.FalloffType = EVoxelBrushFalloff::Sharp;
		Brush.MaterialID = MaterialID;
		Brush.DensityDelta = 80;

		EditMgr->BeginEditOperation(TEXT("Player place"));
		EditMgr->ApplyBrushEdit(VoxelWorldPos, Brush, EEditMode::Add);
		EditMgr->EndEditOperation();

		UE_LOG(LogVoxelCharacter, Verbose, TEXT("Voxel placed at [%d,%d,%d] Material=%d"),
			VoxelCoord.X, VoxelCoord.Y, VoxelCoord.Z, MaterialID);
		break;
	}

	case EVoxelModificationType::Paint:
	{
		Brush.Radius = Config->VoxelSize * 1.0f;
		Brush.Strength = 1.f;
		Brush.MaterialID = MaterialID;

		EditMgr->BeginEditOperation(TEXT("Player paint"));
		EditMgr->ApplyBrushEdit(VoxelWorldPos, Brush, EEditMode::Paint);
		EditMgr->EndEditOperation();

		UE_LOG(LogVoxelCharacter, Verbose, TEXT("Voxel painted at [%d,%d,%d] Material=%d"),
			VoxelCoord.X, VoxelCoord.Y, VoxelCoord.Z, MaterialID);
		break;
	}
	}

	// Edit-protection feedback: the edit manager counts voxels a validator vetoed during the
	// apply above (e.g. protected POI/dungeon claims). Tell the owning client so its UI can react.
	const int32 RejectedVoxels = EditMgr->GetLastRejectedEditCount();
	if (RejectedVoxels > 0)
	{
		Client_NotifyVoxelEditBlocked(RejectedVoxels);
	}
}

void AVCPlayerController::GatherFromVoxel(uint8 MaterialId)
{
#if WITH_INVENTORY_PLUGIN
	AVCCharacterBase* Digger = Cast<AVCCharacterBase>(GetPawn());
	const FVCGatherEntry* Entry = GatherTable ? GatherTable->Find(MaterialId) : nullptr;
	if (!Digger || !Entry || !Entry->ItemId.IsValid())
	{
		return;
	}
	const int32 Count = UVCGatherTable::YieldFor(*Entry, Digger->GetMiningSpeed(), GatherTable->ToolMiningSpeedThreshold);
	if (Count <= 0)
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UItemDatabaseSubsystem* ItemDB = GameInstance ? GameInstance->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	UInventoryComponent* Inventory = Digger->FindComponentByClass<UInventoryComponent>();
	const UItemDefinition* Def = ItemDB ? ItemDB->GetDefinition(Entry->ItemId) : nullptr;
	if (!Def || !Inventory)
	{
		return;
	}
	FItemInstance Item = ItemDB->CreateItemInstance(Entry->ItemId, Count);
	if (Inventory->TryAddItem(Item) != EInventoryOperationResult::Success)
	{
#if WITH_INTERACTION_PLUGIN
		// Full: it lands at the player's feet as a pickup instead of vanishing.
		if (UWorldItemPoolSubsystem* Pool = GetWorld()->GetSubsystem<UWorldItemPoolSubsystem>())
		{
			Pool->SpawnWorldItem(Item, Digger->GetActorLocation() + Digger->GetActorForwardVector() * 80.f);
		}
#endif
	}
	UE_LOG(LogVoxelCharacter, Log, TEXT("Gathered %d x %s from material %d"), Count, *Entry->ItemId.PrimaryAssetName.ToString(), MaterialId);
	Client_NotifyGathered(Def->DisplayName, Count);
#endif
}

void AVCPlayerController::Client_NotifyGathered_Implementation(const FText& ItemName, int32 Count)
{
	ShowToast(Count > 0 ? FText::Format(NSLOCTEXT("VCGather", "Gathered", "+{0} {1}"), Count, ItemName) : ItemName, 2.0f);
}

void AVCPlayerController::ToggleCraftingUI()
{
	if (bCampsiteOpen)
	{
		CloseCampsiteUI();
		return;
	}
	bCraftingOpen = !bCraftingOpen;
	if (bCraftingOpen)
	{
		if (!CraftingPanelWidget)
		{
			TSubclassOf<UUserWidget> ClassToUse = CraftingPanelWidgetClass ? CraftingPanelWidgetClass : TSubclassOf<UUserWidget>(UVCCraftingPanelWidget::StaticClass());
			CraftingPanelWidget = CreateWidget<UUserWidget>(this, ClassToUse);
		}
		if (CraftingPanelWidget)
		{
			if (!CraftingPanelWidget->IsInViewport())
			{
				CraftingPanelWidget->AddToViewport(2);
				CraftingPanelWidget->SetAnchorsInViewport(FAnchors(0.35f, 0.3f, 0.35f, 0.3f));
				CraftingPanelWidget->SetAlignmentInViewport(FVector2D(0.5f, 0.f));
			}
			if (UVCCraftingPanelWidget* Panel = Cast<UVCCraftingPanelWidget>(CraftingPanelWidget))
			{
				Panel->InitPanel(Cast<AVCCharacterBase>(GetPawn()), nullptr);
			}
		}
		SetUIInputMode(CraftingPanelWidget);
	}
	else
	{
		if (CraftingPanelWidget && CraftingPanelWidget->IsInViewport())
		{
			CraftingPanelWidget->RemoveFromParent();
		}
		if (!bInventoryOpen)
		{
			SetGameInputMode();
		}
	}
}

void AVCPlayerController::OpenCampsiteUI(AActor* RestPoint)
{
	if (!RestPoint)
	{
		return;
	}
	if (bCraftingOpen)
	{
		ToggleCraftingUI();
	}
	if (!CampsiteWidget)
	{
		TSubclassOf<UUserWidget> ClassToUse = CampsiteWidgetClass ? CampsiteWidgetClass : TSubclassOf<UUserWidget>(UVCCampsiteWidget::StaticClass());
		CampsiteWidget = CreateWidget<UUserWidget>(this, ClassToUse);
	}
	if (!CampsiteWidget)
	{
		return;
	}
	if (!CampsiteWidget->IsInViewport())
	{
		CampsiteWidget->AddToViewport(2);
		CampsiteWidget->SetAnchorsInViewport(FAnchors(0.35f, 0.25f, 0.35f, 0.25f));
		CampsiteWidget->SetAlignmentInViewport(FVector2D(0.5f, 0.f));
	}
	if (UVCCampsiteWidget* Panel = Cast<UVCCampsiteWidget>(CampsiteWidget))
	{
		Panel->InitPanel(Cast<AVCCharacterBase>(GetPawn()), RestPoint);
	}
	bCampsiteOpen = true;
	SetUIInputMode(CampsiteWidget);
}

void AVCPlayerController::CloseCampsiteUI()
{
	bCampsiteOpen = false;
	if (CampsiteWidget && CampsiteWidget->IsInViewport())
	{
		CampsiteWidget->RemoveFromParent();
	}
	if (!bInventoryOpen && !bCraftingOpen)
	{
		SetGameInputMode();
	}
}

void AVCPlayerController::Client_OpenCampsite_Implementation(AActor* RestPoint)
{
	OpenCampsiteUI(RestPoint);
}

void AVCPlayerController::Client_NotifyVoxelEditBlocked_Implementation(int32 RejectedVoxelCount)
{
	OnVoxelEditBlocked.Broadcast(RejectedVoxelCount);

	// Minimal built-in message until a game binds real UI to OnVoxelEditBlocked. Keyed so rapid
	// repeat blocks refresh one line instead of stacking.
	if (bShowEditBlockedMessage && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			/*Key=*/0x7C0EDB10, 1.5f, FColor::Orange, TEXT("This area is protected."));
	}
}
