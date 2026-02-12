// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Camera/VCCameraModeBase.h"
#include "VCCameraManager.generated.h"

class AVCCharacterBase;
class UCameraComponent;

/**
 * Manages a stack of camera modes with smooth blending transitions.
 *
 * Sits on AVCCharacterBase and drives a UCameraComponent each tick.
 * Supports voxel-aware camera collision in third-person mode
 * via ECC_Camera sphere trace against voxel terrain collision meshes.
 */
UCLASS(ClassGroup = (VoxelCharacter), meta = (BlueprintSpawnableComponent))
class VOXELCHARACTERPLUGIN_API UVCCameraManager : public UActorComponent
{
	GENERATED_BODY()

public:
	UVCCameraManager();

	// --- Camera Mode Stack ---

	/** Push a new camera mode onto the top of the stack (begins blend-in). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Camera")
	void PushCameraMode(TSubclassOf<UVCCameraModeBase> CameraModeClass);

	/** Pop the top camera mode off the stack. */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Camera")
	void PopCameraMode();

	/** Called each tick to compute and apply the blended camera transform. */
	void UpdateCamera(float DeltaTime);

	// --- Configuration ---

	/** Default first-person camera mode class. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Camera")
	TSubclassOf<UVCCameraModeBase> FirstPersonModeClass;

	/** Default third-person camera mode class. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Camera")
	TSubclassOf<UVCCameraModeBase> ThirdPersonModeClass;

	/** Blend time when transitioning between camera modes (seconds). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Camera", meta = (ClampMin = "0.0"))
	float ModeTransitionBlendTime = 0.3f;

	// --- Voxel Camera Collision ---

	/** Enable camera-to-terrain collision checks in third-person mode. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Camera|Collision")
	bool bUseVoxelCameraCollision = true;

	/** Radius of the sphere trace used for camera collision. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Camera|Collision", meta = (ClampMin = "0.0"))
	float CameraCollisionProbeSize = 12.f;

	// --- Accessors ---

	/** Current blended camera world location. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Camera")
	FVector GetCurrentCameraLocation() const { return CurrentCameraLocation; }

	/** Current blended camera world rotation. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Camera")
	FRotator GetCurrentCameraRotation() const { return CurrentCameraRotation; }

	/** Current blended FOV (degrees). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Camera")
	float GetCurrentFOV() const { return CurrentFOV; }

	/** Set the camera component this manager drives (called by character on construction). */
	void SetCameraComponent(UCameraComponent* InCamera) { CameraComponent = InCamera; }

protected:
	virtual void BeginPlay() override;

	/** The camera component we write our results into each frame. */
	UPROPERTY()
	TObjectPtr<UCameraComponent> CameraComponent;

	/** Active camera mode stack (top = current target mode). */
	UPROPERTY()
	TArray<TObjectPtr<UVCCameraModeBase>> CameraModeStack;

	// --- Blended Output ---
	FVector CurrentCameraLocation = FVector::ZeroVector;
	FRotator CurrentCameraRotation = FRotator::ZeroRotator;
	float CurrentFOV = 90.f;

	/**
	 * Pull the camera toward the pivot if terrain blocks the view.
	 * Uses ECC_Camera sphere trace which hits voxel terrain collision meshes.
	 */
	FVector ResolveVoxelCameraCollision(const FVector& IdealLocation, const FVector& PivotLocation) const;
};
