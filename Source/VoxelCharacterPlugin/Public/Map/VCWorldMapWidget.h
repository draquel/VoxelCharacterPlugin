// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VCWorldMapWidget.generated.h"

class UVoxelMapSubsystem;
class UVCMapMarkerRegistry;
class UImage;
class UTextBlock;
class UCanvasPanel;

/**
 * Full-screen world map overlay toggled with M key.
 *
 * Shows all explored terrain with fog of war on unexplored areas.
 * Supports mouse zoom/pan for navigation and shows a player position marker.
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

	/** Delegate handle for tile ready events. */
	FDelegateHandle TileReadyHandle;

	/** Whether the map needs a texture rebuild. */
	bool bMapDirty = true;

	// Cached from last render — used by player marker positioning
	FIntPoint RenderedCenterTile = FIntPoint(0, 0);
	int32 RenderedTileRadius = 0;
	int32 RenderedTexSize = 0;
	float RenderedWorldPerPixel = 0.0f;
};
