// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BoneContainer.h"
#include "BonePose.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "Engine/EngineTypes.h"
#include "VCAnimNode_FootBoneIK.generated.h"

/** One leg of FVCAnimNode_FootBoneIK: its foot bone; the calf and the thigh are the foot's parents. */
USTRUCT()
struct VOXELCHARACTERPLUGIN_API FVCFootBoneIKLeg
{
	GENERATED_BODY()

	/** The foot bone (foot_l, foot_r), placed on the ground: its parent (the calf) bends, the thigh above turns. */
	UPROPERTY(EditAnywhere, Category = "Leg")
	FBoneReference FootBone;

	FCompactPoseBoneIndex Foot = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex Calf = FCompactPoseBoneIndex(INDEX_NONE);
	FCompactPoseBoneIndex Thigh = FCompactPoseBoneIndex(INDEX_NONE);

	/** The foot's height over the component's ground plane in the reference pose: a foot this low is planted. */
	double RestHeight = 0.0;

	/** The ground's height under the foot over the component's ground plane, smoothed (component space). */
	double Offset = 0.0;

	/** The ground's normal under the foot, smoothed (component space). */
	FVector Normal = FVector::UpVector;
};

/**
 * Foot placement on the foot bones themselves (character animation P6), for bodies without the UE5 Mannequin's ik_foot
 * bones, which the engine's foot placement moves and so skips without: a MetaHuman's. Each foot traces the ground under
 * it along the mesh's own down (a planet's tilted frame needs nothing more), the pelvis drops to the lower foot, each
 * leg reaches its foot's ground by two-bone IK solved from the animated pose, and a foot near the ground tilts to its
 * normal. The heights and normals ease at InterpSpeed. Everything is in component space, where the reference ground is
 * the plane through the component's origin (the capsule's bottom).
 */
USTRUCT(BlueprintInternalUseOnly)
struct VOXELCHARACTERPLUGIN_API FVCAnimNode_FootBoneIK : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	/** The bone that drops with the lower foot, carrying the body (pelvis). */
	UPROPERTY(EditAnywhere, Category = "Settings")
	FBoneReference PelvisBone;

	/** The legs, each by its foot bone. */
	UPROPERTY(EditAnywhere, Category = "Settings")
	TArray<FVCFootBoneIKLeg> Legs;

	/** The trace's start over the reference ground (uu): the highest step a foot finds. */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ClampMin = "0"))
	float TraceUp = 50.f;

	/** The trace's end under the reference ground (uu): the deepest dip a foot finds. */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ClampMin = "0"))
	float TraceDown = 75.f;

	/** The sweep's radius (uu): a sphere, so a foot over a gap between voxel faces still finds the ground. */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ClampMin = "0"))
	float TraceRadius = 4.f;

	/** The furthest the pelvis drops (uu), so one foot on a block step does not crouch the body. */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ClampMin = "0"))
	float MaxPelvisDrop = 50.f;

	/** The highest a foot rises over its animated height (uu). */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ClampMin = "0"))
	float MaxFootRaise = 50.f;

	/** The largest tilt of a planted foot to the ground's normal (degrees). */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ClampMin = "0", ClampMax = "90"))
	float MaxFootTilt = 30.f;

	/** How far over its rest height a foot still tilts with the ground (uu): in its swing it keeps the pose's angle. */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ClampMin = "1"))
	float TiltFadeHeight = 15.f;

	/** How fast the ground's heights and normals follow the traces (FInterpTo speed); 0 snaps. */
	UPROPERTY(EditAnywhere, Category = "Settings", meta = (ClampMin = "0"))
	float InterpSpeed = 15.f;

	/** The channel the feet trace (the voxel terrain's collision answers Visibility). */
	UPROPERTY(EditAnywhere, Category = "Settings")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	// FAnimNode_Base interface
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	// FAnimNode_SkeletalControlBase interface
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	// End of FAnimNode_SkeletalControlBase interface

	/**
	 * Pure: how far the pelvis drops for the legs' ground heights.
	 * @param Offsets The ground's height under each foot over the reference ground (uu)
	 * @param MaxDrop The furthest drop (uu, positive)
	 * @return The pelvis' offset along the component's up: the lowest foot's height, clamped to [-MaxDrop, 0]
	 */
	static double PelvisOffsetFor(TConstArrayView<double> Offsets, double MaxDrop);

	/**
	 * Pure: a foot's rotation tilted to the ground's normal.
	 * @param FootRotation The animated foot's rotation (component space)
	 * @param Normal       The ground's normal under it (component space)
	 * @param MaxTiltDeg   The largest tilt (degrees)
	 * @param Weight       How planted the foot is (0 keeps the pose's rotation, 1 tilts fully)
	 * @return The rotation, turned by the shortest arc from up to the normal (clamped to MaxTiltDeg) times Weight
	 */
	static FQuat TiltFoot(const FQuat& FootRotation, const FVector& Normal, double MaxTiltDeg, double Weight);

private:
	virtual void InitializeBoneReferences(const FBoneContainer& RequiredBones) override;

	FCompactPoseBoneIndex Pelvis = FCompactPoseBoneIndex(INDEX_NONE);
	float DeltaTime = 0.f;
	double PelvisOffset = 0.0;

	/** The frame of the last evaluation: after a gap (the alpha at 0, a LOD that skips the node) the heights snap. */
	uint64 LastEvaluatedFrame = 0;
};
