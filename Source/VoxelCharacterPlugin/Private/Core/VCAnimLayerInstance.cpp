// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCAnimLayerInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Core/VCAnimInstance.h"

UVCAnimLayerInstance::UVCAnimLayerInstance()
{
	// A montage is played on the body's instance; without this a slot inside the layer would never see it.
	bUseMainInstanceMontageEvaluationData = true;
}

void UVCAnimLayerInstance::CopyBodyValues(const UVCAnimInstance* Body)
{
	AimPitch = Body ? Body->AimPitch : 0.f;
	AimYaw = Body ? Body->AimYaw : 0.f;
	UpperBodyMontageWeight = Body ? Body->GetSlotMontageGlobalWeight(UpperBodySlot) : 0.f;
	Speed = Body && Body->StanceMode != 1 ? Body->Speed : 0.f;
	// The fade is the moving stance's (mode 2); mode 3 is C2's moving stance at every gait, mode 1 the static idle.
	const float Fade = Body && Body->StanceMode == 2 ? ComputeStanceFade(Body->Speed, StanceFadeSpeeds, MovingStanceWeight) : 1.f;
	StanceWeight = Body ? Body->StanceWeight * Fade : 0.f;
	bIsCrouching = Body && Body->bIsCrouching;
	AimRaiseWeight = Body ? Body->AimRaiseWeight : 0.f;
	DrawnAimWeight = FMath::Max(UpperBodyMontageWeight, AimRaiseWeight);
}

float UVCAnimLayerInstance::ComputeStanceFade(float Speed, const FVector2D& FadeSpeeds, float MovingWeight)
{
	const float Moving = FMath::Clamp(MovingWeight, 0.f, 1.f);
	if (FadeSpeeds.Y <= FadeSpeeds.X)
	{
		return Speed > FadeSpeeds.X ? Moving : 1.f;
	}
	const float Alpha = FMath::Clamp((Speed - FadeSpeeds.X) / (FadeSpeeds.Y - FadeSpeeds.X), 0.f, 1.f);
	return FMath::Lerp(1.f, Moving, Alpha);
}

void UVCAnimLayerInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	// On the game thread, before any worker runs the body's update: the body's values from its last update.
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	CopyBodyValues(Mesh ? Cast<UVCAnimInstance>(Mesh->GetAnimInstance()) : nullptr);
}
