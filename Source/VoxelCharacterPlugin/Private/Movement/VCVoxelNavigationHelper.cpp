// Copyright Daniel Raquel. All Rights Reserved.

#include "Movement/VCVoxelNavigationHelper.h"
#include "Movement/VCMovementComponent.h"
#include "VoxelChunkManager.h"
#include "VoxelWorldConfiguration.h"
#include "VoxelCoordinates.h"
#include "VoxelData.h"
#include "IVoxelWorldMode.h"
#include "VoxelCharacterPlugin.h"

// ---------------------------------------------------------------------------
// Query Terrain Context
// ---------------------------------------------------------------------------

FVoxelTerrainContext FVCVoxelNavigationHelper::QueryTerrainContext(const UWorld* World, const FVector& Location, const FVector& Up)
{
	FVoxelTerrainContext Context;

	UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(World);
	if (!ChunkMgr)
	{
		return Context;
	}

	const UVoxelWorldConfiguration* Config = ChunkMgr->GetConfiguration();
	if (!Config)
	{
		return Context;
	}

	// Get voxel data at feet position (sample slightly below, along local down, to catch surface)
	const FVector SamplePos = Location - Up * 10.f;
	const FVoxelData VoxelAtFeet = ChunkMgr->GetVoxelAtWorldPosition(SamplePos);

	// Material and surface type
	Context.VoxelMaterialID = VoxelAtFeet.MaterialID;
	Context.SurfaceType = UVCMovementComponent::MaterialIDToSurfaceType(VoxelAtFeet.MaterialID);
	Context.FrictionMultiplier = UVCMovementComponent::GetSurfaceFriction(Context.SurfaceType);

	// Surface hardness based on material
	switch (Context.SurfaceType)
	{
	case EVoxelSurfaceType::Stone:
	case EVoxelSurfaceType::Metal:
		Context.SurfaceHardness = 1.0f;
		break;
	case EVoxelSurfaceType::Dirt:
	case EVoxelSurfaceType::Grass:
		Context.SurfaceHardness = 0.5f;
		break;
	case EVoxelSurfaceType::Sand:
	case EVoxelSurfaceType::Snow:
		Context.SurfaceHardness = 0.3f;
		break;
	case EVoxelSurfaceType::Mud:
		Context.SurfaceHardness = 0.2f;
		break;
	default:
		Context.SurfaceHardness = 1.0f;
		break;
	}

	// Water state — check the voxel water flag at character position; the depth is the sea's (SP-E1: under the WaterLevel
	// plane generation floods, G19, or along the radius under WaterRadius on a planet)
	if (Config->bEnableWaterLevel)
	{
		const FVoxelData VoxelAtLocation = ChunkMgr->GetVoxelAtWorldPosition(Location);
		if (VoxelAtLocation.HasWaterFlag())
		{
			Context.bIsUnderwater = true;
			Context.WaterDepth = FMath::Max(0.0, ChunkMgr->GetSeaSurface().GetDepth(Location));
		}
	}

	// Chunk coordinate
	const FVector RelativePos = Location - Config->WorldOrigin;
	Context.CurrentChunkCoord = FVoxelCoordinates::WorldToChunk(
		RelativePos, Config->ChunkSize, Config->VoxelSize);

	return Context;
}

// ---------------------------------------------------------------------------
// Water Check
// ---------------------------------------------------------------------------

bool FVCVoxelNavigationHelper::IsPositionUnderwater(const UWorld* World, const FVector& Location, float& OutWaterDepth)
{
	OutWaterDepth = 0.f;

	UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(World);
	if (!ChunkMgr)
	{
		return false;
	}

	const UVoxelWorldConfiguration* Config = ChunkMgr->GetConfiguration();
	if (!Config || !Config->bEnableWaterLevel)
	{
		return false;
	}

	const FVoxelData VoxelAtLocation = ChunkMgr->GetVoxelAtWorldPosition(Location);
	if (VoxelAtLocation.HasWaterFlag())
	{
		OutWaterDepth = FMath::Max(0.0, ChunkMgr->GetSeaSurface().GetDepth(Location));
		return true;
	}

	return false;
}

