// Copyright Daniel Raquel. All Rights Reserved.

#include "Map/VCPlanetMapProjection.h"
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
		const double FU = (UV.X + 1.0) / CellUV;
		const double FV = (UV.Y + 1.0) / CellUV;
		const int32 IU = FMath::Clamp(FMath::FloorToInt32(FU), 0, N - 1);
		const int32 IV = FMath::Clamp(FMath::FloorToInt32(FV), 0, N - 1);
		const FIntPoint Tile = Grid.ToAtlas(Face, IU, IV);
		const uint64 Key = (static_cast<uint64>(static_cast<uint32>(Tile.X)) << 32) | static_cast<uint32>(Tile.Y);
		if (Key != CachedKey)
		{
			CachedKey = Key;
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
		const int32 PX = FMath::Clamp(FMath::FloorToInt32((FU - IU) * TileRes), 0, TileRes - 1);
		const int32 PY = FMath::Clamp(FMath::FloorToInt32((FV - IV) * TileRes), 0, TileRes - 1);
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
			for (int32 Y = Y0; Y < Y1; ++Y)
			{
				for (int32 X = X0; X < X1; ++X)
				{
					if (bOneFace)
					{
						const double TX = (X + 0.5 - X0) / Step;
						const double TY = (Y + 0.5 - Y0) / Step;
						const FVector2D UV = FMath::Lerp(FMath::Lerp(C00.UV, C10.UV, TX), FMath::Lerp(C01.UV, C11.UV, TX), TY);
						Shade(X, Y, C00.Face, UV);
						continue;
					}
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
