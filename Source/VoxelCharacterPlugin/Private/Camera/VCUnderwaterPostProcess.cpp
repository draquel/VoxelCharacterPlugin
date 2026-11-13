// Copyright Daniel Raquel. All Rights Reserved.

#include "Camera/VCUnderwaterPostProcess.h"
#include "Core/VCSettings.h"
#include "Movement/VCTerrainQueryHelper.h"
#include "Components/AudioComponent.h"
#include "Components/PostProcessComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "VoxelCharacterPlugin.h"

namespace VCUnderwaterPrivate
{
	// The underwater material's parameters (the parent's Scripts/setup_water_material.py builds the material).
	const FName CameraDepthParam(TEXT("CameraDepth"));
	const FName SurfaceUpParam(TEXT("SurfaceUp"));
	const FName AmountParam(TEXT("Amount"));
	const FName FogDensityParam(TEXT("FogDensity"));
	const FName FogMaxDistanceParam(TEXT("FogMaxDistance"));
	const FName DepthFalloffParam(TEXT("DepthFalloff"));
	const FName NightFloorParam(TEXT("NightFloor"));
	const FName FogColorParam(TEXT("FogColor"));
	const FName TintParam(TEXT("Tint"));

	/** The under-water loop's fade, seconds. */
	constexpr float LoopFadeSeconds = 0.35f;
}

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
	const UVCSettings& Settings = UVCSettings::Get();

	// The water volume first: with its material the tint is per pixel (only what is under the waterline), so the view
	// volume below leaves the scene's tint alone.
	if (UMaterialInterface* Material = Settings.UnderwaterMaterial.LoadSynchronous())
	{
		WaterPostProcessComp = NewObject<UPostProcessComponent>(Owner, TEXT("UnderwaterWaterVolume"));
		WaterPostProcessComp->SetupAttachment(Owner->GetRootComponent());
		WaterPostProcessComp->RegisterComponent();
		WaterPostProcessComp->bUnbound = true;
		WaterPostProcessComp->BlendWeight = 1.f;
		WaterPostProcessComp->bEnabled = false;
		WaterMaterial = UMaterialInstanceDynamic::Create(Material, this);
		WaterPostProcessComp->Settings.WeightedBlendables.Array.Add(FWeightedBlendable(1.f, WaterMaterial));
	}
	else if (!Settings.UnderwaterMaterial.IsNull())
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("UnderwaterPostProcess: the underwater material %s does not load: no fog, no waterline"),
			*Settings.UnderwaterMaterial.ToString());
	}

	// The view volume
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

		FPostProcessSettings& PP = PostProcessComp->Settings;

		// Without the water material the tint is the whole screen's, via the scene colour multiply.
		PP.bOverride_SceneColorTint = WaterMaterial == nullptr;
		PP.SceneColorTint = UnderwaterTint;

		// Vignette
		PP.bOverride_VignetteIntensity = true;
		PP.VignetteIntensity = UnderwaterVignetteIntensity;

		// No depth of field (W1): the fog is the distance cue now, and the blur reached across the waterline into the
		// half of the screen above the water and hid the surface's ripples from below.

		// Bloom for underwater caustic feel
		PP.bOverride_BloomIntensity = true;
		PP.BloomIntensity = 1.5f;
	}

	// The loop under the surface: created silent, faded in and out as the camera crosses.
	if (USoundBase* Sound = Settings.WaterUnderLoopSound.LoadSynchronous())
	{
		UnderLoop = UGameplayStatics::CreateSound2D(this, Sound, UnderLoopVolume, 1.f, 0.f, nullptr, /*bPersistAcrossLevelTransition=*/false, /*bAutoDestroy=*/false);
	}

	UE_LOG(LogVoxelCharacter, Log, TEXT("UnderwaterPostProcess: view volume created; water material %s, loop %s"),
		WaterMaterial ? TEXT("on") : TEXT("none"), UnderLoop ? TEXT("on") : TEXT("none"));
}

void UVCUnderwaterPostProcess::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	// The volumes and the loop go with this component: destroyed on its own (the pawn's player controller went, G133)
	// they would otherwise stay registered on the pawn and keep tinting the view; with the whole actor they are
	// destroyed anyway.
	if (!bDestroyingHierarchy)
	{
		if (PostProcessComp)
		{
			PostProcessComp->DestroyComponent();
		}
		if (WaterPostProcessComp)
		{
			WaterPostProcessComp->DestroyComponent();
		}
	}
	if (UnderLoop)
	{
		UnderLoop->Stop();
		UnderLoop->DestroyComponent();
	}
	PostProcessComp = nullptr;
	WaterPostProcessComp = nullptr;
	WaterMaterial = nullptr;
	UnderLoop = nullptr;
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UVCUnderwaterPostProcess::IsLocalPlayerView(const APawn* Pawn)
{
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	return PC && PC->IsLocalController();
}

