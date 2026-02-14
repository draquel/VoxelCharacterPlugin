// Copyright Daniel Raquel. All Rights Reserved.

#include "Map/VCWorldMapWidget.h"
#include "VoxelMapSubsystem.h"
#include "VoxelCharacterPlugin.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

// ---------------------------------------------------------------------------
// Widget Tree Construction
// ---------------------------------------------------------------------------

void UVCWorldMapWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UVCWorldMapWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	// Root canvas (full screen)
	RootCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("WorldMapCanvas"));
	WidgetTree->RootWidget = RootCanvas;

	// Dark background fill
	MapBackground = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MapBackground"));
	MapBackground->SetColorAndOpacity(FLinearColor(0.02f, 0.02f, 0.05f, 0.92f));
	UCanvasPanelSlot* BgSlot = RootCanvas->AddChildToCanvas(MapBackground);
	if (BgSlot)
	{
		BgSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		BgSlot->SetOffsets(FMargin(0.0f));
	}

	// Map terrain image (centered, fixed display size from texture)
	MapImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MapTerrainImage"));
	MapImage->SetColorAndOpacity(FLinearColor::White);
	UCanvasPanelSlot* MapSlot = RootCanvas->AddChildToCanvas(MapImage);
	if (MapSlot)
	{
		MapSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		MapSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		MapSlot->SetAutoSize(true);
	}

	// Player marker (small colored dot)
	PlayerMarker = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PlayerMarker"));
	PlayerMarker->SetColorAndOpacity(FLinearColor(1.0f, 0.2f, 0.2f, 1.0f));
	PlayerMarker->SetDesiredSizeOverride(FVector2D(12.0f, 12.0f));
	UCanvasPanelSlot* MarkerSlot = RootCanvas->AddChildToCanvas(PlayerMarker);
	if (MarkerSlot)
	{
		MarkerSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		MarkerSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		MarkerSlot->SetAutoSize(true);
	}

	// Coordinate text at bottom
	MapCoordinateText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("MapCoordText"));
	MapCoordinateText->SetText(FText::GetEmpty());
	FSlateFontInfo Font = MapCoordinateText->GetFont();
	Font.Size = 14;
	MapCoordinateText->SetFont(Font);
	MapCoordinateText->SetColorAndOpacity(FSlateColor(FLinearColor(0.9f, 0.9f, 0.9f, 0.9f)));
	MapCoordinateText->SetJustification(ETextJustify::Center);
	UCanvasPanelSlot* TextSlot = RootCanvas->AddChildToCanvas(MapCoordinateText);
	if (TextSlot)
	{
		TextSlot->SetAnchors(FAnchors(0.5f, 0.95f, 0.5f, 0.95f));
		TextSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		TextSlot->SetAutoSize(true);
	}
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void UVCWorldMapWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Resolve subsystem
	if (const UWorld* World = GetWorld())
	{
		MapSubsystem = World->GetSubsystem<UVoxelMapSubsystem>();
	}

	// Bind to tile ready events so we can refresh while map is open
	if (MapSubsystem.IsValid() && !TileReadyHandle.IsValid())
	{
		TileReadyHandle = MapSubsystem->OnMapTileReady.AddLambda([this](FIntPoint)
		{
			bMapDirty = true;
		});
	}

	// Center on player
	if (const APlayerController* PC = GetOwningPlayer())
	{
		if (const APawn* PlayerPawn = PC->GetPawn())
		{
			PanOffset = FVector2D(PlayerPawn->GetActorLocation().X, PlayerPawn->GetActorLocation().Y);
		}
	}

	bMapDirty = true;
}

void UVCWorldMapWidget::NativeDestruct()
{
	// Unbind delegate
	if (MapSubsystem.IsValid() && TileReadyHandle.IsValid())
	{
		MapSubsystem->OnMapTileReady.Remove(TileReadyHandle);
		TileReadyHandle.Reset();
	}

	Super::NativeDestruct();
}

void UVCWorldMapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (bMapDirty)
	{
		RebuildMapTexture();
		bMapDirty = false;
	}

	UpdatePlayerMarker();
}

// ---------------------------------------------------------------------------
// Input — Zoom / Pan
// ---------------------------------------------------------------------------

FReply UVCWorldMapWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const float Delta = InMouseEvent.GetWheelDelta();
	const float ZoomFactor = (Delta > 0) ? 1.25f : 0.8f;
	CurrentZoom = FMath::Clamp(CurrentZoom * ZoomFactor, MinZoom, MaxZoom);
	bMapDirty = true;
	return FReply::Handled();
}

