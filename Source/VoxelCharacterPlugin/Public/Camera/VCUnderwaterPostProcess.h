// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VCUnderwaterPostProcess.generated.h"

class UPostProcessComponent;
class UVCMovementComponent;

/**
 * Underwater post-processing effect component.
 *
 * Toggles a post-process effect based on the camera's position relative
 * to voxel water. When the camera is inside a water-flagged voxel, the
 * effect ramps on (blue tint, fog, vignette). Uses camera position (not
 * character feet) so third-person view above water while character is
 * submerged does NOT trigger the effect.
 *
 * Attach to the character and it auto-discovers the movement component
 * for water state queries.
 */
UCLASS(ClassGroup = (VoxelCharacter), meta = (BlueprintSpawnableComponent))
class VOXELCHARACTERPLUGIN_API UVCUnderwaterPostProcess : public UActorComponent
{
	GENERATED_BODY()

public:
	UVCUnderwaterPostProcess();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// --- Tuning ---

	/** Blue/green tint applied underwater via scene color multiply. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater")
	FLinearColor UnderwaterTint = FLinearColor(0.15f, 0.4f, 0.7f, 1.0f);

	/** Exponential fog density underwater. Higher = murkier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater", meta = (ClampMin = "0.0", ClampMax = "0.1"))
	float UnderwaterFogDensity = 0.02f;

	/** Maximum distance visible underwater (fog falloff end). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater", meta = (ClampMin = "100.0"))
	float UnderwaterFogMaxDistance = 5000.f;

	/** Fog color underwater. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater")
	FLinearColor UnderwaterFogColor = FLinearColor(0.05f, 0.15f, 0.3f, 1.0f);

	/** Vignette intensity underwater (0-1). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float UnderwaterVignetteIntensity = 0.6f;

	/** How quickly the effect blends on/off (seconds for 0->1 transition). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "VoxelCharacter|Underwater", meta = (ClampMin = "0.01"))
	float BlendSpeed = 4.0f;

protected:
	/** Runtime post-process component (created in BeginPlay). */
	UPROPERTY()
	TObjectPtr<UPostProcessComponent> PostProcessComp;

	/** Cached movement component for water state queries. */
	UPROPERTY()
	TObjectPtr<UVCMovementComponent> MovementComp;

	/** Current blend weight (0 = no effect, 1 = full effect). */
	float CurrentBlendWeight = 0.f;

	/** Check if the camera position is inside a water voxel. */
	bool IsCameraUnderwater() const;
};
