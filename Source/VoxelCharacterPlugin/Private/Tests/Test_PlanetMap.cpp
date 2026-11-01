// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Map/VCMapSamplers.h"
#include "Map/VCWorldMapWidget.h"
#include "Misc/Parse.h"
#include "VoxelMapSubsystem.h"
#include "VoxelCubeSphere.h"
#include "Testing/VoxelTestChunkWorld.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformProcess.h"

// ---------------------------------------------------------------------------
// The maps on a planet, the character side (VoxelWorlds WORLD_MODES_REVIEW_PLAN.md §18, WM-SY9; SP-G3, D11):
// - MinimapFrame: the tangent plane under the player, north toward +Z, the heading from north, the plane's rotation
//   carried over (straight ahead points up), offsets and directions inverse to each other, latitude and longitude.
// - WorldMapProjection: the globe's orthographic view, north up, round trips, the far side hidden, drags that move the
//   content with the mouse.
// - MapSampler: a minimap texture filled from a small planet's tiles shows at every sampled texel the tile pixel its
//   exact direction falls in (the control-point interpolation inside one face, texel by texel across an edge).
// ---------------------------------------------------------------------------

#define VC_PLANET_MAP_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCPlanet_MinimapFrame, "VoxelCharacter.Planet.MinimapFrame", VC_PLANET_MAP_FLAGS)
bool FVCPlanet_MinimapFrame::RunTest(const FString& Parameters)
{
	const FVector Center(1000.0, -2000.0, 500.0);
	constexpr double Radius = 250000.0;

	// At the equator facing +X: east +Y, north +Z (the plane's map: north +X, east +Y).
	const FVCPlanetMapFrame Equator = FVCPlanetMapFrame::At(Center, Radius, Center + FVector(Radius + 300.0, 0, 0));
	TestTrue(TEXT("Equator: up +X"), Equator.Up.Equals(FVector(1, 0, 0), 1.0e-9));
	TestTrue(TEXT("Equator: east +Y"), Equator.East.Equals(FVector(0, 1, 0), 1.0e-9));
	TestTrue(TEXT("Equator: north +Z"), Equator.North.Equals(FVector(0, 0, 1), 1.0e-9));
	const FVector2D EquatorLatLon = Equator.GetLatLon();
	TestTrue(TEXT("Equator: latitude 0, longitude 0"), FMath::IsNearlyZero(EquatorLatLon.X, 1.0e-9) && FMath::IsNearlyZero(EquatorLatLon.Y, 1.0e-9));
	const double C45 = FMath::Cos(FMath::DegreesToRadians(45.0));
	const FVector2D LatLon45 = FVCPlanetMapFrame::At(Center, Radius, Center + FVector(0, C45, C45) * Radius).GetLatLon();
	TestTrue(TEXT("Latitude 45, longitude 90"), FMath::IsNearlyEqual(LatLon45.X, 45.0, 1.0e-6) && FMath::IsNearlyEqual(LatLon45.Y, 90.0, 1.0e-6));

	FRandomStream Random(29);
	int32 BadFrame = 0, BadNorth = 0, BadRoundTrip = 0, BadHeading = 0, BadAhead = 0;
	TArray<FVector> Ups = { FVector(0, 0, 1), FVector(0, 0, -1), FVector(1, 1, 1).GetSafeNormal() };
	for (int32 i = 0; i < 300; ++i)
	{
		Ups.Add(Random.GetUnitVector());
	}
	for (const FVector& Up : Ups)
	{
		const FVCPlanetMapFrame Frame = FVCPlanetMapFrame::At(Center, Radius, Center + Up * (Radius + Random.FRandRange(-3000.0, 3000.0)));
		const bool bOrthonormal = FMath::IsNearlyEqual(Frame.East.Size(), 1.0, 1.0e-9) && FMath::IsNearlyEqual(Frame.North.Size(), 1.0, 1.0e-9)
			&& FMath::Abs(FVector::DotProduct(Frame.East, Frame.North)) < 1.0e-9 && FMath::Abs(FVector::DotProduct(Frame.East, Frame.Up)) < 1.0e-9
			&& FMath::Abs(FVector::DotProduct(Frame.North, Frame.Up)) < 1.0e-9
			&& FVector::CrossProduct(Frame.East, Frame.North).Equals(Frame.Up, 1.0e-9);
		BadFrame += bOrthonormal ? 0 : 1;
		BadNorth += (FMath::Abs(Up.Z) < 0.9999 && FVector::DotProduct(Frame.North, FVector::UpVector) <= 0.0) ? 1 : 0;

		// Offsets and directions are inverse to each other within 20 km.
		const FVector2D Offset(Random.FRandRange(-20000.0, 20000.0), Random.FRandRange(-20000.0, 20000.0));
		const FVector2D Back = Frame.OffsetOf(Center + Frame.DirectionAtOffset(Offset.X, Offset.Y) * (Radius + 500.0));
		BadRoundTrip += (Back - Offset).GetAbsMax() > 1.0e-4 ? 1 : 0;

		// A heading H: forward = cos H north + sin H east; the minimap's rotation (-H - 90) puts a point ahead straight up.
		const double Heading = Random.FRandRange(-179.0, 179.0);
		const FVector Forward = Frame.North * FMath::Cos(FMath::DegreesToRadians(Heading)) + Frame.East * FMath::Sin(FMath::DegreesToRadians(Heading));
		BadHeading += FMath::Abs(Frame.HeadingDegrees(Forward + Frame.Up * 0.3) - Heading) > 1.0e-6 ? 1 : 0;
		const FVector Ahead = Center + Frame.DirectionAtOffset(1000.0 * FMath::Cos(FMath::DegreesToRadians(Heading)), 1000.0 * FMath::Sin(FMath::DegreesToRadians(Heading))) * Radius;
		const FVector2D OnScreen = Frame.OffsetOf(Ahead).GetRotated(static_cast<float>(-Heading - 90.0));
		BadAhead += (FMath::Abs(OnScreen.X) > 0.01 || FMath::Abs(OnScreen.Y + 1000.0) > 0.01) ? 1 : 0;
	}
	TestEqual(TEXT("Every frame is orthonormal and right-handed (east x north = up)"), BadFrame, 0);
	TestEqual(TEXT("North points toward the +Z pole off the poles"), BadNorth, 0);
	TestEqual(TEXT("OffsetOf inverts DirectionAtOffset"), BadRoundTrip, 0);
	TestEqual(TEXT("The heading is measured from north toward east, across local up"), BadHeading, 0);
	TestEqual(TEXT("With the minimap's rotation a point straight ahead is straight up"), BadAhead, 0);
	TestTrue(TEXT("The far hemisphere has no offset"), FVCPlanetMapFrame::At(Center, Radius, Center + FVector(0, 0, Radius)).OffsetOf(Center - FVector(0, 0, Radius)).IsZero());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCPlanet_WorldMapProjection, "VoxelCharacter.Planet.WorldMapProjection", VC_PLANET_MAP_FLAGS)
