// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCPlayerController.h"
#include "Input/VCInputConfig.h"
#include "Movement/VCVoxelNavigationHelper.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "Blueprint/UserWidget.h"
#include "VoxelChunkManager.h"
#include "VoxelEditManager.h"
#include "VoxelEditTypes.h"
#include "VoxelWorldConfiguration.h"
#include "VoxelCoordinates.h"
#include "VoxelCharacterPlugin.h"

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
