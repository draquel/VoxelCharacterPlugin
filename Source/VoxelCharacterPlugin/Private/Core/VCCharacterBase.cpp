// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCCharacterBase.h"
#include "VoxelWorldModeTypes.h"
#include "HAL/IConsoleManager.h"
#include "Core/VCPlayerState.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Core/VCPlayerController.h"
#include "Core/VCGameModeBase.h"
#include "Core/VCSettings.h"
#include "Combat/VCCombatComponent.h"
#include "Combat/VCMeleeAttackAbility.h"
#include "Combat/VCRangedAttackAbility.h"
#include "Combat/VCBlockAbility.h"
#include "Animation/VCActionAnimSet.h"
#include "Animation/VCActionMontageComponent.h"
#include "Animation/VCAppearanceComponent.h"
#include "Animation/VCCharacterAppearance.h"
#include "Animation/VCLocomotionClipSet.h"
#include "Core/VCLocomotionAnimInstance.h"
#include "Editing/VCBlockInteractionComponent.h"
#include "Utilities/CGFCombatStatics.h"
#include "Tags/CGFGameplayTags.h"
#include "Core/VCGameplayTags.h"
#include "Interfaces/CGFRestPointInterface.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/VCCameraManager.h"
#include "Camera/VCFirstPersonCameraMode.h"
#include "Camera/VCThirdPersonCameraMode.h"
#include "Movement/VCMovementComponent.h"
#include "Movement/VCTerrainQueryHelper.h"
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
#include "Core/VCGameplayTags.h"
#include "Core/VCWeatherEffects.h"
#include "VoxelCoordinates.h"
#include "VoxelWorldConfiguration.h"
#include "VoxelChunkManager.h"
#include "VoxelTreeInjector.h"
#include "VoxelSeaSurface.h"
#include "VoxelScatterManager.h"
#include "VoxelCollisionManager.h"
#include "VoxelCharacterPlugin.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
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
	 * Mirror edit mode into the demo actor's runtime switch for its brush (voxel.Edit.PlayerInputs), so a map
	 * whose AVoxelDemoActor has bEnableEditInputs set (the demo) obeys the same toggle instead of polling the
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
		const UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(World);
		return ChunkMgr && UVCBlockInteractionComponent::ShowsBlockFeedback(ChunkMgr->GetConfiguration());
	}

	/** vc.Anim.Locomotion changed: every player in play takes the locomotion it now names. */
	void OnLocomotionChanged(IConsoleVariable* Variable)
	{
		if (!GEngine)
		{
			return;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (UWorld* World = Context.World(); World && World->IsGameWorld())
			{
				for (TActorIterator<AVCCharacterBase> It(World); It; ++It)
				{
					It->ApplyLocomotionAnimClass();
				}
			}
		}
	}

	TAutoConsoleVariable<int32> CVarAnimLocomotion(
		TEXT("vc.Anim.Locomotion"), 2,
		TEXT("The player's locomotion: 0 the player Blueprint's own anim class (ABP_VoxelCharacter), 1 motion matching (UVCSettings::PlayerLocomotionAnimClass, character animation P5, kept for comparison), 2 the state machine over the packs' clips (UVCSettings::PlayerStateMachineAnimClass, the locomotion state machine plan; the default since its L4, D86 reversing D44); a class that does not load plays the Blueprint's. Applies at once to every player in play."),
		FConsoleVariableDelegate::CreateStatic(&OnLocomotionChanged),
		ECVF_Default);

	/** vc.Anim.MotionMatching, the switch's name before the state machine: sets vc.Anim.Locomotion to its value. */
	void OnMotionMatchingAliasChanged(IConsoleVariable* Variable)
	{
		CVarAnimLocomotion->Set(Variable->GetInt(), ECVF_SetByConsole);
	}

	TAutoConsoleVariable<int32> CVarAnimMotionMatching(
		TEXT("vc.Anim.MotionMatching"), 2,
		TEXT("The old name of vc.Anim.Locomotion (0 the Blueprint's anim class, 1 motion matching, 2 the state machine): setting it sets that switch. Read vc.Anim.Locomotion for the locomotion in force."),
		FConsoleVariableDelegate::CreateStatic(&OnMotionMatchingAliasChanged),
		ECVF_Default);
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

		// The quiver, worn on the back with its opening over the right shoulder blade (the bow pack's reload from the back,
		// D97). It shares the back's place with the back's sheath: only a bow can be there with it (its fragment's
		// SharesPlaceWith*), so a shield or a two-hander sends it to the bag. The item's AttachOffset fits it on the bone.
		FEquipmentSlotDefinition Quiver;
		Quiver.SlotTag = CGFGameplayTags::Equipment_Slot_Quiver;
		Quiver.SlotDisplayName = NSLOCTEXT("VoxelCharacter", "Quiver", "Quiver");
		Quiver.AttachSocket = FName("spine_05");
		Quiver.AcceptedItemTags.AddTag(CGFGameplayTags::Item_Category_Quiver);
		EquipmentManager->AvailableSlots.Add(Quiver);
		// The arrows standing in it (D100), for the pack's quiver (its opening up +Z, the rim at 18 cm, the tube 6.7 cm out
		// along +Y): heads down inside, the nocks 12 cm above the rim, fanned a little.
		const FVector4f Fan[] = { FVector4f(-3.f, 5.f, -4.f, 3.f), FVector4f(3.f, 5.5f, 4.f, 2.f), FVector4f(-1.5f, 9.f, -2.f, -4.f), FVector4f(2.f, 8.5f, 3.f, -3.f) };
		for (const FVector4f& Arrow : Fan)
		{
			QuiverArrowLayout.Add(FTransform(FRotator(Arrow.W, 0.f, 180.f + Arrow.Z), FVector(Arrow.X, Arrow.Y, 30.f)));
		}

		// The sheaths (the equipment sheathing plan, D78), in the cycle's order: a weapon carried on the body, drawn into
		// the main hand. Each item's SheathOffset fits it on the bone; the slot's correction stays the identity. Each is
		// one place on the body and holds one item (the sheath loadout plan, D95): the back carries a bow, a two-hander
		// or a shield, and draws a shield into the off hand (its item's hand) with the one-handed weapon (D96).
		auto AddSheath = [this](const FGameplayTag& Tag, const FText& Name, FName Bone)
		{
			FEquipmentSlotDefinition Sheath;
			Sheath.SlotTag = Tag;
			Sheath.SlotDisplayName = Name;
			Sheath.AttachSocket = Bone;
			Sheath.DrawSlotTag = CGFGameplayTags::Equipment_Slot_MainHand;
			Sheath.AcceptedItemTags.AddTag(FGameplayTag::RequestGameplayTag(FName("Item.Category.Weapon")));
			EquipmentManager->AvailableSlots.Add(Sheath);
		};
		AddSheath(CGFGameplayTags::Equipment_Slot_Sheath_Belt, NSLOCTEXT("VoxelCharacter", "SheathBelt", "Belt"), FName("pelvis"));
		AddSheath(CGFGameplayTags::Equipment_Slot_Sheath_HipLeft, NSLOCTEXT("VoxelCharacter", "SheathHipLeft", "Left Hip"), FName("pelvis"));
		AddSheath(CGFGameplayTags::Equipment_Slot_Sheath_Back, NSLOCTEXT("VoxelCharacter", "SheathBack", "Back"), FName("spine_05"));
	}
