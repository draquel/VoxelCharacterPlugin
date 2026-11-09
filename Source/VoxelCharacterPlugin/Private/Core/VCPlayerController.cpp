// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCPlayerController.h"
#include "Core/VCPlayerState.h"
#include "UI/VCVitalsWidget.h"
#include "UI/VCCarriedWeaponsWidget.h"
#include "UI/VCReticleWidget.h"
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
#include "VoxelTreeInjector.h"
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

void AVCPlayerController::UpdateRotation(float DeltaTime)
{
	const ACharacter* PlayerCharacter = Cast<ACharacter>(GetPawn());
	const UCharacterMovementComponent* Move = PlayerCharacter ? PlayerCharacter->GetCharacterMovement() : nullptr;
	if (!Move || !Move->HasCustomGravity())
	{
		// The default gravity: the engine's world-space view (heightmap worlds, and a planet's north pole).
		LastGravityDirection = Move ? Move->GetGravityDirection() : FVector::ZeroVector;
		if (Move)
		{
			bLevelViewInGravity = false;
		}
		Super::UpdateRotation(DeltaTime);
		return;
	}

	// Carry the view across the gravity's change since the last frame: it stays put relative to the ground.
	const FVector Gravity = Move->GetGravityDirection();
	FRotator ViewRotation = GetControlRotation();
	if (!LastGravityDirection.IsNearlyZero() && !LastGravityDirection.Equals(Gravity))
	{
		ViewRotation = (FQuat::FindBetweenNormals(LastGravityDirection, Gravity) * ViewRotation.Quaternion()).Rotator();
	}
	LastGravityDirection = Gravity;

	// The input and the pitch clamp relative to local up, no roll about it. Only these frame-independent operations are
	// done in the gravity frame (its own twist, which spins near the south pole, never shows).
	FRotator Relative = FVCGravityFrame::ToGravityRelative(Move, ViewRotation);
	if (bLevelViewInGravity)
	{
		// The first planet frame since a possession: the restart's world-level rotation, level along its heading.
		Relative.Pitch = 0.0;
		bLevelViewInGravity = false;
	}
	FRotator DeltaRot(RotationInput);
	if (PlayerCameraManager)
	{
		PlayerCameraManager->ProcessViewRotation(DeltaTime, Relative, DeltaRot);
	}
	else
	{
		Relative += DeltaRot;
	}
	Relative.Roll = 0.0;
	ViewRotation = FVCGravityFrame::ToWorld(Move, Relative);
	SetControlRotation(ViewRotation);

	if (APawn* const P = GetPawnOrSpectator())
	{
		P->FaceRotation(ViewRotation, DeltaTime);
	}
}

void AVCPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	bLevelViewInGravity = true;

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

void AVCPlayerController::AcknowledgePossession(APawn* P)
{
	Super::AcknowledgePossession(P);
	bLevelViewInGravity = true;
}

void AVCPlayerController::PawnLeavingGame()
{
	if (HasAuthority())
	{
		if (AVCPlayerState* PS = GetPlayerState<AVCPlayerState>())
		{
			PS->CaptureAvatarState();
		}
	}
	Super::PawnLeavingGame();
}

