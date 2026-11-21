// Copyright Daniel Raquel. All Rights Reserved.

#include "Effects/VCWaterEffectsComponent.h"
#include "Core/VCSettings.h"
#include "Movement/VCTerrainQueryHelper.h"
#include "Components/AudioComponent.h"
#include "Components/MeshComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "VoxelCharacterPlugin.h"

namespace VCWaterEffectsPrivate
{
	/** How far over the surface a body may be and its effects still find the water under it, uu (a jump out of a swim). */
	constexpr float SurfaceReach = 300.f;

	/** The swim loop's fade, seconds. */
	constexpr float LoopFadeSeconds = 0.3f;

	/** How often a swimmer's meshes are gathered again for custom depth, seconds. */
	constexpr float BodyRefreshInterval = 0.5f;
}

UVCWaterEffectsComponent::UVCWaterEffectsComponent()
{
	// Ticks only while the character swims (HandleMovementModeChanged): the wake follows the surface over it.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UVCWaterEffectsComponent::BeginPlay()
{
	Super::BeginPlay();

	const UVCSettings& Settings = UVCSettings::Get();
	SplashSystem = Settings.WaterSplashSystem.LoadSynchronous();
	WakeSystem = Settings.WaterWakeSystem.LoadSynchronous();
	SplashInSound = Settings.WaterSplashInSound.LoadSynchronous();
	SplashOutSound = Settings.WaterSplashOutSound.LoadSynchronous();
	SwimLoopSound = Settings.WaterSwimLoopSound.LoadSynchronous();

	// A sound played at a location with no attenuation is heard everywhere at full volume: another player's splash
	// across the map would be. One falloff for the three sounds.
	Attenuation = NewObject<USoundAttenuation>(this);
	Attenuation->Attenuation.bAttenuate = true;
	Attenuation->Attenuation.bSpatialize = true;
	Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Attenuation->Attenuation.AttenuationShapeExtents = FVector(300.f);
	Attenuation->Attenuation.FalloffDistance = SoundFalloffDistance;

	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->MovementModeChangedDelegate.AddDynamic(this, &UVCWaterEffectsComponent::HandleMovementModeChanged);
		// Already swimming (spawned in the sea, or the component arrived late): the wake follows, with no splash.
		const UCharacterMovementComponent* Movement = Character->GetCharacterMovement();
		const bool bSwims = Movement && Movement->MovementMode == MOVE_Swimming;
		SetComponentTickEnabled(bSwims);
		SetBodyCustomDepth(bSwims);
	}
}

void UVCWaterEffectsComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		Character->MovementModeChangedDelegate.RemoveDynamic(this, &UVCWaterEffectsComponent::HandleMovementModeChanged);
	}
	SetBodyCustomDepth(false);
	if (Wake)
	{
		Wake->DestroyComponent();
		Wake = nullptr;
	}
	if (SwimLoop)
	{
		SwimLoop->Stop();
		SwimLoop->DestroyComponent();
		SwimLoop = nullptr;
	}
	bWakeActive = false;
	Super::EndPlay(EndPlayReason);
}

float UVCWaterEffectsComponent::SplashStrength(float SpeedIntoWater, float FullSpeed, float MinStrength)
{
	const float Alpha = FMath::Clamp(SpeedIntoWater / FMath::Max(FullSpeed, 1.f), 0.f, 1.f);
	return FMath::Lerp(FMath::Clamp(MinStrength, 0.f, 1.f), 1.f, Alpha);
}

bool UVCWaterEffectsComponent::LeavesWake(float SurfaceSpeed, double HeadDepth, float MinSpeed, float MaxHeadDepth)
{
	return SurfaceSpeed >= MinSpeed && HeadDepth <= static_cast<double>(MaxHeadDepth);
}

void UVCWaterEffectsComponent::HandleMovementModeChanged(ACharacter* Character, EMovementMode PreviousMode, uint8 PreviousCustomMode)
{
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}
	const bool bSwims = Movement->MovementMode == MOVE_Swimming;
	const bool bSwam = PreviousMode == MOVE_Swimming;
	if (bSwims == bSwam)
	{
		return;
	}
	SetComponentTickEnabled(bSwims);
	SetBodyCustomDepth(bSwims);

	FVCWaterSurfaceSample Water;
	const bool bSea = FVCTerrainQueryHelper::SampleWaterSurface(GetWorld(), Character->GetActorLocation(), VCWaterEffectsPrivate::SurfaceReach, Water);
	if (bSwims)
	{
		// Out of a hold (MOVE_None: the spawn's terrain wait, a teleport's settle) the body did not fall in: no splash.
		if (bSea && PreviousMode != MOVE_None)
		{
			const float SpeedIntoWater = static_cast<float>(-FVector::DotProduct(Character->GetVelocity(), Water.Up));
			PlaySplash(/*bEntering=*/true, SplashStrength(SpeedIntoWater, SplashFullSpeed, SplashMinStrength));
		}
	}
	else
	{
		SetWakeActive(false);
		// Onto the shore or up into the air; a flight or a hold out of the water is not a body leaving it.
		if (bSea && (Movement->MovementMode == MOVE_Walking || Movement->MovementMode == MOVE_Falling))
		{
			PlaySplash(/*bEntering=*/false, SplashExitStrength);
		}
	}
}

