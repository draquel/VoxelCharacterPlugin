// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCCharacterBase.h"
#include "HAL/IConsoleManager.h"
#include "Core/VCPlayerState.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Core/VCPlayerController.h"
#include "Core/VCGameModeBase.h"
#include "Combat/VCCombatComponent.h"
#include "Editing/VCBlockInteractionComponent.h"
#include "Utilities/CGFCombatStatics.h"
#include "Tags/CGFGameplayTags.h"
#include "Interfaces/CGFRestPointInterface.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/VCCameraManager.h"
#include "Camera/VCFirstPersonCameraMode.h"
#include "Camera/VCThirdPersonCameraMode.h"
#include "Movement/VCMovementComponent.h"
#include "Movement/VCVoxelNavigationHelper.h"
#include "Movement/VCGravityFrame.h"
#include "Camera/VCUnderwaterPostProcess.h"
#include "Input/VCInputConfig.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "TimerManager.h"
#include "AbilitySystemComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Net/UnrealNetwork.h"
#include "VoxelCoordinates.h"
#include "VoxelWorldConfiguration.h"
#include "VoxelChunkManager.h"
#include "VoxelSeaSurface.h"
#include "VoxelScatterManager.h"
#include "VoxelCollisionManager.h"
#include "VoxelCharacterPlugin.h"
#include "Engine/Engine.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/GameModeBase.h"
#include "Combat/VCCombatStatics.h"

#if WITH_INTERACTION_PLUGIN
#include "Components/InteractionComponent.h"
#include "Components/InteractableComponent.h"
#include "Detection/SphereOverlapDetection.h"
#include "Subsystems/WorldItemPoolSubsystem.h"
#include "Actors/WorldItem.h"
#endif

#if WITH_EQUIPMENT_PLUGIN
#include "Components/EquipmentManagerComponent.h"
#include "Types/EquipmentSystemTypes.h"
#endif

#if WITH_INVENTORY_PLUGIN
#include "Components/InventoryComponent.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Data/ItemDefinition.h"
#include "Data/Fragments/ItemFragment_Consumable.h"
#include "Data/Fragments/ItemFragment_LightSource.h"
#include "Data/Fragments/ItemFragment_Placeable.h"
#include "Data/CraftingRecipe.h"
#include "Subsystems/CraftingSubsystem.h"
#include "Data/Fragments/ItemFragment_Equipment.h"
#include "Storage/ItemSerializationUtils.h"
#include "Utilities/CGFGameplayEffectStatics.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/GameInstance.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#endif

namespace
{
	/**
	 * Mirror edit mode into VoxelWorlds' runtime switch for the test-actor brush (voxel.Edit.PlayerInputs), so a map
	 * whose AVoxelWorldTestActor has bEnableEditInputs set (the demo) obeys the same toggle instead of polling the
	 * raw mouse buttons. Looked up by name: no module dependency, and a no-op where VoxelStreaming is absent.
	 * In a cubic world the character targets, previews and breaks the blocks itself (world modes review WM-C16), so the
	 * test actor keeps only its keys (2): its own press-to-remove would skip the break time and its boxes double ours.
	 */
	void ApplyEditModeToVoxelTools(bool bEnabled, bool bCharacterEditsBlocks)
	{
		if (IConsoleVariable* Switch = IConsoleManager::Get().FindConsoleVariable(TEXT("voxel.Edit.PlayerInputs")))
		{
			Switch->Set(bEnabled ? (bCharacterEditsBlocks ? 2 : 1) : 0, ECVF_SetByCode);
		}
	}

	/** The world is cubic: the character's block feedback handles editing there. */
	bool IsBlockWorld(const UWorld* World)
	{
		const UVoxelChunkManager* ChunkMgr = FVCVoxelNavigationHelper::FindChunkManager(World);
		return ChunkMgr && UVCBlockInteractionComponent::ShowsBlockFeedback(ChunkMgr->GetConfiguration());
	}
}

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
		OffHand.AcceptedItemTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Item.Category.Light"))); // torches (feature 7)
		EquipmentManager->AvailableSlots.Add(OffHand);

		FEquipmentSlotDefinition Head;
		Head.SlotTag = FGameplayTag::RequestGameplayTag(FName("Equipment.Slot.Head"));
		Head.SlotDisplayName = NSLOCTEXT("VoxelCharacter", "Head", "Head");
		Head.AttachSocket = FName("head");
		// Mannequin head / spine bones point X up the chain with Y forward; turn Z-up, X-forward item meshes upright
		// and lift the helmet rim (its pivot) to the brow. Tuned live in PIE on the idle pose (QA R9).
		Head.AttachOffset = FTransform(FRotator(-7.f, 79.f, -90.f), FVector(4.f, 1.f, 0.f));
		Head.AcceptedItemTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Item.Category.Armor")));
		EquipmentManager->AvailableSlots.Add(Head);

		FEquipmentSlotDefinition Chest;
		Chest.SlotTag = FGameplayTag::RequestGameplayTag(FName("Equipment.Slot.Chest"));
		Chest.SlotDisplayName = NSLOCTEXT("VoxelCharacter", "Chest", "Chest");
		Chest.AttachSocket = FName("spine_03");
		Chest.AttachOffset = FTransform(FRotator(-23.f, 83.f, -90.f), FVector(16.f, -3.f, 0.f)); // up onto the chest
		Chest.AcceptedItemTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Item.Category.Armor")));
		EquipmentManager->AvailableSlots.Add(Chest);
	}
#endif

#if WITH_INVENTORY_PLUGIN
	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("InventoryComponent"));
#endif

	// --- Combat ---
	CombatComponent = CreateDefaultSubobject<UVCCombatComponent>(TEXT("CombatComponent"));
	CombatComponent->FactionTag = CGFGameplayTags::Faction_Player;

	// --- Block feedback and timed breaking (cubic worlds) ---
	BlockInteraction = CreateDefaultSubobject<UVCBlockInteractionComponent>(TEXT("BlockInteraction"));

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
		EquipmentManager->OnCarriedLightChanged.AddDynamic(this, &AVCCharacterBase::HandleCarriedLightChanged);
		EquipmentManager->OnItemBroken.AddDynamic(this, &AVCCharacterBase::HandleItemBroken);
		EquipmentManager->OnVisualAttached.AddDynamic(this, &AVCCharacterBase::HandleEquipmentVisualAttached);
	}
#endif

	if (CombatComponent)
	{
		CombatComponent->OnDied.AddDynamic(this, &AVCCharacterBase::HandleDied);
		CombatComponent->OnDowned.AddDynamic(this, &AVCCharacterBase::HandleDowned);
	}

	// --- Underwater Post-Process ---
	{
		UVCUnderwaterPostProcess* UnderwaterPP = NewObject<UVCUnderwaterPostProcess>(this, TEXT("UnderwaterPostProcess"));
		if (UnderwaterPP)
		{
			UnderwaterPP->RegisterComponent();
		}
	}

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

	// A planet's ragdoll: the pull toward the centre (until the corpse freezes).
	if (!RagdollGravityDirection.IsZero())
	{
		TickRadialRagdoll(GetMesh(), RagdollGravityDirection);
	}

	TickWorldSafety(DeltaSeconds);

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
				const int32 TotalChunks = TerrainWaitTotalChunks;
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

		// Exact-location resume: the floor probe decides, not the chunk grid (chunks around a dungeon
		// spot can be solid and never get a collision body; the tile floor streams in with its POI).
		if (bResumeAtExactLocation && FMath::Fmod(TerrainWaitElapsed, 1.0f) < DeltaSeconds)
		{
			PlaceOnTerrainAndResume();
			return;
		}

		if (TerrainWaitElapsed >= TerrainWaitTimeout)
		{
			UE_LOG(LogVoxelCharacter, Warning,
				TEXT("Terrain wait timeout (%.1fs) — %d/%d chunks still pending. Force-placing."),
				TerrainWaitTimeout, PendingTerrainChunks.Num(), TerrainWaitTotalChunks);
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
	DOREPLIFETIME(AVCCharacterBase, bIsCorpse);
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

			bool bRespawning = false;
			if (CombatComponent)
			{
				CombatComponent->InitializeWithAbilitySystem(ASC);

				// A respawned avatar inherits the player state's ASC, which still carries the
				// previous avatar's State.Dead / State.Downed tag and zero health.
				bRespawning = ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Dead) || ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Downed);
				if (bRespawning)
				{
					CombatComponent->Revive(1.f);
				}
			}

			// Items the previous avatar carried (captured in HandleDied). Before the vitals reset so
			// equipment that raises MaxHealth is in place when health is set to the maximum.
			RestoreItemSnapshotFromPlayerState();
			// A loaded save also carries the vitals the avatar had (after the items, so equipment stats are in).
			if (AVCPlayerState* VCPS = GetPlayerState<AVCPlayerState>())
			{
				VCPS->ApplyPendingVitals();
			}

			if (bRespawning)
			{
				PS->HandleRespawnAttributeReset();
			}
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
			if (CombatComponent)
			{
				CombatComponent->InitializeWithAbilitySystem(ASC);
			}
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

	if (PS->MeleeAttackAbilityClass)
	{
		ASC->GiveAbility(FGameplayAbilitySpec(PS->MeleeAttackAbilityClass, 1, INDEX_NONE, this));
	}
}

// ---------------------------------------------------------------------------
// Combat
// ---------------------------------------------------------------------------

bool AVCCharacterBase::IsIncapacitated() const
{
	return CombatComponent && (ICGFDamageableInterface::Execute_IsDead(CombatComponent) || CombatComponent->IsDowned());
}

