// Copyright Daniel Raquel. All Rights Reserved.

#include "Map/VCMinimapWidget.h"
#include "Map/VCMapMarkerRegistry.h"
#include "Map/VCPlanetMapProjection.h"
#include "VoxelMapSubsystem.h"
#include "VoxelCharacterPlugin.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

// About sqrt(2): the rotating map reaches this far beyond the visible square's half-width at 45 degrees, so markers are
// gathered that far (the texture's own size is GetTextureSize: the diagonal plus the shift margin).
static constexpr float RotationOversize = 1.42f;

// ---------------------------------------------------------------------------
// Refresh timing (world modes review G64): what each refresh of the minimap costs the game thread, by phase.
// ---------------------------------------------------------------------------

namespace VCMinimapStats
{
	enum EPhase : int32 { Tiles = 0, Fill, Upload, Markers, Total, Lock, Num };
	static int64 Fills = 0;
	static const TCHAR* PhaseNames[Num] = { TEXT("tile requests"), TEXT("texture fill"), TEXT("texture upload"), TEXT("markers"), TEXT("refresh"), TEXT("texture lock (in the fill)") };
	static double SumMs[Num] = {};
	static double MaxMs[Num] = {};
	static int64 Refreshes = 0;
	static int64 Skipped = 0;
	static int64 Over2Ms = 0;

	static void Add(EPhase Phase, double Ms)
	{
		SumMs[Phase] += Ms;
		MaxMs[Phase] = FMath::Max(MaxMs[Phase], Ms);
	}

	static void Reset()
	{
		FMemory::Memzero(SumMs, sizeof(SumMs));
		FMemory::Memzero(MaxMs, sizeof(MaxMs));
		Refreshes = Skipped = Over2Ms = Fills = 0;
	}

	/** Times a scope into a phase. */
	struct FScope
	{
		EPhase Phase;
		uint64 Start;
		explicit FScope(EPhase InPhase) : Phase(InPhase), Start(FPlatformTime::Cycles64()) {}
		~FScope() { Add(Phase, FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - Start)); }
	};
}

static TAutoConsoleVariable<int32> CVarMinimapSkipUnchanged(
	TEXT("vc.Minimap.SkipUnchanged"),
	1,
	TEXT("Minimap (world modes review G64): 1 = fill the texture again only when the player moved RefillShiftTexels, a tile it "
	     "shows changed or MaxRefillSeconds passed, shifting the image in between; 0 = fill it every refresh, as before."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarMinimapTexelFill(
	TEXT("vc.Minimap.TexelFill"),
	1,
	TEXT("Minimap on a plane (G64): 1 = fill texel by texel (columns and rows worked out once); 0 = blit every pixel of every "
	     "tile in view, as before."),
	ECVF_Default);

// Console: vc.Minimap.Stats [reset]
static FAutoConsoleCommand GVCMinimapStatsCmd(
	TEXT("vc.Minimap.Stats"),
	TEXT("Log the minimap's refresh cost per phase since the last reset (G64). 'reset' clears it."),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
	{
		using namespace VCMinimapStats;
		if (Args.Num() > 0 && Args[0].Equals(TEXT("reset"), ESearchCase::IgnoreCase))
		{
			Reset();
			UE_LOG(LogVoxelCharacter, Warning, TEXT("vc.Minimap.Stats: reset"));
			return;
		}
		UE_LOG(LogVoxelCharacter, Warning, TEXT("vc.Minimap.Stats: %lld refreshes, %lld fills (%lld skipped: nothing changed), %lld over 2 ms"),
			Refreshes, Fills, Skipped, Over2Ms);
		for (int32 Phase = 0; Phase < Num; ++Phase)
		{
			// The fill and the upload per fill, the others per refresh.
			const double N = FMath::Max<int64>(1, (Phase == Fill || Phase == Upload || Phase == Lock) ? Fills : Refreshes);
			UE_LOG(LogVoxelCharacter, Warning, TEXT("vc.Minimap.Stats: %s: mean %.3f ms, max %.3f ms, total %.1f ms"), PhaseNames[Phase],
				SumMs[Phase] / N, MaxMs[Phase], SumMs[Phase]);
		}
	}));

// ---------------------------------------------------------------------------
// Widget Tree Construction
// ---------------------------------------------------------------------------

void UVCMinimapWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildWidgetTree();
}

