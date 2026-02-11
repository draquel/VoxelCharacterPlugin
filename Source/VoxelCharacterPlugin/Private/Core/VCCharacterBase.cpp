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
#include "VoxelCharacterPlugin.h"

#if WITH_INTERACTION_PLUGIN
#include "Components/InteractionComponent.h"
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
#endif

#if WITH_EQUIPMENT_PLUGIN
	EquipmentManager = CreateDefaultSubobject<UEquipmentManagerComponent>(TEXT("EquipmentManager"));
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
}

void AVCCharacterBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (CameraManager)
	{
		CameraManager->UpdateCamera(DeltaSeconds);
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

	// 2. Mesh visibility
	UpdateMeshVisibility();

	// 3. Equipment re-attachment (FP arms vs TP body)
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
	UpdateMeshVisibility();
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

	// Forward to PlayerController for HUD prompt display
	// (HUD widget creation is game-specific; the controller provides the hooks)
}

void AVCCharacterBase::HandleInteractableLost(AActor* InteractableActor)
{
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("Interactable lost: %s"),
		InteractableActor ? *InteractableActor->GetName() : TEXT("null"));
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
	if (Config->IA_HotbarSlot)
	{
		EIC->BindAction(Config->IA_HotbarSlot, ETriggerEvent::Started, this, &AVCCharacterBase::Input_HotbarSlot);
	}
	if (Config->IA_ScrollHotbar)
	{
		EIC->BindAction(Config->IA_ScrollHotbar, ETriggerEvent::Triggered, this, &AVCCharacterBase::Input_ScrollHotbar);
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
