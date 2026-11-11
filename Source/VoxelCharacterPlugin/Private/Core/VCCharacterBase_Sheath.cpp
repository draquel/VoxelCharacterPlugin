// Copyright Daniel Raquel. All Rights Reserved.

// AVCCharacterBase: carried weapons, drawn from their sheaths and put back (the equipment sheathing plan, D77-D82).
// EquipmentPlugin keeps the state (a sheath slot's bDrawn, replicated); this file asks for it at the right moment and
// animates it: the owner plays its montages at input, the server plays them to everyone else, sheathes at once and draws
// at the unsheathe's hand-off, which it reads from the montage asset (a dedicated server plays nothing).

#include "Core/VCCharacterBase.h"
#include "Animation/VCActionAnimSet.h"
#include "Animation/VCActionMontageComponent.h"
#include "Tags/CGFGameplayTags.h"
#include "VoxelCharacterPlugin.h"
#include "InputActionValue.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Animation/AnimMontage.h"

#if WITH_EQUIPMENT_PLUGIN
#include "Components/EquipmentManagerComponent.h"
#include "Types/EquipmentSystemTypes.h"
#endif

#if WITH_INVENTORY_PLUGIN
#include "Components/InventoryComponent.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Data/ItemDefinition.h"
#include "Data/Fragments/ItemFragment_Equipment.h"
#endif

// ---------------------------------------------------------------------------
// Queries and the predicate
// ---------------------------------------------------------------------------

TArray<FGameplayTag> AVCCharacterBase::GetCarriedWeaponSlots() const
{
	TArray<FGameplayTag> Carried;
#if WITH_EQUIPMENT_PLUGIN
	if (EquipmentManager)
	{
		for (const FEquipmentSlot& Slot : EquipmentManager->EquipmentSlots)
		{
			// The main hand's sheaths: the shield's (drawn into the off hand) comes and goes on its own (G175).
			if (Slot.DrawSlotTag == CGFGameplayTags::Equipment_Slot_MainHand && Slot.bIsOccupied)
			{
				Carried.Add(Slot.SlotTag);
			}
		}
	}
#endif
	return Carried;
}

FGameplayTag AVCCharacterBase::GetDrawnSheath() const
{
#if WITH_EQUIPMENT_PLUGIN
	if (EquipmentManager)
	{
		const FGameplayTag Held = EquipmentManager->GetHeldSlotTag(CGFGameplayTags::Equipment_Slot_MainHand);
		if (EquipmentManager->IsSheathSlot(Held))
		{
			return Held;
		}
	}
#endif
	return FGameplayTag();
}

bool AVCCharacterBase::CanSheathAction(FGameplayTag SheathSlotTag) const
{
#if WITH_EQUIPMENT_PLUGIN
	if (!EquipmentManager || IsIncapacitated())
	{
		return false;
	}
	if (!SheathSlotTag.IsValid())
	{
		return GetDrawnSheath().IsValid();
	}
	return GetCarriedWeaponSlots().Contains(SheathSlotTag) && !EquipmentManager->IsDrawn(SheathSlotTag);
#else
	return false;
#endif
}

FGameplayTag AVCCharacterBase::GetSheathActionSet(FGameplayTag Sheath) const
{
	FGameplayTag ItemSet;
#if WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
	const UGameInstance* GI = GetGameInstance();
	const UItemDatabaseSubsystem* ItemDB = GI ? GI->GetSubsystem<UItemDatabaseSubsystem>() : nullptr;
	const UItemDefinition* Def = ItemDB && EquipmentManager ? ItemDB->GetDefinition(EquipmentManager->GetEquippedItem(Sheath).ItemDefinitionId) : nullptr;
	if (const UItemFragment_Equipment* EquipFrag = Def ? Def->FindFragment<UItemFragment_Equipment>() : nullptr)
	{
		ItemSet = EquipFrag->ActionSetTag;
	}
#endif
	return PickActionSet(true, ItemSet);
}

// ---------------------------------------------------------------------------
// Requests (the owner)
// ---------------------------------------------------------------------------

