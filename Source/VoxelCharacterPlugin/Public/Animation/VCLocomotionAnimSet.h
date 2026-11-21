// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VCLocomotionAnimSet.generated.h"

class UAnimSequenceBase;
class UBlendSpace;

/**
 * A body's own locomotion (character animation P7): what the creature template AnimBP, which is bound to no skeleton,
 * plays for a body whose clips are its own (an animal's). The NPC names it (AVCNPCCharacterBase::LocomotionAnimSet) and
 * UVCAnimInstance hands it to the graph. A new animal is one of these, an action set for its attacks, hits and death, and
 * its row in the spawn table.
 */
UCLASS(BlueprintType)
class VOXELCHARACTERPLUGIN_API UVCLocomotionAnimSet : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Standing to running on one axis, the speed (uu/s); its samples sit at the clips' own speeds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion")
	TObjectPtr<UBlendSpace> Locomotion;

	/**
	 * The run clip's own speed (uu/s): a faster body plays the locomotion quicker (UVCAnimInstance::LocomotionPlayRate).
	 * 0 takes the anim instance's LocomotionReferenceSpeed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Locomotion", meta = (ClampMin = "0"))
	float ReferenceSpeed = 0.f;

	/** What the body does now and then while it stands, instead of its idle: a deer grazes, a wolf looks round. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle")
	TObjectPtr<UAnimSequenceBase> IdleVariant;

	/** Seconds standing before the variant plays. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle", meta = (ClampMin = "0"))
	float IdleVariantAfter = 5.f;

	/** Seconds the variant plays before the idle comes back (and the wait starts again). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Idle", meta = (ClampMin = "0"))
	float IdleVariantFor = 4.f;
};