void UVCMinimapWidget::BuildWidgetTree()
{
	if (!WidgetTree)
	{
		return;
	}

	// Root SizeBox — constrains the minimap to a fixed pixel size and clips
	// the oversized rotating map image to this square.
	RootSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("MinimapSizeBox"));
	RootSizeBox->SetWidthOverride(MinimapSize);
	RootSizeBox->SetHeightOverride(MinimapSize);
	RootSizeBox->SetClipping(EWidgetClipping::ClipToBounds);
	WidgetTree->RootWidget = RootSizeBox;

	// Canvas panel — children can be positioned and sized freely, including
	// beyond the parent bounds. The SizeBox clips the overflow.
	MapCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MinimapCanvas"));
	RootSizeBox->AddChild(MapCanvas);

	// Dark background fill (stays static, fills the visible square)
	MapBackground = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MinimapBackground"));
	MapBackground->SetColorAndOpacity(FLinearColor(0.02f, 0.02f, 0.05f, 0.85f));
	UCanvasPanelSlot* BgSlot = MapCanvas->AddChildToCanvas(MapBackground);
	if (BgSlot)
	{
		BgSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		BgSlot->SetOffsets(FMargin(0.0f));
	}

	// Map image — oversized, centered, rotated by camera yaw each tick.
	// AutoSize lets SetDesiredSizeOverride control the actual widget size,
	// and the canvas slot allows it to extend beyond the canvas bounds.
	MapImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("MapImage"));
	MapImage->SetColorAndOpacity(FLinearColor::White);
	MapImage->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	UCanvasPanelSlot* MapSlot = MapCanvas->AddChildToCanvas(MapImage);
	if (MapSlot)
	{
		MapSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		MapSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		MapSlot->SetAutoSize(true);
	}

	// Player arrow at center — a small dot
	PlayerArrow = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PlayerArrow"));
	PlayerArrow->SetColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.9f));
	PlayerArrow->SetDesiredSizeOverride(FVector2D(8.0f, 8.0f));
	UCanvasPanelSlot* ArrowSlot = MapCanvas->AddChildToCanvas(PlayerArrow);
	if (ArrowSlot)
	{
		ArrowSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		ArrowSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		ArrowSlot->SetAutoSize(true);
	}

	// Coordinate text at bottom center
	CoordinateText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("CoordText"));
	CoordinateText->SetText(FText::FromString(TEXT("X: 0  Y: 0")));
	FSlateFontInfo Font = CoordinateText->GetFont();
	Font.Size = 10;
	CoordinateText->SetFont(Font);
	CoordinateText->SetColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f, 0.9f)));
	CoordinateText->SetJustification(ETextJustify::Center);
	UCanvasPanelSlot* TextSlot = MapCanvas->AddChildToCanvas(CoordinateText);
	if (TextSlot)
	{
		TextSlot->SetAnchors(FAnchors(0.5f, 1.0f, 0.5f, 1.0f));
		TextSlot->SetAlignment(FVector2D(0.5f, 1.0f));
		TextSlot->SetAutoSize(true);
	}

	// North indicator — orbits the minimap edge to show compass north
	NorthIndicator = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NorthIndicator"));
	NorthIndicator->SetText(FText::FromString(TEXT("N")));
	FSlateFontInfo NorthFont = NorthIndicator->GetFont();
	NorthFont.Size = 14;
	NorthIndicator->SetFont(NorthFont);
	NorthIndicator->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f, 1.0f)));
	NorthIndicator->SetJustification(ETextJustify::Center);
	UCanvasPanelSlot* NorthSlot = MapCanvas->AddChildToCanvas(NorthIndicator);
	if (NorthSlot)
	{
		NorthSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
		NorthSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		NorthSlot->SetAutoSize(true);
	}
}

// ---------------------------------------------------------------------------
// Tick — Refresh Map
// ---------------------------------------------------------------------------

void UVCMinimapWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	TimeSinceLastUpdate += InDeltaTime;
	if (TimeSinceLastUpdate < UpdateInterval)
	{
		return;
	}
	TimeSinceLastUpdate = 0.0f;
	TRACE_CPUPROFILER_EVENT_SCOPE(VCMinimap_Refresh);
	const uint64 RefreshStart = FPlatformTime::Cycles64();
	ON_SCOPE_EXIT
	{
		const double Ms = FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - RefreshStart);
		VCMinimapStats::Add(VCMinimapStats::Total, Ms);
		++VCMinimapStats::Refreshes;
		VCMinimapStats::Over2Ms += Ms > 2.0 ? 1 : 0;
	};

	// Resolve subsystem lazily
	if (!MapSubsystem.IsValid())
	{
		if (const UWorld* World = GetWorld())
		{
			MapSubsystem = World->GetSubsystem<UVoxelMapSubsystem>();
		}
		if (!MapSubsystem.IsValid())
		{
			return;
		}
	}

	// Get player position and camera yaw
	const APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	const APawn* PlayerPawn = PC->GetPawn();
	if (!PlayerPawn)
	{
		return;
	}

	const FVector PlayerPos = PlayerPawn->GetActorLocation();

	// Request tiles for the minimap's visible area (oversized for rotation and the shift margin)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VCMinimap_RequestTiles);
		const VCMinimapStats::FScope Timed(VCMinimapStats::Tiles);
		MapSubsystem->RequestTilesInRadius(PlayerPos, static_cast<float>(0.5 * GetTextureSize() * GetWorldPerPixel()));
	}

	// A planet's minimap is the tangent plane under the player, north toward the +Z pole (spherical Phase 4, WM-SY9).
	const bool bPlanet = MapSubsystem->IsPlanetMap();
	const FVCPlanetMapFrame PlanetFrame = bPlanet
		? FVCPlanetMapFrame::At(MapSubsystem->GetPlanetCenter(), MapSubsystem->GetPlanetRadius(), PlayerPos)
		: FVCPlanetMapFrame();
	if (CoordinateText && bPlanet)
	{
		const FVector2D LatLon = PlanetFrame.GetLatLon();
		CoordinateText->SetText(FText::FromString(FString::Printf(TEXT("Lat %.2f  Lon %.2f"), LatLon.X, LatLon.Y)));
	}
	// Update coordinate text — display in voxel coordinates (divide by VoxelSize)
	else if (CoordinateText)
	{
		const float VoxelSize = MapSubsystem->GetTileWorldSize() / FMath::Max(1, MapSubsystem->GetTileResolution());
		const int32 VoxelX = FMath::RoundToInt(PlayerPos.X / VoxelSize);
		const int32 VoxelY = FMath::RoundToInt(PlayerPos.Y / VoxelSize);
		CoordinateText->SetText(FText::FromString(
			FString::Printf(TEXT("X: %d  Y: %d"), VoxelX, VoxelY)
		));
	}

	// Fill the map texture again only when what it shows changed (G64); in between it is shifted below.
	if (NeedsRefill(PlayerPos, FPlatformTime::Seconds()))
	{
		RefreshMapTexture(PlayerPos);
	}
	else
	{
		++VCMinimapStats::Skipped;
	}

	// Rotate map so the player's forward direction points up on the minimap.
	// UE yaw=0 is +X. The texture maps +X to the right, so a -90 offset
	// rotates +X (forward at yaw=0) from right to up.
	FRotator CameraRot;
	FVector CameraLoc;
	PC->GetPlayerViewPoint(CameraLoc, CameraRot);

	// On a planet the heading is measured from north on the tangent plane, as yaw is from +X on a plane: the plane the shown
	// texture was drawn in (G64).
	const float Yaw = bPlanet ? static_cast<float>(GetDisplayFrame(PlayerPos).HeadingDegrees(CameraRot.Vector())) : CameraRot.Yaw;
	const float MapAngleDeg = -Yaw - 90.0f;
	if (MapImage)
	{
		MapImage->SetRenderTransformAngle(MapAngleDeg);
		// The texture is centred where it was last filled: shift it back by the player's movement since, rotated with the
		// map (the render transform translates after rotating), so the terrain stays under the player.
		MapImage->SetRenderTranslation((-GetFillOffsetTexels(PlayerPos)).GetRotated(MapAngleDeg));
	}

	// Marker dots from the registry (POIs, quests, ... — the map is source-agnostic),
	// rotated with the map so they stay glued to the terrain.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VCMinimap_Markers);
		const VCMinimapStats::FScope Timed(VCMinimapStats::Markers);
		UpdateMarkers(PlayerPos, MapAngleDeg);
	}

	// Position the north indicator on the minimap edge.
	// North = +X axis (yaw=0 direction). As the camera rotates, north's
	// position on the minimap orbits around the center.
	if (NorthIndicator)
	{
		const float NorthAngleRad = FMath::DegreesToRadians(-Yaw);
		const float Radius = MinimapSize * 0.40f;
		const float NX = FMath::Sin(NorthAngleRad) * Radius;
		const float NY = -FMath::Cos(NorthAngleRad) * Radius;

		UCanvasPanelSlot* NorthSlot = Cast<UCanvasPanelSlot>(NorthIndicator->Slot);
		if (NorthSlot)
		{
			NorthSlot->SetPosition(FVector2D(NX, NY));
		}
	}
}

