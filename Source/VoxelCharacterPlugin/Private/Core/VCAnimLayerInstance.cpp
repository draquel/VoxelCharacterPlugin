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
}

void UVCAnimLayerInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	// On the game thread, before any worker runs the body's update: the body's values from its last update.
	const USkeletalMeshComponent* Mesh = GetSkelMeshComponent();
	CopyBodyValues(Mesh ? Cast<UVCAnimInstance>(Mesh->GetAnimInstance()) : nullptr);
}
