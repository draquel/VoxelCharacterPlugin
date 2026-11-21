// Copyright Daniel Raquel. All Rights Reserved.

// AVCCharacterBase's surface gameplay (placing, gathering, mining, rest, crafting) (a file of the class, code review CR4).

#include "Core/VCCharacterBase.h"
#include "VoxelWorldModeTypes.h"
#include "HAL/IConsoleManager.h"
#include "Core/VCPlayerState.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Core/VCPlayerController.h"
#include "Core/VCGameModeBase.h"
#include "Combat/VCCombatComponent.h"
#include "Animation/VCActionAnimSet.h"
#include "Editing/VCBlockInteractionComponent.h"
#include "Utilities/CGFCombatStatics.h"
#include "Tags/CGFGameplayTags.h"
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
#include "VoxelCoordinates.h"
#include "VoxelWorldConfiguration.h"
#include "VoxelChunkManager.h"
#include "VoxelTreeInjector.h"
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
	UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld());
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
	PlayOwnAction(EVCAnimAction::Chop);
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
	AVCPlayerController* PC = Cast<AVCPlayerController>(GetController());
	if (PC && PC->ChopScatterAt(AimPoint))
	{
		ShareOwnAction(EVCAnimAction::Chop);
	}
}

bool AVCCharacterBase::TryChopVoxelTree()
{
	UVoxelChunkManager* ChunkMgr = UVoxelChunkManager::Find(GetWorld());
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
	PlayOwnAction(EVCAnimAction::Chop);
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
	AVCPlayerController* PC = Cast<AVCPlayerController>(GetController());
	if (PC && PC->ChopVoxelTreeAt(VoxelCoord))
	{
		ShareOwnAction(EVCAnimAction::Chop);
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
	// A pickaxe in the hand, its own slot's or drawn from the belt (a belted one grants no mining speed either, D77).
	if (!EquipmentManager || !EquipmentManager->GetHeldItem(CGFGameplayTags::Equipment_Slot_MainHand).IsValid())
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
