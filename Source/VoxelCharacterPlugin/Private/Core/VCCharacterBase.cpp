// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCCharacterBase.h"
#include "Core/VCPlayerState.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Core/VCPlayerController.h"
#include "Camera/VCCameraManager.h"
#include "Camera/VCFirstPersonCameraMode.h"
#include "Camera/VCThirdPersonCameraMode.h"
#include "Movement/VCMovementComponent.h"
#include "Movement/VCVoxelNavigationHelper.h"
#include "Input/VCInputConfig.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "AbilitySystemComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Net/UnrealNetwork.h"
#include "VoxelCoordinates.h"
#include "VoxelWorldConfiguration.h"
#include "VoxelChunkManager.h"
#include "VoxelCollisionManager.h"
#include "VoxelCharacterPlugin.h"
#include "Engine/Engine.h"

#if WITH_INTERACTION_PLUGIN
#include "Components/InteractionComponent.h"
#include "Components/InteractableComponent.h"
#include "Detection/SphereOverlapDetection.h"
#include "Subsystems/WorldItemPoolSubsystem.h"
#include "Actors/WorldItem.h"
#include "Tags/CGFGameplayTags.h"
#endif

#if WITH_EQUIPMENT_PLUGIN
#include "Components/EquipmentManagerComponent.h"
#include "Types/EquipmentSystemTypes.h"
#endif

#if WITH_INVENTORY_PLUGIN
#include "Components/InventoryComponent.h"
#endif

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

AVCCharacterBase::AVCCharacterBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UVCMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// --- Camera Component ---
	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComponent"));
	CameraComponent->SetupAttachment(RootComponent);
	CameraComponent->bUsePawnControlRotation = false; // Driven by CameraManager

	// --- Camera Manager ---
	CameraManager = CreateDefaultSubobject<UVCCameraManager>(TEXT("CameraManager"));
	CameraManager->SetCameraComponent(CameraComponent);
	CameraManager->FirstPersonModeClass = UVCFirstPersonCameraMode::StaticClass();
	CameraManager->ThirdPersonModeClass = UVCThirdPersonCameraMode::StaticClass();

	// --- First Person Arms Mesh ---
	FirstPersonArmsMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonArmsMesh"));
	FirstPersonArmsMesh->SetupAttachment(CameraComponent);
	FirstPersonArmsMesh->SetOnlyOwnerSee(true);
	FirstPersonArmsMesh->bCastDynamicShadow = false;
	FirstPersonArmsMesh->CastShadow = false;
	FirstPersonArmsMesh->SetVisibility(false);

	// --- Integration Components ---

#if WITH_INTERACTION_PLUGIN
	InteractionComponent = CreateDefaultSubobject<UInteractionComponent>(TEXT("InteractionComponent"));
	InteractionComponent->DetectionStrategy = CreateDefaultSubobject<USphereOverlapDetection>(TEXT("DefaultDetectionStrategy"));
	InteractionComponent->InteractionRange = 400.f;
#endif

#if WITH_EQUIPMENT_PLUGIN
	EquipmentManager = CreateDefaultSubobject<UEquipmentManagerComponent>(TEXT("EquipmentManager"));
	{
		FEquipmentSlotDefinition MainHand;
		MainHand.SlotTag = FGameplayTag::RequestGameplayTag(FName("Equipment.Slot.MainHand"));
		MainHand.SlotDisplayName = NSLOCTEXT("VoxelCharacter", "MainHand", "Main Hand");
		MainHand.AttachSocket = FName("hand_r");
		MainHand.AcceptedItemTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Item.Category.Weapon")));
		EquipmentManager->AvailableSlots.Add(MainHand);

		FEquipmentSlotDefinition OffHand;
		OffHand.SlotTag = FGameplayTag::RequestGameplayTag(FName("Equipment.Slot.OffHand"));
		OffHand.SlotDisplayName = NSLOCTEXT("VoxelCharacter", "OffHand", "Off Hand");
		OffHand.AttachSocket = FName("hand_l");
		OffHand.AcceptedItemTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Item.Category.Weapon")));
		EquipmentManager->AvailableSlots.Add(OffHand);

		FEquipmentSlotDefinition Head;
		Head.SlotTag = FGameplayTag::RequestGameplayTag(FName("Equipment.Slot.Head"));
		Head.SlotDisplayName = NSLOCTEXT("VoxelCharacter", "Head", "Head");
		Head.AttachSocket = FName("head");
		Head.AcceptedItemTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Item.Category.Armor")));
		EquipmentManager->AvailableSlots.Add(Head);

		FEquipmentSlotDefinition Chest;
		Chest.SlotTag = FGameplayTag::RequestGameplayTag(FName("Equipment.Slot.Chest"));
		Chest.SlotDisplayName = NSLOCTEXT("VoxelCharacter", "Chest", "Chest");
		Chest.AttachSocket = FName("spine_03");
		Chest.AcceptedItemTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Item.Category.Armor")));
		EquipmentManager->AvailableSlots.Add(Chest);
	}
#endif

#if WITH_INVENTORY_PLUGIN
	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("InventoryComponent"));
#endif

	// --- Body mesh defaults ---
	GetMesh()->SetOwnerNoSee(false);

	// --- Character defaults ---
	bUseControllerRotationYaw = false;

	// --- Replication ---
	bReplicates = true;
}

// ---------------------------------------------------------------------------
// IAbilitySystemInterface
// ---------------------------------------------------------------------------

