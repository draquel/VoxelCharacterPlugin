// Copyright Daniel Raquel. All Rights Reserved.

#include "Map/VCWorldMapWidget.h"
#include "Map/VCMapSamplers.h"
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
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

// ---------------------------------------------------------------------------
// Rebuild timing (the map scaling plan's M0, G163): what the open world map costs the game thread, by phase, and how
// often tile arrivals rebuild it.
// ---------------------------------------------------------------------------

namespace VCWorldMapStats
{
	enum EPhase : int32 { Fill = 0, Upload, Markers, Rebuild, Num };
	static const TCHAR* PhaseNames[Num] = { TEXT("texture fill"), TEXT("texture upload"), TEXT("markers"), TEXT("rebuild") };
	static double SumMs[Num] = {};
	static double MaxMs[Num] = {};
	static int64 Counts[Num] = {};
	static int64 Arrivals = 0;
	static int64 Deferred = 0;
	static int64 Over2Ms = 0;
	static double SinceSeconds = FPlatformTime::Seconds();

	static void Add(EPhase Phase, double Ms)
	{
		SumMs[Phase] += Ms;
		MaxMs[Phase] = FMath::Max(MaxMs[Phase], Ms);
		++Counts[Phase];
	}

	static void Reset()
	{
		FMemory::Memzero(SumMs, sizeof(SumMs));
		FMemory::Memzero(MaxMs, sizeof(MaxMs));
		FMemory::Memzero(Counts, sizeof(Counts));
		Arrivals = Deferred = Over2Ms = 0;
		SinceSeconds = FPlatformTime::Seconds();
	}

	/** Times a scope into a phase. */
	struct FScope
	{
		EPhase Phase;
		uint64 Start;
		explicit FScope(EPhase InPhase) : Phase(InPhase), Start(FPlatformTime::Cycles64()) {}
		~FScope()
		{
			const double Ms = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Start);
			Add(Phase, Ms);
			Over2Ms += (Phase == Rebuild && Ms > 2.0) ? 1 : 0;
		}
	};
}

// Console: vc.WorldMap.Stats [reset]
static FAutoConsoleCommand GVCWorldMapStatsCmd(
	TEXT("vc.WorldMap.Stats"),
	TEXT("Log the world map's rebuilds since the last reset: per second, each phase's mean and maximum (texture fill, upload, ")
	TEXT("markers, the rebuild), and the tile arrivals: those in view (coalesced into rebuilds) and those that do not show. 'reset' clears it."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		using namespace VCWorldMapStats;
		if (Args.Num() > 0 && Args[0].Equals(TEXT("reset"), ESearchCase::IgnoreCase))
		{
			Reset();
			UE_LOG(LogVoxelCharacter, Warning, TEXT("vc.WorldMap.Stats: reset"));
			return;
		}
		const double Elapsed = FMath::Max(FPlatformTime::Seconds() - SinceSeconds, 1.0e-3);
		UE_LOG(LogVoxelCharacter, Warning, TEXT("vc.WorldMap.Stats: %lld rebuilds in %.1f s (%.1f/s), %lld over 2 ms; %lld tile arrivals, %lld deferred (not in view), %lld in view"),
			Counts[Rebuild], Elapsed, static_cast<double>(Counts[Rebuild]) / Elapsed, Over2Ms, Arrivals, Deferred, Arrivals - Deferred);
		for (int32 Phase = 0; Phase < Num; ++Phase)
		{
			const double N = static_cast<double>(FMath::Max<int64>(1, Counts[Phase]));
			UE_LOG(LogVoxelCharacter, Warning, TEXT("vc.WorldMap.Stats: %s: %lld, mean %.3f ms, max %.3f ms, total %.1f ms"), PhaseNames[Phase],
				Counts[Phase], SumMs[Phase] / N, MaxMs[Phase], SumMs[Phase]);
		}
	}));

// ---------------------------------------------------------------------------
// Arrivals in view (MAP_PYRAMID_PLAN.md P1, G166)
// ---------------------------------------------------------------------------