// ---------------------------------------------------------------------------
// Find Spawnable Position
// ---------------------------------------------------------------------------

bool FVCVoxelNavigationHelper::FindSpawnablePosition(
	const UWorld* World,
	const FVector& NearPosition,
	FVector& OutPosition,
	float MaxSearchRadius,
	bool bSearchWholeWorld)
{
	UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(World);
	if (!ChunkMgr)
	{
		return false;
	}

	const UVoxelWorldConfiguration* Config = ChunkMgr->GetConfiguration();
	if (!Config)
	{
		return false;
	}

	const float ChunkWorldSize = Config->ChunkSize * Config->VoxelSize;
	// Terrain heights are world Z, so the water surface is too: the sea at WaterLevel, where generation floods, wherever
	// WorldOrigin is (G19; the nav, swim and NPC code read the same sea); the sphere of WaterRadius on a planet, SP-E1.
	const FVoxelSeaSurface Sea = ChunkMgr->GetSeaSurface();
	const float WaterLevel = static_cast<float>(Sea.PlaneZ);
	const bool bHasWater = Sea.bEnabled;

	// Only points inside the world can host a spawn (world modes review WM-I3, KI8): past an island's R + falloff
	// generation writes a flat floor (dry with water off) and then nothing, so a pawn placed there waits for terrain that
	// never comes. A start outside searches from the nearest point a chunk inside the edge instead.
	const FVoxelWorldBounds Bounds = ChunkMgr->GetWorldBounds();

	// A planet: by direction from the centre (spherical SP-F4), the generated surface along it (GetGeneratedSurfacePoint:
	// noise, continentalness and the POI pads' conditioning, what generation makes; SP-B10), dry above the sea sphere.
	if (Bounds.Shape == EVoxelWorldBoundsShape::Shell)
	{
		const FVector Centre = Bounds.Center;
		FVector StartDirection = (NearPosition - Centre).GetSafeNormal();
		if (StartDirection.IsNearlyZero())
		{
			StartDirection = (ChunkMgr->GetPlanetSpawnPosition() - Centre).GetSafeNormal();
		}
		const double Radius = Config->WorldRadius;
		auto SurfaceAt = [ChunkMgr, &Centre, Radius](const FVector& Direction)
		{
			return ChunkMgr->GetGeneratedSurfacePoint(Centre + Direction * Radius);
		};
		int32 Ring = 0;
		if (FindPlanetSpawnPoint(Centre, Radius, StartDirection, ChunkWorldSize, MaxSearchRadius, SurfaceAt, Sea, OutPosition, Ring))
		{
			UE_LOG(LogVoxelCharacter, Log,
				TEXT("FindSpawnablePosition: planet surface at (%.0f, %.0f, %.0f), altitude %.0f, ring %d (%.0f of arc from the start)."),
				OutPosition.X, OutPosition.Y, OutPosition.Z, ChunkMgr->GetAltitudeAt(OutPosition), Ring, Ring * ChunkWorldSize);
			return true;
		}
		// The rest of the planet (WM-BP11, G36): a big planet's open sea can lie hundreds of km across.
		double Arc = 0.0;
		if (bSearchWholeWorld && FindPlanetSpawnPointAtScale(Centre, Radius, StartDirection, FMath::Max(static_cast<double>(MaxSearchRadius), static_cast<double>(ChunkWorldSize)),
			SurfaceAt, Sea, OutPosition, Arc))
		{
			UE_LOG(LogVoxelCharacter, Log,
				TEXT("FindSpawnablePosition: no land within %.0f of arc; the nearest land found on the planet at (%.0f, %.0f, %.0f), altitude %.0f, %.0f of arc from the start."),
				MaxSearchRadius, OutPosition.X, OutPosition.Y, OutPosition.Z, ChunkMgr->GetAltitudeAt(OutPosition), Arc);
			return true;
		}
		UE_LOG(LogVoxelCharacter, Warning,
			TEXT("FindSpawnablePosition: no land above the sea within %.0f of arc of direction (%.3f, %.3f, %.3f) on the planet."),
			MaxSearchRadius, StartDirection.X, StartDirection.Y, StartDirection.Z);
		return false;
	}

	FVector Start = NearPosition;
	if (!Bounds.ContainsXY(Start.X, Start.Y))
	{
		const FVector2D Inside = Bounds.ClampXY(Start.X, Start.Y, ChunkWorldSize);
		Start = FVector(Inside.X, Inside.Y, NearPosition.Z);
		UE_LOG(LogVoxelCharacter, Log,
			TEXT("FindSpawnablePosition: (%.0f, %.0f) is outside the world; searching from (%.0f, %.0f), inside its edge."),
			NearPosition.X, NearPosition.Y, Start.X, Start.Y);
	}

	// Helper: query terrain height and check if above water. GetGeneratedSurfaceHeight is the
	// canonical analytic surface (continentalness AND terrain-conditioning zones): the raw
	// IVoxelWorldMode::GetTerrainHeightAt misses POI-pad flattening, so spawns/settles on
	// conditioner-FILLED ground landed below the real surface (inside the fill) and fell/traced
	// into whatever lay beneath.
	auto IsAboveWater = [&](float X, float Y, float& OutTerrainHeight) -> bool
	{
		if (!Bounds.ContainsXY(X, Y))
		{
			OutTerrainHeight = 0.f;
			return false;
		}
		OutTerrainHeight = ChunkMgr->GetGeneratedSurfaceHeight(X, Y);
		return !bHasWater || Sea.GetDepth(FVector(X, Y, OutTerrainHeight)) < 0.0;
	};

	// Try the requested position first
	float TerrainHeight = 0.f;
	if (IsAboveWater(Start.X, Start.Y, TerrainHeight))
	{
		OutPosition = FVector(Start.X, Start.Y, TerrainHeight);
		UE_LOG(LogVoxelCharacter, Log,
			TEXT("FindSpawnablePosition: Position (%.0f, %.0f) is above water at Z=%.0f"),
			Start.X, Start.Y, TerrainHeight);
		return true;
	}

	UE_LOG(LogVoxelCharacter, Log,
		TEXT("FindSpawnablePosition: Position (%.0f, %.0f) is underwater (terrain Z=%.0f, water=%.0f). Searching outward..."),
		Start.X, Start.Y, TerrainHeight, WaterLevel);

	// Spiral search outward at ChunkWorldSize intervals in 8 directions
	static const FVector2D Directions[] =
	{
		FVector2D( 1,  0), FVector2D( 1,  1), FVector2D( 0,  1), FVector2D(-1,  1),
		FVector2D(-1,  0), FVector2D(-1, -1), FVector2D( 0, -1), FVector2D( 1, -1)
	};

	const float Step = ChunkWorldSize;
	const int32 MaxRings = FMath::CeilToInt(MaxSearchRadius / Step);

	for (int32 Ring = 1; Ring <= MaxRings; ++Ring)
	{
		const float Radius = Ring * Step;

		for (const FVector2D& Dir : Directions)
		{
			const float SampleX = Start.X + Dir.X * Radius;
			const float SampleY = Start.Y + Dir.Y * Radius;

			if (IsAboveWater(SampleX, SampleY, TerrainHeight))
			{
				OutPosition = FVector(SampleX, SampleY, TerrainHeight);
				UE_LOG(LogVoxelCharacter, Log,
					TEXT("FindSpawnablePosition: Found land at (%.0f, %.0f) Z=%.0f, ring %d (%.0f units away)"),
					SampleX, SampleY, TerrainHeight, Ring, Radius);
				return true;
			}
		}
	}

	// The rest of the world (world modes review WM-MP10; the plane's G36): widening rings to the island's bounds, or a few
	// hundred chunks out on a plane, so a seed whose spawn is open sea, or an island whose radius shrank, still lands.
	if (bSearchWholeWorld)
	{
		double MaxRadius = 512.0 * ChunkWorldSize;
		if (Bounds.IsBounded())
		{
			const FBox2D Box = Bounds.GetBox2D();
			const FVector2D Start2D(Start.X, Start.Y);
			MaxRadius = 0.0;
			for (const FVector2D& Corner : { Box.Min, Box.Max, FVector2D(Box.Min.X, Box.Max.Y), FVector2D(Box.Max.X, Box.Min.Y) })
			{
				MaxRadius = FMath::Max(MaxRadius, FVector2D::Distance(Start2D, Corner));
			}
		}
		auto HeightAt = [ChunkMgr](double X, double Y) { return ChunkMgr->GetGeneratedSurfaceHeight(X, Y); };
		double Distance = 0.0;
		if (FindPlaneSpawnPointAtScale(FVector2D(Start.X, Start.Y), FMath::Max(static_cast<double>(MaxSearchRadius), static_cast<double>(Step)), Step,
			MaxRadius, Bounds, HeightAt, Sea, OutPosition, Distance))
		{
			UE_LOG(LogVoxelCharacter, Log,
				TEXT("FindSpawnablePosition: no land within %.0f of (%.0f, %.0f); the nearest land found at (%.0f, %.0f) Z=%.0f, %.0f away."),
				MaxSearchRadius, Start.X, Start.Y, OutPosition.X, OutPosition.Y, OutPosition.Z, Distance);
			return true;
		}
	}

	UE_LOG(LogVoxelCharacter, Warning,
		TEXT("FindSpawnablePosition: No land found within %.0f units of (%.0f, %.0f)%s"),
		MaxSearchRadius, Start.X, Start.Y, bSearchWholeWorld ? TEXT(", nor in the rest of the world searched") : TEXT(""));
	return false;
}

