// Copyright Daniel Raquel. All Rights Reserved.

// vc.Anim.Probe (G284, dev builds): one log line per skeletal mesh with an anim instance in every game world, saying
// whether its body animates, in numbers rather than by eye. A body that plays no animation sits in its reference pose, so
// the line gives how far the pose is from the reference pose (the largest distance of a bone's component-space position
// from its place in the reference pose, and the mean) and how far it moved since the last probe of the same mesh; and,
// for why a body would not animate, the anim class, its compiled anim nodes (how many, and whether the Foot Bone IK is
// among them), its Blueprint's compile status where the process has the Blueprint (uncooked), and each state machine's
// current state. Scheduled with vox.After it works in a -game process, which has no Claudius.

#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimNode_StateMachine.h"
#include "Animation/VCAnimNode_FootBoneIK.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/Engine.h"
#include "Engine/SkinnedAsset.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include "VoxelCharacterPlugin.h"

namespace
{
	/** Each probed mesh's bone positions at its last probe, for the movement since. */
	TMap<TWeakObjectPtr<const USkeletalMeshComponent>, TArray<FVector>> GVCProbeLastPositions;

	/** The reference pose's component-space bone positions of a skeleton. */
	TArray<FVector> ReferencePositions(const FReferenceSkeleton& RefSkeleton)
	{
		const TArray<FTransform>& Local = RefSkeleton.GetRefBonePose();
		TArray<FTransform> Component;
		Component.SetNum(Local.Num());
		TArray<FVector> Positions;
		Positions.SetNum(Local.Num());
		for (int32 Bone = 0; Bone < Local.Num(); ++Bone)
		{
			const int32 Parent = RefSkeleton.GetParentIndex(Bone);
			Component[Bone] = Parent == INDEX_NONE ? Local[Bone] : Local[Bone] * Component[Parent];
			Positions[Bone] = Component[Bone].GetLocation();
		}
		return Positions;
	}

	/** The compiled graph's anim nodes: how many, and whether a Foot Bone IK is among them. */
	FString DescribeNodes(const UClass* AnimClass)
	{
		const IAnimClassInterface* Interface = IAnimClassInterface::GetFromClass(AnimClass);
		if (!Interface)
		{
			return TEXT("nodes=n/a");
		}
		int32 FootBoneIK = 0;
		for (const FStructProperty* Property : Interface->GetAnimNodeProperties())
		{
			if (Property && Property->Struct && Property->Struct->IsChildOf(FVCAnimNode_FootBoneIK::StaticStruct()))
			{
				++FootBoneIK;
			}
		}
		const TArray<FBakedAnimationStateMachine>& Machines = Interface->GetBakedStateMachines();
		return FString::Printf(TEXT("nodes=%d footBoneIK=%d machines=%d"), Interface->GetAnimNodeProperties().Num(),
			FootBoneIK, Machines.Num());
	}

	/** The Blueprint's compile status, where the process holds the Blueprint (an uncooked one). */
	const TCHAR* DescribeBlueprint(const UClass* AnimClass)
	{
#if WITH_EDITORONLY_DATA
		if (const UBlueprint* Blueprint = Cast<UBlueprint>(AnimClass->ClassGeneratedBy))
		{
			switch (Blueprint->Status)
			{
			case BS_Unknown: return TEXT("unknown");
			case BS_Dirty: return TEXT("dirty");
			case BS_Error: return TEXT("error");
			case BS_UpToDate: return TEXT("up-to-date");
			case BS_BeingCreated: return TEXT("being-created");
			case BS_UpToDateWithWarnings: return TEXT("warnings");
			default: return TEXT("?");
			}
		}
#endif
		return TEXT("none");
	}

