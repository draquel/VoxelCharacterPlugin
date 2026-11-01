// Copyright Daniel Raquel. All Rights Reserved.

#include "Editing/VCBlockInteractionComponent.h"
#include "Core/VCCharacterBase.h"
#include "Core/VCPlayerController.h"
#include "Movement/VCVoxelNavigationHelper.h"
#include "VoxelCharacterPlugin.h"
#include "VoxelChunkManager.h"
#include "VoxelEditManager.h"
#include "VoxelMaterialRegistry.h"
#include "VoxelWorldConfiguration.h"
#include "Components/LineBatchComponent.h"
#include "HAL/IConsoleManager.h"
#include "Math/RandomStream.h"

static TAutoConsoleVariable<int32> CVarVCDigBreakTimes(
	TEXT("vc.Dig.BreakTimes"),
	1,
	TEXT("Cubic worlds: 1 = a block breaks after its material's hardness / MiningSpeed while the primary action is held (world modes review WM-C15); ")
	TEXT("0 = a press breaks it at once, as before."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarVCDigHighlight(
	TEXT("vc.Dig.Highlight"),
	1,
	TEXT("Cubic worlds: 1 = outline the block a dig would break, show a ghost of the block a place would add and draw a break's cracks (WM-C13); 0 = draw none."),
	ECVF_Default);

static TAutoConsoleVariable<float> CVarVCDigBreakTimeScale(
	TEXT("vc.Dig.BreakTimeScale"),
	1.f,
	TEXT("QA: multiply every block's break time (10 = ten times slower, to look at a break's cracks). The server and the client read the same value."),
	ECVF_Cheat);

namespace
{
	/** Who the edits are for: a block cell centred on its voxel's centre, VoxelSize wide. */
	FBox BlockCell(const UVoxelChunkManager* ChunkMgr, const UVoxelWorldConfiguration* Config, const FIntVector& VoxelCoord)
	{
		const FVector Centre = ChunkMgr->GlobalVoxelToWorldCentre(VoxelCoord);
		return FBox(Centre - FVector(Config->VoxelSize * 0.5f), Centre + FVector(Config->VoxelSize * 0.5f));
	}

	/** The voxel world the character is in, initialised, and its configuration. */
	UVoxelChunkManager* ResolveVoxelWorld(const AActor* Owner, const UVoxelWorldConfiguration*& OutConfig)
	{
		OutConfig = nullptr;
		UVoxelChunkManager* ChunkMgr = Owner ? UVoxelChunkManager::Find(Owner->GetWorld()) : nullptr;
		if (!ChunkMgr || !ChunkMgr->IsInitialized())
		{
			return nullptr;
		}
		OutConfig = ChunkMgr->GetConfiguration();
		return OutConfig ? ChunkMgr : nullptr;
	}
}

UVCBlockInteractionComponent::UVCBlockInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// After the camera has moved this frame, so the outline is where the view is now.
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetIsReplicatedByDefault(true);
}

// ---------------------------------------------------------------------------
// Rules
// ---------------------------------------------------------------------------

bool UVCBlockInteractionComponent::UsesTimedBreaks(const UVoxelWorldConfiguration* Config)
{
	return ShowsBlockFeedback(Config) && CVarVCDigBreakTimes.GetValueOnGameThread() != 0;
}

bool UVCBlockInteractionComponent::ShowsBlockFeedback(const UVoxelWorldConfiguration* Config)
{
	// The same test as GetEditVoxelCoord's block rule: one dig is one block only in a cubic world (D32).
	return Config && Config->MeshingMode == EMeshingMode::Cubic;
}

float UVCBlockInteractionComponent::GetBreakSeconds(uint8 MaterialId, float MiningSpeed)
{
	return FVoxelMaterialRegistry::GetHardness(MaterialId) / FMath::Max(MiningSpeed, 0.1f) * FMath::Max(CVarVCDigBreakTimeScale.GetValueOnGameThread(), 0.f);
}

uint32 UVCBlockInteractionComponent::HashVoxel(const FIntVector& VoxelCoord)
{
	return HashCombineFast(GetTypeHash(VoxelCoord), 0x9E3779B9u);
}

void UVCBlockInteractionComponent::BuildCrackLines(const FBox& Block, uint8 ExposedFaces, float Progress, uint32 Seed, float SurfaceOffset, TArray<FVector>& OutSegments)
{
	OutSegments.Reset();
	Progress = FMath::Clamp(Progress, 0.f, 1.f);
	if (Progress <= 0.f || !Block.IsValid)
	{
		return;
	}
	const FVector Centre = Block.GetCenter();
	const FVector Half = Block.GetExtent();
	constexpr int32 Branches = 5;
	constexpr int32 Steps = 4;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		if ((ExposedFaces & (1 << Face)) == 0)
		{
			continue;
		}
		// Faces +X, -X, +Y, -Y, +Z, -Z; U and V span the face.
		const int32 Axis = Face / 2;
		const int32 UAxis = (Axis + 1) % 3;
		const int32 VAxis = (Axis + 2) % 3;
		FVector Normal = FVector::ZeroVector;
		Normal[Axis] = (Face % 2 == 0) ? 1.0 : -1.0;
		FVector U = FVector::ZeroVector;
		U[UAxis] = 1.0;
		FVector V = FVector::ZeroVector;
		V[VAxis] = 1.0;
		const double HalfU = Half[UAxis] * 0.94;
		const double HalfV = Half[VAxis] * 0.94;
		const FVector FaceCentre = Centre + Normal * (Half[Axis] + SurfaceOffset);
		auto ToWorld = [&](const FVector2D& P) { return FaceCentre + U * P.X + V * P.Y; };

		// The whole pattern is drawn from the stream before any of it is clipped to the progress, so the cracks a break
		// shows only ever grow.
		FRandomStream Rand(static_cast<int32>(HashCombineFast(Seed, static_cast<uint32>(Face) * 0x85EBCA6Bu)));
		const FVector2D Origin(Rand.FRandRange(-0.2f, 0.2f) * HalfU, Rand.FRandRange(-0.2f, 0.2f) * HalfV);
		const double StepScale = FMath::Min(HalfU, HalfV);
		for (int32 Branch = 0; Branch < Branches; ++Branch)
		{
			double Angle = UE_TWO_PI * Branch / Branches + Rand.FRandRange(-0.45f, 0.45f);
			TArray<FVector2D, TInlineAllocator<Steps + 1>> Points;
			Points.Add(Origin);
			double Total = 0.0;
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				Angle += Rand.FRandRange(-0.6f, 0.6f);
				const double Length = Rand.FRandRange(0.22f, 0.34f) * StepScale;
				FVector2D Next = Points.Last() + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Length;
				Next.X = FMath::Clamp(Next.X, -HalfU, HalfU);
				Next.Y = FMath::Clamp(Next.Y, -HalfV, HalfV);
				Total += FVector2D::Distance(Points.Last(), Next);
				Points.Add(Next);
			}
			// Branch b shows from 60% * b / Branches of the progress and reaches its full length at the end.
			const float Start = 0.6f * static_cast<float>(Branch) / Branches;
			if (Progress <= Start || Total <= 0.0)
			{
				continue;
			}
			double Reveal = Total * (Progress - Start) / (1.f - Start);
			for (int32 Seg = 1; Seg < Points.Num() && Reveal > 0.0; ++Seg)
			{
				const double SegLength = FVector2D::Distance(Points[Seg - 1], Points[Seg]);
				if (SegLength <= 0.0)
				{
					continue;
				}
				const double Shown = FMath::Min(SegLength, Reveal);
				OutSegments.Add(ToWorld(Points[Seg - 1]));
				OutSegments.Add(ToWorld(Points[Seg - 1] + (Points[Seg] - Points[Seg - 1]) * (Shown / SegLength)));
				Reveal -= Shown;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Targets
// ---------------------------------------------------------------------------

AVCCharacterBase* UVCBlockInteractionComponent::GetCharacter() const
{
	return Cast<AVCCharacterBase>(GetOwner());
}

FVCBlockTargets UVCBlockInteractionComponent::ComputeTargets() const
{
	FVCBlockTargets Out;
	AVCCharacterBase* Character = GetCharacter();
	const UVoxelWorldConfiguration* Config = nullptr;
	UVoxelChunkManager* ChunkMgr = ResolveVoxelWorld(Character, Config);
	if (!ChunkMgr || !ShowsBlockFeedback(Config) || Character->IsIncapacitated())
	{
		return Out;
	}
	if (const AVCPlayerController* PC = Cast<AVCPlayerController>(Character->GetController()); PC && PC->IsInUIMode())
	{
		return Out;
	}
	FHitResult Hit;
	FIntVector DigVoxel, PlaceVoxel;
	if (!Character->FindEditTarget(Hit, DigVoxel, PlaceVoxel))
	{
		return Out;
	}
	const FVector PawnLocation = Character->GetActorLocation();
	// The primary action digs with edit mode on or a digging tool in hand (its gate), the secondary places in edit mode.
	if (Character->IsEditModeEnabled() || Character->HasDiggingTool())
	{
		const FVoxelData Voxel = ChunkMgr->GetVoxelAt(ChunkMgr->GlobalVoxelToWorldCentre(DigVoxel));
		if (Voxel.IsSolid() && AVCPlayerController::IsVoxelInEditReach(PawnLocation, DigVoxel, Config))
		{
			Out.bHasDig = true;
			Out.DigVoxel = DigVoxel;
			Out.DigMaterial = Voxel.MaterialID;
		}
	}
	if (Character->IsEditModeEnabled())
	{
		const FVoxelData Voxel = ChunkMgr->GetVoxelAt(ChunkMgr->GlobalVoxelToWorldCentre(PlaceVoxel));
		if (Voxel.IsAir() && AVCPlayerController::IsVoxelInEditReach(PawnLocation, PlaceVoxel, Config))
		{
			Out.bHasPlace = true;
			Out.PlaceVoxel = PlaceVoxel;
			Out.bPlaceBlocked = AVCPlayerController::WouldPlaceTrapPawn(Character->GetWorld(), Config, PlaceVoxel);
		}
	}
	return Out;
}

uint8 UVCBlockInteractionComponent::FindExposedFaces(const UVoxelChunkManager* ChunkMgr, const FIntVector& VoxelCoord)
{
	static const FIntVector Offsets[6] = { FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0),
		FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };
	uint8 Mask = 0;
	for (int32 Face = 0; Face < 6; ++Face)
	{
		if (ChunkMgr->GetVoxelAt(ChunkMgr->GlobalVoxelToWorldCentre(VoxelCoord + Offsets[Face])).IsAir())
		{
			Mask |= 1 << Face;
		}
	}
	return Mask;
}

// ---------------------------------------------------------------------------
// The owning client
// ---------------------------------------------------------------------------

void UVCBlockInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AVCCharacterBase* Character = GetCharacter();
	if (!Character)
	{
		return;
	}
	if (Character->HasAuthority())
	{
		AdvanceServerBreak(DeltaTime);
	}
	if (Character->IsLocallyControlled() && Character->IsPlayerControlled())
	{
		TickLocal(DeltaTime);
	}
}

void UVCBlockInteractionComponent::TickLocal(float DeltaSeconds)
{
	const UVoxelWorldConfiguration* Config = nullptr;
	const UVoxelChunkManager* ChunkMgr = ResolveVoxelWorld(GetOwner(), Config);
	Targets = ComputeTargets();
	if (bSuppressVoxel && (!Targets.bHasDig || Targets.DigVoxel != SuppressedVoxel))
	{
		bSuppressVoxel = false;
	}
	UpdateHold(DeltaSeconds);
	Draw(Config, ChunkMgr);
}

void UVCBlockInteractionComponent::StartHold()
{
	bHolding = true;
	// A press is a new attempt, also on a block whose last break was refused or has just ended.
	bSuppressVoxel = false;
	// Begin on the press itself: the input runs before this component's tick.
	Targets = ComputeTargets();
	UpdateHold(0.f);
}

void UVCBlockInteractionComponent::StopHold()
{
	bHolding = false;
	if (bClientBreaking)
	{
		SendStop();
	}
	bClientBreaking = false;
	ClientProgress = 0.f;
	bSuppressVoxel = false;
}

bool UVCBlockInteractionComponent::GetClientBreak(FIntVector& OutVoxel, float& OutProgress) const
{
	OutVoxel = ClientVoxel;
	OutProgress = bClientBreaking ? ClientProgress : 0.f;
	return bClientBreaking;
}

void UVCBlockInteractionComponent::UpdateHold(float DeltaSeconds)
{
	const UVoxelWorldConfiguration* Config = nullptr;
	const UVoxelChunkManager* ChunkMgr = ResolveVoxelWorld(GetOwner(), Config);
	const AVCCharacterBase* Character = GetCharacter();
	const bool bCanBreak = bHolding && Character && ChunkMgr && Targets.bHasDig && UsesTimedBreaks(Config)
		&& !(bSuppressVoxel && Targets.DigVoxel == SuppressedVoxel);
	if (!bCanBreak)
	{
		// Nothing breakable under the crosshair (or the button is up): the running break stops and its progress is lost.
		if (bClientBreaking)
		{
			SendStop();
			bClientBreaking = false;
			ClientProgress = 0.f;
		}
		return;
	}
	if (!bClientBreaking || Targets.DigVoxel != ClientVoxel)
	{
		// A new block under the crosshair (or the first): its break starts from nothing, on the server as well.
		bClientBreaking = true;
		ClientVoxel = Targets.DigVoxel;
		ClientMaterial = Targets.DigMaterial;
		ClientProgress = 0.f;
		DrawnExposedFaces = FindExposedFaces(ChunkMgr, ClientVoxel);
		SendBegin(ClientVoxel);
		return;
	}
	// The prediction: the server's own rule at the MiningSpeed this client sees. The block goes when the server says.
	const float Seconds = GetBreakSeconds(ClientMaterial, Character->GetMiningSpeed());
	ClientProgress = Seconds > 0.f ? FMath::Min(1.f, ClientProgress + DeltaSeconds / Seconds) : 1.f;
}

void UVCBlockInteractionComponent::SendBegin(const FIntVector& VoxelCoord)
{
	const AVCCharacterBase* Character = GetCharacter();
	if (Character && Character->HasAuthority())
	{
		BeginBreakOnServer(VoxelCoord);
	}
	else
	{
		Server_BeginBlockBreak(VoxelCoord);
	}
}

void UVCBlockInteractionComponent::SendStop()
{
	const AVCCharacterBase* Character = GetCharacter();
	if (Character && Character->HasAuthority())
	{
		StopBreakOnServer();
	}
	else
	{
		Server_StopBlockBreak();
	}
}

void UVCBlockInteractionComponent::Client_BlockBreakFinished_Implementation(FIntVector VoxelCoord, bool bBroken)
{
	if (bClientBreaking && VoxelCoord == ClientVoxel)
	{
		bClientBreaking = false;
		ClientProgress = 0.f;
	}
	// Not begun again on this block until the crosshair leaves it: a dug block is about to vanish (a remote client, which
	// gets no voxel edits, keeps drawing it), and a refused one would be refused again.
	bSuppressVoxel = true;
	SuppressedVoxel = VoxelCoord;
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("Block break at %s ended: %s"), *VoxelCoord.ToString(), bBroken ? TEXT("broken") : TEXT("refused or cancelled"));
}

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------

