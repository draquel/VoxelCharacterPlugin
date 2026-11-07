// Copyright Daniel Raquel. All Rights Reserved.

#include "Animation/VCAppearanceComponent.h"

#include "Animation/AnimClassInterface.h"
#include "Animation/AnimInstance.h"
#include "Animation/VCCharacterAppearance.h"
#include "ClothingSimulationInteractor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInterface.h"
#include "VoxelCharacterPlugin.h"

UVCAppearanceComponent::UVCAppearanceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

TSoftObjectPtr<UVCCharacterAppearance> UVCAppearanceComponent::PickAppearance(const TSoftObjectPtr<UVCCharacterAppearance>& Chosen,
	const TSoftObjectPtr<UVCCharacterAppearance>& ForGameMode, const TMap<EMeshingMode, TSoftObjectPtr<UVCCharacterAppearance>>& ByMeshing,
	const TSoftObjectPtr<UVCCharacterAppearance>& Default, bool bHasWorld, EMeshingMode Meshing)
{
	if (!Chosen.IsNull())
	{
		return Chosen;
	}
	if (!ForGameMode.IsNull())
	{
		return ForGameMode;
	}
	if (bHasWorld)
	{
		if (const TSoftObjectPtr<UVCCharacterAppearance>* ForMode = ByMeshing.Find(Meshing); ForMode && !ForMode->IsNull())
		{
			return *ForMode;
		}
	}
	return Default;
}

UClass* UVCAppearanceComponent::GetAppearanceAnimClass() const
{
	return Current && !Current->AnimClass.IsNull() ? Current->AnimClass.LoadSynchronous() : nullptr;
}

bool UVCAppearanceComponent::HasIKFootBones() const
{
	return !Current || Current->bHasIKFootBones;
}

bool UVCAppearanceComponent::ApplyAppearance(const UVCCharacterAppearance* Appearance)
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	USkeletalMeshComponent* Mesh = Character ? Character->GetMesh() : nullptr;
	if (!Mesh)
	{
		return false;
	}
	if (!bCapturedOriginal)
	{
		OriginalMesh = Mesh->GetSkeletalMeshAsset();
		OriginalMaterials = Mesh->OverrideMaterials;
		bCapturedOriginal = true;
	}
	RemoveParts();
	Current = Appearance;

	// The parts actor, where the character is drawn: its body component lends the mesh and the materials
	USkeletalMeshComponent* PartsBody = nullptr;
	UClass* PartsClass = Appearance && !Appearance->PartsActorClass.IsNull() ? Appearance->PartsActorClass.LoadSynchronous() : nullptr;
	if (Appearance && !Appearance->PartsActorClass.IsNull() && !PartsClass)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("%s: the appearance %s's parts actor %s does not load"),
			*GetNameSafe(Character), *Appearance->GetName(), *Appearance->PartsActorClass.ToString());
		return false;
	}
	UWorld* World = GetWorld();
	if (PartsClass && World && GetNetMode() != NM_DedicatedServer)
	{
		FActorSpawnParameters Params;
		Params.Owner = GetOwner();
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.ObjectFlags |= RF_Transient;
		PartsActor = World->SpawnActor<AActor>(PartsClass, Mesh->GetComponentTransform(), Params);
		if (!PartsActor)
		{
			return false;
		}
		PartsActor->SetActorEnableCollision(false);
		PartsActor->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		TArray<USkeletalMeshComponent*> Parts;
		PartsActor->GetComponents<USkeletalMeshComponent>(Parts);
		for (USkeletalMeshComponent* Part : Parts)
		{
			if (Part->GetFName() == Appearance->PartsBodyComponent)
			{
				PartsBody = Part;
			}
		}
	}

	// The body: the appearance's mesh, the parts actor's body (its class's default where none is spawned: a dedicated
	// server animates the same skeleton as its clients), or the class's own
	USkeletalMesh* Body = OriginalMesh;
	if (Appearance)
	{
		if (USkeletalMesh* Named = Appearance->BodyMesh.LoadSynchronous())
		{
			Body = Named;
		}
		else if (PartsBody && PartsBody->GetSkeletalMeshAsset())
		{
			Body = PartsBody->GetSkeletalMeshAsset();
		}
		else if (const USkeletalMeshComponent* Template = PartsClass ? Cast<USkeletalMeshComponent>(AActor::GetActorClassDefaultComponentByName(
			PartsClass, USkeletalMeshComponent::StaticClass(), Appearance->PartsBodyComponent)) : nullptr; Template && Template->GetSkeletalMeshAsset())
		{
			Body = Template->GetSkeletalMeshAsset();
		}
	}
	if (Mesh->GetSkeletalMeshAsset() != Body)
	{
		Mesh->SetSkeletalMesh(Body, /*bReinitPose=*/ true);
	}
	Mesh->EmptyOverrideMaterials();
	if (PartsBody)
	{
		for (int32 Slot = 0; Slot < PartsBody->GetNumMaterials(); ++Slot)
		{
			Mesh->SetMaterial(Slot, PartsBody->GetMaterial(Slot));
		}
	}
	else if (Body == OriginalMesh)
	{
		for (int32 Slot = 0; Slot < OriginalMaterials.Num(); ++Slot)
		{
			if (OriginalMaterials[Slot])
			{
				Mesh->SetMaterial(Slot, OriginalMaterials[Slot]);
			}
		}
	}

	// The other parts follow the animated mesh: by leader pose, or on their own AnimBP re-initialised on the new parent
	// (a leader pose its Blueprint gave it would hold the part to the hidden body)
	if (PartsActor)
	{
		TArray<USkeletalMeshComponent*> Parts;
		PartsActor->GetComponents<USkeletalMeshComponent>(Parts);
		for (USkeletalMeshComponent* Part : Parts)
		{
			if (Part == PartsBody)
			{
				Part->SetVisibility(false);
				Part->SetComponentTickEnabled(false);
				continue;
			}
			Part->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			if (Appearance->PartsWithOwnAnimation.Contains(Part->GetFName()))
			{
				Part->SetLeaderPoseComponent(nullptr);
				Part->InitAnim(true);
			}
			else
			{
				Part->SetLeaderPoseComponent(Mesh);
			}
		}
		SetPartsOwnerNoSee(Mesh->bOwnerNoSee, Mesh->bCastHiddenShadow);
	}
	RefreshSimulations();
	UE_LOG(LogVoxelCharacter, Log, TEXT("%s wears %s: body %s%s"), *GetNameSafe(Character), Appearance ? *Appearance->GetName() : TEXT("its own body"),
		*GetNameSafe(Mesh->GetSkeletalMeshAsset()), PartsActor ? *FString::Printf(TEXT(", parts from %s"), *PartsActor->GetClass()->GetName()) : TEXT(""));
	return true;
}