// ---------------------------------------------------------------------------
// Markers
// ---------------------------------------------------------------------------

void UVCMinimapWidget::UpdateMarkers(const FVector& PlayerPos, float MapAngleDeg)
{
	if (!MapCanvas || !WidgetTree)
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

	const UVoxelMapSubsystem* Map = MapSubsystem.Get();
	const bool bPlanet = Map && Map->IsPlanetMap();
	TArray<FVCMapMarker> Markers;
	if (UVCMapMarkerRegistry* Registry = MarkerRegistry.Get())
	{
		// Gather slightly beyond the visible radius so dots slide in smoothly at the rim.
		const FVector2D Player2D(PlayerPos.X, PlayerPos.Y);
		const float GatherRadius = MinimapWorldRadius * RotationOversize;
		if (bPlanet)
		{
			Registry->GatherMarkersNear(PlayerPos, GatherRadius * UE_SQRT_2, Markers);
		}
		else
		{
			const FBox2D Area(Player2D - FVector2D(GatherRadius, GatherRadius), Player2D + FVector2D(GatherRadius, GatherRadius));
			Registry->GatherMarkers(Area, Markers);
		}
	}
	// Offsets on the plane the shown texture was drawn in, from the player (G64).
	const FVCPlanetMapFrame PlanetFrame = bPlanet ? GetDisplayFrame(PlayerPos) : FVCPlanetMapFrame();
	const FVector2D PlayerOnFrame = bPlanet ? PlanetFrame.OffsetOf(PlayerPos) : FVector2D::ZeroVector;

	// Highest priority first — they win the marker budget and draw on top (added last below).
	Markers.Sort([](const FVCMapMarker& A, const FVCMapMarker& B) { return A.Priority > B.Priority; });

	const FVector2D Player2D(PlayerPos.X, PlayerPos.Y);
	const float PixelsPerWorldUnit = MinimapSize / (2.0f * FMath::Max(MinimapWorldRadius, 1.0f));
	const float VisibleRadiusPx = MinimapSize * 0.48f; // keep dots inside the square

	int32 Used = 0;
	for (const FVCMapMarker& Marker : Markers)
	{
		if (Used >= MaxMarkers)
		{
			break;
		}

		// World offset -> pixel offset -> rotate with the map image (same angle, same pivot). On a planet the offset on the
		// tangent plane, (north, east) as (X, Y) are on a plane.
		const FVector2D PixelOffset = bPlanet
			? (PlanetFrame.OffsetOf(Marker.WorldLocation) - PlayerOnFrame) * PixelsPerWorldUnit
			: (Marker.WorldPosition - Player2D) * PixelsPerWorldUnit;
		const FVector2D Rotated = PixelOffset.GetRotated(MapAngleDeg);
		if (Rotated.SizeSquared() > VisibleRadiusPx * VisibleRadiusPx)
		{
			continue;
		}

		// Pool: create on demand, reuse thereafter.
		if (!MarkerDots.IsValidIndex(Used))
		{
			UImage* Dot = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
			Dot->SetDesiredSizeOverride(FVector2D(MarkerDotSize, MarkerDotSize));
			if (UCanvasPanelSlot* DotSlot = MapCanvas->AddChildToCanvas(Dot))
			{
				DotSlot->SetAnchors(FAnchors(0.5f, 0.5f, 0.5f, 0.5f));
				DotSlot->SetAlignment(FVector2D(0.5f, 0.5f));
				DotSlot->SetAutoSize(true);
			}
			MarkerDots.Add(Dot);
		}

		UImage* Dot = MarkerDots[Used];
		Dot->SetVisibility(ESlateVisibility::HitTestInvisible);
		Dot->SetColorAndOpacity(Marker.Color);
		if (UCanvasPanelSlot* DotSlot = Cast<UCanvasPanelSlot>(Dot->Slot))
		{
			DotSlot->SetPosition(Rotated);
		}
		++Used;
	}

	// Hide the unused tail of the pool.
	for (int32 i = Used; i < MarkerDots.Num(); ++i)
	{
		MarkerDots[i]->SetVisibility(ESlateVisibility::Collapsed);
	}
}

