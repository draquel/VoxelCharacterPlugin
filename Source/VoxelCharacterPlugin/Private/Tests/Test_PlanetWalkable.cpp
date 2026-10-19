// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Core/VCCharacterBase.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/VCNPCCharacterBase.h"
#include "AI/VCNPCAIController.h"
#include "Movement/VCGravityFrame.h"
#include "Movement/VCMovementComponent.h"
#include "Movement/VCVoxelNavigationHelper.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "VoxelChunkManager.h"
#include "VoxelSeaSurface.h"
#include "VoxelWorldConfiguration.h"
#include "DistanceBandLODStrategy.h"
#include "IVoxelMeshRenderer.h"
#include "ChunkRenderData.h"

// ---------------------------------------------------------------------------
// A walkable planet, the character side (VoxelWorlds WORLD_MODES_REVIEW_PLAN.md §17; SPHERICAL_PLANET_REQUIREMENTS.md
// SP-F1 to SP-F5):
// - The gravity frame: world <-> gravity-relative rotations, the yaw-only heading in the local horizontal, and every
//   default-gravity case returning the old values bit for bit (heightmap worlds keep their arithmetic).
// - Gravity toward the centre: a character falling at both poles, 45 degrees, the equator and a cube corner falls along
//   the radius and stands upright; on a plane gravity stays the engine's default.
// - The spawn search by direction: on the analytic surface, above the sea, the nearest land from a sea start.
// - The out-of-world rule: radial on a planet.
// ---------------------------------------------------------------------------

namespace VCPlanetWalkableTestHelpers
{
	/** A renderer that draws nothing (the chunk manager needs one to initialise). */
	class FNullMeshRenderer : public IVoxelMeshRenderer
	{
	public:
		virtual void Initialize(UWorld* World, const UVoxelWorldConfiguration* WorldConfig) override { bInitialized = true; }
		virtual void Shutdown() override { bInitialized = false; }
		virtual bool IsInitialized() const override { return bInitialized; }
		virtual void UpdateChunkMesh(const FChunkRenderData& RenderData) override {}
		virtual void UpdateChunkMeshFromCPU(const FIntVector& ChunkCoord, int32 LODLevel, const FChunkMeshData& MeshData) override {}
		virtual void RemoveChunk(const FIntVector& ChunkCoord) override {}
		virtual void ClearAllChunks() override {}
		virtual void SetChunkVisible(const FIntVector& ChunkCoord, bool bVisible) override {}
		virtual void SetAllChunksVisible(bool bVisible) override {}
		virtual void SetMaterial(UMaterialInterface* Material) override {}
		virtual UMaterialInterface* GetMaterial() const override { return nullptr; }
		virtual void UpdateMaterialParameters() override {}
		virtual void UpdateSeamMeshFromCPU(const FIntVector& OwnerChunkCoord, uint8 SeamSlot, int32 LODLevel, FChunkMeshData&& MeshData, bool bCrossfade) override {}
		virtual void RemoveSeamMesh(const FIntVector& OwnerChunkCoord, uint8 SeamSlot) override {}
		virtual void ClearAllSeamMeshes() override {}
		virtual void GetSeamMeshOwners(TArray<FIntVector>& OutOwners) const override {}
		virtual void UpdateLODTransition(const FIntVector& ChunkCoord, float MorphFactor) override {}
		virtual bool IsChunkLoaded(const FIntVector& ChunkCoord) const override { return false; }
		virtual int32 GetLoadedChunkCount() const override { return 0; }
		virtual void GetLoadedChunks(TArray<FIntVector>& OutChunks) const override {}
		virtual int64 GetGPUMemoryUsage() const override { return 0; }
		virtual int64 GetTotalVertexCount() const override { return 0; }
		virtual int64 GetTotalTriangleCount() const override { return 0; }
		virtual bool GetChunkBounds(const FIntVector& ChunkCoord, FBox& OutBounds) const override { return false; }
		virtual FBox GetTotalBounds() const override { return FBox(ForceInit); }
		virtual FString GetDebugStats() const override { return FString(); }
		virtual FString GetRendererTypeName() const override { return TEXT("Null (test)"); }

	private:
		bool bInitialized = false;
	};

	constexpr float PlanetRadius = 20000.0f;

	/** A game world with a chunk manager on a small planet (or a plane) that streams nothing, and room for characters. */
	struct FVoxelWorldHarness
	{
		UWorld* World = nullptr;
		UVoxelWorldConfiguration* Config = nullptr;
		AActor* Owner = nullptr;
		UVoxelChunkManager* Manager = nullptr;
		FNullMeshRenderer Renderer;

		explicit FVoxelWorldHarness(bool bPlanet)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, bPlanet ? TEXT("VCPlanetWalkableWorld") : TEXT("VCPlaneWalkableWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			if (AWorldSettings* WorldSettings = World->GetWorldSettings())
			{
				WorldSettings->NotifyBeginPlay();
			}
			Config = NewObject<UVoxelWorldConfiguration>();
			Config->AddToRoot();
			Config->WorldMode = bPlanet ? EWorldMode::SphericalPlanet : EWorldMode::InfinitePlane;
			Config->WorldOrigin = FVector::ZeroVector;
			Config->WorldRadius = PlanetRadius;
			Config->PlanetHeightScale = 1600.0f; // terrain about +-800
			Config->HeightScale = 1600.0f;
			Config->BaseHeight = 0.0f;
			Config->PlanetMaxTerrainHeight = 1000.0f;
			Config->PlanetMaxTerrainDepth = 1000.0f;
			Config->MeshingMode = EMeshingMode::DualContouring;
			Config->ChunkSize = 32;
			Config->VoxelSize = 50.0f;
			Config->ViewDistance = 3000.0f;
			Config->bUseGPUGeneration = false;
			Config->bUseGPUMeshing = false;
			Config->bGenerateCollision = false;
			Config->bEnableScatter = false;
			Config->bEnableCaves = false;
			Config->bEnableBiomes = false;
			Config->bEnableWaterLevel = true;
			Config->WaterRadius = PlanetRadius; // a sea at the datum: coasts everywhere
			Config->WaterLevel = 0.0f;
			Config->NoiseParams.Seed = 9001;
			Config->NoiseParams.Frequency = 0.0004f;
			Config->NoiseParams.Octaves = 3;

			Owner = World->SpawnActor<AActor>();
			Manager = NewObject<UVoxelChunkManager>(Owner);
			Manager->RegisterComponent();
			Renderer.Initialize(World, Config);
			Manager->Initialize(Config, new FDistanceBandLODStrategy(), &Renderer);
			FVCVoxelNavigationHelper::ClearCache();
		}