FReply UVCWorldMapWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bIsPanning = true;
		LastMousePos = InMouseEvent.GetScreenSpacePosition();
		return FReply::Handled().CaptureMouse(TakeWidget());
	}
	return FReply::Unhandled();
}

FReply UVCWorldMapWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && bIsPanning)
	{
		bIsPanning = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply UVCWorldMapWidget::NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsPanning && RenderedWorldPerPixel > 0.f)
	{
		const FVector2D CurrentMousePos = InMouseEvent.GetScreenSpacePosition();
		const FVector2D Delta = CurrentMousePos - LastMousePos;
		LastMousePos = CurrentMousePos;

		// Convert screen pixel delta to world delta
		PanOffset.X -= Delta.X * RenderedWorldPerPixel;
		PanOffset.Y -= Delta.Y * RenderedWorldPerPixel;
		bMapDirty = true;

		return FReply::Handled();
	}
	return FReply::Unhandled();
}

// ---------------------------------------------------------------------------
// Map Texture
// ---------------------------------------------------------------------------

void UVCWorldMapWidget::RefreshMap()
{
	bMapDirty = true;
}

void UVCWorldMapWidget::RebuildMapTexture()
{
	UVoxelMapSubsystem* Subsystem = MapSubsystem.Get();
	if (!Subsystem || !MapImage)
	{
		return;
	}

	const float TileWorldSize = Subsystem->GetTileWorldSize();
	const int32 TileResolution = Subsystem->GetTileResolution();
	if (TileWorldSize <= 0.f || TileResolution <= 0)
	{
		return;
	}

	const int32 TexSize = FMath::Clamp(MapTextureFixedSize, 256, 2048);

	// How many world units does the entire texture cover?
	// At zoom=1, we show a "base" world radius. Zooming in shrinks the world area.
	// Base: the texture covers ~32 tiles per side (reasonable default view).
	const float BaseWorldExtent = 32.0f * TileWorldSize;  // world units from center to edge
	const float ViewWorldExtent = BaseWorldExtent / CurrentZoom;  // shrinks as zoom increases

	// World units per pixel
	RenderedWorldPerPixel = (ViewWorldExtent * 2.0f) / static_cast<float>(TexSize);

	// How many tiles from center to edge
	RenderedTileRadius = FMath::CeilToInt(ViewWorldExtent / TileWorldSize);
	RenderedCenterTile = Subsystem->WorldToTileCoord(FVector(PanOffset.X, PanOffset.Y, 0.0f));
	RenderedTexSize = TexSize;

	// Create or recreate texture
	if (!WorldMapTexture || WorldMapTexture->GetSizeX() != TexSize)
	{
		WorldMapTexture = UTexture2D::CreateTransient(TexSize, TexSize, PF_B8G8R8A8, TEXT("WorldMapTexture"));
		if (!WorldMapTexture)
		{
			return;
		}
		WorldMapTexture->Filter = TF_Bilinear;
		WorldMapTexture->SRGB = true;
		WorldMapTexture->CompressionSettings = TC_VectorDisplacementmap;
		WorldMapTexture->AddressX = TA_Clamp;
		WorldMapTexture->AddressY = TA_Clamp;
	}

	// Lock texture
	FTexture2DMipMap& Mip = WorldMapTexture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	if (!TextureData)
	{
		return;
	}

	uint8* PixelData = static_cast<uint8*>(TextureData);
	const int32 TotalPixels = TexSize * TexSize;

	// Clear to fog of war
	FMemory::Memset(PixelData, 0, TotalPixels * 4);
	for (int32 i = 0; i < TotalPixels; ++i)
	{
		PixelData[i * 4 + 0] = 10;  // B
		PixelData[i * 4 + 1] = 10;  // G
		PixelData[i * 4 + 2] = 10;  // R
		PixelData[i * 4 + 3] = 255; // A
	}

	// The view center in world space
	const float ViewCenterX = PanOffset.X;
	const float ViewCenterY = PanOffset.Y;

	// World bounds of the texture
	const float WorldMinX = ViewCenterX - ViewWorldExtent;
	const float WorldMinY = ViewCenterY - ViewWorldExtent;

	// Iterate visible tiles and blit them into the texture
	for (int32 TY = -RenderedTileRadius; TY <= RenderedTileRadius; ++TY)
	{
		for (int32 TX = -RenderedTileRadius; TX <= RenderedTileRadius; ++TX)
		{
			const FIntPoint TileCoord(RenderedCenterTile.X + TX, RenderedCenterTile.Y + TY);

			// Fog of war: skip unexplored
			const bool bExplored = Subsystem->IsTileExplored(TileCoord);
			const FVoxelMapTile* Tile = bExplored ? Subsystem->GetTile(TileCoord) : nullptr;

			if (!bExplored)
			{
				continue;
			}

			// World origin of this tile
			const FVector TileWorldOrigin = Subsystem->TileCoordToWorld(TileCoord);

			// For each pixel in the tile source, compute where it lands in the texture
			const int32 SrcRes = Tile ? Tile->Resolution : TileResolution;
			const float PixelWorldSize = TileWorldSize / SrcRes;

			for (int32 PY = 0; PY < SrcRes; ++PY)
			{
				for (int32 PX = 0; PX < SrcRes; ++PX)
				{
					const float PixWorldX = TileWorldOrigin.X + PX * PixelWorldSize;
					const float PixWorldY = TileWorldOrigin.Y + PY * PixelWorldSize;

					// Map to texture pixel
					const int32 DstX = FMath::FloorToInt((PixWorldX - WorldMinX) / RenderedWorldPerPixel);
					const int32 DstY = FMath::FloorToInt((PixWorldY - WorldMinY) / RenderedWorldPerPixel);

					if (DstX < 0 || DstX >= TexSize || DstY < 0 || DstY >= TexSize)
					{
						continue;
					}

					const int32 DstIdx = (DstY * TexSize + DstX) * 4;

					if (Tile)
					{
						const FColor& SrcColor = Tile->PixelData[PY * SrcRes + PX];
						PixelData[DstIdx + 0] = SrcColor.B;
						PixelData[DstIdx + 1] = SrcColor.G;
						PixelData[DstIdx + 2] = SrcColor.R;
						PixelData[DstIdx + 3] = SrcColor.A;
					}
					else
					{
						// Explored but not generated — slightly lighter fog
						PixelData[DstIdx + 0] = 25;
						PixelData[DstIdx + 1] = 25;
						PixelData[DstIdx + 2] = 25;
						PixelData[DstIdx + 3] = 255;
					}
				}
			}
		}
	}

	Mip.BulkData.Unlock();
	WorldMapTexture->UpdateResource();

	MapImage->SetBrushFromTexture(WorldMapTexture);
	MapImage->SetDesiredSizeOverride(FVector2D(TexSize, TexSize));
}

