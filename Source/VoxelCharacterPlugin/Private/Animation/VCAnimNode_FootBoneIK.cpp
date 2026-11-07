// Copyright Daniel Raquel. All Rights Reserved.

#include "Animation/VCAnimNode_FootBoneIK.h"

#include "Animation/AnimInstanceProxy.h"
#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "TwoBoneIK.h"

double FVCAnimNode_FootBoneIK::PelvisOffsetFor(TConstArrayView<double> Offsets, double MaxDrop)
{
	double Lowest = 0.0;
	for (const double Offset : Offsets)
	{
		Lowest = FMath::Min(Lowest, Offset);
	}
	return FMath::Clamp(Lowest, -FMath::Max(MaxDrop, 0.0), 0.0);
}

FQuat FVCAnimNode_FootBoneIK::TiltFoot(const FQuat& FootRotation, const FVector& Normal, double MaxTiltDeg, double Weight)
{
	const FVector Up = Normal.GetSafeNormal();
	if (Up.IsZero() || Weight <= 0.0)
	{
		return FootRotation;
	}
	FVector Axis;
	double Angle = 0.0;
	FQuat::FindBetweenNormals(FVector::UpVector, Up).ToAxisAndAngle(Axis, Angle);
	Angle = FMath::Min(Angle, FMath::DegreesToRadians(FMath::Max(MaxTiltDeg, 0.0))) * FMath::Min(Weight, 1.0);
	return Angle > UE_KINDA_SMALL_NUMBER ? FQuat(Axis, Angle) * FootRotation : FootRotation;
}

void FVCAnimNode_FootBoneIK::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	FAnimNode_SkeletalControlBase::Initialize_AnyThread(Context);
	LastEvaluatedFrame = 0;
}

void FVCAnimNode_FootBoneIK::UpdateInternal(const FAnimationUpdateContext& Context)
{
	FAnimNode_SkeletalControlBase::UpdateInternal(Context);
	DeltaTime = Context.GetDeltaTime();
}

void FVCAnimNode_FootBoneIK::GatherDebugData(FNodeDebugData& DebugData)
{
	FString Line = DebugData.GetNodeName(this) + FString::Printf(TEXT("(pelvis %.1f"), PelvisOffset);
	for (const FVCFootBoneIKLeg& Leg : Legs)
	{
		Line += FString::Printf(TEXT(", %s %.1f"), *Leg.FootBone.BoneName.ToString(), Leg.Offset);
	}
	DebugData.AddDebugItem(Line + TEXT(")"));
	ComponentPose.GatherDebugData(DebugData);
}

bool FVCAnimNode_FootBoneIK::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	if (!Pelvis.IsValid() || Legs.IsEmpty())
	{
		return false;
	}
	for (const FVCFootBoneIKLeg& Leg : Legs)
	{
		if (!Leg.Foot.IsValid() || !Leg.Calf.IsValid() || !Leg.Thigh.IsValid())
		{
			return false;
		}
	}
	return true;
}

void FVCAnimNode_FootBoneIK::InitializeBoneReferences(const FBoneContainer& RequiredBones)
{
	PelvisBone.Initialize(RequiredBones);
	Pelvis = PelvisBone.GetCompactPoseIndex(RequiredBones);
	for (FVCFootBoneIKLeg& Leg : Legs)
	{
		Leg.FootBone.Initialize(RequiredBones);
		Leg.Foot = Leg.FootBone.GetCompactPoseIndex(RequiredBones);
		Leg.Calf = Leg.Foot.IsValid() ? RequiredBones.GetParentBoneIndex(Leg.Foot) : FCompactPoseBoneIndex(INDEX_NONE);
		Leg.Thigh = Leg.Calf.IsValid() ? RequiredBones.GetParentBoneIndex(Leg.Calf) : FCompactPoseBoneIndex(INDEX_NONE);
		Leg.RestHeight = Leg.Foot.IsValid() ? FAnimationRuntime::GetComponentSpaceRefPose(Leg.Foot, RequiredBones).GetLocation().Z : 0.0;
	}
	LastEvaluatedFrame = 0;
}