		~FVoxelWorldHarness()
		{
			FVCVoxelNavigationHelper::ClearCache();
			TArray<AActor*> Actors;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (*It != Owner)
				{
					Actors.Add(*It);
				}
			}
			for (AActor* Actor : Actors)
			{
				World->DestroyActor(Actor);
			}
			Manager->Shutdown();
			Owner->Destroy();
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
			Config->RemoveFromRoot();
		}

		AVCCharacterBase* SpawnCharacter(const FVector& Location)
		{
			const FTransform Transform(Location);
			AVCCharacterBase* Character = World->SpawnActorDeferred<AVCCharacterBase>(AVCCharacterBase::StaticClass(), Transform,
				nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Character)
			{
				return nullptr;
			}
			Character->bWaitForTerrain = false;
			Character->FinishSpawning(Transform);
			return Character;
		}
	};

	/** Direction for a latitude and longitude (degrees; +Z north, longitude from +X toward +Y). */
	FVector LatLon(double Lat, double Lon)
	{
		const double La = FMath::DegreesToRadians(Lat);
		const double Lo = FMath::DegreesToRadians(Lon);
		return FVector(FMath::Cos(La) * FMath::Cos(Lo), FMath::Cos(La) * FMath::Sin(Lo), FMath::Sin(La));
	}

	double AngleDeg(const FVector& A, const FVector& B)
	{
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()), -1.0, 1.0)));
	}

	struct FSite
	{
		const TCHAR* Name;
		double Lat;
		double Lon;
	};

	const FSite Sites[] = {
		{ TEXT("north pole"), 90.0, 0.0 },
		{ TEXT("south pole"), -90.0, 0.0 },
		{ TEXT("latitude 45"), 45.0, 20.0 },
		{ TEXT("equator"), 0.0, 100.0 },
		{ TEXT("cube corner"), 35.264, 45.0 },
		{ TEXT("latitude -60"), -60.0, 200.0 },
	};
}

#define VC_PLANET_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// The gravity frame (SP-F3) and the heightmap reductions (SP-F2): with the default gravity every gravity-relative
// expression the movement component uses is the old Z arithmetic, bit for bit; with a custom gravity the round trip
// holds and a yaw-only heading lies in the local horizontal.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCPlanet_GravityFrame, "VoxelCharacter.Planet.GravityFrame", VC_PLANET_FLAGS)
bool FVCPlanet_GravityFrame::RunTest(const FString& Parameters)
{
	using namespace VCPlanetWalkableTestHelpers;
	UCharacterMovementComponent* Move = NewObject<UCharacterMovementComponent>();
	FRandomStream Random(4242);

	// Default gravity: the helpers hand back their input, the engine's gravity-space helpers reduce to Z.
	TestFalse(TEXT("A new movement component has the default gravity"), Move->HasCustomGravity());
	int32 Mismatches = 0;
	for (int32 i = 0; i < 500; ++i)
	{
		const FVector V(Random.FRandRange(-5.0e5, 5.0e5), Random.FRandRange(-5.0e5, 5.0e5), Random.FRandRange(-5.0e5, 5.0e5));
		const float H = Random.FRandRange(0.0f, 200.0f);
		const FRotator Rot(Random.FRandRange(-89.0, 89.0), Random.FRandRange(-180.0, 180.0), Random.FRandRange(-10.0, 10.0));
		Mismatches += (Move->GetGravitySpaceZ(V) != V.Z) ? 1 : 0;
		FVector S = V;
		Move->SetGravitySpaceZ(S, H);
		Mismatches += (S != FVector(V.X, V.Y, H)) ? 1 : 0;
		Mismatches += (Move->ProjectToGravityFloor(V) != FVector(V.X, V.Y, 0.0)) ? 1 : 0;
		Mismatches += (V + Move->GetGravityDirection() * H != V - FVector(0.f, 0.f, H)) ? 1 : 0;
		Mismatches += (V + FVCGravityFrame::GetUp(Move) * H != V + FVector(0.f, 0.f, H)) ? 1 : 0;
		Mismatches += (FVCGravityFrame::ToGravityRelative(Move, Rot) != Rot) ? 1 : 0;
		Mismatches += (FVCGravityFrame::ToWorld(Move, Rot) != Rot) ? 1 : 0;
		Mismatches += (FVCGravityFrame::GetYawRotation(Move, Rot) != FRotator(0.0, Rot.Yaw, 0.0)) ? 1 : 0;
		Mismatches += (FVCGravityFrame::VectorToWorld(Move, V) != V) ? 1 : 0;
		Mismatches += (FVCGravityFrame::GetHorizontalSpeed(Move, V) != V.Size2D()) ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("Default gravity: %d of 5,000 expressions differ from the old Z arithmetic"), Mismatches));
	TestEqual(TEXT("Default gravity: the old values bit for bit"), Mismatches, 0);

	// Custom gravity at the sites.
	double WorstRoundTrip = 0.0, WorstHeadingUp = 0.0, WorstPitch = 0.0;
	for (const FSite& Site : Sites)
	{
		const FVector Up = LatLon(Site.Lat, Site.Lon);
		Move->SetGravityDirection(-Up);
		for (int32 i = 0; i < 50; ++i)
		{
			const FRotator World(Random.FRandRange(-80.0, 80.0), Random.FRandRange(-180.0, 180.0), 0.0);
			const FRotator Back = FVCGravityFrame::ToWorld(Move, FVCGravityFrame::ToGravityRelative(Move, World));
			WorstRoundTrip = FMath::Max(WorstRoundTrip, AngleDeg(Back.Vector(), World.Vector()));
			const FRotator Yaw = FVCGravityFrame::GetYawRotation(Move, World);
			WorstHeadingUp = FMath::Max(WorstHeadingUp, FMath::Abs(FVector::DotProduct(Yaw.Vector(), Up)));
			WorstHeadingUp = FMath::Max(WorstHeadingUp, 1.0 - FVector::DotProduct(FRotationMatrix(Yaw).GetUnitAxis(EAxis::Z), Up));
			// A view along the local horizontal has zero gravity-relative pitch.
			const FRotator Level = FVCGravityFrame::ToGravityRelative(Move, Yaw);
			WorstPitch = FMath::Max(WorstPitch, FMath::Abs(Level.Pitch));
		}
		const FVector Walk = (FRotationMatrix(FVCGravityFrame::GetYawRotation(Move, FRotator(0.0, 30.0, 0.0))).GetUnitAxis(EAxis::X) * 600.0) + Up * 200.0;
		TestTrue(FString::Printf(TEXT("%s: the horizontal speed ignores the speed along up"), Site.Name),
			FMath::IsNearlyEqual(FVCGravityFrame::GetHorizontalSpeed(Move, Walk), 600.0, 1.0e-6));
	}
	AddInfo(FString::Printf(TEXT("Custom gravity: round trip %.2e deg, heading off the horizontal %.2e, level pitch %.2e deg"),
		WorstRoundTrip, WorstHeadingUp, WorstPitch));
	TestTrue(TEXT("World -> gravity -> world round trip"), WorstRoundTrip < 1.0e-4);
	TestTrue(TEXT("A yaw-only heading lies in the local horizontal, upright"), WorstHeadingUp < 1.0e-6);
	TestTrue(TEXT("A level view has zero relative pitch"), WorstPitch < 1.0e-4);
	return true;
}