UAbilitySystemComponent* AVCCharacterBase::GetAbilitySystemComponent() const
{
	if (const AVCPlayerState* PS = GetPlayerState<AVCPlayerState>())
	{
		return PS->GetAbilitySystemComponent();
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// ICGFInventoryInterface
// ---------------------------------------------------------------------------

UActorComponent* AVCCharacterBase::GetInventoryComponent_Implementation() const
{
#if WITH_INVENTORY_PLUGIN
	return InventoryComponent;
#else
	return nullptr;
#endif
}

TArray<UActorComponent*> AVCCharacterBase::GetInventoryComponents_Implementation() const
{
	TArray<UActorComponent*> Result;
#if WITH_INVENTORY_PLUGIN
	if (InventoryComponent)
	{
		Result.Add(InventoryComponent);
	}
#endif
	return Result;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void AVCCharacterBase::BeginPlay()
{
	Super::BeginPlay();
	UpdateMeshVisibility();

	// --- Bind integration delegates ---

#if WITH_INTERACTION_PLUGIN
	if (InteractionComponent)
	{
		InteractionComponent->OnInteractableFound.AddDynamic(this, &AVCCharacterBase::HandleInteractableFound);
		InteractionComponent->OnInteractableLost.AddDynamic(this, &AVCCharacterBase::HandleInteractableLost);
	}
#endif

#if WITH_EQUIPMENT_PLUGIN
	if (EquipmentManager)
	{
		EquipmentManager->OnItemEquipped.AddDynamic(this, &AVCCharacterBase::HandleItemEquipped);
		EquipmentManager->OnItemUnequipped.AddDynamic(this, &AVCCharacterBase::HandleItemUnequipped);
	}
#endif

	// --- Terrain Ready Spawn ---
	if (bWaitForTerrain)
	{
		FreezeForTerrainWait();
		InitiateChunkBasedWait();
	}
}

void AVCCharacterBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Unbind from collision manager delegate
	if (UVoxelCollisionManager* ColMgr = CachedCollisionManager.Get())
	{
		if (CollisionReadyDelegateHandle.IsValid())
		{
			ColMgr->OnCollisionReady.Remove(CollisionReadyDelegateHandle);
			CollisionReadyDelegateHandle.Reset();
		}
	}
	CachedCollisionManager.Reset();
	PendingTerrainChunks.Empty();

	Super::EndPlay(EndPlayReason);
}

void AVCCharacterBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// --- Terrain Ready Spawn ---
	// Primary: OnChunkCollisionReady delegate. Fallback: periodic poll + timeout.
	if (bIsWaitingForTerrain)
	{
		TerrainWaitElapsed += DeltaSeconds;

		// Periodic poll every 2s: re-check HasCollision and re-request dropped chunks
		if (PendingTerrainChunks.Num() > 0 && FMath::Fmod(TerrainWaitElapsed, 2.0f) < DeltaSeconds)
		{
			if (UVoxelCollisionManager* ColMgr = CachedCollisionManager.Get())
			{
				TArray<FIntVector> NowReady;
				for (const FIntVector& Coord : PendingTerrainChunks)
				{
					if (ColMgr->HasCollision(Coord))
					{
						NowReady.Add(Coord);
					}
					else
					{
						// Re-request in case the previous request was dropped (chunk data wasn't ready)
						ColMgr->RequestCollision(Coord, 2000.f);
					}
				}
				for (const FIntVector& Coord : NowReady)
				{
					PendingTerrainChunks.Remove(Coord);
					UE_LOG(LogVoxelCharacter, Log,
						TEXT("Terrain poll: Chunk (%d,%d,%d) ready — %d remaining"),
						Coord.X, Coord.Y, Coord.Z, PendingTerrainChunks.Num());
				}
				if (PendingTerrainChunks.Num() == 0)
				{
					UE_LOG(LogVoxelCharacter, Log, TEXT("All terrain chunks ready (via poll) — placing character."));
					PlaceOnTerrainAndResume();
					return;
				}

				// "Good enough" placement: if most chunks ready and we've waited a while,
				// proceed even if some edge chunks fail. Center chunk is the most important.
				const int32 TotalChunks = (2 * TerrainWaitChunkRadius + 1) * (2 * TerrainWaitChunkRadius + 1);
				const int32 ReadyChunks = TotalChunks - PendingTerrainChunks.Num();
				if (TerrainWaitElapsed > 10.f && ReadyChunks >= FMath::CeilToInt(TotalChunks * 0.75f))
				{
					UE_LOG(LogVoxelCharacter, Log,
						TEXT("Terrain mostly ready (%d/%d chunks) after %.1fs — placing character."),
						ReadyChunks, TotalChunks, TerrainWaitElapsed);
					PendingTerrainChunks.Empty();
					PlaceOnTerrainAndResume();
					return;
				}
			}
		}

		if (TerrainWaitElapsed >= TerrainWaitTimeout)
		{
			UE_LOG(LogVoxelCharacter, Warning,
				TEXT("Terrain wait timeout (%.1fs) — %d/%d chunks still pending. Force-placing."),
				TerrainWaitTimeout, PendingTerrainChunks.Num(),
				(2 * TerrainWaitChunkRadius + 1) * (2 * TerrainWaitChunkRadius + 1));
			PendingTerrainChunks.Empty();
			PlaceOnTerrainAndResume();
		}
		return; // Skip camera/debug updates while frozen
	}

	if (CameraManager)
	{
		CameraManager->UpdateCamera(DeltaSeconds);

		// Deferred FP mesh hide: wait until camera blend is nearly complete
		// so the player sees the camera zoom in on the character before it vanishes.
		if (bPendingFPMeshHide && CameraManager->GetTopModeBlendWeight() >= 0.9f)
		{
			bPendingFPMeshHide = false;
			UpdateMeshVisibility();
			// Restore default near clip plane now that the body mesh is hidden
			GNearClippingPlane = 10.f;
		}
	}

	if (bShowVoxelDebug && IsLocallyControlled())
	{
		DrawVoxelDebugInfo();
	}
}

// ---------------------------------------------------------------------------
// Replication
// ---------------------------------------------------------------------------

void AVCCharacterBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVCCharacterBase, CurrentViewMode);
}

// ---------------------------------------------------------------------------
// Possession / ASC Initialization
// ---------------------------------------------------------------------------

