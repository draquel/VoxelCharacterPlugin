// Copyright Daniel Raquel. All Rights Reserved.

#include "Map/VCMapSamplers.h"
#include "VoxelCubeSphere.h"
#include "VoxelMapSubsystem.h"
#include "Async/ParallelFor.h"
#include "HAL/IConsoleManager.h"

// ==================== FVCPlanetMapFrame ====================

FVCPlanetMapFrame FVCPlanetMapFrame::At(const FVector& InCenter, double InRadius, const FVector& Position)
{
	FVCPlanetMapFrame Frame;
	Frame.Center = InCenter;
	Frame.Radius = FMath::Max(InRadius, 1.0);
	const FVector ToPoint = Position - InCenter;
	Frame.Up = ToPoint.IsNearlyZero() ? FVector::UpVector : ToPoint.GetSafeNormal();
	FVoxelCubeSphere::GetTangentFrame(Frame.Up, Frame.East, Frame.North);
	return Frame;
}

FVector FVCPlanetMapFrame::DirectionAtOffset(double NorthOffset, double EastOffset) const
{
	return (Up * Radius + North * NorthOffset + East * EastOffset).GetSafeNormal();
}

FVector2D FVCPlanetMapFrame::OffsetOf(const FVector& WorldPoint) const
{
	const FVector Direction = (WorldPoint - Center).GetSafeNormal();
	const double Along = FVector::DotProduct(Direction, Up);
	if (Along <= 1.0e-6)
	{
		return FVector2D::ZeroVector;
	}
	// The direction's line meets the tangent plane at Radius / Along along it.
	const FVector OnPlane = Direction * (Radius / Along);
	return FVector2D(FVector::DotProduct(OnPlane, North), FVector::DotProduct(OnPlane, East));
}

double FVCPlanetMapFrame::HeadingDegrees(const FVector& Forward) const
{
	return FMath::RadiansToDegrees(FMath::Atan2(FVector::DotProduct(Forward, East), FVector::DotProduct(Forward, North)));
}

FVector2D FVCPlanetMapFrame::GetLatLon() const
{
	return FVector2D(
		FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Up.Z, -1.0, 1.0))),
		FMath::RadiansToDegrees(FMath::Atan2(Up.Y, Up.X)));
}

// ==================== FVCGlobeProjection ====================

FVCGlobeProjection FVCGlobeProjection::Make(const FVector& ViewDirection, double InRadiusPx)
{
	FVCGlobeProjection Projection;
	Projection.View = ViewDirection.IsNearlyZero() ? FVector::UpVector : ViewDirection.GetSafeNormal();
	FVoxelCubeSphere::GetTangentFrame(Projection.View, Projection.East, Projection.North);
	Projection.RadiusPx = FMath::Max(InRadiusPx, 1.0);
	return Projection;
}

bool FVCGlobeProjection::ScreenToDirection(const FVector2D& Screen, FVector& OutDirection) const
{
	const double X = Screen.X / RadiusPx;
	const double Y = -Screen.Y / RadiusPx;
	const double Rho2 = X * X + Y * Y;
	if (Rho2 > 1.0)
	{
		return false;
	}
	OutDirection = (East * X + North * Y + View * FMath::Sqrt(1.0 - Rho2)).GetSafeNormal();
	return true;
}

bool FVCGlobeProjection::DirectionToScreen(const FVector& Direction, FVector2D& OutScreen) const
{
	if (FVector::DotProduct(Direction, View) < 0.0)
	{
		return false;
	}
	OutScreen = FVector2D(FVector::DotProduct(Direction, East) * RadiusPx, -FVector::DotProduct(Direction, North) * RadiusPx);
	return true;
}

FVCGlobeProjection FVCGlobeProjection::Dragged(const FVector2D& DeltaPx) const
{
	// The content follows the mouse: what showed at -Delta comes to the centre (past the rim, the rim). The old centre then
	// shows |Delta| from the centre, along Delta turned by the meridians' convergence (north stays up).
	FVector NewView;
	if (!ScreenToDirection(-DeltaPx, NewView))
	{
		ScreenToDirection(-DeltaPx.GetSafeNormal() * RadiusPx * 0.999, NewView);
	}
	return Make(NewView, RadiusPx);
}

// ==================== The view's tiles ====================

