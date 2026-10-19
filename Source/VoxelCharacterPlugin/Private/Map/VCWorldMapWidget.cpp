// Copyright Daniel Raquel. All Rights Reserved.

#include "Map/VCWorldMapWidget.h"
#include "Map/VCMapMarkerRegistry.h"
#include "VoxelMapSubsystem.h"
#include "VoxelCharacterPlugin.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "UObject/UObjectIterator.h"
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

	// North indicator above the map image (the map is north-up: world +X points up).
	UTextBlock* NorthLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("WorldMapNorth"));
	NorthLabel->SetText(FText::FromString(TEXT("N")));
	FSlateFontInfo NorthFont = NorthLabel->GetFont();
	NorthFont.Size = 16;
	NorthLabel->SetFont(NorthFont);
	NorthLabel->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f, 1.0f)));
	NorthLabel->SetJustification(ETextJustify::Center);
	if (UCanvasPanelSlot* NorthSlot = RootCanvas->AddChildToCanvas(NorthLabel))
	{
		NorthSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		NorthSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		NorthSlot->SetPosition(FVector2D(0.0f, -0.5f * FMath::Clamp(MapTextureFixedSize, 256, 2048) - 6.0f));
		NorthSlot->SetAutoSize(true);
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
		TileReadyHandle = MapSubsystem->OnMapTileReady.AddLambda([this](FIntPoint Tile)
		{
			// Zoomed out to the globe's albedo the tile images do not show: only the albedo landing ((-1, -1)) redraws at
			// once. Tiles, and the exploration they carry, redraw it at most every 2 s: an ExploreAll on a big planet
			// streams tens of thousands of images, and a redraw for each held the map at 6 FPS (spherical Phase 6, P6.5).
			if (bRenderedFromAlbedo && Tile != FIntPoint(-1, -1))
			{
				bAlbedoTilesPending = true;
				return;
			}
			bMapDirty = true;
		});
	}

	// Center on player
	if (const APlayerController* PC = GetOwningPlayer())
	{
		if (const APawn* PlayerPawn = PC->GetPawn())
		{
			PanOffset = FVector2D(PlayerPawn->GetActorLocation().X, PlayerPawn->GetActorLocation().Y);
			if (MapSubsystem.IsValid() && MapSubsystem->IsPlanetMap())
			{
				GlobeView = (PlayerPawn->GetActorLocation() - MapSubsystem->GetPlanetCenter()).GetSafeNormal();
			}
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

	bool bViewChanged = false;
	const bool bPlanet = MapSubsystem.IsValid() && MapSubsystem->IsPlanetMap();
	if (bAlbedoTilesPending && bPlanet && FPlatformTime::Seconds() - LastPlanetRebuildSeconds >= 2.0)
	{
		bAlbedoTilesPending = false;
		bMapDirty = true;
	}
	if (bMapDirty && bPlanet)
	{
		// The globe redraws at most 30 times a second (tiles stream in one by one).
		const double Now = FPlatformTime::Seconds();
		if (Now - LastPlanetRebuildSeconds >= 1.0 / 30.0)
		{
			LastPlanetRebuildSeconds = Now;
			RebuildMapTexture();
			bMapDirty = false;
			bViewChanged = true;
		}
	}
	else if (bMapDirty)
	{
		RebuildMapTexture();
		bMapDirty = false;
		bViewChanged = true;
	}

	UpdatePlayerMarker();

	// Registry markers refresh on view changes and at a slow cadence otherwise (gathering runs
	// deterministic placement over the whole visible area, which can be large when zoomed out).
	TimeSinceMarkerUpdate += InDeltaTime;
	if (bViewChanged || TimeSinceMarkerUpdate >= FMath::Max(MarkerUpdateInterval, 0.1f))
	{
		TimeSinceMarkerUpdate = 0.0f;
		UpdateMarkers();
	}
}

// ---------------------------------------------------------------------------
// Input — Zoom / Pan
// ---------------------------------------------------------------------------

FReply UVCWorldMapWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	const float Delta = InMouseEvent.GetWheelDelta();
	const float ZoomFactor = (Delta > 0) ? 1.25f : 0.8f;
	float LowZoom = MinZoom, HighZoom = MaxZoom;
	GetZoomRange(LowZoom, HighZoom);
	CurrentZoom = FMath::Clamp(CurrentZoom * ZoomFactor, LowZoom, HighZoom);
	bMapDirty = true;
	return FReply::Handled();
}