void AVCCharacterBase::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// Server: initialize ASC with this character as the avatar
	if (AVCPlayerState* PS = GetPlayerState<AVCPlayerState>())
	{
		UAbilitySystemComponent* ASC = PS->GetAbilitySystemComponent();
		if (ASC)
		{
			ASC->InitAbilityActorInfo(PS, this);

			if (!PS->bAbilitiesGranted)
			{
				GrantDefaultAbilities(ASC);
				PS->bAbilitiesGranted = true;
			}

			BindAttributeChangeDelegates(ASC);
		}
	}
}

void AVCCharacterBase::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	// Client: initialize ASC with this character as the avatar
	if (AVCPlayerState* PS = GetPlayerState<AVCPlayerState>())
	{
		if (UAbilitySystemComponent* ASC = PS->GetAbilitySystemComponent())
		{
			ASC->InitAbilityActorInfo(PS, this);
			BindAttributeChangeDelegates(ASC);
		}
	}
}

void AVCCharacterBase::BindAttributeChangeDelegates(UAbilitySystemComponent* ASC)
{
	if (!ASC)
	{
		return;
	}

	// Movement component listens to speed attribute
	if (UVCMovementComponent* MovComp = Cast<UVCMovementComponent>(GetCharacterMovement()))
	{
		ASC->GetGameplayAttributeValueChangeDelegate(
			UVCCharacterAttributeSet::GetMoveSpeedMultiplierAttribute()
		).AddUObject(MovComp, &UVCMovementComponent::OnMoveSpeedAttributeChanged);
	}
}

void AVCCharacterBase::GrantDefaultAbilities(UAbilitySystemComponent* ASC)
{
	if (!ASC)
	{
		return;
	}

	const AVCPlayerState* PS = GetPlayerState<AVCPlayerState>();
	if (!PS)
	{
		return;
	}

	for (const TSubclassOf<UGameplayAbility>& AbilityClass : PS->DefaultAbilities)
	{
		if (AbilityClass)
		{
			ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
		}
	}
}

// ---------------------------------------------------------------------------
// View Mode
// ---------------------------------------------------------------------------

void AVCCharacterBase::SetViewMode(EVCViewMode NewMode)
{
	if (CurrentViewMode == NewMode)
	{
		return;
	}

	const EVCViewMode OldMode = CurrentViewMode;
	CurrentViewMode = NewMode;

	// 1. Camera transition
	if (CameraManager)
	{
		if (NewMode == EVCViewMode::FirstPerson)
		{
			CameraManager->PushCameraMode(CameraManager->FirstPersonModeClass);
		}
		else
		{
			CameraManager->PushCameraMode(CameraManager->ThirdPersonModeClass);
		}
	}

	// 2. Rotation behavior
	if (UCharacterMovementComponent* MovComp = GetCharacterMovement())
	{
		if (NewMode == EVCViewMode::FirstPerson)
		{
			// FP: character yaw locked to camera
			bUseControllerRotationYaw = true;
			MovComp->bOrientRotationToMovement = false;
		}
		else
		{
			// TP: character faces movement direction
			bUseControllerRotationYaw = false;
			MovComp->bOrientRotationToMovement = true;
		}
	}

	// 3. Mesh visibility — defer hide when entering FP so the camera
	//    blend can "zoom in" on the character before hiding the body.
	if (NewMode == EVCViewMode::FirstPerson && IsLocallyControlled())
	{
		bPendingFPMeshHide = true;
		// Temporarily shrink near clip plane so the mesh doesn't get clipped
		// as the camera zooms through it during the blend.
		GNearClippingPlane = 1.f;
	}
	else
	{
		bPendingFPMeshHide = false;
		UpdateMeshVisibility();
	}

	// 4. Equipment re-attachment (FP arms vs TP body)
	UpdateEquipmentAttachments();

	// 4. Interaction scanner range adjustment
#if WITH_INTERACTION_PLUGIN
	if (InteractionComponent)
	{
		InteractionComponent->InteractionRange = GetInteractionRange();
	}
#endif

	// 5. Broadcast
	OnViewModeChanged.Broadcast(OldMode, NewMode);

	if (HasAuthority())
	{
		OnRep_ViewMode();
	}
}

void AVCCharacterBase::OnRep_ViewMode()
{
	// Skip immediate mesh hide if we're deferring it for the FP camera blend
	if (!bPendingFPMeshHide)
	{
		UpdateMeshVisibility();
	}
	UpdateEquipmentAttachments();
}

void AVCCharacterBase::UpdateMeshVisibility()
{
	const bool bIsLocalFP = IsLocallyControlled() && CurrentViewMode == EVCViewMode::FirstPerson;

	if (GetMesh())
	{
		GetMesh()->SetOwnerNoSee(bIsLocalFP);
		GetMesh()->bCastHiddenShadow = bIsLocalFP;
	}

	if (FirstPersonArmsMesh)
	{
		FirstPersonArmsMesh->SetVisibility(bIsLocalFP);
	}
}

// ---------------------------------------------------------------------------
// Terrain Ready Spawn
// ---------------------------------------------------------------------------

void AVCCharacterBase::FreezeForTerrainWait()
{
	GetCharacterMovement()->DisableMovement();
	SetActorEnableCollision(false);
	bIsWaitingForTerrain = true;
	TerrainWaitElapsed = 0.f;
	UE_LOG(LogVoxelCharacter, Log, TEXT("Waiting for terrain collision before placing character..."));
}

