// Copyright Daniel Raquel. All Rights Reserved.

// vc.Appearance.TraceLOD (G278, dev builds): one log line, and a named scope in an Insights trace, whenever a worn body's
// skeletal mesh changes the state that decides whether its anim graph's nodes cache their bones again: its LOD (predicted
// and forced), whether it was recently drawn, its visibility-based tick option, its update rate, its post-process
// AnimBP switch, and the serial of its anim and post-process instances' required bones (a new serial is a rebuild:
// every node of that graph runs CacheBones on its next evaluation). Sampled once a frame after the world has ticked.

#include "Animation/VCAppearanceComponent.h"

#if !UE_BUILD_SHIPPING

#include "Animation/AnimInstance.h"
#include "Engine/EngineTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/LODSyncComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/UObjectIterator.h"
#include "VoxelCharacterPlugin.h"

namespace
{
	/** One sampled state of a worn mesh. */
	struct FVCBodyLODState
	{
		int32 PredictedLOD = -1;
		int32 ForcedLOD = -1;
		bool bRendered = false;
		bool bVisible = false;
		bool bUpToDate = true;
		bool bPostProcessOff = false;
		int32 TickOption = 0;
		int32 UpdateRate = 0;
		bool bSkipUpdate = false;
		bool bSkipEvaluation = false;
		uint16 AnimSerial = 0;
		uint16 PostProcessSerial = 0;
		FString Instances;
	};

	TMap<TWeakObjectPtr<USkeletalMeshComponent>, FVCBodyLODState> GLastStates;
	FTSTicker::FDelegateHandle GTickerHandle;

	FVCBodyLODState Sample(const USkeletalMeshComponent& Mesh)
	{
		FVCBodyLODState State;
		State.PredictedLOD = Mesh.GetPredictedLODLevel();
		State.ForcedLOD = Mesh.GetForcedLOD();
		State.bRendered = Mesh.WasRecentlyRendered(0.2f);
		State.bVisible = Mesh.IsVisible();
		State.bUpToDate = Mesh.bRequiredBonesUpToDate;
		State.bPostProcessOff = Mesh.GetDisablePostProcessBlueprint();
		State.TickOption = static_cast<int32>(Mesh.VisibilityBasedAnimTickOption);
		if (Mesh.AnimUpdateRateParams)
		{
			State.UpdateRate = Mesh.AnimUpdateRateParams->UpdateRate;
			State.bSkipUpdate = Mesh.AnimUpdateRateParams->ShouldSkipUpdate();
			State.bSkipEvaluation = Mesh.AnimUpdateRateParams->ShouldSkipEvaluation();
		}
		// The any-thread read: the game-thread getter would wait for the parallel evaluation
		if (const UAnimInstance* Anim = Mesh.GetAnimInstance())
		{
			State.AnimSerial = Anim->GetRequiredBonesOnAnyThread().GetSerialNumber();
			State.Instances = Anim->GetClass()->GetName();
		}
		if (const UAnimInstance* Post = Mesh.GetPostProcessInstance())
		{
			State.PostProcessSerial = Post->GetRequiredBonesOnAnyThread().GetSerialNumber();
			State.Instances += TEXT("+") + Post->GetClass()->GetName();
		}
		return State;
	}

	FString Describe(const FVCBodyLODState& S)
	{
		return FString::Printf(TEXT("lod=%d forced=%d rendered=%d visible=%d bonesUpToDate=%d ppOff=%d tickOption=%d uro=%d/%d%d animSerial=%u ppSerial=%u [%s]"),
			S.PredictedLOD, S.ForcedLOD, S.bRendered, S.bVisible, S.bUpToDate, S.bPostProcessOff, S.TickOption, S.UpdateRate,
			S.bSkipUpdate, S.bSkipEvaluation, S.AnimSerial, S.PostProcessSerial, *S.Instances);
	}

	FString Changes(const FVCBodyLODState& A, const FVCBodyLODState& B)
	{
		TArray<FString> Out;
		if (A.PredictedLOD != B.PredictedLOD) { Out.Add(FString::Printf(TEXT("lod %d->%d"), A.PredictedLOD, B.PredictedLOD)); }
		if (A.ForcedLOD != B.ForcedLOD) { Out.Add(FString::Printf(TEXT("forced %d->%d"), A.ForcedLOD, B.ForcedLOD)); }
		if (A.bRendered != B.bRendered) { Out.Add(FString::Printf(TEXT("rendered %d->%d"), A.bRendered, B.bRendered)); }
		if (A.bVisible != B.bVisible) { Out.Add(FString::Printf(TEXT("visible %d->%d"), A.bVisible, B.bVisible)); }
		if (A.bUpToDate != B.bUpToDate) { Out.Add(FString::Printf(TEXT("bonesUpToDate %d->%d"), A.bUpToDate, B.bUpToDate)); }
		if (A.bPostProcessOff != B.bPostProcessOff) { Out.Add(FString::Printf(TEXT("ppOff %d->%d"), A.bPostProcessOff, B.bPostProcessOff)); }
		if (A.TickOption != B.TickOption) { Out.Add(FString::Printf(TEXT("tickOption %d->%d"), A.TickOption, B.TickOption)); }
		if (A.UpdateRate != B.UpdateRate) { Out.Add(FString::Printf(TEXT("uro %d->%d"), A.UpdateRate, B.UpdateRate)); }
		if (A.AnimSerial != B.AnimSerial) { Out.Add(TEXT("ANIM BONES REBUILT")); }
		if (A.PostProcessSerial != B.PostProcessSerial) { Out.Add(TEXT("PP BONES REBUILT")); }
		if (A.Instances != B.Instances) { Out.Add(TEXT("instances changed")); }
		return FString::Join(Out, TEXT(", "));
	}

