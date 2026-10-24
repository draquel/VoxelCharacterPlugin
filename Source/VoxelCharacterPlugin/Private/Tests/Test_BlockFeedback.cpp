// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Editing/VCBlockInteractionComponent.h"
#include "Core/VCCharacterBase.h"
#include "Core/VCNPCCharacterBase.h"
#include "Core/VCPlayerController.h"
#include "Camera/VCCameraManager.h"
#include "Gathering/VCGatherTable.h"
#include "Movement/VCVoxelNavigationHelper.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "HAL/IConsoleManager.h"
#include "IVoxelEditValidator.h"
#include "VoxelChunkManager.h"
#include "VoxelCoordinates.h"
#include "VoxelEditManager.h"
#include "VoxelMaterialRegistry.h"
#include "VoxelWorldConfiguration.h"
#include "Testing/VoxelTestChunkWorld.h"
#include "Tests/VCBlockBreakTestListener.h"

// ---------------------------------------------------------------------------
// Cubic block feedback (VoxelWorlds WORLD_MODES_REVIEW_PLAN.md §22):
// - WM-C12 / WM-C13: the outline and the ghost are the cells of the voxels the dig and the place edit (FindEditTarget),
//   within the edit reach, only where those actions would act, only in a cubic world.
// - WM-C14 (D31, D34): a block's break time is its material's hardness / MiningSpeed.
// - WM-C15 (D32, D33): a block breaks after that time while the primary action is held, decided on the server; a release
//   or a retarget resets it; the instant dig request is refused where breaks are timed; smooth worlds dig at once.
// ---------------------------------------------------------------------------

namespace VCBlockFeedbackTestHelpers
{
	/** Set a console variable for the scope of a test and put the old value back. */
	struct FCVarScope
	{
		IConsoleVariable* Var = nullptr;
		FString Old;

		FCVarScope(const TCHAR* Name, const TCHAR* Value)
		{
			Var = IConsoleManager::Get().FindConsoleVariable(Name);
			if (Var)
			{
				Old = Var->GetString();
				Var->Set(Value, ECVF_SetByCode);
			}
		}

		~FCVarScope()
		{
			if (Var)
			{
				Var->Set(*Old, ECVF_SetByCode);
			}
		}
	};

	/** A validator that protects one voxel cell (a POI claim's stand-in). */
	class FProtectCell : public IVoxelEditValidator
	{
	public:
		FBox Cell = FBox(ForceInit);
		virtual bool CanApplyEdit(const FVector& WorldPos, EEditSource Source) const override { return !Cell.IsInsideOrOn(WorldPos); }
	};

	/**
	 * A small CPU-streamed plane (cubic or smooth) with a player character and its controller standing on it. Nothing ticks
	 * on its own: the tests drive the component's client and server halves frame by frame.
	 */
	struct FBlockWorld
	{
		VoxelTesting::FTestChunkWorld W;
		AVCCharacterBase* Character = nullptr;
		AVCPlayerController* PC = nullptr;
		UVCBlockInteractionComponent* Block = nullptr;
		UVCGatherTable* Table = nullptr;
		UStaticMesh* Cube = nullptr;
		float VoxelSize = 0.f;

		bool Start(bool bCubic, const TCHAR* Name)
		{
			W.Create(Name);
			// Actors initialised (no begin play): AActor::ProcessEvent drops every call, the RPCs this path makes among them,
			// in a world whose actors are not.
			W.World->InitializeActorsForPlay(FURL());
			W.ConfigureSmallPlane();
			W.Config->MeshingMode = bCubic ? EMeshingMode::Cubic : EMeshingMode::DualContouring;
			W.StartManager();
			FVCVoxelNavigationHelper::ClearCache();
			VoxelSize = W.Config->VoxelSize;
			W.SetViewer(FVector(0.0, 0.0, W.Manager->GetGeneratedSurfaceHeight(0.0, 0.0) + 300.0));
			if (!W.Settle())
			{
				return false;
			}
			Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			Table = NewObject<UVCGatherTable>();
			FVCGatherEntry Stone;
			Stone.MaterialId = EVoxelMaterial::Stone;
			Stone.ItemId = FPrimaryAssetId(TEXT("ItemDefinition"), TEXT("ID_Material_Stone"));
			Table->Entries.Add(Stone);
			return Cube != nullptr;
		}