#endif

#if WITH_INVENTORY_PLUGIN
	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("InventoryComponent"));
#endif

	// --- Combat ---
	CombatComponent = CreateDefaultSubobject<UVCCombatComponent>(TEXT("CombatComponent"));
	ActionMontages = CreateDefaultSubobject<UVCActionMontageComponent>(TEXT("ActionMontages"));
	AppearanceComponent = CreateDefaultSubobject<UVCAppearanceComponent>(TEXT("Appearance"));
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
// IVCAnimatedCharacter
// ---------------------------------------------------------------------------

EVCViewMode AVCCharacterBase::GetAnimViewMode() const
{
	// The view mode is the owning player's alone (G87): every other machine draws the pawn in third person.
	return IsLocallyControlled() ? CurrentViewMode : EVCViewMode::ThirdPerson;
}

const UVCCombatComponent* AVCCharacterBase::GetAnimCombatComponent() const
{
	return CombatComponent;
}

FGameplayTag AVCCharacterBase::PickActionSet(bool bHoldsItem, const FGameplayTag& ItemActionSet)
{
	if (!bHoldsItem)
	{
		return VCGameplayTags::Equipment_AnimSet_Unarmed.GetTag();
	}
	const FGameplayTag Root = VCGameplayTags::Equipment_AnimSet.GetTag();
	return ItemActionSet.MatchesTag(Root) && ItemActionSet != Root ? ItemActionSet : VCGameplayTags::Equipment_AnimSet_Tool.GetTag();
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
	ApplyLocomotionAnimClass();

	// The body, once the voxel world is up (a pawn possessed before play began; a later one does it at possession)
	if (AVCPlayerState* VCPS = GetPlayerState<AVCPlayerState>())
	{
		VCPS->OnAppearanceChanged.AddUniqueDynamic(this, &AVCCharacterBase::HandleAppearanceChanged);
		if (HasAuthority())
		{
			VCPS->ResolveAppearance();
		}
		ApplyPlayerAppearance();
	}

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
		EquipmentManager->OnEquipmentChanged.AddDynamic(this, &AVCCharacterBase::HandleEquipmentChanged);
		EquipmentManager->OnDrawnChanged.AddDynamic(this, &AVCCharacterBase::HandleEquipmentDrawnChanged);
	}
#endif
#if WITH_INVENTORY_PLUGIN
	if (InventoryComponent)
	{
		InventoryComponent->OnInventoryChanged.AddDynamic(this, &AVCCharacterBase::HandleInventoryChangedForReload);
	}
#endif
	RefreshActionSet();

	// The player's montage sets come from the project settings unless the Blueprint lists its own.
	if (ActionMontages && ActionMontages->ActionAnimSets.IsEmpty())
	{
		for (const TSoftObjectPtr<UVCActionAnimSet>& Set : UVCSettings::Get().PlayerActionAnimSets)
		{
			if (UVCActionAnimSet* Loaded = Set.LoadSynchronous())
			{
				ActionMontages->ActionAnimSets.Add(Loaded);
			}
			else if (!Set.IsNull())
			{
				UE_LOG(LogVoxelCharacter, Warning, TEXT("%s: the action set %s does not load (Scripts/setup_character_layers.py builds it)"),
					*GetName(), *Set.ToString());
			}
		}
	}

	if (CombatComponent)
	{
		CombatComponent->OnDied.AddDynamic(this, &AVCCharacterBase::HandleDied);
		CombatComponent->OnDowned.AddDynamic(this, &AVCCharacterBase::HandleDowned);
		CombatComponent->OnDamaged.AddDynamic(this, &AVCCharacterBase::HandleDamagedForReload);
	}

	// --- Underwater Post-Process (the local player's view only; again on each controller change, G133) ---
	UpdateUnderwaterPostProcess();

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

static TAutoConsoleVariable<int32> CVarWeatherLog(
	TEXT("vc.Weather.Log"), 0,
	TEXT("Log each player's wet level, air temperature and weather states every half second (the weather plan's P5)"),
	ECVF_Default);

bool AVCCharacterBase::IsWet() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	return ASC && ASC->HasMatchingGameplayTag(VCGameplayTags::State_Weather_Wet);
}

bool AVCCharacterBase::IsCold() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	return ASC && ASC->HasMatchingGameplayTag(VCGameplayTags::State_Weather_Cold);
}

bool AVCCharacterBase::HysteresisOn(bool bWasOn, float Value, float OnAt, float OffAt, bool bHighIsOn)
{
	if (bHighIsOn)
	{
		return bWasOn ? Value > OffAt : Value >= OnAt;
	}
	return bWasOn ? Value < OffAt : Value <= OnAt;
}

void AVCCharacterBase::UpdateWeatherEffects()
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	const UVCMovementComponent* Movement = Cast<UVCMovementComponent>(GetCharacterMovement());
	if (!ASC || !Movement)
	{
		return;
	}
	using namespace VCGameplayTags;
	// The states are read back from the ability system, not kept here: a player's lives on the player state and
	// outlives this avatar, so a respawn finds the old tags and clears them if the new body is dry and warm.
	const bool bAlive = !bIsCorpse && !IsIncapacitated();
	const bool bWasWet = ASC->HasMatchingGameplayTag(State_Weather_Wet);
	const bool bWasCold = ASC->HasMatchingGameplayTag(State_Weather_Cold);
	const bool bWet = bAlive && HysteresisOn(bWasWet, Movement->GetWeatherWetLevel(), WetOnLevel, WetOffLevel, true);
	const bool bCold = bAlive && HysteresisOn(bWasCold, Movement->GetWeatherTemperatureC(), ColdBelowC, ColdBelowC + ColdHysteresisC, false);
	if (bWet != bWasWet || bCold != bWasCold || (CVarWeatherLog.GetValueOnGameThread() != 0 && IsPlayerControlled()))
	{
		UE_LOG(LogVoxelCharacter, Log, TEXT("%s weather: %s, %s (wet level %.2f, %.1f C%s)"), *GetName(),
			bWet ? TEXT("soaked") : TEXT("dry"), bCold ? TEXT("cold") : TEXT("warm"), Movement->GetWeatherWetLevel(), Movement->GetWeatherTemperatureC(),
			Movement->IsSwimming() ? TEXT(", swimming") : TEXT(""));
	}
	if (bWet != bWasWet)
	{
		// Loose tags replicated as tags (no count): the owning client's widget and the simulated proxies see them.
		if (bWet)
		{
			ASC->AddLooseGameplayTag(State_Weather_Wet, 1, EGameplayTagReplicationState::TagOnly);
		}
		else
		{
			ASC->SetLooseGameplayTagCount(State_Weather_Wet, 0, EGameplayTagReplicationState::TagOnly);
		}
	}
	if (bCold != bWasCold)
	{
		if (bCold)
		{
			ASC->AddLooseGameplayTag(State_Weather_Cold, 1, EGameplayTagReplicationState::TagOnly);
			FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
			Context.AddSourceObject(this);
			ASC->ApplyGameplayEffectToSelf(GetDefault<UVCWeatherColdEffect>(), 1.f, Context);
		}
		else
		{
			ASC->SetLooseGameplayTagCount(State_Weather_Cold, 0, EGameplayTagReplicationState::TagOnly);
			// By class, not a handle: a respawned player's effect was applied to the same ability system by the last body.
			ASC->RemoveActiveGameplayEffectBySourceEffect(UVCWeatherColdEffect::StaticClass(), nullptr);
		}
	}
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
	// A press kept during an attack's swing plays the next attack at its recovery (D104).
	if (ActionMontages && IsLocallyControlled() && ActionMontages->TakeDueBufferedAttack())
	{
		StartMeleeSwing();
	}
	UpdateDrawnGrip();
	UpdateSheathHandOff();
	UpdateBowReload();
	UpdateHeldArrow();

	// The weather on the character (the weather plan's P5): the server sets the wet and cold states every half second.
	if (HasAuthority())
	{
		WeatherEffectsTimer += DeltaSeconds;
		if (WeatherEffectsTimer >= 0.5f)
		{
			WeatherEffectsTimer = 0.f;
			UpdateWeatherEffects();
		}
	}

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