// ---------------------------------------------------------------------------
// SP-F1: a character in the air over a planet falls toward its centre at every site and stands upright in its gravity;
// on a plane gravity stays the engine's default.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCPlanet_GravityTowardCentre, "VoxelCharacter.Planet.GravityTowardCentre", VC_PLANET_FLAGS)
bool FVCPlanet_GravityTowardCentre::RunTest(const FString& Parameters)
{
	using namespace VCPlanetWalkableTestHelpers;
	TestTrue(TEXT("Pure: gravity toward the centre"),
		UVCMovementComponent::ComputePlanetGravityDirection(FVector(0.0, 0.0, 30000.0), FVector(0.0, 0.0, 10000.0), FVector::UpVector).Equals(FVector(0.0, 0.0, -1.0)));
	TestTrue(TEXT("Pure: at the centre, the fallback"),
		UVCMovementComponent::ComputePlanetGravityDirection(FVector(5.0), FVector(5.0), FVector::ForwardVector).Equals(FVector::ForwardVector));
	{
		FVoxelWorldHarness Planet(/*bPlanet=*/true);
		for (const FSite& Site : Sites)
		{
			const FVector Up = LatLon(Site.Lat, Site.Lon);
			AVCCharacterBase* Character = Planet.SpawnCharacter(Up * (PlanetRadius + 3000.0));
			UVCMovementComponent* Move = Character ? Cast<UVCMovementComponent>(Character->GetCharacterMovement()) : nullptr;
			if (!Move)
			{
				AddError(FString::Printf(TEXT("%s: spawn failed"), Site.Name));
				continue;
			}
			Move->bRunPhysicsWithNoController = true;
			Move->SetMovementMode(MOVE_Falling);
			for (int32 Frame = 0; Frame < 20; ++Frame)
			{
				Move->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
			}
			const double GravityOff = AngleDeg(-Move->GetGravityDirection(), Character->GetActorLocation());
			const double FallOff = AngleDeg(-Move->Velocity, Character->GetActorLocation());
			Move->SnapUprightToGravity();
			const double UprightOff = AngleDeg(Character->GetActorUpVector(), Character->GetActorLocation());
			AddInfo(FString::Printf(TEXT("%s: gravity %.4f deg off the radius, fall %.4f deg, upright %.4f deg, speed %.0f"),
				Site.Name, GravityOff, FallOff, UprightOff, Move->Velocity.Size()));
			TestTrue(FString::Printf(TEXT("%s: planet gravity"), Site.Name), Move->HasPlanetGravity());
			TestTrue(FString::Printf(TEXT("%s: gravity points at the centre"), Site.Name), GravityOff < 0.01);
			TestTrue(FString::Printf(TEXT("%s: the body falls along the radius"), Site.Name), FallOff < 0.05 && Move->Velocity.Size() > 100.0);
			TestTrue(FString::Printf(TEXT("%s: upright in its gravity"), Site.Name), UprightOff < 0.01);
			Character->Destroy();
		}
	}
	{
		FVoxelWorldHarness Plane(/*bPlanet=*/false);
		AVCCharacterBase* Character = Plane.SpawnCharacter(FVector(1000.0, -2000.0, 5000.0));
		UVCMovementComponent* Move = Character ? Cast<UVCMovementComponent>(Character->GetCharacterMovement()) : nullptr;
		if (!Move)
		{
			AddError(TEXT("Plane: spawn failed"));
			return false;
		}
		Move->bRunPhysicsWithNoController = true;
		Move->SetMovementMode(MOVE_Falling);
		for (int32 Frame = 0; Frame < 20; ++Frame)
		{
			Move->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
		}
		TestFalse(TEXT("Plane: no planet gravity"), Move->HasPlanetGravity());
		TestFalse(TEXT("Plane: the engine's default gravity"), Move->HasCustomGravity());
		TestTrue(TEXT("Plane: falls along world -Z"), Move->Velocity.Z < -100.0 && FMath::IsNearlyZero(Move->Velocity.X) && FMath::IsNearlyZero(Move->Velocity.Y));
	}
	return true;
}