		/** A player character at Location with its controller (no possession: the server path needs only the link). */
		void SpawnPlayer(const FVector& Location)
		{
			const FTransform Stand(Location);
			Character = W.World->SpawnActorDeferred<AVCCharacterBase>(AVCCharacterBase::StaticClass(), Stand, nullptr, nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			Character->bWaitForTerrain = false;
			Character->FinishSpawning(Stand);
			PC = W.World->SpawnActor<AVCPlayerController>();
			Character->Controller = PC;
			PC->SetPawn(Character);
			PC->GatherTable = Table;
			Block = Character->BlockInteraction;
		}

		FVector Centre(const FIntVector& Voxel) const { return W.Manager->GlobalVoxelToWorldCentre(Voxel); }

		FBox Cell(const FIntVector& Voxel) const
		{
			return FBox(Centre(Voxel) - FVector(VoxelSize * 0.5f), Centre(Voxel) + FVector(VoxelSize * 0.5f));
		}

		FVoxelData Voxel(const FIntVector& Coord) const { return W.Manager->GetEditMergedVoxelAtWorldPosition(Centre(Coord)); }

		/** Set one voxel (a block of Material, or air) in the edit layer. */
		void SetVoxel(const FIntVector& Coord, uint8 Material, bool bSolid)
		{
			W.Manager->GetEditManager()->ApplyEdit(Centre(Coord), FVoxelData(Material, bSolid ? 255 : 0), EEditMode::Set);
		}

		/** A blocking box over a block's cell: what the terrain's collision is for it, so a crosshair trace hits its faces. */
		AStaticMeshActor* Collider(const FIntVector& Voxel)
		{
			AStaticMeshActor* Actor = W.World->SpawnActor<AStaticMeshActor>(Centre(Voxel), FRotator::ZeroRotator);
			Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
			Actor->GetStaticMeshComponent()->SetStaticMesh(Cube);
			Actor->SetActorScale3D(FVector(VoxelSize / 100.0));
			Actor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));
			return Actor;
		}

		/** Look from From at At (the crosshair). */
		void Aim(const FVector& From, const FVector& At) const
		{
			Character->CameraManager->SetCurrentView(From, (At - From).Rotation());
		}

		/** One frame as the component's tick runs it on a listen server's own player: the server half, then the client half. */
		void Frame(float DeltaSeconds = 1.0f / 60.0f) const
		{
			Block->AdvanceServerBreak(DeltaSeconds);
			Block->TickLocal(DeltaSeconds);
		}

		/** A floating block of Material well above the ground near the origin, with its collider; all six faces exposed. */
		FIntVector FloatingBlock(uint8 Material, int32 BlocksUp = 4)
		{
			const float Top = W.Manager->GetGeneratedSurfaceHeight(0.0, 0.0);
			const FIntVector Ground = FVoxelCoordinates::WorldToVoxel(FVector(0.0, 0.0, Top - VoxelSize * 0.5f) - W.Config->WorldOrigin, VoxelSize);
			const FIntVector Coord = Ground + FIntVector(0, 0, BlocksUp);
			SetVoxel(Coord, Material, true);
			Collider(Coord);
			return Coord;
		}

		void Stop()
		{
			if (PC)
			{
				PC->SetPawn(nullptr);
			}
			if (Character)
			{
				Character->Controller = nullptr;
			}
			FVCVoxelNavigationHelper::ClearCache();
			W.Stop();
		}
	};
}