#if !UE_BUILD_SHIPPING
	if (bShowVoxelDebug && IsLocallyControlled())
	{
		DrawVoxelDebugInfo();
	}
#endif
}

// ---------------------------------------------------------------------------
// Replication
// ---------------------------------------------------------------------------

void AVCCharacterBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVCCharacterBase, bIsCorpse);
	DOREPLIFETIME(AVCCharacterBase, bIsAiming);
	DOREPLIFETIME(AVCCharacterBase, bArrowNocked);
	DOREPLIFETIME(AVCCharacterBase, ArrowMesh);
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

	// The body: the server resolves the player's appearance (it replicates to the clients) and wears it. A pawn possessed
	// before play begins (the first spawn of a PIE session) waits for BeginPlay: the voxel world's meshing mode is known then.
	if (AVCPlayerState* VCPS = GetPlayerState<AVCPlayerState>())
	{
		VCPS->OnAppearanceChanged.AddUniqueDynamic(this, &AVCCharacterBase::HandleAppearanceChanged);
		if (HasActorBegunPlay())
		{
			VCPS->ResolveAppearance();
			ApplyPlayerAppearance();
		}
	}
}

void AVCCharacterBase::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();

	// Possessed, unpossessed, or the controller arrived by replication (the owning client's OnRep_Controller): the
	// view tint follows the local player controller. Before BeginPlay, BeginPlay does it.
	if (HasActorBegunPlay())
	{
		UpdateUnderwaterPostProcess();
	}
}

void AVCCharacterBase::UpdateUnderwaterPostProcess()
{
	// The tint is a view effect (G133): it exists only on the machine whose player views through this pawn. A pawn with
	// no local player controller (another player's character on a client, an NPC, every pawn on a dedicated server) has
	// no view to tint, and its unbound volume would tint the local player's; several such pawns stacked theirs.
	const bool bWanted = UVCUnderwaterPostProcess::IsLocalPlayerView(this);
	if (bWanted && !UnderwaterPostProcess)
	{
		UnderwaterPostProcess = NewObject<UVCUnderwaterPostProcess>(this, TEXT("UnderwaterPostProcess"));
		UnderwaterPostProcess->RegisterComponent();
	}
	else if (!bWanted && UnderwaterPostProcess)
	{
		UnderwaterPostProcess->DestroyComponent();
		UnderwaterPostProcess = nullptr;
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

		// The body the server resolved, now and whenever it changes
		PS->OnAppearanceChanged.AddUniqueDynamic(this, &AVCCharacterBase::HandleAppearanceChanged);
		ApplyPlayerAppearance();
	}
}

void AVCCharacterBase::HandleAppearanceChanged()
{
	ApplyPlayerAppearance();
}

void AVCCharacterBase::ApplyPlayerAppearance()
{
	const AVCPlayerState* PS = GetPlayerState<AVCPlayerState>();
	if (!PS || !AppearanceComponent)
	{
		return;
	}
	const UVCCharacterAppearance* Wanted = PS->GetAppearance().LoadSynchronous();
	if (bAppearanceApplied && AppearanceComponent->GetAppearance() == Wanted)
	{
		return;
	}
	if (!PS->GetAppearance().IsNull() && !Wanted)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("%s: the appearance %s does not load (Scripts/setup_character_appearances.py builds it)"),
			*GetName(), *PS->GetAppearance().ToString());
	}
	AppearanceComponent->ApplyAppearance(Wanted);
	bAppearanceApplied = true;
	ApplyLocomotionAnimClass();
	RelinkEquipmentAnimLayers();
	UpdateEquipmentAttachments(); // the new body's corrections (D71); the visuals rode the mesh swap by bone name
	UpdateMeshVisibility();
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
	if (PS->RangedAttackAbilityClass)
	{
		ASC->GiveAbility(FGameplayAbilitySpec(PS->RangedAttackAbilityClass, 1, INDEX_NONE, this));
	}
	if (PS->BlockAbilityClass)
	{
		ASC->GiveAbility(FGameplayAbilitySpec(PS->BlockAbilityClass, 1, INDEX_NONE, this));
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
	if (!GetWorld())
	{
		return nullptr;
	}
	// The same sweep the server-side ability runs (UVCCombatStatics::SweepHostileUnderView): eyes + control rotation, not the
	// third-person camera (which sits metres behind the pawn and lags the control rotation by a frame).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VCMeleeTarget), false, this);
	const FVCViewSweep Sweep = UVCCombatStatics::SweepHostileUnderView(this, AttackTargetRange, AttackTargetProbeRadius, Params);
	if (!Sweep.bHit)
	{
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s melee target: nothing within %.0f uu from %s toward %s."), *GetName(), AttackTargetRange,
			*Sweep.ViewLocation.ToCompactString(), *Sweep.ViewRotation.ToCompactString());
		return nullptr;
	}
	if (!Sweep.Hostile)
	{
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s melee target: the crosshair's sweep met %s (%s) at %s, %.0f uu, from %s toward %s."), *GetName(),
			*GetNameSafe(Sweep.Hit.GetActor()), *GetNameSafe(Sweep.Hit.GetComponent()), *Sweep.Hit.ImpactPoint.ToCompactString(), Sweep.Hit.Distance,
			*Sweep.ViewLocation.ToCompactString(), *Sweep.ViewRotation.ToCompactString());
		return nullptr;
	}
	return Sweep.Hostile;
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
	return StartMeleeSwing();
}