AActor* AVCCharacterBase::FindMeleeTargetUnderCrosshair()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	// Same view the server-side ability uses: eyes + control rotation, not the third-person camera
	// (which sits metres behind the pawn and lags the control rotation by a frame).
	// ECC_Pawn, not ECC_Visibility: the engine's Pawn and CharacterMesh profiles ignore Visibility,
	// so a visibility trace passes straight through every character. Terrain still blocks Pawn.
	// A thin sphere, not a ray: a ray exactly through a capsule's axis misses it (WM-C9, KC12).
	FVector Start;
	FRotator ViewRotation;
	GetActorEyesViewPoint(Start, ViewRotation);
	const FVector End = Start + ViewRotation.Vector() * AttackTargetRange;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VCMeleeTarget), false, this);
	FHitResult Hit;
	if (!World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(AttackTargetProbeRadius), Params))
	{
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s melee target: nothing within %.0f uu from %s toward %s."), *GetName(), AttackTargetRange,
			*Start.ToCompactString(), *ViewRotation.ToCompactString());
		return nullptr;
	}

	AActor* HitActor = Hit.GetActor();
	if (!HitActor || !UCGFCombatStatics::FindDamageable(HitActor).GetObject() || !UCGFCombatStatics::AreHostile(this, HitActor))
	{
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s melee target: the crosshair's sweep met %s (%s) at %s, %.0f uu, from %s toward %s."), *GetName(),
			*GetNameSafe(HitActor), *GetNameSafe(Hit.GetComponent()), *Hit.ImpactPoint.ToCompactString(), Hit.Distance, *Start.ToCompactString(),
			*ViewRotation.ToCompactString());
		return nullptr;
	}
	return HitActor;
}

bool AVCCharacterBase::TryStartMeleeAttack()
{
	if (!CombatComponent || IsIncapacitated())
	{
		return false;
	}
	if (!FindMeleeTargetUnderCrosshair())
	{
		return false;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC)
	{
		return false;
	}

	// The ability is ServerOnly: on a client this sends the activation request to the server,
	// which does its own sweep, so a client cannot fabricate a hit.
	return ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(CGFGameplayTags::Ability_Attack_Melee));
}

void AVCCharacterBase::Incapacitate()
{
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	}
	if (AController* OwningController = GetController())
	{
		OwningController->SetIgnoreMoveInput(true);
		OwningController->SetIgnoreLookInput(true);
	}
}

void AVCCharacterBase::HandleDowned(const FCGFDamageContext& Context)
{
	Incapacitate();
	BP_OnDowned(Context);
}

void AVCCharacterBase::BecomeCorpse()
{
	if (!HasAuthority() || bIsCorpse)
	{
		return;
	}
	bIsCorpse = true;
	ApplyCorpseState();
	UE_LOG(LogVoxelCharacter, Log, TEXT("%s: kept as the player's corpse at %s."), *GetName(), *GetActorLocation().ToCompactString());
}

void AVCCharacterBase::OnRep_IsCorpse()
{
	if (bIsCorpse)
	{
		ApplyCorpseState();
	}
}

void AVCCharacterBase::ApplyCorpseState()
{
	Tags.AddUnique(TEXT("Corpse"));
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->DisableMovement();
		Movement->SetComponentTickEnabled(false);
	}
	// A client that only now learns of the body (joined late) starts the ragdoll here.
	USkeletalMeshComponent* Body = GetMesh();
	if (Body && Body->GetPhysicsAsset() && !Body->IsSimulatingPhysics())
	{
		Body->SetCollisionProfileName(TEXT("Ragdoll"));
		Body->SetSimulatePhysics(true);
		StartRadialRagdoll(Body, GetCharacterMovement(), RagdollGravityDirection);
	}
	if (UWorld* World = GetWorld())
	{
		CorpseStartTime = World->GetTimeSeconds();
		CorpseCalmChecks = 0;
		World->GetTimerManager().SetTimer(CorpseSettleTimer, this, &AVCCharacterBase::CheckCorpseSettled, 0.25f, true);
	}
}

void AVCCharacterBase::CheckCorpseSettled()
{
	USkeletalMeshComponent* Body = GetMesh();
	UWorld* World = GetWorld();
	if (!Body || !World)
	{
		return;
	}
	if (Body->IsSimulatingPhysics())
	{
		const bool bCalm = Body->GetPhysicsLinearVelocity().Size() < 10.f;
		CorpseCalmChecks = bCalm ? CorpseCalmChecks + 1 : 0;
		if (CorpseCalmChecks < 4 && World->GetTimeSeconds() - CorpseStartTime < CorpseFreezeAfterSeconds)
		{
			return;
		}
	}
	// Freeze: stop updating the skeleton first so the bones keep the ragdoll pose, then stop simulating. The body
	// then stays put even if the terrain under it unloads; it keeps query collision for later item recovery.
	World->GetTimerManager().ClearTimer(CorpseSettleTimer);
	Body->bNoSkeletonUpdate = true;
	Body->SetSimulatePhysics(false);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	UE_LOG(LogVoxelCharacter, Log, TEXT("%s: corpse settled and frozen."), *GetName());
}

void AVCCharacterBase::HandleDied(const FCGFDamageContext& Context)
{
	Incapacitate();

	if (USkeletalMeshComponent* Body = GetMesh())
	{
		if (Body->GetPhysicsAsset())
		{
			Body->SetCollisionProfileName(TEXT("Ragdoll"));
			Body->SetSimulatePhysics(true);
			StartRadialRagdoll(Body, GetCharacterMovement(), RagdollGravityDirection);
		}
	}

	BP_OnDied(Context);

	if (HasAuthority())
	{
		CaptureItemSnapshotToPlayerState();

		if (AVCGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AVCGameModeBase>() : nullptr)
		{
			GameMode->HandlePlayerDied(GetController(), GetActorTransform());
		}
		else
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("%s died but the game mode is not an AVCGameModeBase; no respawn will be scheduled."), *GetName());
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
	// (Not for an exact-location resume: a saved spot inside a dungeon must stay under the surface.)
	// On a planet the search covers the whole planet (spherical Phase 6, WM-BP11, G36): a big planet's pole can be open
	// sea far past the near search (4.7 km of arc on the 1,000 km test planet), and a spawn there waited in the planet's
	// core for land that never streamed. A planet with no land at all spawns at the sea's surface, swimming.
	FVector ValidSpawn;
	bSpawnAtSea = false;
	if (!bResumeAtExactLocation && FVCVoxelNavigationHelper::FindSpawnablePosition(GetWorld(), GetActorLocation(), ValidSpawn, 50000.f, /*bAnywhereOnPlanet=*/true))
	{
		SetActorLocation(ValidSpawn);
	}
	else if (!bResumeAtExactLocation && ChunkMgr->GetWorldBounds().Shape == EVoxelWorldBoundsShape::Shell)
	{
		const FVoxelSeaSurface Sea = ChunkMgr->GetSeaSurface();
		const FVector Direction = (GetActorLocation() - Sea.Center).GetSafeNormal();
		if (Sea.bEnabled && Sea.bRadial && !Direction.IsNearlyZero())
		{
			const float HalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
			SetActorLocation(Sea.Center + Direction * (Sea.Radius + HalfHeight));
			bSpawnAtSea = true;
			UE_LOG(LogVoxelCharacter, Warning, TEXT("InitiateChunkBasedWait: no land on the planet; spawning at the sea's surface."));
		}
	}
	// A planet: gravity at the new spot and the body upright in it before anything moves (spherical SP-F1, SP-F4).
	if (UVCMovementComponent* VCMove = Cast<UVCMovementComponent>(GetCharacterMovement()); VCMove && VCMove->UpdatePlanetGravity())
	{
		VCMove->SnapUprightToGravity();
	}

	UE_LOG(LogVoxelCharacter, Log,
		TEXT("InitiateChunkBasedWait: Bound to CollisionManager %p in world '%s' (PIE=%d)"),
		ColMgr, *GetWorld()->GetName(), GetWorld()->IsPlayInEditor());

	// Convert character world position to chunk coordinate
	const FVector CharPos = GetActorLocation();
	const FVector RelPos = CharPos - Config->WorldOrigin;
	const FIntVector CenterChunk = FVoxelCoordinates::WorldToChunk(RelPos, Config->ChunkSize, Config->VoxelSize);

	// Build the grid of chunks we need to wait for (radius on X/Y, center chunk Z only). On a planet the grid lies in the
	// tangent plane at the spawn point (plus the chunk under the feet along local down): at mid latitudes the surface
	// crosses chunk layers diagonally, so an X/Y grid at one Z misses the ground (spherical SP-F4).
	TArray<FIntVector> WaitChunks;
	const FVoxelWorldBounds Bounds = ChunkMgr->GetWorldBounds();
	if (Bounds.Shape == EVoxelWorldBoundsShape::Shell)
	{
		const FVector Up = ChunkMgr->GetUpVectorAt(CharPos);
		const FVector T1 = FVector::CrossProduct(FMath::Abs(Up.Z) < 0.9 ? FVector::UpVector : FVector::ForwardVector, Up).GetSafeNormal();
		const FVector T2 = FVector::CrossProduct(Up, T1);
		const float ChunkWorldSize = Config->GetChunkWorldSize();
		for (int32 DX = -TerrainWaitChunkRadius; DX <= TerrainWaitChunkRadius; ++DX)
		{
			for (int32 DY = -TerrainWaitChunkRadius; DY <= TerrainWaitChunkRadius; ++DY)
			{
				WaitChunks.AddUnique(ChunkMgr->WorldToChunkCoord(CharPos + (T1 * DX + T2 * DY) * ChunkWorldSize));
			}
		}
		WaitChunks.AddUnique(ChunkMgr->WorldToChunkCoord(CharPos - Up * (0.5f * ChunkWorldSize)));
	}
	else
	{
		for (int32 DX = -TerrainWaitChunkRadius; DX <= TerrainWaitChunkRadius; ++DX)
		{
			for (int32 DY = -TerrainWaitChunkRadius; DY <= TerrainWaitChunkRadius; ++DY)
			{
				WaitChunks.Add(FIntVector(CenterChunk.X + DX, CenterChunk.Y + DY, CenterChunk.Z));
			}
		}
	}
	TerrainWaitTotalChunks = WaitChunks.Num();
	PendingTerrainChunks.Empty();
	for (const FIntVector& ChunkCoord : WaitChunks)
	{
		if (ColMgr->HasCollision(ChunkCoord))
		{
			// Already ready — skip
			continue;
		}

		PendingTerrainChunks.Add(ChunkCoord);

		// Request collision with high priority so it's processed ASAP
		ColMgr->RequestCollision(ChunkCoord, 2000.f);
	}

	UE_LOG(LogVoxelCharacter, Log,
		TEXT("InitiateChunkBasedWait: Center chunk (%d,%d,%d), waiting for %d chunks in %dx%d grid%s"),
		CenterChunk.X, CenterChunk.Y, CenterChunk.Z,
		PendingTerrainChunks.Num(),
		2 * TerrainWaitChunkRadius + 1, 2 * TerrainWaitChunkRadius + 1,
		Bounds.Shape == EVoxelWorldBoundsShape::Shell ? TEXT(" (tangent plane)") : TEXT(""));

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

/** Seconds an exact-location resume waits for a floor close under the feet before taking a deeper one. */
static constexpr float ExactFloorGraceSeconds = 15.f;

void AVCCharacterBase::PlaceOnTerrainAndResume()
{
	// Local up: +Z, or the radius on a planet (the movement component's gravity, set when the wait began).
	const FVector Up = FVCGravityFrame::GetUp(GetCharacterMovement());
	if (bResumeAtExactLocation)
	{
		// Look for a floor just below the feet; a surface trace from above would put a player saved
		// inside a dungeon on the terrain over it.
		const float CapsuleHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector Feet = GetActorLocation() - Up * CapsuleHalfHeight;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(this);
		FHitResult Hit;
		const bool bFloor = GetWorld()->SweepSingleByChannel(Hit, Feet + Up * 20.f, Feet - Up * FloorProbeDistance,
			FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeSphere(20.f), Params);
		// The saved spot stood ON a floor, so the real one is within a few units of the feet. A hit far
		// below is the voxel void under a dungeon whose tile floor has not streamed in yet: give the
		// tiles a grace period before accepting it.
		const float Drop = bFloor ? FVector::DotProduct(Feet - Hit.ImpactPoint, Up) : 0.f;
		const bool bFloorIsClose = bFloor && Drop <= 50.f;
		if (!bFloorIsClose && TerrainWaitElapsed < (bFloor ? ExactFloorGraceSeconds : TerrainWaitTimeout))
		{
			// Keep waiting (frozen, no collision); the tick polls again every second.
			bIsWaitingForTerrain = true;
			return;
		}
		bResumeAtExactLocation = false;
		PendingTerrainChunks.Empty();
		// No floor at all under the saved spot by the timeout: nothing will come (the spot is outside the world, or its
		// terrain never generates). Go to the respawn point instead of dropping into the void (WM-I4).
		if (!bFloor && RecoverFromMissingTerrain())
		{
			return;
		}
		SetActorEnableCollision(true);
		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		}
		if (bFloor)
		{
			SetActorLocation(Hit.ImpactPoint + Up * (CapsuleHalfHeight + 2.f));
			UE_LOG(LogVoxelCharacter, Log, TEXT("PlaceOnTerrainAndResume: resumed at the saved spot, floor %.0f below (%s)."),
				Drop, Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("null"));
		}
		else
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("PlaceOnTerrainAndResume: no floor under the saved spot after %.1fs — resuming anyway."), TerrainWaitElapsed);
		}
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		bIsWaitingForTerrain = false;
		return;
	}

	// Re-enable collision on the actor and explicitly on the capsule
	SetActorEnableCollision(true);
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}

	// Sphere sweep downward (along local down: the radius on a planet) to find terrain surface (more robust than line
	// trace at a single point)
	const FVector SpawnPos = GetActorLocation();
	const FVector TraceStart = SpawnPos + Up * 50000.f;
	const FVector TraceEnd = SpawnPos - Up * 50000.f;
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
		SetActorLocation(Hit.ImpactPoint + Up * CapsuleHalfHeight);

		UE_LOG(LogVoxelCharacter, Log,
			TEXT("PlaceOnTerrainAndResume: Trace HIT at (%.0f, %.0f, %.0f) — Component=%s — placed at Z=%.0f"),
			Hit.ImpactPoint.X, Hit.ImpactPoint.Y, Hit.ImpactPoint.Z,
			Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("null"),
			GetActorLocation().Z);
	}
	else
	{
		UE_LOG(LogVoxelCharacter, Warning,
			TEXT("PlaceOnTerrainAndResume: Trace MISSED — character at (%.0f, %.0f, %.0f), World=%s%s"),
			SpawnPos.X, SpawnPos.Y, SpawnPos.Z,
			*GetWorld()->GetName(), bSpawnAtSea ? TEXT(" (at the sea's surface: swimming)") : TEXT(""));
		// Nothing under the pawn after the wait: go to the respawn point and wait there (WM-I4). Without one (or after
		// two tries) it falls, and falling out of the world respawns it. A spawn at the sea's surface (a planet without
		// land, G36) drops into the sea instead and swims.
		if (!bSpawnAtSea && RecoverFromMissingTerrain())
		{
			return;
		}
	}

	// Resume normal movement (upright in the gravity at the landing spot on a planet)
	if (UVCMovementComponent* VCMove = Cast<UVCMovementComponent>(GetCharacterMovement()); VCMove && VCMove->UpdatePlanetGravity())
	{
		VCMove->SnapUprightToGravity();
	}
	// At the sea's surface the pawn drops in and swims; on ground it walks.
	GetCharacterMovement()->SetMovementMode((bSpawnAtSea && !bTraceHit) ? MOVE_Falling : MOVE_Walking);
	bIsWaitingForTerrain = false;
	bSpawnAtSea = false;
	TerrainRecoveryAttempts = 0;

	UE_LOG(LogVoxelCharacter, Log,
		TEXT("Terrain ready — ActorCollision=%s, CapsuleCollision=%s, MovementMode=%d"),
		GetActorEnableCollision() ? TEXT("Enabled") : TEXT("DISABLED"),
		GetCapsuleComponent() ? (*StaticEnum<ECollisionEnabled::Type>()->GetNameStringByValue(static_cast<int64>(GetCapsuleComponent()->GetCollisionEnabled()))) : TEXT("?"),
		static_cast<int32>(GetCharacterMovement()->MovementMode.GetValue()));
}

