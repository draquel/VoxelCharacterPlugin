# Map UI Widgets

> State as of 2026-11-10 (MAP_PYRAMID_PLAN.md, done: P1, both maps on the tile pyramid; P2, the fog from the explored
> mask, the globe from the top levels; P3, the fills on the task graph's workers, the planet's marker caps kept, G169; P4,
> the frame with the map open measured).

**Module**: VoxelCharacterPlugin
**Dependencies**: VoxelWorlds' `VoxelMap` module (`UVoxelMapSubsystem`, the tile data), UMG (rendering)

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
changed (G64). A fill costs 2-3 ms of game thread, so filling at every refresh
put one in six frames over budget. `NeedsRefill` fills when:

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
   by texel (each texel the pixel just inside its far corner, the pixel the old per-tile-pixel blit wrote last; the
   columns' and rows' tiles and positions worked out once; the per-tile-pixel blit before G64 left gaps where tile
   pixels are coarser than texels). Both compose from the tile pyramid at the level matching the texels
   (`UVoxelMapSubsystem::LevelForTexel`: level 1 at the defaults, 160 uu texels over 75 uu voxels), the nearest coarser
   level stored over a texel while its tile draws, and report the tiles they showed at that level (`ViewTiles`, nearest
   the centre first).
3. Unlock and `UpdateResource()`
4. Apply via `SetBrushFromTexture()` + `SetDesiredSizeOverride()`
5. Ask the map for the tiles shown (`RequestView(EVoxelMapRequester::Minimap, ...)`, served before the world map's); the
   arrivals change the tiles' signature, so the next refresh fills again. The request goes when the widget does

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
| `MaxMarkers` | 24 | Markers drawn at most |

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
| Explored, tile generated | Full terrain colors from the tile pyramid at the view's level, or the nearest coarser level stored while that draws |

The fog is read per texel from the map's explored mask (`UVoxelMapSubsystem::GetExploredMask`, MAP_PYRAMID_PLAN.md P2):
a texel is explored where any level-0 tile under its pixel at the view's level is (`FVoxelMapExploredMask::FReader::
IsAnyExplored` over an aligned square of 2^(level - 6) tiles a side at 64-pixel tiles; one tile at levels 0-6), one cached
lookup that most texels answer from the block of the last. Before the mask the samplers asked the explored key set per
tile, the zoomed-out globe its albedo cells' explored set, rebuilt from every explored key at most once a second (G170:
40-49 ms at a million keys); there is nothing to rebuild now.

### Tile Images on Demand (the pyramid)

The map draws images for the views, at the level of its tile pyramid matching their texels
(`Plugins/VoxelWorlds/Documentation/Features/MAP_SYSTEM.md` "The Tile Pyramid"). `RebuildMapTexture()` composes the view
from that level (`LevelForTexel` of a texel's size: level 2 at zoom 1, where a texel is 4 voxels; level 0 zoomed in four
times; level 6 at a tile a texel), each texel the nearest coarser level stored over it while its tile draws, and then
asks for the tiles it showed, nearest the centre first (`RequestView(EVoxelMapRequester::WorldMap, ...)`): about 300 at
any zoom and on any world, replacing its last request, so what a pan or a zoom left stops waiting. Closing the map drops
the request. Before the pyramid (MAP_PYRAMID_PLAN.md P1) every explored tile in view asked for an image of its own: half a
million at a tile a texel on the 50 km planet (G167).

### Texture Rendering

`RebuildMapTexture()` is triggered by the `bMapDirty` flag, set when:
- The widget opens (`NativeConstruct`)
- Tiles of the view arrive (`OnMapTileReady`), coalesced: `FVCMapArrivalCoalescer` counts only the tiles of the view's
  last request at its level and redraws for them at most every quarter second (G166: a redraw per arrival held the 50 km
  globe at 94 ms frames; `VoxelCharacter.Map.ArrivalsInViewCoalesce`). Arrivals elsewhere (another view's, another level's)
  ask for nothing
- The exploration grew (`GetExplorationRevision`; the fog of war), at most once a second
- The user zooms or pans (a dragged globe at most 30 times a second)

The rendering composes the view into a larger fixed texture (`MapTextureFixedSize`, default 1024) with the fog of war
above. It replaces the 2 s throttle of world modes review WM-MP9 (`DefersTileRedraw`, `TileRedrawThrottleTiles`), which a
planet's globe never applied.

Both samplers fill in bands on the task graph's workers (MAP_PYRAMID_PLAN.md P3; `vc.Map.ParallelFill`, 0 for the calling
thread): a planet's by its rows of control cells (64 bands at 1024² and 16 texels a cell), a plane's by 16 rows. The game
thread waits in the `ParallelFor`, so the tile store and the explored mask are only read meanwhile (tiles arrive on the game
thread); each band has its own caches, reads the store without marking its images used (`FindTile(Id, false)`: the store's
LRU clock is game-thread state) and lists them, and the merge marks them used and joins the bands' view tiles. On the fully
explored 50 km planet a 1024² fill at zoom 1 fell from 27.0 to 5.4 ms and its rebuild, the upload included (1.4 ms), from
28.5 to 7.1 ms (Insights, `VCWorldMap_Rebuild`). The cost was the per-texel loop: the globe's projection and
`DirectionToSurface` at the control points took 0.19 ms of the 27.

**The frame with the map open** (MAP_PYRAMID_PLAN.md P4). The open world map costs a frame 0.3-0.4 ms of Slate: the
canvas's paint and the text (`SConstraintCanvas`, `Slate::AddShapedTextElement`), from Insights traces of a standing view
at zooms 4, 1, 0.05 and the whole globe on the fully explored 50 km planet. A settled view does nothing else per frame; it
rebuilds only for arrivals, exploration and input. With the weather held clear (`map_scale.py --weather=Clear`), every
zoom reads 13.3-15.7 ms of game thread at P50 against 15.4 with the map closed. P3 had measured 6-10 ms more with the map
open at zooms 2 to 0.25. That was a snow front, which the weather schedules at the same time in every PIE of the planet
(G172, `NS_WeatherSnow`): with the map kept closed through the same windows, the frame rose by the same amount.

`vc.WorldMap.Stats [reset]` logs the rebuilds since the last reset (per second, those over 2 ms, the tile arrivals and
those deferred) and each phase's count, mean and maximum: texture fill, upload, markers, the rebuild. Insights shows the
same phases as `VCWorldMap_*` scopes (the map scaling plan's M0, G163).

### Markers

Both maps draw the markers `UVCMapMarkerRegistry` gathers around the view, at most `MaxMarkers` (minimap 24, world map 64,
highest `Priority` first); the world map gathers them when the view changes and every `MarkerUpdateInterval` (0.5 s).
VoxelWorldPOI's `VoxelWorlds.POI.Map.MarkerCap` checks the cap on a zoomed-out view; the POI side bounds its own
query on a big planet (G50).

On a planet the POI side places its far markers over a cap on a coarse ladder (`UPOIMapMarkerSubsystem::
SnapPlanetMarkerCap`: the angle up to the next of a ladder a quarter octave apart, the direction to a cube-sphere cell an
eighth of that across) and keeps the last four caps with their markers finished (G169, MAP_PYRAMID_PLAN.md P3). The
minimap, which stays open under the world map and gathers at the player every 0.1 s, had replaced the globe's one cached
cap, so every world map gather placed the globe's 20,000 cells again: 65.8 ms twice a second on the 50 km planet
zoomed out (64.8 of it placement). Now each map keeps its cap, and a view dragged or zoomed less than a step keeps it
too: 0.98 ms a gather (`VoxelWorlds.POI.Map.PlanetMarkerCapSnap`, `VoxelWorlds.POI.Planet.MapMarkersAtScale`).

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
| `MaxMarkers` | 64 | Markers drawn at most |
| `MarkerUpdateInterval` | 0.5 | Seconds between marker gathers |

### Lifecycle

| Event | Action |
|-------|--------|
| `NativeOnInitialized` | Build widget tree |
| `NativeConstruct` | Resolve subsystem, bind `OnMapTileReady`, center on player |
| `NativeDestruct` | Unbind the delegates, drop the world map's request (`RequestView(WorldMap, {})`) |
| `NativeTick` | Rebuild texture if dirty, update player marker |

## Planets (VoxelWorlds spherical Phase 4, SP-G3)

On a planet (`UVoxelMapSubsystem::IsPlanetMap`) both widgets read the cube-sphere tiles (VoxelWorlds `MAP_SYSTEM.md`
"Planets"); heightmap code paths are unchanged.

- **Minimap**: the tangent plane under the player (`FVCPlanetMapFrame`: east = +Z × up, north = up × east, at a pole the
  face's axes), drawn north along texture X and east along Y like world X and Y on a plane, so the rotation
  (−heading − 90) and the north indicator carry over; the heading is measured from north. The coordinate text shows
  latitude and longitude. The texture is filled from the tile images by `FVCPlanetMapSampler` (a 16-texel grid of exact
  control points with bilinear UV inside one-face cells, per texel across a face edge), filled by the same rule as on a
  plane (`NeedsRefill`) and shifted in between on the plane it was drawn in.
- **World map**: a globe (decision D11, `FVCGlobeProjection`): an orthographic view around a direction, north up,
  opening on the player. Dragging brings what showed at −Δ to the centre; the wheel zooms at the plane map's scale (32
  tiles from the centre at zoom 1). The zoom range comes from the planet's radius (`GetPlanetZoomRange`): the most
  zoomed-out view shows the whole globe at any radius. At every zoom the globe is composed from the pyramid at the level of
  a texel at its centre; arrivals redraw it coalesced, as on a plane. Zoomed out, while the view's own tiles draw, it shows
  the planet's top levels, which the map draws when it resolves the world and keeps (`UVoxelMapSubsystem::GetPrefetchLevel`:
  from level 9 on the 50 km planet; the samplers fall back up to 16 levels, `MaxFallbackLevels`), so the globe always has
  an image (MAP_PYRAMID_PLAN.md P2). They replace the coarse albedo the globe was drawn from zoomed out until P2 (spherical
  Phase 6, G51: `GetGlobeAlbedo`, `GlobeAlbedoTexelsPerTile`).
  `SetZoom` (the game module's `vox.Map`) sets the zoom within the range. The footer shows the player's latitude and
  longitude.
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

**Input:** The M key (`UVCInputConfig::IA_OpenMap`, bound by `AVCCharacterBase::Input_OpenMap`) calls `ToggleWorldMapUI()`.

## Dynamic Texture Pattern

Both widgets use the same `UTexture2D` creation and update pattern:

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
| `Public/Map/VCWorldMapWidget.h` | World map widget declaration; `FVCMapArrivalCoalescer` (arrivals in view, coalesced) |
| `Private/Map/VCWorldMapWidget.cpp` | World map implementation |
| `Public/Map/VCMapSamplers.h`, `Private/Map/VCMapSamplers.cpp` | `FVCPlanetMapFrame`, `FVCGlobeProjection`, `FVCPlanetMapSampler`, `FVCPlaneMapSampler` |
| `Public/Map/VCMapMarkerRegistry.h` | `FVCMapMarker`, `UVCMapMarkerRegistry` (the marker gather) |

## See Also

- `Plugins/VoxelWorlds/Documentation/Features/MAP_SYSTEM.md` — the map data subsystem, tile generation and exploration
- [CHARACTER_SYSTEM.md](CHARACTER_SYSTEM.md) — the controller and HUD that host the widgets
