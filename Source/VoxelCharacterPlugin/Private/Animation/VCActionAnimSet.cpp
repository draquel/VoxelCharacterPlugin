// Copyright Daniel Raquel. All Rights Reserved.

#include "Animation/VCActionAnimSet.h"
#include "Animation/AnimMontage.h"

FString UVCAnimNotify_ActionHit::GetNotifyName_Implementation() const
{
	return TEXT("ActionHit");
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
	case EVCAnimAction::Unsheathe:
	case EVCAnimAction::Sheathe:  return GetSheathMontage(Action, FGameplayTag());
	}
	return nullptr;
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