void AVCCharacterBase::InitiateChunkBasedWait()
{
	// Find the collision manager via the chunk manager
	UVoxelChunkManager* ChunkMgr = FVCVoxelNavigationHelper::FindChunkManager(GetWorld());
	if (!ChunkMgr)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("InitiateChunkBasedWait: No VoxelChunkManager found — placing immediately."));
		PlaceOnTerrainAndResume();
		return;
	}

	UVoxelCollisionManager* ColMgr = ChunkMgr->GetCollisionManager();
	if (!ColMgr)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("InitiateChunkBasedWait: No CollisionManager — placing immediately."));
		PlaceOnTerrainAndResume();
		return;
	}

	const UVoxelWorldConfiguration* Config = ChunkMgr->GetConfiguration();
	if (!Config)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("InitiateChunkBasedWait: No VoxelWorldConfiguration — placing immediately."));
		PlaceOnTerrainAndResume();
		return;
	}

	CachedCollisionManager = ColMgr;

	// Relocate to valid terrain if current position is over water or invalid.
	// Place at terrain surface height so chunk Z calculation is correct.
	// Movement/collision are disabled during wait, so the character won't fall.
	// PlaceOnTerrainAndResume() raycast (±50000u) handles precise final placement.
	FVector ValidSpawn;
	if (FVCVoxelNavigationHelper::FindSpawnablePosition(GetWorld(), GetActorLocation(), ValidSpawn))
	{
		SetActorLocation(ValidSpawn);
	}

	UE_LOG(LogVoxelCharacter, Log,
		TEXT("InitiateChunkBasedWait: Bound to CollisionManager %p in world '%s' (PIE=%d)"),
		ColMgr, *GetWorld()->GetName(), GetWorld()->IsPlayInEditor());

	// Convert character world position to chunk coordinate
	const FVector CharPos = GetActorLocation();
	const FVector RelPos = CharPos - Config->WorldOrigin;
	const FIntVector CenterChunk = FVoxelCoordinates::WorldToChunk(RelPos, Config->ChunkSize, Config->VoxelSize);

	// Build the grid of chunks we need to wait for (radius on X/Y, center chunk Z only)
	PendingTerrainChunks.Empty();
	for (int32 DX = -TerrainWaitChunkRadius; DX <= TerrainWaitChunkRadius; ++DX)
	{
		for (int32 DY = -TerrainWaitChunkRadius; DY <= TerrainWaitChunkRadius; ++DY)
		{
			const FIntVector ChunkCoord(CenterChunk.X + DX, CenterChunk.Y + DY, CenterChunk.Z);

			if (ColMgr->HasCollision(ChunkCoord))
			{
				// Already ready — skip
				continue;
			}

			PendingTerrainChunks.Add(ChunkCoord);

			// Request collision with high priority so it's processed ASAP
			ColMgr->RequestCollision(ChunkCoord, 2000.f);
		}
	}

	UE_LOG(LogVoxelCharacter, Log,
		TEXT("InitiateChunkBasedWait: Center chunk (%d,%d,%d), waiting for %d chunks in %dx%d grid"),
		CenterChunk.X, CenterChunk.Y, CenterChunk.Z,
		PendingTerrainChunks.Num(),
		2 * TerrainWaitChunkRadius + 1, 2 * TerrainWaitChunkRadius + 1);

	if (PendingTerrainChunks.Num() == 0)
	{
		// All chunks already have collision — place immediately
		PlaceOnTerrainAndResume();
		return;
	}

	// Bind to the collision ready delegate
	CollisionReadyDelegateHandle = ColMgr->OnCollisionReady.AddUObject(
		this, &AVCCharacterBase::OnChunkCollisionReady);
}

void AVCCharacterBase::OnChunkCollisionReady(const FIntVector& ChunkCoord)
{
	if (!bIsWaitingForTerrain)
	{
		return;
	}

	const int32 Removed = PendingTerrainChunks.Remove(ChunkCoord);
	if (Removed > 0)
	{
		UE_LOG(LogVoxelCharacter, Log,
			TEXT("OnChunkCollisionReady: Chunk (%d,%d,%d) ready — %d remaining"),
			ChunkCoord.X, ChunkCoord.Y, ChunkCoord.Z, PendingTerrainChunks.Num());
	}

	if (PendingTerrainChunks.Num() == 0)
	{
		// All required chunks are ready — place character
		UE_LOG(LogVoxelCharacter, Log, TEXT("All terrain chunks ready — placing character."));

		// Unbind delegate now that we're done waiting
		if (UVoxelCollisionManager* ColMgr = CachedCollisionManager.Get())
		{
			ColMgr->OnCollisionReady.Remove(CollisionReadyDelegateHandle);
			CollisionReadyDelegateHandle.Reset();
		}

		PlaceOnTerrainAndResume();
	}
}

void AVCCharacterBase::PlaceOnTerrainAndResume()
{
	// Re-enable collision on the actor and explicitly on the capsule
	SetActorEnableCollision(true);
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	// Sphere sweep downward to find terrain surface (more robust than line trace at a single point)
	const FVector SpawnPos = GetActorLocation();
	const FVector TraceStart(SpawnPos.X, SpawnPos.Y, SpawnPos.Z + 50000.f);
	const FVector TraceEnd(SpawnPos.X, SpawnPos.Y, SpawnPos.Z - 50000.f);
	const float SweepRadius = 50.f; // Small sphere to avoid exact-point misses on trimesh seams

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);

	FHitResult Hit;
	const FCollisionShape SweepShape = FCollisionShape::MakeSphere(SweepRadius);
	bool bTraceHit = GetWorld()->SweepSingleByChannel(Hit, TraceStart, TraceEnd, FQuat::Identity, ECC_WorldStatic, SweepShape, Params);

	// Fallback: line trace without sweep
	if (!bTraceHit)
	{
		bTraceHit = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, Params);
	}

	if (bTraceHit)
	{
		const float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		SetActorLocation(Hit.ImpactPoint + FVector(0.f, 0.f, CapsuleHalfHeight));

		UE_LOG(LogVoxelCharacter, Log,
			TEXT("PlaceOnTerrainAndResume: Trace HIT at (%.0f, %.0f, %.0f) — Component=%s — placed at Z=%.0f"),
			Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Hit.ImpactPoint.Z,
			Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("null"),
			GetActorLocation().Z);
	}
	else
	{
		UE_LOG(LogVoxelCharacter, Warning,
			TEXT("PlaceOnTerrainAndResume: Trace MISSED — character at (%.0f, %.0f, %.0f), World=%s, will fall freely"),
			SpawnPos.X, SpawnPos.Y, SpawnPos.Z,
			*GetWorld()->GetName());
	}

	// Resume normal movement
	GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	bIsWaitingForTerrain = false;

	UE_LOG(LogVoxelCharacter, Log,
		TEXT("Terrain ready — ActorCollision=%s, CapsuleCollision=%s, MovementMode=%d"),
		GetActorEnableCollision() ? TEXT("Enabled") : TEXT("DISABLED"),
		GetCapsuleComponent() ? (*StaticEnum<ECollisionEnabled::Type>()->GetNameStringByValue(static_cast<int64>(GetCapsuleComponent()->GetCollisionEnabled()))) : TEXT("?"),
		static_cast<int32>(GetCharacterMovement()->MovementMode.GetValue()));
}

