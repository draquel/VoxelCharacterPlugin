// Copyright Daniel Raquel. All Rights Reserved.

#include "Map/VCMinimapWidget.h"
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

// sqrt(2) — the map texture is this much larger than the visible square
// so that when rotated 45 degrees the corners still fill the entire area.
static constexpr float RotationOversize = 1.42f;

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

	// Request tiles for the minimap's visible area (oversized for rotation)
	MapSubsystem->RequestTilesInRadius(PlayerPos, MinimapWorldRadius * RotationOversize);

	// Update coordinate text — display in voxel coordinates (divide by VoxelSize)
	if (CoordinateText)
	{
		const float VoxelSize = MapSubsystem->GetTileWorldSize() / FMath::Max(1, MapSubsystem->GetTileResolution());
		const int32 VoxelX = FMath::RoundToInt(PlayerPos.X / VoxelSize);
		const int32 VoxelY = FMath::RoundToInt(PlayerPos.Y / VoxelSize);
		CoordinateText->SetText(FText::FromString(
			FString::Printf(TEXT("X: %d  Y: %d"), VoxelX, VoxelY)
		));
	}

	// Refresh the map texture
	RefreshMapTexture();

	// Rotate map so the player's forward direction points up on the minimap.
	// UE yaw=0 is +X. The texture maps +X to the right, so a -90 offset
	// rotates +X (forward at yaw=0) from right to up.
	FRotator CameraRot;
	FVector CameraLoc;
	PC->GetPlayerViewPoint(CameraLoc, CameraRot);

	if (MapImage)
	{
		MapImage->SetRenderTransformAngle(-CameraRot.Yaw - 90.0f);
	}

	// Position the north indicator on the minimap edge.
	// North = +X axis (yaw=0 direction). As the camera rotates, north's
	// position on the minimap orbits around the center.
	if (NorthIndicator)
	{
		const float NorthAngleRad = FMath::DegreesToRadians(-CameraRot.Yaw);
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

void UVCMinimapWidget::RefreshMapTexture()
{
	UVoxelMapSubsystem* Subsystem = MapSubsystem.Get();
	if (!Subsystem || !MapImage)
	{
		return;
	}

	const APlayerController* PC = GetOwningPlayer();
	if (!PC || !PC->GetPawn())
	{
		return;
	}

	const FVector PlayerPos = PC->GetPawn()->GetActorLocation();
	const float TileWorldSize = Subsystem->GetTileWorldSize();
	const int32 TileResolution = Subsystem->GetTileResolution();
	if (TileWorldSize <= 0.f || TileResolution <= 0)
	{
		return;
	}

	// Texture size matches the oversized display size exactly — no scaling.
	// The image is sqrt(2) larger than the visible square so it fills the
	// corners at any rotation angle. The SizeBox clips it to MinimapSize.
	const int32 TexSize = FMath::CeilToInt(MinimapSize * RotationOversize);
	const float ViewWorldExtent = MinimapWorldRadius * RotationOversize;
	const float WorldPerPixel = (ViewWorldExtent * 2.0f) / static_cast<float>(TexSize);

	EnsureTexture(TexSize);
	if (!MapTexture)
	{
		return;
	}

	// Lock texture for writing
	FTexture2DMipMap& Mip = MapTexture->GetPlatformData()->Mips[0];
	void* TextureData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	if (!TextureData)
	{
		return;
	}

	uint8* PixelData = static_cast<uint8*>(TextureData);
	const int32 TotalPixels = TexSize * TexSize;

	// Clear to dark background (matches border color)
	for (int32 i = 0; i < TotalPixels; ++i)
	{
		PixelData[i * 4 + 0] = 5;   // B
		PixelData[i * 4 + 1] = 5;   // G
		PixelData[i * 4 + 2] = 5;   // R
		PixelData[i * 4 + 3] = 255; // A
	}

	// World bounds of the texture, centered on player
	const float WorldMinX = PlayerPos.X - ViewWorldExtent;
	const float WorldMinY = PlayerPos.Y - ViewWorldExtent;

	// How many tiles from center to edge we need to iterate
	const FIntPoint CenterTile = Subsystem->WorldToTileCoord(PlayerPos);
	const int32 TileRadius = FMath::CeilToInt(ViewWorldExtent / TileWorldSize);

	// Blit tile data — map each tile's source pixels to destination via world coords
	for (int32 TY = -TileRadius; TY <= TileRadius; ++TY)
	{
		for (int32 TX = -TileRadius; TX <= TileRadius; ++TX)
		{
			const FIntPoint TileCoord(CenterTile.X + TX, CenterTile.Y + TY);
			const FVoxelMapTile* Tile = Subsystem->GetTile(TileCoord);
			if (!Tile)
			{
				continue;
			}

			const FVector TileWorldOrigin = Subsystem->TileCoordToWorld(TileCoord);
			const int32 SrcRes = Tile->Resolution;
			const float SrcPixelWorldSize = TileWorldSize / SrcRes;

			for (int32 PY = 0; PY < SrcRes; ++PY)
			{
				for (int32 PX = 0; PX < SrcRes; ++PX)
				{
					const float PixWorldX = TileWorldOrigin.X + PX * SrcPixelWorldSize;
					const float PixWorldY = TileWorldOrigin.Y + PY * SrcPixelWorldSize;

					const int32 DstX = FMath::FloorToInt((PixWorldX - WorldMinX) / WorldPerPixel);
					const int32 DstY = FMath::FloorToInt((PixWorldY - WorldMinY) / WorldPerPixel);

					if (DstX < 0 || DstX >= TexSize || DstY < 0 || DstY >= TexSize)
					{
						continue;
					}

					const FColor& SrcColor = Tile->PixelData[PY * SrcRes + PX];
					const int32 DstIdx = (DstY * TexSize + DstX) * 4;
					PixelData[DstIdx + 0] = SrcColor.B;
					PixelData[DstIdx + 1] = SrcColor.G;
					PixelData[DstIdx + 2] = SrcColor.R;
					PixelData[DstIdx + 3] = SrcColor.A;
				}
			}
		}
	}

	Mip.BulkData.Unlock();
	MapTexture->UpdateResource();

	// Display at 1:1 pixel ratio — texture size matches display size exactly.
	// The canvas slot with AutoSize respects this, and the SizeBox clips
	// the overflow to the visible MinimapSize square.
	MapImage->SetBrushFromTexture(MapTexture);
	MapImage->SetDesiredSizeOverride(FVector2D(static_cast<float>(TexSize), static_cast<float>(TexSize)));
}