bool AVCCharacterBase::StartMeleeSwing()
{
	if (!CombatComponent || IsIncapacitated())
	{
		return false;
	}
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC)
	{
		return false;
	}

	// The owner swings at once (D49); the server's swing plays for everyone else and lands the hit at the montage's hit
	// time. A press goes by the combo's rule (D104), as the server's acceptance does: nothing in the wind-up, kept from the
	// strike until the recovery (Tick plays it then), the next attack from the recovery on.
	int32 Variant = -1;
	if (ActionMontages && ActionMontages->GetMontage(EVCAnimAction::Attack, 0))
	{
		switch (ActionMontages->ClassifyAttackPress())
		{
		case EVCAttackPress::Ignore:
			return true;
		case EVCAttackPress::Buffer:
			ActionMontages->BufferAttack();
			return true;
		default:
			break;
		}
		const AVCPlayerState* PS = GetPlayerState<AVCPlayerState>();
		const UVCMeleeAttackAbility* Melee = PS && PS->MeleeAttackAbilityClass
			? Cast<UVCMeleeAttackAbility>(PS->MeleeAttackAbilityClass->GetDefaultObject()) : nullptr;
		float PlayRate = 1.f;
		Variant = ActionMontages->ChooseAttack(Melee ? Melee->GetAttackSpeed(this) : 1.f, PlayRate);
		ActionMontages->PlayActionLocal(EVCAnimAction::Attack, Variant, PlayRate);
	}

	// The ability is ServerOnly: the server does its own sweep, so a client cannot fabricate a hit. The request carries
	// the owner's combo index, which the server takes when its own rule allows it (G179).
	if (HasAuthority())
	{
		return ActivateMeleeSwing(Variant);
	}
	Server_RequestMeleeSwing(static_cast<int8>(FMath::Clamp(Variant, -1, 127)));
	return true;
}

bool AVCCharacterBase::ActivateMeleeSwing(int32 ComboIndex)
{
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC || !CombatComponent || IsIncapacitated())
	{
		return false;
	}
	if (ActionMontages)
	{
		ActionMontages->SetRequestedAttack(ComboIndex);
	}
	return ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(CGFGameplayTags::Ability_Attack_Melee));
}

void AVCCharacterBase::Server_RequestMeleeSwing_Implementation(int8 ComboIndex)
{
	// The ability checks the rest (the combo's timing, the attacker's state); an index out of the combo is the server's to pick.
	ActivateMeleeSwing(ComboIndex);
}

bool AVCCharacterBase::TryStartRangedAttack()
{
	if (!UVCRangedAttackAbility::HoldsRangedWeapon(this))
	{
		return false;
	}
	if (bDrawingRangedWeapon || !CombatComponent || IsIncapacitated())
	{
		return true;
	}
	// The owner's check; the server's activation runs the same one.
	if (!UVCRangedAttackAbility::CanStartDraw(this))
	{
		// Arrows but none nocked yet: the draw starts at the nock if the press is still held (D98).
		if (UVCRangedAttackAbility::HasAmmoToLoad(this))
		{
			bDrawWaitsForNock = true;
			return true;
		}
		if (AVCPlayerController* PC = Cast<AVCPlayerController>(GetController()))
		{
			PC->ShowToast(NSLOCTEXT("VCHud", "NoArrows", "No arrows"), 2.0f);
		}
		return true;
	}
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	// ServerOnly, as the melee attack: on a client this asks the server, which times the draw from its activation.
	if (!ASC || !ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(VCGameplayTags::Ability_Attack_Ranged)))
	{
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s: the ranged attack did not start"), *GetName());
		return true;
	}
	bDrawingRangedWeapon = true;
	OwnDrawStartTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	ApplyAimState(); // a draw aims (C3): the facing, the camera and the reticle at once on the owner
	PlayOwnAction(EVCAnimAction::Draw);
	return true;
}

void AVCCharacterBase::ReleaseRangedAttack()
{
	// Let go before the nock: no draw waits any more.
	bDrawWaitsForNock = false;
	if (!bDrawingRangedWeapon)
	{
		return;
	}
	bDrawingRangedWeapon = false;
	ApplyAimState(); // the draw ended: the aim stays only while the secondary action is held (C3)
	// The owner's look follows its own clock; the server's measure decides the shot.
	if (ActionMontages && IsLocallyControlled())
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : OwnDrawStartTime;
		if (UVCRangedAttackAbility::GetDrawPower(this, static_cast<float>(Now - OwnDrawStartTime)) > 0.f)
		{
			ActionMontages->PlayActionLocal(EVCAnimAction::Release);
		}
		else
		{
			ActionMontages->StopActionLocal(EVCAnimAction::Draw);
		}
	}
	UVCRangedAttackAbility::ReleaseDraw(GetAbilitySystemComponent());
}

void AVCCharacterBase::SetFacesAim(bool bFaceAim)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Move || CurrentViewMode == EVCViewMode::FirstPerson)
	{
		return;
	}
	// The voxel movement eases the flip (C4): the capsule turns toward the view (or back toward where it moves) first,
	// and the mode changes once the body is nearly there, so the mesh is not left 180 degrees behind an instant yaw.
	if (UVCMovementComponent* VoxelMove = Cast<UVCMovementComponent>(Move))
	{
		VoxelMove->SetFacesAim(bFaceAim);
		return;
	}
	// The engine's turn toward the controller's rotation keeps the body upright in a custom gravity frame.
	Move->bOrientRotationToMovement = !bFaceAim;
	Move->bUseControllerDesiredRotation = bFaceAim;
}

// ---------------------------------------------------------------------------
// Aiming (the character refinement plan's C3, D73)
// ---------------------------------------------------------------------------

bool AVCCharacterBase::IsAiming() const
{
	if (bIsAiming)
	{
		return true;
	}
	// The owner counts its own request and draw before the server answers; the server its own draw before it replicates.
	if (IsLocallyControlled() && (bLocalWantsToAim || bDrawingRangedWeapon))
	{
		return true;
	}
	return HasAuthority() && (bServerWantsToAim || bServerDrawing);
}

void AVCCharacterBase::SetWantsToAim(bool bWantsToAim)
{
	if (!IsLocallyControlled())
	{
		return;
	}
	// Nothing to aim without a ranged weapon; a release always lands (the weapon may have gone meanwhile).
	if (bWantsToAim && (IsIncapacitated() || !UVCRangedAttackAbility::HoldsRangedWeapon(this)))
	{
		bWantsToAim = false;
	}
	if (bLocalWantsToAim == bWantsToAim)
	{
		return;
	}
	bLocalWantsToAim = bWantsToAim;
	if (HasAuthority())
	{
		bServerWantsToAim = bWantsToAim;
		UpdateServerAiming();
	}
	else
	{
		Server_SetWantsToAim(bWantsToAim);
		ApplyAimState();
	}
}

void AVCCharacterBase::Server_SetWantsToAim_Implementation(bool bWantsToAim)
{
	// The server's own check: a ranged weapon in hand, the character able.
	bServerWantsToAim = bWantsToAim && !IsIncapacitated() && UVCRangedAttackAbility::HoldsRangedWeapon(this);
	UpdateServerAiming();
}

void AVCCharacterBase::SetDrawingOnServer(bool bDrawing)
{
	if (!HasAuthority() || bServerDrawing == bDrawing)
	{
		return;
	}
	bServerDrawing = bDrawing;
	UpdateServerAiming();
}

void AVCCharacterBase::UpdateServerAiming()
{
	if (!HasAuthority())
	{
		return;
	}
	const bool bNow = bServerWantsToAim || bServerDrawing;
	if (bIsAiming != bNow)
	{
		bIsAiming = bNow;
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
		{
			ASC->SetLooseGameplayTagCount(VCGameplayTags::State_Aiming, bNow ? 1 : 0, EGameplayTagReplicationState::TagOnly);
		}
	}
	ApplyAimState();
}

void AVCCharacterBase::OnRep_IsAiming()
{
	ApplyAimState();
}

