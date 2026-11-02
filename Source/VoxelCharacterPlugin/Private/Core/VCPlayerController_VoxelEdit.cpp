// Copyright Daniel Raquel. All Rights Reserved.

// AVCPlayerController's voxel edit server path (a file of the class, code review CR4).

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

// ---------------------------------------------------------------------------
// Server RPC — Voxel Modification
// ---------------------------------------------------------------------------

void AVCPlayerController::Server_RequestVoxelModification_Implementation(const FIntVector& VoxelCoord, EVCVoxelModificationType ModType, uint8 MaterialID)
{
	// --- Validation ---
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("Server_RequestVoxelModification: No pawn"));
		return;
	}

	UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld());
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
	const FVector VoxelWorldPos = VoxelCornerWorld(Config, VoxelCoord);

	// Distance check: reject modifications beyond the edit reach (the block feedback shows nothing beyond it either)
	if (!IsVoxelInEditReach(ControlledPawn->GetActorLocation(), VoxelCoord, Config))
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("Server_RequestVoxelModification: Out of range (%.0f > %.0f)"),
			FVector::Dist(ControlledPawn->GetActorLocation(), VoxelWorldPos), EditReach);
		return;
	}

	if (ModType == EVCVoxelModificationType::Destroy)
	{
		// A cubic world with break times breaks a block only through the timed path (UVCBlockInteractionComponent,
		// WM-C15), which waits the material's time on this server; an instant request would skip it.
		if (UVCBlockInteractionComponent::UsesTimedBreaks(Config))
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("Server_RequestVoxelModification: refused a dig at [%d,%d,%d]: blocks here break by holding ")
				TEXT("the primary action (Server_BeginBlockBreak)"), VoxelCoord.X, VoxelCoord.Y, VoxelCoord.Z);
			return;
		}
		DigVoxelAt(VoxelCoord);
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

	// Cubic worlds edit whole blocks (world modes review WM-C2, KC5): a place fills exactly the aimed voxel, a
	// full-strength cube brush around that one voxel (density to 255), so one click is one block (a dig: DigVoxelAt).
	// Smooth worlds keep the density brushes that build the surface gradually.
	const bool bBlockEdit = Config->MeshingMode == EMeshingMode::Cubic;
	const FVector BlockCentre = BlockCentreWorld(Config, VoxelCoord);

	switch (ModType)
	{
	case EVCVoxelModificationType::Destroy:
		break; // DigVoxelAt, above

	case EVCVoxelModificationType::Place:
	{
		// Reject a placement that would close terrain around any pawn (this player, other players, NPCs); the
		// placement preview shows the same test in red.
		if (WouldPlaceTrapPawn(GetWorld(), Config, VoxelCoord))
		{
			UE_LOG(LogVoxelCharacter, Verbose,
				TEXT("Server_RequestVoxelModification: Rejected place at [%d,%d,%d] — overlaps a pawn"),
				VoxelCoord.X, VoxelCoord.Y, VoxelCoord.Z);
			Client_ShowToast(NSLOCTEXT("VoxelCharacter", "PlaceTooClose", "Too close to place a block"), 1.5f);
			return;
		}

		if (bBlockEdit)
		{
			Brush = MakeBlockBrush(Config);
		}
		else
		{
			Brush.Radius = Config->VoxelSize * 0.8f;
			Brush.Strength = 1.f;
			Brush.FalloffType = EVoxelBrushFalloff::Sharp;
			Brush.DensityDelta = 80;
		}
		Brush.MaterialID = MaterialID;

		EditMgr->BeginEditOperation(TEXT("Player place"));
		EditMgr->ApplyBrushEdit(bBlockEdit ? BlockCentre : VoxelWorldPos, Brush, EEditMode::Add);
		EditMgr->EndEditOperation();

		UE_LOG(LogVoxelCharacter, Verbose, TEXT("Voxel placed at [%d,%d,%d] Material=%d"),
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

FVector AVCPlayerController::VoxelCornerWorld(const UVoxelWorldConfiguration* Config, const FIntVector& VoxelCoord)
{
	return FVector(VoxelCoord) * Config->VoxelSize + Config->WorldOrigin;
}

FVector AVCPlayerController::BlockCentreWorld(const UVoxelWorldConfiguration* Config, const FIntVector& VoxelCoord)
{
	// A cubic voxel's block spans [corner, corner + VoxelSize].
	return VoxelCornerWorld(Config, VoxelCoord) + FVector(Config->VoxelSize * 0.5f);
}

FVoxelBrushParams AVCPlayerController::MakeBlockBrush(const UVoxelWorldConfiguration* Config)
{
	FVoxelBrushParams Brush;
	Brush.Shape = EVoxelBrushShape::Cube;
	Brush.Radius = Config->VoxelSize * 0.4f; // under half a voxel: the cube reaches no neighbour
	Brush.Strength = 1.f;
	Brush.FalloffType = EVoxelBrushFalloff::Sharp;
	Brush.DensityDelta = 255;
	return Brush;
}

bool AVCPlayerController::IsVoxelInEditReach(const FVector& PawnLocation, const FIntVector& VoxelCoord, const UVoxelWorldConfiguration* Config)
{
	if (!Config)
	{
		return false;
	}
	const FVector VoxelWorldPos = VoxelCornerWorld(Config, VoxelCoord);
	return FVector::Dist(PawnLocation, VoxelWorldPos) <= EditReach;
}

bool AVCPlayerController::WouldPlaceTrapPawn(const UWorld* World, const UVoxelWorldConfiguration* Config, const FIntVector& VoxelCoord)
{
	if (!World || !Config)
	{
		return false;
	}
	// Smooth: the place brush fills the single voxel SAMPLE at VoxelWorldPos, and the smooth mesher puts the new surface
	// about half a voxel around that sample -- so the solid cube is centred on the sample, not spanning
	// [sample, sample + VoxelSize] (an older test was shifted half a voxel and ignored NPCs).
	// Cubic: the new block is exactly the voxel's cell, [sample, sample + VoxelSize]; a pawn only touching one of its
	// faces is not inside it.
	const bool bBlockEdit = Config->MeshingMode == EMeshingMode::Cubic;
	const FVector VoxelWorldPos = VoxelCornerWorld(Config, VoxelCoord);
	const FVector BoxCentre = bBlockEdit ? BlockCentreWorld(Config, VoxelCoord) : VoxelWorldPos;
	const float HalfExtent = bBlockEdit ? Config->VoxelSize * 0.5f - 2.f : Config->VoxelSize * 0.5f + 10.f;
	const FCollisionObjectQueryParams PawnObjects(ECC_Pawn);
	return World->OverlapAnyTestByObjectType(BoxCentre, FQuat::Identity, PawnObjects, FCollisionShape::MakeBox(FVector(HalfExtent)));
}

bool AVCPlayerController::DigVoxelAt(const FIntVector& VoxelCoord)
{
	APawn* ControlledPawn = GetPawn();
	UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld());
	const UVoxelWorldConfiguration* Config = ChunkMgr && ChunkMgr->IsInitialized() ? ChunkMgr->GetConfiguration() : nullptr;
	UVoxelEditManager* EditMgr = ChunkMgr ? ChunkMgr->GetEditManager() : nullptr;
	if (!HasAuthority() || !ControlledPawn || !Config || !EditMgr)
	{
		return false;
	}
	if (!IsVoxelInEditReach(ControlledPawn->GetActorLocation(), VoxelCoord, Config))
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("DigVoxelAt: [%d,%d,%d] out of reach"), VoxelCoord.X, VoxelCoord.Y, VoxelCoord.Z);
		return false;
	}

	// Set edit source to Player so scatter is permanently removed
	EditMgr->SetEditSource(EEditSource::Player);

	const FVector VoxelWorldPos = VoxelCornerWorld(Config, VoxelCoord);
	// Cubic worlds edit whole blocks (world modes review WM-C2, KC5): a dig empties exactly the aimed voxel, a
	// full-strength cube brush around that one voxel (density to 0), so one dig is one block. Smooth worlds keep the
	// density brush that dents the surface gradually.
	const bool bBlockEdit = Config->MeshingMode == EMeshingMode::Cubic;
	const FVector BlockCentre = BlockCentreWorld(Config, VoxelCoord);
	FVoxelBrushParams Brush;
	if (bBlockEdit)
	{
		Brush = MakeBlockBrush(Config);
	}
	else
	{
		Brush.Shape = EVoxelBrushShape::Sphere;
		Brush.Radius = Config->VoxelSize * 1.5f;
		Brush.Strength = 1.f;
		Brush.FalloffType = EVoxelBrushFalloff::Smooth;
		Brush.DensityDelta = 80;
	}

	// Gathering (feature 8) pays for a voxel dug out, not for a swing: read every voxel the brush can
	// reach before and after it runs, and pay once when the dig turned a solid voxel to air. A dug voxel
	// keeps its material id, so paying whenever the brush changed anything let one voxel yield forever (G3).
	// Sampled at voxel centres, so the position-to-index floor lands on the voxel the brush edits.
	TArray<FVector> Reachable;
	TArray<FVoxelData> Before;
	TArray<float> DistSq;
	const int32 Reach = FMath::CeilToInt(Brush.Radius / Config->VoxelSize);
	for (int32 DZ = -Reach; DZ <= Reach; ++DZ)
	{
		for (int32 DY = -Reach; DY <= Reach; ++DY)
		{
			for (int32 DX = -Reach; DX <= Reach; ++DX)
			{
				const FVector Offset = FVector(DX, DY, DZ) * Config->VoxelSize;
				if (Offset.SizeSquared() > FMath::Square(Brush.Radius))
				{
					continue;
				}
				Reachable.Add(VoxelWorldPos + Offset + FVector(Config->VoxelSize * 0.5f));
				Before.Add(ChunkMgr->GetVoxelAt(Reachable.Last()));
				DistSq.Add(static_cast<float>(Offset.SizeSquared()));
			}
		}
	}

	EditMgr->BeginEditOperation(TEXT("Player dig"));
	const int32 Modified = EditMgr->ApplyBrushEdit(bBlockEdit ? BlockCentre : VoxelWorldPos, Brush, EEditMode::Subtract);
	EditMgr->EndEditOperation();
	int32 Dug = INDEX_NONE;
	if (Modified > 0)
	{
		TArray<FVoxelData> After;
		After.Reserve(Reachable.Num());
		for (const FVector& Sample : Reachable)
		{
			After.Add(ChunkMgr->GetVoxelAt(Sample));
		}
		Dug = UVCGatherTable::FindDugVoxel(Before, After, DistSq);
		if (Dug != INDEX_NONE)
		{
			GatherFromVoxel(Before[Dug].MaterialID);
			// This machine's own player sees the hole now: keep the placement ghost out of it until the aim moves (G75).
			// A remote client is told by its timed break's end (Client_BlockBreakFinished).
			const AVCCharacterBase* Digger = Cast<AVCCharacterBase>(ControlledPawn);
			if (Digger && Digger->IsLocallyControlled() && Digger->BlockInteraction)
			{
				Digger->BlockInteraction->NoteDug(VoxelCoord);
			}
		}
	}

	UE_LOG(LogVoxelCharacter, Verbose, TEXT("Voxel destroyed at [%d,%d,%d]"),
		VoxelCoord.X, VoxelCoord.Y, VoxelCoord.Z);

	// Edit-protection feedback: the edit manager counts voxels a validator vetoed during the
	// apply above (e.g. protected POI/dungeon claims). Tell the owning client so its UI can react.
	const int32 RejectedVoxels = EditMgr->GetLastRejectedEditCount();
	if (RejectedVoxels > 0)
	{
		Client_NotifyVoxelEditBlocked(RejectedVoxels);
	}
	return Dug != INDEX_NONE;
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
	UE_LOG(LogVoxelCharacter, Log, TEXT("Gathered %d x %s from material %d"), Count, *Entry->ItemId.PrimaryAssetName.ToString(), MaterialId);
	OnVoxelGathered.Broadcast(MaterialId, Entry->ItemId, Count);
	GiveGatheredItem(Entry->ItemId, Count);
#endif
}