void UVCWorldMapWidget::GetZoomRange(float& OutMinZoom, float& OutMaxZoom) const
{
	OutMinZoom = MinZoom;
	OutMaxZoom = MaxZoom;
	if (const UVoxelMapSubsystem* Subsystem = MapSubsystem.Get(); Subsystem && Subsystem->IsPlanetMap())
	{
		// A planet: out to the whole globe at any radius (WM-BP11).
		GetPlanetZoomRange(Subsystem->GetPlanetRadius(), Subsystem->GetTileWorldSize(), MinZoom, MaxZoom, OutMinZoom, OutMaxZoom);
	}
}

UVCWorldMapWidget* UVCWorldMapWidget::FindOpen(const UWorld* World)
{
	for (TObjectIterator<UVCWorldMapWidget> It; It; ++It)
	{
		if (It->GetWorld() == World && It->IsInViewport())
		{
			return *It;
		}
	}
	return nullptr;
}

void UVCWorldMapWidget::SetZoom(float Zoom)
{
	float LowZoom = MinZoom, HighZoom = MaxZoom;
	GetZoomRange(LowZoom, HighZoom);
	CurrentZoom = Zoom < 0.0f ? LowZoom : FMath::Clamp(Zoom, LowZoom, HighZoom);
	bMapDirty = true;
}

