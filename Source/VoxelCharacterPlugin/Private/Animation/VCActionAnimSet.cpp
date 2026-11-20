// Copyright Daniel Raquel. All Rights Reserved.

#include "Animation/VCActionAnimSet.h"
#include "Animation/AnimMontage.h"

const FName UVCActionAnimSet::ChargeLoopSection(TEXT("Loop"));
const FName UVCActionAnimSet::ChargeEndSection(TEXT("End"));

FString UVCAnimNotify_ActionHit::GetNotifyName_Implementation() const
{
	return TEXT("ActionHit");
}

FString UVCAnimNotify_ActionRecovery::GetNotifyName_Implementation() const
{
	return TEXT("ActionRecovery");
}

FString UVCAnimNotify_ActionTravelled::GetNotifyName_Implementation() const
{
	return TEXT("ActionTravelled");
}

FString UVCAnimNotify_ItemHandOff::GetNotifyName_Implementation() const
{
	return TEXT("ItemHandOff");
}

UAnimMontage* FVCDirectionalMontages::Get(EVCHitDirection Direction) const
{
	UAnimMontage* Side = nullptr;
	switch (Direction)
	{
	case EVCHitDirection::Back:  Side = Back;  break;
	case EVCHitDirection::Left:  Side = Left;  break;
	case EVCHitDirection::Right: Side = Right; break;
	default: break;
	}
	return Side ? Side : Front.Get();
}

UAnimMontage* UVCActionAnimSet::GetMontage(EVCAnimAction Action, int32 Variant) const
{
	switch (Action)
	{
	case EVCAnimAction::Attack:
		return AttackCombo.Num() > 0 ? AttackCombo[FMath::Abs(Variant) % AttackCombo.Num()].Get() : nullptr;
	case EVCAnimAction::Dig:      return Dig;
	case EVCAnimAction::Chop:     return Chop;
	case EVCAnimAction::Place:    return Place;
	case EVCAnimAction::Use:      return Use;
	case EVCAnimAction::HitReact: return HitReact.Get(static_cast<EVCHitDirection>(FMath::Clamp(Variant, 0, 3)));
	case EVCAnimAction::Death:    return Death.Get(static_cast<EVCHitDirection>(FMath::Clamp(Variant, 0, 3)));
	case EVCAnimAction::Revive:   return Revive;
	case EVCAnimAction::Draw:     return Draw;
	case EVCAnimAction::Release:  return Release;
	case EVCAnimAction::Block:      return Block;
	case EVCAnimAction::BlockEnd:   return BlockEnd;
	case EVCAnimAction::BlockHit:   return BlockHit;
	case EVCAnimAction::GuardBreak: return GuardBreak;
	case EVCAnimAction::Charge:   return Charge;
	case EVCAnimAction::Reload:
		return Variant == static_cast<int32>(EVCReloadFrom::Bag) && ReloadFromBag ? ReloadFromBag.Get() : ReloadFromQuiver.Get();
	case EVCAnimAction::Dodge:    return Dodge.Get(static_cast<EVCHitDirection>(FMath::Clamp(Variant, 0, 3)));
	case EVCAnimAction::Roll:     return Roll;
	case EVCAnimAction::Unsheathe:
	case EVCAnimAction::Sheathe:  return GetSheathMontage(Action, FGameplayTag());
	}
	return nullptr;
}

int32 UVCActionAnimSet::GetFirstAttack(bool bSecondary) const
{
	if (!bSecondary)
	{
		return AttackCombo.Num() > 0 ? 0 : -1;
	}
	return AttackCombo.IsValidIndex(SecondaryAttackStart) ? SecondaryAttackStart : -1;
}

int32 UVCActionAnimSet::GetNextAttack(int32 Current, bool bSecondary) const
{
	const int32 Count = AttackCombo.Num();
	if (Count == 0)
	{
		return -1;
	}
	if (AttackComboSteps.IsValidIndex(Current))
	{
		const FVCAttackComboStep& Step = AttackComboSteps[Current];
		const int32 Next = bSecondary ? Step.NextSecondary : Step.NextPrimary;
		if (AttackCombo.IsValidIndex(Next))
		{
			return Next;
		}
	}
	// No step, or none for this button: the list's next for the primary, the secondary's first.
	return bSecondary ? GetFirstAttack(true) : (Current >= 0 ? (Current + 1) % Count : 0);
}

bool UVCActionAnimSet::IsSecondaryAttack(int32 Index) const
{
	if (!AttackCombo.IsValidIndex(Index) || GetFirstAttack(true) < 0)
	{
		return false;
	}
	if (Index == SecondaryAttackStart)
	{
		return true;
	}
	for (int32 Step = 0; Step < AttackCombo.Num(); ++Step)
	{
		if (GetNextAttack(Step, true) == Index)
		{
			return true;
		}
	}
	return false;
}