void AVCPlayerController::OnUnPossess()
{
	// A LOCAL player's controller unpossesses (never PawnLeavingGame) when the world ends: keep the
	// avatar's items / transform / vitals on the player state for the save (feature 9).
	if (HasAuthority())
	{
		if (AVCPlayerState* PS = GetPlayerState<AVCPlayerState>())
		{
			PS->CaptureAvatarState();
		}
	}

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

void AVCPlayerController::TogglePauseMenu()
{
	if (IsPauseMenuOpen())
	{
		ResumeFromPauseMenu();
		return;
	}
	if (!PauseMenuWidget)
	{
		TSubclassOf<UUserWidget> ClassToUse = PauseMenuWidgetClass ? PauseMenuWidgetClass : TSubclassOf<UUserWidget>(UVCPauseMenuWidget::StaticClass());
		PauseMenuWidget = CreateWidget<UUserWidget>(this, ClassToUse);
	}
	if (!PauseMenuWidget)
	{
		return;
	}
	PauseMenuWidget->AddToViewport(50);
	PauseMenuWidget->SetAnchorsInViewport(FAnchors(0.5f, 0.4f, 0.5f, 0.4f));
	PauseMenuWidget->SetAlignmentInViewport(FVector2D(0.5f, 0.5f));

	// Pause only where nobody else is playing (Esc stops PIE in the editor, so the key is P).
	const ENetMode Mode = GetNetMode();
	const UWorld* World = GetWorld();
	const bool bAlone = Mode == NM_Standalone || (Mode == NM_ListenServer && World && World->GetNumPlayerControllers() == 1);
	bPausedByMenu = bAlone && SetPause(true);
	if (UVCPauseMenuWidget* Menu = Cast<UVCPauseMenuWidget>(PauseMenuWidget))
	{
		Menu->SetPausedState(bPausedByMenu);
	}
	SetUIInputMode(PauseMenuWidget);
	UE_LOG(LogVoxelCharacter, Log, TEXT("Pause menu opened (%s)."), bPausedByMenu ? TEXT("game paused") : TEXT("game keeps running"));
}

void AVCPlayerController::ResumeFromPauseMenu()
{
	if (PauseMenuWidget && PauseMenuWidget->IsInViewport())
	{
		PauseMenuWidget->RemoveFromParent();
	}
	if (bPausedByMenu)
	{
		SetPause(false);
		bPausedByMenu = false;
	}
	if (bInventoryOpen || bWorldMapOpen || bCraftingOpen || bCampsiteOpen)
	{
		SetUIInputMode();
	}
	else
	{
		SetGameInputMode();
	}
}

bool AVCPlayerController::IsPauseMenuOpen() const
{
	return PauseMenuWidget && PauseMenuWidget->IsInViewport();
}

void AVCPlayerController::RequestSaveGame()
{
	if (HasAuthority())
	{
		if (AVCGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AVCGameModeBase>() : nullptr)
		{
			GameMode->RequestSave(this);
		}
		return;
	}
	Server_RequestSaveGame();
}

void AVCPlayerController::Server_RequestSaveGame_Implementation()
{
	RequestSaveGame();
}

void AVCPlayerController::QuitFromPauseMenu()
{
	if (bPausedByMenu)
	{
		SetPause(false);
		bPausedByMenu = false;
	}
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
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
	if (UVCCarriedWeaponsWidget* Carried = Cast<UVCCarriedWeaponsWidget>(CarriedWeaponsWidget))
	{
		Carried->InitWithCharacter(Cast<AVCCharacterBase>(InPawn));
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

	// The weapons carried on the body, just above the hotbar (display only, Z-order 0)
#if WITH_EQUIPMENT_PLUGIN
	{
		CarriedWeaponsWidget = CreateWidget<UUserWidget>(this, UVCCarriedWeaponsWidget::StaticClass());
		if (CarriedWeaponsWidget)
		{
			CarriedWeaponsWidget->AddToViewport(0);
			CarriedWeaponsWidget->SetAnchorsInViewport(FAnchors(0.5f, 0.86f, 0.5f, 0.86f));
			CarriedWeaponsWidget->SetAlignmentInViewport(FVector2D(0.5f, 1.f));
			CarriedWeaponsWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
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

	// The aim reticle (centre, Z-order 1; C3): it shows itself while the pawn aims.
	{
		TSubclassOf<UUserWidget> ClassToUse = ReticleWidgetClass;
		if (!ClassToUse)
		{
			ClassToUse = UVCReticleWidget::StaticClass();
		}
		ReticleWidget = CreateWidget<UUserWidget>(this, ClassToUse);
		if (ReticleWidget)
		{
			ReticleWidget->AddToViewport(1);
			// The size first: SetDesiredSizeInViewport after the anchors left them at the top-left corner (the dot drew there).
			ReticleWidget->SetDesiredSizeInViewport(FVector2D(64.f, 64.f));
			ReticleWidget->SetAnchorsInViewport(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
			ReticleWidget->SetAlignmentInViewport(FVector2D(0.5f, 0.5f));
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
// Debug Commands
// ---------------------------------------------------------------------------

void AVCPlayerController::GiveItem(FString AssetName, int32 Count)
{
#if !UE_BUILD_SHIPPING
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

	// The inventory on the possessed pawn (judged after the item is resolved and made, as the messages below are ordered).
	UInventoryComponent* Inventory = nullptr;
	if (APawn* ControlledPawn = GetPawn())
	{
		Inventory = ControlledPawn->FindComponentByClass<UInventoryComponent>();
	}

	// The one give (code review CR2, FW-37): the asset name by substring match against all registered definitions.
	FPrimaryAssetId FoundId;
	const EInventoryOperationResult Result = ItemDB->GiveItemByName(Inventory, AssetName, Count, /*bMatchSubstring=*/true, FoundId);
	if (Result == EInventoryOperationResult::ItemNotFound)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("GiveItem: No item found matching '%s'. Available items:"), *AssetName);
		for (const FPrimaryAssetId& Id : ItemDB->GetAllDefinitionIds())
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("  - %s"), *Id.PrimaryAssetName.ToString());
		}
		return;
	}
	if (Result == EInventoryOperationResult::InvalidItem)
	{
		UE_LOG(LogVoxelCharacter, Error, TEXT("GiveItem: Failed to create item instance for '%s'"), *FoundId.ToString());
		return;
	}
	if (!Inventory)
	{
		UE_LOG(LogVoxelCharacter, Error, TEXT("GiveItem: No InventoryComponent found on possessed pawn"));
		return;
	}

	UE_LOG(LogVoxelCharacter, Log, TEXT("GiveItem: %s x%d -> %s"),
		*FoundId.PrimaryAssetName.ToString(), Count,
		Result == EInventoryOperationResult::Success ? TEXT("Success") : TEXT("Failed"));
#else
	UE_LOG(LogVoxelCharacter, Warning, TEXT("GiveItem: ItemInventoryPlugin not enabled"));
#endif
#endif // !UE_BUILD_SHIPPING
}

void AVCPlayerController::SpawnWorldItem(FString AssetName, int32 Count)
{
#if !UE_BUILD_SHIPPING
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
#endif // !UE_BUILD_SHIPPING
}