bool FVCVoxelNavigationHelper::FindPlaneSpawnPointAtScale(const FVector2D& Start, double FirstRadius, double StepSize, double MaxRadius,
	const FVoxelWorldBounds& Bounds, TFunctionRef<float(double X, double Y)> SurfaceHeightAt, const FVoxelSeaSurface& Sea,
	FVector& OutPosition, double& OutDistance, int32 MaxSamples)
{
	OutDistance = 0.0;
	if (FirstRadius <= 0.0 || StepSize <= 0.0 || MaxRadius < FirstRadius)
	{
		return false;
	}
	// Rings a quarter of their radius apart, at least a step (so the search is bounded in samples, as the planet's is in
	// angle), each ring's bearings as far apart as the rings.
	constexpr double Growth = 0.25;
	int32 Samples = 0;
	double Radius = FirstRadius;
	while (Radius <= MaxRadius && Samples < MaxSamples)
	{
		const double Spacing = FMath::Max(Radius * Growth, StepSize);
		const int32 Bearings = FMath::Clamp(FMath::CeilToInt32(UE_DOUBLE_TWO_PI * Radius / Spacing), 8, 128);
		bool bAnyInside = false;
		for (int32 Bearing = 0; Bearing < Bearings && Samples < MaxSamples; ++Bearing)
		{
			const double B = Bearing * (UE_DOUBLE_TWO_PI / Bearings);
			const double X = Start.X + Radius * FMath::Cos(B);
			const double Y = Start.Y + Radius * FMath::Sin(B);
			if (!Bounds.ContainsXY(X, Y))
			{
				continue;
			}
			bAnyInside = true;
			++Samples;
			const float Height = SurfaceHeightAt(X, Y);
			if (!Sea.bEnabled || Sea.GetDepth(FVector(X, Y, Height)) < 0.0)
			{
				OutPosition = FVector(X, Y, Height);
				OutDistance = Radius;
				return true;
			}
		}
		if (!bAnyInside)
		{
			break; // the ring lies wholly outside the world: nothing farther is inside it either
		}
		Radius += Spacing;
	}
	return false;
}

