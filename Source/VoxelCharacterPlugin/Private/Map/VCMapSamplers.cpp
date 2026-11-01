// Copyright Daniel Raquel. All Rights Reserved.

#include "Map/VCMapSamplers.h"
#include "VoxelCubeSphere.h"
#include "VoxelMapSubsystem.h"

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

// ==================== FVCPlanetMapSampler ====================

int32 FVCPlanetMapSampler::Fill(UVoxelMapSubsystem& Map, uint8* OutBGRA, int32 TexSize, int32 Step,
	TFunctionRef<bool(double X, double Y, FVector& OutDirection)> TexelDirection) const
{
	const FVoxelCubeSphereGrid& Grid = Map.GetPlanetTileGrid();
	const int32 Res = Map.GetTileResolution();
	if (!Grid.IsValid() || Res <= 0 || TexSize <= 0 || !OutBGRA)
	{
		return 0;
	}
	const int32 N = Grid.GetCellsPerEdge();
	const double CellUV = Grid.GetCellUV();
	const double InvCellUV = 1.0 / CellUV;
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
	for (int32 CY = 0; CY < Points; ++CY)
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
	}

	// The tile the last texel read, cached across texels (texels in a run share tiles).
	uint64 CachedKey = MAX_uint64;
	const FVoxelMapTile* CachedTile = nullptr;
	bool bCachedExplored = false;
	TSet<FIntPoint> Requested;
	int32 Drawn = 0;

	auto Write = [OutBGRA, TexSize](int32 X, int32 Y, const FColor& Color)
	{
		uint8* Texel = OutBGRA + (static_cast<int64>(Y) * TexSize + X) * 4;
		Texel[0] = Color.B;
		Texel[1] = Color.G;
		Texel[2] = Color.R;
		Texel[3] = Color.A;
	};
	// Zoomed out: the globe albedo (WM-BP11), when it is there.
	const FVoxelMapGlobeAlbedo* Albedo = bUseGlobeAlbedo ? Map.GetGlobeAlbedo() : nullptr;
	const int32 AlbedoN = Albedo ? Albedo->Grid.GetCellsPerEdge() : 0;
	auto Shade = [&](int32 X, int32 Y, int32 Face, const FVector2D& UV)
	{
		if (Albedo)
		{
			const int32 AU = FMath::Clamp(FMath::FloorToInt32((UV.X + 1.0) * 0.5 * AlbedoN), 0, AlbedoN - 1);
			const int32 AV = FMath::Clamp(FMath::FloorToInt32((UV.Y + 1.0) * 0.5 * AlbedoN), 0, AlbedoN - 1);
			const FIntPoint Cell = Albedo->Grid.ToAtlas(Face, AU, AV);
			if (bRequireExplored && !Map.IsGlobeCellExplored(Cell))
			{
				Write(X, Y, Unexplored);
				return;
			}
			Write(X, Y, Albedo->At(Cell));
			++Drawn;
			return;
		}
		// UV >= -1, so FU and FV are not negative (but by rounding, which the clamps take): truncation is the floor, without
		// the library call (G64: this runs for every texel of the minimap).
		const double FU = (UV.X + 1.0) * InvCellUV;
		const double FV = (UV.Y + 1.0) * InvCellUV;
		const int32 IU = FMath::Clamp(static_cast<int32>(FU), 0, N - 1);
		const int32 IV = FMath::Clamp(static_cast<int32>(FV), 0, N - 1);
		const FIntPoint Tile = Grid.ToAtlas(Face, IU, IV);
		const uint64 Key = (static_cast<uint64>(static_cast<uint32>(Tile.X)) << 32) | static_cast<uint32>(Tile.Y);
		if (Key != CachedKey)
		{
			CachedKey = Key;
			if (VisitedTiles)
			{
				VisitedTiles->Add(Tile);
			}
			bCachedExplored = !bRequireExplored || Map.IsTileExplored(Tile);
			CachedTile = bCachedExplored ? Map.GetTile(Tile) : nullptr;
			const bool bNeedsImage = bCachedExplored && (!CachedTile || (bWantDetail && CachedTile->Resolution < Res));
			if (bNeedsImage && bRequireExplored && !Requested.Contains(Tile))
			{
				Requested.Add(Tile);
				Map.RequestTileImage(Tile, bWantDetail);
			}
		}
		if (!bCachedExplored)
		{
			Write(X, Y, Unexplored);
			return;
		}
		if (!CachedTile || CachedTile->Resolution <= 0)
		{
			Write(X, Y, Undrawn);
			return;
		}
		const int32 TileRes = CachedTile->Resolution;
		const int32 PX = FMath::Clamp(static_cast<int32>((FU - IU) * TileRes), 0, TileRes - 1);
		const int32 PY = FMath::Clamp(static_cast<int32>((FV - IV) * TileRes), 0, TileRes - 1);
		Write(X, Y, CachedTile->PixelData[PY * TileRes + PX]);
		++Drawn;
	};

	for (int32 CY = 0; CY + 1 < Points; ++CY)
	{
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
	}
	return Drawn;
}