bool FVCPlanet_WorldMapProjection::RunTest(const FString& Parameters)
{
	FRandomStream Random(31);
	int32 BadCentre = 0, BadRoundTrip = 0, BadNorthUp = 0, BadDrag = 0, BadDragNorth = 0;
	for (int32 i = 0; i < 200; ++i)
	{
		const FVector View = i == 0 ? FVector(0, 0, 1) : (i == 1 ? FVector(0, 0, -1) : Random.GetUnitVector());
		const double RadiusPx = Random.FRandRange(150.0, 5000.0);
		const FVCGlobeProjection Globe = FVCGlobeProjection::Make(View, RadiusPx);

		FVector AtCentre;
		FVector2D CentreScreen;
		BadCentre += (Globe.ScreenToDirection(FVector2D::ZeroVector, AtCentre) && AtCentre.Equals(Globe.View, 1.0e-9)
			&& Globe.DirectionToScreen(Globe.View, CentreScreen) && CentreScreen.IsNearlyZero(1.0e-6)) ? 0 : 1;

		const FVector2D Screen(Random.FRandRange(-0.7, 0.7) * RadiusPx, Random.FRandRange(-0.7, 0.7) * RadiusPx);
		FVector Direction;
		FVector2D Back;
		BadRoundTrip += (Globe.ScreenToDirection(Screen, Direction) && Globe.DirectionToScreen(Direction, Back) && (Back - Screen).GetAbsMax() < 1.0e-6) ? 0 : 1;

		// North up, east right.
		FVector2D NorthScreen, EastScreen;
		Globe.DirectionToScreen((Globe.View + Globe.North * 0.01).GetSafeNormal(), NorthScreen);
		Globe.DirectionToScreen((Globe.View + Globe.East * 0.01).GetSafeNormal(), EastScreen);
		BadNorthUp += (NorthScreen.Y < 0.0 && FMath::Abs(NorthScreen.X) < 1.0e-6 * RadiusPx && EastScreen.X > 0.0 && FMath::Abs(EastScreen.Y) < 1.0e-6 * RadiusPx) ? 0 : 1;

		// A drag moves the content with the mouse: what showed at -Delta is at the centre, and the old centre shows |Delta|
		// away, along Delta (within the meridians' convergence, which north-up adds: away from the poles); north stays up.
		const FVector2D Delta(Random.FRandRange(-4.0, 4.0), Random.FRandRange(-4.0, 4.0));
		const FVCGlobeProjection Dragged = Globe.Dragged(Delta);
		FVector Expected;
		Globe.ScreenToDirection(-Delta, Expected);
		FVector2D Moved;
		const bool bMoved = Dragged.DirectionToScreen(Globe.View, Moved);
		const bool bAlong = FMath::Abs(Globe.View.Z) > 0.8 || FMath::Abs(FMath::Acos(FMath::Clamp(FVector2D::DotProduct(Moved.GetSafeNormal(), Delta.GetSafeNormal()), -1.0, 1.0))) < 0.15;
		BadDrag += (Dragged.View.Equals(Expected, 1.0e-9) && bMoved && FMath::Abs(Moved.Size() - Delta.Size()) < 1.0e-6 * RadiusPx && bAlong) ? 0 : 1;
		BadDragNorth += (FMath::Abs(Dragged.View.Z) < 0.999 && FVector::DotProduct(Dragged.North, FVector::UpVector) <= 0.0) ? 1 : 0;
	}
	TestEqual(TEXT("The view's centre shows the view direction"), BadCentre, 0);
	TestEqual(TEXT("ScreenToDirection and DirectionToScreen invert on the disc"), BadRoundTrip, 0);
	TestEqual(TEXT("North up, east right"), BadNorthUp, 0);
	TestEqual(TEXT("A drag moves the content with the mouse"), BadDrag, 0);
	TestEqual(TEXT("North stays up after a drag"), BadDragNorth, 0);

	const FVCGlobeProjection Globe = FVCGlobeProjection::Make(FVector(1, 0, 0), 400.0);
	FVector Direction;
	FVector2D Screen;
	TestFalse(TEXT("Off the disc shows nothing"), Globe.ScreenToDirection(FVector2D(300.0, 300.0), Direction));
	TestFalse(TEXT("The far side is hidden"), Globe.DirectionToScreen(FVector(-1, 0.1, 0).GetSafeNormal(), Screen));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCPlanet_MapSampler, "VoxelCharacter.Planet.MapSampler", VC_PLANET_MAP_FLAGS)