// ---------------------------------------------------------------------------
// Leaving the world (world modes review WM-I4, KI14; WM-I5)
// ---------------------------------------------------------------------------

bool AVCCharacterBase::GetLastSafeLocation(FVector& OutLocation) const
{
	OutLocation = LastSafeLocation;
	return bHasLastSafeLocation;
}

bool AVCCharacterBase::ResolveRecoveryLocation(bool bAllowLastSafe, FVector& OutLocation) const
{
	if (bAllowLastSafe && bHasLastSafeLocation)
	{
		OutLocation = LastSafeLocation;
		return true;
	}
	const UVoxelChunkManager* ChunkMgr = FVCVoxelNavigationHelper::FindChunkManager(GetWorld());
	const FVoxelWorldBounds Bounds = ChunkMgr ? ChunkMgr->GetWorldBounds() : FVoxelWorldBounds();
	const FVector Here = GetActorLocation();
	// A candidate must be inside the world and away from here: the point is to leave a spot with no terrain. (On a planet
	// distances are 3D: two points over each other in X and Y are on different sides of it.)
	const bool bShell = Bounds.Shape == EVoxelWorldBoundsShape::Shell;
	auto Usable = [&Bounds, &Here, ChunkMgr, bShell](const FVector& Candidate)
	{
		const float MinMove = ChunkMgr && ChunkMgr->GetConfiguration() ? ChunkMgr->GetConfiguration()->GetChunkWorldSize() : 4800.f;
		return Bounds.ContainsXY(Candidate.X, Candidate.Y)
			&& (bShell ? FVector::Dist(Candidate, Here) : FVector::Dist2D(Candidate, Here)) > MinMove;
	};

	FTransform RestPoint;
	if (const AVCPlayerState* PS = GetPlayerState<AVCPlayerState>(); PS && PS->GetRespawnPoint(RestPoint) && Usable(RestPoint.GetLocation()))
	{
		OutLocation = RestPoint.GetLocation();
		return true;
	}
	if (AGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr)
	{
		if (const AActor* Start = GetController() ? GameMode->FindPlayerStart(GetController()) : nullptr; Start && Usable(Start->GetActorLocation()))
		{
			OutLocation = Start->GetActorLocation();
			return true;
		}
	}
	if (ChunkMgr && ChunkMgr->GetConfiguration())
	{
		// The world's centre: an island's middle, a plane's origin; a planet's spawn point over its terrain (its centre is
		// inside it, SP-F4).
		OutLocation = bShell ? ChunkMgr->GetPlanetSpawnPosition()
			: (Bounds.IsBounded() ? Bounds.Center : ChunkMgr->GetConfiguration()->WorldOrigin);
		return true;
	}
	return false;
}

bool AVCCharacterBase::RecoverFromMissingTerrain()
{
	static constexpr int32 MaxRecoveries = 2;
	if (!HasAuthority() || TerrainRecoveryAttempts >= MaxRecoveries)
	{
		return false;
	}
	FVector Target;
	if (!ResolveRecoveryLocation(/*bAllowLastSafe=*/false, Target))
	{
		return false;
	}
	++TerrainRecoveryAttempts;
	UE_LOG(LogVoxelCharacter, Warning,
		TEXT("%s: no terrain under (%.0f, %.0f, %.0f) after %.1fs; moving to the respawn point (%.0f, %.0f, %.0f), try %d."),
		*GetName(), GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z, TerrainWaitElapsed,
		Target.X, Target.Y, Target.Z, TerrainRecoveryAttempts);
	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		PC->Client_ShowToast(NSLOCTEXT("VCCharacter", "NoGroundHere", "No ground here: moved to your respawn point"));
	}
	bResumeAtExactLocation = false;
	SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
	FreezeForTerrainWait();
	InitiateChunkBasedWait();
	return true;
}