// ---------------------------------------------------------------------------
// SP-F4: the spawn search by direction on a planet: on the analytic surface along the start direction when it is dry,
// else the nearest dry ring; on a plane the old (X, Y, height) answer.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCPlanet_SpawnByDirection, "VoxelCharacter.Planet.SpawnByDirection", VC_PLANET_FLAGS)
bool FVCPlanet_SpawnByDirection::RunTest(const FString& Parameters)
{
	using namespace VCPlanetWalkableTestHelpers;

	// The ring search on a synthetic planet: a sphere of radius 10,000 flooded but for a cap 10 degrees around +X.
	{
		const FVector Land = FVector::ForwardVector;
		const double R = 10000.0;
		const FVoxelSeaSurface Sea = FVoxelSeaSurface::MakeSphere(FVector::ZeroVector, R);
		auto SurfaceAt = [&](const FVector& Direction)
		{
			const bool bDry = AngleDeg(Direction, Land) < 10.0;
			return Direction * (bDry ? R + 100.0 : R - 100.0);
		};
		FVector Out;
		int32 Ring = 0;
		const FVector Start = LatLon(25.0, 0.0); // 25 degrees north of the land's centre
		const bool bFound = FVCVoxelNavigationHelper::FindPlanetSpawnPoint(FVector::ZeroVector, R, Start, 500.0, 6000.0, SurfaceAt, Sea, Out, Ring);
		const double FromStart = AngleDeg(Out, Start);
		AddInfo(FString::Printf(TEXT("Synthetic: found %d, ring %d, %.2f deg from the start, %.2f from the land's centre"), bFound ? 1 : 0, Ring,
			FromStart, AngleDeg(Out, Land)));
		TestTrue(TEXT("Synthetic: land found from the sea"), bFound);
		TestTrue(TEXT("Synthetic: in the land cap"), AngleDeg(Out, Land) < 10.0);
		// The first ring reaching the cap: 15 degrees is 2,618 of arc, so ring 6 (3,000) at 500 a ring.
		TestEqual(TEXT("Synthetic: the nearest ring that reaches land"), Ring, 6);
		TestTrue(TEXT("Synthetic: that ring's arc from the start"), FMath::IsNearlyEqual(FromStart, FMath::RadiansToDegrees(Ring * 500.0 / R), 1.0e-6));
		int32 NoRing = 0;
		TestFalse(TEXT("Synthetic: no land within the search arc"),
			FVCVoxelNavigationHelper::FindPlanetSpawnPoint(FVector::ZeroVector, R, -Land, 500.0, 6000.0, SurfaceAt, Sea, Out, NoRing));
	}

	// The real planet: a sea at the datum, so some sites start wet.
	{
		FVoxelWorldHarness Planet(/*bPlanet=*/true);
		int32 DryStarts = 0, WetStarts = 0;
		double WorstOnSurface = 0.0;
		for (const FSite& Site : Sites)
		{
			const FVector Start = LatLon(Site.Lat, Site.Lon) * (PlanetRadius + 5000.0);
			const bool bStartDry = Planet.Manager->GetSeaDepthAt(Planet.Manager->GetSurfacePointAt(Start).Position) < 0.0f;
			FVector Out;
			const bool bFound = FVCVoxelNavigationHelper::FindSpawnablePosition(Planet.World, Start, Out);
			TestTrue(FString::Printf(TEXT("%s: found land"), Site.Name), bFound);
			if (!bFound)
			{
				continue;
			}
			const FVoxelTerrainSurfacePoint Surface = Planet.Manager->GetSurfacePointAt(Out);
			WorstOnSurface = FMath::Max(WorstOnSurface, FVector::Dist(Out, Surface.Position));
			TestTrue(FString::Printf(TEXT("%s: above the sea"), Site.Name), Planet.Manager->GetSeaDepthAt(Out) < 0.0f);
			if (bStartDry)
			{
				++DryStarts;
				TestTrue(FString::Printf(TEXT("%s: a dry start stays on its direction"), Site.Name), AngleDeg(Out, Start) < 1.0e-6);
			}
			else
			{
				++WetStarts;
				TestTrue(FString::Printf(TEXT("%s: a sea start moves to land"), Site.Name), AngleDeg(Out, Start) > 1.0e-3);
			}
			AddInfo(FString::Printf(TEXT("%s: start %s, spawn %.2f deg from it at altitude %.0f"), Site.Name, bStartDry ? TEXT("dry") : TEXT("wet"),
				AngleDeg(Out, Start), Surface.Altitude));
		}
		AddInfo(FString::Printf(TEXT("Planet: %d dry and %d wet starts, worst distance from the analytic surface %.4f"), DryStarts, WetStarts, WorstOnSurface));
		TestTrue(TEXT("Every spawn is on the analytic surface"), WorstOnSurface < 1.0);
		TestTrue(TEXT("Both kinds of start were tried"), DryStarts > 0 && WetStarts > 0);
	}

	// A plane: the old answer, (X, Y) at the generated surface height.
	{
		FVoxelWorldHarness Plane(/*bPlanet=*/false);
		Plane.Config->WaterLevel = -1.0e6f; // no sea in the way
		FVector Out;
		const bool bFound = FVCVoxelNavigationHelper::FindSpawnablePosition(Plane.World, FVector(1234.0, -5678.0, 9999.0), Out);
		TestTrue(TEXT("Plane: found"), bFound);
		TestTrue(TEXT("Plane: (X, Y, generated height) as before"),
			Out == FVector(1234.0, -5678.0, Plane.Manager->GetGeneratedSurfaceHeight(1234.0, -5678.0)));
	}
	return true;
}

// ---------------------------------------------------------------------------
// SP-F5: out of the world is radial on a planet (inside the shell's inner radius by OutOfWorldDepth), below the lowest
// terrain elsewhere.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCPlanet_OutOfWorld, "VoxelCharacter.Planet.OutOfWorld", VC_PLANET_FLAGS)
bool FVCPlanet_OutOfWorld::RunTest(const FString& Parameters)
{
	FVoxelWorldBounds Shell;
	Shell.Shape = EVoxelWorldBoundsShape::Shell;
	Shell.Center = FVector(100.0, 200.0, 300.0);
	Shell.InnerRadius = 243500.0f;
	Shell.OuterRadius = 256500.0f;
	Shell.MinZ = 300.0f - 256500.0f;
	const FVector South = Shell.Center + FVector(0.0, 0.0, -249000.0); // the south pole's surface
	TestFalse(TEXT("Planet: the south pole's surface is in the world (world Z far below the plane rule's floor)"),
		AVCCharacterBase::IsOutOfWorld(Shell, South, 20000.f));
	TestFalse(TEXT("Planet: just inside the inner radius is still in"), AVCCharacterBase::IsOutOfWorld(Shell, Shell.Center + FVector(1.0, 0.0, 0.0) * 230000.0, 20000.f));
	TestTrue(TEXT("Planet: deep inside the core is out"), AVCCharacterBase::IsOutOfWorld(Shell, Shell.Center + FVector(0.0, 0.6, 0.8) * 200000.0, 20000.f));
	TestFalse(TEXT("Depth 0: never"), AVCCharacterBase::IsOutOfWorld(Shell, Shell.Center, 0.f));
	TestFalse(TEXT("Planet without an orbit radius: no outer bound"), AVCCharacterBase::IsOutOfWorld(Shell, Shell.Center + FVector(0.0, 0.0, -1.0e7), 20000.f));
	Shell.OrbitRadius = 1000000.0; // the datum (250,000) + a 3 R ceiling (spherical Phase 6)
	TestFalse(TEXT("Planet: 2 R over the south pole is in"), AVCCharacterBase::IsOutOfWorld(Shell, Shell.Center + FVector(0.0, 0.0, -750000.0), 20000.f));
	TestTrue(TEXT("Planet: past the orbit radius is out"), AVCCharacterBase::IsOutOfWorld(Shell, Shell.Center + FVector(0.6, 0.0, -0.8) * 1000001.0, 20000.f));

	FVoxelWorldBounds Plane;
	Plane.Shape = EVoxelWorldBoundsShape::Unbounded;
	Plane.MinZ = -6000.0f;
	TestTrue(TEXT("Plane: below the lowest terrain by the depth is out"), AVCCharacterBase::IsOutOfWorld(Plane, FVector(0.0, 0.0, -26001.0), 20000.f));
	TestFalse(TEXT("Plane: above it is in"), AVCCharacterBase::IsOutOfWorld(Plane, FVector(0.0, 0.0, -25999.0), 20000.f));
	Plane.MinZ = -1.0e9f;
	TestFalse(TEXT("Plane without a terrain range: never"), AVCCharacterBase::IsOutOfWorld(Plane, FVector(0.0, 0.0, -1.0e8), 20000.f));
	return true;
}