void UVCWaterEffectsComponent::PlaySplash(bool bEntering, float Strength)
{
	UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return;
	}
	const double Now = World->GetTimeSeconds();
	if (Now - LastSplashSeconds < SplashMinInterval)
	{
		return;
	}
	FVCWaterSurfaceSample Water;
	if (!FVCTerrainQueryHelper::SampleWaterSurface(World, Owner->GetActorLocation(), VCWaterEffectsPrivate::SurfaceReach, Water))
	{
		return;
	}
	LastSplashSeconds = Now;
	++SplashCount;

	const FVector Point = Water.SurfacePoint(Owner->GetActorLocation());
	Strength = FMath::Clamp(Strength, 0.f, 1.f);
	if (SplashSystem)
	{
		// The system's +Z is the surface's up (radial on a planet); the strength is its scale.
		const float Scale = FMath::Lerp(static_cast<float>(SplashScaleRange.X), static_cast<float>(SplashScaleRange.Y), Strength);
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, SplashSystem, Point, FRotationMatrix::MakeFromZ(Water.Up).Rotator(),
			FVector(Scale), /*bAutoDestroy=*/true, /*bAutoActivate=*/true, ENCPoolMethod::AutoRelease);
	}
	if (USoundBase* Sound = bEntering ? SplashInSound : SplashOutSound)
	{
		// A harder splash is louder and a little lower.
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Point, FRotator::ZeroRotator, FMath::Lerp(0.45f, 1.f, Strength),
			FMath::Lerp(1.15f, 0.9f, Strength), 0.f, Attenuation);
	}
}

void UVCWaterEffectsComponent::SetWakeActive(bool bActive)
{
	if (bActive == bWakeActive)
	{
		return;
	}
	bWakeActive = bActive;
	USceneComponent* Root = GetOwner() ? GetOwner()->GetRootComponent() : nullptr;
	if (bActive && Root)
	{
		if (!Wake && WakeSystem)
		{
			Wake = UNiagaraFunctionLibrary::SpawnSystemAttached(WakeSystem, Root, NAME_None, FVector::ZeroVector, FRotator::ZeroRotator,
				EAttachLocation::KeepRelativeOffset, /*bAutoDestroy=*/false, /*bAutoActivate=*/false);
		}
		if (Wake)
		{
			Wake->Activate(/*bReset=*/false);
		}
		if (!SwimLoop && SwimLoopSound)
		{
			SwimLoop = UGameplayStatics::SpawnSoundAttached(SwimLoopSound, Root, NAME_None, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
				/*bStopWhenAttachedToDestroyed=*/true, SwimLoopVolume, 1.f, 0.f, Attenuation, nullptr, /*bAutoDestroy=*/false);
		}
		else if (SwimLoop)
		{
			SwimLoop->FadeIn(VCWaterEffectsPrivate::LoopFadeSeconds, SwimLoopVolume);
		}
	}
	else
	{
		if (Wake)
		{
			Wake->Deactivate(); // the foam already on the water lives out its time
		}
		if (SwimLoop)
		{
			SwimLoop->FadeOut(VCWaterEffectsPrivate::LoopFadeSeconds, 0.f);
		}
	}
}

void UVCWaterEffectsComponent::SetBodyCustomDepth(bool bOn)
{
	BodyRefreshSeconds = VCWaterEffectsPrivate::BodyRefreshInterval;
	if (!bOn)
	{
		for (const TWeakObjectPtr<UPrimitiveComponent>& Body : CustomDepthBodies)
		{
			if (UPrimitiveComponent* Component = Body.Get())
			{
				Component->SetRenderCustomDepth(false);
			}
		}
		CustomDepthBodies.Reset();
		return;
	}
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	// The character's own meshes and those of the actors attached to it (a MetaHuman's parts actor, a held item).
	TArray<AActor*> Actors;
	Owner->GetAttachedActors(Actors, /*bResetArray=*/true, /*bRecursivelyIncludeAttachedActors=*/true);
	Actors.Add(const_cast<AActor*>(Owner));
	for (const AActor* Actor : Actors)
	{
		TInlineComponentArray<UMeshComponent*> Meshes(Actor);
		for (UMeshComponent* Mesh : Meshes)
		{
			if (Mesh && Mesh->IsVisible() && !Mesh->bRenderCustomDepth)
			{
				Mesh->SetRenderCustomDepth(true);
				CustomDepthBodies.Add(Mesh);
			}
		}
	}
}

void UVCWaterEffectsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// A mesh that arrived since the swim began (an appearance loaded, a weapon drawn) joins the others.
	BodyRefreshSeconds -= DeltaTime;
	if (BodyRefreshSeconds <= 0.f)
	{
		SetBodyCustomDepth(true);
	}

	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	FVCWaterSurfaceSample Water;
	if (!Character || !FVCTerrainQueryHelper::SampleWaterSurface(GetWorld(), Character->GetActorLocation(), VCWaterEffectsPrivate::SurfaceReach, Water))
	{
		SetWakeActive(false);
		return;
	}
	const FVector Location = Character->GetActorLocation();
	const double HeadDepth = Water.Depth - Character->GetSimpleCollisionHalfHeight() * HeadHeightFraction;
	const float SurfaceSpeed = static_cast<float>(FVector::VectorPlaneProject(Character->GetVelocity(), Water.Up).Size());
	SetWakeActive(LeavesWake(SurfaceSpeed, HeadDepth, WakeMinSpeed, WakeMaxHeadDepth));
	if (Wake && bWakeActive)
	{
		// At the surface over the swimmer, the system's +Z along the surface's up.
		Wake->SetWorldLocationAndRotation(Water.SurfacePoint(Location), FRotationMatrix::MakeFromZ(Water.Up).Rotator());
	}
}