void FVCMapArrivalCoalescer::SetView(int32 InLevel, TConstArrayView<FIntPoint> Tiles)
{
	Level = InLevel;
	ViewTiles.Reset();
	ViewTiles.Append(Tiles);
}

bool FVCMapArrivalCoalescer::OnArrival(int32 InLevel, const FIntPoint& Tile)
{
	if (InLevel != Level || !ViewTiles.Contains(Tile))
	{
		return false;
	}
	bPending = true;
	return true;
}

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

	// Tile arrivals redraw the open map, coalesced, and only those the view shows (MAP_PYRAMID_PLAN.md P1, G166): a redraw
	// per arrival held the 50 km globe at 94 ms frames, and before the pyramid an ExploreAll streamed images the view never
	// showed.
	if (MapSubsystem.IsValid() && !TileReadyHandle.IsValid())
	{
		TileReadyHandle = MapSubsystem->OnMapTileReady.AddLambda([this](const FVoxelMapTileId& Id)
		{
			++VCWorldMapStats::Arrivals;
			if (!Arrivals.OnArrival(Id.Level, Id.Coord))
			{
				++VCWorldMapStats::Deferred;
			}
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
	// Unbind the delegates, and drop the world map's request: its tiles stop waiting.
	if (MapSubsystem.IsValid())
	{
		MapSubsystem->OnMapTileReady.Remove(TileReadyHandle);
		MapSubsystem->RequestView(EVoxelMapRequester::WorldMap, {});
	}
	TileReadyHandle.Reset();

	Super::NativeDestruct();
}

void UVCWorldMapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	bool bViewChanged = false;
	const UVoxelMapSubsystem* Subsystem = MapSubsystem.Get();
	const bool bPlanet = Subsystem && Subsystem->IsPlanetMap();
	const double Now = FPlatformTime::Seconds();
	// Arrivals in view, coalesced (G166); new exploration (the fog of war) at most every ExplorationRedrawSeconds. Both
	// wait longer after a costly rebuild (FVCMapArrivalCoalescer::GetMinGap).
	if (Arrivals.IsDue(Now))
	{
		bMapDirty = true;
	}
	if (Subsystem && Subsystem->GetExplorationRevision() != RenderedExplorationRevision
		&& Now - LastRebuildSeconds >= FMath::Max(ExplorationRedrawSeconds, Arrivals.GetMinGap()))
	{
		bMapDirty = true;
	}
	// A planet's globe redraws at most 30 times a second while it is dragged.
	if (bMapDirty && (!bPlanet || Now - LastRebuildSeconds >= 1.0 / 30.0))
	{
		LastRebuildSeconds = Now;
		RenderedExplorationRevision = Subsystem ? Subsystem->GetExplorationRevision() : 0;
		RebuildMapTexture();
		Arrivals.OnRedrawn(Now, FPlatformTime::Seconds() - Now);
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

void UVCWorldMapWidget::RequestViewTiles(int32 Level, const TArray<FIntPoint>& Tiles)
{
	Arrivals.SetView(Level, Tiles);
	if (UVoxelMapSubsystem* Subsystem = MapSubsystem.Get())
	{
		TArray<FVoxelMapTileId> Request;
		Request.Reserve(Tiles.Num());
		for (const FIntPoint& Coord : Tiles)
		{
			Request.Emplace(Coord, Level);
		}
		Subsystem->RequestView(EVoxelMapRequester::WorldMap, Request);
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
	TRACE_CPUPROFILER_EVENT_SCOPE(VCWorldMap_Rebuild);
	const VCWorldMapStats::FScope Timed(VCWorldMapStats::Rebuild);
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

	// The image through the plane sampler, north up (world +X up, +Y right, the minimap's compass convention; world modes
	// review G64's texel fill; code review CR2, VCP-15): each texel the pixel just inside its far corner, explored ground
	// only with the fog colours for the rest, composed from the tile pyramid at the level matching the texels (about 300
	// tiles at any zoom, MAP_PYRAMID_PLAN.md P1), the coarser stored levels shown while those draw. The tiles it shows are
	// asked for, nearest the centre first.
	FVCPlaneMapSampler Sampler;
	Sampler.bNorthUp = true;
	Sampler.bRequireExplored = true;
	Sampler.Level = Subsystem->LevelForTexel(RenderedWorldPerPixel);
	Sampler.Unexplored = FColor(10, 10, 10, 255);
	Sampler.Undrawn = FColor(25, 25, 25, 255);
	Sampler.Background = Sampler.Unexplored;
	TArray<FIntPoint> ViewTiles;
	Sampler.ViewTiles = &ViewTiles;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VCWorldMap_Fill);
		const VCWorldMapStats::FScope TimedFill(VCWorldMapStats::Fill);
		Sampler.Fill(*Subsystem, PixelData, TexSize, FVector(PanOffset.X, PanOffset.Y, 0.0), RenderedWorldPerPixel);
	}
	RequestViewTiles(Sampler.Level, ViewTiles);

	TRACE_CPUPROFILER_EVENT_SCOPE(VCWorldMap_Upload);
	const VCWorldMapStats::FScope TimedUpload(VCWorldMapStats::Upload);
	Mip.BulkData.Unlock();
	WorldMapTexture->UpdateResource();

	MapImage->SetBrushFromTexture(WorldMapTexture);	MapImage->SetDesiredSizeOverride(FVector2D(TexSize, TexSize));
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

	// The pyramid level matching a texel at the view's centre (MAP_PYRAMID_PLAN.md P1), at every zoom: zoomed out the
	// globe shows the planet's prefetched top levels while the view's own draw (P2: they replace the globe albedo), and its
	// fog of war is read from the explored mask per texel.
	FVCPlanetMapSampler Sampler;
	Sampler.Level = Subsystem->LevelForTexel(RenderedWorldPerPixel);
	Sampler.Background = FColor(2, 2, 5, 255);
	TArray<FIntPoint> ViewTiles;
	Sampler.ViewTiles = &ViewTiles;
	const double Half = 0.5 * TexSize;
	const FVCGlobeProjection Globe = RenderedGlobe;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VCWorldMap_Fill);
		const VCWorldMapStats::FScope TimedFill(VCWorldMapStats::Fill);
		Sampler.Fill(*Subsystem, PixelData, TexSize, 16, [&Globe, Half](double X, double Y, FVector& OutDirection)
		{
			return Globe.ScreenToDirection(FVector2D(X - Half, Y - Half), OutDirection);
		});
	}
	RequestViewTiles(Sampler.Level, ViewTiles);

	TRACE_CPUPROFILER_EVENT_SCOPE(VCWorldMap_Upload);
	const VCWorldMapStats::FScope TimedUpload(VCWorldMapStats::Upload);
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
	TRACE_CPUPROFILER_EVENT_SCOPE(VCWorldMap_Markers);
	const VCWorldMapStats::FScope Timed(VCWorldMapStats::Markers);
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
		PlanetRegistry->GatherMarkers(MapSubsystem->GetPlanetCenter() + RenderedGlobe.View * Radius, 2.0 * Radius * FMath::Sin(0.5 * VisibleAngle), Markers);
	}
	else if (UVCMapMarkerRegistry* Registry = MarkerRegistry.Get())
	{
		// The rendered view: PanOffset-centred, RenderedTexSize pixels at RenderedWorldPerPixel (the square of that half extent).
		const float ViewWorldExtent = 0.5f * RenderedTexSize * RenderedWorldPerPixel;
		Registry->GatherMarkers(FVector(PanOffset.X, PanOffset.Y, 0.0), ViewWorldExtent, Markers);
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
			(Marker.WorldLocation.Y - PanOffset.Y) / RenderedWorldPerPixel,
			(PanOffset.X - Marker.WorldLocation.X) / RenderedWorldPerPixel);
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