#define VC_BLOCK_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// WM-C14 (D31, D34): break time = hardness / MiningSpeed.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCBlock_BreakTime, "VoxelCharacter.Cubic.BreakTime", VC_BLOCK_FLAGS)
bool FVCBlock_BreakTime::RunTest(const FString& Parameters)
{
	VCBlockFeedbackTestHelpers::FCVarScope Scale(TEXT("vc.Dig.BreakTimeScale"), TEXT("1"));
	for (const FVoxelMaterialDefinition& Material : FVoxelMaterialRegistry::GetAllMaterials())
	{
		TestTrue(FString::Printf(TEXT("%s (%d) has a hardness of 0 or more"), *Material.Name, Material.MaterialID), Material.Hardness >= 0.f);
	}
	const float Dirt = FVoxelMaterialRegistry::GetHardness(EVoxelMaterial::Dirt);
	const float Stone = FVoxelMaterialRegistry::GetHardness(EVoxelMaterial::Stone);
	const float Iron = FVoxelMaterialRegistry::GetHardness(EVoxelMaterial::Iron);
	const float Diamond = FVoxelMaterialRegistry::GetHardness(EVoxelMaterial::Diamond);
	AddInfo(FString::Printf(TEXT("Hardness: dirt %.2f, stone %.2f, iron %.2f, diamond %.2f s"), Dirt, Stone, Iron, Diamond));
	TestTrue(TEXT("dirt < stone < iron < diamond"), Dirt < Stone && Stone < Iron && Iron < Diamond);
	TestEqual(TEXT("An unknown material has the default"), FVoxelMaterialRegistry::GetHardness(250), FVoxelMaterialRegistry::DefaultHardness);
	for (const float Speed : { 1.0f, 1.5f, 2.0f })
	{
		TestEqual(FString::Printf(TEXT("Stone at MiningSpeed %.1f"), Speed), UVCBlockInteractionComponent::GetBreakSeconds(EVoxelMaterial::Stone, Speed), Stone / Speed, 1.e-5f);
	}
	TestEqual(TEXT("The iron pickaxe (2) halves bare hands"), UVCBlockInteractionComponent::GetBreakSeconds(EVoxelMaterial::Iron, 2.0f) * 2.0f,
		UVCBlockInteractionComponent::GetBreakSeconds(EVoxelMaterial::Iron, 1.0f), 1.e-5f);
	return true;
}

// ---------------------------------------------------------------------------
// WM-C15 (D33): the crack pattern. Nothing at 0, more line length as the break goes on (the same lines, grown), every
// line on an exposed face and inside it.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCBlock_CrackPattern, "VoxelCharacter.Cubic.CrackPattern", VC_BLOCK_FLAGS)
bool FVCBlock_CrackPattern::RunTest(const FString& Parameters)
{
	const FBox Block(FVector(150.0, -75.0, 300.0), FVector(225.0, 0.0, 375.0));
	constexpr float Offset = 0.5f;
	const uint32 Seed = UVCBlockInteractionComponent::HashVoxel(FIntVector(2, -1, 4));
	TArray<FVector> Segments;
	UVCBlockInteractionComponent::BuildCrackLines(Block, 0x3F, 0.f, Seed, Offset, Segments);
	TestEqual(TEXT("No cracks at 0"), Segments.Num(), 0);

	auto TotalLength = [](const TArray<FVector>& S)
	{
		double Length = 0.0;
		for (int32 i = 0; i + 1 < S.Num(); i += 2)
		{
			Length += FVector::Dist(S[i], S[i + 1]);
		}
		return Length;
	};
	double Last = 0.0;
	bool bGrows = true;
	bool bOnFaces = true;
	for (int32 Step = 1; Step <= 20; ++Step)
	{
		const float Progress = Step / 20.f;
		// Only the top (+Z) and the -X faces are exposed.
		UVCBlockInteractionComponent::BuildCrackLines(Block, (1 << 4) | (1 << 1), Progress, Seed, Offset, Segments);
		const double Length = TotalLength(Segments);
		bGrows &= Length >= Last - 1.e-6;
		Last = Length;
		for (const FVector& P : Segments)
		{
			const bool bTop = FMath::IsNearlyEqual(P.Z, Block.Max.Z + Offset, 1.e-3) && P.X >= Block.Min.X && P.X <= Block.Max.X && P.Y >= Block.Min.Y && P.Y <= Block.Max.Y;
			const bool bMinusX = FMath::IsNearlyEqual(P.X, Block.Min.X - Offset, 1.e-3) && P.Y >= Block.Min.Y && P.Y <= Block.Max.Y && P.Z >= Block.Min.Z && P.Z <= Block.Max.Z;
			bOnFaces &= bTop || bMinusX;
		}
	}
	AddInfo(FString::Printf(TEXT("At progress 1: %d lines, %.1f uu of crack on two faces"), Segments.Num() / 2, Last));
	TestTrue(TEXT("The cracks only grow"), bGrows);
	TestTrue(TEXT("Every crack point lies on an exposed face, inside it"), bOnFaces);
	TestTrue(TEXT("A finished break is well cracked (more than a face's width of lines)"), Last > Block.GetSize().X * 2.0);
	TArray<FVector> Again;
	UVCBlockInteractionComponent::BuildCrackLines(Block, (1 << 4) | (1 << 1), 1.f, Seed, Offset, Again);
	TestTrue(TEXT("The same block cracks the same way"), Again == Segments);
	return true;
}

