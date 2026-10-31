// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Core/VCCharacterBase.h"
#include "Core/VCNPCCharacterBase.h"
#include "Core/VCPlayerController.h"
#include "Camera/VCCameraManager.h"
#include "Gathering/VCGatherTable.h"
#include "Movement/VCMovementComponent.h"
#include "Movement/VCVoxelNavigationHelper.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "VoxelChunkManager.h"
#include "VoxelWorldConfiguration.h"
#include "DistanceBandLODStrategy.h"
#include "IVoxelMeshRenderer.h"
#include "ChunkRenderData.h"
#include "Testing/VoxelTestChunkWorld.h"

// ---------------------------------------------------------------------------
// Cubic as a game world, the character side (VoxelWorlds WORLD_MODES_REVIEW_PLAN.md §21):
// - WM-C7 (KC7, decision D29): in a cubic world a walker steps one block (MaxStepHeight = voxel size + margin) and two
//   blocks stay a wall; smooth worlds keep their 50 uu step. The camera eases a block step.
// - WM-C9 (KC12): the melee target under the crosshair is found by a thin sweep; a ray exactly through a capsule's axis
//   misses the capsule, which an aim straight at a body's centre (auto-aim, a target a block below) does.
// - WM-C8 (KC9, decision D30): a voxel tree is chopped through the server path like a scatter tree and felled at the
//   harvest entry's hits.
// ---------------------------------------------------------------------------

namespace VCCubicGameplayTestHelpers
{
	/** A renderer that draws nothing (the chunk manager needs one to initialise). */
	class FNullMeshRenderer : public IVoxelMeshRenderer
	{
	public:
		virtual void Initialize(UWorld* World, const UVoxelWorldConfiguration* WorldConfig) override { bInitialized = true; }
		virtual void Shutdown() override { bInitialized = false; }
		virtual bool IsInitialized() const override { return bInitialized; }
		virtual void UpdateChunkMeshFromCPU(const FIntVector& ChunkCoord, int32 LODLevel, const FChunkMeshData& MeshData) override {}
		virtual void RemoveChunk(const FIntVector& ChunkCoord) override {}
		virtual void ClearAllChunks() override {}
		virtual void SetMaterial(UMaterialInterface* Material) override {}
		virtual UMaterialInterface* GetMaterial() const override { return nullptr; }
		virtual void UpdateMaterialParameters() override {}
		virtual void UpdateSeamMeshFromCPU(const FIntVector& OwnerChunkCoord, uint8 SeamSlot, int32 LODLevel, FChunkMeshData&& MeshData, bool bCrossfade) override {}
		virtual void RemoveSeamMesh(const FIntVector& OwnerChunkCoord, uint8 SeamSlot) override {}
		virtual void ClearAllSeamMeshes() override {}
		virtual void GetSeamMeshOwners(TArray<FIntVector>& OutOwners) const override {}
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

	/**
	 * A playing game world with a chunk manager (cubic or smooth, 75 uu voxels) that streams nothing: the ground is static
	 * mesh boxes, so a walker's steps are exactly what the test builds.
	 */
	struct FBoxWorld
	{
		UWorld* World = nullptr;
		UVoxelWorldConfiguration* Config = nullptr;
		AActor* Owner = nullptr;
		UVoxelChunkManager* Manager = nullptr;
		FNullMeshRenderer Renderer;
		UStaticMesh* Cube = nullptr;

		explicit FBoxWorld(bool bCubic)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, bCubic ? TEXT("VCCubicBoxWorld") : TEXT("VCSmoothBoxWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			if (AWorldSettings* WorldSettings = World->GetWorldSettings())
			{
				WorldSettings->NotifyBeginPlay();
			}
			Config = NewObject<UVoxelWorldConfiguration>();
			Config->AddToRoot();
			Config->WorldMode = EWorldMode::InfinitePlane;
			Config->MeshingMode = bCubic ? EMeshingMode::Cubic : EMeshingMode::DualContouring;
			Config->ChunkSize = 32;
			Config->VoxelSize = 75.0f;
			Config->ViewDistance = 2000.0f;
			Config->bUseGPUGeneration = false;
			Config->bGenerateCollision = false;
			Config->bEnableScatter = false;
			Config->bEnableCaves = false;
			Config->bEnableWaterLevel = false;
			Owner = World->SpawnActor<AActor>();
			Manager = NewObject<UVoxelChunkManager>(Owner);
			Manager->RegisterComponent();
			Renderer.Initialize(World, Config);
			Manager->Initialize(Config, new FDistanceBandLODStrategy(), &Renderer);
			FVCVoxelNavigationHelper::ClearCache();
			Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
		}

