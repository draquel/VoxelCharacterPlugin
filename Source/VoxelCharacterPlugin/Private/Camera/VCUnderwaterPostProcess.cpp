// Copyright Daniel Raquel. All Rights Reserved.

#include "Camera/VCUnderwaterPostProcess.h"
#include "Movement/VCMovementComponent.h"
#include "Movement/VCTerrainQueryHelper.h"
#include "Camera/CameraComponent.h"
#include "Components/PostProcessComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "VoxelCharacterPlugin.h"

UVCUnderwaterPostProcess::UVCUnderwaterPostProcess()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork; // After camera update
}

void UVCUnderwaterPostProcess::BeginPlay()
{
	Super::BeginPlay();

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// Cache movement component
	if (ACharacter* Character = Cast<ACharacter>(Owner))
	{
		MovementComp = Cast<UVCMovementComponent>(Character->GetCharacterMovement());
	}

	// Create post-process component at runtime
	PostProcessComp = NewObject<UPostProcessComponent>(Owner, TEXT("UnderwaterPPVolume"));
	if (PostProcessComp)
	{
		PostProcessComp->SetupAttachment(Owner->GetRootComponent());
		PostProcessComp->RegisterComponent();

		// Infinite unbound volume so it affects the entire view
		PostProcessComp->bUnbound = true;

		// Start disabled
		PostProcessComp->BlendWeight = 0.f;
		PostProcessComp->bEnabled = true;

		// Configure post-process settings
		FPostProcessSettings& PP = PostProcessComp->Settings;

		// Color grading: tint via scene color multiply
		PP.bOverride_SceneColorTint = true;
		PP.SceneColorTint = UnderwaterTint;

		// Vignette
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = UnderwaterVignetteIntensity;

		// Depth of field for underwater blur at distance
		PP.bOverride_DepthOfFieldFocalDistance = true;
		PP.DepthOfFieldFocalDistance = 200.f;
		PP.bOverride_DepthOfFieldFstop = true;
		PP.DepthOfFieldFstop = 2.0f;

		// Bloom for underwater caustic feel
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = 1.5f;

		UE_LOG(LogVoxelCharacter, Log, TEXT("UnderwaterPostProcess: Created and configured post-process component"));
	}
}

void UVCUnderwaterPostProcess::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// The volume goes with this component: destroyed on its own (the pawn's player controller went, G133) it would
	// otherwise stay registered on the pawn and keep tinting the view; with the whole actor it is destroyed anyway.
	if (PostProcessComp && !bDestroyingHierarchy)
	{
		PostProcessComp->DestroyComponent();
	}
	PostProcessComp = nullptr;
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UVCUnderwaterPostProcess::IsLocalPlayerView(const APawn* Pawn)
{
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	return PC && PC->IsLocalController();
}

void UVCUnderwaterPostProcess::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!PostProcessComp)
	{
		return;
	}

	// No local player views through this pawn: nothing to tint, and the unbound volume must not tint anyone else's
	// view (G133). The owner destroys the component in this case; this holds the weight at zero should one remain.
	if (!IsLocalPlayerView(Cast<APawn>(GetOwner())))
	{
		CurrentBlendWeight = 0.f;
		PostProcessComp->BlendWeight = 0.f;
		return;
	}

	const bool bUnderwater = IsCameraUnderwater();

	// Smooth blend toward target
	const float TargetWeight = bUnderwater ? 1.0f : 0.0f;
	CurrentBlendWeight = FMath::FInterpTo(CurrentBlendWeight, TargetWeight, DeltaTime, BlendSpeed);

	// Apply blend weight
	PostProcessComp->BlendWeight = CurrentBlendWeight;

	// Dynamically update tint intensity based on water depth for deeper = darker
	if (bUnderwater && MovementComp)
	{
		const float Immersion = MovementComp->ImmersionDepth();
		// Lerp tint toward darker at deeper immersion
		FLinearColor DepthTint = FMath::Lerp(
			FLinearColor(0.6f, 0.7f, 0.85f, 1.0f),  // Shallow: light blue
			UnderwaterTint,                            // Deep: full tint
			FMath::Clamp(Immersion, 0.f, 1.f));

		PostProcessComp->Settings.SceneColorTint = DepthTint;
	}
}

bool UVCUnderwaterPostProcess::IsCameraUnderwater() const
{
	// The camera's position, not the character's: in third person the camera may be above water while the character is
	// submerged. Only a local player controller has a view; without one there is nothing to tint (G133: the pawn's own
	// terrain context used to stand in here, which tinted the local view blue for a swimming remote player or NPC).
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!PC || !PC->IsLocalController())
	{
		return false;
	}

	FVector CameraLoc;
	FRotator CameraRot;
	PC->GetPlayerViewPoint(CameraLoc, CameraRot);

	float WaterDepth = 0.f;
	return FVCTerrainQueryHelper::IsPositionUnderwater(Pawn->GetWorld(), CameraLoc, WaterDepth);
}
