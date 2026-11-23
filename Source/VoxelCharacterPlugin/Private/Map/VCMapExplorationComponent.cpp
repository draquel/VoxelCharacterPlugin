// Copyright Daniel Raquel. All Rights Reserved.

#include "Map/VCMapExplorationComponent.h"
#include "Core/VCPlayerState.h"
#include "VoxelCharacterPlugin.h"
#include "VoxelMapExploredMask.h"
#include "VoxelMapSubsystem.h"
#include "VoxelEditReplicationComponent.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<float> CVarMapOverlayPushSeconds(
		TEXT("vc.Map.OverlayPushSeconds"),
		2.0f,
		TEXT("How often at most the server sends its map's edit overlays to a remote client, in seconds, when they changed and ")
		TEXT("the client's edits are in step with the server's (G273): a client's map shows the edits far from it only ")
		TEXT("through them. 0 = every tick of the component (a second)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarMapExploredPushSeconds(
		TEXT("vc.Map.ExploredPushSeconds"),
		5.0f,
		TEXT("How often at most a client sends its map's explored mask to the server for the save, in seconds, when it ")
		TEXT("has grown (G271). A player who disconnects loses at most this much exploration. 0 = every second."),
		ECVF_Default);

#if !UE_BUILD_SHIPPING
	/** vc.Map.Explored [X,Y,Z ...]: every play world's map against every player's place and the places given (G271's check). */
	void LogExploredPerWorld(const TArray<FString>& Args)
	{
		if (!GEngine)
		{
			return;
		}
		// The players' places from the authority's world, which holds every pawn (a client holds the ones near it).
		TArray<TPair<FString, FVector>> Players;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			const UWorld* World = Context.World();
			if (!World || (Context.WorldType != EWorldType::PIE && Context.WorldType != EWorldType::Game) || World->GetNetMode() == NM_Client)
			{
				continue;
			}
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				const APlayerController* PC = It->Get();
				const AVCPlayerState* PS = PC ? PC->GetPlayerState<AVCPlayerState>() : nullptr;
				if (PS && PC->GetPawn())
				{
					Players.Emplace(PS->GetSaveKey(), PC->GetPawn()->GetActorLocation());
				}
			}
			break;
		}
		int32 PlaceNumber = 0;
		for (const FString& Arg : Args)
		{
			TArray<FString> Parts;
			FVector Place;
			if (Arg.ParseIntoArray(Parts, TEXT(","), true) == 3)
			{
				Place = FVector(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
				Players.Emplace(FString::Printf(TEXT("place%d"), ++PlaceNumber), Place);
			}
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			const UWorld* World = Context.World();
			if (!World || (Context.WorldType != EWorldType::PIE && Context.WorldType != EWorldType::Game))
			{
				continue;
			}
			const UVoxelMapSubsystem* Map = World->GetSubsystem<UVoxelMapSubsystem>();
			if (!Map)
			{
				continue;
			}
			const ENetMode Mode = World->GetNetMode();
			FString Line = FString::Printf(TEXT("[MapExplored] world=%d net=%s explored=%lld overlays=%d |"), Context.PIEInstance,
				Mode == NM_Client ? TEXT("Client") : Mode == NM_DedicatedServer ? TEXT("DedicatedServer") : Mode == NM_ListenServer ? TEXT("ListenServer") : TEXT("Standalone"),
				Map->GetNumExploredTiles(), Map->GetEditOverlayCount());
			for (const TPair<FString, FVector>& Player : Players)
			{
				const FIntPoint Tile = Map->WorldToTileCoord(Player.Value);
				Line += FString::Printf(TEXT(" %s@%s=%d/%d"), *Player.Key, *Player.Value.ToCompactString(),
					Map->IsTileExplored(Tile) ? 1 : 0, Map->GetEditOverlay(Tile) ? 1 : 0);
			}
			UE_LOG(LogVoxelCharacter, Log, TEXT("%s"), *Line);
		}
	}

	FAutoConsoleCommand GVCMapExploredCmd(
		TEXT("vc.Map.Explored"),
		TEXT("Log every play world's map: its explored tiles and edit overlays, and for every player (from the server's world) ")
		TEXT("and every place given (X,Y,Z ...) whether the tile there is explored in that world and has an edit overlay, as ")
		TEXT("explored/overlay (G271, G272: each client's map holds its own player's ground and every edit's overlay)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&LogExploredPerWorld));
#endif
}

// ---------------------------------------------------------------------------
// FVCExploredBlobAssembler
// ---------------------------------------------------------------------------

bool FVCExploredBlobAssembler::AddPart(int32 Index, int32 Count, const FString& Part, int32 MaxChars, FString& OutBlob)
{
	if (Index == 0)
	{
		Reset();
		ExpectedCount = Count;
	}
	const bool bInOrder = Count > 0 && Count == ExpectedCount && Index == NextIndex && Index < Count;
	if (!bInOrder || Joined.Len() + Part.Len() > MaxChars)
	{
		Reset();
		return false;
	}
	Joined += Part;
	++NextIndex;
	if (NextIndex < ExpectedCount)
	{
		return false;
	}
	OutBlob = MoveTemp(Joined);
	Reset();
	return true;
}

void FVCExploredBlobAssembler::Reset()
{
	Joined.Reset();
	ExpectedCount = 0;
	NextIndex = 0;
}

// ---------------------------------------------------------------------------
// UVCMapExplorationComponent
// ---------------------------------------------------------------------------

UVCMapExplorationComponent::UVCMapExplorationComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickInterval = 1.0f;
}

