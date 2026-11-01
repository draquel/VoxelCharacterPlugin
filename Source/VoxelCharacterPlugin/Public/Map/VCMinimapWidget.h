// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Map/VCMapSamplers.h"
#include "VCMinimapWidget.generated.h"

class UVoxelMapSubsystem;
class UVCMapMarkerRegistry;
class UImage;
class UTextBlock;
class UCanvasPanel;
class USizeBox;

/**
 * Always-visible minimap widget showing nearby terrain from above.
 *
 * Renders a square view of voxel terrain around the player using
 * tile data from UVoxelMapSubsystem. The map rotates with the
 * player's camera heading so "forward" always points up.
 *
 * The map image is oversized by sqrt(2) so it fills the visible square
 * at any rotation angle. A SizeBox with ClipToBounds crops the result.
 *
 * Widget tree is built programmatically in NativeOnInitialized()
 * following the project's C++ widget construction pattern.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCMinimapWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Widget pixel size on screen (square). */
	UPROPERTY(EditDefaultsOnly, Category = "Minimap")
	float MinimapSize = 200.0f;

	/** World units visible from center to edge of the minimap. */
	UPROPERTY(EditDefaultsOnly, Category = "Minimap")
	float MinimapWorldRadius = 16000.0f;

	/** Seconds between texture refreshes (throttle). */
	UPROPERTY(EditDefaultsOnly, Category = "Minimap")
	float UpdateInterval = 0.1f;

	/**
	 * Texels the player may move from where the texture was last filled before it is filled again (world modes review
	 * G64); until then the image is shifted by that movement, so the map stays glued to the terrain. The texture carries
	 * this margin beyond the rotation's.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Minimap", meta = (ClampMin = "0.0"))
	float RefillShiftTexels = 2.0f;

	/** The texture is filled again at least this often (seconds), whether or not anything it shows changed (G64). */
	UPROPERTY(EditDefaultsOnly, Category = "Minimap", meta = (ClampMin = "0.1"))
	float MaxRefillSeconds = 2.0f;

	/** Max markers drawn on the minimap (highest priority first; dots only, no labels). */
	UPROPERTY(EditDefaultsOnly, Category = "Minimap")
	int32 MaxMarkers = 24;

	/** Marker dot size in pixels. */
	UPROPERTY(EditDefaultsOnly, Category = "Minimap")
	float MarkerDotSize = 6.0f;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	/** Build the widget tree programmatically. */
	void BuildWidgetTree();

	/**
	 * Fill the map texture from tile data around Center and upload it (the costly step, G64). Records the tiles it read
	 * (FillTiles) and their signature.
	 *
	 * @param Center The world position the texture is centred on (the player's at the fill)
	 */
	void RefreshMapTexture(const FVector& Center);

	/**
	 * Whether the texture must be filled again (G64): none yet, its size changed, MaxRefillSeconds passed, the player
	 * moved RefillShiftTexels from the fill's centre, or an image of a tile it shows changed.
	 *
	 * @param PlayerPos The player's position
	 * @param Now       Seconds (platform time)
	 * @return True to fill
	 */
	bool NeedsRefill(const FVector& PlayerPos, double Now) const;

	/** A signature of the tile images the last fill read (each tile's entry, its pixel buffer and resolution, G64). */
	uint64 ComputeTileSignature() const;

	/** World units per texel, which is also per screen pixel: 2 * MinimapWorldRadius / MinimapSize. */
	double GetWorldPerPixel() const;

	/** Texture size: the visible square's diagonal (it rotates) plus the shift margin on each side. */
	int32 GetTextureSize() const;

	/**
	 * The player's offset from the last fill's centre, in texels along the texture's axes (world X, Y on a plane; north,
	 * east on the tangent plane at the fill's centre on a planet).
	 */
	FVector2D GetFillOffsetTexels(const FVector& PlayerPos) const;

	/**
	 * On a planet, the tangent frame the shown texture was drawn in (the last fill's centre; the player's before the first
	 * fill). The heading and the markers are taken in it, so they stay consistent with the image between fills even near
	 * a pole, where neighbouring frames turn against each other.
	 */
	FVCPlanetMapFrame GetDisplayFrame(const FVector& PlayerPos) const;

	/** Create or recreate the dynamic texture. */
	void EnsureTexture(int32 TexSize);

	/**
	 * Refresh marker dots from the marker registry: world offsets from the player are scaled to
	 * pixels and rotated by the same angle as the map image, so dots stay glued to the terrain.
	 */
	void UpdateMarkers(const FVector& PlayerPos, float MapAngleDeg);

	// Widget tree references
	UPROPERTY()
	TObjectPtr<USizeBox> RootSizeBox;

	UPROPERTY()
	TObjectPtr<UCanvasPanel> MapCanvas;

	UPROPERTY()
	TObjectPtr<UImage> MapBackground;

	UPROPERTY()
	TObjectPtr<UImage> MapImage;

	UPROPERTY()
	TObjectPtr<UImage> PlayerArrow;

	UPROPERTY()
	TObjectPtr<UTextBlock> CoordinateText;

	UPROPERTY()
	TObjectPtr<UTextBlock> NorthIndicator;

	// Runtime state
	UPROPERTY()
	TObjectPtr<UTexture2D> MapTexture;

	/** Pooled marker dot widgets (created on demand, hidden when unused). */
	UPROPERTY()
	TArray<TObjectPtr<UImage>> MarkerDots;

	TWeakObjectPtr<UVoxelMapSubsystem> MapSubsystem;
	TWeakObjectPtr<UVCMapMarkerRegistry> MarkerRegistry;
	float TimeSinceLastUpdate = 0.0f;
	int32 CurrentTextureSize = 0;

	// The last fill (G64): where it was centred, when, the tiles it read and their signature.
	FVector FillCenter = FVector::ZeroVector;
	bool bHasFill = false;
	double LastFillTime = 0.0;
	uint64 FillTileSignature = 0;
	TArray<FIntPoint> FillTiles;
};