		~FBoxWorld()
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

		/** A blocking box (the engine cube is 100 uu, centred) from Min to Max. */
		AStaticMeshActor* Box(const FVector& Min, const FVector& Max)
		{
			AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>((Min + Max) * 0.5, FRotator::ZeroRotator);
			if (Actor && Cube)
			{
				Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
				Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
				Actor->SetActorScale3D((Max - Min) / 100.0);
				Actor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			}
			return Actor;
		}

		AVCCharacterBase* SpawnCharacter(const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator)
		{
			const FTransform Transform(Rotation, Location);
			AVCCharacterBase* Character = World->SpawnActorDeferred<AVCCharacterBase>(AVCCharacterBase::StaticClass(), Transform,
				nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (Character)
			{
				Character->bWaitForTerrain = false;
				Character->FinishSpawning(Transform);
			}
			return Character;
		}
	};

	/** Walk a character along +X for Seconds by feeding movement input and ticking its movement (no controller). */
	void Walk(ACharacter* Character, float Seconds, const FVector& Direction)
	{
		UCharacterMovementComponent* Move = Character->GetCharacterMovement();
		Move->bRunPhysicsWithNoController = true;
		const int32 Frames = FMath::CeilToInt(Seconds * 60.0f);
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			if (!Direction.IsZero())
			{
				Character->AddMovementInput(Direction, 1.0f);
			}
			Move->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
		}
	}