void UVCAppearanceComponent::SetPartsOwnerNoSee(bool bOwnerNoSee, bool bCastHiddenShadow)
{
	if (!PartsActor)
	{
		return;
	}
	TArray<UPrimitiveComponent*> Primitives;
	PartsActor->GetComponents<UPrimitiveComponent>(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		Primitive->SetOwnerNoSee(bOwnerNoSee);
		Primitive->bCastHiddenShadow = bCastHiddenShadow;
	}
}

void UVCAppearanceComponent::RefreshSimulations()
{
	Simulations.Reset();
	ClothMeshes.Reset();
	// The engine's rigid-body node exports no StaticStruct: found by name, its two gravity members by reflection
	static const UScriptStruct* RigidBody = FindObject<UScriptStruct>(nullptr, TEXT("/Script/AnimGraphRuntime.AnimNode_RigidBody"));
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	TArray<USkeletalMeshComponent*> Meshes;
	if (Character && Character->GetMesh())
	{
		Meshes.Add(Character->GetMesh());
	}
	if (PartsActor)
	{
		TArray<USkeletalMeshComponent*> Parts;
		PartsActor->GetComponents<USkeletalMeshComponent>(Parts);
		Meshes.Append(Parts);
	}
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (Mesh->GetSkeletalMeshAsset() && !Mesh->GetSkeletalMeshAsset()->GetMeshClothingAssets().IsEmpty())
		{
			ClothMeshes.Add(Mesh);
		}
		for (UAnimInstance* Instance : { Mesh->GetAnimInstance(), Mesh->GetPostProcessInstance() })
		{
			const IAnimClassInterface* AnimClass = Instance && RigidBody ? IAnimClassInterface::GetFromClass(Instance->GetClass()) : nullptr;
			if (!AnimClass)
			{
				continue;
			}
			for (const FStructProperty* Property : AnimClass->GetAnimNodeProperties())
			{
				if (Property->Struct->IsChildOf(RigidBody))
				{
					Simulations.Add({ Instance, Property->ContainerPtrToValuePtr<void>(Instance) });
				}
			}
		}
	}
	SetComponentTickEnabled(!Simulations.IsEmpty() || !ClothMeshes.IsEmpty());
	UpdateSimulationGravity();
}

void UVCAppearanceComponent::UpdateSimulationGravity()
{
	static const UScriptStruct* RigidBody = FindObject<UScriptStruct>(nullptr, TEXT("/Script/AnimGraphRuntime.AnimNode_RigidBody"));
	static const FBoolProperty* Override = RigidBody ? CastField<FBoolProperty>(RigidBody->FindPropertyByName(TEXT("bOverrideWorldGravity"))) : nullptr;
	static const FStructProperty* Gravity = RigidBody ? CastField<FStructProperty>(RigidBody->FindPropertyByName(TEXT("OverrideWorldGravity"))) : nullptr;
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}
	const FVector WorldGravity = Movement->GetGravityDirection() * FMath::Abs(Movement->GetGravityZ());
	for (const FSimulation& Simulation : Simulations)
	{
		if (Override && Gravity && Simulation.Instance.IsValid())
		{
			Override->SetPropertyValue_InContainer(Simulation.Node, true);
			*Gravity->ContainerPtrToValuePtr<FVector>(Simulation.Node) = WorldGravity;
		}
	}
	for (const TWeakObjectPtr<USkeletalMeshComponent>& Mesh : ClothMeshes)
	{
		if (UClothingSimulationInteractor* Cloth = Mesh.IsValid() ? Mesh->GetClothingSimulationInteractor() : nullptr)
		{
			Cloth->EnableGravityOverride(WorldGravity);
		}
	}
}

void UVCAppearanceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateSimulationGravity();
}

void UVCAppearanceComponent::RemoveParts()
{
	if (PartsActor)
	{
		PartsActor->Destroy();
		PartsActor = nullptr;
	}
}

void UVCAppearanceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RemoveParts();
	Super::EndPlay(EndPlayReason);
}