void AVCPlayerController::GiveGatheredItem(FPrimaryAssetId ItemId, int32 Count)
{
#if WITH_INVENTORY_PLUGIN
	APawn* Gatherer = GetPawn();
	if (!Gatherer || !ItemId.IsValid() || Count <= 0)
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UItemDatabaseSubsystem* ItemDB = GameInstance ? GameInstance->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	UInventoryComponent* Inventory = Gatherer->FindComponentByClass<UInventoryComponent>();
	const UItemDefinition* Def = ItemDB ? ItemDB->GetDefinition(ItemId) : nullptr;
	if (!Def || !Inventory)
	{
		return;
	}
	FItemInstance Item = ItemDB->CreateItemInstance(ItemId, Count);
	if (Inventory->TryAddItem(Item) != EInventoryOperationResult::Success)
	{
#if WITH_INTERACTION_PLUGIN
		// Full: it lands at the player's feet as a pickup instead of vanishing.
		if (UWorldItemPoolSubsystem* Pool = GetWorld()->GetSubsystem<UWorldItemPoolSubsystem>())
		{
			Pool->SpawnWorldItem(Item, Gatherer->GetActorLocation() + Gatherer->GetActorForwardVector() * 80.f);
		}
#endif
	}
	Client_NotifyGathered(Def->DisplayName, Count);
#endif
}

