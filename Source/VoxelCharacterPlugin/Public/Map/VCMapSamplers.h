// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"

class UVoxelMapSubsystem;

/**
 * The minimap's frame on a planet (spherical Phase 4, WORLD_MODES_REVIEW_PLAN.md §18, WM-SY9): the tangent plane of the
 * datum sphere under a position, north toward the +Z pole and east to its right (FVoxelCubeSphere::GetTangentFrame).
 * The minimap's texture is this plane, north along texture X and east along texture Y as world X and Y are on a plane,
 * so the plane's rotation and north indicator arithmetic carry over with the heading measured from north. Pure.
 */
struct VOXELCHARACTERPLUGIN_API FVCPlanetMapFrame
{
	/** Planet centre and datum radius. */
	FVector Center = FVector::ZeroVector;
	double Radius = 1.0;

	/** Unit local up, east and north at the frame's origin (the datum point under the position). */
	FVector Up = FVector::UpVector;
	FVector East = FVector::RightVector;
	FVector North = FVector::ForwardVector;

	/**
	 * The frame under a position.
	 * @param InCenter Planet centre
	 * @param InRadius Datum radius
	 * @param Position Any position (its direction from the centre gives the frame)
	 * @return The frame
	 */
	static FVCPlanetMapFrame At(const FVector& InCenter, double InRadius, const FVector& Position);

	/**
	 * The direction from the centre through the tangent plane's point at (north, east) offsets from the origin.
	 * @param NorthOffset Offset along north (uu)
	 * @param EastOffset Offset along east (uu)
	 * @return Unit direction
	 */
	FVector DirectionAtOffset(double NorthOffset, double EastOffset) const;

	/**
	 * A world point's offset on the tangent plane (the inverse of DirectionAtOffset for a point on the line it gives):
	 * the point's direction carried to the plane through the centre.
	 * @param WorldPoint Any point on the near side of the planet
	 * @return (north, east) offset (uu); zero for a point on the far hemisphere
	 */
	FVector2D OffsetOf(const FVector& WorldPoint) const;

	/**
	 * A forward vector's heading: degrees from north toward east, in (-180, 180].
	 * @param Forward Any vector (its part across local up counts)
	 * @return Heading (degrees)
	 */
	double HeadingDegrees(const FVector& Forward) const;

	/**
	 * Latitude and longitude of the frame's origin: +Z north, longitude from +X toward +Y (vox.TPLatLon's).
	 * @return (latitude, longitude) in degrees
	 */
	FVector2D GetLatLon() const;
};

/**
 * The world map's globe on a planet (D11; WM-SY9): an orthographic view of the planet around a view direction, north up.
 * A screen offset (pixels from the view's centre, Y down) within RadiusPx of the centre shows the direction
 * x * East + y * North + sqrt(1 - x^2 - y^2) * View (x, y the offset over RadiusPx, y up); zoomed in far it is a local
 * map. Dragging turns the globe so the content follows the mouse. Pure.
 */
struct VOXELCHARACTERPLUGIN_API FVCGlobeProjection
{
	/** Unit direction at the view's centre, and the view's east and north there. */
	FVector View = FVector::UpVector;
	FVector East = FVector::RightVector;
	FVector North = FVector::ForwardVector;

	/** The globe's radius on screen (pixels). */
	double RadiusPx = 256.0;

	/**
	 * A north-up view.
	 * @param ViewDirection Direction at the view's centre (normalised here)
	 * @param InRadiusPx Globe radius on screen (pixels)
	 * @return The projection
	 */
	static FVCGlobeProjection Make(const FVector& ViewDirection, double InRadiusPx);

	/**
	 * The direction shown at a screen offset.
	 * @param Screen Pixels from the view's centre (X right, Y down)
	 * @param OutDirection Unit direction
	 * @return False off the globe's disc
	 */
	bool ScreenToDirection(const FVector2D& Screen, FVector& OutDirection) const;

	/**
	 * Where a direction shows.
	 * @param Direction Unit direction from the centre
	 * @param OutScreen Pixels from the view's centre (X right, Y down)
	 * @return False on the far side of the globe
	 */
	bool DirectionToScreen(const FVector& Direction, FVector2D& OutScreen) const;

	/**
	 * The view after a drag: the content follows the mouse (what showed at -Delta comes to the centre), north stays up.
	 * @param DeltaPx Mouse movement (pixels, Y down)
	 * @return The turned projection
	 */
	FVCGlobeProjection Dragged(const FVector2D& DeltaPx) const;
};

/**
 * Fills a BGRA texture from a planet map's tile pyramid (MAP_PYRAMID_PLAN.md P1): each texel shows the pixel its direction
 * falls in at the view's Level, or, while that tile draws, the pixel of the nearest coarser level stored over it, through a
 * grid of control points every Step texels (texels of a cell whose corners lie on one face interpolate the face's UV;
 * cells across a face edge or the globe's rim go texel by texel). It asks for nothing: it reports the tiles at Level it
 * shows (ViewTiles), and the caller requests them (UVoxelMapSubsystem::RequestView). Game thread.
 */
struct VOXELCHARACTERPLUGIN_API FVCPlanetMapSampler
{
	/** Texel colours for unexplored ground and for explored ground whose image has not been drawn yet. */
	FColor Unexplored = FColor(10, 10, 10, 255);
	FColor Undrawn = FColor(25, 25, 25, 255);