// ---------------------------------------------------------------------------
// Spherical Phase 6, WM-BP2 (D20): debug flight in the gravity frame. At every site a flyer climbs along local up at the
// set rate, holds its distance from the centre with no climb (flying across the planet too, where a straight line would
// rise off it), comes back down, and stops into a fall; another mode ends the flight. On a plane the held level is Z.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCPlanet_Fly, "VoxelCharacter.Planet.Fly", VC_PLANET_FLAGS)
bool FVCPlanet_Fly::RunTest(const FString& Parameters)
{
	using namespace VCPlanetWalkableTestHelpers;
	constexpr float Dt = 1.0f / 60.0f;
	constexpr float FlySpeed = 1200.0f;
	auto Tick = [](UVCMovementComponent* Move, int32 Frames, const FVector* Input = nullptr)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			if (Input)
			{
				// Across local up at the pawn, as vox.AutoRun feeds it.
				const FVector Up = Move->GetLocalUp();
				Move->GetPawnOwner()->AddMovementInput(FVector::CrossProduct(FVector::CrossProduct(Up, *Input), Up).GetSafeNormal(), 1.0f);
			}
			Move->TickComponent(Dt, LEVELTICK_All, nullptr);
		}
	};
	{
		FVoxelWorldHarness Planet(/*bPlanet=*/true);
		for (const FSite& Site : Sites)
		{
			const FVector Up = LatLon(Site.Lat, Site.Lon);
			const double StartRadius = PlanetRadius + 3000.0;
			AVCCharacterBase* Character = Planet.SpawnCharacter(Up * StartRadius);
			UVCMovementComponent* Move = Character ? Cast<UVCMovementComponent>(Character->GetCharacterMovement()) : nullptr;
			if (!Move)
			{
				AddError(FString::Printf(TEXT("%s: spawn failed"), Site.Name));
				continue;
			}
			Move->bRunPhysicsWithNoController = true;
			Move->MaxFlySpeed = FlySpeed;
			// Brought here tilted (a teleport from elsewhere on the planet), the body is levelled in its gravity, at the exact
			// north pole too, where that gravity is the engine's default.
			Character->SetActorRotation(FRotator(30.0, 10.0, 20.0));
			Move->UpdatePlanetGravity();
			Move->SnapUprightToGravity();
			const double SnapOff = AngleDeg(Character->GetActorUpVector(), Character->GetActorLocation());
			TestTrue(FString::Printf(TEXT("%s: a tilted body snaps upright (%.4f deg)"), Site.Name, SnapOff), SnapOff < 0.01);
			Move->SetDebugFlight(true);
			TestTrue(FString::Printf(TEXT("%s: flying"), Site.Name), Move->IsDebugFlying() && Move->IsFlying());

			// Climb one second at full rate: along the radius, MaxFlySpeed far.
			Move->SetFlightClimb(1.0f);
			Tick(Move, 60);
			const FVector Climbed = Character->GetActorLocation();
			const double Rise = Climbed.Size() - StartRadius;
			const double ClimbOff = AngleDeg(Climbed, Up);

			// No climb for a second: the radius holds.
			Move->SetFlightClimb(0.0f);
			const double HeldAt = Character->GetActorLocation().Size();
			Tick(Move, 60);
			const double Drift = Character->GetActorLocation().Size() - HeldAt;

			// Across the planet for two seconds with no climb: the radius holds, the pawn moves along the surface.
			const FVector Heading = FVector::CrossProduct(Up, FMath::Abs(Up.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector).GetSafeNormal();
			const FVector Before = Character->GetActorLocation();
			Tick(Move, 120, &Heading);
			const double Across = AngleDeg(Character->GetActorLocation(), Before) * FMath::DegreesToRadians(1.0) * HeldAt;
			const double CrossDrift = Character->GetActorLocation().Size() - HeldAt;

			// Back down at half rate for two seconds: the rise again.
			Move->SetFlightClimb(-0.5f);
			Tick(Move, 120);
			const double Fall = HeldAt - Character->GetActorLocation().Size();
			Move->SnapUprightToGravity();
			const double UprightOff = AngleDeg(Character->GetActorUpVector(), Character->GetActorLocation());

			AddInfo(FString::Printf(TEXT("%s: climbed %.1f (%.4f deg off the radius), held drift %.3f, across %.0f with drift %.3f, down %.1f, upright %.4f deg"),
				Site.Name, Rise, ClimbOff, Drift, Across, CrossDrift, Fall, UprightOff));
			TestTrue(FString::Printf(TEXT("%s: the climb rises MaxFlySpeed a second"), Site.Name), FMath::Abs(Rise - FlySpeed) < 30.0);
			TestTrue(FString::Printf(TEXT("%s: the climb follows local up"), Site.Name), ClimbOff < 0.01);
			TestTrue(FString::Printf(TEXT("%s: no climb holds the radius"), Site.Name), FMath::Abs(Drift) < 1.0);
			TestTrue(FString::Printf(TEXT("%s: flying across holds the radius"), Site.Name), FMath::Abs(CrossDrift) < 2.0 && Across > 0.5 * FlySpeed);
			TestTrue(FString::Printf(TEXT("%s: the descent comes back down"), Site.Name), FMath::Abs(Fall - FlySpeed) < 30.0);
			TestTrue(FString::Printf(TEXT("%s: upright in its gravity"), Site.Name), UprightOff < 0.01);
			TestTrue(FString::Printf(TEXT("%s: still flying"), Site.Name), Move->IsFlying() && Move->IsDebugFlying());

			// Stopping drops into a fall; another mode ends the flight.
			Move->SetDebugFlight(false);
			TestTrue(FString::Printf(TEXT("%s: stopping falls"), Site.Name), Move->IsFalling() && !Move->IsDebugFlying());
			Move->SetDebugFlight(true);
			Move->SetMovementMode(MOVE_Walking);
			TestFalse(FString::Printf(TEXT("%s: another mode ends the flight"), Site.Name), Move->IsDebugFlying());
			Character->Destroy();
		}
	}
	{
		FVoxelWorldHarness Plane(/*bPlanet=*/false);
		AVCCharacterBase* Character = Plane.SpawnCharacter(FVector(1000.0, -2000.0, 5000.0));
		UVCMovementComponent* Move = Character ? Cast<UVCMovementComponent>(Character->GetCharacterMovement()) : nullptr;
		if (!Move)
		{
			AddError(TEXT("Plane: spawn failed"));
			return false;
		}
		Move->bRunPhysicsWithNoController = true;
		Move->MaxFlySpeed = FlySpeed;
		Move->SetDebugFlight(true);
		const FVector Heading(1.0, 0.0, 0.0);
		Tick(Move, 60, &Heading);
		const double ZDrift = Character->GetActorLocation().Z - 5000.0;
		Move->SetFlightClimb(1.0f);
		Tick(Move, 60);
		const double Rise = Character->GetActorLocation().Z - 5000.0;
		AddInfo(FString::Printf(TEXT("Plane: Z drift %.3f flying along X, climbed %.1f"), ZDrift, Rise));
		TestTrue(TEXT("Plane: no climb holds Z"), FMath::Abs(ZDrift) < 1.0);
		TestTrue(TEXT("Plane: the climb rises along +Z"), FMath::Abs(Rise - FlySpeed) < 30.0);
	}
	return true;
}


