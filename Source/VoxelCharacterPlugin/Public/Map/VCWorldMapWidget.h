// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Map/VCMapSamplers.h"
#include "VCWorldMapWidget.generated.h"

class UVoxelMapSubsystem;
class UVCMapMarkerRegistry;
class UImage;
class UTextBlock;
class UCanvasPanel;

/**
 * The world map's redraws for tile arrivals (MAP_PYRAMID_PLAN.md P1, G166): only the arrival of a tile the view showed (its
 * last request, at its level) counts, and those arrivals coalesce into at most one redraw every Interval seconds, so a
 * view's few hundred tiles landing one by one redraw the map a few times a second, not once per tile. A redraw that cost
 * C seconds waits CostFactor x C before the next (from its start), so redraws for arrivals hold at most about a fifth of
 * the game thread however slow the fill (a tile a texel on a fully explored 50 km planet: 0.8 s a fill, which held the
 * drawing to the frame rate and redrew the map 41 times while the view's tiles arrived). Pure.
 */
struct VOXELCHARACTERPLUGIN_API FVCMapArrivalCoalescer
{
	/** Seconds between redraws for arrivals, at least. */
	double Interval = 0.25;

	/** A redraw costing C seconds waits this many times C (from its start) before the next for arrivals. */
	double CostFactor = 4.0;

	/**
	 * The view as last requested.
	 * @param InLevel Its pyramid level
	 * @param Tiles Its tiles' coordinates at that level
	 */
	void SetView(int32 InLevel, TConstArrayView<FIntPoint> Tiles);

	/**
	 * A tile's image arrived.
	 * @param InLevel Its level
	 * @param Tile Its coordinate at that level
	 * @return True if it is in the view (a redraw is now pending); false if it does not show (ignored)
	 */
	bool OnArrival(int32 InLevel, const FIntPoint& Tile);

	/** @return True when a redraw for arrivals is pending and due at Now (seconds) */
	bool IsDue(double Now) const { return bPending && Now - LastRedrawSeconds >= GetMinGap(); }

	/** @return The seconds a redraw for arrivals waits after the last one's start: Interval, or CostFactor times its cost */
	double GetMinGap() const { return FMath::Max(Interval, CostFactor * LastRedrawCost); }

	/**
	 * The map was redrawn, for whatever reason: the pending arrivals show.
	 * @param Now When the redraw started (seconds)
	 * @param CostSeconds What it cost
	 */
	void OnRedrawn(double Now, double CostSeconds = 0.0)
	{
		bPending = false;
		LastRedrawSeconds = Now;
		LastRedrawCost = CostSeconds;
	}

	/** @return Whether arrivals in view wait for a redraw */
	bool IsPending() const { return bPending; }

private:
	int32 Level = -1;
	TSet<FIntPoint> ViewTiles;
	bool bPending = false;
	double LastRedrawSeconds = -1.0e9;
	double LastRedrawCost = 0.0;
};