void FVCAnimNode_FootBoneIK::EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms)
{
	const USkeletalMeshComponent* Mesh = Output.AnimInstanceProxy->GetSkelMeshComponent();
	const UWorld* World = Mesh ? Mesh->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	const FTransform ComponentToWorld = Output.AnimInstanceProxy->GetComponentTransform();

	// After a gap the smoothed heights are stale (another ground, another frame): they snap to the traces
	const bool bSnap = InterpSpeed <= 0.f || LastEvaluatedFrame == 0 || GFrameCounter > LastEvaluatedFrame + 2;
	LastEvaluatedFrame = GFrameCounter;

	// The ground under each foot, traced along the component's down from TraceUp over the reference ground to TraceDown
	// under it (the engine's foot placement traces on this thread too)
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VCFootBoneIK), /*bTraceComplex=*/ false, Mesh->GetOwner());
	const FCollisionShape Sphere = FCollisionShape::MakeSphere(TraceRadius);
	TArray<double, TInlineAllocator<4>> Offsets;
	for (FVCFootBoneIKLeg& Leg : Legs)
	{
		const FVector FootCS = Output.Pose.GetComponentSpaceTransform(Leg.Foot).GetLocation();
		const FVector Start = ComponentToWorld.TransformPosition(FVector(FootCS.X, FootCS.Y, TraceUp));
		const FVector End = ComponentToWorld.TransformPosition(FVector(FootCS.X, FootCS.Y, -TraceDown));
		double TargetOffset = 0.0;
		FVector TargetNormal = FVector::UpVector;
		FHitResult Hit;
		if (World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, TraceChannel, Sphere, Params) && !Hit.bStartPenetrating)
		{
			TargetOffset = FMath::Clamp(ComponentToWorld.InverseTransformPosition(Hit.ImpactPoint).Z, -static_cast<double>(TraceDown),
				static_cast<double>(MaxFootRaise));
			TargetNormal = ComponentToWorld.InverseTransformVectorNoScale(Hit.ImpactNormal).GetSafeNormal();
		}
		if (bSnap)
		{
			Leg.Offset = TargetOffset;
			Leg.Normal = TargetNormal;
		}
		else
		{
			Leg.Offset = FMath::FInterpTo(Leg.Offset, TargetOffset, static_cast<double>(DeltaTime), static_cast<double>(InterpSpeed));
			Leg.Normal = FMath::VInterpTo(Leg.Normal, TargetNormal, DeltaTime, InterpSpeed).GetSafeNormal();
		}
		Offsets.Add(Leg.Offset);
	}

	// The pelvis drops to the lowest foot's ground; the legs ride on it
	PelvisOffset = PelvisOffsetFor(Offsets, MaxPelvisDrop);
	const FVector Drop(0.0, 0.0, PelvisOffset);
	FTransform PelvisCS = Output.Pose.GetComponentSpaceTransform(Pelvis);
	PelvisCS.AddToTranslation(Drop);
	OutBoneTransforms.Add(FBoneTransform(Pelvis, PelvisCS));

	// Each foot keeps its animated height over its own ground: the leg reaches it by two-bone IK from the animated pose
	// (the lengths come from that pose, so the shin never stretches), bending in the plane the knee bends in now
	for (const FVCFootBoneIKLeg& Leg : Legs)
	{
		FTransform Thigh = Output.Pose.GetComponentSpaceTransform(Leg.Thigh);
		FTransform Calf = Output.Pose.GetComponentSpaceTransform(Leg.Calf);
		FTransform Foot = Output.Pose.GetComponentSpaceTransform(Leg.Foot);
		const FVector AnimatedFoot = Foot.GetLocation();
		const FQuat AnimatedRotation = Foot.GetRotation();
		Thigh.AddToTranslation(Drop);
		Calf.AddToTranslation(Drop);
		Foot.AddToTranslation(Drop);

		// A straight leg has no bend plane of its own: the knee goes forward (the mesh's +Y, the skeletons' facing)
		FVector Bend = Calf.GetLocation() - 0.5 * (Thigh.GetLocation() + Foot.GetLocation());
		if (Bend.SizeSquared() < 1.0)
		{
			Bend = FVector(0.0, 1.0, 0.0);
		}
		const FVector JointTarget = Calf.GetLocation() + Bend.GetSafeNormal() * 100.0;
		const FVector Effector = AnimatedFoot + FVector(0.0, 0.0, Leg.Offset);
		AnimationCore::SolveTwoBoneIK(Thigh, Calf, Foot, JointTarget, Effector, /*bAllowStretching=*/ false, 1.0, 1.0);

		// A foot near the ground lies along it; one in its swing keeps the pose's angle
		const double Weight = 1.0 - FMath::Clamp((AnimatedFoot.Z - Leg.RestHeight) / FMath::Max(static_cast<double>(TiltFadeHeight), 1.0), 0.0, 1.0);
		Foot.SetRotation(TiltFoot(AnimatedRotation, Leg.Normal, MaxFootTilt, Weight));

		OutBoneTransforms.Add(FBoneTransform(Leg.Thigh, Thigh));
		OutBoneTransforms.Add(FBoneTransform(Leg.Calf, Calf));
		OutBoneTransforms.Add(FBoneTransform(Leg.Foot, Foot));
	}
	OutBoneTransforms.Sort(FCompareBoneTransformIndex());
}