bool AVCPlayerController::ChopScatterAt(const FVector& AimPoint)
{
	AVCCharacterBase* Chopper = Cast<AVCCharacterBase>(GetPawn());
	UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld());
	UVoxelScatterManager* Scatter = ChunkMgr ? ChunkMgr->GetScatterManager() : nullptr;
	if (!HasAuthority() || !Chopper || !Scatter || !GatherTable)
	{
		return false;
	}
	FScatterHarvestResult Found;
	if (!Scatter->HarvestScatterNear(AimPoint, ChopSearchRadius, /*bRemove=*/false, Found))
	{
		return false;
	}
	const FVCHarvestEntry* Entry = GatherTable->FindHarvest(Found.HarvestCategory);
	if (!Entry)
	{
		return false;
	}
	const FVector Base = Found.InstanceTransform.GetLocation();
	const FIntVector Key(FMath::RoundToInt(Base.X), FMath::RoundToInt(Base.Y), FMath::RoundToInt(Base.Z));
	int32& Hits = ChopHits.FindOrAdd(Key);
	++Hits;
	const int32 Count = UVCGatherTable::HarvestYieldFor(*Entry, Chopper->GetMiningSpeed(), GatherTable->ToolMiningSpeedThreshold);
	UE_LOG(LogVoxelCharacter, Log, TEXT("Chop %d/%d on '%s' (%s) at %s: +%d %s"), Hits, Entry->HitsToRemove, *Found.Name,
		*Found.HarvestCategory.ToString(), *Base.ToCompactString(), Count, *Entry->ItemId.PrimaryAssetName.ToString());
	GiveGatheredItem(Entry->ItemId, Count);
	if (Hits >= Entry->HitsToRemove)
	{
		ChopHits.Remove(Key);
		Multicast_ScatterHarvested(Base);
	}
	return true;
}