/**
 * Full-screen world map overlay toggled with M key.
 *
 * Shows all explored terrain with fog of war on unexplored areas.
 * Supports mouse zoom/pan for navigation and shows a player position marker.
 *
 * On a planet (spherical Phase 4, D11, WM-SY9) the map is a globe: an orthographic view around a direction, north up,
 * opening on the player; dragging turns the globe, the wheel zooms (the same scale as a plane's map at each zoom, so it
 * becomes a local map zoomed in).
 *
 * The image is composed from the map's tile pyramid at the level matching its texels (MAP_PYRAMID_PLAN.md P1): about 300
 * tiles at any zoom, asked for nearest the view's centre (UVoxelMapSubsystem::RequestView), the coarser stored levels
 * shown while they draw; their arrivals redraw it coalesced (FVCMapArrivalCoalescer).
 *
 * Widget tree is built programmatically in NativeOnInitialized()
 * following the project's C++ widget construction pattern.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCWorldMapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Minimum zoom level (most zoomed out — more tiles visible). */
	UPROPERTY(EditDefaultsOnly, Category = "WorldMap")
	float MinZoom = 0.25f;

	/** Maximum zoom level (most zoomed in — fewer tiles visible). */
	UPROPERTY(EditDefaultsOnly, Category = "WorldMap")
	float MaxZoom = 4.0f;

	/** Current zoom level (1.0 = default view). Higher = more zoomed in. */
	UPROPERTY(BlueprintReadOnly, Category = "WorldMap")
	float CurrentZoom = 1.0f;

	/** Fixed texture size for the world map (square). Clamped to keep memory reasonable. */
	UPROPERTY(EditDefaultsOnly, Category = "WorldMap")
	int32 MapTextureFixedSize = 1024;

	/** Max markers drawn on the world map (highest priority first). */
	UPROPERTY(EditDefaultsOnly, Category = "WorldMap")
	int32 MaxMarkers = 64;

	/** Marker dot size in pixels. */
	UPROPERTY(EditDefaultsOnly, Category = "WorldMap")
	float MarkerDotSize = 8.0f;

	/** Seconds between marker refreshes (gathering runs deterministic placement over the view). */
	UPROPERTY(EditDefaultsOnly, Category = "WorldMap")
	float MarkerUpdateInterval = 0.5f;

	/** Refresh the map texture from subsystem data. Call after opening or when new tiles arrive. */
	void RefreshMap();

	/**
	 * Pure: the world map's zoom range on a planet (spherical Phase 6, WM-BP11): out to the whole globe with a tenth of the
	 * view to spare at any radius (MinZoom showed a whole globe only up to R of about 5 x 10^5), in to MaxZoom (the plane's
	 * scale at each zoom: zoom 1 shows 32 tiles from the centre to the texture's edge).
	 * @param PlanetRadius Datum radius
	 * @param TileWorldSize A tile's edge (uu)
	 * @param InMinZoom The configured MinZoom (kept when it already shows the globe)
	 * @param InMaxZoom The configured MaxZoom
	 * @param OutMinZoom Most zoomed out
	 * @param OutMaxZoom Most zoomed in
	 */
	static void GetPlanetZoomRange(double PlanetRadius, double TileWorldSize, float InMinZoom, float InMaxZoom, float& OutMinZoom, float& OutMaxZoom);

	/** Zoomed out until a texel spans more tiles than 1 / this, the globe is drawn from the planet's albedo (WM-BP11). */
	static constexpr double GlobeAlbedoTexelsPerTile = 0.5;

	/**
	 * The zoom range in force: MinZoom to MaxZoom, out to the whole globe on a planet (GetPlanetZoomRange).
	 * @param OutMinZoom Most zoomed out
	 * @param OutMaxZoom Most zoomed in
	 */
	void GetZoomRange(float& OutMinZoom, float& OutMaxZoom) const;

	/**
	 * Set the zoom (QA: vox.Map), clamped to the zoom range; below 0 the most zoomed out.
	 * @param Zoom The zoom (1 = the default view, higher = closer)
	 */
	UFUNCTION(BlueprintCallable, Category = "WorldMap")
	void SetZoom(float Zoom);

	/**
	 * The world map open in a world (in the viewport), if any (QA: vox.Map).
	 * @param World The world
	 * @return The open map, or null
	 */
	static UVCWorldMapWidget* FindOpen(const UWorld* World);

	/** The fog of war follows new exploration at most this often (seconds; the minimap explores as the player walks). */
	static constexpr double ExplorationRedrawSeconds = 1.0;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	/** Build the widget tree programmatically. */
	void BuildWidgetTree();

	/** Rebuild the map texture to show tiles around PanOffset at CurrentZoom. */
	void RebuildMapTexture();

	/** RebuildMapTexture on a planet: the globe around GlobeView. */
	void RebuildPlanetMapTexture();

	/** The globe's radius on screen at the current zoom (the plane's scale: BaseWorldExtent / CurrentZoom per half texture). */
	double GetGlobeRadiusPx() const;

	/**
	 * Ask the map for the tiles the view shows (UVoxelMapSubsystem::RequestView), replacing the world map's last request.
	 * @param Level Their pyramid level
	 * @param Tiles Their coordinates, nearest the view's centre first
	 */
	void RequestViewTiles(int32 Level, const TArray<FIntPoint>& Tiles);

	/** Update the player marker position on the map. */
	void UpdatePlayerMarker();

	/** Refresh registry markers (dots + labels) over the rendered view (throttled). */
	void UpdateMarkers();

	// Widget tree references
	UPROPERTY()
	TObjectPtr<UCanvasPanel> RootCanvas;

	UPROPERTY()
	TObjectPtr<UImage> MapBackground;

	UPROPERTY()
	TObjectPtr<UImage> MapImage;

	UPROPERTY()
	TObjectPtr<UImage> PlayerMarker;

	UPROPERTY()
	TObjectPtr<UTextBlock> MapCoordinateText;

	// Runtime state
	UPROPERTY()
	TObjectPtr<UTexture2D> WorldMapTexture;

	/** Pooled marker widgets (created on demand, hidden when unused). */
	UPROPERTY()
	TArray<TObjectPtr<UImage>> MarkerDots;

	UPROPERTY()
	TArray<TObjectPtr<UTextBlock>> MarkerLabels;

	TWeakObjectPtr<UVoxelMapSubsystem> MapSubsystem;
	TWeakObjectPtr<UVCMapMarkerRegistry> MarkerRegistry;
	float TimeSinceMarkerUpdate = 1000.0f; // refresh immediately on open

	/** Pan offset in world units (center of the map view). */
	FVector2D PanOffset = FVector2D::ZeroVector;

	/** Whether user is currently dragging to pan. */
	bool bIsPanning = false;

	/** Last mouse position during pan. */
	FVector2D LastMousePos = FVector2D::ZeroVector;

	/** Delegate handles for tile and globe albedo arrivals. */
	FDelegateHandle TileReadyHandle;
	FDelegateHandle AlbedoReadyHandle;

	/** Arrivals in view, coalesced into redraws (G166). */
	FVCMapArrivalCoalescer Arrivals;

	/** The map's exploration revision at the last rebuild: the fog of war redraws when it moves on. */
	uint64 RenderedExplorationRevision = 0;

	/** Whether the map needs a texture rebuild. */
	bool bMapDirty = true;

	/** A planet's view: the direction at the globe's centre (spherical Phase 4). */
	FVector GlobeView = FVector::UpVector;

	/** The globe as last drawn (planets). */
	FVCGlobeProjection RenderedGlobe;
	bool bRenderedPlanet = false;

	/** Real time of the last rebuild: a planet's redraws for dragging run at most 30 times a second. */
	double LastRebuildSeconds = 0.0;

	// Cached from last render — used by player marker positioning
	FIntPoint RenderedCenterTile = FIntPoint(0, 0);
	int32 RenderedTileRadius = 0;
	int32 RenderedTexSize = 0;
	float RenderedWorldPerPixel = 0.0f;
};