	float FeetZ(const ACharacter* Character)
	{
		return static_cast<float>(Character->GetActorLocation().Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	}
}

#define VC_CUBIC_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// WM-C7: the step height for the world's surface, and a walker climbing one block but not two.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCCubic_StepUpOneBlock, "VoxelCharacter.Cubic.StepUpOneBlock", VC_CUBIC_FLAGS)
bool FVCCubic_StepUpOneBlock::RunTest(const FString& Parameters)
{
	using namespace VCCubicGameplayTestHelpers;
	TestEqual(TEXT("One block of 75 uu with the 5 uu margin"), UVCMovementComponent::GetBlockStepHeight(75.0f, 5.0f), 80.0f);

	for (const bool bCubic : { true, false })
	{
		const TCHAR* Mode = bCubic ? TEXT("Cubic") : TEXT("Smooth");
		FBoxWorld W(bCubic);
		if (!TestNotNull(TEXT("The engine cube"), W.Cube))
		{
			return false;
		}
		// Ground at Z 0; a block (75) from X 300 to 700; two more blocks above it (to 225) from X 700 on.
		W.Box(FVector(-1500.0, -600.0, -100.0), FVector(1500.0, 600.0, 0.0));
		W.Box(FVector(300.0, -600.0, 0.0), FVector(700.0, 600.0, 75.0));
		W.Box(FVector(700.0, -600.0, 0.0), FVector(1100.0, 600.0, 225.0));
		AVCCharacterBase* Character = W.SpawnCharacter(FVector(0.0, 0.0, 200.0));
		if (!TestNotNull(FString::Printf(TEXT("%s: spawned"), Mode), Character))
		{
			continue;
		}
		UVCMovementComponent* Move = Cast<UVCMovementComponent>(Character->GetCharacterMovement());
		Move->SetMovementMode(MOVE_Falling); // no voxel terrain to wait for: start falling onto the boxes
		Walk(Character, 1.0f, FVector::ZeroVector); // land, and let the terrain refresh see the voxel world
		TestEqual(FString::Printf(TEXT("%s: the step height"), Mode), Move->MaxStepHeight, bCubic ? 80.0f : 50.0f);
		TestEqual(FString::Printf(TEXT("%s: stepping blocks"), Mode), Move->IsSteppingBlocks(), bCubic);
		TestTrue(FString::Printf(TEXT("%s: standing on the ground"), Mode), Move->IsMovingOnGround() && FMath::Abs(FeetZ(Character)) < 5.0f);

		Walk(Character, 4.0f, FVector::ForwardVector);
		const FVector At = Character->GetActorLocation();
		AddInfo(FString::Printf(TEXT("%s: after 4 s walking +X the feet are at X %.0f, Z %.1f"), Mode, At.X, FeetZ(Character)));
		if (bCubic)
		{
			TestTrue(TEXT("Cubic: up the one-block step without a jump"), FMath::Abs(FeetZ(Character) - 75.0f) < 5.0f && At.X > 300.0);
			TestTrue(TEXT("Cubic: stopped by the two-block rise"), At.X < 700.0);
		}
		else
		{
			TestTrue(TEXT("Smooth: a 75 uu step stays a wall (50 uu step)"), FMath::Abs(FeetZ(Character)) < 5.0f && At.X < 300.0);
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
// WM-C7: the camera's block-step ease (pure).
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCCubic_StepEase, "VoxelCharacter.Camera.BlockStepEase", VC_CUBIC_FLAGS)
bool FVCCubic_StepEase::RunTest(const FString& Parameters)
{
	constexpr float Dt = 1.0f / 60.0f;
	// A step up of one block: the view stays where it was, then eases after the pawn, never overshooting.
	float Offset = UVCCameraManager::ComputeStepEaseOffset(0.0f, 75.0f, true, 20.0f, 90.0f, Dt, 12.0f);
	TestTrue(TEXT("Step up: the view starts most of a block under the pawn's new eyes"), Offset <= -55.0f && Offset > -75.0f);
	float Last = Offset;
	bool bMonotonic = true;
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Offset = UVCCameraManager::ComputeStepEaseOffset(Offset, 0.0f, true, 20.0f, 90.0f, Dt, 12.0f);
		bMonotonic &= Offset >= Last && Offset <= 0.0f;
		Last = Offset;
	}
	TestTrue(TEXT("Step up: eases toward the pawn without overshooting"), bMonotonic);
	TestTrue(TEXT("Step up: closed after half a second"), FMath::Abs(Offset) < 2.0f);
	// A step down eases the other way.
	TestTrue(TEXT("Step down: the view starts above"), UVCCameraManager::ComputeStepEaseOffset(0.0f, -75.0f, true, 20.0f, 90.0f, Dt, 12.0f) >= 55.0f);
	// Walking a slope, a jump or a fall add nothing; a teleport drops the offset.
	TestEqual(TEXT("A slope's frame of rise is not a step"), UVCCameraManager::ComputeStepEaseOffset(0.0f, 8.0f, true, 20.0f, 90.0f, Dt, 12.0f), 0.0f);
	TestEqual(TEXT("Airborne: nothing added"), UVCCameraManager::ComputeStepEaseOffset(0.0f, 75.0f, false, 20.0f, 90.0f, Dt, 12.0f), 0.0f);
	TestEqual(TEXT("A teleport drops the ease"), UVCCameraManager::ComputeStepEaseOffset(-40.0f, 3000.0f, true, 20.0f, 90.0f, Dt, 12.0f), 0.0f);
	return true;
}

// ---------------------------------------------------------------------------
// WM-C9 (KC12): the target under the crosshair, aimed exactly at a body's centre from above, level and below, and not
// through a wall. Review A's misses across a block came from the line trace this replaced: a ray through a capsule's axis
// misses it.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCCubic_MeleeTargetThroughAxis, "VoxelCharacter.Combat.MeleeTargetThroughAxis", VC_CUBIC_FLAGS)
bool FVCCubic_MeleeTargetThroughAxis::RunTest(const FString& Parameters)
{
	using namespace VCCubicGameplayTestHelpers;
	FBoxWorld W(/*bCubic=*/true);
	AVCCharacterBase* Player = W.SpawnCharacter(FVector(0.0, 0.0, 2000.0));
	const FTransform Far(FVector(5000.0, 0.0, 2000.0));
	AVCNPCCharacterBase* Target = W.World->SpawnActorDeferred<AVCNPCCharacterBase>(AVCNPCCharacterBase::StaticClass(), Far, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("Player"), Player) || !TestNotNull(TEXT("Target"), Target))
	{
		return false;
	}
	Target->bAIEnabled = false;
	Target->FinishSpawning(Far);

	// Aims straight at the target's centre from the eyes: a block below at 1-2 blocks out (the KC12 case), level, above.
	struct FAim { double Pitch; double Yaw; double Distance; };
	const FAim Aims[] = { { -49.88, -138.0, 181.8 }, { -45.0, 30.0, 110.0 }, { -30.0, 200.0, 150.0 }, { 0.0, 0.0, 120.0 },
		{ 0.0, 90.0, 200.0 }, { 20.0, -60.0, 140.0 }, { -60.0, 10.0, 160.0 } };
	int32 Found = 0, RayMisses = 0;
	for (const FAim& Aim : Aims)
	{
		const FRotator View(Aim.Pitch, Aim.Yaw, 0.0);
		Player->SetActorRotation(View);
		FVector Eyes;
		FRotator Unused;
		Player->GetActorEyesViewPoint(Eyes, Unused);
		Target->SetActorLocation(Eyes + View.Vector() * Aim.Distance, false, nullptr, ETeleportType::TeleportPhysics);
		Found += Player->FindMeleeTargetUnderCrosshair() == Target ? 1 : 0;
		// What the line trace this replaced reports for the same aim (information: a Chaos query case, not asserted).
		FHitResult Ray;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(VCMeleeTargetTest), false, Player);
		const bool bRay = W.World->LineTraceSingleByChannel(Ray, Eyes, Eyes + View.Vector() * Player->AttackTargetRange, ECC_Pawn, Params);
		RayMisses += (!bRay || Ray.GetActor() != Target) ? 1 : 0;
	}
	AddInfo(FString::Printf(TEXT("%d of %d aims at a body's centre find it; the old line trace missed %d of them"), Found, UE_ARRAY_COUNT(Aims), RayMisses));
	TestEqual(TEXT("Every aim at a body's centre finds it"), Found, static_cast<int32>(UE_ARRAY_COUNT(Aims)));