// ---------------------------------------------------------------------------
// Voxel Interaction
// ---------------------------------------------------------------------------

bool AVCCharacterBase::TraceForVoxel(FHitResult& OutHit, float MaxDistance) const
{
	if (!CameraManager)
	{
		return false;
	}

	const FVector Start = CameraManager->GetCurrentCameraLocation();
	const FVector End = Start + CameraManager->GetCurrentCameraRotation().Vector() * MaxDistance;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);

	return GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, Params);
}

// ---------------------------------------------------------------------------
// Bridge: Inventory
// ---------------------------------------------------------------------------

UActorComponent* AVCCharacterBase::GetPrimaryInventory() const
{
#if WITH_INVENTORY_PLUGIN
	return InventoryComponent;
#else
	return nullptr;
#endif
}

int32 AVCCharacterBase::GetActiveHotbarSlot() const
{
	return ActiveHotbarSlot;
}

void AVCCharacterBase::SetActiveHotbarSlot(int32 SlotIndex)
{
	ActiveHotbarSlot = FMath::Clamp(SlotIndex, 0, NumHotbarSlots - 1);
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("ActiveHotbarSlot = %d"), ActiveHotbarSlot);

	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		PC->UpdateHotbarSelection(ActiveHotbarSlot);
	}
}

bool AVCCharacterBase::RequestPickupItem(AActor* WorldItem)
{
#if WITH_INTERACTION_PLUGIN && WITH_INVENTORY_PLUGIN
	if (!WorldItem)
	{
		return false;
	}

	if (UInteractableComponent* Interactable = WorldItem->FindComponentByClass<UInteractableComponent>())
	{
		return Interactable->Interact(this, CGFGameplayTags::Interaction_Type_Pickup) == EInteractionResult::Success;
	}
#endif
	return false;
}

bool AVCCharacterBase::RequestDropActiveItem(int32 Count)
{
#if WITH_INVENTORY_PLUGIN && WITH_INTERACTION_PLUGIN
	if (!InventoryComponent)
	{
		return false;
	}

	FItemInstance Item = InventoryComponent->GetItemInSlot(ActiveHotbarSlot);
	if (!Item.IsValid())
	{
		return false;
	}

	// Determine drop location: in front of character
	const FVector DropLoc = GetActorLocation() + GetActorForwardVector() * 150.f;

	UWorldItemPoolSubsystem* Pool = GetWorld()->GetSubsystem<UWorldItemPoolSubsystem>();
	if (!Pool)
	{
		return false;
	}

	// Create a copy with the requested count for dropping
	FItemInstance DropItem = Item;
	DropItem.StackCount = FMath::Min(Count, Item.StackCount);

	AWorldItem* Spawned = Pool->SpawnWorldItem(DropItem, DropLoc);
	if (!Spawned)
	{
		return false;
	}

	// Remove from inventory
	InventoryComponent->TryRemoveItem(Item.InstanceId, DropItem.StackCount);
	return true;
#else
	return false;
#endif
}

// ---------------------------------------------------------------------------
// Bridge: Interaction
// ---------------------------------------------------------------------------

FVector AVCCharacterBase::GetInteractionTraceOrigin() const
{
	if (CurrentViewMode == EVCViewMode::FirstPerson)
	{
		// Trace from camera position
		if (CameraManager)
		{
			return CameraManager->GetCurrentCameraLocation();
		}
	}

	// Third person: trace from character eye height (avoids targeting behind character)
	return GetActorLocation() + FVector(0.f, 0.f, BaseEyeHeight);
}

FVector AVCCharacterBase::GetInteractionTraceDirection() const
{
	// Always use camera forward, regardless of view mode
	if (CameraManager)
	{
		return CameraManager->GetCurrentCameraRotation().Vector();
	}

	return GetActorForwardVector();
}

float AVCCharacterBase::GetInteractionRange() const
{
	if (CurrentViewMode == EVCViewMode::FirstPerson)
	{
		return 300.f;
	}

	// Third person: wider range to compensate for camera offset
	return 400.f;
}

// ---------------------------------------------------------------------------
// Bridge: Equipment
// ---------------------------------------------------------------------------

void AVCCharacterBase::UpdateEquipmentAttachments()
{
#if WITH_EQUIPMENT_PLUGIN
	if (!EquipmentManager)
	{
		return;
	}

	for (FEquipmentSlot& Slot : EquipmentManager->EquipmentSlots)
	{
		if (!Slot.bIsOccupied || !Slot.AttachedVisualComponent)
		{
			continue;
		}

		USkeletalMeshComponent* TargetMesh = GetTargetMeshForSlot(Slot.SlotTag);
		if (!TargetMesh)
		{
			continue;
		}

		// Determine the correct socket for the current view mode
		FName SocketName = Slot.AttachSocket;
		for (const FVCEquipmentSocketMapping& Mapping : EquipmentSocketMappings)
		{
			if (Mapping.SlotTag == Slot.SlotTag)
			{
				if (CurrentViewMode == EVCViewMode::FirstPerson && !Mapping.ArmsSocket.IsNone())
				{
					SocketName = Mapping.ArmsSocket;
				}
				else
				{
					SocketName = Mapping.BodySocket;
				}
				break;
			}
		}

		Slot.AttachedVisualComponent->AttachToComponent(
			TargetMesh,
			FAttachmentTransformRules::SnapToTargetNotIncludingScale,
			SocketName
		);
	}
#endif
}

