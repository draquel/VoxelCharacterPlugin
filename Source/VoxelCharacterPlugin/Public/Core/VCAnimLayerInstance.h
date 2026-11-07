// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "VCAnimLayerInstance.generated.h"

class UVCAnimInstance;

/**
 * The parent of a stance layer that aims or plays montages of its own (an item's AnimLayerClass; the bow's, ranged
 * combat plan B4). Its slots play the montages the body's anim instance plays (bUseMainInstanceMontageEvaluationData),
 * so a montage meant for the upper body alone (the bow's draw and release) plays in a slot inside the layer, over the
 * legs of the body's own graph. Each update it copies the body's aim and that slot's montage weight for an aim offset.
 * A layer with neither (the swords', the pickaxes') can keep UAnimInstance as its parent.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCAnimLayerInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UVCAnimLayerInstance();

	/** The body's AimPitch (UVCAnimInstance, degrees above the facing), as of its last update. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float AimPitch = 0.f;

	/** The body's AimYaw (degrees from the facing, positive to the right), as of its last update. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float AimYaw = 0.f;

	/** The weight (0 to 1) of the montage playing in UpperBodySlot: how far the layer's drawn aim offset applies. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float UpperBodyMontageWeight = 0.f;

	/** The slot whose montage weight UpperBodyMontageWeight follows. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation")
	FName UpperBodySlot = TEXT("UpperBody");

	/**
	 * Copies the body's values: its aim, and its montage weight in UpperBodySlot. Each update does it on the game thread.
	 * @param Body The body's anim instance; null resets the values to 0
	 */
	void CopyBodyValues(const UVCAnimInstance* Body);

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};