// ---------------------------------------------------------------------------
// WM-C12 / WM-C13: the outline and the ghost are the cells of the voxels FindEditTarget gives the dig and the place, from
// every face of a block; nothing beyond the edit reach, with edit mode off (and no tool), in a smooth world or with
// vc.Dig.Highlight 0; the ghost red with a pawn in its cell.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCBlock_Highlight, "VoxelCharacter.Cubic.BlockHighlight", VC_BLOCK_FLAGS)
bool FVCBlock_Highlight::RunTest(const FString& Parameters)
{
	using namespace VCBlockFeedbackTestHelpers;
	FCVarScope Highlight(TEXT("vc.Dig.Highlight"), TEXT("1"));
	FCVarScope PlayerInputs(TEXT("voxel.Edit.PlayerInputs"), TEXT("1"));
	FBlockWorld B;
	if (!TestTrue(TEXT("The cubic world streams"), B.Start(/*bCubic=*/true, TEXT("VCBlockHighlight"))))
	{
		B.Stop();
		return false;
	}
	const FIntVector Target = B.FloatingBlock(EVoxelMaterial::Stone);
	const FVector C = B.Centre(Target);
	B.SpawnPlayer(C + FVector(-300.0, 0.0, 0.0));
	B.Character->SetEditModeEnabled(true);

	// Every face, at the face's centre and off-centre: the dig voxel is the block, the place voxel the cell beyond the face.
	const FIntVector Normals[6] = { FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };
	const FVector2D Offsets[3] = { FVector2D(0.0, 0.0), FVector2D(0.3, -0.2), FVector2D(-0.42, 0.37) };
	int32 Aims = 0, Matches = 0, OutlineMatches = 0, GhostMatches = 0;
	for (const FIntVector& N : Normals)
	{
		const FVector Normal(N);
		const FVector U = FMath::Abs(Normal.Z) > 0.5 ? FVector::ForwardVector : FVector::UpVector;
		const FVector V = FVector::CrossProduct(Normal, U);
		for (const FVector2D& O : Offsets)
		{
			const FVector OnFace = C + Normal * (B.VoxelSize * 0.5) + (U * O.X + V * O.Y) * B.VoxelSize;
			B.Aim(OnFace + (Normal * 1.0 + U * 0.2).GetSafeNormal() * 250.0, OnFace);
			FHitResult Hit;
			FIntVector Dig, Place;
			++Aims;
			if (!B.Character->FindEditTarget(Hit, Dig, Place))
			{
				AddError(FString::Printf(TEXT("Face %s offset %s: the trace found nothing"), *N.ToString(), *O.ToString()));
				continue;
			}
			// The edit path's own rule on the same hit.
			const FIntVector RuleDig = AVCCharacterBase::GetEditVoxelCoord(Hit.ImpactPoint, Hit.ImpactNormal, B.W.Config, false);
			const FIntVector RulePlace = AVCCharacterBase::GetEditVoxelCoord(Hit.ImpactPoint, Hit.ImpactNormal, B.W.Config, true);
			Matches += (Dig == Target && Place == Target + N && Dig == RuleDig && Place == RulePlace) ? 1 : 0;
			B.Block->TickLocal(1.0f / 60.0f);
			FBox Outline, Ghost;
			bool bBlocked = true;
			OutlineMatches += (B.Block->IsOutlineShown(Outline) && Outline.Equals(B.Cell(Target), 0.01)) ? 1 : 0;
			GhostMatches += (B.Block->IsGhostShown(Ghost, bBlocked) && Ghost.Equals(B.Cell(Target + N), 0.01) && !bBlocked) ? 1 : 0;
		}
	}
	AddInfo(FString::Printf(TEXT("%d aims at the six faces: %d targets as the edit rule gives them, %d outlines and %d ghosts on those cells"),
		Aims, Matches, OutlineMatches, GhostMatches));
	TestEqual(TEXT("Dig and place voxels are the edit rule's"), Matches, Aims);
	TestEqual(TEXT("The outline is the dig voxel's cell"), OutlineMatches, Aims);
	TestEqual(TEXT("The ghost is the place voxel's cell"), GhostMatches, Aims);

	// Aim at the top face for the rest.
	const FVector Top = C + FVector(0.0, 0.0, B.VoxelSize * 0.5);
	B.Aim(Top + FVector(-150.0, 0.0, 150.0), Top);
	FBox Box;
	bool bBlocked = false;

	// A pawn in the place cell: the ghost turns red, as the server would refuse it.
	const FTransform InCell(B.Centre(Target + FIntVector(0, 0, 1)) + FVector(0.0, 0.0, 60.0));
	AVCNPCCharacterBase* Pawn = B.W.World->SpawnActorDeferred<AVCNPCCharacterBase>(AVCNPCCharacterBase::StaticClass(), InCell, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	Pawn->bAIEnabled = false;
	Pawn->FinishSpawning(InCell);
	B.Block->TickLocal(1.0f / 60.0f);
	TestTrue(TEXT("A pawn in the place cell: the ghost shows, red"), B.Block->IsGhostShown(Box, bBlocked) && bBlocked);
	TestEqual(TEXT("... the server's own test"), AVCPlayerController::WouldPlaceTrapPawn(B.W.World, B.W.Config, Target + FIntVector(0, 0, 1)), true);
	Pawn->Destroy();
	B.Block->TickLocal(1.0f / 60.0f);
	TestTrue(TEXT("The pawn gone: white again"), B.Block->IsGhostShown(Box, bBlocked) && !bBlocked);

	// Edit mode off and no digging tool: neither (the primary action would not dig, the secondary not place).
	B.Character->SetEditModeEnabled(false);
	B.Block->TickLocal(1.0f / 60.0f);
	TestFalse(TEXT("Edit mode off: no outline"), B.Block->IsOutlineShown(Box));
	TestFalse(TEXT("Edit mode off: no ghost"), B.Block->IsGhostShown(Box, bBlocked));
	B.Character->SetEditModeEnabled(true);

	// vc.Dig.Highlight 0: nothing drawn, the targets are still found (a hold still breaks).
	{
		FCVarScope Off(TEXT("vc.Dig.Highlight"), TEXT("0"));
		B.Block->TickLocal(1.0f / 60.0f);
		TestFalse(TEXT("vc.Dig.Highlight 0: no outline"), B.Block->IsOutlineShown(Box));
		TestTrue(TEXT("vc.Dig.Highlight 0: the dig target is still there"), B.Block->GetTargets().bHasDig);
	}

	// Out of reach: the player 900 uu away, the view as close as before. Nothing, as the server refuses it.
	B.Character->SetActorLocation(C + FVector(-900.0, 0.0, 0.0));
	B.Block->TickLocal(1.0f / 60.0f);
	TestFalse(TEXT("Out of reach: no outline"), B.Block->IsOutlineShown(Box));
	TestFalse(TEXT("Out of reach: the server's reach agrees"), AVCPlayerController::IsVoxelInEditReach(B.Character->GetActorLocation(), Target, B.W.Config));
	B.Character->SetActorLocation(C + FVector(-300.0, 0.0, 0.0));

	// The block gone (dug elsewhere) while its collision lingers: no outline on air.
	B.SetVoxel(Target, EVoxelMaterial::Stone, false);
	B.Block->TickLocal(1.0f / 60.0f);
	TestFalse(TEXT("Air under the crosshair: no outline"), B.Block->IsOutlineShown(Box));
	B.Stop();

	// A smooth world: no block feedback at all.
	FBlockWorld S;
	if (TestTrue(TEXT("The smooth world streams"), S.Start(/*bCubic=*/false, TEXT("VCBlockHighlightSmooth"))))
	{
		const FIntVector Smooth = S.FloatingBlock(EVoxelMaterial::Stone);
		S.SpawnPlayer(S.Centre(Smooth) + FVector(-300.0, 0.0, 0.0));
		S.Character->SetEditModeEnabled(true);
		const FVector SmoothTop = S.Centre(Smooth) + FVector(0.0, 0.0, S.VoxelSize * 0.5);
		S.Aim(SmoothTop + FVector(-150.0, 0.0, 150.0), SmoothTop);
		S.Block->TickLocal(1.0f / 60.0f);
		TestFalse(TEXT("Smooth: no outline"), S.Block->IsOutlineShown(Box));
		TestFalse(TEXT("Smooth: no ghost"), S.Block->IsGhostShown(Box, bBlocked));
		TestFalse(TEXT("Smooth: no timed breaks"), UVCBlockInteractionComponent::UsesTimedBreaks(S.W.Config));
	}
	S.Stop();
	return true;
}

// ---------------------------------------------------------------------------
// WM-C15: the timed break, on the server path and through the client's hold.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCBlock_Break, "VoxelCharacter.Cubic.BlockBreak", VC_BLOCK_FLAGS)
bool FVCBlock_Break::RunTest(const FString& Parameters)
{
	using namespace VCBlockFeedbackTestHelpers;
	FCVarScope Times(TEXT("vc.Dig.BreakTimes"), TEXT("1"));
	FCVarScope Scale(TEXT("vc.Dig.BreakTimeScale"), TEXT("1"));
	FCVarScope PlayerInputs(TEXT("voxel.Edit.PlayerInputs"), TEXT("1"));
	FBlockWorld B;
	if (!TestTrue(TEXT("The cubic world streams"), B.Start(/*bCubic=*/true, TEXT("VCBlockBreak"))))
	{
		B.Stop();
		return false;
	}
	const FIntVector A = B.FloatingBlock(EVoxelMaterial::Stone, 4);
	const FIntVector Other = A + FIntVector(0, 2, 0);
	B.SetVoxel(Other, EVoxelMaterial::Dirt, true);
	B.Collider(Other);
	B.SpawnPlayer(B.Centre(A) + FVector(-300.0, 0.0, 0.0));
	B.Character->SetEditModeEnabled(true);
	UVCBlockBreakTestListener* Listener = NewObject<UVCBlockBreakTestListener>();
	Listener->AddToRoot();
	B.PC->OnVoxelGathered.AddDynamic(Listener, &UVCBlockBreakTestListener::HandleGathered);
	B.Block->OnBlockBroken.AddDynamic(Listener, &UVCBlockBreakTestListener::HandleBroken);
	const int32& Paid = Listener->Paid;
	const uint8& PaidMaterial = Listener->LastMaterial;
	int32& Broken = Listener->Broken;

	constexpr float Dt = 1.0f / 60.0f;
	const float StoneSeconds = UVCBlockInteractionComponent::GetBreakSeconds(EVoxelMaterial::Stone, B.Character->GetMiningSpeed());
	const int32 StoneFrames = FMath::CeilToInt(StoneSeconds / Dt);
	FIntVector Voxel;
	float Progress = 0.f;

	// The server path: the block stands until its time, then breaks once and pays the gather table.
	TestTrue(TEXT("Begin on a stone block in reach"), B.Block->BeginBreakOnServer(A));
	for (int32 Frame = 0; Frame < StoneFrames - 2; ++Frame)
	{
		B.Block->AdvanceServerBreak(Dt);
	}
	TestTrue(TEXT("Standing a frame or two before its time"), B.Voxel(A).IsSolid());
	TestTrue(TEXT("... the break still running"), B.Block->GetServerBreak(Voxel, Progress) && Voxel == A && Progress > 0.9f);
	for (int32 Frame = 0; Frame < 4; ++Frame)
	{
		B.Block->AdvanceServerBreak(Dt);
	}
	AddInfo(FString::Printf(TEXT("Stone: %.2f s bare-handed (%d frames); broken: %s; paid %d of material %d"), StoneSeconds, StoneFrames,
		B.Voxel(A).IsAir() ? TEXT("yes") : TEXT("no"), Paid, PaidMaterial));
	TestTrue(TEXT("Broken at its time"), B.Voxel(A).IsAir());
	TestEqual(TEXT("One block broken"), Broken, 1);
	TestEqual(TEXT("The gather table paid once, for stone"), Paid, 1);
	TestEqual(TEXT("... stone"), static_cast<int32>(PaidMaterial), static_cast<int32>(EVoxelMaterial::Stone));
	TestFalse(TEXT("No break running after it"), B.Block->GetServerBreak(Voxel, Progress));

	// A release resets: most of the time, a stop, a new begin needs the whole time again.
	B.SetVoxel(A, EVoxelMaterial::Stone, true);
	TestTrue(TEXT("Begin again"), B.Block->BeginBreakOnServer(A));
	for (int32 Frame = 0; Frame < StoneFrames * 9 / 10; ++Frame)
	{
		B.Block->AdvanceServerBreak(Dt);
	}
	B.Block->StopBreakOnServer();
	B.Block->BeginBreakOnServer(A);
	for (int32 Frame = 0; Frame < StoneFrames / 2; ++Frame)
	{
		B.Block->AdvanceServerBreak(Dt);
	}
	TestTrue(TEXT("Released at 90%: half the time later it still stands"), B.Voxel(A).IsSolid());

	// A retarget resets: the other block's break starts from nothing and the first block stands.
	TestTrue(TEXT("Retarget to the dirt block"), B.Block->BeginBreakOnServer(Other));
	const int32 DirtFrames = FMath::CeilToInt(UVCBlockInteractionComponent::GetBreakSeconds(EVoxelMaterial::Dirt, 1.f) / Dt);
	for (int32 Frame = 0; Frame < DirtFrames + 2; ++Frame)
	{
		B.Block->AdvanceServerBreak(Dt);
	}
	TestTrue(TEXT("The dirt block broke in dirt's time"), B.Voxel(Other).IsAir());
	TestTrue(TEXT("The first block (half broken, then left) stands"), B.Voxel(A).IsSolid());

	// Refusals: air, out of reach, a protected block.
	TestFalse(TEXT("Air is refused"), B.Block->BeginBreakOnServer(Other));
	B.Character->SetActorLocation(B.Centre(A) + FVector(-900.0, 0.0, 0.0));
	TestFalse(TEXT("Out of reach is refused"), B.Block->BeginBreakOnServer(A));
	B.Character->SetActorLocation(B.Centre(A) + FVector(-300.0, 0.0, 0.0));
	FProtectCell Protect;
	Protect.Cell = B.Cell(A);
	B.W.Manager->GetEditManager()->SetEditValidator(&Protect);
	TestFalse(TEXT("A protected block is refused at the start"), B.Block->BeginBreakOnServer(A));
	B.W.Manager->GetEditManager()->SetEditValidator(nullptr);
	// Walking out of reach mid-break cancels it.
	TestTrue(TEXT("Begin before walking away"), B.Block->BeginBreakOnServer(A));
	B.Block->AdvanceServerBreak(Dt);
	B.Character->SetActorLocation(B.Centre(A) + FVector(-900.0, 0.0, 0.0));
	B.Block->AdvanceServerBreak(Dt);
	TestFalse(TEXT("Out of reach mid-break: cancelled"), B.Block->GetServerBreak(Voxel, Progress));
	B.Character->SetActorLocation(B.Centre(A) + FVector(-300.0, 0.0, 0.0));

	// The instant request cannot skip the time where breaks are timed.
	AddExpectedMessage(TEXT("refused a dig"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	B.PC->Server_RequestVoxelModification(A, EVoxelModificationType::Destroy, 0);
	TestTrue(TEXT("Server_RequestVoxelModification(Destroy) breaks nothing in a timed world"), B.Voxel(A).IsSolid());
	{
		// vc.Dig.BreakTimes 0: the request digs at once, as before.
		FCVarScope Instant(TEXT("vc.Dig.BreakTimes"), TEXT("0"));
		B.PC->Server_RequestVoxelModification(A, EVoxelModificationType::Destroy, 0);
		TestTrue(TEXT("vc.Dig.BreakTimes 0: the request digs at once"), B.Voxel(A).IsAir());
	}

	// The client's hold, as the input drives it: press on the block, frames, the break, then on to the next block.
	B.SetVoxel(A, EVoxelMaterial::Stone, true);
	const FVector Top = B.Centre(A) + FVector(0.0, 0.0, B.VoxelSize * 0.5);
	B.Aim(Top + FVector(-150.0, 0.0, 150.0), Top);
	Broken = 0;
	B.Block->StartHold();
	TestTrue(TEXT("Hold: the server's break begins on the press"), B.Block->GetServerBreak(Voxel, Progress) && Voxel == A);
	for (int32 Frame = 0; Frame < StoneFrames / 2; ++Frame)
	{
		B.Frame(Dt);
	}
	float ClientProgress = 0.f;
	FIntVector ClientVoxel;
	TestTrue(TEXT("Hold: the client shows the break"), B.Block->GetClientBreak(ClientVoxel, ClientProgress) && ClientVoxel == A);
	B.Block->GetServerBreak(Voxel, Progress);
	AddInfo(FString::Printf(TEXT("Halfway: the client predicts %.3f, the server is at %.3f; %d crack lines drawn"), ClientProgress, Progress, B.Block->GetCrackSegmentCount()));
	TestTrue(TEXT("Hold: the prediction is within two frames of the server"), FMath::Abs(ClientProgress - Progress) <= 2.f * Dt / StoneSeconds + 1.e-4f);
	TestTrue(TEXT("Hold: cracks are drawn"), B.Block->GetCrackSegmentCount() > 0);
	FBox HoldBox;
	bool bHoldBlocked = false;
	TestTrue(TEXT("Hold: the outline stays on the block"), B.Block->IsOutlineShown(HoldBox) && HoldBox.Equals(B.Cell(A), 0.01));
	TestFalse(TEXT("Hold: the ghost steps aside while the break runs"), B.Block->IsGhostShown(HoldBox, bHoldBlocked));
	for (int32 Frame = 0; Frame < StoneFrames; ++Frame)
	{
		B.Frame(Dt);
	}
	TestTrue(TEXT("Hold: the block broke"), B.Voxel(A).IsAir() && Broken == 1);
	TestFalse(TEXT("Hold: its collision still there, nothing begins on the air"), B.Block->GetServerBreak(Voxel, Progress));

	// A release mid-break stops the server's break.
	B.SetVoxel(A, EVoxelMaterial::Stone, true);
	B.Block->StopHold();
	B.Block->StartHold();
	B.Frame(Dt);
	TestTrue(TEXT("Release: a break was running"), B.Block->GetServerBreak(Voxel, Progress));
	B.Block->StopHold();
	TestFalse(TEXT("Release: the server's break stops"), B.Block->GetServerBreak(Voxel, Progress));
	TestFalse(TEXT("Release: the client's too"), B.Block->GetClientBreak(ClientVoxel, ClientProgress));

	// A retarget while holding: the crosshair moves to the dirt block; the stone block's break is dropped.
	B.SetVoxel(Other, EVoxelMaterial::Dirt, true);
	B.Block->StartHold();
	for (int32 Frame = 0; Frame < 10; ++Frame)
	{
		B.Frame(Dt);
	}
	const FVector OtherTop = B.Centre(Other) + FVector(0.0, 0.0, B.VoxelSize * 0.5);
	B.Aim(OtherTop + FVector(-150.0, 0.0, 150.0), OtherTop);
	B.Frame(Dt);
	TestTrue(TEXT("Retarget: the server breaks the dirt block now, from the start"), B.Block->GetServerBreak(Voxel, Progress) && Voxel == Other && Progress <= Dt / 0.5f + 1.e-4f);
	B.Block->StopHold();
	TestTrue(TEXT("Retarget: the stone block stands"), B.Voxel(A).IsSolid());
	Listener->RemoveFromRoot();
	B.Stop();

	// A smooth world: the dig request dents at once, as before (no timed breaks).
	FBlockWorld S;
	if (TestTrue(TEXT("The smooth world streams"), S.Start(/*bCubic=*/false, TEXT("VCBlockBreakSmooth"))))
	{
		const float GroundTop = S.W.Manager->GetGeneratedSurfaceHeight(0.0, 0.0);
		const FIntVector Ground = FVoxelCoordinates::WorldToVoxel(FVector(0.0, 0.0, GroundTop - S.VoxelSize) - S.W.Config->WorldOrigin, S.VoxelSize);
		S.SpawnPlayer(S.Centre(Ground) + FVector(-300.0, 0.0, 150.0));
		const uint8 Before = S.Voxel(Ground).Density;
		S.PC->Server_RequestVoxelModification(Ground, EVoxelModificationType::Destroy, 0);
		AddInfo(FString::Printf(TEXT("Smooth: density %d -> %d at once"), Before, S.Voxel(Ground).Density));
		TestTrue(TEXT("Smooth: the dig dents at once"), S.Voxel(Ground).Density < Before);
		TestFalse(TEXT("Smooth: no break begins"), S.Block->BeginBreakOnServer(Ground));
	}
	S.Stop();
	return true;
}

#endif // WITH_AUTOMATION_TESTS
