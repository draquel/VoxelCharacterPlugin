// Copyright Daniel Raquel. All Rights Reserved.

#include "Camera/VCCameraManager.h"
#include "Camera/CameraComponent.h"
#include "Core/VCCharacterBase.h"
#include "VoxelCharacterPlugin.h"

UVCCameraManager::UVCCameraManager()
{
	PrimaryComponentTick.bCanEverTick = false; // Ticked manually from character
}

void UVCCameraManager::BeginPlay()
{
	Super::BeginPlay();

	// Push the default third-person mode if stack is empty
	if (CameraModeStack.Num() == 0 && ThirdPersonModeClass)
	{
		PushCameraMode(ThirdPersonModeClass);
	}
}

// ---------------------------------------------------------------------------
// Camera Mode Stack
// ---------------------------------------------------------------------------

void UVCCameraManager::PushCameraMode(TSubclassOf<UVCCameraModeBase> CameraModeClass)
{
	if (!CameraModeClass)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("PushCameraMode: null class"));
		return;
	}

	UVCCameraModeBase* NewMode = NewObject<UVCCameraModeBase>(this, CameraModeClass);
	NewMode->CurrentBlendWeight = 0.f;
	CameraModeStack.Push(NewMode);
}

void UVCCameraManager::PopCameraMode()
{
	if (CameraModeStack.Num() > 1)
	{
		CameraModeStack.Pop();
	}
}

// ---------------------------------------------------------------------------
// Update Camera
// ---------------------------------------------------------------------------

void UVCCameraManager::UpdateCamera(float DeltaTime)
{
	if (CameraModeStack.Num() == 0)
	{
		return;
	}

	const AVCCharacterBase* Character = Cast<AVCCharacterBase>(GetOwner());
	if (!Character)
	{
		return;
	}

	// --- Update blend weights ---
	// Top mode blends toward 1.0, all others blend toward 0.0.
	const float BlendSpeed = (ModeTransitionBlendTime > KINDA_SMALL_NUMBER)
		? (1.f / ModeTransitionBlendTime)
		: 100.f; // Near-instant

	for (int32 i = 0; i < CameraModeStack.Num(); ++i)
	{
		UVCCameraModeBase* Mode = CameraModeStack[i];
		if (!Mode) continue;

		const bool bIsTopMode = (i == CameraModeStack.Num() - 1);
		const float TargetWeight = bIsTopMode ? 1.f : 0.f;
		Mode->CurrentBlendWeight = FMath::FInterpTo(Mode->CurrentBlendWeight, TargetWeight, DeltaTime, BlendSpeed);
	}

	// Remove fully blended-out modes (except the top one)
	for (int32 i = CameraModeStack.Num() - 2; i >= 0; --i)
	{
		if (CameraModeStack[i] && CameraModeStack[i]->CurrentBlendWeight <= KINDA_SMALL_NUMBER)
		{
			CameraModeStack.RemoveAt(i);
		}
	}

	// --- Compute blended transform ---
	FVector BlendedLocation = FVector::ZeroVector;
	FQuat BlendedRotation = FQuat::Identity;
	float BlendedFOV = 90.f;
	float TotalWeight = 0.f;

	for (const UVCCameraModeBase* Mode : CameraModeStack)
	{
		if (!Mode || Mode->CurrentBlendWeight <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FTransform DesiredTransform = Mode->ComputeDesiredTransform(Character, DeltaTime);
		const float W = Mode->CurrentBlendWeight;

		BlendedLocation += DesiredTransform.GetLocation() * W;
		// Accumulate rotation via weighted slerp
		if (TotalWeight <= KINDA_SMALL_NUMBER)
		{
			BlendedRotation = DesiredTransform.GetRotation();
		}
		else
		{
			const float SlerpAlpha = W / (TotalWeight + W);
			BlendedRotation = FQuat::Slerp(BlendedRotation, DesiredTransform.GetRotation(), SlerpAlpha);
		}
		BlendedFOV = FMath::Lerp(BlendedFOV, Mode->FieldOfView, W);
		TotalWeight += W;
	}

	// Normalize location by total weight
	if (TotalWeight > KINDA_SMALL_NUMBER)
	{
		BlendedLocation /= TotalWeight;
	}

	// --- Voxel camera collision (third-person only) ---
	if (bUseVoxelCameraCollision && CameraModeStack.Num() > 0)
	{
		// Check if the top mode is a third-person type (arm length > 0 means TP)
		// Use character location as pivot
		const FVector PivotLocation = Character->GetActorLocation() + FVector(0.f, 0.f, Character->BaseEyeHeight);
		BlendedLocation = ResolveVoxelCameraCollision(BlendedLocation, PivotLocation);
	}

	// --- Store results ---
	CurrentCameraLocation = BlendedLocation;
	CurrentCameraRotation = BlendedRotation.Rotator();
	CurrentFOV = BlendedFOV;

	// --- Apply to camera component ---
	if (CameraComponent)
	{
		CameraComponent->SetWorldLocationAndRotation(CurrentCameraLocation, CurrentCameraRotation);
		CameraComponent->SetFieldOfView(CurrentFOV);
	}
}

// ---------------------------------------------------------------------------
// Voxel Camera Collision (stub)
// ---------------------------------------------------------------------------

float UVCCameraManager::GetTopModeBlendWeight() const
{
	if (CameraModeStack.Num() > 0 && CameraModeStack.Last())
	{
		return CameraModeStack.Last()->CurrentBlendWeight;
	}
	return 1.f;
}

FVector UVCCameraManager::ResolveVoxelCameraCollision(const FVector& IdealLocation, const FVector& PivotLocation) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return IdealLocation;
	}

	// Sphere trace from pivot to desired camera location
	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetOwner());

	const bool bHit = World->SweepSingleByChannel(
		Hit,
		PivotLocation,
		IdealLocation,
		FQuat::Identity,
		ECC_Camera,
		FCollisionShape::MakeSphere(CameraCollisionProbeSize),
		Params
	);

	if (bHit)
	{
		// Pull camera to hit point, offset slightly toward pivot
		const FVector Dir = (PivotLocation - IdealLocation).GetSafeNormal();
		return Hit.Location + Dir * CameraCollisionProbeSize;
	}

	return IdealLocation;
}