bool UVCBlockInteractionComponent::IsOutlineShown(FBox& OutBox) const
{
	OutBox = OutlineBox;
	return bOutlineShown;
}

bool UVCBlockInteractionComponent::IsGhostShown(FBox& OutBox, bool& bOutBlocked) const
{
	OutBox = GhostBox;
	bOutBlocked = bGhostBlockedShown;
	return bGhostShown;
}

void UVCBlockInteractionComponent::Draw(const UVoxelWorldConfiguration* Config, const UVoxelChunkManager* ChunkMgr)
{
	const bool bEnabled = ChunkMgr && ShowsBlockFeedback(Config) && CVarVCDigHighlight.GetValueOnGameThread() != 0;
	const bool bOutline = bEnabled && Targets.bHasDig;
	// The ghost steps aside while a break runs: the player is mining, and it would cover the cracks on the top face.
	const bool bBreaking = bOutline && bClientBreaking && ClientVoxel == Targets.DigVoxel;
	const bool bGhost = bEnabled && Targets.bHasPlace && !bBreaking;
	const FBox NewOutline = bOutline ? BlockCell(ChunkMgr, Config, Targets.DigVoxel) : FBox(ForceInit);
	const FBox NewGhost = bGhost ? BlockCell(ChunkMgr, Config, Targets.PlaceVoxel) : FBox(ForceInit);
	const float Progress = bBreaking ? ClientProgress : 0.f;
	if (bOutline == bOutlineShown && bGhost == bGhostShown && NewOutline == OutlineBox && NewGhost == GhostBox
		&& (!bGhost || Targets.bPlaceBlocked == bGhostBlockedShown) && Progress == DrawnProgress)
	{
		return;
	}
	bOutlineShown = bOutline;
	bGhostShown = bGhost;
	bGhostBlockedShown = bGhost && Targets.bPlaceBlocked;
	OutlineBox = NewOutline;
	GhostBox = NewGhost;
	DrawnProgress = Progress;
	CrackSegmentCount = 0;

	if (!Lines && (bOutline || bGhost))
	{
		// The character's own batcher, in world space, drawn only for this player (it exists only where they are local).
		Lines = NewObject<ULineBatchComponent>(GetOwner(), TEXT("BlockFeedbackLines"));
		Lines->SetUsingAbsoluteLocation(true);
		Lines->SetUsingAbsoluteRotation(true);
		Lines->SetUsingAbsoluteScale(true);
		Lines->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Lines->SetCastShadow(false);
		Lines->RegisterComponent();
	}
	if (!Lines)
	{
		return;
	}
	Lines->Flush();
	if (bOutline)
	{
		// Just off the block's faces, so the faces neither hide the lines nor fight them.
		Lines->DrawBox(OutlineBox.GetCenter(), OutlineBox.GetExtent() + FVector(0.75), OutlineColor, 0.f, SDPG_World, OutlineThickness);
		if (Progress > 0.f)
		{
			TArray<FVector> Segments;
			BuildCrackLines(OutlineBox, DrawnExposedFaces, Progress, HashVoxel(Targets.DigVoxel), 0.5f, Segments);
			TArray<FBatchedLine> Batch;
			Batch.Reserve(Segments.Num() / 2);
			for (int32 i = 0; i + 1 < Segments.Num(); i += 2)
			{
				Batch.Emplace(Segments[i], Segments[i + 1], CrackColor, 0.f, CrackThickness, SDPG_World);
			}
			Lines->DrawLines(Batch);
			CrackSegmentCount = Batch.Num();
		}
	}
	if (bGhost)
	{
		// A hair inside the cell, so its faces do not fight the terrain face it rests on.
		const FColor Fill = bGhostBlockedShown ? GhostBlockedColor : GhostColor;
		Lines->DrawSolidBox(GhostBox.ExpandBy(-1.0), FTransform::Identity, Fill, SDPG_World, 0.f);
		Lines->DrawBox(GhostBox.GetCenter(), GhostBox.GetExtent(), FLinearColor(Fill.WithAlpha(255)), 0.f, SDPG_World, 0.75f);
	}
}