bool FVCPlanet_MapSampler::RunTest(const FString& Parameters)
{
	VoxelTesting::FTestChunkWorld W;
	W.Create(TEXT("VCPlanetMapSampler"));
	W.ConfigureSmallPlanet();
	W.StartManager();
	UVoxelMapSubsystem* Map = W.World->GetSubsystem<UVoxelMapSubsystem>();
	const FVector Center = W.Config->WorldOrigin;
	const double Radius = W.Config->WorldRadius;
	if (TestNotNull(TEXT("The game world has a map"), Map))
	{
		// A minimap at a cube corner (three faces in view) and one at 45 degrees.
		for (const FVector& Site : { FVector(1, 1, 1).GetSafeNormal(), FVector(0.7071, 0.0, 0.7071).GetSafeNormal() })
		{
			const FVector Player = Center + Site * (Radius + 200.0);
			constexpr double Extent = 3000.0;
			Map->RequestTilesInRadius(Player, static_cast<float>(Extent * 1.5));
			const double Deadline = FPlatformTime::Seconds() + 60.0;
			while (!Map->GetDebugStats().Contains(TEXT("waiting=0 drawing=0")) && FPlatformTime::Seconds() < Deadline)
			{
				FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
				FPlatformProcess::Sleep(0.005f);
			}
			TestTrue(TEXT("The tiles drew"), Map->GetDebugStats().Contains(TEXT("waiting=0 drawing=0")));

			constexpr int32 TexSize = 256;
			const double WorldPerPixel = 2.0 * Extent / TexSize;
			const FVCPlanetMapFrame Frame = FVCPlanetMapFrame::At(Center, Radius, Player);
			auto TexelDirection = [&Frame, WorldPerPixel, Extent](double X, double Y, FVector& OutDirection)
			{
				OutDirection = Frame.DirectionAtOffset(X * WorldPerPixel - Extent, Y * WorldPerPixel - Extent);
				return true;
			};
			TArray<uint8> Texels;
			Texels.SetNumZeroed(TexSize * TexSize * 4);
			FVCPlanetMapSampler Sampler;
			Sampler.bRequireExplored = false;
			const int32 Drawn = Sampler.Fill(*Map, Texels.GetData(), TexSize, 16, TexelDirection);

			// Each texel against the tile pixel of its exact direction (texels at a pixel boundary may round either way).
			int32 Exact = 0, Faces = 0;
			TSet<int32> FaceSet;
			const int32 Res = Map->GetTileResolution();
			for (int32 Y = 0; Y < TexSize; ++Y)
			{
				for (int32 X = 0; X < TexSize; ++X)
				{
					FVector Direction;
					TexelDirection(X + 0.5, Y + 0.5, Direction);
					FIntPoint Tile;
					int32 PX = 0, PY = 0;
					Map->PlanetDirectionToTilePixel(Direction, Tile, PX, PY);
					FaceSet.Add(Tile.X / Map->GetPlanetTileGrid().GetCellsPerEdge());
					const FVoxelMapTile* Image = Map->GetTile(Tile);
					if (!Image || Image->Resolution != Res)
					{
						continue;
					}
					const FColor Expected = Image->PixelData[PY * Res + PX];
					const uint8* Got = Texels.GetData() + (Y * TexSize + X) * 4;
					Exact += (Got[0] == Expected.B && Got[1] == Expected.G && Got[2] == Expected.R) ? 1 : 0;
				}
			}
			Faces = FaceSet.Num();
			const int32 Total = TexSize * TexSize;
			AddInfo(FString::Printf(TEXT("Site %s: %d of %d texels drawn, %d exact (%.2f%%), %d face(s) in view"),
				*Site.ToCompactString(), Drawn, Total, Exact, 100.0 * Exact / Total, Faces));
			TestEqual(TEXT("Every texel is drawn from a tile image"), Drawn, Total);
			TestTrue(TEXT("At least 98% of the texels are the exact tile pixel"), Exact >= Total * 98 / 100);
			if (Site.X > 0.5 && Site.Y > 0.5)
			{
				TestEqual(TEXT("The cube corner's minimap spans three faces"), Faces, 3);
			}
		}
	}
	W.Stop();
	return true;
}