// ---------------------------------------------------------------------------
// Spherical Phase 4, WM-SY12 (SP-F6): NPC AI in the gravity frame. With the default gravity the horizontal helpers and the
// AI's steering, wander and flee geometry are the old Z-up expressions bit for bit; with planet gravity at every site they
// lie across local up: steering toward a waypoint is its horizontal direction, wander goals are on the tangent plane at
// their distance, flee directions turn about local up, and a facing is upright with its forward along the target's
// horizontal direction.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCPlanet_NPCSteering, "VoxelCharacter.Planet.NPCSteering", VC_PLANET_FLAGS)
bool FVCPlanet_NPCSteering::RunTest(const FString& Parameters)
{
	using namespace VCPlanetWalkableTestHelpers;
	UCharacterMovementComponent* Move = NewObject<UCharacterMovementComponent>();
	FRandomStream Random(777);

	int32 Mismatches = 0;
	for (int32 i = 0; i < 500; ++i)
	{
		const FVector A(Random.FRandRange(-5.0e4, 5.0e4), Random.FRandRange(-5.0e4, 5.0e4), Random.FRandRange(-5.0e4, 5.0e4));
		const FVector B(Random.FRandRange(-5.0e4, 5.0e4), Random.FRandRange(-5.0e4, 5.0e4), Random.FRandRange(-5.0e4, 5.0e4));
		const float Angle = Random.FRandRange(0.f, 2.f * UE_PI);
		const float Dist = Random.FRandRange(100.f, 3000.f);
		const float Deg = Random.FRandRange(-120.f, 120.f);
		const FVector To = B - A;
		Mismatches += FVCGravityFrame::Horizontal(Move, To) != FVector(To.X, To.Y, 0.0) ? 1 : 0;
		Mismatches += FVCGravityFrame::HorizontalDirection(Move, To) != To.GetSafeNormal2D() ? 1 : 0;
		Mismatches += FVCGravityFrame::HorizontalDistance(Move, A, B) != FVector::Dist2D(A, B) ? 1 : 0;
		Mismatches += FVCGravityFrame::UpComponent(Move, To) != To.Z ? 1 : 0;
		Mismatches += FVCGravityFrame::FacingRotation(Move, To) != FRotator(0.f, To.Rotation().Yaw, 0.f) ? 1 : 0;
		Mismatches += AVCNPCAIController::SteerDirection(Move, To) != FVector(To.X, To.Y, 0.f).GetSafeNormal() ? 1 : 0;
		Mismatches += AVCNPCAIController::WanderGoalAt(Move, A, Angle, Dist) != A + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.f) ? 1 : 0;
		Mismatches += AVCNPCAIController::FleeDirection(Move, To.GetSafeNormal2D(), Deg) != To.GetSafeNormal2D().RotateAngleAxis(Deg, FVector::UpVector) ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("Default gravity: %d of 4,000 expressions differ from the old ones"), Mismatches));
	TestEqual(TEXT("Default gravity: the old AI geometry bit for bit"), Mismatches, 0);

	double WorstAcross = 0.0, WorstSteer = 0.0, WorstWander = 0.0, WorstFlee = 0.0, WorstFacing = 0.0;
	for (const FSite& Site : Sites)
	{
		const FVector Up = LatLon(Site.Lat, Site.Lon);
		Move->SetGravityDirection(-Up);
		const FVector Pawn = Up * (PlanetRadius + 100.0);
		for (int32 i = 0; i < 50; ++i)
		{
			const FVector Waypoint = Pawn + Random.GetUnitVector() * Random.FRandRange(200.0, 3000.0);
			const FVector To = Waypoint - Pawn;
			const FVector Steer = AVCNPCAIController::SteerDirection(Move, To);
			const FVector Expected = (To - Up * FVector::DotProduct(To, Up)).GetSafeNormal();
			WorstAcross = FMath::Max(WorstAcross, FMath::Abs(FVector::DotProduct(Steer, Up)));
			WorstSteer = FMath::Max(WorstSteer, (Steer - Expected).Size());
			WorstAcross = FMath::Max(WorstAcross, FMath::Abs(FVCGravityFrame::HorizontalDistance(Move, Pawn, Waypoint) - (To - Up * FVector::DotProduct(To, Up)).Size()));

			const float Angle = Random.FRandRange(0.f, 2.f * UE_PI);
			const FVector Goal = AVCNPCAIController::WanderGoalAt(Move, Pawn, Angle, 1500.f);
			WorstWander = FMath::Max(WorstWander, FMath::Abs(FVector::DotProduct(Goal - Pawn, Up)) + FMath::Abs((Goal - Pawn).Size() - 1500.0));

			const float Deg = Random.FRandRange(-120.f, 120.f);
			const FVector Flee = AVCNPCAIController::FleeDirection(Move, Steer, Deg);
			WorstFlee = FMath::Max(WorstFlee, FMath::Abs(FVector::DotProduct(Flee, Up))
				+ FMath::Abs(AngleDeg(Flee, Steer) - FMath::Abs(Deg)) * 1.0e-3);

			const FRotator Face = FVCGravityFrame::FacingRotation(Move, To);
			WorstFacing = FMath::Max(WorstFacing, AngleDeg(Face.Vector(), Expected));
			WorstFacing = FMath::Max(WorstFacing, AngleDeg(FRotationMatrix(Face).GetUnitAxis(EAxis::Z), Up));
		}
	}
	AddInfo(FString::Printf(TEXT("Planet gravity: across up %.2e, steer %.2e, wander %.2e, flee %.2e, facing %.2e deg"),
		WorstAcross, WorstSteer, WorstWander, WorstFlee, WorstFacing));
	TestTrue(TEXT("Steering and distances across local up"), WorstAcross < 1.0e-6);
	TestTrue(TEXT("Steering toward the waypoint's horizontal direction"), WorstSteer < 1.0e-6);
	TestTrue(TEXT("Wander goals on the tangent plane at their distance"), WorstWander < 1.0e-3);
	TestTrue(TEXT("Flee directions turn about local up"), WorstFlee < 1.0e-3);
	TestTrue(TEXT("Facing upright toward the target's horizontal direction"), WorstFacing < 1.0e-3);
	return true;
}