// ---------------------------------------------------------------------------
// Player Marker
// ---------------------------------------------------------------------------

void UVCWorldMapWidget::UpdatePlayerMarker()
{
	if (!PlayerMarker || !MapSubsystem.IsValid() || RenderedTexSize <= 0 || RenderedWorldPerPixel <= 0.f)
	{
		return;
	}

	const APlayerController* PC = GetOwningPlayer();
	if (!PC || !PC->GetPawn())
	{
		return;
	}

	const FVector PlayerPos = PC->GetPawn()->GetActorLocation();

	// Player's offset from the view center, in texture pixels
	const float PixelOffsetX = (PlayerPos.X - PanOffset.X) / RenderedWorldPerPixel;
	const float PixelOffsetY = (PlayerPos.Y - PanOffset.Y) / RenderedWorldPerPixel;

	// Position marker relative to canvas center (anchor is 0.5, 0.5)
	UCanvasPanelSlot* MarkerSlot = Cast<UCanvasPanelSlot>(PlayerMarker->Slot);
	if (MarkerSlot)
	{
		MarkerSlot->SetPosition(FVector2D(PixelOffsetX, PixelOffsetY));
	}

	// Update coordinate display in voxel coordinates
	if (MapCoordinateText)
	{
		const float VoxelSize = MapSubsystem->GetTileWorldSize() / FMath::Max(1, MapSubsystem->GetTileResolution());
		const int32 VoxelX = FMath::RoundToInt(PlayerPos.X / VoxelSize);
		const int32 VoxelY = FMath::RoundToInt(PlayerPos.Y / VoxelSize);
		MapCoordinateText->SetText(FText::FromString(
			FString::Printf(TEXT("Player: X=%d  Y=%d  |  Zoom: %.1fx"), VoxelX, VoxelY, CurrentZoom)
		));
	}
}