void AVCCharacterBase::ApplyAimState()
{
	const bool bAiming = IsAiming();
	// The held set's aim ceiling (in first person too: the server does not know the owner's view mode).
	if (UVCMovementComponent* VCMovement = Cast<UVCMovementComponent>(GetCharacterMovement()))
	{
		VCMovement->bAimingForGait = bAiming;
	}
	// The body toward the view while aiming (third person; the movement eases the flip), back to its heading after.
	SetFacesAim(bAiming);
	if (bAimStateApplied == bAiming)
	{
		return;
	}
	bAimStateApplied = bAiming;
	// The local player's camera: in over the shoulder while aiming in third person, back when it ends. First person keeps
	// its camera (nothing to move in; the reticle shows there too).
	if (IsLocallyControlled() && CameraManager && CurrentViewMode != EVCViewMode::FirstPerson && CameraManager->AimModeClass)
	{
		const bool bAimCameraOn = CameraManager->GetTopCameraModeClass() == CameraManager->AimModeClass;
		if (bAiming && !bAimCameraOn)
		{
			CameraManager->PushCameraMode(CameraManager->AimModeClass);
		}
		else if (!bAiming && bAimCameraOn)
		{
			CameraManager->PopCameraMode(CameraManager->ThirdPersonModeClass);
		}
	}
}

float AVCCharacterBase::GetOwnDrawPower() const
{
	if (!bDrawingRangedWeapon || !GetWorld())
	{
		return 0.f;
	}
	return UVCRangedAttackAbility::GetDrawPower(this, static_cast<float>(GetWorld()->GetTimeSeconds() - OwnDrawStartTime));
}

bool AVCCharacterBase::IsAimOnHostile(float Range) const
{
	UWorld* World = GetWorld();
	FVector Origin, Direction;
	if (!World || !UVCCombatStatics::ComputeAimRay(this, Origin, Direction))
	{
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VCAimOnHostile), /*bTraceComplex*/ false, this);
	FHitResult Hit;
	if (!World->SweepSingleByChannel(Hit, Origin, Origin + Direction * Range, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(4.f), Params))
	{
		return false;
	}
	const UVCCombatComponent* Target = Hit.GetActor() ? UVCCombatStatics::FindCombatComponent(Hit.GetActor()) : nullptr;
	return Target && CombatComponent && UCGFCombatStatics::AreHostileFactions(CombatComponent->FactionTag, Target->FactionTag)
		&& !Target->IsDead_Implementation();
}

void AVCCharacterBase::PlayOwnAction(EVCAnimAction Action)
{
	if (ActionMontages && IsLocallyControlled())
	{
		ActionMontages->PlayActionLocal(Action);
	}
}

void AVCCharacterBase::ShareOwnAction(EVCAnimAction Action)
{
	if (ActionMontages && HasAuthority())
	{
		ActionMontages->PlayActionForEveryone(Action, 0, 1.f, /*bOwnerPlayedIt*/ true);
	}
}

void AVCCharacterBase::Incapacitate()
{
	// Down or dead: the string is empty and no reload runs (D101).
	if (HasAuthority())
	{
		CancelReload(0.f);
		SetArrowNocked(false);
	}
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
	// A view toggle mid-aim keeps the body on the view and moves the camera with the mode (G155, C3).
	bAimStateApplied = false;
	ApplyAimState();

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
}

void AVCCharacterBase::UpdateMeshVisibility()
{
	const bool bIsLocalFP = IsLocallyControlled() && CurrentViewMode == EVCViewMode::FirstPerson;

	if (GetMesh())
	{
		GetMesh()->SetOwnerNoSee(bIsLocalFP);
		GetMesh()->bCastHiddenShadow = bIsLocalFP;
	}
	if (AppearanceComponent)
	{
		AppearanceComponent->SetPartsOwnerNoSee(bIsLocalFP, bIsLocalFP);
		AppearanceComponent->SetBodyHiddenFromView(bIsLocalFP); // no correctives for a body its player cannot see (G148)
	}

	if (FirstPersonArmsMesh)
	{
		FirstPersonArmsMesh->SetVisibility(bIsLocalFP);
	}
	UpdateEquipmentVisualVisibility();
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
	const UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld());
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

		// A sheath's weapon sits where this machine's unsheathe or sheathe montage has it until the montage's hand-off (at
		// the sheath while an unsheathe reaches for it, in the hand while a sheathe carries it back), else where its state
		// says (the equipment sheathing plan, D79). In the hand it takes the hand slot's view mapping.
		const bool bSheath = Slot.DrawSlotTag.IsValid();
		bool bInHand = Slot.IsInHand();
		EVCAnimAction PendingAction;
		if (bSheath && ActionMontages && ActionMontages->IsSheathHandOffPending(Slot.SlotTag, PendingAction))
		{
			bInHand = PendingAction == EVCAnimAction::Sheathe;
		}
		const FGameplayTag ViewSlot = bSheath && bInHand ? Slot.DrawSlotTag : Slot.SlotTag;

		USkeletalMeshComponent* TargetMesh = GetTargetMeshForSlot(ViewSlot);
		if (!TargetMesh)
		{
			continue;
		}

		// Determine the correct socket for the current view mode; an item that chooses its own bone (a bow on the left
		// hand) keeps it in both views.
		const FName StateSocket = EquipmentManager->GetVisualAttachSocketForState(Slot.SlotTag, bInHand);
		const FName SlotSocket = bSheath && bInHand ? EquipmentManager->GetVisualAttachSocketForState(Slot.DrawSlotTag, true) : Slot.AttachSocket;
		const bool bOwnBone = StateSocket != SlotSocket;
		FName SocketName = StateSocket;
		for (const FVCEquipmentSocketMapping& Mapping : EquipmentSocketMappings)
		{
			if (Mapping.SlotTag == ViewSlot && !bOwnBone)
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
		// The snap wipes the visual's offset: re-apply the slot / item fit (or the set's drawn grip, as far as the draw
		// plays), the body's correction for the bone (D71) and any QA nudge.
		const FTransform* Nudge = EquipFitNudges.Find(Slot.SlotTag.GetTagName());
		FTransform Fit = EquipmentManager->GetVisualAttachOffsetForState(Slot.SlotTag, bInHand);
		if (bInHand && ViewSlot == CGFGameplayTags::Equipment_Slot_MainHand.GetTag())
		{
			const float DrawWeight = GetDrawnGripWeight();
			if (DrawWeight > 0.f)
			{
				const UVCActionAnimSet* Set = ActionMontages ? ActionMontages->GetCurrentActionAnimSet() : nullptr;
				FTransform Blended;
				Blended.Blend(Fit, Set->DrawnGrip, DrawWeight);
				Fit = Blended;
			}
		}
		Slot.AttachedVisualComponent->SetRelativeTransform(ComposeAttachTransform(
			Fit,
			TargetMesh == GetMesh() ? GetBodyAttachCorrection(SocketName) : FTransform::Identity,
			Nudge ? *Nudge : FTransform::Identity));
	}
	UpdateEquipmentVisualVisibility();
#endif
}

float AVCCharacterBase::GetDrawnGripWeight() const
{
	const UVCActionAnimSet* Set = ActionMontages ? ActionMontages->GetCurrentActionAnimSet() : nullptr;
	if (!Set || !Set->bHasDrawnGrip || !GetMesh())
	{
		return 0.f;
	}
	const UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	return Anim ? FMath::Clamp(Anim->GetSlotMontageGlobalWeight(TEXT("UpperBody")), 0.f, 1.f) : 0.f;
}