void AVCCharacterBase::FellOutOfWorld(const UDamageType& DamageType)
{
	if (!HasAuthority())
	{
		return; // the server decides; the replicated pawn follows
	}
	const bool bDead = CombatComponent && ICGFDamageableInterface::Execute_IsDead(CombatComponent);
	if (bIsCorpse || bDead || !GetController())
	{
		// A corpse, a body already dying, or no player: the engine default (destroy).
		Super::FellOutOfWorld(DamageType);
		return;
	}
	if (bFellOutOfWorld)
	{
		return;
	}
	bFellOutOfWorld = true;

	// Stop the fall and put the body back on the last ground it stood on inside the world, so the death, the corpse
	// and an at-death-location respawn all happen there.
	FVector Back;
	const bool bHasBack = ResolveRecoveryLocation(/*bAllowLastSafe=*/true, Back);
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->StopMovementImmediately();
	}
	if (bHasBack)
	{
		SetActorLocation(Back, false, nullptr, ETeleportType::TeleportPhysics);
	}
	UE_LOG(LogVoxelCharacter, Warning, TEXT("%s fell out of the world; dying and respawning (body back at %s)."),
		*GetName(), bHasBack ? *Back.ToCompactString() : TEXT("(nowhere known)"));
	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		PC->Client_ShowToast(NSLOCTEXT("VCCharacter", "FellOutOfWorld", "You fell out of the world"));
	}

	// Death through the combat pipeline, so the respawn, the item snapshot and the corpse follow as for any death.
	if (CombatComponent)
	{
		const FCGFDamageContext Fall = UVCCombatStatics::MakeDamageContext(nullptr, nullptr, 1.0e6f,
			CGFGameplayTags::Damage_Type_Pure, /*bIgnoreFaction=*/true);
		UVCCombatStatics::ApplyDamageToActor(this, Fall);
	}
	if (!(CombatComponent && ICGFDamageableInterface::Execute_IsDead(CombatComponent)))
	{
		// Could not die (no combat component, or immune): it is back on safe ground; let it carry on from there.
		bFellOutOfWorld = false;
		if (UCharacterMovementComponent* Move = GetCharacterMovement())
		{
			Move->SetMovementMode(MOVE_Falling);
		}
	}
}

void AVCCharacterBase::TickWorldSafety(float DeltaSeconds)
{
	// The world edge hint (every machine; the HUD only exists where the player is local).
	const UVCMovementComponent* Move = Cast<UVCMovementComponent>(GetCharacterMovement());
	const bool bNear = Move && Move->IsNearWorldEdge() && !bIsCorpse;
	if (bNear != bNearWorldEdge)
	{
		bNearWorldEdge = bNear;
		OnWorldEdgeChanged.Broadcast(bNear);
	}

	if (!HasAuthority() || bIsWaitingForTerrain || bIsCorpse || bFellOutOfWorld || !Move)
	{
		return;
	}

	SafeLocationTimer -= DeltaSeconds;
	if (SafeLocationTimer > 0.f)
	{
		return;
	}
	SafeLocationTimer = 0.25f;

	const UVoxelChunkManager* ChunkMgr = FVCVoxelNavigationHelper::FindChunkManager(GetWorld());
	if (!ChunkMgr)
	{
		return;
	}
	const FVector Here = GetActorLocation();
	const FVoxelWorldBounds WorldBounds = ChunkMgr->GetWorldBounds();
	if (WorldBounds.Shape == EVoxelWorldBoundsShape::Shell)
	{
		// A planet: below its terrain is toward the centre (SP-F5).
		if (IsOutOfWorld(WorldBounds, Here, OutOfWorldDepth) && !(CombatComponent && ICGFDamageableInterface::Execute_IsDead(CombatComponent)))
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("%s is %.0f inside the planet's lowest terrain or past its orbit ceiling (%.0f from the centre): out of the world."),
				*GetName(), OutOfWorldDepth, FVector::Dist(Here, WorldBounds.Center));
			FellOutOfWorld(*GetDefault<UDamageType>());
			return;
		}
	}
	else
	{
		if (!bHasOutOfWorldZ && OutOfWorldDepth > 0.f)
		{
			// Only from a real terrain range (a mode without one reports about +-1e9).
			if (WorldBounds.MinZ > -1.0e6f)
			{
				OutOfWorldZ = WorldBounds.MinZ - OutOfWorldDepth;
				bHasOutOfWorldZ = true;
			}
		}
		if (bHasOutOfWorldZ && Here.Z < OutOfWorldZ && !(CombatComponent && ICGFDamageableInterface::Execute_IsDead(CombatComponent)))
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("%s is %.0f below the world's lowest terrain: out of the world."), *GetName(), OutOfWorldDepth);
			FellOutOfWorld(*GetDefault<UDamageType>());
			return;
		}
	}
	if (Move->IsMovingOnGround() && ChunkMgr->IsInsideWorld(Here.X, Here.Y))
	{
		LastSafeLocation = Here;
		bHasLastSafeLocation = true;
	}
}

bool AVCCharacterBase::IsOutOfWorld(const FVoxelWorldBounds& Bounds, const FVector& Location, float Depth)
{
	if (Depth <= 0.f)
	{
		return false;
	}
	if (Bounds.Shape == EVoxelWorldBoundsShape::Shell)
	{
		const double Dist = FVector::Dist(Location, Bounds.Center);
		// Outside the orbit ceiling is out too (spherical Phase 6): flight and the orbit camera stay inside it.
		return Dist < static_cast<double>(Bounds.InnerRadius) - Depth || (Bounds.OrbitRadius > 0.0 && Dist > Bounds.OrbitRadius);
	}
	// Only from a real terrain range (a mode without one reports about +-1e9).
	return Bounds.MinZ > -1.0e6f && Location.Z < Bounds.MinZ - Depth;
}

void AVCCharacterBase::FaceRotation(FRotator NewControlRotation, float DeltaTime)
{
	const UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Move || !Move->HasCustomGravity())
	{
		Super::FaceRotation(NewControlRotation, DeltaTime);
		return;
	}
	if (!(bUseControllerRotationPitch || bUseControllerRotationYaw || bUseControllerRotationRoll))
	{
		return;
	}
	// In the gravity frame: take the components the flags ask for from the control rotation, keep the body upright
	// otherwise (APawn::FaceRotation does this in world space, which tilts the body on a planet).
	const FRotator Control = FVCGravityFrame::ToGravityRelative(Move, NewControlRotation);
	const FRotator Current = FVCGravityFrame::ToGravityRelative(Move, GetActorRotation());
	const FRotator Facing(
		bUseControllerRotationPitch ? Control.Pitch : 0.0,
		bUseControllerRotationYaw ? Control.Yaw : Current.Yaw,
		bUseControllerRotationRoll ? Control.Roll : 0.0);
	SetActorRotation(FVCGravityFrame::ToWorld(Move, Facing));
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

bool AVCCharacterBase::FindEditTarget(FHitResult& OutHit, FIntVector& OutDigVoxel, FIntVector& OutPlaceVoxel) const
{
	const UVoxelChunkManager* ChunkMgr = FVCVoxelNavigationHelper::FindChunkManager(GetWorld());
	const UVoxelWorldConfiguration* Config = ChunkMgr ? ChunkMgr->GetConfiguration() : nullptr;
	if (!Config || !TraceForVoxel(OutHit))
	{
		return false;
	}
	OutDigVoxel = GetEditVoxelCoord(OutHit.ImpactPoint, OutHit.ImpactNormal, Config, /*bPlace=*/false);
	OutPlaceVoxel = GetEditVoxelCoord(OutHit.ImpactPoint, OutHit.ImpactNormal, Config, /*bPlace=*/true);
	return true;
}

FIntVector AVCCharacterBase::GetEditVoxelCoord(const FVector& ImpactPoint, const FVector& ImpactNormal, const UVoxelWorldConfiguration* Config, bool bPlace)
{
	if (!Config)
	{
		return FIntVector::ZeroValue;
	}
	const float VoxelSize = Config->VoxelSize;
	FVector Target = ImpactPoint;
	if (bPlace)
	{
		Target += ImpactNormal * (VoxelSize * 0.5f);
	}
	else if (Config->MeshingMode == EMeshingMode::Cubic)
	{
		Target -= ImpactNormal * (VoxelSize * 0.5f);
	}
	return FVoxelCoordinates::WorldToVoxel(Target - Config->WorldOrigin, VoxelSize);
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
	// The server spawns the pickup and removes the stack (a client-side spawn was local only; QA R18).
	if (!HasAuthority())
	{
		Server_DropItemInSlot(ActiveHotbarSlot, Count);
		return true;
	}
	return DropItemInSlot(ActiveHotbarSlot, Count);
}

void AVCCharacterBase::Server_DropItemInSlot_Implementation(int32 SlotIndex, int32 Count)
{
	DropItemInSlot(SlotIndex, Count);
}