	/** Texel colour off the view (the globe's background). */
	FColor Background = FColor(5, 5, 5, 255);

	/**
	 * Draw only explored ground, Unexplored elsewhere (the world map's fog of war); false: every tile (the minimap's area is
	 * explored as it draws). A texel is explored where any level-0 tile under its pixel at Level is, read from the explored
	 * mask (UVoxelMapSubsystem::GetExploredMask, MAP_PYRAMID_PLAN.md P2), so the fog at every zoom is one cached lookup.
	 */
	bool bRequireExplored = true;

	/** The pyramid level the view is composed from (UVoxelMapSubsystem::LevelForTexel: its pixels match the texels). */
	int32 Level = 0;

	/**
	 * A tile not drawn yet at Level falls back on the stored tiles up to this many levels coarser (beyond: Undrawn): to the
	 * top levels a planet keeps (UVoxelMapSubsystem::GetPrefetchLevel, level 9 on the 50 km planet), so the globe always
	 * has an image (MAP_PYRAMID_PLAN.md P2).
	 */
	int32 MaxFallbackLevels = 16;

	/**
	 * If set, receives the coordinates at Level of the tiles the fill shows (those holding an explored texel with
	 * bRequireExplored), nearest the texture's centre first: the view's request (UVoxelMapSubsystem::RequestView).
	 */
	TArray<FIntPoint>* ViewTiles = nullptr;

	/**
	 * Fill the texture.
	 * @param Map The planet's map subsystem
	 * @param OutBGRA TexSize^2 BGRA texels
	 * @param TexSize Texture size (square)
	 * @param Step Control point spacing (texels)
	 * @param TexelDirection The direction at a texel position (continuous: texel (X, Y)'s centre is (X + 0.5, Y + 0.5));
	 *        false where the view shows no ground
	 * @return Number of texels drawn from a tile image (at Level or coarser) or the albedo
	 */
	int32 Fill(UVoxelMapSubsystem& Map, uint8* OutBGRA, int32 TexSize, int32 Step,
		TFunctionRef<bool(double X, double Y, FVector& OutDirection)> TexelDirection) const;
};

/**
 * A map texture on a plane (heightmap worlds; world modes review G64): the texture centred on a position, WorldPerPixel
 * units a texel, texel (X, Y) at world (X, Y) from the texture's corner, or with bNorthUp (the world map) texel X along
 * world +Y (east right) and texel Y along world -X (north up). Each texel shows the pixel just inside its far corner (in
 * +X, +Y) at the view's Level of the tile pyramid (MAP_PYRAMID_PLAN.md P1), or, while that tile draws, the nearest
 * coarser level's stored over it. Columns and rows are worked out once, so a fill costs a few operations a texel. With
 * bRequireExplored (the world map) only explored ground draws and the fog colours cover the rest. It asks for nothing: it
 * reports the tiles at Level it shows (ViewTiles), and the caller requests them (UVoxelMapSubsystem::RequestView). The
 * minimap and the world map share it (code review CR2, VCP-15).
 */
struct VOXELCHARACTERPLUGIN_API FVCPlaneMapSampler
{
	/** Texel colour with no tile image (bRequireExplored off). */
	FColor Background = FColor(5, 5, 5, 255);

	/** Texel colours for unexplored ground and for explored ground whose image has not been drawn yet (bRequireExplored). */
	FColor Unexplored = FColor(10, 10, 10, 255);
	FColor Undrawn = FColor(25, 25, 25, 255);

	/**
	 * Draw only explored ground, Unexplored elsewhere and Undrawn where no level has an image yet (the world map); false:
	 * every tile with an image, Background elsewhere (the minimap, whose area is explored as it draws). A texel is explored
	 * where any level-0 tile under its pixel at Level is (the explored mask, MAP_PYRAMID_PLAN.md P2).
	 */
	bool bRequireExplored = false;

	/** The pyramid level the view is composed from (UVoxelMapSubsystem::LevelForTexel: its pixels match the texels). */
	int32 Level = 0;

	/** A tile not drawn yet at Level falls back on the stored tiles up to this many levels coarser (beyond: Undrawn). */
	int32 MaxFallbackLevels = 16;

	/** Texel X along world +Y and texel Y along world -X (the world map's north-up image); false: texel (X, Y) is world (X, Y). */
	bool bNorthUp = false;

	/**
	 * If set, receives the coordinates at Level of the tiles the fill shows (those holding an explored texel with
	 * bRequireExplored), nearest the texture's centre first: the view's request (UVoxelMapSubsystem::RequestView).
	 */
	TArray<FIntPoint>* ViewTiles = nullptr;

	/**
	 * Fill the texture.
	 * @param Map The plane's map subsystem
	 * @param OutBGRA TexSize^2 BGRA texels
	 * @param TexSize Texture size (square)
	 * @param Center World position the texture is centred on
	 * @param WorldPerPixel World units per texel
	 * @return Texels drawn from a tile image (at Level or coarser)
	 */
	int32 Fill(UVoxelMapSubsystem& Map, uint8* OutBGRA, int32 TexSize, const FVector& Center, double WorldPerPixel) const;
};