bool AVCCharacterBase::RequestDrawWeapon(FGameplayTag SheathSlotTag)
{
	// One sequence at a time: a press while a weapon comes out or goes back is ignored.
	if (!CanSheathAction(SheathSlotTag) || (ActionMontages && ActionMontages->IsSheathActionPlaying()))
	{
		return false;
	}
	LastDrawnSheath = SheathSlotTag;
	if (HasAuthority())
	{
		RunSheathSequence(SheathSlotTag, /*bOwnerPlayedIt*/ false);
	}
	else
	{
		RunSheathSequence(SheathSlotTag, /*bOwnerPlayedIt*/ true);
		Server_SheathSequence(SheathSlotTag);
	}
	return true;
}

bool AVCCharacterBase::RequestSheatheWeapon()
{
	if (!CanSheathAction(FGameplayTag()) || (ActionMontages && ActionMontages->IsSheathActionPlaying()))
	{
		return false;
	}
	if (HasAuthority())
	{
		RunSheathSequence(FGameplayTag(), /*bOwnerPlayedIt*/ false);
	}
	else
	{
		RunSheathSequence(FGameplayTag(), /*bOwnerPlayedIt*/ true);
		Server_SheathSequence(FGameplayTag());
	}
	return true;
}

bool AVCCharacterBase::RequestToggleSheathe()
{
	if (GetDrawnSheath().IsValid())
	{
		return RequestSheatheWeapon();
	}
	const TArray<FGameplayTag> Carried = GetCarriedWeaponSlots();
	if (Carried.IsEmpty())
	{
		return false;
	}
	return RequestDrawWeapon(Carried.Contains(LastDrawnSheath) ? LastDrawnSheath : Carried[0]);
}

bool AVCCharacterBase::RequestCycleWeapon()
{
	const TArray<FGameplayTag> Carried = GetCarriedWeaponSlots();
	const FGameplayTag Drawn = GetDrawnSheath();
	const FGameplayTag From = Drawn.IsValid() ? Drawn : LastDrawnSheath;
	const int32 Index = Carried.IndexOfByKey(From);
	for (int32 Step = 1; Step <= Carried.Num(); ++Step)
	{
		// With nothing drawn and nothing drawn before, the first carried weapon (Index is -1).
		const FGameplayTag Next = Carried[(Index + Step) % Carried.Num()];
		if (Next != Drawn)
		{
			return RequestDrawWeapon(Next);
		}
	}
	return false;
}

void AVCCharacterBase::Input_Sheathe(const FInputActionValue& Value)
{
	RequestToggleSheathe();
}

void AVCCharacterBase::Input_CycleWeapon(const FInputActionValue& Value)
{
	RequestCycleWeapon();
}

// ---------------------------------------------------------------------------
// The sequence (the owner's montages; the server's montages and state)
// ---------------------------------------------------------------------------

void AVCCharacterBase::Server_SheathSequence_Implementation(FGameplayTag DrawSheath)
{
	// The owner's check, again (D-CR16): a refused request changes nothing (its montage already played at home).
	if (!CanSheathAction(DrawSheath))
	{
		UE_LOG(LogVoxelCharacter, Log, TEXT("%s: refused the %s of %s."), *GetName(), DrawSheath.IsValid() ? TEXT("draw") : TEXT("sheathe"),
			DrawSheath.IsValid() ? *DrawSheath.ToString() : *GetDrawnSheath().ToString());
		return;
	}
	RunSheathSequence(DrawSheath, /*bOwnerPlayedIt*/ true);
}