bool AVCCharacterBase::DropItemInSlot(int32 SlotIndex, int32 Count)
{
#if WITH_INVENTORY_PLUGIN && WITH_INTERACTION_PLUGIN
	if (!HasAuthority() || !InventoryComponent || Count <= 0)
	{
		return false;
	}

	FItemInstance Item = InventoryComponent->GetItemInSlot(SlotIndex);
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

FVector AVCCharacterBase::GetPawnViewLocation() const
{
	return GetActorLocation() + FVCGravityFrame::VectorToWorld(GetCharacterMovement(), FVector(0.f, 0.f, BaseEyeHeight));
}

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

	// Third person: trace from character eye height (avoids targeting behind character), along local up on a planet
	return GetActorLocation() + FVCGravityFrame::VectorToWorld(GetCharacterMovement(), FVector(0.f, 0.f, BaseEyeHeight));
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
		// The snap wipes the visual's offset: re-apply the slot / item fit.
		Slot.AttachedVisualComponent->SetRelativeTransform(EquipmentManager->GetVisualAttachOffset(Slot.SlotTag));
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
// Carried light (feature 7)
// ---------------------------------------------------------------------------

float AVCCharacterBase::GetCarriedLightLevel_Implementation() const
{
#if WITH_EQUIPMENT_PLUGIN
	return EquipmentManager ? EquipmentManager->GetCarriedLightLevel() : 0.f;
#else
	return 0.f;
#endif
}

bool AVCCharacterBase::ConsumeCarriedLightFuel_Implementation(float Seconds)
{
#if WITH_EQUIPMENT_PLUGIN
	return EquipmentManager && HasAuthority() && EquipmentManager->ConsumeCarriedLightFuel(Seconds);
#else
	return false;
#endif
}

bool AVCCharacterBase::GetCarriedLightFuel(float& OutFuel, float& OutMaxFuel) const
{
	OutFuel = 0.f;
	OutMaxFuel = 0.f;
#if WITH_EQUIPMENT_PLUGIN
	FGameplayTag SlotTag;
	return EquipmentManager && EquipmentManager->GetCarriedLight(SlotTag, OutFuel, OutMaxFuel);
#else
	return false;
#endif
}

void AVCCharacterBase::HandleCarriedLightChanged(FGameplayTag SlotTag, bool bLit, float FuelSeconds, float MaxFuel)
{
	// Report the strongest light, not the slot that changed: unequipping one torch while another burns stays lit.
	float Fuel = FuelSeconds, Max = MaxFuel;
	const bool bAnyLit = GetCarriedLightFuel(Fuel, Max);
	OnCarriedLightChanged.Broadcast(bAnyLit, bAnyLit ? Fuel : 0.f, bAnyLit ? Max : 0.f);

	// What a target offers can depend on our flame (a dark wall torch reads "Light torch" only with one): when the
	// flame comes or goes, rebuild the prompt for whatever we are looking at.
	if (bAnyLit != bLastCarriedLightLit)
	{
		bLastCarriedLightLit = bAnyLit;
#if WITH_INTERACTION_PLUGIN
		if (InteractionComponent && IsLocallyControlled())
		{
			InteractionComponent->RefreshCurrentTarget();
		}
#endif
	}
}

void AVCCharacterBase::HandleItemBroken(FGameplayTag SlotTag, const FItemInstance& Item)
{
#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
	UGameInstance* GameInstance = GetGameInstance();
	UItemDatabaseSubsystem* ItemDB = GameInstance ? GameInstance->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	const UItemDefinition* Def = ItemDB ? ItemDB->GetDefinition(Item.ItemDefinitionId) : nullptr;
	if (Def && Def->FindFragment<UItemFragment_LightSource>())
	{
		UE_LOG(LogVoxelCharacter, Log, TEXT("%s: carried light %s burnt out."), *GetName(), *Item.ItemDefinitionId.PrimaryAssetName.ToString());
		Client_LightBurntOut();
	}
#endif
}

void AVCCharacterBase::Client_LightBurntOut_Implementation()
{
	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		PC->ShowToast(NSLOCTEXT("VCHud", "TorchBurntOut", "Your torch burnt out"), 4.0f);
	}
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
	if (Config->IA_Sprint)
	{
		EIC->BindAction(Config->IA_Sprint, ETriggerEvent::Started, this, &AVCCharacterBase::Input_StartSprint);
		EIC->BindAction(Config->IA_Sprint, ETriggerEvent::Completed, this, &AVCCharacterBase::Input_StopSprint);
	}
	if (Config->IA_Crouch)
	{
		EIC->BindAction(Config->IA_Crouch, ETriggerEvent::Started, this, &AVCCharacterBase::Input_StartCrouch);
		EIC->BindAction(Config->IA_Crouch, ETriggerEvent::Completed, this, &AVCCharacterBase::Input_StopCrouch);
	}

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
		// A cubic world's block breaks while the button stays down (WM-C15): the release ends the hold.
		EIC->BindAction(Config->IA_PrimaryAction, ETriggerEvent::Completed, this, &AVCCharacterBase::Input_StopPrimaryAction);
		EIC->BindAction(Config->IA_PrimaryAction, ETriggerEvent::Canceled, this, &AVCCharacterBase::Input_StopPrimaryAction);
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
	if (Config->IA_Pause)
	{
		EIC->BindAction(Config->IA_Pause, ETriggerEvent::Started, this, &AVCCharacterBase::Input_Pause);
	}
	if (Config->IA_OpenMap)
	{
		EIC->BindAction(Config->IA_OpenMap, ETriggerEvent::Started, this, &AVCCharacterBase::Input_OpenMap);
	}
	if (Config->IA_HotbarSlot)
	{
		EIC->BindAction(Config->IA_HotbarSlot, ETriggerEvent::Started, this, &AVCCharacterBase::Input_HotbarSlot);
	}
	if (Config->IA_Use)
	{
		EIC->BindAction(Config->IA_Use, ETriggerEvent::Started, this, &AVCCharacterBase::Input_Use);
	}
	if (Config->IA_Craft)
	{
		EIC->BindAction(Config->IA_Craft, ETriggerEvent::Started, this, &AVCCharacterBase::Input_Craft);
	}
	if (Config->IA_ToggleEditMode)
	{
		EIC->BindAction(Config->IA_ToggleEditMode, ETriggerEvent::Started, this, &AVCCharacterBase::Input_ToggleEditMode);
	}

	// Input is only set up for the local player: start the world's test brush in the same (off) state as edit mode.
	ApplyEditModeToVoxelTools(bEditModeEnabled, IsBlockWorld(GetWorld()));
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
		const UVCMovementComponent* MovComp = Cast<UVCMovementComponent>(GetCharacterMovement());
		if (MovComp && MovComp->IsSwimming())
		{
			// Swimming: full 3D direction from camera (look down + forward = dive)
			const FRotator ControlRot = Controller->GetControlRotation();
			const FVector ForwardDir = FRotationMatrix(ControlRot).GetUnitAxis(EAxis::X);
			const FVector RightDir = FRotationMatrix(ControlRot).GetUnitAxis(EAxis::Y);
			AddMovementInput(ForwardDir, MoveInput.Y);
			AddMovementInput(RightDir, MoveInput.X);
		}
		else
		{
			// Ground/air: yaw only, in the gravity frame (the world's yaw without custom gravity; spherical SP-F3)
			const FRotator YawRotation = FVCGravityFrame::GetYawRotation(GetCharacterMovement(), Controller->GetControlRotation());
			const FVector ForwardDir = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
			const FVector RightDir = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
			AddMovementInput(ForwardDir, MoveInput.Y);
			AddMovementInput(RightDir, MoveInput.X);
		}
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
	// Held, the jump key also rises while swimming (Jump's own flag lasts one frame).
	if (UVCMovementComponent* Movement = Cast<UVCMovementComponent>(GetCharacterMovement()))
	{
		Movement->SetWantsToAscend(true);
	}
}

void AVCCharacterBase::Input_StopJump(const FInputActionValue& Value)
{
	StopJumping();
	if (UVCMovementComponent* Movement = Cast<UVCMovementComponent>(GetCharacterMovement()))
	{
		Movement->SetWantsToAscend(false);
	}
}

void AVCCharacterBase::Input_StartSprint(const FInputActionValue& Value)
{
	if (UVCMovementComponent* Movement = Cast<UVCMovementComponent>(GetCharacterMovement()))
	{
		Movement->SetSprinting(!IsIncapacitated());
	}
}

void AVCCharacterBase::Input_StopSprint(const FInputActionValue& Value)
{
	if (UVCMovementComponent* Movement = Cast<UVCMovementComponent>(GetCharacterMovement()))
	{
		Movement->SetSprinting(false);
	}
}

void AVCCharacterBase::Input_StartCrouch(const FInputActionValue& Value)
{
	if (!IsIncapacitated())
	{
		Crouch();
	}
}

void AVCCharacterBase::Input_StopCrouch(const FInputActionValue& Value)
{
	UnCrouch();
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
	// Priority chain: GAS ability -> [edit mode only] equipped item action -> voxel dig -> fallback
	if (IsIncapacitated())
	{
		return;
	}

	// Step 1: a hostile combatant under the crosshair takes priority over digging.
	if (TryStartMeleeAttack())
	{
		return;
	}

	// Step 1b: a harvestable scatter instance (a tree) in front of the character is chopped, not dug.
	if (TryChopScatter())
	{
		return;
	}

	// Step 1c: a cubic world's voxel tree under the crosshair is chopped too (WM-C8), unless edit mode digs its blocks.
	if (!bEditModeEnabled && TryChopVoxelTree())
	{
		return;
	}

	// Steps 2-3 carve terrain, so they only run while edit mode is on (IA_ToggleEditMode / vox.EditMode)
	// or a digging tool is equipped (feature 8: a pickaxe is the gathering mechanic). Attacking and
	// interacting with neither must never touch voxels.
	if (!bEditModeEnabled && !HasDiggingTool())
	{
		return;
	}

	// Step 2 (an equipped tool's dig) and step 3 (an unarmed dig, edit mode) dig the same voxel, FindEditTarget's (WM-C12).
	// In a cubic world with break times the press starts a hold instead (WM-C15): the block under the crosshair breaks
	// after its material's time while the button stays down, decided on the server.
	const UVoxelChunkManager* ChunkMgr = FVCVoxelNavigationHelper::FindChunkManager(GetWorld());
	if (BlockInteraction && ChunkMgr && UVCBlockInteractionComponent::UsesTimedBreaks(ChunkMgr->GetConfiguration()))
	{
		BlockInteraction->StartHold();
		return;
	}
	FHitResult Hit;
	FIntVector DigVoxel, PlaceVoxel;
	AVCPlayerController* PC = Cast<AVCPlayerController>(GetController());
	if (PC && FindEditTarget(Hit, DigVoxel, PlaceVoxel))
	{
		PC->Server_RequestVoxelModification(DigVoxel, EVoxelModificationType::Destroy, 0);
	}
}

void AVCCharacterBase::Input_StopPrimaryAction(const FInputActionValue& Value)
{
	if (BlockInteraction)
	{
		BlockInteraction->StopHold();
	}
}

void AVCCharacterBase::Input_SecondaryAction(const FInputActionValue& Value)
{
	// Priority chain: GAS ability -> [edit mode only] equipped item alt -> voxel place -> fallback
	if (IsIncapacitated())
	{
		return;
	}

	// Every step below places a block: a no-op unless edit mode is on (IA_ToggleEditMode / vox.EditMode).
	if (!bEditModeEnabled)
	{
		return;
	}

	// An equipped tool and bare hands place the same block, stone, in the voxel FindEditTarget gives (WM-C12): adjacent
	// to the hit face, where the placement preview shows it.
	FHitResult Hit;
	FIntVector DigVoxel, PlaceVoxel;
	AVCPlayerController* PC = Cast<AVCPlayerController>(GetController());
	if (PC && FindEditTarget(Hit, DigVoxel, PlaceVoxel))
	{
		PC->Server_RequestVoxelModification(PlaceVoxel, EVoxelModificationType::Place, 2); // Stone
	}
}

void AVCCharacterBase::Input_OpenInventory(const FInputActionValue& Value)
{
	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		PC->ToggleInventoryUI();
	}
}