TArray<FString> UVCMapExplorationComponent::SplitBlob(const FString& Blob)
{
	TArray<FString> Parts;
	for (int32 Start = 0; Start < Blob.Len(); Start += PartChars)
	{
		Parts.Add(Blob.Mid(Start, PartChars));
	}
	return Parts;
}

bool UVCMapExplorationComponent::ValidateBlob(const FString& Blob, int32& OutTileCount)
{
	OutTileCount = 0;
	if (Blob.IsEmpty() || Blob.Len() > MaxBlobChars)
	{
		return false;
	}
	FVoxelMapExploredMask Mask;
	if (!UVoxelMapSubsystem::DecodeExploredMask(Blob, Mask))
	{
		return false;
	}
	OutTileCount = static_cast<int32>(FMath::Min<int64>(Mask.Num(), MAX_int32));
	return true;
}

bool UVCMapExplorationComponent::IsRemoteOwningClient() const
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	const UWorld* World = GetWorld();
	return PC && World && World->GetNetMode() == NM_Client && PC->IsLocalController();
}

bool UVCMapExplorationComponent::IsServingRemoteClient() const
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	const UWorld* World = GetWorld();
	return PC && World && World->GetNetMode() != NM_Client && World->GetNetMode() != NM_Standalone && !PC->IsLocalController()
		&& PC->GetNetConnection() != nullptr;
}

void UVCMapExplorationComponent::PushOverlays(float DeltaTime)
{
	SecondsSinceOverlayPush += DeltaTime;
	if (SecondsSinceOverlayPush < CVarMapOverlayPushSeconds.GetValueOnGameThread())
	{
		return;
	}
	SecondsSinceOverlayPush = 0.f;
	// The client's edit layer first: a client reconciles the overlays with its own edits (a tile with none left is
	// cleared), so they go once its edits are the server's. Messages on the controller's channel arrive in order.
	const AActor* Owner = GetOwner();
	const UVoxelEditReplicationComponent* Edits = Owner ? Owner->FindComponentByClass<UVoxelEditReplicationComponent>() : nullptr;
	const UVoxelMapSubsystem* Map = GetWorld()->GetSubsystem<UVoxelMapSubsystem>();
	if (!Map || (Edits && !Edits->IsClientInStep()))
	{
		return;
	}
	int32 TileCount = 0;
	const FString Blob = Map->ExportEditOverlays(TileCount);
	if (LastSentOverlays.IsSet() ? LastSentOverlays.GetValue() == Blob : Blob.IsEmpty())
	{
		return;
	}
	if (Blob.Len() > MaxBlobChars)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("Map overlays: the server's edit overlays (%d chars, %d tiles) are over the %d a client takes; not sent."),
			Blob.Len(), TileCount, MaxBlobChars);
		return;
	}
	LastSentOverlays = Blob;
	TArray<FString> Parts = SplitBlob(Blob);
	if (Parts.Num() == 0)
	{
		Parts.Add(FString()); // no overlay left: the client's import clears its own, then rebuilds what its loaded chunks hold
	}
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		Client_ReceiveOverlayPart(Index, Parts.Num(), Parts[Index]);
	}
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("Map overlays: sent %d tile(s) to %s (%d chars in %d part(s))."), TileCount, *GetNameSafe(Owner),
		Blob.Len(), Parts.Num());
}

