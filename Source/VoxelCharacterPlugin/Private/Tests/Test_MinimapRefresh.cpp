// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Map/VCPlanetMapProjection.h"
#include "Map/VCWorldMapWidget.h"
#include "VoxelMapSubsystem.h"
#include "Testing/VoxelTestChunkWorld.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformProcess.h"

// ---------------------------------------------------------------------------
// The minimap's refresh (VoxelWorlds WORLD_MODES_REVIEW_PLAN.md §14.1 G64): the texture is filled again only when what it
// shows changed, and shifted by the player's movement in between; a plane's texture is filled texel by texel.
// - PlaneMapSampler: on a small plane's tiles the texel fill draws the image the per-tile-pixel blit did where tiles are
//   finer than texels (but texels at a pixel boundary, rounded either way), and draws every texel where the blit left
//   gaps (tiles coarser than texels).
// - MinimapShift: on a planet, a texture filled two texels away and shifted, with the heading taken on the plane it was
//   drawn in, shows the terrain where a fresh fill would, to a fraction of a texel, at every latitude short of the pole
//   (the heading taken on the player's own plane would be off by over a texel at the rim near a pole).
// ---------------------------------------------------------------------------

#define VC_MINIMAP_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * World modes review WM-MP9 (§23): the world map's redraw for tile arrivals waits (at most every 2 s) when a redraw per
 * arrival would be waste: the globe drawn from its albedo (the planet's G51 rule) and, in every mode, a view of more than
 * TileRedrawThrottleTiles tiles (a plane's most zoomed-out view holds about 66,000, and voxel.Map.ExploreAll streams an
 * image for each); a view zoomed in to a few tiles redraws at once, as the albedo's own arrival does.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCMap_TileRedrawThrottle, "VoxelCharacter.Map.TileRedrawThrottle", VC_MINIMAP_FLAGS)
bool FVCMap_TileRedrawThrottle::RunTest(const FString& Parameters)
{
	const int32 Threshold = UVCWorldMapWidget::TileRedrawThrottleTiles;
	TestFalse(TEXT("A narrow view redraws at once"), UVCWorldMapWidget::DefersTileRedraw(false, 1));
	TestFalse(TEXT("A view at the threshold redraws at once"), UVCWorldMapWidget::DefersTileRedraw(false, Threshold));
	TestTrue(TEXT("A view past the threshold waits"), UVCWorldMapWidget::DefersTileRedraw(false, Threshold + 1));
	TestTrue(TEXT("The plane's most zoomed-out view (257 x 257 tiles) waits"), UVCWorldMapWidget::DefersTileRedraw(false, 257 * 257));
	TestTrue(TEXT("The globe on its albedo waits for a tile"), UVCWorldMapWidget::DefersTileRedraw(true, 0));
	TestFalse(TEXT("The globe on its albedo redraws for the albedo itself"), UVCWorldMapWidget::DefersTileRedraw(false, 0));
	TestFalse(TEXT("A custom threshold"), UVCWorldMapWidget::DefersTileRedraw(false, 1000, 1000));
	TestTrue(TEXT("... exceeded"), UVCWorldMapWidget::DefersTileRedraw(false, 1001, 1000));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCMap_PlaneMapSampler, "VoxelCharacter.Map.PlaneMapSampler", VC_MINIMAP_FLAGS)
bool FVCMap_PlaneMapSampler::RunTest(const FString& Parameters)
{
	VoxelTesting::FTestChunkWorld W;
	W.Create(TEXT("VCPlaneMapSampler"));
	W.ConfigureSmallPlane();
	W.StartManager();
	UVoxelMapSubsystem* Map = W.World->GetSubsystem<UVoxelMapSubsystem>();
	if (TestNotNull(TEXT("The game world has a map"), Map))
	{
		const double TileSize = Map->GetTileWorldSize();
		// Off the tile grid on purpose.
		const FVector Center = W.Config->WorldOrigin + FVector(TileSize * 0.37, -TileSize * 0.61, 0.0);
		constexpr int32 TexSize = 128;

		// The tile images' own resolution (the image under the centre, drawn first).
		Map->RequestTilesInRadius(Center, static_cast<float>(TileSize * 2.0));
		const double FirstDeadline = FPlatformTime::Seconds() + 60.0;
		while (!Map->GetTile(Map->WorldToTileCoord(Center)) && FPlatformTime::Seconds() < FirstDeadline)
		{
			FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
			FPlatformProcess::Sleep(0.005f);
		}
		const FVoxelMapTile* CenterTile = Map->GetTile(Map->WorldToTileCoord(Center));
		if (!TestNotNull(TEXT("The tile under the centre drew"), CenterTile))
		{
			W.Stop();
			return true;
		}
		const double SourcePixel = TileSize / FMath::Max(1, CenterTile->Resolution);
		AddInfo(FString::Printf(TEXT("Tile %.0f uu, %d pixels a side (the map's tile resolution %d)"), TileSize, CenterTile->Resolution,
			Map->GetTileResolution()));

		// Texels coarser than tile pixels (the minimap's case), then finer.
		for (const double SourcePixelsPerTexel : { 1.6, 0.4 })
		{
			const double WorldPerPixel = SourcePixel * SourcePixelsPerTexel;
			const double Extent = 0.5 * TexSize * WorldPerPixel;
			Map->RequestTilesInRadius(Center, static_cast<float>(Extent * 1.5 + TileSize));
			const double Deadline = FPlatformTime::Seconds() + 60.0;
			while (!Map->GetDebugStats().Contains(TEXT("waiting=0 drawing=0")) && FPlatformTime::Seconds() < Deadline)
			{
				FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
				FPlatformProcess::Sleep(0.005f);
			}
			TestTrue(TEXT("The tiles drew"), Map->GetDebugStats().Contains(TEXT("waiting=0 drawing=0")));

			TArray<uint8> Blit, Texels;
			Blit.SetNumZeroed(TexSize * TexSize * 4);
			Texels.SetNumZeroed(TexSize * TexSize * 4);
			FVCPlaneMapSampler Old;
			Old.bTexelFill = false;
			Old.Fill(*Map, Blit.GetData(), TexSize, Center, WorldPerPixel);
			FVCPlaneMapSampler New;
			TSet<FIntPoint> Visited;
			New.VisitedTiles = &Visited;
			const int32 Drawn = New.Fill(*Map, Texels.GetData(), TexSize, Center, WorldPerPixel);

			const FColor Background = New.Background;
			auto IsBackground = [&Background](const uint8* T) { return T[0] == Background.B && T[1] == Background.G && T[2] == Background.R; };
			int32 Same = 0, BlitGaps = 0, NewGaps = 0;
			for (int32 I = 0; I < TexSize * TexSize; ++I)
			{
				const uint8* A = Blit.GetData() + I * 4;
				const uint8* B = Texels.GetData() + I * 4;
				Same += (A[0] == B[0] && A[1] == B[1] && A[2] == B[2] && A[3] == B[3]) ? 1 : 0;
				BlitGaps += IsBackground(A) ? 1 : 0;
				NewGaps += IsBackground(B) ? 1 : 0;
			}
			const int32 Total = TexSize * TexSize;
			AddInfo(FString::Printf(TEXT("%.2f tile pixels a texel: %d of %d texels drawn, %d the blit's (%.2f%%); background: blit %d, texel fill %d; %d tiles read"),
				SourcePixelsPerTexel, Drawn, Total, Same, 100.0 * Same / Total, BlitGaps, NewGaps, Visited.Num()));
			TestEqual(TEXT("Every texel is drawn from a tile image"), Drawn, Total);
			TestEqual(TEXT("No texel is left as background"), NewGaps, 0);
			TestTrue(TEXT("The tiles read are reported"), Visited.Num() > 0);
			if (SourcePixelsPerTexel > 1.0)
			{
				// Finer tiles: the same image but texels at a tile pixel's boundary.
				TestTrue(TEXT("Finer tiles: at least 98% of the texels are the blit's"), Same >= Total * 98 / 100);
			}
			else
			{
				// Tile pixels coarser than texels: the blit wrote one texel per tile pixel and left the rest as background (a
				// third of them here); the texel fill draws them all.
				TestTrue(TEXT("Coarser tile pixels: the blit left gaps"), BlitGaps > Total / 10);
			}
		}
	}
	W.Stop();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCPlanet_MinimapShift, "VoxelCharacter.Planet.MinimapShift", VC_MINIMAP_FLAGS)
bool FVCPlanet_MinimapShift::RunTest(const FString& Parameters)
{
	// The minimap's defaults: 200 px across showing 16,000 uu to the edge; refilled after two texels.
	constexpr double MinimapSize = 200.0;
	constexpr double MinimapWorldRadius = 16000.0;
	const double WorldPerPixel = 2.0 * MinimapWorldRadius / MinimapSize;
	const double Move = 2.0 * WorldPerPixel;
	const FVector Center(1000.0, -2000.0, 500.0);

	for (const double Radius : { 250000.0, 5000000.0 })
	{
		for (const double LatDeg : { 0.0, 45.0, 80.0, 85.0, 89.0, 89.9 })
		{
			double WorstShown = 0.0, WorstOwnFrame = 0.0;
			for (int32 Bearing = 0; Bearing < 8; ++Bearing)
			{
				const double Lat = FMath::DegreesToRadians(LatDeg);
				const FVector Up0(FMath::Cos(Lat), 0.0, FMath::Sin(Lat));
				const FVector P0 = Center + Up0 * (Radius + 300.0);
				const FVCPlanetMapFrame Fill = FVCPlanetMapFrame::At(Center, Radius, P0);
				// Move two texels along the bearing on the fill's plane.
				const double B = FMath::DegreesToRadians(45.0 * Bearing);
				const FVector P1 = Center + Fill.DirectionAtOffset(Move * FMath::Cos(B), Move * FMath::Sin(B)) * (Radius + 300.0);
				const FVCPlanetMapFrame Current = FVCPlanetMapFrame::At(Center, Radius, P1);
				// A camera looking along some heading on the player's plane.
				const FVector Forward = (Current.North * FMath::Cos(0.7) + Current.East * FMath::Sin(0.7)).GetSafeNormal();
				const double FreshAngle = -Current.HeadingDegrees(Forward) - 90.0;
				const double ShownAngle = -Fill.HeadingDegrees(Forward) - 90.0;
				const FVector2D PlayerOnFill = Fill.OffsetOf(P1);
				for (int32 GY = -4; GY <= 4; ++GY)
				{
					for (int32 GX = -4; GX <= 4; ++GX)
					{
						// Terrain points across the visible square around the player.
						const FVector Q = Center + Current.DirectionAtOffset(GX * MinimapWorldRadius / 4.0, GY * MinimapWorldRadius / 4.0) * Radius;
						const FVector2D Fresh = (Current.OffsetOf(Q) / WorldPerPixel).GetRotated(FreshAngle);
						const FVector2D Shown = ((Fill.OffsetOf(Q) - PlayerOnFill) / WorldPerPixel).GetRotated(ShownAngle);
						const FVector2D OwnFrame = ((Fill.OffsetOf(Q) - PlayerOnFill) / WorldPerPixel).GetRotated(FreshAngle);
						WorstShown = FMath::Max(WorstShown, (Shown - Fresh).Size());
						WorstOwnFrame = FMath::Max(WorstOwnFrame, (OwnFrame - Fresh).Size());
					}
				}
			}
			AddInfo(FString::Printf(TEXT("R %.0f, %.1f deg: worst offset %.3f texels (heading on the player's own plane: %.3f)"), Radius, LatDeg,
				WorstShown, WorstOwnFrame));
			TestTrue(*FString::Printf(TEXT("R %.0f, %.1f deg: the shifted texture shows the terrain within half a texel of a fresh fill"), Radius, LatDeg),
				WorstShown < 0.5);
		}
	}
	return true;
}

#endif // WITH_AUTOMATION_TESTS