void AVCCharacterBase::UpdateDrawnGrip()
{
	const float Weight = GetDrawnGripWeight();
	if (Weight > 0.f || LastDrawnGripWeight > 0.f)
	{
		UpdateEquipmentAttachments();
	}
	LastDrawnGripWeight = Weight;
}

FTransform AVCCharacterBase::ComposeAttachTransform(const FTransform& Fit, const FTransform& BodyCorrection, const FTransform& Nudge)
{
	// A * B applies A then B: the fit in the bone's space, the correction moving the fitted item, the nudge last.
	return Fit * BodyCorrection * Nudge;
}

FTransform AVCCharacterBase::GetBodyAttachCorrection(FName Bone) const
{
	const UVCCharacterAppearance* Appearance = AppearanceComponent ? AppearanceComponent->GetAppearance() : nullptr;
	return Appearance ? Appearance->GetAttachCorrection(Bone) : FTransform::Identity;
}

void AVCCharacterBase::UpdateEquipmentVisualVisibility()
{
#if WITH_EQUIPMENT_PLUGIN
	if (!EquipmentManager)
	{
		return;
	}
	const bool bIsLocalFP = IsLocallyControlled() && CurrentViewMode == EVCViewMode::FirstPerson;
	for (FEquipmentSlot& Slot : EquipmentManager->EquipmentSlots)
	{
		if (!Slot.AttachedVisualComponent)
		{
			continue;
		}
		if (UPrimitiveComponent* Visual = Cast<UPrimitiveComponent>(Slot.AttachedVisualComponent.Get()))
		{
			// Hidden with the body it hangs on; a visual on the arms mesh is the first-person view's own.
			const bool bOnBody = Visual->GetAttachParent() == GetMesh();
			Visual->SetOwnerNoSee(bIsLocalFP && bOnBody);
			Visual->bCastHiddenShadow = bIsLocalFP && bOnBody;
		}
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

	// Re-attach to correct mesh for current view mode
	UpdateEquipmentAttachments();
#endif
}

void AVCCharacterBase::HandleItemUnequipped(const FItemInstance& Item, FGameplayTag SlotTag)
{
#if WITH_EQUIPMENT_PLUGIN
	UE_LOG(LogVoxelCharacter, Log, TEXT("Item unequipped from slot %s"), *SlotTag.ToString());
#endif
}

void AVCCharacterBase::HandleEquipmentChanged()
{
	RefreshActionSet();

	// The reload's gate (D98): a ranged weapon in the hand. Leaving the hand (sheathed, swapped, dropped) empties the
	// string; coming into it, the unsheathe's end plays before the reload starts.
	const bool bWasInHand = bRangedWeaponInHand;
	bRangedWeaponInHand = UVCRangedAttackAbility::HoldsRangedWeapon(this);
	if (HasAuthority())
	{
		if (!bRangedWeaponInHand)
		{
			CancelReload(0.f);
			SetArrowNocked(false);
		}
		else if (!bWasInHand && GetWorld())
		{
			NextReloadTime = FMath::Max(NextReloadTime, GetWorld()->GetTimeSeconds() + ReloadAfterShotSeconds);
		}
		RefreshArrowMesh();
	}
	UpdateQuiverArrows();
}

void AVCCharacterBase::RefreshActionSet()
{
	bool bHoldsItem = false;
	FGameplayTag ItemActionSet;
#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
	// What is in the hand: the hand slot's own item or the weapon drawn from a sheath; all sheathed is Unarmed (D80).
	const FItemInstance Item = EquipmentManager ? EquipmentManager->GetHeldItem(CGFGameplayTags::Equipment_Slot_MainHand) : FItemInstance();
	if (Item.IsValid())
	{
		bHoldsItem = true;
		const UGameInstance* GI = GetGameInstance();
		const UItemDatabaseSubsystem* ItemDB = GI ? GI->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
		const UItemDefinition* Def = ItemDB ? ItemDB->GetDefinition(Item.ItemDefinitionId) : nullptr;
		if (const UItemFragment_Equipment* EquipFrag = Def ? Def->FindFragment<UItemFragment_Equipment>() : nullptr)
		{
			ItemActionSet = EquipFrag->ActionSetTag;
		}
	}
#endif
	ActiveActionSet = PickActionSet(bHoldsItem, ItemActionSet);
	UpdateClipGaitCaps();
}

void AVCCharacterBase::UpdateClipGaitCaps()
{
	UVCMovementComponent* VCMovement = Cast<UVCMovementComponent>(GetCharacterMovement());
	if (!VCMovement || !ActionMontages)
	{
		return;
	}
	// The set whose loops play: the held set's own locomotion, else the base set's (UVCLocomotionAnimInstance::ResolveClips).
	const UVCActionAnimSet* Held = ActionMontages->GetCurrentActionAnimSet();
	const UVCActionAnimSet* Base = ActionMontages->FindActionAnimSet(VCGameplayTags::Equipment_AnimSet_Unarmed.GetTag());
	const UVCLocomotionClipSet* Clips = Held && Held->LocomotionClips && Held->LocomotionClips->HasOwnLocomotion()
		? Held->LocomotionClips.Get() : (Base ? Base->LocomotionClips.Get() : nullptr);
	FVCGaitCaps Caps;
	if (Clips)
	{
		Caps.Walk = Clips->WalkSpeed;
		Caps.Run = Clips->RunSpeed;
		Caps.Sprint = Clips->SprintSpeed;
		// The crouch's from the set whose crouch plays (its own or the one it borrows, L3); none: the table's row.
		if (const UVCLocomotionClipSet* Crouch = Clips->GetCrouchSet())
		{
			Caps.Crouch = Crouch->CrouchSpeed;
		}
		// The held set's aim ceiling: aiming walks it in its aim walk alone (the bow's hold C).
		Caps.Aim = Clips->PacesAim() ? Clips->AimSpeed : 0.f;
	}
	VCMovement->SetClipGaitCaps(Caps);
}

void AVCCharacterBase::ApplyLocomotionAnimClass()
{
	USkeletalMeshComponent* MeshComponent = GetMesh();
	UWorld* World = GetWorld();
	if (!MeshComponent || !World)
	{
		return;
	}
	if (!BlueprintAnimClass)
	{
		BlueprintAnimClass = MeshComponent->GetAnimClass();
	}

	// The appearance's anim class, else the class vc.Anim.Locomotion names (1 motion matching, character animation P6;
	// 2 the state machine, the locomotion state machine plan), on every machine that draws the player; a dedicated server
	// and the automation tests keep the Blueprint's state machine (nothing there reads a pose, and the databases are
	// large to load)
	TSubclassOf<UAnimInstance> Wanted = BlueprintAnimClass;
	const int32 Mode = CVarAnimLocomotion.GetValueOnGameThread();
	TSoftClassPtr<UAnimInstance> Named = Mode == 2 ? UVCSettings::Get().PlayerStateMachineAnimClass
		: (Mode == 1 ? UVCSettings::Get().PlayerLocomotionAnimClass : TSoftClassPtr<UAnimInstance>());
	if (const UVCCharacterAppearance* Worn = AppearanceComponent ? AppearanceComponent->GetAppearance() : nullptr;
		Worn && !Worn->AnimClass.IsNull() && !Named.IsNull())
	{
		Named = Worn->AnimClass;
	}
	if (!Named.IsNull() && GetNetMode() != NM_DedicatedServer && !GIsAutomationTesting)
	{
		if (UClass* Loaded = Named.LoadSynchronous())
		{
			Wanted = Loaded;
		}
		else
		{
			static TSet<int32> WarnedModes;
			UE_CLOG(!WarnedModes.Contains(Mode), LogVoxelCharacter, Warning,
				TEXT("The player's locomotion AnimBP %s (vc.Anim.Locomotion %d) does not load (Scripts/setup_character_locomotion.py or setup_character_state_machine.py builds it): the Blueprint's own plays"),
				*Named.ToString(), Mode);
			WarnedModes.Add(Mode);
		}
	}
	// The state machine's gait caps (D91) apply while this machine plays it; a server takes a remote player's from its
	// saved moves (FLAG_Custom_3), so a dedicated server's Blueprint graph does not decide them
	if (UVCMovementComponent* VCMovement = Cast<UVCMovementComponent>(GetCharacterMovement()))
	{
		VCMovement->bWantsClipGaitCaps = Wanted && Wanted->IsChildOf(UVCLocomotionAnimInstance::StaticClass());
	}
	UpdateClipGaitCaps();
	if (!Wanted || MeshComponent->GetAnimClass() == Wanted)
	{
		return;
	}
	MeshComponent->SetAnimInstanceClass(Wanted);
	RelinkEquipmentAnimLayers();
	if (AppearanceComponent)
	{
		AppearanceComponent->RefreshSimulations(); // the new instances' rigid-body nodes fall along the movement's gravity
	}
}

void AVCCharacterBase::RelinkEquipmentAnimLayers()
{
#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
	// A new anim instance links no layers: the held items' stances come back (EquipmentPlugin links them on equip)
	USkeletalMeshComponent* MeshComponent = GetMesh();
	const UGameInstance* GI = GetGameInstance();
	const UItemDatabaseSubsystem* ItemDB = GI ? GI->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	if (!EquipmentManager || !MeshComponent || !ItemDB)
	{
		return;
	}
	for (const FGameplayTag& SlotTag : EquipmentManager->GetOccupiedSlotTags())
	{
		if (EquipmentManager->IsSheathSlot(SlotTag) && !EquipmentManager->IsDrawn(SlotTag))
		{
			continue; // a sheathed weapon links no stance (D77)
		}
		const UItemDefinition* Def = ItemDB->GetDefinition(EquipmentManager->GetEquippedItem(SlotTag).ItemDefinitionId);
		const UItemFragment_Equipment* EquipFrag = Def ? Def->FindFragment<UItemFragment_Equipment>() : nullptr;
		if (EquipFrag && EquipFrag->AnimLayerClass)
		{
			MeshComponent->LinkAnimClassLayers(EquipFrag->AnimLayerClass);
		}
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
#if !UE_BUILD_SHIPPING
	bShowVoxelDebug = !bShowVoxelDebug;
	UE_LOG(LogVoxelCharacter, Log, TEXT("VoxelCharacter debug: %s"), bShowVoxelDebug ? TEXT("ON") : TEXT("OFF"));
#endif
}

#if !UE_BUILD_SHIPPING
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
		const FVCTerrainContext& Ctx = MovComp->GetTerrainContext();
		const UEnum* SurfaceEnum = StaticEnum<EVCSurfaceType>();
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
		// The gait table (C4): the gait and the rotation mode in force, and the row the values come from.
		const UEnum* GaitEnum = StaticEnum<EVCGait>();
		GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Yellow,
			FString::Printf(TEXT("Gait: %s  Rotation: %s%s  Table: %s"),
				GaitEnum ? *GaitEnum->GetNameStringByValue(static_cast<int64>(MovComp->GetGait())) : TEXT("?"),
				MovComp->GetRotationMode() == EVCRotationMode::Strafe ? TEXT("Strafe") : TEXT("Orient"),
				MovComp->IsAimTurnPending() ? TEXT(" (turning)") : TEXT(""),
				MovComp->bUseGaitTable ? TEXT("on") : TEXT("off")));
		GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::Yellow,
			FString::Printf(TEXT("Row: %s  Accel: %.0f  Brake: %.0f  RotRate: %.0f"),
				*MovComp->GetGaitRow().ToString(), MovComp->MaxAcceleration, MovComp->BrakingDecelerationWalking, MovComp->RotationRate.Yaw));
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
	GEngine->AddOnScreenDebugMessage(-1, 0.f, FColor::White,
		FString::Printf(TEXT("Hotbar: %d  ActionSet: %s"), ActiveHotbarSlot, *ActiveActionSet.ToString()));
}
#endif

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
		// Holding it aims a ranged weapon (C3): the release ends the aim.
		EIC->BindAction(Config->IA_SecondaryAction, ETriggerEvent::Completed, this, &AVCCharacterBase::Input_StopSecondaryAction);
		EIC->BindAction(Config->IA_SecondaryAction, ETriggerEvent::Canceled, this, &AVCCharacterBase::Input_StopSecondaryAction);
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
	if (Config->IA_Sheathe)
	{
		EIC->BindAction(Config->IA_Sheathe, ETriggerEvent::Started, this, &AVCCharacterBase::Input_Sheathe);
	}
	if (Config->IA_CycleWeapon)
	{
		EIC->BindAction(Config->IA_CycleWeapon, ETriggerEvent::Started, this, &AVCCharacterBase::Input_CycleWeapon);
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
	PressPrimaryAction();
}