USkeletalMeshComponent* AVCCharacterBase::GetTargetMeshForSlot(const FGameplayTag& SlotTag) const
{
	if (CurrentViewMode == EVCViewMode::FirstPerson)
	{
		// Check if this slot has an FP arms socket mapping
		for (const FVCEquipmentSocketMapping& Mapping : EquipmentSocketMappings)
		{
			if (Mapping.SlotTag == SlotTag && !Mapping.ArmsSocket.IsNone())
			{
				return FirstPersonArmsMesh;
			}
		}
	}

	// Default: body mesh (TP mode, or slot has no FP mapping)
	return GetMesh();
}

// ---------------------------------------------------------------------------
// Bridge: Ability
// ---------------------------------------------------------------------------

void AVCCharacterBase::OnEquipmentAbilitiesChanged(const FGameplayTag& SlotTag)
{
	// EquipmentGASIntegration handles ability grant/revoke automatically.
	// This hook is for game-specific responses (e.g., UI updates).
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("Equipment abilities changed for slot: %s"), *SlotTag.ToString());
}

// ---------------------------------------------------------------------------
// Integration Delegate Handlers
// ---------------------------------------------------------------------------

void AVCCharacterBase::HandleInteractableFound(AActor* InteractableActor)
{
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("Interactable found: %s"),
		InteractableActor ? *InteractableActor->GetName() : TEXT("null"));

	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		PC->ShowInteractionPrompt(InteractableActor);
	}
}

void AVCCharacterBase::HandleInteractableLost(AActor* InteractableActor)
{
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("Interactable lost: %s"),
		InteractableActor ? *InteractableActor->GetName() : TEXT("null"));

	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		PC->HideInteractionPrompt();
	}
}

void AVCCharacterBase::HandleItemEquipped(const FItemInstance& Item, FGameplayTag SlotTag)
{
#if WITH_EQUIPMENT_PLUGIN
	UE_LOG(LogVoxelCharacter, Log, TEXT("Item equipped in slot %s"), *SlotTag.ToString());

	// Update equipment anim type for the main hand
	// (Game-specific: read from item data asset or equipment fragment)
	// Default: any equipped item in a hand slot sets Tool anim type
	static const FGameplayTag MainHandTag = FGameplayTag::RequestGameplayTag(FName("Equipment.Slot.MainHand"), false);
	if (SlotTag == MainHandTag)
	{
		ActiveItemAnimType = EVCEquipmentAnimType::Tool;
	}

	// Re-attach to correct mesh for current view mode
	UpdateEquipmentAttachments();
#endif
}

void AVCCharacterBase::HandleItemUnequipped(const FItemInstance& Item, FGameplayTag SlotTag)
{
#if WITH_EQUIPMENT_PLUGIN
	UE_LOG(LogVoxelCharacter, Log, TEXT("Item unequipped from slot %s"), *SlotTag.ToString());

	static const FGameplayTag MainHandTag = FGameplayTag::RequestGameplayTag(FName("Equipment.Slot.MainHand"), false);
	if (SlotTag == MainHandTag)
	{
		ActiveItemAnimType = EVCEquipmentAnimType::Unarmed;
	}
#endif
}

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------

void AVCCharacterBase::ToggleVoxelDebug()
{
	bShowVoxelDebug = !bShowVoxelDebug;
	UE_LOG(LogVoxelCharacter, Log, TEXT("VoxelCharacter debug: %s"), bShowVoxelDebug ? TEXT("ON") : TEXT("OFF"));
}

void AVCCharacterBase::DrawVoxelDebugInfo()
{
	if (!GEngine)
	{
		return;
	}

	const UVCMovementComponent* MovComp = Cast<UVCMovementComponent>(GetCharacterMovement());

	// --- Terrain Context ---
	if (MovComp)
	{
		const FVoxelTerrainContext& Ctx = MovComp->GetTerrainContext();
		const UEnum* SurfaceEnum = StaticEnum<EVoxelSurfaceType>();
		const FString SurfaceName = SurfaceEnum ? SurfaceEnum->GetNameStringByValue(static_cast<int64>(Ctx.SurfaceType)) : TEXT("?");

		GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Cyan,
			FString::Printf(TEXT("=== VoxelCharacter Debug ===")));
		GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Green,
			FString::Printf(TEXT("Surface: %s (MatID: %d)"), *SurfaceName, Ctx.VoxelMaterialID));
		GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Green,
			FString::Printf(TEXT("Friction: %.2f  Hardness: %.2f"), Ctx.FrictionMultiplier, Ctx.SurfaceHardness));
		GEngine->AddOnScreenDebugMessage(-1, 0.f, Ctx.bIsUnderwater ? FColor::Blue : FColor::Green,
			FString::Printf(TEXT("Water: %s  Depth: %.1f"), Ctx.bIsUnderwater ? TEXT("YES") : TEXT("No"), Ctx.WaterDepth));
		GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Green,
			FString::Printf(TEXT("Chunk: [%d, %d, %d]"), Ctx.CurrentChunkCoord.X, Ctx.CurrentChunkCoord.Y, Ctx.CurrentChunkCoord.Z));
	}

	// --- Movement ---
	if (MovComp)
	{
		const FString MoveMode = MovComp->IsMovingOnGround() ? TEXT("Ground")
			: MovComp->IsFalling() ? TEXT("Falling")
			: MovComp->IsSwimming() ? TEXT("Swimming")
			: MovComp->IsFlying() ? TEXT("Flying")
			: TEXT("Custom");

		GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Yellow,
			FString::Printf(TEXT("Move: %s  Speed: %.0f  MaxWalk: %.0f"),
				*MoveMode, MovComp->Velocity.Size(), MovComp->MaxWalkSpeed));
		GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Yellow,
			FString::Printf(TEXT("GroundFriction: %.2f  Grip: %.2f"),
				MovComp->GroundFriction, MovComp->VoxelSurfaceGripMultiplier));
	}

	// --- Camera ---
	if (CameraManager)
	{
		const UEnum* ViewEnum = StaticEnum<EVCViewMode>();
		const FString ViewName = ViewEnum ? ViewEnum->GetNameStringByValue(static_cast<int64>(CurrentViewMode)) : TEXT("?");

		GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Magenta,
			FString::Printf(TEXT("View: %s  FOV: %.1f"), *ViewName, CameraManager->GetCurrentFOV()));

		const FVector CamLoc = CameraManager->GetCurrentCameraLocation();
		GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Magenta,
			FString::Printf(TEXT("CamPos: (%.0f, %.0f, %.0f)"), CamLoc.X, CamLoc.Y, CamLoc.Z));
	}

	// --- Voxel Target ---
	FHitResult Hit;
	if (TraceForVoxel(Hit))
	{
		GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Orange,
			FString::Printf(TEXT("Target: (%.0f, %.0f, %.0f) Dist: %.0f"),
				Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Hit.ImpactPoint.Z,
				FVector::Dist(GetActorLocation(), Hit.ImpactPoint)));
	}
	else
	{
		GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Orange, TEXT("Target: None"));
	}

	// --- Hotbar / Equipment ---
	const UEnum* AnimEnum = StaticEnum<EVCEquipmentAnimType>();
	const FString AnimName = AnimEnum ? AnimEnum->GetNameStringByValue(static_cast<int64>(ActiveItemAnimType)) : TEXT("?");
	GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::White,
		FString::Printf(TEXT("Hotbar: %d  AnimType: %s"), ActiveHotbarSlot, *AnimName));
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

