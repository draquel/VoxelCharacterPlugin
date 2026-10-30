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

	// --- Block steps (world modes review WM-C7, D29) ---

	/**
	 * Ease the view through a step of a walker that steps whole blocks (UVCMovementComponent::IsSteppingBlocks, a cubic
	 * world): the pawn rises or drops a block in one frame, and the camera follows it over a short ease instead of
	 * jumping with it. Smooth worlds are not touched.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Camera|Steps")
	bool bEaseBlockSteps = true;

	/** How fast the eased step closes (FInterpTo speed; 12 closes most of a block in about 0.15 s). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Camera|Steps", meta = (ClampMin = "0.0"))
	float BlockStepEaseSpeed = 12.f;

	/**
	 * The step ease's next offset along local up (pure): a grounded walker whose height along up changed by a step
	 * (more than MinStep, at most MaxStep) since the last frame adds the opposite of that change to the offset, which then
	 * eases toward zero; anything else (a jump, a fall, a teleport) drops the offset.
	 *
	 * @param Offset The offset carried from the last frame
	 * @param HeightDelta Change of the pawn's height along up since the last frame
	 * @param bGroundedBoth The walker was on the ground last frame and is now
	 * @param MinStep Smallest change taken for a step
	 * @param MaxStep Largest change taken for a step (the walker's step height)
	 * @param DeltaTime Frame time
	 * @param EaseSpeed BlockStepEaseSpeed
	 * @return The offset to add to the camera along up this frame
	 */
	static float ComputeStepEaseOffset(float Offset, float HeightDelta, bool bGroundedBoth, float MinStep, float MaxStep, float DeltaTime, float EaseSpeed);

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

	/**
	 * Put the blended view somewhere until the next camera update (scripted views, tests): the crosshair traces aim from
	 * it (TraceForVoxel, the block feedback).
	 * @param Location View location
	 * @param Rotation View rotation
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Camera")
	void SetCurrentView(const FVector& Location, const FRotator& Rotation) { CurrentCameraLocation = Location; CurrentCameraRotation = Rotation; }

	/** Blend weight of the top (active) camera mode (0 = just pushed, 1 = fully blended). */
	float GetTopModeBlendWeight() const;

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

	// --- Block step ease ---
	float StepEaseOffset = 0.f;
	FVector LastPawnLocation = FVector::ZeroVector;
	bool bLastPawnGrounded = false;
	bool bHasLastPawnLocation = false;

	/**
	 * Pull the camera toward the pivot if terrain blocks the view.
	 * Uses ECC_Camera sphere trace which hits voxel terrain collision meshes.
	 */
	FVector ResolveVoxelCameraCollision(const FVector& IdealLocation, const FVector& PivotLocation) const;
};
