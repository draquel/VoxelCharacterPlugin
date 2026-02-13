// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCPlayerController.h"
#include "Input/VCInputConfig.h"
#include "Movement/VCVoxelNavigationHelper.h"
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
#endif

#if WITH_INTERACTION_PLUGIN
#include "Subsystems/WorldItemPoolSubsystem.h"
#include "Actors/WorldItem.h"
#include "UI/InteractionPromptWidget.h"
#endif

#if WITH_EQUIPMENT_PLUGIN
#include "Components/EquipmentManagerComponent.h"
#include "UI/EquipmentPanelWidget.h"
#endif

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
}

void AVCPlayerController::HideInventoryPanels()
{
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

		EditMgr->BeginEditOperation(TEXT("Player dig"));
		EditMgr->ApplyBrushEdit(VoxelWorldPos, Brush, EEditMode::Subtract);
		EditMgr->EndEditOperation();

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
}