// ---------------------------------------------------------------------------
// Spherical Phase 6, WM-BP11: the world map at scale, the character side (the albedo and ExploreAll are
// VoxelWorlds.Map.Planet.ZoomAtScale).
// - The zoom range: on the small planet, M and L (tiles of a 4,800 uu chunk) the most zoomed out view shows the whole
//   globe with a tenth to spare (MinZoom 0.25 showed it only up to R of about 5 x 10^5), the configured range kept where
//   it already did; zoomed in to MaxZoom as before.
// - Zoomed out the globe is drawn from the albedo: on a small planet's globe 200 px across, every texel on the disc is its
//   albedo cell's colour where a tile in the cell is explored and the fog's elsewhere; no tile image is asked for.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCPlanet_MapZoomAtScale, "VoxelCharacter.Planet.MapZoomAtScale", VC_PLANET_MAP_FLAGS)
bool FVCPlanet_MapZoomAtScale::RunTest(const FString& Parameters)
{
	constexpr double TileWorldSize = 4800.0;
	constexpr double HalfTex = 512.0;
	for (const double Radius : { 2.5e5, 5.0e6, 1.0e8 })
	{
		float Low = 0.0f, High = 0.0f;
		UVCWorldMapWidget::GetPlanetZoomRange(Radius, TileWorldSize, 0.25f, 4.0f, Low, High);
		// GetGlobeRadiusPx at a zoom: HalfTex * R / (32 tiles / zoom).
		const double RadiusPx = HalfTex * Radius * Low / (32.0 * TileWorldSize);
		AddInfo(FString::Printf(TEXT("R %.0f: zoom %.5f to %.1f; the globe %.0f px across at the lowest (the view %.0f)"), Radius, Low, High, 2.0 * RadiusPx, 2.0 * HalfTex));
		TestTrue(*FString::Printf(TEXT("R %.0f: the whole globe in view zoomed out"), Radius), RadiusPx <= 0.9 * HalfTex + 1.0);
		TestTrue(*FString::Printf(TEXT("R %.0f: zoomed in as before"), Radius), High == 4.0f);
		TestTrue(*FString::Printf(TEXT("R %.0f: the configured MinZoom kept when it shows the globe"), Radius), Low <= 0.25f && (Radius > 5.0e5 || Low == 0.25f));
	}

	// The sampler on the albedo.
	VoxelTesting::FTestChunkWorld W;
	W.Create(TEXT("VCPlanetMapZoomAtScale"));
	W.ConfigureSmallPlanet();
	W.StartManager();
	UVoxelMapSubsystem* Map = W.World->GetSubsystem<UVoxelMapSubsystem>();
	if (TestNotNull(TEXT("The game world has a map"), Map))
	{
		const FVoxelMapGlobeAlbedo* Albedo = Map->GetGlobeAlbedo();
		const double Deadline = FPlatformTime::Seconds() + 60.0;
		while (!Albedo && FPlatformTime::Seconds() < Deadline)
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			FPlatformProcess::Sleep(0.01f);
			Albedo = Map->GetGlobeAlbedo();
		}
		if (TestNotNull(TEXT("The albedo builds"), Albedo))
		{
			const FVector Site = FVector(0.3, 0.2, 1.0).GetSafeNormal();
			Map->RequestTilesInRadius(W.Config->WorldOrigin + Site * (W.Config->WorldRadius + 200.0), 4000.0f);
			// Tile images waiting or drawing (the map's debug line).
			auto Pending = [Map]()
			{
				const FString Stats = Map->GetDebugStats();
				int32 Waiting = 0, Drawing = 0;
				FParse::Value(*Stats, TEXT("waiting="), Waiting);
				FParse::Value(*Stats, TEXT("drawing="), Drawing);
				return Waiting + Drawing;
			};
			const int32 QueuedBefore = Pending();
			constexpr int32 TexSize = 256;
			const FVCGlobeProjection Globe = FVCGlobeProjection::Make(Site, 100.0);
			TArray<uint8> Texels;
			Texels.SetNumZeroed(TexSize * TexSize * 4);
			FVCPlanetMapSampler Sampler;
			Sampler.bUseGlobeAlbedo = true;
			const double Half = 0.5 * TexSize;
			auto TexelDirection = [&Globe, Half](double X, double Y, FVector& OutDirection)
			{
				return Globe.ScreenToDirection(FVector2D(X - Half, Y - Half), OutDirection);
			};
			const int32 Drawn = Sampler.Fill(*Map, Texels.GetData(), TexSize, 16, TexelDirection);
			int32 OnDisc = 0, Exact = 0, Fog = 0, Wrong = 0;
			for (int32 Y = 0; Y < TexSize; ++Y)
			{
				for (int32 X = 0; X < TexSize; ++X)
				{
					FVector Direction;
					if (!TexelDirection(X + 0.5, Y + 0.5, Direction))
					{
						continue;
					}
					++OnDisc;
					const FIntPoint Cell = Albedo->Grid.CellOfDirection(Direction);
					const uint8* Got = Texels.GetData() + (Y * TexSize + X) * 4;
					const FColor Expected = Map->IsGlobeCellExplored(Cell) ? Albedo->At(Cell) : Sampler.Unexplored;
					const bool bMatch = Got[0] == Expected.B && Got[1] == Expected.G && Got[2] == Expected.R;
					Exact += bMatch ? 1 : 0;
					Fog += Map->IsGlobeCellExplored(Cell) ? 0 : 1;
					Wrong += (!bMatch && Map->IsGlobeCellExplored(Cell)) ? 1 : 0;
				}
			}
			const int32 QueuedAfter = Pending();
			AddInfo(FString::Printf(TEXT("The globe 200 px across: %d texels on the disc, %d drawn from the albedo, %d exact (%d in the fog, %d explored texels off); tile requests %d -> %d"),
				OnDisc, Drawn, Exact, Fog, Wrong, QueuedBefore, QueuedAfter));
			TestTrue(TEXT("Some texels drawn from the albedo"), Drawn > 0);
			TestTrue(TEXT("The rest in the fog"), Fog > 0 && Drawn + Fog >= OnDisc * 98 / 100);
			TestTrue(TEXT("At least 98% of the texels exact (cell boundaries may round either way)"), Exact >= OnDisc * 98 / 100);
			TestTrue(TEXT("No tile image asked for"), QueuedAfter <= QueuedBefore);
		}
	}
	W.Stop();
	return true;
}

#endif // WITH_AUTOMATION_TESTS
