// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VCMinimapWidget.generated.h"

class UVoxelMapSubsystem;
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

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

private:
	/** Build the widget tree programmatically. */
	void BuildWidgetTree();

	/** Rebuild the map texture from tile data. */
	void RefreshMapTexture();

	/** Create or recreate the dynamic texture. */
	void EnsureTexture(int32 TexSize);

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

	TWeakObjectPtr<UVoxelMapSubsystem> MapSubsystem;
	float TimeSinceLastUpdate = 0.0f;
	int32 CurrentTextureSize = 0;
};