void AVCCharacterBase::Input_Pause(const FInputActionValue& Value)
{
	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		PC->TogglePauseMenu();
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

void AVCCharacterBase::Input_Use(const FInputActionValue& Value)
{
	RequestUseActiveItem();
}

void AVCCharacterBase::Input_ToggleEditMode(const FInputActionValue& Value)
{
	ToggleEditMode();
}

// ---------------------------------------------------------------------------
// Voxel edit mode
// ---------------------------------------------------------------------------


void AVCCharacterBase::SetEditModeEnabled(bool bEnabled)
{
	if (bEditModeEnabled == bEnabled)
	{
		return;
	}
	bEditModeEnabled = bEnabled;
	UE_LOG(LogVoxelCharacter, Log, TEXT("%s: voxel edit mode %s"), *GetName(), bEditModeEnabled ? TEXT("ON") : TEXT("OFF"));
	if (IsLocallyControlled())
	{
		ApplyEditModeToVoxelTools(bEditModeEnabled, IsBlockWorld(GetWorld()));
	}
	OnEditModeChanged.Broadcast(bEditModeEnabled);
}

void AVCCharacterBase::ToggleEditMode()
{
	SetEditModeEnabled(!bEditModeEnabled);
}

// ---------------------------------------------------------------------------
// Item actions
// ---------------------------------------------------------------------------

bool AVCCharacterBase::EquipHotbarItem(int32 HotbarSlot)
{
#if WITH_INVENTORY_PLUGIN && WITH_EQUIPMENT_PLUGIN
	if (!InventoryComponent || !EquipmentManager)
	{
		return false;
	}
	const int32 Slot = HotbarSlot < 0 ? ActiveHotbarSlot : HotbarSlot;
	const FItemInstance Item = InventoryComponent->GetItemInSlot(Slot);
	if (!Item.IsValid())
	{
		return false;
	}

	UGameInstance* GI = GetGameInstance();
	UItemDatabaseSubsystem* ItemDB = GI ? GI->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	const UItemDefinition* Def = ItemDB ? ItemDB->GetDefinition(Item.ItemDefinitionId) : nullptr;
	const UItemFragment_Equipment* EquipFrag = Def ? Def->FindFragment<UItemFragment_Equipment>() : nullptr;
	if (!EquipFrag || !EquipFrag->EquipmentSlotTag.IsValid())
	{
		UE_LOG(LogVoxelCharacter, Log, TEXT("EquipHotbarItem: slot %d holds %s, which is not equippable."), Slot, *Item.ItemDefinitionId.PrimaryAssetName.ToString());
		return false;
	}

	// The manager routes to its server RPC when called on a client.
	const EEquipmentResult Result = EquipmentManager->TryEquipFromInventory(Item.InstanceId, InventoryComponent, EquipFrag->EquipmentSlotTag);
	UE_LOG(LogVoxelCharacter, Log, TEXT("EquipHotbarItem: %s -> %s: %s"), *Item.ItemDefinitionId.PrimaryAssetName.ToString(),
		*EquipFrag->EquipmentSlotTag.ToString(), *UEnum::GetValueAsString(Result));
	return Result == EEquipmentResult::Success;
#else
	return false;
#endif
}

bool AVCCharacterBase::UnequipSlotToInventory(FGameplayTag SlotTag)
{
#if WITH_INVENTORY_PLUGIN && WITH_EQUIPMENT_PLUGIN
	if (!InventoryComponent || !EquipmentManager || !SlotTag.IsValid())
	{
		return false;
	}
	const EEquipmentResult Result = EquipmentManager->TryUnequipToInventory(SlotTag, InventoryComponent);
	UE_LOG(LogVoxelCharacter, Log, TEXT("UnequipSlotToInventory: %s: %s"), *SlotTag.ToString(), *UEnum::GetValueAsString(Result));
	return Result == EEquipmentResult::Success;
#else
	return false;
#endif
}

void AVCCharacterBase::RequestUseActiveItem()
{
	RequestUseItemInSlot(ActiveHotbarSlot);
}

void AVCCharacterBase::RequestUseItemInSlot(int32 SlotIndex)
{
	if (IsIncapacitated())
	{
		return;
	}
	if (HasAuthority())
	{
		UseItemInSlot(SlotIndex);
	}
	else
	{
		Server_UseItemInSlot(SlotIndex);
	}
}

void AVCCharacterBase::HandleEquipmentVisualAttached(FGameplayTag SlotTag)
{
	// The manager attaches to the body; first-person arms take their slots once the mesh exists.
	UpdateEquipmentAttachments();
}

void AVCCharacterBase::Server_UseItemInSlot_Implementation(int32 SlotIndex)
{
	UseItemInSlot(SlotIndex);
}

bool AVCCharacterBase::UseItemInSlot(int32 SlotIndex)
{
#if WITH_INVENTORY_PLUGIN
	if (!HasAuthority() || !InventoryComponent || IsIncapacitated())
	{
		return false;
	}

	const FItemInstance Item = InventoryComponent->GetItemInSlot(SlotIndex);
	if (!Item.IsValid())
	{
		return false;
	}

	UGameInstance* GI = GetGameInstance();
	UItemDatabaseSubsystem* ItemDB = GI ? GI->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	UItemDefinition* Def = ItemDB ? ItemDB->GetDefinition(Item.ItemDefinitionId) : nullptr;
	if (const UItemFragment_Placeable* Placeable = Def ? Def->FindFragment<UItemFragment_Placeable>() : nullptr)
	{
		return TryPlaceItem(SlotIndex, Item, *Placeable);
	}
	const UItemFragment_Consumable* Consumable = Def ? Def->FindFragment<UItemFragment_Consumable>() : nullptr;
	if (!Consumable)
	{
#if WITH_EQUIPMENT_PLUGIN
		// Hotbar option C (QA R18): using gear equips it, swapping out whatever held that slot.
		const UItemFragment_Equipment* EquipFrag = Def ? Def->FindFragment<UItemFragment_Equipment>() : nullptr;
		if (EquipFrag && EquipFrag->EquipmentSlotTag.IsValid() && EquipmentManager)
		{
			const EEquipmentResult Result = EquipmentManager->TryEquipFromInventory(Item.InstanceId, InventoryComponent, EquipFrag->EquipmentSlotTag);
			UE_LOG(LogVoxelCharacter, Log, TEXT("UseItemInSlot: equip %s -> %s: %s"), *Item.ItemDefinitionId.PrimaryAssetName.ToString(),
				*EquipFrag->EquipmentSlotTag.ToString(), *UEnum::GetValueAsString(Result));
			return Result == EEquipmentResult::Success;
		}
#endif
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("UseItemInSlot: %s is not consumable."), *Item.ItemDefinitionId.PrimaryAssetName.ToString());
		return false;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	if (const double* ReadyAt = ConsumableReadyTime.Find(Item.ItemDefinitionId))
	{
		if (Now < *ReadyAt)
		{
			UE_LOG(LogVoxelCharacter, Verbose, TEXT("UseItemInSlot: %s on cooldown for %.1fs."), *Item.ItemDefinitionId.PrimaryAssetName.ToString(), *ReadyAt - Now);
			return false;
		}
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC)
	{
		return false;
	}

	bool bDidSomething = false;
	if (Consumable->AttributeChanges.Num() > 0)
	{
		bDidSomething |= UCGFGameplayEffectStatics::ApplyInstantAttributeModifiers(ASC, Consumable->AttributeChanges, Def);
	}
	if (Consumable->ConsumeEffect)
	{
		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		Context.AddSourceObject(Def);
		ASC->ApplyGameplayEffectToSelf(Consumable->ConsumeEffect->GetDefaultObject<UGameplayEffect>(), 1.f, Context);
		bDidSomething = true;
	}
	if (Consumable->ConsumeAbility)
	{
		FGameplayAbilitySpec Spec(Consumable->ConsumeAbility, 1, INDEX_NONE, this);
		ASC->GiveAbilityAndActivateOnce(Spec);
		bDidSomething = true;
	}

	if (!bDidSomething)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("UseItemInSlot: %s has a Consumable fragment with nothing to apply."), *Item.ItemDefinitionId.PrimaryAssetName.ToString());
		return false;
	}

	if (Consumable->CooldownDuration > 0.f)
	{
		ConsumableReadyTime.Add(Item.ItemDefinitionId, Now + Consumable->CooldownDuration);
	}
	if (Consumable->bConsumeOnUse)
	{
		InventoryComponent->TryRemoveItem(Item.InstanceId, 1);
	}

	UE_LOG(LogVoxelCharacter, Log, TEXT("UseItemInSlot: used %s from slot %d."), *Item.ItemDefinitionId.PrimaryAssetName.ToString(), SlotIndex);
	return true;
#else
	return false;
#endif
}

// ---------------------------------------------------------------------------
// Surface gameplay (feature 8): placing, gathering helpers, rest, crafting
// ---------------------------------------------------------------------------

bool AVCCharacterBase::TryPlaceItem(int32 SlotIndex, const FItemInstance& Item, const UItemFragment_Placeable& Placeable)
{
#if WITH_INVENTORY_PLUGIN
	UWorld* World = GetWorld();
	if (!World || !HasAuthority() || !InventoryComponent)
	{
		return false;
	}
	UClass* ActorClass = Placeable.ActorClass.LoadSynchronous();
	if (!ActorClass)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("TryPlaceItem: %s has no ActorClass."), *Item.ItemDefinitionId.PrimaryAssetName.ToString());
		return false;
	}

	// Aim point: PlaceDistance ahead on the horizontal view direction, then straight down to the ground (along local up on
	// a planet, where the slope test reads the normal in the gravity frame).
	FVector Eyes;
	FRotator View;
	GetActorEyesViewPoint(Eyes, View);
	FVector TraceStart, TraceEnd;
	GetPlacementTrace(GetCharacterMovement(), GetActorLocation(), View, Placeable.PlaceDistance, TraceStart, TraceEnd);
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VCPlaceItem), false, this);
	const bool bHit = World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params);
	if (!bHit || !Placeable.AcceptsGround(FVCGravityFrame::VectorToGravity(GetCharacterMovement(), Hit.ImpactNormal)))
	{
		UE_LOG(LogVoxelCharacter, Log, TEXT("TryPlaceItem: no level ground ahead for %s."), *Item.ItemDefinitionId.PrimaryAssetName.ToString());
		if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
		{
			PC->Client_NotifyGathered(NSLOCTEXT("VCPlace", "NoGround", "Needs level ground ahead"), 0);
		}
		return false;
	}

	// Face the player.
	const FTransform Placement = GetPlacementTransform(GetCharacterMovement(), View, Hit.ImpactPoint, Placeable.PlacementOffsetZ);
	const FVector Location = Placement.GetLocation();
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	SpawnParams.Owner = this;
	AActor* Placed = World->SpawnActor<AActor>(ActorClass, Placement, SpawnParams);
	if (!Placed)
	{
		return false;
	}
	// Marks it for the save system: player-placed actors are saved by class + transform (feature 9).
	Placed->Tags.AddUnique(TEXT("Placed"));
	if (Placeable.bConsumeOnPlace)
	{
		InventoryComponent->TryRemoveItem(Item.InstanceId, 1);
	}
	UE_LOG(LogVoxelCharacter, Log, TEXT("TryPlaceItem: placed %s (%s) at %s."), *Item.ItemDefinitionId.PrimaryAssetName.ToString(),
		*Placed->GetName(), *Location.ToCompactString());
	return true;