void UVCMapExplorationComponent::Client_ReceiveOverlayPart_Implementation(int32 Index, int32 Count, const FString& Part)
{
	FString Blob;
	if (!OverlayAssembler.AddPart(Index, Count, Part, MaxBlobChars, Blob))
	{
		return;
	}
	UVoxelMapSubsystem* Map = GetWorld() ? GetWorld()->GetSubsystem<UVoxelMapSubsystem>() : nullptr;
	if (!Map)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("Map overlays: the server's edit overlays arrived in a world with no map; not kept."));
		return;
	}
	const int32 Tiles = Map->ImportEditOverlays(Blob);
	UE_LOG(LogVoxelCharacter, Log, TEXT("Map overlays: took the server's edit overlays (%d tile(s))."), Tiles);
}

void UVCMapExplorationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (IsServingRemoteClient())
	{
		PushOverlays(DeltaTime);
		return;
	}
	if (!IsRemoteOwningClient())
	{
		return;
	}
	SecondsSincePush += DeltaTime;
	const UVoxelMapSubsystem* Map = GetWorld()->GetSubsystem<UVoxelMapSubsystem>();
	if (!Map || Map->GetExplorationRevision() == LastPushedRevision
		|| SecondsSincePush < CVarMapExploredPushSeconds.GetValueOnGameThread())
	{
		return;
	}
	LastPushedRevision = Map->GetExplorationRevision();
	SecondsSincePush = 0.f;
	int32 TileCount = 0;
	const FString Blob = Map->ExportExploredTiles(TileCount);
	if (Blob.IsEmpty())
	{
		return;
	}
	if (Blob.Len() > MaxBlobChars)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("Map exploration: the explored mask (%d chars, %d tiles) is over the %d the server takes; not sent."),
			Blob.Len(), TileCount, MaxBlobChars);
		return;
	}
	const TArray<FString> Parts = SplitBlob(Blob);
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		Server_ReportExploredPart(Index, Parts.Num(), Parts[Index]);
	}
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("Map exploration: sent %d tile(s) to the server (%d chars in %d part(s))."), TileCount, Blob.Len(), Parts.Num());
}

void UVCMapExplorationComponent::Server_ReportExploredPart_Implementation(int32 Index, int32 Count, const FString& Part)
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	AVCPlayerState* PS = PC ? PC->GetPlayerState<AVCPlayerState>() : nullptr;
	FString Blob;
	if (!PS || !Assembler.AddPart(Index, Count, Part, MaxBlobChars, Blob))
	{
		return;
	}
	int32 TileCount = 0;
	if (!ValidateBlob(Blob, TileCount))
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("Map exploration: %s sent an explored mask that is not one (%d chars); kept the last one (%d tiles)."),
			*PS->GetSaveKey(), Blob.Len(), PS->GetMapExploredTileCount());
		return;
	}
	PS->SetMapExplored(Blob, TileCount);
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("Map exploration: %s reported %d tile(s)."), *PS->GetSaveKey(), TileCount);
}

void UVCMapExplorationComponent::SendToOwningClient(const FString& Blob, int32 TileCount)
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->HasAuthority() || PC->IsLocalController() || Blob.IsEmpty())
	{
		return;
	}
	const TArray<FString> Parts = SplitBlob(Blob);
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		Client_RestoreExploredPart(Index, Parts.Num(), Parts[Index], TileCount);
	}
	UE_LOG(LogVoxelCharacter, Log, TEXT("Map exploration: sent the saved %d tile(s) to %s (%d part(s))."), TileCount, *GetNameSafe(PC), Parts.Num());
}

void UVCMapExplorationComponent::Client_RestoreExploredPart_Implementation(int32 Index, int32 Count, const FString& Part, int32 TileCount)
{
	FString Blob;
	if (!Assembler.AddPart(Index, Count, Part, MaxBlobChars, Blob))
	{
		return;
	}
	UVoxelMapSubsystem* Map = GetWorld() ? GetWorld()->GetSubsystem<UVoxelMapSubsystem>() : nullptr;
	if (!Map)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("Map exploration: the saved explored mask arrived in a world with no map; not restored."));
		return;
	}
	const int32 Held = Map->ImportExploredTiles(Blob, TileCount);
	UE_LOG(LogVoxelCharacter, Log, TEXT("Map exploration: restored the saved explored mask from the server (%d tile(s))."), Held);
}
