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
 * legs of the body's own graph. Each update it copies the body's aim and that slot's montage weight for an aim offset,
 * and since the character refinement plan's C2 the body's speed and stance weight for the moving stance, so every
 * stance layer has it as its parent. Since D85 a layer may fade its stance as the body moves (MovingStanceWeight: the
 * bow's carry gives the moving body to the locomotion) and read the body's crouch (the bow's crouched stance).
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

	/** The weight (0 to 1) of the montage playing in UpperBodySlot (DrawnAimWeight takes the larger of it and the raise). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float UpperBodyMontageWeight = 0.f;

	/** The body's AimRaiseWeight: how far the aim has raised the held weapon (the layer's raised pose). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float AimRaiseWeight = 0.f;

	/**
	 * How far the layer's aim branch (the raised pose, the UpperBody slot's draw and release, the drawn aim offset) lays
	 * over the upper body in mesh space: the raise or the montage, the larger, so a draw from the hip and a raise both
	 * show and a release while aiming returns to the raised pose.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float DrawnAimWeight = 0.f;

	/** The slot whose montage weight UpperBodyMontageWeight follows. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation")
	FName UpperBodySlot = TEXT("UpperBody");

	/**
	 * The body's speed across local up (uu/s), for the stance blend space (the set's idle, walk and jog; the character
	 * refinement plan's C2); 0 under vc.Anim.Stance 1, which holds the idle as before C2.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float Speed = 0.f;

	/**
	 * How far the layer's upper body lays over the locomotion (its layered blend's weight): the body's StanceWeight, faded
	 * by the body's speed toward MovingStanceWeight (D85; under vc.Anim.Stance 2 only).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float StanceWeight = 0.f;

	/** The body's bIsCrouching: a layer with a crouched stance (the bow's, D85) plays it instead of its stance blend space. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	bool bIsCrouching = false;

	/**
	 * The stance's weight once the body moves, as a share of its weight at a stand (D85): 1 keeps the stance at every gait
	 * (the swords', the tools'), 0 gives the moving body to the locomotion, whose arms swing (the bow's: the pack's carry
	 * barely moves the arms at a walk, so the bow walked and ran in its idle). Set per layer by setup_character_layers.py.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation", meta = (ClampMin = "0", ClampMax = "1"))
	float MovingStanceWeight = 1.f;

	/** The body's speeds (uu/s) between which the stance fades from its weight at a stand to MovingStanceWeight (D85). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation")
	FVector2D StanceFadeSpeeds = FVector2D(10.f, 150.f);

	/**
	 * Copies the body's values: its aim and aim raise, its montage weight in UpperBodySlot, its speed, crouch and stance
	 * weight (faded by the speed, D85). Each update does it on the game thread.
	 * @param Body The body's anim instance; null resets the values to 0
	 */
	void CopyBodyValues(const UVCAnimInstance* Body);

	/**
	 * Pure: the share of the stance's weight at a speed (D85): 1 up to FadeSpeeds.X, MovingWeight from FadeSpeeds.Y, linear
	 * between (a step at X when Y is not above it).
	 * @param Speed       The body's speed across local up (uu/s)
	 * @param FadeSpeeds  The speeds the fade starts and ends at (uu/s)
	 * @param MovingWeight The share once moving (0 to 1)
	 * @return The share (0 to 1)
	 */
	static float ComputeStanceFade(float Speed, const FVector2D& FadeSpeeds, float MovingWeight);

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};