const UVCInputConfig* AVCCharacterBase::GetInputConfig() const
{
	if (const AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		return PC->GetInputConfig();
	}
	return nullptr;
}

void AVCCharacterBase::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	const UVCInputConfig* Config = GetInputConfig();

	if (!EIC || !Config)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("SetupPlayerInputComponent: missing EnhancedInputComponent or InputConfig"));
		return;
	}

	// Movement
	if (Config->IA_Move)
	{
		EIC->BindAction(Config->IA_Move, ETriggerEvent::Triggered, this, &AVCCharacterBase::Input_Move);
	}
	if (Config->IA_Look)
	{
		EIC->BindAction(Config->IA_Look, ETriggerEvent::Triggered, this, &AVCCharacterBase::Input_Look);
	}
	if (Config->IA_Jump)
	{
		EIC->BindAction(Config->IA_Jump, ETriggerEvent::Started, this, &AVCCharacterBase::Input_Jump);
		EIC->BindAction(Config->IA_Jump, ETriggerEvent::Completed, this, &AVCCharacterBase::Input_StopJump);
	}

	// Actions
	if (Config->IA_Interact)
	{
		EIC->BindAction(Config->IA_Interact, ETriggerEvent::Started, this, &AVCCharacterBase::Input_Interact);
	}
	if (Config->IA_ToggleView)
	{
		EIC->BindAction(Config->IA_ToggleView, ETriggerEvent::Started, this, &AVCCharacterBase::Input_ToggleView);
	}
	if (Config->IA_PrimaryAction)
	{
		EIC->BindAction(Config->IA_PrimaryAction, ETriggerEvent::Started, this, &AVCCharacterBase::Input_PrimaryAction);
	}
	if (Config->IA_SecondaryAction)
	{
		EIC->BindAction(Config->IA_SecondaryAction, ETriggerEvent::Started, this, &AVCCharacterBase::Input_SecondaryAction);
	}

	// UI / Hotbar
	if (Config->IA_OpenInventory)
	{
		EIC->BindAction(Config->IA_OpenInventory, ETriggerEvent::Started, this, &AVCCharacterBase::Input_OpenInventory);
	}
	if (Config->IA_OpenMap)
	{
		EIC->BindAction(Config->IA_OpenMap, ETriggerEvent::Started, this, &AVCCharacterBase::Input_OpenMap);
	}
	if (Config->IA_HotbarSlot)
	{
		EIC->BindAction(Config->IA_HotbarSlot, ETriggerEvent::Started, this, &AVCCharacterBase::Input_HotbarSlot);
	}
	if (Config->IA_ScrollHotbar)
	{
		EIC->BindAction(Config->IA_ScrollHotbar, ETriggerEvent::Triggered, this, &AVCCharacterBase::Input_ScrollHotbar);
	}
	if (Config->IA_Drop)
	{
		EIC->BindAction(Config->IA_Drop, ETriggerEvent::Started, this, &AVCCharacterBase::Input_Drop);
	}
}

// ---------------------------------------------------------------------------
// Input Callbacks
// ---------------------------------------------------------------------------

void AVCCharacterBase::Input_Move(const FInputActionValue& Value)
{
	const FVector2D MoveInput = Value.Get<FVector2D>();
	if (Controller)
	{
		const FRotator YawRotation(0.f, Controller->GetControlRotation().Yaw, 0.f);
		const FVector ForwardDir = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDir = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		AddMovementInput(ForwardDir, MoveInput.Y);
		AddMovementInput(RightDir, MoveInput.X);
	}
}

void AVCCharacterBase::Input_Look(const FInputActionValue& Value)
{
	const FVector2D LookInput = Value.Get<FVector2D>();
	AddControllerYawInput(LookInput.X);
	AddControllerPitchInput(LookInput.Y);
}

void AVCCharacterBase::Input_Jump(const FInputActionValue& Value)
{
	Jump();
}

void AVCCharacterBase::Input_StopJump(const FInputActionValue& Value)
{
	StopJumping();
}

void AVCCharacterBase::Input_Interact(const FInputActionValue& Value)
{
#if WITH_INTERACTION_PLUGIN
	if (InteractionComponent)
	{
		InteractionComponent->TryInteract(FGameplayTag());
	}
#else
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("Input_Interact (no InteractionPlugin)"));
#endif
}

