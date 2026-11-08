// Copyright Daniel Raquel. All Rights Reserved.

// vc.Equip.Nudge / vc.Equip.PrintFit (the character refinement plan's C1): fit a held item in the pose it is held in, live,
// then paste the result into the item's script (configure_ranged_items.py, setup_character_layers.py's GRIPS) or, for a
// body's hand, into the appearance's AttachCorrections (setup_character_appearances.py). Dev only.

#include "Core/VCCharacterBase.h"
#include "Animation/VCActionAnimSet.h"
#include "Animation/VCActionMontageComponent.h"
#include "VoxelCharacterPlugin.h"

#if !UE_BUILD_SHIPPING

#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#if WITH_EQUIPMENT_PLUGIN
#include "Components/EquipmentManagerComponent.h"
#endif

namespace
{
	/** The local player's character in the play world. */
	AVCCharacterBase* FindLocalCharacter()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || !World->IsGameWorld())
			{
				continue;
			}
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				APlayerController* PC = It->Get();
				if (PC && PC->IsLocalController())
				{
					if (AVCCharacterBase* Character = Cast<AVCCharacterBase>(PC->GetPawn()))
					{
						return Character;
					}
				}
			}
		}
		return nullptr;
	}

	/** A slot's tag from a short name (MainHand) or a full one (Equipment.Slot.MainHand). */
	FGameplayTag SlotTagFromArg(const FString& Arg)
	{
		const FString Full = Arg.StartsWith(TEXT("Equipment.")) ? Arg : FString::Printf(TEXT("Equipment.Slot.%s"), *Arg);
		return FGameplayTag::RequestGameplayTag(FName(*Full), /*ErrorIfNotFound*/ false);
	}

	FString PythonTransform(const FTransform& T)
	{
		const FVector L = T.GetLocation();
		const FRotator R = T.Rotator();
		return FString::Printf(TEXT("unreal.Transform(location=unreal.Vector(%.2f, %.2f, %.2f), rotation=unreal.Rotator(roll=%.2f, pitch=%.2f, yaw=%.2f))"),
			L.X, L.Y, L.Z, R.Roll, R.Pitch, R.Yaw);
	}

	void OnNudgeCommand(const TArray<FString>& Args)
	{
#if WITH_EQUIPMENT_PLUGIN
		AVCCharacterBase* Character = FindLocalCharacter();
		if (!Character)
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("vc.Equip.Nudge: no local player character."));
			return;
		}
		// vc.Equip.Nudge [slot] dx dy dz [pitch yaw roll] | vc.Equip.Nudge [slot] reset
		int32 First = 0;
		FGameplayTag SlotTag = SlotTagFromArg(TEXT("MainHand"));
		if (Args.Num() > 0 && !FCString::IsNumeric(*Args[0]) && Args[0] != TEXT("reset"))
		{
			SlotTag = SlotTagFromArg(Args[0]);
			First = 1;
		}
		if (!SlotTag.IsValid())
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("vc.Equip.Nudge: unknown slot %s"), Args.Num() ? *Args[0] : TEXT(""));
			return;
		}
		FTransform& Nudge = Character->EquipFitNudges.FindOrAdd(SlotTag.GetTagName());
		if (Args.Num() > First && Args[First] == TEXT("reset"))
		{
			Nudge = FTransform::Identity;
		}
		else if (Args.Num() >= First + 3)
		{
			const FVector Delta(FCString::Atod(*Args[First]), FCString::Atod(*Args[First + 1]), FCString::Atod(*Args[First + 2]));
			FRotator Turn = FRotator::ZeroRotator;
			if (Args.Num() >= First + 6)
			{
				Turn = FRotator(FCString::Atod(*Args[First + 3]), FCString::Atod(*Args[First + 4]), FCString::Atod(*Args[First + 5]));
			}
			// Each nudge moves the item further in the bone's space: translate, then turn about the item's own axes.
			Nudge = Nudge * FTransform(Turn, Delta);
		}
		else
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("Usage: vc.Equip.Nudge [slot=MainHand] dx dy dz [pitch yaw roll] | vc.Equip.Nudge [slot] reset"));
			return;
		}
		Character->UpdateEquipmentAttachments();
		UE_LOG(LogVoxelCharacter, Display, TEXT("vc.Equip.Nudge %s: nudge now %s"), *SlotTag.ToString(), *PythonTransform(Nudge));