#else
	return false;
#endif
}

FVector AVCCharacterBase::GetChopAimPoint() const
{
	return GetChopAimPointFor(GetCharacterMovement(), GetActorLocation(), GetControlRotation());
}

void AVCCharacterBase::GetPlacementTrace(const UCharacterMovementComponent* Movement, const FVector& ActorLocation, const FRotator& View,
	float PlaceDistance, FVector& OutStart, FVector& OutEnd)
{
	if (Movement && Movement->HasCustomGravity())
	{
		const FVector Up = FVCGravityFrame::GetUp(Movement);
		const FVector Ahead = ActorLocation + FVCGravityFrame::FacingRotation(Movement, View.Vector()).Vector() * PlaceDistance;
		OutStart = Ahead + Up * 200.f;
		OutEnd = Ahead - Up * 400.f;
		return;
	}
	const FVector Forward = FRotator(0.f, View.Yaw, 0.f).Vector();
	const FVector Ahead = ActorLocation + Forward * PlaceDistance;
	OutStart = Ahead + FVector(0.f, 0.f, 200.f);
	OutEnd = Ahead - FVector(0.f, 0.f, 400.f);
}

FTransform AVCCharacterBase::GetPlacementTransform(const UCharacterMovementComponent* Movement, const FRotator& View, const FVector& GroundPoint, float OffsetZ)
{
	if (Movement && Movement->HasCustomGravity())
	{
		const FVector Forward = FVCGravityFrame::FacingRotation(Movement, View.Vector()).Vector();
		return FTransform(FVCGravityFrame::FacingRotation(Movement, -Forward), GroundPoint + FVCGravityFrame::GetUp(Movement) * OffsetZ);
	}
	return FTransform(FRotator(0.f, View.Yaw + 180.f, 0.f), GroundPoint + FVector(0.f, 0.f, OffsetZ));
}

FVector AVCCharacterBase::GetChopAimPointFor(const UCharacterMovementComponent* Movement, const FVector& ActorLocation, const FRotator& View)
{
	if (Movement && Movement->HasCustomGravity())
	{
		return ActorLocation + FVCGravityFrame::FacingRotation(Movement, View.Vector()).Vector() * 160.f;
	}
	const FVector Forward = FRotator(0.f, View.Yaw, 0.f).Vector();
	return ActorLocation + Forward * 160.f;
}

bool AVCCharacterBase::StartRadialRagdoll(USkeletalMeshComponent* Body, const UCharacterMovementComponent* Movement, FVector& OutGravityDirection)
{
	OutGravityDirection = FVector::ZeroVector;
	if (!Body || !Movement || !Movement->HasCustomGravity())
	{
		return false;
	}
	// Chaos pulls every body along world -Z; on a planet the ragdoll falls toward the centre instead (SP-F7).
	Body->SetEnableGravity(false);
	OutGravityDirection = Movement->GetGravityDirection();
	return true;
}

void AVCCharacterBase::TickRadialRagdoll(USkeletalMeshComponent* Body, const FVector& GravityDirection)
{
	if (Body && !GravityDirection.IsZero() && Body->IsSimulatingPhysics())
	{
		const UWorld* World = Body->GetWorld();
		const float Gravity = World ? FMath::Abs(World->GetGravityZ()) : 980.f;
		Body->AddForceToAllBodiesBelow(GravityDirection * Gravity, NAME_None, /*bAccelChange=*/true, /*bIncludeSelf=*/true);
	}
}

bool AVCCharacterBase::TryChopScatter()
{
	UVoxelChunkManager* ChunkMgr = FVCVoxelNavigationHelper::FindChunkManager(GetWorld());
	UVoxelScatterManager* Scatter = ChunkMgr ? ChunkMgr->GetScatterManager() : nullptr;
	const AVCPlayerController* PC = Cast<AVCPlayerController>(GetController());
	if (!Scatter || !PC)
	{
		return false;
	}
	// Local check first so a swing at empty ground still digs; the server re-validates.
	const FVector AimPoint = GetChopAimPoint();
	FScatterHarvestResult Found;
	if (!Scatter->HarvestScatterNear(AimPoint, PC->ChopSearchRadius, /*bRemove=*/false, Found))
	{
		return false;
	}
	if (HasAuthority())
	{
		Server_ChopScatter_Implementation(AimPoint);
	}
	else
	{
		Server_ChopScatter(AimPoint);
	}
	return true;
}

void AVCCharacterBase::Server_ChopScatter_Implementation(const FVector& AimPoint)
{
	if (IsIncapacitated())
	{
		return;
	}
	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		PC->ChopScatterAt(AimPoint);
	}
}

bool AVCCharacterBase::TryChopVoxelTree()
{
	UVoxelChunkManager* ChunkMgr = FVCVoxelNavigationHelper::FindChunkManager(GetWorld());
	const UVoxelWorldConfiguration* Config = ChunkMgr ? ChunkMgr->GetConfiguration() : nullptr;
	if (!Config || !ChunkMgr->HasVoxelTrees() || !Cast<AVCPlayerController>(GetController()))
	{
		return false;
	}
	// The block under the crosshair, as a dig would take it (behind the hit face); local check first so a swing at the
	// ground still digs, the server re-validates.
	FHitResult Hit;
	if (!TraceForVoxel(Hit))
	{
		return false;
	}
	const FIntVector VoxelCoord = GetEditVoxelCoord(Hit.ImpactPoint, Hit.ImpactNormal, Config, /*bPlace=*/false);
	FVoxelTreeInstance Tree;
	if (!ChunkMgr->FindVoxelTreeAt(ChunkMgr->GlobalVoxelToWorldCentre(VoxelCoord), Tree) || !AVCPlayerController::IsVoxelTreeStanding(ChunkMgr, Tree))
	{
		return false;
	}
	if (HasAuthority())
	{
		Server_ChopVoxelTree_Implementation(VoxelCoord);
	}
	else
	{
		Server_ChopVoxelTree(VoxelCoord);
	}
	return true;
}

void AVCCharacterBase::Server_ChopVoxelTree_Implementation(const FIntVector& VoxelCoord)
{
	if (IsIncapacitated())
	{
		return;
	}
	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		PC->ChopVoxelTreeAt(VoxelCoord);
	}
}

float AVCCharacterBase::GetMiningSpeed() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	return ASC ? ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetMiningSpeedAttribute()) : 1.f;
}

bool AVCCharacterBase::HasDiggingTool() const
{
#if WITH_EQUIPMENT_PLUGIN
	static const FGameplayTag MainHandTag = FGameplayTag::RequestGameplayTag(FName("Equipment.Slot.MainHand"), false);
	if (!EquipmentManager || !EquipmentManager->IsSlotOccupied(MainHandTag))
	{
		return false;
	}
	return GetMiningSpeed() > 1.f + KINDA_SMALL_NUMBER;
#else
	return false;
#endif
}

void AVCCharacterBase::RestoreVitals()
{
	UAbilitySystemComponent* ASC = HasAuthority() ? GetAbilitySystemComponent() : nullptr;
	if (!ASC)
	{
		return;
	}
	ASC->SetNumericAttributeBase(UVCCharacterAttributeSet::GetHealthAttribute(),
		ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetMaxHealthAttribute()));
	ASC->SetNumericAttributeBase(UVCCharacterAttributeSet::GetStaminaAttribute(),
		ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetMaxStaminaAttribute()));
}

bool AVCCharacterBase::IsRestPointInRange(const AActor* RestPoint) const
{
	if (!RestPoint || !RestPoint->Implements<UCGFRestPointInterface>())
	{
		return false;
	}
	const float Range = FMath::Max(GetInteractionRange(), 400.f) + 100.f; // the actor's origin may sit behind its mesh
	return FVector::Dist(RestPoint->GetActorLocation(), GetActorLocation()) <= Range;
}

void AVCCharacterBase::RequestRest(AActor* RestPoint)
{
	if (HasAuthority())
	{
		Server_RestAt_Implementation(RestPoint);
	}
	else
	{
		Server_RestAt(RestPoint);
	}
}

void AVCCharacterBase::RequestSleep(AActor* RestPoint)
{
	if (HasAuthority())
	{
		Server_SleepAt_Implementation(RestPoint);
	}
	else
	{
		Server_SleepAt(RestPoint);
	}
}

void AVCCharacterBase::RequestCraft(FPrimaryAssetId RecipeId, AActor* Station)
{
	if (HasAuthority())
	{
		Server_CraftRecipe_Implementation(RecipeId, Station);
	}
	else
	{
		Server_CraftRecipe(RecipeId, Station);
	}
}

void AVCCharacterBase::Server_RestAt_Implementation(AActor* RestPoint)
{
	if (IsIncapacitated() || !IsRestPointInRange(RestPoint))
	{
		return;
	}
	const bool bRested = ICGFRestPointInterface::Execute_Rest(RestPoint, this);
	UE_LOG(LogVoxelCharacter, Log, TEXT("%s rests at %s: %s"), *GetName(), *RestPoint->GetName(), bRested ? TEXT("ok") : TEXT("refused"));
	Client_CraftResult(NSLOCTEXT("VCRest", "Rest", "Rest"), bRested,
		bRested ? NSLOCTEXT("VCRest", "Rested", "You feel rested") : NSLOCTEXT("VCRest", "RestRefused", "You cannot rest here"));
}