void AVCCharacterBase::PressPrimaryAction()
{
	// Priority chain: GAS ability -> [edit mode only] equipped item action -> voxel dig -> fallback
	// Behind a raised guard nothing else happens (D94): lower it first.
	if (IsIncapacitated() || bBlocking)
	{
		return;
	}
	// The hands are busy while a weapon comes out or goes back (the equipment sheathing plan, D79).
	if (ActionMontages && ActionMontages->IsSheathActionPlaying())
	{
		return;
	}

	// Step 0: a ranged weapon in the main hand draws while the button is held and shoots on release.
	if (TryStartRangedAttack())
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
		// Step 1d: a melee weapon in hand swings with nothing hostile under the crosshair too, and the server's sweep
		// lands on whatever is there at the hit time (G174: before, a sword did nothing until an enemy was in reach).
		if (UVCMeleeAttackAbility::HoldsMeleeWeapon(this))
		{
			StartMeleeSwing();
		}
		return;
	}

	// Step 2 (an equipped tool's dig) and step 3 (an unarmed dig, edit mode) dig the same voxel, FindEditTarget's (WM-C12).
	// In a cubic world with break times the press starts a hold instead (WM-C15): the block under the crosshair breaks
	// after its material's time while the button stays down, decided on the server.
	const UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld());
	if (BlockInteraction && ChunkMgr && UVCBlockInteractionComponent::UsesTimedBreaks(ChunkMgr->GetConfiguration()))
	{
		PlayOwnAction(EVCAnimAction::Dig);
		BlockInteraction->StartHold();
		return;
	}
	FHitResult Hit;
	FIntVector DigVoxel, PlaceVoxel;
	AVCPlayerController* PC = Cast<AVCPlayerController>(GetController());
	if (PC && FindEditTarget(Hit, DigVoxel, PlaceVoxel))
	{
		PlayOwnAction(EVCAnimAction::Dig);
		PC->Server_RequestVoxelModification(DigVoxel, EVCVoxelModificationType::Destroy, 0);
	}
}