void UVCBlockInteractionComponent::ClearDrawing()
{
	if (Lines)
	{
		Lines->Flush();
	}
	bOutlineShown = bGhostShown = bGhostBlockedShown = false;
	OutlineBox = GhostBox = FBox(ForceInit);
	DrawnProgress = -1.f;
	CrackSegmentCount = 0;
}

void UVCBlockInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearDrawing();
	if (Lines)
	{
		Lines->DestroyComponent();
		Lines = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------
// The server
// ---------------------------------------------------------------------------

void UVCBlockInteractionComponent::Server_BeginBlockBreak_Implementation(FIntVector VoxelCoord)
{
	BeginBreakOnServer(VoxelCoord);
}

void UVCBlockInteractionComponent::Server_StopBlockBreak_Implementation()
{
	StopBreakOnServer();
}

bool UVCBlockInteractionComponent::BeginBreakOnServer(const FIntVector& VoxelCoord)
{
	// A begin replaces the running break: a retarget starts the new block from nothing.
	bServerBreaking = false;
	ServerProgress = 0.f;
	AVCCharacterBase* Character = GetCharacter();
	AVCPlayerController* PC = Character ? Cast<AVCPlayerController>(Character->GetController()) : nullptr;
	const UVoxelWorldConfiguration* Config = nullptr;
	UVoxelChunkManager* ChunkMgr = ResolveVoxelWorld(Character, Config);
	auto Refuse = [this, &VoxelCoord](const TCHAR* Why)
	{
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("Block break at %s refused: %s"), *VoxelCoord.ToString(), Why);
		Client_BlockBreakFinished(VoxelCoord, false);
		return false;
	};
	if (!Character || !Character->HasAuthority() || !PC || !ChunkMgr || Character->IsIncapacitated() || !UsesTimedBreaks(Config))
	{
		return Refuse(TEXT("no timed break here"));
	}
	if (!AVCPlayerController::IsVoxelInEditReach(Character->GetActorLocation(), VoxelCoord, Config))
	{
		return Refuse(TEXT("out of reach"));
	}
	const FVector Centre = ChunkMgr->GlobalVoxelToWorldCentre(VoxelCoord);
	const FVoxelData Voxel = ChunkMgr->GetVoxelAt(Centre);
	if (!Voxel.IsSolid())
	{
		return Refuse(TEXT("not a solid block"));
	}
	// A protected block (a POI's claim) is refused now, not after its time has passed.
	const UVoxelEditManager* EditMgr = ChunkMgr->GetEditManager();
	if (!EditMgr || !EditMgr->CanEditAt(Centre, EEditSource::Player))
	{
		PC->Client_NotifyVoxelEditBlocked(1);
		return Refuse(TEXT("protected"));
	}
	bServerBreaking = true;
	ServerVoxel = VoxelCoord;
	ServerMaterial = Voxel.MaterialID;
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("Block break at %s began: material %d, %.2f s at MiningSpeed %.2f"), *VoxelCoord.ToString(),
		ServerMaterial, GetBreakSeconds(ServerMaterial, Character->GetMiningSpeed()), Character->GetMiningSpeed());
	return true;
}