void AVCCharacterBase::Server_SleepAt_Implementation(AActor* RestPoint)
{
	if (IsIncapacitated() || !IsRestPointInRange(RestPoint))
	{
		return;
	}
	const bool bSlept = ICGFRestPointInterface::Execute_SleepUntilMorning(RestPoint, this);
	UE_LOG(LogVoxelCharacter, Log, TEXT("%s sleeps at %s: %s"), *GetName(), *RestPoint->GetName(), bSlept ? TEXT("ok") : TEXT("refused"));
	Client_CraftResult(NSLOCTEXT("VCRest", "Sleep", "Sleep"), bSlept,
		bSlept ? NSLOCTEXT("VCRest", "Slept", "A new day") : NSLOCTEXT("VCRest", "SleepRefused", "Not tired yet (sleep at night)"));
}

void AVCCharacterBase::Server_CraftRecipe_Implementation(FPrimaryAssetId RecipeId, AActor* Station)
{
#if WITH_INVENTORY_PLUGIN
	if (IsIncapacitated() || !InventoryComponent)
	{
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UCraftingSubsystem* Crafting = GameInstance ? GameInstance->GetSubsystem<UCraftingSubsystem>() : nullptr;
	const UCraftingRecipe* Recipe = Crafting ? Crafting->FindRecipe(RecipeId) : nullptr;
	if (!Recipe)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("Server_CraftRecipe: unknown recipe %s"), *RecipeId.ToString());
		return;
	}
	// A station only counts when it is a rest point within reach; anything else crafts by hand.
	FGameplayTag StationTag;
	if (Station && IsRestPointInRange(Station))
	{
		StationTag = ICGFRestPointInterface::Execute_GetCraftingStationTag(Station);
	}
	const ECraftResult Result = Crafting->Craft(InventoryComponent, Recipe, StationTag);
	const FText Name = Recipe->DisplayName.IsEmpty() ? FText::FromName(Recipe->OutputItemId.PrimaryAssetName) : Recipe->DisplayName;
	FText Reason;
	switch (Result)
	{
	case ECraftResult::Success:            Reason = NSLOCTEXT("VCCraft", "Crafted", "Crafted {0}"); break;
	case ECraftResult::WrongStation:       Reason = NSLOCTEXT("VCCraft", "WrongStation", "{0} needs a campfire"); break;
	case ECraftResult::MissingIngredients: Reason = NSLOCTEXT("VCCraft", "Missing", "Not enough materials for {0}"); break;
	case ECraftResult::NoRoomForOutput:    Reason = NSLOCTEXT("VCCraft", "NoRoom", "No room for {0}"); break;
	default:                               Reason = NSLOCTEXT("VCCraft", "Failed", "Cannot craft {0}"); break;
	}
	UE_LOG(LogVoxelCharacter, Log, TEXT("%s crafts %s at station '%s': %s"), *GetName(), *RecipeId.PrimaryAssetName.ToString(),
		*StationTag.ToString(), *UEnum::GetValueAsString(Result));
	Client_CraftResult(Name, Result == ECraftResult::Success, FText::Format(Reason, Name));
#endif
}

void AVCCharacterBase::Client_CraftResult_Implementation(const FText& RecipeName, bool bSuccess, const FText& Reason)
{
	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		PC->ShowToast(Reason, bSuccess ? 3.0f : 2.5f);
	}
}

void AVCCharacterBase::Input_Craft(const FInputActionValue& Value)
{
	if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
	{
		PC->ToggleCraftingUI();
	}
}

// ---------------------------------------------------------------------------
// Items across death
// ---------------------------------------------------------------------------

#if WITH_INVENTORY_PLUGIN
namespace
{
	FString ItemToJson(const FItemInstance& Item)
	{
		FString Out;
		const TSharedPtr<FJsonObject> Json = FItemSerializationUtils::SerializeItemInstance(Item);
		if (Json.IsValid())
		{
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Json.ToSharedRef(), Writer);
		}
		return Out;
	}

	FItemInstance ItemFromJson(const FString& JsonText, UObject* Outer)
	{
		TSharedPtr<FJsonObject> Json;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
		if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		{
			return FItemInstance();
		}
		return FItemSerializationUtils::DeserializeItemInstance(Json, Outer);
	}
}
#endif

void AVCCharacterBase::CaptureItemSnapshotToPlayerState()
{
#if WITH_INVENTORY_PLUGIN
	AVCPlayerState* PS = GetPlayerState<AVCPlayerState>();
	if (!HasAuthority() || !PS)
	{
		return;
	}

	FVCItemSnapshot& Snapshot = PS->PendingItemSnapshot;
	BuildItemSnapshot(Snapshot);
	PS->bHasPendingItemSnapshot = !Snapshot.IsEmpty();
	UE_LOG(LogVoxelCharacter, Log, TEXT("CaptureItemSnapshot: %d inventory slots, %d equipped items."), Snapshot.Inventory.Num(), Snapshot.Equipment.Num());
#endif
}

void AVCCharacterBase::BuildItemSnapshot(FVCItemSnapshot& Snapshot) const
{
	Snapshot.Reset();
#if WITH_INVENTORY_PLUGIN
	if (InventoryComponent)
	{
		for (int32 SlotIndex = 0; SlotIndex < InventoryComponent->MaxSlots; ++SlotIndex)
		{
			const FItemInstance Item = InventoryComponent->GetItemInSlot(SlotIndex);
			if (Item.IsValid())
			{
				FVCSnapshotInventoryEntry& Entry = Snapshot.Inventory.AddDefaulted_GetRef();
				Entry.SlotIndex = SlotIndex;
				Entry.ItemJson = ItemToJson(Item);
			}
		}
	}

#if WITH_EQUIPMENT_PLUGIN
	if (EquipmentManager)
	{
		for (const FGameplayTag& SlotTag : EquipmentManager->GetOccupiedSlotTags())
		{
			const FItemInstance Item = EquipmentManager->GetEquippedItem(SlotTag);
			if (Item.IsValid())
			{
				FVCSnapshotEquipmentEntry& Entry = Snapshot.Equipment.AddDefaulted_GetRef();
				Entry.SlotTag = SlotTag;
				Entry.ItemJson = ItemToJson(Item);
			}
		}
	}
#endif

#endif
}

void AVCCharacterBase::ApplyPendingSaveState()
{
	AVCPlayerState* PS = GetPlayerState<AVCPlayerState>();
	if (!HasAuthority() || !PS)
	{
		return;
	}

#if WITH_INVENTORY_PLUGIN
	if (PS->bHasPendingItemSnapshot)
	{
		// Replace, never merge: the save is the truth for what this player carries.
		int32 Dropped = 0;
#if WITH_EQUIPMENT_PLUGIN
		if (EquipmentManager && InventoryComponent)
		{
			for (const FGameplayTag& SlotTag : EquipmentManager->GetOccupiedSlotTags())
			{
				FItemInstance Unequipped;
				if (EquipmentManager->TryUnequip(SlotTag, Unequipped) == EEquipmentResult::Success)
				{
					++Dropped;
				}
			}
		}
#endif
		if (InventoryComponent)
		{
			for (int32 SlotIndex = 0; SlotIndex < InventoryComponent->MaxSlots; ++SlotIndex)
			{
				const FItemInstance Item = InventoryComponent->GetItemInSlot(SlotIndex);
				if (Item.IsValid() && InventoryComponent->TryRemoveItem(Item.InstanceId) == EInventoryOperationResult::Success)
				{
					++Dropped;
				}
			}
		}
		UE_LOG(LogVoxelCharacter, Log, TEXT("ApplyPendingSaveState: dropped %d carried item(s) before restoring the save."), Dropped);
		RestoreItemSnapshotFromPlayerState();
	}
#endif

	FTransform Saved;
	if (PS->ConsumePendingSpawnTransform(Saved))
	{
		SetActorLocationAndRotation(Saved.GetLocation(), Saved.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
		if (UCharacterMovementComponent* Move = GetCharacterMovement())
		{
			Move->StopMovementImmediately();
		}
		UE_LOG(LogVoxelCharacter, Log, TEXT("ApplyPendingSaveState: moved to the saved transform %s."), *Saved.GetLocation().ToCompactString());
		if (bWaitForTerrain)
		{
			// The saved spot was a valid standing position (possibly inside a dungeon): resume there.
			bResumeAtExactLocation = true;
			FreezeForTerrainWait();
			InitiateChunkBasedWait();
		}
	}

	PS->ApplyPendingVitals();
}

bool AVCCharacterBase::RestoreItemSnapshotFromPlayerState()
{
#if WITH_INVENTORY_PLUGIN
	AVCPlayerState* PS = GetPlayerState<AVCPlayerState>();
	if (!HasAuthority() || !PS || !PS->bHasPendingItemSnapshot || !InventoryComponent)
	{
		return false;
	}

	const FVCItemSnapshot Snapshot = PS->PendingItemSnapshot;
	PS->PendingItemSnapshot.Reset();
	PS->bHasPendingItemSnapshot = false;

	int32 Restored = 0;
	for (const FVCSnapshotInventoryEntry& Entry : Snapshot.Inventory)
	{
		const FItemInstance Item = ItemFromJson(Entry.ItemJson, InventoryComponent);
		if (Item.IsValid() && InventoryComponent->TryAddItem(Item, Entry.SlotIndex) == EInventoryOperationResult::Success)
		{
			++Restored;
		}
	}

#if WITH_EQUIPMENT_PLUGIN
	if (EquipmentManager)
	{
		for (const FVCSnapshotEquipmentEntry& Entry : Snapshot.Equipment)
		{
			// Through the inventory so the manager's validation and GAS path run exactly as for a normal equip.
			const FItemInstance Item = ItemFromJson(Entry.ItemJson, InventoryComponent);
			if (!Item.IsValid() || InventoryComponent->TryAddItem(Item) != EInventoryOperationResult::Success)
			{
				continue;
			}
			if (EquipmentManager->TryEquipFromInventory(Item.InstanceId, InventoryComponent, Entry.SlotTag) == EEquipmentResult::Success)
			{
				++Restored;
			}
		}
	}
#endif

	UE_LOG(LogVoxelCharacter, Log, TEXT("RestoreItemSnapshot: %d of %d items restored."), Restored, Snapshot.Inventory.Num() + Snapshot.Equipment.Num());
	return Restored > 0;
#else
	return false;
#endif
}