void AVCPlayerController::Multicast_ScatterHarvested_Implementation(const FVector& Location)
{
	UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld());
	UVoxelScatterManager* Scatter = ChunkMgr ? ChunkMgr->GetScatterManager() : nullptr;
	FScatterHarvestResult Removed;
	if (Scatter)
	{
		// Scatter is local to each machine: each one removes its own copy of the instance.
		Scatter->HarvestScatterNear(Location, 60.f, /*bRemove=*/true, Removed);
	}
}

bool AVCPlayerController::IsVoxelTreeStanding(const UVoxelChunkManager* ChunkMgr, const FVoxelTreeInstance& Tree)
{
	if (!ChunkMgr || !Tree.IsValid())
	{
		return false;
	}
	TArray<FVoxelTreeVoxel> Blocks;
	ChunkMgr->GetVoxelTreeBlocks(Tree, Blocks);
	return Blocks.ContainsByPredicate([&Tree](const FVoxelTreeVoxel& Block) { return Block.bTrunk && Block.GlobalVoxel.Z > Tree.BaseVoxel.Z; });
}

bool AVCPlayerController::ChopVoxelTreeAt(const FIntVector& VoxelCoord)
{
	AVCCharacterBase* Chopper = Cast<AVCCharacterBase>(GetPawn());
	UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld());
	const UVoxelWorldConfiguration* Config = ChunkMgr ? ChunkMgr->GetConfiguration() : nullptr;
	UVoxelEditManager* EditMgr = ChunkMgr ? ChunkMgr->GetEditManager() : nullptr;
	if (!HasAuthority() || !Chopper || !Config || !EditMgr || !GatherTable || !ChunkMgr->HasVoxelTrees())
	{
		return false;
	}
	// The same reach as digging (Server_RequestVoxelModification).
	const FVector BlockCentre = ChunkMgr->GlobalVoxelToWorldCentre(VoxelCoord);
	constexpr float MaxChopRange = 800.f;
	if (FVector::Dist(Chopper->GetActorLocation(), BlockCentre) > MaxChopRange)
	{
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("ChopVoxelTreeAt: out of range (%.0f > %.0f)"), FVector::Dist(Chopper->GetActorLocation(), BlockCentre), MaxChopRange);
		return false;
	}
	FVoxelTreeInstance Tree;
	if (!ChunkMgr->FindVoxelTreeAt(BlockCentre, Tree) || !IsVoxelTreeStanding(ChunkMgr, Tree))
	{
		return false;
	}
	const FVCHarvestEntry* Entry = GatherTable->FindHarvest(VoxelTreeHarvestCategory);
	if (!Entry)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("ChopVoxelTreeAt: the gather table has no '%s' harvest entry."), *VoxelTreeHarvestCategory.ToString());
		return false;
	}
	int32& Hits = VoxelTreeChopHits.FindOrAdd(Tree.BaseVoxel);
	++Hits;
	const int32 Count = UVCGatherTable::HarvestYieldFor(*Entry, Chopper->GetMiningSpeed(), GatherTable->ToolMiningSpeedThreshold);
	UE_LOG(LogVoxelCharacter, Log, TEXT("Chop %d/%d on the voxel tree at %s (template %d): +%d %s"), Hits, Entry->HitsToRemove,
		*Tree.BaseVoxel.ToString(), Tree.TemplateIndex, Count, *Entry->ItemId.PrimaryAssetName.ToString());
	GiveGatheredItem(Entry->ItemId, Count);
	if (Hits >= Entry->HitsToRemove)
	{
		VoxelTreeChopHits.Remove(Tree.BaseVoxel);
		// A player edit, so scatter treats the cleared ground as the player's (as a dig is), and the edit layer saves it.
		EditMgr->SetEditSource(EEditSource::Player);
		const int32 Removed = ChunkMgr->FellVoxelTree(Tree, /*bKeepStump=*/true);
		UE_LOG(LogVoxelCharacter, Log, TEXT("Felled the voxel tree at %s: %d blocks removed, stump kept."), *Tree.BaseVoxel.ToString(), Removed);
	}
	return true;
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

void AVCPlayerController::Client_ShowToast_Implementation(const FText& Text, float Duration)
{
	ShowToast(Text, Duration);
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