namespace
{
	/**
	 * The tiles a fill shows at its level, each with the box of the texels it was seen at, reported nearest the texture's
	 * centre first: the view's request (UVoxelMapSubsystem::RequestView, MAP_PYRAMID_PLAN.md P1).
	 */
	struct FVCViewTileCollector
	{
		struct FSpan
		{
			FIntPoint Tile;
			int32 MinX, MinY, MaxX, MaxY;
		};
		TMap<FIntPoint, int32> Index;
		TArray<FSpan> Spans;
		int32 Last = INDEX_NONE;

		void Touch(const FIntPoint& Tile, int32 X, int32 Y)
		{
			if (Last == INDEX_NONE || Spans[Last].Tile != Tile)
			{
				if (const int32* Found = Index.Find(Tile))
				{
					Last = *Found;
				}
				else
				{
					Last = Spans.Add(FSpan{ Tile, X, Y, X, Y });
					Index.Add(Tile, Last);
					return;
				}
			}
			FSpan& Span = Spans[Last];
			Span.MinX = FMath::Min(Span.MinX, X);
			Span.MinY = FMath::Min(Span.MinY, Y);
			Span.MaxX = FMath::Max(Span.MaxX, X);
			Span.MaxY = FMath::Max(Span.MaxY, Y);
		}

		/** Another collector's tiles and their texel boxes added to these (a band of a fill). */
		void Merge(const FVCViewTileCollector& Other)
		{
			for (const FSpan& Span : Other.Spans)
			{
				Touch(Span.Tile, Span.MinX, Span.MinY);
				Touch(Span.Tile, Span.MaxX, Span.MaxY);
			}
		}

		void NearestFirst(int32 TexSize, TArray<FIntPoint>& Out) const
		{
			const double Centre = 0.5 * TexSize;
			TArray<TPair<double, FIntPoint>> Keyed;
			Keyed.Reserve(Spans.Num());
			for (const FSpan& Span : Spans)
			{
				const double DX = 0.5 * (Span.MinX + Span.MaxX + 1) - Centre;
				const double DY = 0.5 * (Span.MinY + Span.MaxY + 1) - Centre;
				Keyed.Emplace(DX * DX + DY * DY, Span.Tile);
			}
			Keyed.Sort([](const TPair<double, FIntPoint>& A, const TPair<double, FIntPoint>& B) { return A.Key < B.Key; });
			Out.Reset(Keyed.Num());
			for (const TPair<double, FIntPoint>& Entry : Keyed)
			{
				Out.Add(Entry.Value);
			}
		}
	};

	void WriteTexel(uint8* Texel, const FColor& Color)
	{
		Texel[0] = Color.B;
		Texel[1] = Color.G;
		Texel[2] = Color.R;
		Texel[3] = Color.A;
	}

	/** Floor division (a pixel index's tile, negative on a plane). */
	int64 FloorDiv(int64 A, int64 B)
	{
		const int64 Q = A / B;
		return (A % B != 0 && ((A < 0) != (B < 0))) ? Q - 1 : Q;
	}

	TAutoConsoleVariable<int32> CVarMapParallelFill(
		TEXT("vc.Map.ParallelFill"),
		1,
		TEXT("The map textures fill in bands on the task graph's workers (1, MAP_PYRAMID_PLAN.md P3), or on the calling thread (0: A/B)."),
		ECVF_Default);

	/** Rows of a plane fill's band (a 1024^2 world map: 64 bands). */
	constexpr int32 FillBandRows = 16;

	EParallelForFlags ParallelFlags()
	{
		return CVarMapParallelFill.GetValueOnAnyThread() != 0 ? EParallelForFlags::None : EParallelForFlags::ForceSingleThread;
	}

	/** One band of a fill: the tiles it showed, the images it read (to mark used in the store after) and its count. */
	struct FVCFillBand
	{
		FVCViewTileCollector Collector;
		TArray<FVoxelMapTileId> Images;
		int32 Drawn = 0;

		/** An image the band read (once a run: consecutive reads of one image are one). */
		void Used(const FVoxelMapTileId& Id)
		{
			if (Images.IsEmpty() || !(Images.Last() == Id))
			{
				Images.Add(Id);
			}
		}
	};

	/**
	 * The bands' results together, on the calling thread: the images they read marked used in the store (its LRU, which the
	 * bands only read), the tiles shown nearest the centre first, the texels drawn.
	 */
	int32 MergeBands(UVoxelMapSubsystem& Map, const TArray<FVCFillBand>& Bands, int32 TexSize, TArray<FIntPoint>* ViewTiles)
	{
		FVCViewTileCollector Collector;
		int32 Drawn = 0;
		for (const FVCFillBand& Band : Bands)
		{
			Drawn += Band.Drawn;
			for (const FVoxelMapTileId& Id : Band.Images)
			{
				Map.FindTile(Id, /*bTouch*/ true);
			}
			if (ViewTiles)
			{
				Collector.Merge(Band.Collector);
			}
		}
		if (ViewTiles)
		{
			Collector.NearestFirst(TexSize, *ViewTiles);
		}
		return Drawn;
	}
}