#else
		UE_LOG(LogVoxelCharacter, Warning, TEXT("vc.Equip.Nudge: EquipmentPlugin is not present."));
#endif
	}

	void OnPrintFitCommand(const TArray<FString>& Args)
	{
#if WITH_EQUIPMENT_PLUGIN
		AVCCharacterBase* Character = FindLocalCharacter();
		UEquipmentManagerComponent* Manager = Character ? Character->FindComponentByClass<UEquipmentManagerComponent>() : nullptr;
		if (!Manager)
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("vc.Equip.PrintFit: no local player character with equipment."));
			return;
		}
		const FGameplayTag SlotTag = SlotTagFromArg(Args.Num() > 0 ? Args[0] : TEXT("MainHand"));
		const FTransform* Found = Character->EquipFitNudges.Find(SlotTag.GetTagName());
		const FTransform Nudge = Found ? *Found : FTransform::Identity;
		const FName Bone = Manager->GetVisualAttachSocket(SlotTag);
		const FTransform Fit = Manager->GetVisualAttachOffset(SlotTag);
		const FTransform Correction = Character->GetBodyAttachCorrection(Bone);
		// The item's line: the fit with the nudge folded in (the body's correction kept apart, so it stays per body).
		UE_LOG(LogVoxelCharacter, Display, TEXT("vc.Equip.PrintFit %s on %s:"), *SlotTag.ToString(), *Bone.ToString());
		UE_LOG(LogVoxelCharacter, Display, TEXT("  item AttachOffset (fit then nudge): %s"), *PythonTransform(Fit * Nudge));
		UE_LOG(LogVoxelCharacter, Display, TEXT("  body AttachCorrections[%s] (correction then nudge): %s"), *Bone.ToString(), *PythonTransform(Correction * Nudge));
		UE_LOG(LogVoxelCharacter, Display, TEXT("  today: fit %s, correction %s, nudge %s"), *PythonTransform(Fit), *PythonTransform(Correction), *PythonTransform(Nudge));
		if (const UVCActionMontageComponent* Montages = Character->FindComponentByClass<UVCActionMontageComponent>())
		{
			if (const UVCActionAnimSet* Set = Montages->GetCurrentActionAnimSet(); Set && Set->bHasDrawnGrip)
			{
				// While the draw plays the nudge moved the drawn grip, not the fit: the set's line.
				UE_LOG(LogVoxelCharacter, Display, TEXT("  set %s DrawnGrip (drawn grip then nudge; the draw's weight now %.2f): %s"),
					*Set->GetName(), Character->GetDrawnGripWeight(), *PythonTransform(Set->DrawnGrip * Nudge));
			}
		}
#else
		UE_LOG(LogVoxelCharacter, Warning, TEXT("vc.Equip.PrintFit: EquipmentPlugin is not present."));
#endif
	}

	FAutoConsoleCommand CmdEquipNudge(
		TEXT("vc.Equip.Nudge"),
		TEXT("[slot=MainHand] dx dy dz [pitch yaw roll] | [slot] reset: move the local player's held item on its bone, live, to fit it in the pose (character refinement plan C1). vc.Equip.PrintFit prints the result."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&OnNudgeCommand));

	FAutoConsoleCommand CmdEquipPrintFit(
		TEXT("vc.Equip.PrintFit"),
		TEXT("[slot=MainHand]: print the slot's fit with its nudge folded in, as the item's AttachOffset line for the scripts and as the body's AttachCorrections entry for the appearance."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&OnPrintFitCommand));
}

#endif // !UE_BUILD_SHIPPING
