# Map UI Widgets

**Module**: VoxelCharacterPlugin
**Dependencies**: VoxelMap (data), UMG (rendering)

## Overview

Two map widgets provide spatial awareness in the voxel world: a minimap for constant at-a-glance navigation, and a full-screen world map for exploration review. Both consume tile data from `UVoxelMapSubsystem` (VoxelMap module) and render it onto dynamic `UTexture2D` instances.

Widget trees are built programmatically in `NativeOnInitialized()` following the project's C++ widget construction pattern — no UMG Designer `.uasset` files.

## Widget Summary

| Widget | Class | Purpose | Visibility |
|--------|-------|---------|------------|
| Minimap | `UVCMinimapWidget` | Small rotating terrain view near player | Always visible (top-right) |
| World Map | `UVCWorldMapWidget` | Full-screen explored terrain with zoom/pan | Toggle with M key |

## UVCMinimapWidget

### Widget Tree

```
USizeBox (MinimapSize x MinimapSize, ClipToBounds)
└── UCanvasPanel
    ├── UImage [MapBackground]    — dark fill, anchored 0,0→1,1
    ├── UImage [MapImage]         — terrain texture, centered, oversized, rotates
    ├── UImage [PlayerArrow]      — 8x8 white dot at center
    ├── UTextBlock [CoordinateText] — "X: 123  Y: 456" at bottom center
    └── UTextBlock [NorthIndicator] — red "N", orbits minimap edge
```

### Rotation and Clipping

The map image rotates with the player's camera heading so "forward" always points up on the minimap. To prevent gaps at the corners during rotation, the texture is oversized by a factor of sqrt(2) (~1.42):

```
Visible square:    200 x 200 pixels (MinimapSize)
Texture size:      284 x 284 pixels (MinimapSize * 1.42)
```

The `USizeBox` has `ClipToBounds` enabled, which clips the oversized rotated image to the visible square. `UCanvasPanel` is used instead of `UOverlay` because canvas slots with `SetAutoSize(true)` respect `SetDesiredSizeOverride`, allowing children to extend beyond the parent bounds.

**Rotation formula:**
```cpp
MapImage->SetRenderTransformAngle(-CameraRot.Yaw - 90.0f);
```
The -90 offset rotates UE's +X axis (yaw=0 = forward) from the right side to the top of the minimap.

### North Indicator

A red "N" text orbits the minimap edge using trigonometric positioning:

```cpp
const float NorthAngleRad = FMath::DegreesToRadians(-CameraRot.Yaw);
const float Radius = MinimapSize * 0.40f;
NX = Sin(NorthAngleRad) * Radius;
NY = -Cos(NorthAngleRad) * Radius;
```

When the camera faces north (yaw=0), "N" appears at the top. As the camera rotates, "N" orbits the edge to always indicate true north.

### Texture Rendering

`RefreshMapTexture()` runs on a throttled tick (default 10 Hz via `UpdateInterval = 0.1`):

1. Get player world position from `GetOwningPlayer()->GetPawn()`
2. Call `MapSubsystem->RequestTilesInRadius()` for predictive tile generation
3. Calculate visible world area: `ViewWorldExtent = MinimapWorldRadius * 1.42`
4. Lock the transient `UTexture2D` mip 0 bulk data
5. Clear to dark background
6. Iterate tiles in radius, blit each tile's `PixelData` at the correct world-to-pixel offset
7. Unlock and `UpdateResource()`
8. Apply via `SetBrushFromTexture()` + `SetDesiredSizeOverride()`

### Configuration

| Property | Default | Description |
|----------|---------|-------------|
| `MinimapSize` | 200.0 | Widget pixel size on screen (square) |
| `MinimapWorldRadius` | 16000.0 | World units visible from center to edge |
| `UpdateInterval` | 0.1 | Seconds between texture refreshes |

### Coordinate Display

The coordinate text shows the player's position in voxel coordinates:
```
VoxelX = RoundToInt(PlayerPos.X / VoxelSize)
VoxelY = RoundToInt(PlayerPos.Y / VoxelSize)
```

Where `VoxelSize = TileWorldSize / TileResolution`.

## UVCWorldMapWidget

### Widget Tree

```
UCanvasPanel (full screen)
├── UImage [MapBackground]      — dark semi-transparent fill
├── UImage [MapImage]           — terrain texture, centered, auto-sized
├── UImage [PlayerMarker]       — 12x12 red dot, positioned per-tick
└── UTextBlock [MapCoordinateText] — "Player: X=123  Y=456  |  Zoom: 1.0x" at bottom
```

### Zoom and Pan

The world map supports interactive navigation:

**Zoom:** Mouse wheel scales `CurrentZoom` by 1.25x (in) or 0.8x (out), clamped to `[MinZoom, MaxZoom]`. Higher zoom = fewer tiles visible = more detail.

**Pan:** Click-and-drag converts screen pixel deltas to world coordinate offsets:
```cpp
PanOffset.X -= Delta.X * RenderedWorldPerPixel;
PanOffset.Y -= Delta.Y * RenderedWorldPerPixel;
```

**Base view:** At `CurrentZoom = 1.0`, the texture covers 32 tiles per side. The view extent scales inversely with zoom: `ViewWorldExtent = BaseWorldExtent / CurrentZoom`.

### Fog of War

The world map only renders explored tiles. Unexplored areas within the view appear as dark fog:

| State | Rendering |
|-------|-----------|
| Unexplored | Dark background (RGB: 10, 10, 10) |
| Explored, tile not yet generated | Slightly lighter fog (RGB: 25, 25, 25) |
| Explored, tile generated | Full terrain colors from tile pixel data |

Exploration state is queried from `UVoxelMapSubsystem::IsTileExplored()`.

### Texture Rendering

`RebuildMapTexture()` is triggered by the `bMapDirty` flag, set when:
- The widget opens (`NativeConstruct`)
- A new tile finishes generating (`OnMapTileReady` delegate)
- The user zooms or pans

The rendering process mirrors the minimap's tile blitting but operates at a larger fixed texture size (`MapTextureFixedSize`, default 1024) and includes fog of war logic.

### Player Marker

`UpdatePlayerMarker()` runs every tick to position the red dot:
```cpp
PixelOffsetX = (PlayerPos.X - PanOffset.X) / RenderedWorldPerPixel;
PixelOffsetY = (PlayerPos.Y - PanOffset.Y) / RenderedWorldPerPixel;
```

The marker's canvas slot position is set relative to the center anchor (0.5, 0.5).

### Configuration

| Property | Default | Description |
|----------|---------|-------------|
| `MinZoom` | 0.25 | Minimum zoom (most zoomed out) |
| `MaxZoom` | 4.0 | Maximum zoom (most zoomed in) |
| `CurrentZoom` | 1.0 | Current zoom level |
| `MapTextureFixedSize` | 1024 | Texture resolution (clamped 256-2048) |

### Lifecycle

| Event | Action |
|-------|--------|
| `NativeOnInitialized` | Build widget tree |
| `NativeConstruct` | Resolve subsystem, bind `OnMapTileReady`, center on player |
| `NativeDestruct` | Unbind `OnMapTileReady` delegate |
| `NativeTick` | Rebuild texture if dirty, update player marker |

## VCPlayerController Integration

Both widgets are managed by `AVCPlayerController`:

**Minimap:** Created in `CreatePersistentWidgets()`, added to viewport at Z-order 1, positioned top-right via anchors. Visibility set to `HitTestInvisible` (always visible, does not consume input).

**World Map:** Lazy-created on first toggle via `ToggleWorldMapUI()`. When opened, switches to UI input mode (cursor visible). Closing reverts to game input mode.

```cpp
// Widget class overrides (EditDefaultsOnly)
TSubclassOf<UUserWidget> MinimapWidgetClass;
TSubclassOf<UUserWidget> WorldMapWidgetClass;

// Public API
void ToggleWorldMapUI();
```

**Input:** The M key triggers `ToggleWorldMapUI()` via the Enhanced Input action `IA_ToggleWorldMap` bound in `VCInputConfig`.

## Dynamic Texture Pattern

Both widgets use the same `UTexture2D` creation and update pattern (matching `VoxelMaterialAtlas`):

```cpp
// Create once
Texture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
Texture->Filter = TF_Bilinear;
Texture->SRGB = true;
Texture->CompressionSettings = TC_VectorDisplacementmap;

// Update per-frame
FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
// ... write pixel data ...
Mip.BulkData.Unlock();
Texture->UpdateResource();
```

`TC_VectorDisplacementmap` disables compression for pixel-accurate rendering. `TF_Bilinear` provides smooth scaling when the texture is displayed at non-native sizes.

## File Listing

| File | Description |
|------|-------------|
| `Public/Map/VCMinimapWidget.h` | Minimap widget declaration |
| `Private/Map/VCMinimapWidget.cpp` | Minimap implementation |
| `Public/Map/VCWorldMapWidget.h` | World map widget declaration |
| `Private/Map/VCWorldMapWidget.cpp` | World map implementation |

## See Also

- VoxelWorlds [MAP_SYSTEM.md](../../VoxelWorlds/Documentation/MAP_SYSTEM.md) — Map data subsystem and tile generation
- [NAVIGATION_SYSTEM.md](NAVIGATION_SYSTEM.md) — Voxel navigation helpers used alongside map display