	/** The distance from the first local player's camera in the mesh's world, or -1. */
	double CameraDistance(const USkeletalMeshComponent& Mesh)
	{
		const UWorld* World = Mesh.GetWorld();
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		return PC && PC->PlayerCameraManager ? FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(), Mesh.Bounds.Origin) : -1.0;
	}

	bool TraceTick(float)
	{
		TSet<TWeakObjectPtr<USkeletalMeshComponent>> Seen;
		for (TObjectIterator<UVCAppearanceComponent> It; It; ++It)
		{
			const UVCAppearanceComponent* Appearance = *It;
			const UWorld* World = Appearance->GetWorld();
			const ACharacter* Character = Cast<ACharacter>(Appearance->GetOwner());
			if (!World || !World->IsGameWorld() || !Character || !Character->GetMesh() || !Appearance->GetAppearance())
			{
				continue;
			}
			TArray<USkeletalMeshComponent*> Meshes = { Character->GetMesh() };
			if (const AActor* Parts = Appearance->GetPartsActor())
			{
				TArray<USkeletalMeshComponent*> PartMeshes;
				Parts->GetComponents<USkeletalMeshComponent>(PartMeshes);
				Meshes.Append(PartMeshes);
			}
			for (USkeletalMeshComponent* Mesh : Meshes)
			{
				if (!Mesh->IsRegistered())
				{
					continue;
				}
				Seen.Add(Mesh);
				const FVCBodyLODState Now = Sample(*Mesh);
				FVCBodyLODState* Last = GLastStates.Find(Mesh);
				const FString Change = Last ? Changes(*Last, Now) : FString(TEXT("first sample"));
				if (!Change.IsEmpty())
				{
					const FString Name = FString::Printf(TEXT("%s.%s"), *GetNameSafe(Mesh->GetOwner()), *Mesh->GetName());
					UE_LOG(LogVoxelCharacter, Display, TEXT("[G278] f=%llu t=%.3f %s dist=%.0f %s | %s"), static_cast<uint64>(GFrameCounter),
						World->GetRealTimeSeconds(), *Name, CameraDistance(*Mesh), *Change, *Describe(Now));
					// A zero-length named scope on the game thread marks the frame in an Insights trace
					TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("G278 %s: %s"), *Mesh->GetName(), *Change));
				}
				GLastStates.Add(Mesh, Now);
			}
		}
		for (auto It = GLastStates.CreateIterator(); It; ++It)
		{
			if (!Seen.Contains(It.Key()))
			{
				It.RemoveCurrent();
			}
		}
		return true;
	}

	void OnTraceLODChanged(IConsoleVariable* Variable)
	{
		const bool bOn = Variable->GetInt() != 0;
		if (bOn && !GTickerHandle.IsValid())
		{
			GTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TraceTick));
		}
		else if (!bOn && GTickerHandle.IsValid())
		{
			FTSTicker::GetCoreTicker().RemoveTicker(GTickerHandle);
			GTickerHandle.Reset();
			GLastStates.Reset();
		}
	}

	/** Sets every LOD sync in play to the cvar's LOD (a MetaHuman parts actor's): -1 gives each its own drivers back. */
	void OnPartsForcedLODChanged(IConsoleVariable* Variable)
	{
		for (TObjectIterator<ULODSyncComponent> It; It; ++It)
		{
			const UWorld* World = It->GetWorld();
			if (World && World->IsGameWorld())
			{
				It->ForcedLOD = Variable->GetInt();
			}
		}
	}

	TAutoConsoleVariable<int32> CVarPartsForcedLOD(
		TEXT("vc.Appearance.PartsForcedLOD"),
		-1,
		TEXT("Dev (G278, D195): force every LOD sync in play (a MetaHuman parts actor's) to this LOD; -1 lets each follow its drivers (the face). Applies to the syncs that exist when set."),
		FConsoleVariableDelegate::CreateStatic(&OnPartsForcedLODChanged));

	TAutoConsoleVariable<int32> CVarTraceLOD(
		TEXT("vc.Appearance.TraceLOD"),
		0,
		TEXT("Dev: log each change of a worn body's meshes' LOD, drawn state, update rate, post-process switch and required-bones rebuilds (G278), with a named scope in an Insights trace. 0 off, 1 on."),
		FConsoleVariableDelegate::CreateStatic(&OnTraceLODChanged));
}

#endif // !UE_BUILD_SHIPPING