FLinearColor UVCUnderwaterPostProcess::TintAtDepth(double CameraDepth, const FLinearColor& ShallowTint, const FLinearColor& DeepTint, float FullDepth)
{
	const float Alpha = FMath::Clamp(static_cast<float>(CameraDepth) / FMath::Max(FullDepth, 1.f), 0.f, 1.f);
	return FMath::Lerp(ShallowTint, DeepTint, Alpha);
}

void UVCUnderwaterPostProcess::HoldOff()
{
	CurrentBlendWeight = 0.f;
	if (PostProcessComp)
	{
		PostProcessComp->BlendWeight = 0.f;
	}
	if (WaterPostProcessComp)
	{
		WaterPostProcessComp->bEnabled = false;
	}
	if (UnderLoop && bWasUnder)
	{
		UnderLoop->FadeOut(VCUnderwaterPrivate::LoopFadeSeconds, 0.f);
	}
	bWasUnder = false;
}

void UVCUnderwaterPostProcess::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!PostProcessComp)
	{
		return;
	}

	// No local player views through this pawn: nothing to tint, and the unbound volumes must not tint anyone else's
	// view (G133). The owner destroys the component in this case; this holds them off should one remain.
	FVCWaterSurfaceSample Water;
	if (!SampleCamera(Water))
	{
		HoldOff();
		return;
	}

	// Under the surface: in water and below the sea's level. The water flag alone is not enough: the voxel holding the
	// water line is flagged top to bottom, so a camera just above the line reads water too.
	const bool bUnder = Water.IsSubmerged();

	// The view volume (vignette, depth of field, bloom) eases in and out.
	CurrentBlendWeight = FMath::FInterpTo(CurrentBlendWeight, bUnder ? 1.f : 0.f, DeltaTime, BlendSpeed);
	PostProcessComp->BlendWeight = CurrentBlendWeight;

	const FLinearColor Tint = TintAtDepth(Water.Depth, UnderwaterShallowTint, UnderwaterTint, TintFullDepth);
	if (WaterMaterial && WaterPostProcessComp)
	{
		// The water volume follows the surface itself, with no easing: the material splits the screen at the waterline
		// from the camera's depth, so the volume is on whenever the near plane can reach the water.
		WaterPostProcessComp->bEnabled = Water.bWater;
		if (Water.bWater)
		{
			using namespace VCUnderwaterPrivate;
			WaterMaterial->SetScalarParameterValue(CameraDepthParam, static_cast<float>(Water.Depth));
			WaterMaterial->SetVectorParameterValue(SurfaceUpParam, FLinearColor(Water.Up.X, Water.Up.Y, Water.Up.Z, 0.f));
			WaterMaterial->SetScalarParameterValue(AmountParam, 1.f);
			WaterMaterial->SetScalarParameterValue(FogDensityParam, UnderwaterFogDensity);
			WaterMaterial->SetScalarParameterValue(FogMaxDistanceParam, UnderwaterFogMaxDistance);
			WaterMaterial->SetScalarParameterValue(DepthFalloffParam, UnderwaterLightFalloffDepth);
			WaterMaterial->SetScalarParameterValue(NightFloorParam, UnderwaterNightFloor);
			WaterMaterial->SetVectorParameterValue(FogColorParam, UnderwaterFogColor);
			WaterMaterial->SetVectorParameterValue(TintParam, Tint);
		}
	}
	else if (bUnder)
	{
		PostProcessComp->Settings.SceneColorTint = Tint;
	}

	if (UnderLoop && bUnder != bWasUnder)
	{
		if (bUnder)
		{
			UnderLoop->FadeIn(VCUnderwaterPrivate::LoopFadeSeconds, UnderLoopVolume);
		}
		else
		{
			UnderLoop->FadeOut(VCUnderwaterPrivate::LoopFadeSeconds, 0.f);
		}
	}
	bWasUnder = bUnder;
}

bool UVCUnderwaterPostProcess::SampleCamera(FVCWaterSurfaceSample& OutSample) const
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
	// The near plane's lowest corner hangs under the camera by up to about 1.6 near-plane distances (looking down, at a
	// wide view): within that of the surface the water volume must already be on.
	const float Reach = FMath::Max(WaterlineReach, GNearClippingPlane * 1.6f);
	return FVCTerrainQueryHelper::SampleWaterSurface(Pawn->GetWorld(), CameraLoc, Reach, OutSample);
}