void AVCCharacterBase::RunSheathSequence(FGameplayTag DrawSheath, bool bOwnerPlayedIt)
{
	UWorld* World = GetWorld();
	if (!World || !ActionMontages)
	{
		return;
	}
	World->GetTimerManager().ClearTimer(SheathStepTimer);
	World->GetTimerManager().ClearTimer(SheathHandOffTimer);

	// The weapon in the hand goes back first, then the wanted one comes out once its sheathe has played.
	const FGameplayTag Drawn = GetDrawnSheath();
	float Delay = 0.f;
	if (Drawn.IsValid() && Drawn != DrawSheath)
	{
		float HandOff = 0.f;
		const UAnimMontage* Sheathe = ActionMontages->GetSheathMontage(EVCAnimAction::Sheathe, GetSheathActionSet(Drawn), Drawn, HandOff);
		Delay = Sheathe ? Sheathe->GetPlayLength() : 0.f;
		PlaySheathStep(EVCAnimAction::Sheathe, Drawn, bOwnerPlayedIt);
	}
	if (!DrawSheath.IsValid() || DrawSheath == Drawn)
	{
		return;
	}
	if (Delay > 0.f)
	{
		World->GetTimerManager().SetTimer(SheathStepTimer, FTimerDelegate::CreateWeakLambda(this, [this, DrawSheath, bOwnerPlayedIt]()
		{
			if (!IsIncapacitated())
			{
				PlaySheathStep(EVCAnimAction::Unsheathe, DrawSheath, bOwnerPlayedIt);
			}
		}), Delay, false);
	}
	else
	{
		PlaySheathStep(EVCAnimAction::Unsheathe, DrawSheath, bOwnerPlayedIt);
	}
}

void AVCCharacterBase::PlaySheathStep(EVCAnimAction Action, FGameplayTag Sheath, bool bOwnerPlayedIt)
{
#if WITH_EQUIPMENT_PLUGIN
	if (!ActionMontages || !EquipmentManager)
	{
		return;
	}
	const FGameplayTag ActionSet = GetSheathActionSet(Sheath);
	if (!HasAuthority())
	{
		ActionMontages->PlaySheathActionLocal(Action, ActionSet, Sheath);
		return;
	}

	ActionMontages->PlaySheathActionForEveryone(Action, ActionSet, Sheath, bOwnerPlayedIt);
	if (Action == EVCAnimAction::Sheathe)
	{
		// The weapon leaves the hand's use at once; its visual goes back at the montage's hand-off on each machine.
		EquipmentManager->TrySheathe(Sheath);
		return;
	}

	// The weapon is in the hand, and usable, from the unsheathe's hand-off (D82). Dead or downed before it: still sheathed.
	float HandOff = 0.f;
	ActionMontages->GetSheathMontage(Action, ActionSet, Sheath, HandOff);
	auto Draw = [this, Sheath]()
	{
		if (IsIncapacitated() || !EquipmentManager || !EquipmentManager->IsSlotOccupied(Sheath))
		{
			return;
		}
#if WITH_INVENTORY_PLUGIN
		const EEquipmentResult Result = EquipmentManager->TryDraw(Sheath, InventoryComponent);
#else
		const EEquipmentResult Result = EquipmentManager->TryDraw(Sheath);
#endif
		UE_LOG(LogVoxelCharacter, Log, TEXT("%s: draw %s: %s"), *GetName(), *Sheath.ToString(), *UEnum::GetValueAsString(Result));
	};
	if (HandOff > 0.f && GetWorld())
	{
		GetWorld()->GetTimerManager().SetTimer(SheathHandOffTimer, FTimerDelegate::CreateWeakLambda(this, Draw), HandOff, false);
	}
	else
	{
		Draw();
	}
#endif
}

// ---------------------------------------------------------------------------
// The visual's hand-off (every machine)
// ---------------------------------------------------------------------------

void AVCCharacterBase::UpdateSheathHandOff()
{
#if WITH_EQUIPMENT_PLUGIN
	bool bPending = false;
	if (ActionMontages && EquipmentManager && ActionMontages->IsSheathActionPlaying())
	{
		for (const FEquipmentSlot& Slot : EquipmentManager->EquipmentSlots)
		{
			EVCAnimAction Action;
			if (Slot.DrawSlotTag.IsValid() && ActionMontages->IsSheathHandOffPending(Slot.SlotTag, Action))
			{
				bPending = true;
				break;
			}
		}
	}
	// Re-seat while the montage holds the weapon where it started, and once more when the hand-off passes.
	if (bPending || bSheathHandOffWasPending)
	{
		UpdateEquipmentAttachments();
	}
	bSheathHandOffWasPending = bPending;
#endif
}

void AVCCharacterBase::HandleEquipmentDrawnChanged(FGameplayTag SlotTag, bool bDrawn)
{
	if (bDrawn && GetCarriedWeaponSlots().Contains(SlotTag))
	{
		LastDrawnSheath = SlotTag;
	}
	UpdateEquipmentAttachments();
}