// ---------------------------------------------------------------------------
// Spherical Phase 4, WM-SY12: the eye height along local up for the player character and NPCs (GetPawnViewLocation, which
// sight traces, the melee sweep and auto-aim start from); on a plane the engine's location, bit for bit.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCPlanet_EyeHeight, "VoxelCharacter.Planet.EyeHeight", VC_PLANET_FLAGS)
bool FVCPlanet_EyeHeight::RunTest(const FString& Parameters)
{
	using namespace VCPlanetWalkableTestHelpers;
	auto Settle = [](ACharacter* Character)
	{
		if (UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr)
		{
			Move->bRunPhysicsWithNoController = true;
			Move->SetMovementMode(MOVE_Falling);
			for (int32 Frame = 0; Frame < 3; ++Frame)
			{
				Move->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
			}
		}
	};
	auto SpawnNPC = [](UWorld* World, const FVector& Location) -> AVCNPCCharacterBase*
	{
		const FTransform Transform(Location);
		AVCNPCCharacterBase* NPC = World->SpawnActorDeferred<AVCNPCCharacterBase>(AVCNPCCharacterBase::StaticClass(), Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (NPC)
		{
			NPC->FinishSpawning(Transform);
		}
		return NPC;
	};
	{
		FVoxelWorldHarness Planet(/*bPlanet=*/true);
		for (const FSite& Site : Sites)
		{
			const FVector Up = LatLon(Site.Lat, Site.Lon);
			AVCCharacterBase* Character = Planet.SpawnCharacter(Up * (PlanetRadius + 3000.0));
			AVCNPCCharacterBase* NPC = SpawnNPC(Planet.World, Up * (PlanetRadius + 3500.0));
			Settle(Character);
			Settle(NPC);
			for (APawn* Pawn : TArray<APawn*>{ Character, NPC })
			{
				if (!Pawn)
				{
					AddError(FString::Printf(TEXT("%s: spawn failed"), Site.Name));
					continue;
				}
				const FVector Eyes = Pawn->GetPawnViewLocation() - Pawn->GetActorLocation();
				const FVector LocalUp = Pawn->GetActorLocation().GetSafeNormal();
				FVector ViewPoint;
				FRotator ViewRotation;
				Pawn->GetActorEyesViewPoint(ViewPoint, ViewRotation);
				TestTrue(FString::Printf(TEXT("%s, %s: the eyes along local up"), Site.Name, *Pawn->GetClass()->GetName()),
					AngleDeg(Eyes, LocalUp) < 0.01 && FMath::IsNearlyEqual(Eyes.Size(), static_cast<double>(Pawn->BaseEyeHeight), 1.0e-3));
				TestTrue(FString::Printf(TEXT("%s, %s: the eyes' view point is there"), Site.Name, *Pawn->GetClass()->GetName()),
					ViewPoint.Equals(Pawn->GetPawnViewLocation(), 1.0e-6));
			}
			if (Character) { Character->Destroy(); }
			if (NPC) { NPC->Destroy(); }
		}
	}
	{
		FVoxelWorldHarness Plane(/*bPlanet=*/false);
		AVCCharacterBase* Character = Plane.SpawnCharacter(FVector(1000.0, -2000.0, 5000.0));
		AVCNPCCharacterBase* NPC = SpawnNPC(Plane.World, FVector(1500.0, -2000.0, 5000.0));
		for (APawn* Pawn : TArray<APawn*>{ Character, NPC })
		{
			if (TestNotNull(TEXT("Plane: spawned"), Pawn))
			{
				TestTrue(TEXT("Plane: the engine's eye location, bit for bit"),
					Pawn->GetPawnViewLocation() == Pawn->GetActorLocation() + FVector(0.f, 0.f, Pawn->BaseEyeHeight));
			}
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// Spherical Phase 4, WM-SY16 (SP-F7): placing and chopping in the gravity frame. With the default gravity the placement
// trace, the placed transform and the chop aim point are the old expressions bit for bit; with planet gravity the trace
// runs along local down PlaceDistance ahead on the view's heading, the placed actor stands on local up facing back, the
// slope test reads a level ground's normal as +Z, and a ragdoll gets the radial pull.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCPlanet_PlaceItem, "VoxelCharacter.Planet.PlaceItem", VC_PLANET_FLAGS)
bool FVCPlanet_PlaceItem::RunTest(const FString& Parameters)
{
	using namespace VCPlanetWalkableTestHelpers;
	UCharacterMovementComponent* Move = NewObject<UCharacterMovementComponent>();
	FRandomStream Random(99);

	int32 Mismatches = 0;
	for (int32 i = 0; i < 500; ++i)
	{
		const FVector Actor(Random.FRandRange(-5.0e4, 5.0e4), Random.FRandRange(-5.0e4, 5.0e4), Random.FRandRange(-5.0e3, 5.0e3));
		const FRotator View(Random.FRandRange(-80.0, 80.0), Random.FRandRange(-180.0, 180.0), 0.0);
		const float Distance = Random.FRandRange(100.f, 400.f);
		const float Offset = Random.FRandRange(0.f, 30.f);
		FVector Start, End;
		AVCCharacterBase::GetPlacementTrace(Move, Actor, View, Distance, Start, End);
		const FVector Ahead = Actor + FRotator(0.f, View.Yaw, 0.f).Vector() * Distance;
		Mismatches += (Start != Ahead + FVector(0.f, 0.f, 200.f) || End != Ahead - FVector(0.f, 0.f, 400.f)) ? 1 : 0;
		const FVector Ground = Ahead - FVector(0.f, 0.f, 150.f);
		const FTransform Placed = AVCCharacterBase::GetPlacementTransform(Move, View, Ground, Offset);
		const FTransform Old(FRotator(0.f, View.Yaw + 180.f, 0.f), Ground + FVector(0.f, 0.f, Offset));
		Mismatches += (!Placed.GetRotation().Equals(Old.GetRotation(), 0.0) || Placed.GetLocation() != Old.GetLocation()) ? 1 : 0;
		Mismatches += AVCCharacterBase::GetChopAimPointFor(Move, Actor, View) != Actor + FRotator(0.f, View.Yaw, 0.f).Vector() * 160.f ? 1 : 0;
		Mismatches += FVCGravityFrame::VectorToGravity(Move, View.Vector()) != View.Vector() ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("Default gravity: %d of 2,000 expressions differ from the old ones"), Mismatches));
	TestEqual(TEXT("Default gravity: the old placement and chop geometry bit for bit"), Mismatches, 0);

	double WorstTrace = 0.0, WorstPlaced = 0.0, WorstNormal = 0.0, WorstChop = 0.0;
	for (const FSite& Site : Sites)
	{
		const FVector Up = LatLon(Site.Lat, Site.Lon);
		Move->SetGravityDirection(-Up);
		const FVector Actor = Up * (PlanetRadius + 100.0);
		for (int32 i = 0; i < 40; ++i)
		{
			const FRotator View(Random.FRandRange(-80.0, 80.0), Random.FRandRange(-180.0, 180.0), 0.0);
			const FVector Heading = FVCGravityFrame::FacingRotation(Move, View.Vector()).Vector();
			FVector Start, End;
			AVCCharacterBase::GetPlacementTrace(Move, Actor, View, 250.f, Start, End);
			const FVector Ahead = Actor + Heading * 250.0;
			WorstTrace = FMath::Max(WorstTrace, (Start - (Ahead + Up * 200.0)).Size() + (End - (Ahead - Up * 400.0)).Size()
				+ FMath::Abs(FVector::DotProduct(Heading, Up)));
			const FTransform Placed = AVCCharacterBase::GetPlacementTransform(Move, View, Ahead, 5.f);
			WorstPlaced = FMath::Max(WorstPlaced, AngleDeg(Placed.GetRotation().GetUpVector(), Up) + AngleDeg(Placed.GetRotation().GetForwardVector(), -Heading)
				+ (Placed.GetLocation() - (Ahead + Up * 5.0)).Size());
			WorstNormal = FMath::Max(WorstNormal, (FVCGravityFrame::VectorToGravity(Move, Up) - FVector::UpVector).Size()
				+ (FVCGravityFrame::VectorToWorld(Move, FVCGravityFrame::VectorToGravity(Move, View.Vector())) - View.Vector()).Size());
			WorstChop = FMath::Max(WorstChop, (AVCCharacterBase::GetChopAimPointFor(Move, Actor, View) - (Actor + Heading * 160.0)).Size());
		}
	}
	AddInfo(FString::Printf(TEXT("Planet gravity: trace %.2e, placed %.2e, normal %.2e, chop %.2e"), WorstTrace, WorstPlaced, WorstNormal, WorstChop));
	TestTrue(TEXT("The placement trace along local down, ahead on the heading"), WorstTrace < 1.0e-3);
	TestTrue(TEXT("The placed actor upright, facing back, offset along up"), WorstPlaced < 1.0e-2);
	TestTrue(TEXT("Level ground reads +Z in the gravity frame"), WorstNormal < 1.0e-6);
	TestTrue(TEXT("The chop aim ahead on the heading"), WorstChop < 1.0e-3);

	// The ragdoll pull: only with custom gravity, along it, the body's own gravity off.
	USkeletalMeshComponent* Body = NewObject<USkeletalMeshComponent>();
	FVector Pull;
	TestTrue(TEXT("Planet gravity: a radial ragdoll"), AVCCharacterBase::StartRadialRagdoll(Body, Move, Pull) && Pull.Equals(Move->GetGravityDirection(), 1.0e-9)
		&& !Body->IsGravityEnabled());
	UCharacterMovementComponent* PlaneMove = NewObject<UCharacterMovementComponent>();
	USkeletalMeshComponent* PlaneBody = NewObject<USkeletalMeshComponent>();
	TestFalse(TEXT("Default gravity: no radial ragdoll"), AVCCharacterBase::StartRadialRagdoll(PlaneBody, PlaneMove, Pull) || !Pull.IsZero() || !PlaneBody->IsGravityEnabled());
	return true;
}

#undef VC_PLANET_FLAGS
#endif // WITH_AUTOMATION_TESTS