// ==================== FVCPlanetMapSampler ====================

int32 FVCPlanetMapSampler::Fill(UVoxelMapSubsystem& Map, uint8* OutBGRA, int32 TexSize, int32 Step,
	TFunctionRef<bool(double X, double Y, FVector& OutDirection)> TexelDirection) const
{
	const FVoxelMapTileFrame& Frame = Map.GetTileFrame();
	const FVoxelCubeSphereGrid& Grid = Map.GetPlanetTileGrid();
	const int32 Res = Map.GetTileResolution();
	if (!Grid.IsValid() || Res <= 0 || TexSize <= 0 || !OutBGRA)
	{
		return 0;
	}
	const int32 N = Grid.GetCellsPerEdge();
	// A face's level-0 pixels fit an int32 up to N of 2^31 / Res (a 1,000 km planet is 32,725 tiles a face edge: 2.1 million
	// pixels at 64).
	const int32 FacePixels = static_cast<int32>(FMath::Min<int64>(static_cast<int64>(N) * Res, MAX_int32));
	const double PixelsPerUV = 0.5 * static_cast<double>(FacePixels);
	const int32 MaxLevel = Frame.GetMaxLevel();
	const int32 ViewLevel = FMath::Clamp(Level, 0, MaxLevel);
	const int32 ViewTilesPerEdge = Frame.TilesPerFaceEdgeAt(ViewLevel);
	// A pixel's tile by a shift where the resolution is a power of two (it is ChunkSize: 32 or 64): divisions per texel cost
	// a fill at a million texels tens of milliseconds.
	const bool bResPow2 = FMath::IsPowerOfTwo(Res);
	const int32 ResShift = bResPow2 ? static_cast<int32>(FMath::FloorLog2(static_cast<uint32>(Res))) : 0;
	// The fog of war from the explored mask (MAP_PYRAMID_PLAN.md P2): a pixel at the view's level covers 2^FogShift level-0
	// tiles a side, explored where any of them is (one cached lookup; G170's rebuild over every explored key is gone).
	const int32 FogShift = FMath::Max(0, ViewLevel - static_cast<int32>(FMath::FloorLog2(static_cast<uint32>(Res))));
	Step = FMath::Clamp(Step, 1, TexSize);
	const int32 Points = (TexSize + Step - 1) / Step + 1;

	// Control points: (face, UV) at texel corners every Step texels.
	struct FControl
	{
		int32 Face = -1; // -1: no ground here
		FVector2D UV = FVector2D::ZeroVector;
	};
	TArray<FControl> Controls;
	Controls.SetNum(Points * Points);
	ParallelFor(TEXT("VCMapFill_PlanetControls"), Points, 1, [&](int32 CY)
	{
		for (int32 CX = 0; CX < Points; ++CX)
		{
			FVector Direction;
			if (TexelDirection(static_cast<double>(CX * Step), static_cast<double>(CY * Step), Direction))
			{
				const FVoxelSurfaceCoord Coord = FVoxelCubeSphere::DirectionToSurface(Direction);
				Controls[CY * Points + CX] = FControl{ Coord.Face, Coord.UV };
			}
		}
	}, ParallelFlags());

	// A band of control rows at a time, on the task graph's workers (MAP_PYRAMID_PLAN.md P3: a 1024^2 fill was 23-40 ms
	// on one thread). The store and the mask are only read meanwhile (the game thread waits in the ParallelFor; tiles
	// arrive on it); each band keeps its own caches and the images it read, which are marked used in the store after.
	TArray<FVCFillBand> Bands;
	Bands.SetNum(Points - 1);
	ParallelFor(TEXT("VCMapFill_PlanetBands"), Points - 1, 1, [&](int32 CY)
	{
		FVCFillBand& Band = Bands[CY];
		FVoxelMapExploredMask::FReader Explored(Map.GetExploredMask());
		// The fog cell the last texel fell in (its exploration), and the view's tile it read with the image it drew from:
		// the tile's own at the view's level, or the nearest coarser one stored over it (cached across texels: texels in
		// a run share tiles).
		FIntVector CachedFogCell(MIN_int32, MIN_int32, MIN_int32);
		bool bCachedExplored = false;
		FIntPoint CachedViewTile(MIN_int32, MIN_int32);
		const FVoxelMapTile* CachedImage = nullptr;
		int32 CachedShift = 0;
		int32 CachedBaseU = 0;
		int32 CachedBaseV = 0;

		auto Write = [OutBGRA, TexSize](int32 X, int32 Y, const FColor& Color)
		{
			WriteTexel(OutBGRA + (static_cast<int64>(Y) * TexSize + X) * 4, Color);
		};
		auto Resolve = [&](int32 Face, int32 TU, int32 TV)
		{
			CachedImage = nullptr;
			for (int32 Up = 0; Up <= MaxFallbackLevels && ViewLevel + Up <= MaxLevel; ++Up)
			{
				const int32 TileLevel = ViewLevel + Up;
				const int32 U = TU >> Up;
				const int32 V = TV >> Up;
				const FVoxelMapTileId Id(FIntPoint(Face * Frame.TilesPerFaceEdgeAt(TileLevel) + U, V), TileLevel);
				const FVoxelMapTile* Image = Map.FindTile(Id, /*bTouch*/ false);
				if (Image && Image->Resolution == Res && Image->PixelData.Num() == Res * Res)
				{
					Band.Used(Id);
					CachedImage = Image;
					CachedShift = TileLevel;
					CachedBaseU = U * Res;
					CachedBaseV = V * Res;
					return;
				}
			}
		};
		auto Shade = [&](int32 X, int32 Y, int32 Face, const FVector2D& UV)
		{
			// The texel's level-0 pixel along its face. UV >= -1, so the products are not negative (but by rounding, which
			// the clamps take): truncation is the floor, without the library call (G64: this runs for every texel of the
			// minimap).
			const int32 PU = FMath::Clamp(static_cast<int32>((UV.X + 1.0) * PixelsPerUV), 0, FacePixels - 1);
			const int32 PV = FMath::Clamp(static_cast<int32>((UV.Y + 1.0) * PixelsPerUV), 0, FacePixels - 1);
			const int32 U0 = bResPow2 ? PU >> ResShift : PU / Res;
			const int32 V0 = bResPow2 ? PV >> ResShift : PV / Res;
			const FIntVector FogCell(Face, U0 >> FogShift, V0 >> FogShift);
			if (FogCell != CachedFogCell)
			{
				CachedFogCell = FogCell;
				bCachedExplored = !bRequireExplored || Explored.IsAnyExplored(Face, U0, V0, FogShift);
			}
			if (!bCachedExplored)
			{
				Write(X, Y, Unexplored);
				return;
			}
			// The view's tile at its level, its image or the nearest coarser one's.
			const int32 TU = bResPow2 ? PU >> (ViewLevel + ResShift) : (PU >> ViewLevel) / Res;
			const int32 TV = bResPow2 ? PV >> (ViewLevel + ResShift) : (PV >> ViewLevel) / Res;
			const FIntPoint ViewTile(Face * ViewTilesPerEdge + TU, TV);
			if (ViewTile != CachedViewTile)
			{
				CachedViewTile = ViewTile;
				Resolve(Face, TU, TV);
			}
			if (ViewTiles)
			{
				Band.Collector.Touch(ViewTile, X, Y);
			}
			if (!CachedImage)
			{
				Write(X, Y, Undrawn);
				return;
			}
			const int32 PX = FMath::Clamp((PU >> CachedShift) - CachedBaseU, 0, Res - 1);
			const int32 PY = FMath::Clamp((PV >> CachedShift) - CachedBaseV, 0, Res - 1);
			Write(X, Y, CachedImage->PixelData[PY * Res + PX]);
			++Band.Drawn;
		};

		for (int32 CX = 0; CX + 1 < Points; ++CX)
		{
			const FControl& C00 = Controls[CY * Points + CX];
			const FControl& C10 = Controls[CY * Points + CX + 1];
			const FControl& C01 = Controls[(CY + 1) * Points + CX];
			const FControl& C11 = Controls[(CY + 1) * Points + CX + 1];
			const bool bOneFace = C00.Face >= 0 && C00.Face == C10.Face && C00.Face == C01.Face && C00.Face == C11.Face;
			const int32 X0 = CX * Step;
			const int32 Y0 = CY * Step;
			const int32 X1 = FMath::Min(X0 + Step, TexSize);
			const int32 Y1 = FMath::Min(Y0 + Step, TexSize);
			if (bOneFace)
			{
				// One face (G64): UV is bilinear over the block, so along a row it steps by a constant from the row's left
				// edge (the same interpolation as below, taken across the row last).
				const double InvStep = 1.0 / Step;
				for (int32 Y = Y0; Y < Y1; ++Y)
				{
					const double TY = (Y + 0.5 - Y0) * InvStep;
					const FVector2D Left = FMath::Lerp(C00.UV, C01.UV, TY);
					const FVector2D Delta = (FMath::Lerp(C10.UV, C11.UV, TY) - Left) * InvStep;
					FVector2D UV = Left + Delta * 0.5;
					for (int32 X = X0; X < X1; ++X, UV += Delta)
					{
						Shade(X, Y, C00.Face, UV);
					}
				}
				continue;
			}
			for (int32 Y = Y0; Y < Y1; ++Y)
			{
				for (int32 X = X0; X < X1; ++X)
				{
					// The block spans faces (or leaves the ground): each texel's own direction.
					FVector Direction;
					if (!TexelDirection(X + 0.5, Y + 0.5, Direction))
					{
						Write(X, Y, Background);
						continue;
					}
					const FVoxelSurfaceCoord Coord = FVoxelCubeSphere::DirectionToSurface(Direction);
					Shade(X, Y, Coord.Face, Coord.UV);
				}
			}
		}
	}, ParallelFlags());
	return MergeBands(Map, Bands, TexSize, ViewTiles);
}