void AVCCharacterBase::Input_ToggleView(const FInputActionValue& Value)
{
	SetViewMode(CurrentViewMode == EVCViewMode::FirstPerson
		? EVCViewMode::ThirdPerson
		: EVCViewMode::FirstPerson);
}

void AVCCharacterBase::Input_PrimaryAction(const FInputActionValue& Value)
{
	// Priority chain: GAS ability -> equipped item action -> voxel dig -> fallback
#if WITH_EQUIPMENT_PLUGIN
	if (EquipmentManager)
	{
		static const FGameplayTag MainHandTag = FGameplayTag::RequestGameplayTag(FName("Equipment.Slot.MainHand"), false);
		if (EquipmentManager->IsSlotOccupied(MainHandTag))
		{
			// Equipped tool: route to voxel destruction via trace
			FHitResult Hit;
			if (TraceForVoxel(Hit))
			{
				if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
				{
					UVoxelChunkManager* ChunkMgr = FVCVoxelNavigationHelper::FindChunkManager(GetWorld());
					if (ChunkMgr && ChunkMgr->GetConfiguration())
					{
						const FVector RelPos = Hit.ImpactPoint - ChunkMgr->GetConfiguration()->WorldOrigin;
						const FIntVector VoxelCoord = FVoxelCoordinates::WorldToVoxel(RelPos, ChunkMgr->GetConfiguration()->VoxelSize);
						PC->Server_RequestVoxelModification(VoxelCoord, EVoxelModificationType::Destroy, 0);
					}
				}
			}
			return;
		}
	}
#endif

	// Fallback: unarmed voxel dig
	FHitResult Hit;
	if (TraceForVoxel(Hit))
	{
		if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
		{
			UVoxelChunkManager* ChunkMgr = FVCVoxelNavigationHelper::FindChunkManager(GetWorld());
			if (ChunkMgr && ChunkMgr->GetConfiguration())
			{
				const FVector RelPos = Hit.ImpactPoint - ChunkMgr->GetConfiguration()->WorldOrigin;
				const FIntVector VoxelCoord = FVoxelCoordinates::WorldToVoxel(RelPos, ChunkMgr->GetConfiguration()->VoxelSize);
				PC->Server_RequestVoxelModification(VoxelCoord, EVoxelModificationType::Destroy, 0);
			}
		}
	}
}

void AVCCharacterBase::Input_SecondaryAction(const FInputActionValue& Value)
{
	// Priority chain: GAS ability -> equipped item alt -> voxel place -> fallback
#if WITH_EQUIPMENT_PLUGIN
	if (EquipmentManager)
	{
		static const FGameplayTag MainHandTag = FGameplayTag::RequestGameplayTag(FName("Equipment.Slot.MainHand"), false);
		if (EquipmentManager->IsSlotOccupied(MainHandTag))
		{
			// Equipped tool: route to block placement via trace
			FHitResult Hit;
			if (TraceForVoxel(Hit))
			{
				if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
				{
					UVoxelChunkManager* ChunkMgr = FVCVoxelNavigationHelper::FindChunkManager(GetWorld());
					if (ChunkMgr && ChunkMgr->GetConfiguration())
					{
						// Place block adjacent to the hit face (offset by normal)
						const float VoxelSize = ChunkMgr->GetConfiguration()->VoxelSize;
						const FVector PlacePos = Hit.ImpactPoint + Hit.ImpactNormal * (VoxelSize * 0.5f);
						const FVector RelPos = PlacePos - ChunkMgr->GetConfiguration()->WorldOrigin;
						const FIntVector VoxelCoord = FVoxelCoordinates::WorldToVoxel(RelPos, VoxelSize);
						PC->Server_RequestVoxelModification(VoxelCoord, EVoxelModificationType::Place, 2); // Stone
					}
				}
			}
			return;
		}
	}
#endif

	// Fallback: voxel place with default block
	FHitResult Hit;
	if (TraceForVoxel(Hit))
	{
		if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
		{
			UVoxelChunkManager* ChunkMgr = FVCVoxelNavigationHelper::FindChunkManager(GetWorld());
			if (ChunkMgr && ChunkMgr->GetConfiguration())
			{
				const float VoxelSize = ChunkMgr->GetConfiguration()->VoxelSize;
				const FVector PlacePos = Hit.ImpactPoint + Hit.ImpactNormal * (VoxelSize * 0.5f);
				const FVector RelPos = PlacePos - ChunkMgr->GetConfiguration()->WorldOrigin;
				const FIntVector VoxelCoord = FVoxelCoordinates::WorldToVoxel(RelPos, VoxelSize);
				PC->Server_RequestVoxelModification(VoxelCoord, EVoxelModificationType::Place, 2); // Stone
			}
		}
	}
}

void AVCCharacterBase::Input_OpenInventory(const FInputActionValue& Value)
{
	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		PC->ToggleInventoryUI();
	}
}

void AVCCharacterBase::Input_OpenMap(const FInputActionValue& Value)
{
	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		PC->ToggleWorldMapUI();
	}
}

void AVCCharacterBase::Input_HotbarSlot(const FInputActionValue& Value)
{
	const int32 SlotIndex = FMath::RoundToInt(Value.Get<float>()) - 1; // 1-9 keys map to 0-8 index
	SetActiveHotbarSlot(SlotIndex);
}

void AVCCharacterBase::Input_ScrollHotbar(const FInputActionValue& Value)
{
	const float ScrollDelta = Value.Get<float>();
	if (FMath::Abs(ScrollDelta) > KINDA_SMALL_NUMBER)
	{
		const int32 Direction = (ScrollDelta > 0.f) ? 1 : -1;
		int32 NewSlot = ActiveHotbarSlot + Direction;

		// Wrap around
		if (NewSlot < 0)
		{
			NewSlot = NumHotbarSlots - 1;
		}
		else if (NewSlot >= NumHotbarSlots)
		{
			NewSlot = 0;
		}

		SetActiveHotbarSlot(NewSlot);
	}
}

void AVCCharacterBase::Input_Drop(const FInputActionValue& Value)
{
	RequestDropActiveItem(1);
}