// ---------------------------------------------------------------------------
// Texture Management
// ---------------------------------------------------------------------------

void UVCMinimapWidget::EnsureTexture(int32 TexSize)
{
	if (MapTexture && CurrentTextureSize == TexSize)
	{
		return;
	}

	MapTexture = UTexture2D::CreateTransient(TexSize, TexSize, PF_B8G8R8A8, TEXT("MinimapTexture"));
	if (!MapTexture)
	{
		return;
	}

	MapTexture->Filter = TF_Bilinear;
	MapTexture->SRGB = true;
	MapTexture->CompressionSettings = TC_VectorDisplacementmap;
	MapTexture->AddressX = TA_Clamp;
	MapTexture->AddressY = TA_Clamp;

	CurrentTextureSize = TexSize;
}

double UVCMinimapWidget::GetWorldPerPixel() const
{
	return 2.0 * FMath::Max(MinimapWorldRadius, 1.0f) / FMath::Max(MinimapSize, 1.0f);
}

int32 UVCMinimapWidget::GetTextureSize() const
{
	return FMath::CeilToInt(MinimapSize * UE_SQRT_2) + 2 * (FMath::CeilToInt(RefillShiftTexels) + 1);
}

FVCPlanetMapFrame UVCMinimapWidget::GetDisplayFrame(const FVector& PlayerPos) const
{
	const UVoxelMapSubsystem* Subsystem = MapSubsystem.Get();
	if (!Subsystem)
	{
		return FVCPlanetMapFrame();
	}
	return FVCPlanetMapFrame::At(Subsystem->GetPlanetCenter(), Subsystem->GetPlanetRadius(), bHasFill ? FillCenter : PlayerPos);
}

FVector2D UVCMinimapWidget::GetFillOffsetTexels(const FVector& PlayerPos) const
{
	if (!bHasFill)
	{
		return FVector2D::ZeroVector;
	}
	const double WorldPerPixel = GetWorldPerPixel();
	const UVoxelMapSubsystem* Subsystem = MapSubsystem.Get();
	if (Subsystem && Subsystem->IsPlanetMap())
	{
		const FVCPlanetMapFrame Frame = FVCPlanetMapFrame::At(Subsystem->GetPlanetCenter(), Subsystem->GetPlanetRadius(), FillCenter);
		return Frame.OffsetOf(PlayerPos) / WorldPerPixel;
	}
	return FVector2D(PlayerPos.X - FillCenter.X, PlayerPos.Y - FillCenter.Y) / WorldPerPixel;
}

uint64 UVCMinimapWidget::ComputeTileSignature() const
{
	const UVoxelMapSubsystem* Subsystem = MapSubsystem.Get();
	uint64 Signature = 1469598103934665603ull;
	auto Mix = [&Signature](uint64 Value) { Signature = (Signature ^ Value) * 1099511628211ull; };
	if (!Subsystem)
	{
		return Signature;
	}
	for (const FIntPoint& Coord : FillTiles)
	{
		// A tile drawn again replaces its pixel buffer; one dropped from the store is gone.
		const FVoxelMapTile* Tile = Subsystem->GetTile(Coord);
		Mix(static_cast<uint64>(reinterpret_cast<UPTRINT>(Tile)));
		Mix(Tile ? static_cast<uint64>(reinterpret_cast<UPTRINT>(Tile->PixelData.GetData())) : 0);
		Mix(Tile ? static_cast<uint64>(Tile->Resolution) : 0);
	}
	return Signature;
}