// ==================== FVCPlaneMapSampler ====================

int32 FVCPlaneMapSampler::Fill(UVoxelMapSubsystem& Map, uint8* OutBGRA, int32 TexSize, const FVector& Center, double WorldPerPixel) const
{
	const double TileSize = Map.GetTileWorldSize();
	const int32 Res = Map.GetTileResolution();
	if (!OutBGRA || TexSize <= 0 || TileSize <= 0.0 || WorldPerPixel <= 0.0 || Res <= 0)
	{
		return 0;
	}
	const FVoxelMapTileFrame& Frame = Map.GetTileFrame();
	const int32 ViewLevel = FMath::Clamp(Level, 0, Frame.GetMaxLevel());
	const double Extent = 0.5 * TexSize * WorldPerPixel;
	const double WorldMinX = Center.X - Extent;
	const double WorldMinY = Center.Y - Extent;
	const double WorldMaxX = Center.X + Extent;

	// Along each texture axis, worked out once per column and row: the texel's level-0 pixel (the pixel just inside the
	// texel's far corner in world +X and +Y: texel X runs along world +X (or +Y north up), texel Y along world +Y (or -X
	// north up, where the far corner in X is the texel's first edge)), its level-0 tile, its pixel at the view's level and
	// that level's tile.
	const FVector GridOrigin = Map.TileCoordToWorld(FIntPoint(0, 0));
	const double Inside = WorldPerPixel * 1.0e-4;
	struct FAxis
	{
		int32 Tile0 = 0;
		int64 ViewPixel = 0;
		int32 ViewTile = 0;
	};
	TArray<FAxis> Columns, Rows;
	Columns.SetNumUninitialized(TexSize);
	Rows.SetNumUninitialized(TexSize);
	auto MakeAxis = [Res, ViewLevel](double AlongTiles)
	{
		FAxis Axis;
		const int64 Pixel0 = static_cast<int64>(FMath::FloorToDouble(AlongTiles * Res));
		Axis.Tile0 = static_cast<int32>(FloorDiv(Pixel0, Res));
		Axis.ViewPixel = Pixel0 >> ViewLevel;
		Axis.ViewTile = static_cast<int32>(FloorDiv(Axis.ViewPixel, Res));
		return Axis;
	};
	for (int32 I = 0; I < TexSize; ++I)
	{
		// Along texel X: world X, or world Y north up; along texel Y: world Y, or world X north up (descending).
		Columns[I] = MakeAxis(bNorthUp
			? (WorldMinY + (I + 1) * WorldPerPixel - Inside - GridOrigin.Y) / TileSize
			: (WorldMinX + (I + 1) * WorldPerPixel - Inside - GridOrigin.X) / TileSize);
		Rows[I] = MakeAxis(bNorthUp
			? (WorldMaxX - I * WorldPerPixel - Inside - GridOrigin.X) / TileSize
			: (WorldMinY + (I + 1) * WorldPerPixel - Inside - GridOrigin.Y) / TileSize);
	}

	// Bands of rows on the task graph's workers, as the planet's fill (MAP_PYRAMID_PLAN.md P3).
	const int32 FogShift = FMath::Max(0, ViewLevel - static_cast<int32>(FMath::FloorLog2(static_cast<uint32>(Res))));
	const int32 MaxLevel = Frame.GetMaxLevel();
	const int32 NumBands = (TexSize + FillBandRows - 1) / FillBandRows;
	TArray<FVCFillBand> Bands;
	Bands.SetNum(NumBands);
	ParallelFor(TEXT("VCMapFill_PlaneBands"), NumBands, 1, [&](int32 BandIndex)
	{
		FVCFillBand& Band = Bands[BandIndex];
		// The fog cell the last texel fell in (its exploration: a pixel at the view's level covers 2^FogShift level-0 tiles
		// a side, explored where any of them is, read from the explored mask, MAP_PYRAMID_PLAN.md P2), and the view's tile
		// it read with the image it drew from: the tile's own at the view's level, or the nearest coarser one stored over it.
		FVoxelMapExploredMask::FReader Explored(Map.GetExploredMask());
		FIntPoint CachedFogCell(MIN_int32, MIN_int32);
		bool bCachedExplored = false;
		FIntPoint CachedViewTile(MIN_int32, MIN_int32);
		const FVoxelMapTile* CachedImage = nullptr;
		int32 CachedUp = 0;
		int64 CachedBaseX = 0;
		int64 CachedBaseY = 0;
		auto Resolve = [&](const FIntPoint& ViewTile)
		{
			CachedImage = nullptr;
			for (int32 Up = 0; Up <= MaxFallbackLevels && ViewLevel + Up <= MaxLevel; ++Up)
			{
				const FIntPoint Tile(ViewTile.X >> Up, ViewTile.Y >> Up);
				const FVoxelMapTileId Id(Tile, ViewLevel + Up);
				const FVoxelMapTile* Image = Map.FindTile(Id, /*bTouch*/ false);
				if (Image && Image->Resolution == Res && Image->PixelData.Num() == Res * Res)
				{
					Band.Used(Id);
					CachedImage = Image;
					CachedUp = Up;
					CachedBaseX = static_cast<int64>(Tile.X) * Res;
					CachedBaseY = static_cast<int64>(Tile.Y) * Res;
					return;
				}
			}
		};
		const int32 YEnd = FMath::Min(TexSize, (BandIndex + 1) * FillBandRows);
		for (int32 Y = BandIndex * FillBandRows; Y < YEnd; ++Y)
		{
			uint8* Row = OutBGRA + static_cast<int64>(Y) * TexSize * 4;
			for (int32 X = 0; X < TexSize; ++X)
			{
				// North up the texel's column runs along world Y and its row along world X.
				const FAxis& AlongX = bNorthUp ? Rows[Y] : Columns[X];
				const FAxis& AlongY = bNorthUp ? Columns[X] : Rows[Y];
				const FIntPoint FogCell(AlongX.Tile0 >> FogShift, AlongY.Tile0 >> FogShift);
				if (FogCell != CachedFogCell)
				{
					CachedFogCell = FogCell;
					bCachedExplored = !bRequireExplored || Explored.IsAnyExplored(0, AlongX.Tile0, AlongY.Tile0, FogShift);
				}
				if (!bCachedExplored)
				{
					WriteTexel(Row + X * 4, Unexplored);
					continue;
				}
				const FIntPoint ViewTile(AlongX.ViewTile, AlongY.ViewTile);
				if (ViewTile != CachedViewTile)
				{
					CachedViewTile = ViewTile;
					Resolve(ViewTile);
				}
				if (ViewTiles)
				{
					Band.Collector.Touch(ViewTile, X, Y);
				}
				if (!CachedImage)
				{
					WriteTexel(Row + X * 4, bRequireExplored ? Undrawn : Background);
					continue;
				}
				const int32 PX = FMath::Clamp(static_cast<int32>((AlongX.ViewPixel >> CachedUp) - CachedBaseX), 0, Res - 1);
				const int32 PY = FMath::Clamp(static_cast<int32>((AlongY.ViewPixel >> CachedUp) - CachedBaseY), 0, Res - 1);
				WriteTexel(Row + X * 4, CachedImage->PixelData[PY * Res + PX]);
				++Band.Drawn;
			}
		}
	}, ParallelFlags());
	return MergeBands(Map, Bands, TexSize, ViewTiles);
}