// ==================== FVCPlaneMapSampler ====================

int32 FVCPlaneMapSampler::Fill(UVoxelMapSubsystem& Map, uint8* OutBGRA, int32 TexSize, const FVector& Center, double WorldPerPixel) const
{
	const double TileSize = Map.GetTileWorldSize();
	if (!OutBGRA || TexSize <= 0 || TileSize <= 0.0 || WorldPerPixel <= 0.0)
	{
		return 0;
	}
	const double Extent = 0.5 * TexSize * WorldPerPixel;
	const double WorldMinX = Center.X - Extent;
	const double WorldMinY = Center.Y - Extent;
	const double WorldMaxX = Center.X + Extent;
	const int32 TileResolution = Map.GetTileResolution();
	auto Write = [](uint8* Texel, const FColor& Color)
	{
		Texel[0] = Color.B;
		Texel[1] = Color.G;
		Texel[2] = Color.R;
		Texel[3] = Color.A;
	};
	// The tile at a coordinate as the fill may draw it: explored (or every tile without bRequireExplored), with a whole image.
	// An explored tile without the image asked for is requested once a fill (bRequireExplored).
	TSet<FIntPoint> Requested;
	auto Resolve = [&](const FIntPoint& Coord, bool& bOutExplored) -> const FVoxelMapTile*
	{
		if (VisitedTiles)
		{
			VisitedTiles->Add(Coord);
		}
		bOutExplored = !bRequireExplored || Map.IsTileExplored(Coord);
		const FVoxelMapTile* Tile = bOutExplored ? Map.GetTile(Coord) : nullptr;
		if (bRequireExplored && bOutExplored && (!Tile || (bWantDetail && Tile->Resolution < TileResolution)) && !Requested.Contains(Coord))
		{
			// Drawn again in the background; the map redraws when it lands.
			Requested.Add(Coord);
			Map.RequestTileImage(Coord, bWantDetail);
		}
		if (Tile && (Tile->Resolution <= 0 || Tile->PixelData.Num() < Tile->Resolution * Tile->Resolution))
		{
			Tile = nullptr;
		}
		return Tile;
	};
	// Where a tile pixel at world (X, Y) lands: texel (X, Y) from the texture's min corner, or north up.
	auto TexelOf = [&](double X, double Y, int32& OutTX, int32& OutTY)
	{
		if (bNorthUp)
		{
			OutTX = FMath::FloorToInt32((Y - WorldMinY) / WorldPerPixel);
			OutTY = FMath::FloorToInt32((WorldMaxX - X) / WorldPerPixel);
		}
		else
		{
			OutTX = FMath::FloorToInt32((X - WorldMinX) / WorldPerPixel);
			OutTY = FMath::FloorToInt32((Y - WorldMinY) / WorldPerPixel);
		}
	};
	int32 Drawn = 0;

	if (!bTexelFill)
	{
		// As before G64: clear to the background, then blit every pixel of every tile in view at its near corner (an explored
		// tile without an image blits Undrawn over its pixels' places).
		for (int32 I = 0; I < TexSize * TexSize; ++I)
		{
			Write(OutBGRA + static_cast<int64>(I) * 4, bRequireExplored ? Unexplored : Background);
		}
		const FIntPoint CenterTile = Map.WorldToTileCoord(Center);
		const int32 TileRadius = FMath::CeilToInt(Extent / TileSize);
		for (int32 TY = -TileRadius; TY <= TileRadius; ++TY)
		{
			for (int32 TX = -TileRadius; TX <= TileRadius; ++TX)
			{
				const FIntPoint TileCoord(CenterTile.X + TX, CenterTile.Y + TY);
				bool bExplored = false;
				const FVoxelMapTile* Tile = Resolve(TileCoord, bExplored);
				if (!Tile && !(bRequireExplored && bExplored))
				{
					continue;
				}
				const FVector TileWorldOrigin = Map.TileCoordToWorld(TileCoord);
				const int32 SrcRes = Tile ? Tile->Resolution : TileResolution;
				const double SrcPixelWorldSize = TileSize / SrcRes;
				for (int32 PY = 0; PY < SrcRes; ++PY)
				{
					for (int32 PX = 0; PX < SrcRes; ++PX)
					{
						int32 DstX, DstY;
						TexelOf(TileWorldOrigin.X + PX * SrcPixelWorldSize, TileWorldOrigin.Y + PY * SrcPixelWorldSize, DstX, DstY);
						if (DstX < 0 || DstX >= TexSize || DstY < 0 || DstY >= TexSize)
						{
							continue;
						}
						uint8* Texel = OutBGRA + (static_cast<int64>(DstY) * TexSize + DstX) * 4;
						if (Tile)
						{
							Write(Texel, Tile->PixelData[PY * SrcRes + PX]);
							++Drawn;
						}
						else
						{
							Write(Texel, Undrawn);
						}
					}
				}
			}
		}
		return Drawn;
	}

	// Texel by texel: along each texture axis the tile and the position in it, worked out once per column and row. The
	// sample point is just inside the texel's far corner in world +X and +Y: texel X runs along world +X (or +Y north up),
	// texel Y along world +Y (or -X north up, where the far corner in X is the texel's first edge).
	const FVector GridOrigin = Map.TileCoordToWorld(FIntPoint(0, 0));
	const double Inside = WorldPerPixel * 1.0e-4;
	TArray<int32> ColumnTile, RowTile;
	TArray<double> ColumnFraction, RowFraction;
	ColumnTile.SetNumUninitialized(TexSize);
	RowTile.SetNumUninitialized(TexSize);
	ColumnFraction.SetNumUninitialized(TexSize);
	RowFraction.SetNumUninitialized(TexSize);
	for (int32 I = 0; I < TexSize; ++I)
	{
		// Along texel X: world X, or world Y north up; along texel Y: world Y, or world X north up (descending).
		const double AlongX = bNorthUp
			? (WorldMinY + (I + 1) * WorldPerPixel - Inside - GridOrigin.Y) / TileSize
			: (WorldMinX + (I + 1) * WorldPerPixel - Inside - GridOrigin.X) / TileSize;
		const double AlongY = bNorthUp
			? (WorldMaxX - I * WorldPerPixel - Inside - GridOrigin.X) / TileSize
			: (WorldMinY + (I + 1) * WorldPerPixel - Inside - GridOrigin.Y) / TileSize;
		ColumnTile[I] = FMath::FloorToInt32(AlongX);
		RowTile[I] = FMath::FloorToInt32(AlongY);
		ColumnFraction[I] = AlongX - ColumnTile[I];
		RowFraction[I] = AlongY - RowTile[I];
	}
	FIntPoint CachedCoord(MAX_int32, MAX_int32);
	const FVoxelMapTile* Tile = nullptr;
	bool bExplored = false;
	for (int32 Y = 0; Y < TexSize; ++Y)
	{
		uint8* Row = OutBGRA + static_cast<int64>(Y) * TexSize * 4;
		for (int32 X = 0; X < TexSize; ++X)
		{
			// North up the texel's column picks the tile's Y and the pixel's PY, its row the tile's X and PX.
			const FIntPoint Coord = bNorthUp ? FIntPoint(RowTile[Y], ColumnTile[X]) : FIntPoint(ColumnTile[X], RowTile[Y]);
			if (Coord != CachedCoord)
			{
				CachedCoord = Coord;
				Tile = Resolve(Coord, bExplored);
			}
			if (!Tile)
			{
				Write(Row + X * 4, bRequireExplored ? (bExplored ? Undrawn : Unexplored) : Background);
				continue;
			}
			const int32 Res = Tile->Resolution;
			const int32 PX = FMath::Min(static_cast<int32>((bNorthUp ? RowFraction[Y] : ColumnFraction[X]) * Res), Res - 1);
			const int32 PY = FMath::Min(static_cast<int32>((bNorthUp ? ColumnFraction[X] : RowFraction[Y]) * Res), Res - 1);
			Write(Row + X * 4, Tile->PixelData[PY * Res + PX]);
			++Drawn;
		}
	}
	return Drawn;
}