bool UVCMinimapWidget::NeedsRefill(const FVector& PlayerPos, double Now) const
{
	if (CVarMinimapSkipUnchanged.GetValueOnGameThread() == 0)
	{
		return true;
	}
	if (!bHasFill || !MapTexture || CurrentTextureSize != GetTextureSize())
	{
		return true;
	}
	if (Now - LastFillTime >= MaxRefillSeconds)
	{
		return true;
	}
	if (GetFillOffsetTexels(PlayerPos).Size() >= RefillShiftTexels)
	{
		return true;
	}
	return ComputeTileSignature() != FillTileSignature;
}

void UVCMinimapWidget::RefreshMapTexture(const FVector& Center)
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

	// Texture size matches the oversized display size exactly — no scaling. The image is sqrt(2) larger than the visible
	// square (plus the shift margin) so it fills the corners at any rotation angle. The SizeBox clips it to MinimapSize.
	const int32 TexSize = GetTextureSize();
	const double WorldPerPixel = GetWorldPerPixel();
	const double ViewWorldExtent = 0.5 * TexSize * WorldPerPixel;

	EnsureTexture(TexSize);
	if (!MapTexture)
	{
		return;
	}

	// Lock texture for writing
	const uint64 FillStart = FPlatformTime::Cycles64();
	TRACE_CPUPROFILER_EVENT_SCOPE(VCMinimap_Fill);
	FTexture2DMipMap& Mip = MapTexture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	VCMinimapStats::Add(VCMinimapStats::Lock, FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - FillStart));
	if (!TextureData)
	{
		return;
	}
	uint8* PixelData = static_cast<uint8*>(TextureData);
	TSet<FIntPoint> Visited;

	if (Subsystem->IsPlanetMap())
	{
		// A planet (spherical Phase 4, WM-SY9): texel (X, Y) is the tangent plane's point (north, east) = (X, Y) world
		// units per pixel from the corner, as world (X, Y) on a plane, so the rotation carries over.
		const FVCPlanetMapFrame Frame = FVCPlanetMapFrame::At(Subsystem->GetPlanetCenter(), Subsystem->GetPlanetRadius(), Center);
		FVCPlanetMapSampler Sampler;
		Sampler.bRequireExplored = false; // the minimap's area is requested (explored) every refresh
		Sampler.Unexplored = Sampler.Undrawn = Sampler.Background = FColor(5, 5, 5, 255);
		Sampler.VisitedTiles = &Visited;
		Sampler.Fill(*Subsystem, PixelData, TexSize, 16, [&Frame, WorldPerPixel, ViewWorldExtent](double X, double Y, FVector& OutDirection)
		{
			OutDirection = Frame.DirectionAtOffset(X * WorldPerPixel - ViewWorldExtent, Y * WorldPerPixel - ViewWorldExtent);
			return true;
		});
	}
	else
	{
		// A plane: texel by texel (FVCPlaneMapSampler, G64), or the blit of every tile pixel as before.
		FVCPlaneMapSampler Sampler;
		Sampler.bTexelFill = CVarMinimapTexelFill.GetValueOnGameThread() != 0;
		Sampler.VisitedTiles = &Visited;
		Sampler.Fill(*Subsystem, PixelData, TexSize, Center, WorldPerPixel);
	}
	VCMinimapStats::Add(VCMinimapStats::Fill, FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - FillStart));
	++VCMinimapStats::Fills;

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(VCMinimap_Upload);
		const VCMinimapStats::FScope Timed(VCMinimapStats::Upload);
		Mip.BulkData.Unlock();
		MapTexture->UpdateResource();

		// Display at 1:1 pixel ratio — texture size matches display size exactly. The canvas slot with AutoSize respects
		// this, and the SizeBox clips the overflow to the visible MinimapSize square.
		MapImage->SetBrushFromTexture(MapTexture);
		MapImage->SetDesiredSizeOverride(FVector2D(static_cast<float>(TexSize), static_cast<float>(TexSize)));
	}

	FillCenter = Center;
	bHasFill = true;
	LastFillTime = FPlatformTime::Seconds();
	FillTiles = Visited.Array();
	FillTileSignature = ComputeTileSignature();
}
