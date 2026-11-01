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

The map image rotates with the player's camera heading so "forward" always points up on the minimap. To prevent gaps at the corners during rotation, the texture is the visible square's diagonal, plus a margin for the shift between fills (see Texture Rendering):

```
Visible square:    200 x 200 pixels (MinimapSize)
Texture size:      289 x 289 pixels (ceil(MinimapSize * sqrt(2)) + 2 * (ceil(RefillShiftTexels) + 1))
World per texel:   160 uu (2 * MinimapWorldRadius / MinimapSize), so the texture shows 1:1 on screen
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

The tick is throttled (default 10 Hz via `UpdateInterval = 0.1`). Each refresh requests the tiles in view
(`RequestTilesInRadius`), rotates the image and places the markers, but fills the texture again only when what it shows
changed (VoxelWorlds world modes review G64; `vc.Minimap.SkipUnchanged`, 0 = every refresh as before). A fill cost 2-3 ms
of game thread at the old rate of ten a second, one frame in six. `NeedsRefill` fills when:

- there is no texture yet, or its size changed;
- the player moved `RefillShiftTexels` (2) from the last fill's centre;
- an image of a tile the last fill read changed (`ComputeTileSignature`: each tile's entry, pixel buffer and resolution;
  a redrawn tile replaces its buffer, a dropped one is gone);
- `MaxRefillSeconds` (2) passed.

Between fills the image is shifted back by the player's movement since the last fill (`GetFillOffsetTexels`, rotated with
the map: `SetRenderTranslation(-Offset.GetRotated(Angle))`), so the terrain stays glued under the player. On a planet the
heading and the markers are taken on the plane the texture was drawn in (`GetDisplayFrame`, the last fill's tangent
plane): neighbouring tangent planes turn against each other near a pole, and the player's own plane would be off by a
texel at the rim at 80 degrees and by dozens near the pole (`VoxelCharacter.Planet.MinimapShift`).

A fill (`RefreshMapTexture(Center)`):

1. Lock the transient `UTexture2D` mip 0 bulk data
2. A planet: `FVCPlanetMapSampler::Fill` over the tangent plane at `Center`. A plane: `FVCPlaneMapSampler::Fill`, texel
   by texel (each texel the tile pixel just inside its far corner, the pixel the old per-tile-pixel blit wrote last; the
   columns' and rows' tiles and positions worked out once; `vc.Minimap.TexelFill 0` = the blit as before, which left
   gaps where tile pixels are coarser than texels). Both report the tiles they read (`VisitedTiles`).
3. Unlock and `UpdateResource()`
4. Apply via `SetBrushFromTexture()` + `SetDesiredSizeOverride()`

`vc.Minimap.Stats [reset]` logs refreshes, fills, those skipped, refreshes over 2 ms and each phase's mean and maximum
(tile requests, texture fill, texture upload, markers, the refresh).

### Configuration

| Property | Default | Description |
|----------|---------|-------------|
| `MinimapSize` | 200.0 | Widget pixel size on screen (square) |
| `MinimapWorldRadius` | 16000.0 | World units visible from center to edge |
| `UpdateInterval` | 0.1 | Seconds between refreshes (rotation, markers; a fill only when needed) |
| `RefillShiftTexels` | 2.0 | Texels the player may move before the texture is filled again (G64) |
| `MaxRefillSeconds` | 2.0 | The texture is filled again at least this often (G64) |

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
| Explored, tile not yet generated (or its image evicted) | Slightly lighter fog (RGB: 25, 25, 25) |
| Explored, tile generated | Full terrain colors from tile pixel data, its detail image or its overview |

Exploration state is queried from `UVoxelMapSubsystem::IsTileExplored()`.

### Tile Images on Demand

The map keeps tile images under a fixed budget (world modes review WM-E2): a tile may have only its overview (a quarter
of the resolution a side) or no image at all until asked for (VoxelWorlds `Documentation/Features/MAP_SYSTEM.md`). While
`RebuildMapTexture()` blits the view, every explored tile in it without an image asks for one
(`UVoxelMapSubsystem::RequestTileImage`), and when the view shows more than one and a half times an overview's resolution
per tile (zoom above 1.5 at the default texture size) tiles that have only the overview ask for detail. The drawings
arrive through `OnMapTileReady` and redraw the map. At zoom 1 the texture shows 16 texels per tile: exactly an overview,
so a fully explored island draws without asking for anything.

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

## Planets (VoxelWorlds spherical Phase 4, SP-G3)

On a planet (`UVoxelMapSubsystem::IsPlanetMap`) both widgets read the cube-sphere tiles (VoxelWorlds `MAP_SYSTEM.md`
"Planets"); heightmap code paths are unchanged.

- **Minimap**: the tangent plane under the player (`FVCPlanetMapFrame`: east = +Z × up, north = up × east, at a pole the
  face's axes), drawn north along texture X and east along Y like world X and Y on a plane, so the rotation
  (−heading − 90) and the north indicator carry over; the heading is measured from north. The coordinate text shows
  latitude and longitude. The texture is filled from the tile images by `FVCPlanetMapSampler` (a 16-texel grid of exact
  control points with bilinear UV inside one-face cells, per texel across a face edge), and redrawn only when the player
  has moved a texel, turned, or a tile arrived.
- **World map**: a globe (decision D11, `FVCGlobeProjection`): an orthographic view around a direction, north up,
  opening on the player. Dragging brings what showed at −Δ to the centre; the wheel zooms at the plane map's scale (32
  tiles from the centre at zoom 1, the whole disc near `MinZoom` 0.25). Redrawn at most 30 times a second while tiles
  stream in. The footer shows the player's latitude and longitude.
- **Markers** carry one 3D position (`FVCMapMarker::WorldLocation`; a plane's maps draw its X and Y, a planet's its
  direction); every map gathers them through the one `UVCMapMarkerRegistry::OnGatherMarkers(Center, Radius)`, a square of
  that half extent on a plane and the 3D distance on a planet (code review CR2, VCP-21). POIs bind it (Phase 5).

Tests: `VoxelCharacter.Planet.MinimapFrame`, `.WorldMapProjection`, `.MapSampler`.

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
