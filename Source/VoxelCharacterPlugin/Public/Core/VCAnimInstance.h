// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Core/VCTypes.h"
#include "VCAnimInstance.generated.h"

/**
 * Animation instance proxy for the voxel character.
 *
 * Populates BlueprintReadOnly properties each frame from the owning
 * character and its components. AnimBP graph reads these directly.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	// ==================== Locomotion ====================

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float Speed = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float Direction = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	bool bIsFalling = false;

	/** True for one frame when the character transitions from falling to grounded. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	bool bJustLanded = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	bool bIsCrouching = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	bool bIsAccelerating = false;

	// ==================== View Mode ====================

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	EVCViewMode ViewMode = EVCViewMode::ThirdPerson;

	// ==================== Aim ====================

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float AimPitch = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float AimYaw = 0.f;

	// ==================== Equipment ====================

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	EVCEquipmentAnimType ActiveItemAnimType = EVCEquipmentAnimType::Unarmed;

	// ==================== Surface ====================

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	EVoxelSurfaceType SurfaceType = EVoxelSurfaceType::Default;

	// ==================== Swimming ====================

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	bool bIsSwimming = false;

	/** Depth below water surface (world units). 0 when not underwater. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float WaterDepth = 0.f;
};