void UVCBlockInteractionComponent::StopBreakOnServer()
{
	bServerBreaking = false;
	ServerProgress = 0.f;
}

bool UVCBlockInteractionComponent::GetServerBreak(FIntVector& OutVoxel, float& OutProgress) const
{
	OutVoxel = ServerVoxel;
	OutProgress = bServerBreaking ? ServerProgress : 0.f;
	return bServerBreaking;
}

void UVCBlockInteractionComponent::AdvanceServerBreak(float DeltaSeconds)
{
	if (!bServerBreaking)
	{
		return;
	}
	AVCCharacterBase* Character = GetCharacter();
	AVCPlayerController* PC = Character ? Cast<AVCPlayerController>(Character->GetController()) : nullptr;
	const UVoxelWorldConfiguration* Config = nullptr;
	UVoxelChunkManager* ChunkMgr = ResolveVoxelWorld(Character, Config);
	const FIntVector VoxelCoord = ServerVoxel;
	auto Cancel = [this, &VoxelCoord](const TCHAR* Why)
	{
		bServerBreaking = false;
		ServerProgress = 0.f;
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("Block break at %s cancelled: %s"), *VoxelCoord.ToString(), Why);
		Client_BlockBreakFinished(VoxelCoord, false);
	};
	if (!Character || !PC || !ChunkMgr || Character->IsIncapacitated() || !UsesTimedBreaks(Config))
	{
		Cancel(TEXT("no timed break here"));
		return;
	}
	if (!AVCPlayerController::IsVoxelInEditReach(Character->GetActorLocation(), VoxelCoord, Config))
	{
		Cancel(TEXT("out of reach"));
		return;
	}
	const FVoxelData Voxel = ChunkMgr->GetVoxelAt(ChunkMgr->GlobalVoxelToWorldCentre(VoxelCoord));
	if (!Voxel.IsSolid() || Voxel.MaterialID != ServerMaterial)
	{
		Cancel(TEXT("the block changed"));
		return;
	}
	// At the MiningSpeed of this moment: a tool equipped (or lost) mid-break counts from then on.
	const float Seconds = GetBreakSeconds(ServerMaterial, Character->GetMiningSpeed());
	ServerProgress = Seconds > 0.f ? FMath::Min(1.f, ServerProgress + DeltaSeconds / Seconds) : 1.f;
	if (ServerProgress < 1.f)
	{
		return;
	}
	bServerBreaking = false;
	ServerProgress = 0.f;
	const uint8 Material = ServerMaterial;
	// The instant dig's own path: one block, the gather table once per voxel (G3), edit protection, the edit layer.
	const bool bDug = PC->DigVoxelAt(VoxelCoord);
	UE_LOG(LogVoxelCharacter, Log, TEXT("Block break at %s (material %d): %s"), *VoxelCoord.ToString(), Material, bDug ? TEXT("broken") : TEXT("not dug"));
	if (bDug)
	{
		OnBlockBroken.Broadcast(VoxelCoord, Material);
	}
	Client_BlockBreakFinished(VoxelCoord, bDug);
}