void UVCWorldMapWidget::GetPlanetZoomRange(double PlanetRadius, double TileWorldSize, float InMinZoom, float InMaxZoom, float& OutMinZoom, float& OutMaxZoom)
{
	// GetGlobeRadiusPx: the globe's radius on screen is HalfTex * R * Zoom / (32 TileWorldSize); a whole globe with a tenth
	// of the view to spare is a radius of 0.9 HalfTex.
	const double WholeGlobe = PlanetRadius > 0.0 ? 0.9 * 32.0 * TileWorldSize / PlanetRadius : static_cast<double>(InMinZoom);
	OutMinZoom = static_cast<float>(FMath::Min(static_cast<double>(InMinZoom), WholeGlobe));
	OutMaxZoom = FMath::Max(InMaxZoom, OutMinZoom);
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
	if (bIsPanning && bRenderedPlanet)
	{
		// A planet: turn the globe so the content follows the mouse, north up.
		const FVector2D CurrentMousePos = InMouseEvent.GetScreenSpacePosition();
		const FVector2D Delta = CurrentMousePos - LastMousePos;
		LastMousePos = CurrentMousePos;
		GlobeView = FVCGlobeProjection::Make(GlobeView, GetGlobeRadiusPx()).Dragged(Delta).View;
		bMapDirty = true;
		return FReply::Handled();
	}
	if (bIsPanning && RenderedWorldPerPixel > 0.f)
	{
		const FVector2D CurrentMousePos = InMouseEvent.GetScreenSpacePosition();
		const FVector2D Delta = CurrentMousePos - LastMousePos;
		LastMousePos = CurrentMousePos;

		// Convert screen pixel delta to world delta (north-up: screen X = world +Y east,
		// screen Y = world -X north). Content follows the mouse.
		PanOffset.Y -= Delta.X * RenderedWorldPerPixel;
		PanOffset.X += Delta.Y * RenderedWorldPerPixel;
		// The view's centre stays over the world (WM-I2): an island's map does not pan off into the void.
		if (const UVoxelMapSubsystem* Subsystem = MapSubsystem.Get())
		{
			const FBox2D WorldBox = Subsystem->GetWorldBounds().GetBox2D();
			if (WorldBox.bIsValid)
			{
				PanOffset.X = FMath::Clamp(PanOffset.X, WorldBox.Min.X, WorldBox.Max.X);
				PanOffset.Y = FMath::Clamp(PanOffset.Y, WorldBox.Min.Y, WorldBox.Max.Y);
			}
		}
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
	if (Subsystem->IsPlanetMap())
	{
		RebuildPlanetMapTexture();
		return;
	}
	bRenderedPlanet = false;

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

	// World bounds of the texture. NORTH-UP orientation: world +X (north) points up on the
	// map (screen -Y), world +Y (east) points right (screen +X) — matching the minimap's
	// compass convention.
	const float WorldMaxX = ViewCenterX + ViewWorldExtent;
	const float WorldMinY = ViewCenterY - ViewWorldExtent;

	// The map keeps tile images under a budget (world modes review WM-E2): a tile may have only its overview, or no
	// image at all, until asked for. Zoomed in far enough that the overview would show blurred (more texels per tile
	// than half again its resolution), ask for detail; otherwise an image of any kind. Only the tiles in view ask.
	const float TexelsPerTile = TileWorldSize / FMath::Max(RenderedWorldPerPixel, KINDA_SMALL_NUMBER);
	const bool bWantDetail = TexelsPerTile > 1.5f * Subsystem->GetTileOverviewResolution();

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
			if (!Tile || (bWantDetail && Tile->Resolution < TileResolution))
			{
				// Drawn again in the background; OnMapTileReady redraws the map when it lands.
				Subsystem->RequestTileImage(TileCoord, bWantDetail);
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

					// Map to texture pixel (north-up: east right, north up)
					const int32 DstX = FMath::FloorToInt((PixWorldY - WorldMinY) / RenderedWorldPerPixel);
					const int32 DstY = FMath::FloorToInt((WorldMaxX - PixWorldX) / RenderedWorldPerPixel);

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

double UVCWorldMapWidget::GetGlobeRadiusPx() const
{
	const UVoxelMapSubsystem* Subsystem = MapSubsystem.Get();
	if (!Subsystem)
	{
		return 1.0;
	}
	// The plane's view shows BaseWorldExtent / CurrentZoom from the centre to the texture's edge (32 tiles at zoom 1);
	// the globe at the same scale.
	const double HalfTex = 0.5 * FMath::Clamp(MapTextureFixedSize, 256, 2048);
	const double ViewWorldExtent = 32.0 * Subsystem->GetTileWorldSize() / FMath::Max(CurrentZoom, KINDA_SMALL_NUMBER);
	return HalfTex * Subsystem->GetPlanetRadius() / FMath::Max(ViewWorldExtent, 1.0);
}

void UVCWorldMapWidget::RebuildPlanetMapTexture()
{
	UVoxelMapSubsystem* Subsystem = MapSubsystem.Get();
	const int32 TexSize = FMath::Clamp(MapTextureFixedSize, 256, 2048);
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
	FTexture2DMipMap& Mip = WorldMapTexture->GetPlatformData()->Mips[0];
	uint8* PixelData = static_cast<uint8*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	if (!PixelData)
	{
		return;
	}

	RenderedGlobe = FVCGlobeProjection::Make(GlobeView, GetGlobeRadiusPx());
	bRenderedPlanet = true;
	RenderedTexSize = TexSize;
	RenderedWorldPerPixel = static_cast<float>(Subsystem->GetPlanetRadius() / RenderedGlobe.RadiusPx);

	// Detail where a tile spans more texels than half again its overview (as the plane's view decides).
	const double TexelsPerTile = Subsystem->GetTileWorldSize() * RenderedGlobe.RadiusPx / FMath::Max(Subsystem->GetPlanetRadius(), 1.0);
	FVCPlanetMapSampler Sampler;
	Sampler.bWantDetail = TexelsPerTile > 1.5 * Subsystem->GetTileOverviewResolution();
	// Zoomed out so a texel spans many tiles (a big planet's globe): its albedo, not a tile pixel each (WM-BP11).
	Sampler.bUseGlobeAlbedo = TexelsPerTile < GlobeAlbedoTexelsPerTile;
	bRenderedFromAlbedo = Sampler.bUseGlobeAlbedo && Subsystem->GetGlobeAlbedo() != nullptr;
	Sampler.Background = FColor(2, 2, 5, 255);
	const double Half = 0.5 * TexSize;
	const FVCGlobeProjection Globe = RenderedGlobe;
	Sampler.Fill(*Subsystem, PixelData, TexSize, 16, [&Globe, Half](double X, double Y, FVector& OutDirection)
	{
		return Globe.ScreenToDirection(FVector2D(X - Half, Y - Half), OutDirection);
	});

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

	if (bRenderedPlanet)
	{
		// The globe: where the player's direction shows, hidden on the far side.
		const FVector Direction = (PlayerPos - MapSubsystem->GetPlanetCenter()).GetSafeNormal();
		FVector2D Screen;
		const bool bVisible = RenderedGlobe.DirectionToScreen(Direction, Screen)
			&& FMath::Abs(Screen.X) <= 0.5f * RenderedTexSize && FMath::Abs(Screen.Y) <= 0.5f * RenderedTexSize;
		PlayerMarker->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
		if (UCanvasPanelSlot* PlanetMarkerSlot = Cast<UCanvasPanelSlot>(PlayerMarker->Slot))
		{
			PlanetMarkerSlot->SetPosition(Screen);
		}
		if (MapCoordinateText)
		{
			const FVector2D LatLon = FVCPlanetMapFrame::At(MapSubsystem->GetPlanetCenter(), MapSubsystem->GetPlanetRadius(), PlayerPos).GetLatLon();
			MapCoordinateText->SetText(FText::FromString(
				FString::Printf(TEXT("Player: Lat %.2f  Lon %.2f  |  Zoom: %.1fx"), LatLon.X, LatLon.Y, CurrentZoom)));
		}
		return;
	}

	// Player's offset from the view center, in texture pixels (north-up: east right, north up)
	const float PixelOffsetX = (PlayerPos.Y - PanOffset.Y) / RenderedWorldPerPixel;
	const float PixelOffsetY = (PanOffset.X - PlayerPos.X) / RenderedWorldPerPixel;

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

// ---------------------------------------------------------------------------
// Registry Markers
// ---------------------------------------------------------------------------

void UVCWorldMapWidget::UpdateMarkers()
{
	if (!RootCanvas || !WidgetTree || RenderedTexSize <= 0 || RenderedWorldPerPixel <= 0.0f)
	{
		return;
	}

	if (!MarkerRegistry.IsValid())
	{
		if (const UWorld* World = GetWorld())
		{
			MarkerRegistry = World->GetSubsystem<UVCMapMarkerRegistry>();
		}
	}

	TArray<FVCMapMarker> Markers;
	UVCMapMarkerRegistry* PlanetRegistry = bRenderedPlanet ? MarkerRegistry.Get() : nullptr;
	if (PlanetRegistry && MapSubsystem.IsValid())
	{
		// The globe: markers within the visible cap (the view's centre on the datum sphere and the chord to the rim).
		const double Radius = MapSubsystem->GetPlanetRadius();
		const double VisibleAngle = FMath::Min(UE_DOUBLE_HALF_PI, 0.5 * RenderedTexSize * UE_SQRT_2 / RenderedGlobe.RadiusPx);
		PlanetRegistry->GatherMarkersNear(MapSubsystem->GetPlanetCenter() + RenderedGlobe.View * Radius, 2.0 * Radius * FMath::Sin(0.5 * VisibleAngle), Markers);
	}
	else if (UVCMapMarkerRegistry* Registry = MarkerRegistry.Get())
	{
		// The rendered view: PanOffset-centred, RenderedTexSize pixels at RenderedWorldPerPixel.
		const float ViewWorldExtent = 0.5f * RenderedTexSize * RenderedWorldPerPixel;
		const FBox2D Area(PanOffset - FVector2D(ViewWorldExtent, ViewWorldExtent),
			PanOffset + FVector2D(ViewWorldExtent, ViewWorldExtent));
		Registry->GatherMarkers(Area, Markers);
	}

	Markers.Sort([](const FVCMapMarker& A, const FVCMapMarker& B) { return A.Priority > B.Priority; });

	const float HalfTex = 0.5f * RenderedTexSize;

	int32 Used = 0;
	for (const FVCMapMarker& Marker : Markers)
	{
		if (Used >= MaxMarkers)
		{
			break;
		}

		// Same mapping as the player marker (north-up: east right, north up).
		FVector2D PixelOffset(
			(Marker.WorldPosition.Y - PanOffset.Y) / RenderedWorldPerPixel,
			(PanOffset.X - Marker.WorldPosition.X) / RenderedWorldPerPixel);
		if (bRenderedPlanet && (!MapSubsystem.IsValid() || !RenderedGlobe.DirectionToScreen(
			(Marker.WorldLocation - MapSubsystem->GetPlanetCenter()).GetSafeNormal(), PixelOffset)))
		{
			continue; // on the globe's far side
		}
		if (FMath::Abs(PixelOffset.X) > HalfTex || FMath::Abs(PixelOffset.Y) > HalfTex)
		{
			continue; // outside the map image
		}

		// Pool: dot + label per slot, created on demand.
		if (!MarkerDots.IsValidIndex(Used))
		{
			UImage* Dot = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
			Dot->SetDesiredSizeOverride(FVector2D(MarkerDotSize, MarkerDotSize));
			if (UCanvasPanelSlot* DotSlot = RootCanvas->AddChildToCanvas(Dot))
			{
				DotSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
				DotSlot->SetAlignment(FVector2D(0.5f, 0.5f));
				DotSlot->SetAutoSize(true);
			}
			MarkerDots.Add(Dot);

			UTextBlock* Label = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
			FSlateFontInfo Font = Label->GetFont();
			Font.Size = 9;
			Label->SetFont(Font);
			if (UCanvasPanelSlot* LabelSlot = RootCanvas->AddChildToCanvas(Label))
			{
				LabelSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
				LabelSlot->SetAlignment(FVector2D(0.0f, 0.5f));
				LabelSlot->SetAutoSize(true);
			}
			MarkerLabels.Add(Label);
		}

		UImage* Dot = MarkerDots[Used];
		Dot->SetVisibility(ESlateVisibility::HitTestInvisible);
		Dot->SetColorAndOpacity(Marker.Color);
		if (UCanvasPanelSlot* DotSlot = Cast<UCanvasPanelSlot>(Dot->Slot))
		{
			DotSlot->SetPosition(PixelOffset);
		}

		UTextBlock* Label = MarkerLabels[Used];
		Label->SetVisibility(Marker.Label.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		Label->SetText(Marker.Label);
		Label->SetColorAndOpacity(FSlateColor(Marker.Color));
		if (UCanvasPanelSlot* LabelSlot = Cast<UCanvasPanelSlot>(Label->Slot))
		{
			LabelSlot->SetPosition(PixelOffset + FVector2D(0.75f * MarkerDotSize + 2.0f, 0.0f));
		}

		++Used;
	}

	// Hide the unused tail of the pools.
	for (int32 i = Used; i < MarkerDots.Num(); ++i)
	{
		MarkerDots[i]->SetVisibility(ESlateVisibility::Collapsed);
		MarkerLabels[i]->SetVisibility(ESlateVisibility::Collapsed);
	}
}
