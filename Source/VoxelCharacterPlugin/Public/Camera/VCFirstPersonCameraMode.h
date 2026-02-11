// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Camera/VCCameraModeBase.h"
#include "VCFirstPersonCameraMode.generated.h"

/**
 * First-person camera mode.
 *
 * Locks the camera to the character's head socket (or eye height fallback)
 * with a configurable offset and a wider FOV.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCFirstPersonCameraMode : public UVCCameraModeBase
{
	GENERATED_BODY()

public:
	UVCFirstPersonCameraMode();

	virtual FTransform ComputeDesiredTransform(const AVCCharacterBase* Character, float DeltaTime) const override;

	/** Skeleton socket to attach the camera to. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Camera")
	FName HeadSocketName = "head";

	/** Fine-tune offset from the socket / eye height. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Camera")
	FVector EyeOffset = FVector(0.f, 0.f, 5.f);
};
