// Copyright Daniel Raquel. All Rights Reserved.

#include "Movement/VCVoxelNavigationHelper.h"
#include "Movement/VCMovementComponent.h"
#include "VoxelChunkManager.h"
#include "VoxelWorldConfiguration.h"
#include "VoxelCoordinates.h"
#include "VoxelData.h"
#include "IVoxelWorldMode.h"
#include "VoxelCharacterPlugin.h"
#include "EngineUtils.h"

TWeakObjectPtr<UVoxelChunkManager> FVCVoxelNavigationHelper::CachedChunkManager;

// ---------------------------------------------------------------------------
// Find Chunk Manager
// ---------------------------------------------------------------------------

UVoxelChunkManager* FVCVoxelNavigationHelper::FindChunkManager(const UWorld* World)
{
	if (!World)
	{
		return nullptr;
	}

	// Validate cached manager belongs to the same world (critical for PIE)
	if (CachedChunkManager.IsValid() && CachedChunkManager->GetWorld() == World)
	{
		return CachedChunkManager.Get();
	}

	// Cache miss or wrong world — clear and re-search
	CachedChunkManager.Reset();

	// Search all actors for one with a VoxelChunkManager component
	for (TActorIterator<AActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		if (UVoxelChunkManager* ChunkMgr = It->FindComponentByClass<UVoxelChunkManager>())
		{
			if (ChunkMgr->IsInitialized())
			{
				CachedChunkManager = ChunkMgr;
				return ChunkMgr;
			}
		}
	}

	return nullptr;
}

// ---------------------------------------------------------------------------
// Query Terrain Context
// ---------------------------------------------------------------------------

FVoxelTerrainContext FVCVoxelNavigationHelper::QueryTerrainContext(const UWorld* World, const FVector& Location, const FVector& Up)
{
	FVoxelTerrainContext Context;

	UVoxelChunkManager* ChunkMgr = FindChunkManager(World);
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

	// Water state — check the voxel water flag at character position; the depth is the sea's (SP-E1: the plane at
	// WaterLevel + WorldOrigin.Z, or along the radius under WaterRadius on a planet)
	if (Config->bEnableWaterLevel)
	{
		const FVoxelData VoxelAtLocation = ChunkMgr->GetVoxelAtWorldPosition(Location);
		if (VoxelAtLocation.HasWaterFlag())
		{
			Context.bIsUnderwater = true;
			Context.WaterDepth = FMath::Max(0.0, ChunkMgr->GetSeaSurface(/*bOriginRelativePlane=*/true).GetDepth(Location));
		}
	}

	// Chunk coordinate
	const FVector RelativePos = Location - Config->WorldOrigin;
	Context.CurrentChunkCoord = FVoxelCoordinates::WorldToChunk(
		RelativePos, Config->ChunkSize, Config->VoxelSize);

	return Context;
}

// ---------------------------------------------------------------------------
// Material Lookup
// ---------------------------------------------------------------------------

uint8 FVCVoxelNavigationHelper::GetVoxelMaterialAtLocation(const UWorld* World, const FVector& Location)
{
	UVoxelChunkManager* ChunkMgr = FindChunkManager(World);
	if (!ChunkMgr)
	{
		return 0;
	}

	return ChunkMgr->GetVoxelAtWorldPosition(Location).MaterialID;
}

// ---------------------------------------------------------------------------
// Water Check
// ---------------------------------------------------------------------------

bool FVCVoxelNavigationHelper::IsPositionUnderwater(const UWorld* World, const FVector& Location, float& OutWaterDepth)
{
	OutWaterDepth = 0.f;

	UVoxelChunkManager* ChunkMgr = FindChunkManager(World);
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
		OutWaterDepth = FMath::Max(0.0, ChunkMgr->GetSeaSurface(/*bOriginRelativePlane=*/true).GetDepth(Location));
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
	float MaxSearchRadius)
{
	UVoxelChunkManager* ChunkMgr = FindChunkManager(World);
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
	// Terrain heights are world Z, so the water surface is too (the nav, swim and NPC code use the same plane: the sea at
	// WaterLevel + WorldOrigin.Z; the sphere of WaterRadius on a planet, SP-E1).
	const FVoxelSeaSurface Sea = ChunkMgr->GetSeaSurface(/*bOriginRelativePlane=*/true);
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

	UE_LOG(LogVoxelCharacter, Warning,
		TEXT("FindSpawnablePosition: No land found within %.0f units of (%.0f, %.0f)"),
		MaxSearchRadius, Start.X, Start.Y);
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

// ---------------------------------------------------------------------------
// Cache Management
// ---------------------------------------------------------------------------

void FVCVoxelNavigationHelper::ClearCache()
{
	CachedChunkManager.Reset();
}