float UVCActionAnimSet::GetChargeTime() const
{
	if (!HasCharge())
	{
		return 0.f;
	}
	// The windup is the first section: up to the loop's start (the whole montage when it has no loop).
	const int32 Loop = Charge->GetSectionIndex(ChargeLoopSection);
	const float Windup = Loop != INDEX_NONE ? Charge->CompositeSections[Loop].GetTime() : Charge->GetPlayLength();
	return Windup + ChargeHoldTime;
}

float UVCActionAnimSet::GetAttackDamageMultiplier(int32 Index) const
{
	return AttackComboSteps.IsValidIndex(Index) ? FMath::Max(0.f, AttackComboSteps[Index].DamageMultiplier) : 1.f;
}

UAnimMontage* UVCActionAnimSet::GetSheathMontage(EVCAnimAction Action, const FGameplayTag& Sheath) const
{
	const FVCSheathMontages* Entry = SheathMontages.FindByPredicate([&Sheath](const FVCSheathMontages& M) { return M.Sheath == Sheath; });
	if (!Entry && SheathMontages.Num() > 0)
	{
		Entry = &SheathMontages[0];
	}
	if (!Entry)
	{
		return nullptr;
	}
	return Action == EVCAnimAction::Unsheathe ? Entry->Unsheathe.Get() : Action == EVCAnimAction::Sheathe ? Entry->Sheathe.Get() : nullptr;
}

float UVCActionAnimSet::GetHandOffTime(const UAnimMontage* Montage, float DefaultFraction)
{
	if (!Montage)
	{
		return 0.f;
	}
	float Earliest = TNumericLimits<float>::Max();
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		if (Cast<UVCAnimNotify_ItemHandOff>(Event.Notify))
		{
			Earliest = FMath::Min(Earliest, Event.GetTriggerTime());
		}
	}
	if (Earliest < TNumericLimits<float>::Max())
	{
		return FMath::Max(0.f, Earliest);
	}
	return Montage->GetPlayLength() * FMath::Clamp(DefaultFraction, 0.f, 1.f);
}

float UVCActionAnimSet::GetHitTime(const UAnimMontage* Montage, float DefaultFraction)
{
	if (!Montage)
	{
		return 0.f;
	}
	float Earliest = TNumericLimits<float>::Max();
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		if (Cast<UVCAnimNotify_ActionHit>(Event.Notify))
		{
			Earliest = FMath::Min(Earliest, Event.GetTriggerTime());
		}
	}
	if (Earliest < TNumericLimits<float>::Max())
	{
		return FMath::Max(0.f, Earliest);
	}
	return Montage->GetPlayLength() * FMath::Clamp(DefaultFraction, 0.f, 1.f);
}

float UVCActionAnimSet::GetRecoveryTime(const UAnimMontage* Montage)
{
	if (!Montage)
	{
		return 0.f;
	}
	float Earliest = TNumericLimits<float>::Max();
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		if (Cast<UVCAnimNotify_ActionRecovery>(Event.Notify))
		{
			Earliest = FMath::Min(Earliest, Event.GetTriggerTime());
		}
	}
	return Earliest < TNumericLimits<float>::Max() ? FMath::Max(0.f, Earliest) : Montage->GetPlayLength();
}

float UVCActionAnimSet::GetTravelledTime(const UAnimMontage* Montage, float DefaultFraction)
{
	if (!Montage)
	{
		return 0.f;
	}
	float Earliest = TNumericLimits<float>::Max();
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		if (Cast<UVCAnimNotify_ActionTravelled>(Event.Notify))
		{
			Earliest = FMath::Min(Earliest, Event.GetTriggerTime());
		}
	}
	if (Earliest < TNumericLimits<float>::Max())
	{
		return FMath::Max(0.f, Earliest);
	}
	return Montage->GetPlayLength() * FMath::Clamp(DefaultFraction, 0.f, 1.f);
}

bool UVCActionAnimSet::CarriesCapsule(const UAnimMontage* Montage)
{
	return Montage && Montage->HasRootMotion() && Montage->FindMetaDataByClass<UVCAnimMetaData_CarriesCapsule>() != nullptr;
}

EVCHitDirection UVCActionAnimSet::DirectionOfHit(const FVector& VictimForward, const FVector& Up, const FVector& ToSource)
{
	const FVector Forward = VictimForward - (VictimForward | Up) * Up;
	const FVector Toward = ToSource - (ToSource | Up) * Up;
	if (Forward.IsNearlyZero() || Toward.IsNearlyZero())
	{
		return EVCHitDirection::Front;
	}
	// Positive to the right, as everywhere in the gravity frame.
	const double Angle = FMath::RadiansToDegrees(FMath::Atan2((Forward ^ Toward) | Up, Forward | Toward));
	if (FMath::Abs(Angle) <= 45.0)
	{
		return EVCHitDirection::Front;
	}
	if (FMath::Abs(Angle) >= 135.0)
	{
		return EVCHitDirection::Back;
	}
	return Angle > 0.0 ? EVCHitDirection::Right : EVCHitDirection::Left;
}
