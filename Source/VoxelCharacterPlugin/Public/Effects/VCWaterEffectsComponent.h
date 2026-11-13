// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineBaseTypes.h"
#include "Engine/EngineTypes.h"
#include "VCWaterEffectsComponent.generated.h"

class ACharacter;
class UAudioComponent;
class UNiagaraComponent;
class UNiagaraSystem;
class UPrimitiveComponent;
class USoundAttenuation;
class USoundBase;

/**
 * A character's effects at the water's surface (the water plan's W1, D114): a splash and its sound when it starts or
 * stops swimming, a wake and a swim loop while it moves at the surface.
 *
 * Cosmetic, and made on each machine from the character's own movement mode, which replicates (a simulated proxy's
 * mode arrives with its movement): no RPC, no replicated state of its own, so a client sees another player's splash
 * because it sees that player start to swim. AVCCharacterBase creates the component everywhere but on a dedicated
 * server (PlaysIn), where nothing would see or hear it.
 *
 * While the character swims its meshes (its own and its attached actors': a MetaHuman's parts) render custom depth: the
 * water material reads it to tell a body under the surface from a shallow bed, and draws no shore foam over a swimmer.
 *
 * The systems and sounds are the project's (UVCSettings: WaterSplashSystem, WaterWakeSystem, WaterSplashInSound,
 * WaterSplashOutSound, WaterSwimLoopSound); any of them missing is that effect absent. The component ticks only while
 * its character swims.
 */
UCLASS(ClassGroup = (VoxelCharacter))
class VOXELCHARACTERPLUGIN_API UVCWaterEffectsComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVCWaterEffectsComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Pure: whether a machine of this net mode plays the effects: every one but a dedicated server.
	 * @param NetMode The world's net mode
	 * @return True where someone can see or hear them
	 */
	static bool PlaysIn(ENetMode NetMode) { return NetMode != NM_DedicatedServer; }

	/**
	 * Pure: how hard a body hits the water, 0..1, from its speed into the surface: MinStrength wading in (no speed
	 * down), 1 at FullSpeed and faster.
	 * @param SpeedIntoWater The velocity's component along the surface's down, uu/s (rising out of the water: negative)
	 * @param FullSpeed The speed of a full splash, uu/s
	 * @param MinStrength The strength of the gentlest splash
	 * @return The strength
	 */
	static float SplashStrength(float SpeedIntoWater, float FullSpeed, float MinStrength);

	/**
	 * Pure: whether a swimmer leaves a wake: moving along the surface at least MinSpeed with its head no deeper than
	 * MaxHeadDepth under it.
	 * @param SurfaceSpeed The speed along the surface, uu/s
	 * @param HeadDepth The head's depth under the surface, uu (above it: negative)
	 * @param MinSpeed The least speed that leaves a wake
	 * @param MaxHeadDepth The deepest the head may be
	 * @return True when a wake shows
	 */
	static bool LeavesWake(float SurfaceSpeed, double HeadDepth, float MinSpeed, float MaxHeadDepth);

	/** @return How many splashes this component has played since BeginPlay (entries and exits; the tests and the QA row read it) */
	int32 GetSplashCount() const { return SplashCount; }

	/** @return True while the wake is on */
	bool IsWakeActive() const { return bWakeActive; }

	// --- Tuning ---

	/** The speed into the water of a full splash, uu/s (a fall of about four metres). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Water")
	float SplashFullSpeed = 900.f;

	/** The strength of a splash with no speed into the water (wading in), 0..1. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Water", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SplashMinStrength = 0.35f;

	/** The strength of the splash on leaving the water, 0..1. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Water", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SplashExitStrength = 0.4f;

	/** The splash system's scale at strength 0 and at strength 1. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Water")
	FVector2D SplashScaleRange = FVector2D(0.55, 1.5);

	/** The least time between two splashes, seconds: the swim's entry and exit depths are close, and a body at the line could cross both ways. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Water", meta = (ClampMin = "0.0"))
	float SplashMinInterval = 0.5f;

	/** The least speed along the surface that leaves a wake, uu/s. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Water", meta = (ClampMin = "0.0"))
	float WakeMinSpeed = 60.f;

	/** The deepest a swimmer's head may be under the surface and still leave a wake, uu. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Water", meta = (ClampMin = "0.0"))
	float WakeMaxHeadDepth = 45.f;

	/** The head's height over the capsule's centre, as a fraction of its half height. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Water", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float HeadHeightFraction = 0.8f;

	/** The distance at which the effects' sounds have faded out, uu. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Water", meta = (ClampMin = "100.0"))
	float SoundFalloffDistance = 3000.f;

	/** The swim loop's volume. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Water", meta = (ClampMin = "0.0"))
	float SwimLoopVolume = 0.5f;

protected:
	/** The character's movement mode changed (ACharacter::MovementModeChangedDelegate; every machine, proxies included). */
	UFUNCTION()
	void HandleMovementModeChanged(ACharacter* Character, EMovementMode PreviousMode, uint8 PreviousCustomMode);

	/**
	 * A splash at the surface over the character.
	 * @param bEntering Entering the water (the splash-in sound) or leaving it
	 * @param Strength 0..1: the system's scale and the sound's volume
	 */
	void PlaySplash(bool bEntering, float Strength);

	/** Turn the wake and the swim loop on or off. */
	void SetWakeActive(bool bActive);

	/**
	 * Turn custom depth on for the character's visible meshes that do not render it already (its own primitives and its
	 * attached actors'), or back off for exactly the ones this component turned on.
	 */
	void SetBodyCustomDepth(bool bOn);

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> SplashSystem;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraSystem> WakeSystem;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> SplashInSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> SplashOutSound;

	UPROPERTY(Transient)
	TObjectPtr<USoundBase> SwimLoopSound;

	/** The sounds' falloff with distance (a sound played at a location with none is heard everywhere at full volume). */
	UPROPERTY(Transient)
	TObjectPtr<USoundAttenuation> Attenuation;

	/** The wake, attached to the character and held at the surface over it while it swims. */
	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> Wake;

	/** The swim loop, attached to the character. */
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> SwimLoop;

	/** The meshes this component turned custom depth on for (never ones that rendered it already). */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> CustomDepthBodies;

	/** Seconds until the body's meshes are gathered again while swimming (an appearance or a weapon may change mid-swim). */
	float BodyRefreshSeconds = 0.f;

	bool bWakeActive = false;
	int32 SplashCount = 0;
	double LastSplashSeconds = -1.0e9;
};
