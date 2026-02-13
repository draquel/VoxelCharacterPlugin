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
#endif

#if WITH_INTERACTION_PLUGIN
#include "Subsystems/WorldItemPoolSubsystem.h"
#include "Actors/WorldItem.h"
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
}

void AVCPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Add the default gameplay mapping context
	if (InputConfig && InputConfig->IMC_Gameplay)
	{
		AddInputMappingContext(InputConfig->IMC_Gameplay, 0);
	}
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
	if (bIsInUIMode)
	{
		SetGameInputMode();
	}
	else
	{
		SetUIInputMode();
	}
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("ToggleInventoryUI: %s"), bIsInUIMode ? TEXT("UI") : TEXT("Game"));
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
