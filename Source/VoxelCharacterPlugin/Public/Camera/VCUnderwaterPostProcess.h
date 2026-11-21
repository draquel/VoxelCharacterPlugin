// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VCUnderwaterPostProcess.generated.h"

class APawn;
class UAudioComponent;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
struct FVCWaterSurfaceSample;

/**
 * The local player's view under water (the water plan's W1).
 *
 * Two unbound post-process volumes on the pawn, driven by the camera's position against the sea
 * (FVCTerrainQueryHelper::SampleWaterSurface), not the character's: in third person the camera may be above water
 * while the character is submerged.
 *  - The view volume: vignette and bloom, blended in while the camera is under the surface (in water and below the sea's
 *    level: the voxel that holds the water line is flagged top to bottom, so the flag alone tinted a camera just above
 *    the line).
 *  - The water volume, when UVCSettings names the underwater material: a post-process material that decides per pixel
 *    whether the near plane's point is under the surface, so the screen splits at the waterline as the camera crosses,
 *    and under it tints the scene and fogs it by scene depth (the view ends in fog; the seabed no longer shows to the
 *    horizon, G185). On while the camera is in water or within reach of it above (the near plane's). Without the material the view
 *    volume carries the tint for the whole screen, as before W1.
 * A looping sound plays while the camera is under (UVCSettings::WaterUnderLoopSound).
 *
 * The effect is the local player's view (G133): its volumes are unbound, so they affect every view in their world, and
 * the only pawn that may own them is the one a local player controller views through (IsLocalPlayerView).
 * AVCCharacterBase creates this component on that pawn alone and destroys it when the controller goes; a pawn with no
 * local player controller (another player's character on a client, an NPC, anything on a dedicated server) has no view
 * to tint. Should one exist elsewhere, its tick holds the volumes off.
 */
UCLASS(ClassGroup = (VoxelCharacter), meta = (BlueprintSpawnableComponent))
class VOXELCHARACTERPLUGIN_API UVCUnderwaterPostProcess : public UActorComponent
{
	GENERATED_BODY()

public:
	UVCUnderwaterPostProcess();

	virtual void BeginPlay() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * Whether a pawn is viewed through a player controller on this machine: the one condition under which the effect
	 * may exist and blend (G133).
	 * @param Pawn The pawn (null allowed).
	 * @return True when the pawn's controller is a player controller local to this machine.
	 */
	static bool IsLocalPlayerView(const APawn* Pawn);

	/**
	 * Pure: the tint at a camera depth: ShallowTint at the surface, DeepTint from FullDepth down.
	 * @param CameraDepth The camera's depth under the surface, uu (at or above the surface: the shallow tint)
	 * @param ShallowTint The tint just under the surface
	 * @param DeepTint The tint at FullDepth and deeper
	 * @param FullDepth The depth at which the tint is the deep one, uu
	 * @return The tint
	 */
	static FLinearColor TintAtDepth(double CameraDepth, const FLinearColor& ShallowTint, const FLinearColor& DeepTint, float FullDepth);

	/** @return True when the water volume and its material exist (UVCSettings::UnderwaterMaterial loaded): fog and the waterline are on */
	bool HasWaterView() const { return WaterMaterial != nullptr; }

	/** @return The view volume's blend weight: 0 above water, 1 once the camera has been under for a moment */
	float GetBlendWeight() const { return CurrentBlendWeight; }

	// --- Tuning ---

	/** The scene's tint under water, at depth (a scene colour multiply). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater")
	FLinearColor UnderwaterTint = FLinearColor(0.35f, 0.62f, 0.8f, 1.0f);

	/** The tint just under the surface; it deepens to UnderwaterTint over TintFullDepth. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater")
	FLinearColor UnderwaterShallowTint = FLinearColor(0.7f, 0.85f, 0.92f, 1.0f);

	/** The camera depth at which the tint is fully UnderwaterTint, uu. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater", meta = (ClampMin = "1.0"))
	float TintFullDepth = 600.f;

	/**
	 * The fog's density under water, per uu of distance: the share of the scene left at distance d is exp(-density x d)
	 * (0.0006: about half at 11.5 m). Higher is murkier.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater", meta = (ClampMin = "0.0", ClampMax = "0.1"))
	float UnderwaterFogDensity = 0.0006f;

	/** The distance at which the fog is complete whatever the density, uu: nothing shows past it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater", meta = (ClampMin = "100.0"))
	float UnderwaterFogMaxDistance = 5000.f;

	/** The fog's colour under water: the water's own, lit by the sun and the moon reaching the ground at the view. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater")
	FLinearColor UnderwaterFogColor = FLinearColor(0.05f, 0.15f, 0.3f, 1.0f);

	/** The camera depth over which the fog's light falls to about a third, uu: deeper water is darker. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater", meta = (ClampMin = "1.0"))
	float UnderwaterLightFalloffDepth = 3000.f;

	/**
	 * The fog's least light, as a share of what the eye is adapted to (0 = black water at night; 1 = as bright as the
	 * view's mid tone): a night dive shows a dim murk instead of nothing.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UnderwaterNightFloor = 0.3f;

	/**
	 * The least height above the surface at which the camera still gets the water volume, uu. The near plane's lower
	 * edge dips under before the camera does, so the reach is never less than 1.6 near-plane distances.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater", meta = (ClampMin = "0.0"))
	float WaterlineReach = 2.f;

	/** Vignette intensity underwater (0-1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UnderwaterVignetteIntensity = 0.6f;

	/** How quickly the view volume blends on and off (FInterpTo's speed: the larger, the quicker). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater", meta = (ClampMin = "0.01"))
	float BlendSpeed = 4.0f;

	/** The under-water loop's volume. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater", meta = (ClampMin = "0.0"))
	float UnderLoopVolume = 0.6f;

protected:
	/** The view volume: vignette, bloom (and the tint when there is no water material). Created in BeginPlay. */
	UPROPERTY()
	TObjectPtr<UPostProcessComponent> PostProcessComp;

	/** The water volume: the underwater material alone. Null when UVCSettings names no material. */
	UPROPERTY()
	TObjectPtr<UPostProcessComponent> WaterPostProcessComp;

	/** The underwater material's instance on the water volume. */
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> WaterMaterial;

	/** The loop heard while the camera is under the surface. Null when UVCSettings names no sound. */
	UPROPERTY()
	TObjectPtr<UAudioComponent> UnderLoop;

	/** Current blend weight of the view volume (0 = no effect, 1 = full effect). */
	float CurrentBlendWeight = 0.f;

	/** Whether the camera was under the surface last tick (the loop fades on the change). */
	bool bWasUnder = false;

	/**
	 * The water at the local player's camera; false for a pawn no local player controller views through, and in a world
	 * with no sea.
	 * @param OutSample Receives the sea's depth and up at the camera and whether water is there
	 * @return True when there is a local view and a sea
	 */
	bool SampleCamera(FVCWaterSurfaceSample& OutSample) const;

	/** Turn both volumes and the loop off (no local view). */
	void HoldOff();
};