	void OnProbeCommand(const TArray<FString>& Args)
	{
		if (!GEngine)
		{
			return;
		}
		int32 Probed = 0;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || !World->IsGameWorld())
			{
				continue;
			}
			for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
			{
				const USkeletalMeshComponent* Mesh = *It;
				const UAnimInstance* Anim = Mesh ? Mesh->GetAnimInstance() : nullptr;
				if (!Anim || Mesh->GetWorld() != World || !Mesh->GetSkinnedAsset() || !Mesh->IsRegistered())
				{
					continue;
				}
				const TArray<FTransform>& Pose = Mesh->GetComponentSpaceTransforms();
				const TArray<FVector> Reference = ReferencePositions(Mesh->GetSkinnedAsset()->GetRefSkeleton());
				const int32 Bones = FMath::Min(Pose.Num(), Reference.Num());
				double MaxFromReference = 0.0;
				double SumFromReference = 0.0;
				TArray<FVector> Positions;
				Positions.SetNum(Bones);
				for (int32 Bone = 0; Bone < Bones; ++Bone)
				{
					Positions[Bone] = Pose[Bone].GetLocation();
					const double Distance = FVector::Dist(Positions[Bone], Reference[Bone]);
					MaxFromReference = FMath::Max(MaxFromReference, Distance);
					SumFromReference += Distance;
				}
				FString Moved = TEXT("n/a");
				if (const TArray<FVector>* Last = GVCProbeLastPositions.Find(Mesh); Last && Last->Num() == Bones)
				{
					double MaxMoved = 0.0;
					for (int32 Bone = 0; Bone < Bones; ++Bone)
					{
						MaxMoved = FMath::Max(MaxMoved, FVector::Dist(Positions[Bone], (*Last)[Bone]));
					}
					Moved = FString::Printf(TEXT("%.2f"), MaxMoved);
				}
				GVCProbeLastPositions.Add(Mesh, MoveTemp(Positions));

				// Each state machine node's current state, read from the node and the class's baked machines with every
				// index checked: a graph compiled without some of its nodes need not match (the engine's
				// GetCurrentStateName dereferences a description the node may not have cached).
				FString States;
				if (const IAnimClassInterface* Interface = IAnimClassInterface::GetFromClass(Anim->GetClass()))
				{
					const TArray<FBakedAnimationStateMachine>& Baked = Interface->GetBakedStateMachines();
					int32 Listed = 0;
					for (const FStructProperty* Property : Interface->GetAnimNodeProperties())
					{
						if (Listed >= 3 || !Property || !Property->Struct || !Property->Struct->IsChildOf(FAnimNode_StateMachine::StaticStruct()))
						{
							continue;
						}
						const FAnimNode_StateMachine* Machine = Property->ContainerPtrToValuePtr<FAnimNode_StateMachine>(Anim);
						if (!Machine || !Baked.IsValidIndex(Machine->StateMachineIndexInClass))
						{
							continue;
						}
						const FBakedAnimationStateMachine& Description = Baked[Machine->StateMachineIndexInClass];
						const int32 State = Machine->GetCurrentState();
						States += FString::Printf(TEXT(" %s=%s"), *Description.MachineName.ToString(),
							Description.States.IsValidIndex(State) ? *Description.States[State].StateName.ToString() : TEXT("-"));
						++Listed;
					}
				}
				const AActor* Owner = Mesh->GetOwner();
				UE_LOG(LogVoxelCharacter, Display,
					TEXT("vc.Anim.Probe: %s.%s mesh=%s anim=%s blueprint=%s %s bones=%d fromRef max=%.2f mean=%.2f moved=%s speed=%.0f states:%s"),
					Owner ? *Owner->GetName() : TEXT("?"), *Mesh->GetName(), *Mesh->GetSkinnedAsset()->GetName(),
					*Anim->GetClass()->GetName(), DescribeBlueprint(Anim->GetClass()), *DescribeNodes(Anim->GetClass()),
					Bones, MaxFromReference, Bones > 0 ? SumFromReference / Bones : 0.0, *Moved,
					Owner ? Owner->GetVelocity().Size() : 0.0, States.IsEmpty() ? TEXT(" none") : *States);
				++Probed;
			}
		}
		UE_LOG(LogVoxelCharacter, Display, TEXT("vc.Anim.Probe: %d meshes"), Probed);
	}

	FAutoConsoleCommand GVCCmdAnimProbe(
		TEXT("vc.Anim.Probe"),
		TEXT("Logs each animated skeletal mesh in the game worlds: its anim class, compiled nodes and Blueprint status, its state machines' states, and its pose's distance from the reference pose and movement since the last probe (G284)."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&OnProbeCommand));
}

#endif // !UE_BUILD_SHIPPING