void AVCCharacterBase::Input_StopPrimaryAction(const FInputActionValue& Value)
{
	ReleasePrimaryAction();
}

void AVCCharacterBase::ReleasePrimaryAction()
{
	ReleaseRangedAttack();
	if (BlockInteraction)
	{
		BlockInteraction->StopHold();
	}
}

void AVCCharacterBase::Input_SecondaryAction(const FInputActionValue& Value)
{
	PressSecondaryAction();
}

void AVCCharacterBase::PressSecondaryAction()
{
	// Priority chain: aim a ranged weapon (held) -> raise a shield (held) -> [edit mode only] voxel place
	if (IsIncapacitated())
	{
		return;
	}
	// A ranged weapon in hand aims while the button is held (C3, D73); edit mode keeps placing (a held bow is no placing tool).
	if (!bEditModeEnabled && UVCRangedAttackAbility::HoldsRangedWeapon(this))
	{
		SetWantsToAim(true);
		return;
	}
	// A shield in the off hand raises the guard while the button is held (D94); edit mode keeps placing.
	if (!bEditModeEnabled && StartBlock())
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
		PlayOwnAction(EVCAnimAction::Place);
		PC->Server_RequestVoxelModification(PlaceVoxel, EVCVoxelModificationType::Place, 2); // Stone
	}
}

void AVCCharacterBase::Input_StopSecondaryAction(const FInputActionValue& Value)
{
	ReleaseSecondaryAction();
}

void AVCCharacterBase::ReleaseSecondaryAction()
{
	SetWantsToAim(false);
	StopBlock();
}

bool AVCCharacterBase::StartBlock()
{
	if (bBlocking)
	{
		return true;
	}
	// The hands are busy while a weapon comes out or goes back (D79).
	if (!UVCBlockAbility::CanStartBlock(this) || (ActionMontages && ActionMontages->IsSheathActionPlaying()))
	{
		return false;
	}
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC)
	{
		return false;
	}
	// The owner raises its guard at once; the server's activation plays it for everyone else and holds State.Blocking.
	PlayOwnAction(EVCAnimAction::Block);
	if (!ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(VCGameplayTags::Ability_Block)))
	{
		if (ActionMontages && IsLocallyControlled())
		{
			ActionMontages->StopActionLocal(EVCAnimAction::Block);
		}
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s: the guard did not go up"), *GetName());
		return false;
	}
	bBlocking = true;
	return true;
}

void AVCCharacterBase::StopBlock()
{
	if (!bBlocking)
	{
		return;
	}
	bBlocking = false;
	// The guard still up on this machine comes down; one a hit broke staggers already.
	if (ActionMontages && IsLocallyControlled()
		&& (ActionMontages->IsActionPlaying(EVCAnimAction::Block) || ActionMontages->IsActionPlaying(EVCAnimAction::BlockHit)))
	{
		ActionMontages->PlayActionLocal(EVCAnimAction::BlockEnd);
	}
	UVCBlockAbility::ReleaseBlock(GetAbilitySystemComponent());
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

	// A carried weapon goes through the use path: into its sheath and drawn with its montage, on the server (D79).
	if (EquipFrag->SheathSlotTag.IsValid())
	{
		RequestUseItemInSlot(Slot);
		return true;
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
	// A hand names what is in it: a weapon drawn from a sheath leaves from its sheath (the equipment sheathing plan).
	const FGameplayTag Held = EquipmentManager->GetHeldSlotTag(SlotTag);
	if (Held.IsValid())
	{
		SlotTag = Held;
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
	// A quiver's mesh landed: the arrows standing in it ride its visual (D100).
	UpdateQuiverArrows();
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
		// The owner learns of a placement or a use from the server (it may be refused), so everyone plays it from here.
		const bool bPlaced = TryPlaceItem(SlotIndex, Item, *Placeable);
		if (bPlaced && ActionMontages)
		{
			ActionMontages->PlayActionForEveryone(EVCAnimAction::Place);
		}
		return bPlaced;
	}
	const UItemFragment_Consumable* Consumable = Def ? Def->FindFragment<UItemFragment_Consumable>() : nullptr;
	if (!Consumable)
	{
#if WITH_EQUIPMENT_PLUGIN
		// Hotbar option C (QA R18): using gear equips it, swapping out whatever held that slot.
		const UItemFragment_Equipment* EquipFrag = Def ? Def->FindFragment<UItemFragment_Equipment>() : nullptr;
		if (EquipFrag && EquipFrag->EquipmentSlotTag.IsValid() && EquipmentManager)
		{
			// A carried weapon (D79): into its sheath (the sheath's weapon to the bag), then drawn with its montage, the
			// weapon drawn before going back to its own sheath.
			if (EquipFrag->SheathSlotTag.IsValid())
			{
				const EEquipmentResult Equipped = EquipmentManager->TryEquipFromInventory(Item.InstanceId, InventoryComponent, FGameplayTag());
				FGameplayTag Sheath;
				for (const FEquipmentSlot& Slot : EquipmentManager->EquipmentSlots)
				{
					if (Slot.bIsOccupied && Slot.EquippedItem.InstanceId == Item.InstanceId)
					{
						Sheath = Slot.SlotTag;
					}
				}
				UE_LOG(LogVoxelCharacter, Log, TEXT("UseItemInSlot: carry %s -> %s: %s"), *Item.ItemDefinitionId.PrimaryAssetName.ToString(),
					*Sheath.ToString(), *UEnum::GetValueAsString(Equipped));
				// A shield (D96) is no weapon of the cycle: it follows the drawn one into the off hand on its own, and the
				// weapon in the hand stays there.
				if (Equipped == EEquipmentResult::Success && GetCarriedWeaponSlots().Contains(Sheath))
				{
					RunSheathSequence(Sheath, /*bOwnerPlayedIt*/ false);
				}
				return Equipped == EEquipmentResult::Success;
			}
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
	if (ActionMontages)
	{
		ActionMontages->PlayActionForEveryone(EVCAnimAction::Use);
	}
	return true;
#else
	return false;
#endif
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
				Entry.bDrawn = EquipmentManager->IsDrawn(SlotTag);
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
			// A drawn weapon comes back drawn (no montage); an old save's main-hand weapon lands in its sheath, drawn. A slot
			// the character no longer has (a shield saved in the retired Sheath.ShieldBack, D95) gives way to the item's own.
			const bool bHasSlot = EquipmentManager->EquipmentSlots.ContainsByPredicate([&Entry](const FEquipmentSlot& Slot)
			{
				return Slot.SlotTag == Entry.SlotTag;
			});
			if (EquipmentManager->TryEquipFromInventory(Item.InstanceId, InventoryComponent, bHasSlot ? Entry.SlotTag : FGameplayTag(),
				Entry.bDrawn) == EEquipmentResult::Success)
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