	// A wall between: nothing.
	Player->SetActorRotation(FRotator::ZeroRotator);
	FVector Eyes;
	FRotator Unused;
	Player->GetActorEyesViewPoint(Eyes, Unused);
	Target->SetActorLocation(Eyes + FVector(200.0, 0.0, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
	W.Box(Eyes + FVector(80.0, -100.0, -100.0), Eyes + FVector(110.0, 100.0, 100.0));
	TestNull(TEXT("Not through a wall"), Player->FindMeleeTargetUnderCrosshair());
	// Out of reach: nothing.
	Target->SetActorLocation(Eyes + FVector(0.0, 400.0, 0.0), false, nullptr, ETeleportType::TeleportPhysics);
	Player->SetActorRotation(FRotator(0.0, 90.0, 0.0));
	TestNull(TEXT("Not beyond AttackTargetRange"), Player->FindMeleeTargetUnderCrosshair());
	return true;
}

// ---------------------------------------------------------------------------
// WM-C8 (KC9, D30): chopping a voxel tree through the server path (AVCPlayerController::ChopVoxelTreeAt): it stands
// until the harvest entry's third hit, which fells it to its stump; a stump is not chopped again.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCCubic_FellVoxelTree, "VoxelCharacter.Cubic.FellVoxelTree", VC_CUBIC_FLAGS)
bool FVCCubic_FellVoxelTree::RunTest(const FString& Parameters)
{
	VoxelTesting::FTestChunkWorld W;
	W.Create(TEXT("VCCubicFellTree"));
	W.ConfigureSmallPlane();
	W.Config->MeshingMode = EMeshingMode::Cubic;
	W.Config->WorldSeed = 9001;
	FVoxelTreeTemplate Oak;
	Oak.TrunkHeight = 5;
	Oak.TrunkHeightVariance = 1;
	Oak.CanopyRadius = 2;
	Oak.CanopyRadiusVariance = 1;
	Oak.MaxSlopeDegrees = 90.0f;
	W.Config->TreeTemplates = { Oak };
	W.Config->TreeDensity = 3.0f;
	W.Config->TreeMode = EVoxelTreeMode::VoxelData;
	W.StartManager();
	FVCVoxelNavigationHelper::ClearCache();
	W.SetViewer(FVector(0.0, 0.0, W.Manager->GetGeneratedSurfaceHeight(0.0, 0.0) + 300.0));
	if (!TestTrue(TEXT("Streaming settles"), W.Settle()))
	{
		W.Stop();
		return false;
	}

	TArray<FVoxelTreeInstance> Trees;
	W.Manager->FindVoxelTreesNear(FVector::ZeroVector, 1200.0f, Trees);
	if (!TestTrue(TEXT("A standing voxel tree near the viewer"), Trees.Num() > 0))
	{
		W.Stop();
		return false;
	}
	const FVoxelTreeInstance Tree = Trees[0];
	const FIntVector TrunkBlock = Tree.BaseVoxel + FIntVector(0, 0, 1);
	const FVector Trunk = W.Manager->GlobalVoxelToWorldCentre(TrunkBlock);

	// A pawn beside the trunk and a controller with a gather table; the server path needs nothing else.
	const FTransform Stand(Trunk + FVector(150.0, 0.0, 50.0));
	AVCCharacterBase* Chopper = W.World->SpawnActorDeferred<AVCCharacterBase>(AVCCharacterBase::StaticClass(), Stand, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	AVCPlayerController* PC = W.World->SpawnActor<AVCPlayerController>();
	UVCGatherTable* Table = NewObject<UVCGatherTable>();
	FVCHarvestEntry Entry;
	Entry.Category = TEXT("Tree");
	Entry.BaseYield = 1;
	Entry.ToolYield = 2;
	Entry.HitsToRemove = 3;
	Table->HarvestEntries.Add(Entry);
	if (!TestNotNull(TEXT("Chopper"), Chopper) || !TestNotNull(TEXT("Controller"), PC))
	{
		W.Stop();
		return false;
	}
	Chopper->bWaitForTerrain = false;
	Chopper->FinishSpawning(Stand);
	PC->SetPawn(Chopper);
	PC->GatherTable = Table;

	TArray<FVoxelTreeVoxel> Blocks;
	W.Manager->GetVoxelTreeBlocks(Tree, Blocks);
	const int32 Before = Blocks.Num();
	const int32 Stump = Blocks.FilterByPredicate([&Tree](const FVoxelTreeVoxel& B) { return B.bTrunk && B.GlobalVoxel.Z == Tree.BaseVoxel.Z; }).Num();
	TestTrue(TEXT("Hit 1 chops"), PC->ChopVoxelTreeAt(TrunkBlock));
	TestTrue(TEXT("Hit 2 chops"), PC->ChopVoxelTreeAt(TrunkBlock));
	TestTrue(TEXT("Still standing after two hits"), AVCPlayerController::IsVoxelTreeStanding(W.Manager, Tree));
	TestTrue(TEXT("Hit 3 chops"), PC->ChopVoxelTreeAt(TrunkBlock));
	W.Manager->GetVoxelTreeBlocks(Tree, Blocks);
	AddInfo(FString::Printf(TEXT("Tree at %s: %d blocks before, %d after the third hit (stump %d)"), *Tree.BaseVoxel.ToString(), Before, Blocks.Num(), Stump));
	TestFalse(TEXT("Felled at the third hit"), AVCPlayerController::IsVoxelTreeStanding(W.Manager, Tree));
	TestEqual(TEXT("Only the stump is left"), Blocks.Num(), Stump);
	TestFalse(TEXT("A stump is not chopped"), PC->ChopVoxelTreeAt(Tree.BaseVoxel));
	TestFalse(TEXT("The felled trunk's air is not chopped"), PC->ChopVoxelTreeAt(TrunkBlock));

	// Out of reach: a standing tree 800 uu or more away is not chopped.
	if (Trees.Num() > 1)
	{
		const FIntVector OtherTrunk = Trees.Last().BaseVoxel + FIntVector(0, 0, 1);
		Chopper->SetActorLocation(W.Manager->GlobalVoxelToWorldCentre(OtherTrunk) + FVector(5000.0, 0.0, 0.0));
		TestFalse(TEXT("Out of reach"), PC->ChopVoxelTreeAt(OtherTrunk));
	}
	PC->SetPawn(nullptr);
	W.Stop();
	return true;
}

#endif // WITH_AUTOMATION_TESTS
