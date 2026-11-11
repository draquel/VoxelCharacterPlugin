// Copyright Daniel Raquel. All Rights Reserved.

#include "Animation/VCLocomotionClipSet.h"
#include "Animation/AnimSequence.h"

namespace
{
	constexpr EVCLocomotionDirection AllDirections[] = {
		EVCLocomotionDirection::Forward, EVCLocomotionDirection::ForwardRight, EVCLocomotionDirection::Right,
		EVCLocomotionDirection::BackwardRight, EVCLocomotionDirection::Backward, EVCLocomotionDirection::BackwardLeft,
		EVCLocomotionDirection::Left, EVCLocomotionDirection::ForwardLeft,
	};
}

UAnimSequence* FVCLocomotionLoops::Get(EVCLocomotionDirection Direction) const
{
	switch (Direction)
	{
	case EVCLocomotionDirection::Forward:       return Forward;
	case EVCLocomotionDirection::ForwardRight:  return ForwardRight;
	case EVCLocomotionDirection::Right:         return Right;
	case EVCLocomotionDirection::BackwardRight: return BackwardRight;
	case EVCLocomotionDirection::Backward:      return Backward;
	case EVCLocomotionDirection::BackwardLeft:  return BackwardLeft;
	case EVCLocomotionDirection::Left:          return Left;
	case EVCLocomotionDirection::ForwardLeft:   return ForwardLeft;
	}
	return nullptr;
}

bool FVCLocomotionLoops::HasAny() const
{
	if (AuthoredSpeed <= 0.f)
	{
		return false;
	}
	for (const EVCLocomotionDirection Direction : AllDirections)
	{
		if (Get(Direction))
		{
			return true;
		}
	}
	return false;
}

EVCLocomotionDirection UVCLocomotionClipSet::DirectionFromAngle(float Degrees)
{
	// Eight sectors of 45 degrees, Forward centred on 0, clockwise (positive to the right).
	const float Wrapped = FMath::Fmod(FMath::Fmod(Degrees, 360.f) + 360.f + 22.5f, 360.f);
	const int32 Sector = FMath::Clamp(FMath::FloorToInt(Wrapped / 45.f), 0, 7);
	return AllDirections[Sector];
}

float UVCLocomotionClipSet::AngleOfDirection(EVCLocomotionDirection Direction)
{
	const float Angle = 45.f * static_cast<float>(static_cast<uint8>(Direction));
	return Angle > 180.f ? Angle - 360.f : Angle;
}

const FVCLocomotionLoops& UVCLocomotionClipSet::GetLoops(EVCLocomotionGait Gait) const
{
	switch (Gait)
	{
	case EVCLocomotionGait::Walk: return Walk;
	case EVCLocomotionGait::Jog:  return Jog;
	case EVCLocomotionGait::Run:  return Run;
	}
	return Jog;
}

UAnimSequence* UVCLocomotionClipSet::ChooseLoop(EVCLocomotionGait Gait, float Degrees) const
{
	// The gait asked for, then the others, the slower first.
	EVCLocomotionGait Order[3] = { EVCLocomotionGait::Walk, EVCLocomotionGait::Jog, EVCLocomotionGait::Run };
	if (Gait == EVCLocomotionGait::Jog)
	{
		Order[0] = EVCLocomotionGait::Jog;
		Order[1] = EVCLocomotionGait::Walk;
	}
	else if (Gait == EVCLocomotionGait::Run)
	{
		Order[0] = EVCLocomotionGait::Run;
		Order[2] = EVCLocomotionGait::Walk;
	}
	for (const EVCLocomotionGait Candidate : Order)
	{
		const FVCLocomotionLoops& Loops = GetLoops(Candidate);
		UAnimSequence* Best = nullptr;
		float BestGap = TNumericLimits<float>::Max();
		for (const EVCLocomotionDirection Direction : AllDirections)
		{
			UAnimSequence* Clip = Loops.Get(Direction);
			const float Gap = FMath::Abs(FMath::FindDeltaAngleDegrees(Degrees, AngleOfDirection(Direction)));
			// A tie (a pack without diagonals, halfway between forward and a strafe) keeps the first: forward before the side.
			if (Clip && Gap < BestGap - UE_KINDA_SMALL_NUMBER)
			{
				Best = Clip;
				BestGap = Gap;
			}
		}
		if (Best)
		{
			return Best;
		}
	}
	return nullptr;
}

float UVCLocomotionClipSet::GetCycleTopSpeed() const
{
	float Top = 0.f;
	for (const FVCLocomotionLoops* Loops : { &Walk, &Jog })
	{
		if (Loops->HasAny())
		{
			Top = FMath::Max(Top, Loops->AuthoredSpeed);
		}
	}
	return Top;
}

void UVCLocomotionClipSet::GetCycleSamples(bool bAim, TArray<UAnimSequence*>& OutClips, TArray<FVector2D>& OutPositions) const
{
	OutClips.Reset();
	OutPositions.Reset();
	TArray<float> Angles;
	const auto Add = [&OutClips, &OutPositions, &Angles](UAnimSequence* Clip, float Angle, float Speed)
	{
		OutClips.Add(Clip);
		OutPositions.Add(FVector2D(Angle, Speed));
		Angles.AddUnique(Angle);
	};
	const FVCLocomotionLoops& WalkRow = bAim && AimWalk.HasAny() ? AimWalk : Walk;
	UAnimSequence* IdleClip = bAim && AimIdle ? AimIdle.Get() : Idle.Get();
	for (const FVCLocomotionLoops* Loops : { &WalkRow, &Jog })
	{
		if (!Loops->HasAny())
		{
			continue;
		}
		for (const EVCLocomotionDirection Direction : AllDirections)
		{
			if (UAnimSequence* Clip = Loops->Get(Direction))
			{
				Add(Clip, AngleOfDirection(Direction), Loops->AuthoredSpeed);
				if (Direction == EVCLocomotionDirection::Backward)
				{
					Add(Clip, -180.f, Loops->AuthoredSpeed);
				}
			}
		}
	}
	if (IdleClip && Angles.Num() > 0)
	{
		Angles.AddUnique(-180.f);
		Angles.AddUnique(180.f);
		Angles.Sort();
		for (const float Angle : Angles)
		{
			OutClips.Add(IdleClip);
			OutPositions.Add(FVector2D(Angle, 0.0));
		}
	}
}