bool FVCVoxelNavigationHelper::FindPlanetSpawnPoint(const FVector& Centre, double Radius, const FVector& StartDirection, double StepArc,
	double MaxSearchArc, TFunctionRef<FVector(const FVector& Direction)> SurfaceAt, const FVoxelSeaSurface& Sea,
	FVector& OutPosition, int32& OutRing)
{
	OutRing = 0;
	const FVector D = StartDirection.GetSafeNormal();
	if (D.IsNearlyZero() || Radius <= 0.0)
	{
		return false;
	}
	auto IsDry = [&Sea](const FVector& Surface)
	{
		return !Sea.bEnabled || Sea.GetDepth(Surface) < 0.0;
	};
	const FVector Here = SurfaceAt(D);
	if (IsDry(Here))
	{
		OutPosition = Here;
		return true;
	}

	// A tangent frame at the start: bearings are measured from T1 toward T2.
	const FVector T1 = FVector::CrossProduct(FMath::Abs(D.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector, D).GetSafeNormal();
	const FVector T2 = FVector::CrossProduct(D, T1);
	const double Step = FMath::Max(StepArc, 1.0);
	const int32 MaxRings = FMath::Max(1, FMath::CeilToInt32(MaxSearchArc / Step));
	for (int32 Ring = 1; Ring <= MaxRings; ++Ring)
	{
		const double Angle = FMath::Min(Ring * Step / Radius, UE_DOUBLE_PI);
		for (int32 Bearing = 0; Bearing < 8; ++Bearing)
		{
			const double B = Bearing * (UE_DOUBLE_PI / 4.0);
			const FVector Direction = (D * FMath::Cos(Angle) + (T1 * FMath::Cos(B) + T2 * FMath::Sin(B)) * FMath::Sin(Angle)).GetSafeNormal();
			const FVector Surface = SurfaceAt(Direction);
			if (IsDry(Surface))
			{
				OutPosition = Surface;
				OutRing = Ring;
				return true;
			}
		}
	}
	return false;
}

bool FVCVoxelNavigationHelper::FindPlanetSpawnPointAtScale(const FVector& Centre, double Radius, const FVector& StartDirection, double FirstArc,
	TFunctionRef<FVector(const FVector& Direction)> SurfaceAt, const FVoxelSeaSurface& Sea, FVector& OutPosition, double& OutArc,
	int32 MaxSamples)
{
	OutArc = 0.0;
	const FVector D = StartDirection.GetSafeNormal();
	if (D.IsNearlyZero() || Radius <= 0.0 || FirstArc <= 0.0)
	{
		return false;
	}
	auto IsDry = [&Sea](const FVector& Surface)
	{
		return !Sea.bEnabled || Sea.GetDepth(Surface) < 0.0;
	};
	// A tangent frame at the start, as FindPlanetSpawnPoint's.
	const FVector T1 = FVector::CrossProduct(FMath::Abs(D.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector, D).GetSafeNormal();
	const FVector T2 = FVector::CrossProduct(D, T1);
	// Rings a quarter of their angle apart, at most 3.5 degrees (so land wider than about 5 degrees is never missed: 85 km
	// on a 1,000 km planet), each ring's bearings as far apart as the rings.
	constexpr double Growth = 0.25;
	const double MaxSpacing = FMath::DegreesToRadians(3.5);
	int32 Samples = 0;
	double Angle = FirstArc / Radius;
	while (Samples < MaxSamples)
	{
		const double RingAngle = FMath::Min(Angle, UE_DOUBLE_PI);
		const double Spacing = FMath::Clamp(RingAngle * Growth, 1.0e-9, MaxSpacing);
		const int32 Bearings = FMath::Clamp(FMath::CeilToInt32(UE_DOUBLE_TWO_PI * FMath::Sin(RingAngle) / Spacing), 8, 128);
		for (int32 Bearing = 0; Bearing < Bearings && Samples < MaxSamples; ++Bearing, ++Samples)
		{
			const double B = Bearing * (UE_DOUBLE_TWO_PI / Bearings);
			const FVector Direction = (D * FMath::Cos(RingAngle) + (T1 * FMath::Cos(B) + T2 * FMath::Sin(B)) * FMath::Sin(RingAngle)).GetSafeNormal();
			const FVector Surface = SurfaceAt(Direction);
			if (IsDry(Surface))
			{
				OutPosition = Surface;
				OutArc = RingAngle * Radius;
				return true;
			}
		}
		if (RingAngle >= UE_DOUBLE_PI)
		{
			break;
		}
		Angle += Spacing;
	}
	return false;
}
