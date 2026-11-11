// Copyright Daniel Raquel. All Rights Reserved.

#include "Animation/VCLocomotionClipSet.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationAsset.h"

namespace
{
	constexpr EVCLocomotionDirection AllDirections[] = {
		EVCLocomotionDirection::Forward, EVCLocomotionDirection::ForwardRight, EVCLocomotionDirection::Right,
		EVCLocomotionDirection::BackwardRight, EVCLocomotionDirection::Backward, EVCLocomotionDirection::BackwardLeft,
		EVCLocomotionDirection::Left, EVCLocomotionDirection::ForwardLeft,
	};

	/** The loops' clip nearest a direction in angle; a tie (a pack without diagonals, halfway) keeps forward before the side. */
	UAnimSequence* NearestClip(const FVCLocomotionLoops& Loops, float Degrees)
	{
		UAnimSequence* Best = nullptr;
		float BestGap = TNumericLimits<float>::Max();
		for (const EVCLocomotionDirection Direction : AllDirections)
		{
			UAnimSequence* Clip = Loops.Get(Direction);
			const float Gap = FMath::Abs(FMath::FindDeltaAngleDegrees(Degrees, UVCLocomotionClipSet::AngleOfDirection(Direction)));
			if (Clip && Gap < BestGap - UE_KINDA_SMALL_NUMBER)
			{
				Best = Clip;
				BestGap = Gap;
			}
		}
		return Best;
	}

	/** The first of the candidate loops with a clip, the one nearest the direction, at a rate. */
	FVCLocomotionPick PickFirst(std::initializer_list<const FVCLocomotionLoops*> Candidates, float Degrees, float Rate)
	{
		FVCLocomotionPick Pick;
		for (const FVCLocomotionLoops* Loops : Candidates)
		{
			if (UAnimSequence* Clip = Loops ? NearestClip(*Loops, Degrees) : nullptr)
			{
				Pick.Clip = Clip;
				Pick.PlayRate = Rate;
				break;
			}
		}
		return Pick;
	}

	/** The root bone's transform in a clip at a time (the packs keep their root motion in the root bone, the flag off). */
	FTransform RootAt(const UAnimSequenceBase* Clip, float Time)
	{
		const double At = FMath::Clamp(static_cast<double>(Time), 0.0, static_cast<double>(Clip->GetPlayLength()));
		return Clip->ExtractRootTrackTransform(FAnimExtractContext(At), nullptr);
	}
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
		if (UAnimSequence* Best = NearestClip(GetLoops(Candidate), Degrees))
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
	const FVCLocomotionLoops& WalkRow = bAim && AimWalk.HasAny() ? AimWalk : Walk;
	UAnimSequence* IdleClip = bAim && AimIdle ? AimIdle.Get() : Idle.Get();
	BuildCycleSamples(IdleClip, { &WalkRow, &Jog }, OutClips, OutPositions);
}

void UVCLocomotionClipSet::GetCrouchCycleSamples(bool bAim, TArray<UAnimSequence*>& OutClips, TArray<FVector2D>& OutPositions) const
{
	UAnimSequence* IdleClip = bAim && AimCrouchIdle ? AimCrouchIdle.Get() : CrouchIdle.Get();
	BuildCycleSamples(IdleClip, { &CrouchSlowRow(bAim), &CrouchWalk }, OutClips, OutPositions);
}

void UVCLocomotionClipSet::BuildCycleSamples(UAnimSequence* IdleClip, std::initializer_list<const FVCLocomotionLoops*> Rows,
	TArray<UAnimSequence*>& OutClips, TArray<FVector2D>& OutPositions)
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
	for (const FVCLocomotionLoops* Loops : Rows)
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

FVCLocomotionPace UVCLocomotionClipSet::ComputePace(float Speed, TConstArrayView<FVector2f> Points, bool bFromRest)
{
	FVCLocomotionPace Pace;
	Pace.BlendSpeed = Speed;
	if (Points.Num() == 0)
	{
		return Pace;
	}
	const float S = FMath::Max(Speed, 0.f);
	const FVector2f& First = Points[0];
	if (S <= First.X)
	{
		if (bFromRest)
		{
			// Toward the rest: the first gait's rate, its stride shortening with the blend.
			Pace.PlayRate = First.X / First.Y;
			Pace.BlendSpeed = S / Pace.PlayRate;
		}
		else
		{
			Pace.BlendSpeed = First.Y;
			Pace.PlayRate = S / First.Y;
		}
		return Pace;
	}
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		const FVector2f& Low = Points[Index - 1];
		const FVector2f& High = Points[Index];
		if (S <= High.X)
		{
			const float Alpha = (S - Low.X) / FMath::Max(High.X - Low.X, UE_KINDA_SMALL_NUMBER);
			Pace.BlendSpeed = FMath::Lerp(Low.Y, High.Y, Alpha);
			Pace.PlayRate = S / Pace.BlendSpeed;
			return Pace;
		}
	}
	// Past the fastest gait: its clips quicken (a sprint on a set without one).
	Pace.BlendSpeed = Points.Last().Y;
	Pace.PlayRate = S / Pace.BlendSpeed;
	return Pace;
}

FVCLocomotionPace UVCLocomotionClipSet::ComputeCyclePace(float Speed, bool bAim) const
{
	TArray<FVector2f, TInlineAllocator<2>> Points;
	const FVCLocomotionLoops& WalkRow = bAim && AimWalk.HasAny() ? AimWalk : Walk;
	if (WalkRow.HasAny() && WalkSpeed > 0.f)
	{
		Points.Add(FVector2f(WalkSpeed, WalkRow.AuthoredSpeed));
	}
	if (Jog.HasAny() && RunSpeed > 0.f && (Points.Num() == 0 || RunSpeed > Points[0].X))
	{
		Points.Add(FVector2f(RunSpeed, Jog.AuthoredSpeed));
	}
	return ComputePace(Speed, Points, true);
}

FVCLocomotionPace UVCLocomotionClipSet::ComputeSprintPace(float Speed) const
{
	TArray<FVector2f, TInlineAllocator<2>> Points;
	if (Run.HasAny())
	{
		Points.Add(FVector2f(Run.AuthoredSpeed, Run.AuthoredSpeed));
	}
	if (RunFast.HasAny() && (Points.Num() == 0 || RunFast.AuthoredSpeed > Points[0].X))
	{
		Points.Add(FVector2f(RunFast.AuthoredSpeed, RunFast.AuthoredSpeed));
	}
	return ComputePace(Speed, Points, false);
}

float UVCLocomotionClipSet::GetGaitPlayRate(EVCLocomotionGait Gait, bool bAim) const
{
	switch (Gait)
	{
	case EVCLocomotionGait::Walk: return ComputeCyclePace(WalkSpeed, bAim).PlayRate;
	case EVCLocomotionGait::Jog:  return ComputeCyclePace(RunSpeed, bAim).PlayRate;
	case EVCLocomotionGait::Run:  return HasSprint() ? ComputeSprintPace(SprintSpeed).PlayRate : ComputeCyclePace(SprintSpeed, bAim).PlayRate;
	}
	return 1.f;
}

FVCLocomotionPick UVCLocomotionClipSet::ChooseStart(EVCLocomotionGait Gait, float Degrees, bool bAim) const
{
	if (Gait == EVCLocomotionGait::Walk)
	{
		// The jog's start for a set without walk starts (SSH, 2H), at the walk's rate (§3.3).
		return PickFirst({ bAim ? &AimWalkStart : nullptr, &WalkStart, &JogStart }, Degrees, GetGaitPlayRate(Gait, bAim));
	}
	// The run's start is the jog's: a sprint from a stand starts as a jog and runs once it is fast enough.
	return PickFirst({ &JogStart }, Degrees, GetGaitPlayRate(EVCLocomotionGait::Jog, bAim));
}

FVCLocomotionPick UVCLocomotionClipSet::ChooseStop(EVCLocomotionGait Gait, float Degrees, bool bAim) const
{
	if (Gait == EVCLocomotionGait::Walk)
	{
		return PickFirst({ bAim ? &AimWalkStop : nullptr, &WalkStop, &JogStop }, Degrees, GetGaitPlayRate(Gait, bAim));
	}
	if (Gait == EVCLocomotionGait::Run && FMath::Abs(FMath::FindDeltaAngleDegrees(0.f, Degrees)) <= 45.f)
	{
		const FVCLocomotionPick Pick = PickFirst({ &RunStop }, 0.f, GetGaitPlayRate(Gait, bAim));
		if (Pick.Clip)
		{
			return Pick;
		}
	}
	return PickFirst({ &JogStop }, Degrees, GetGaitPlayRate(EVCLocomotionGait::Jog, bAim));
}

FVCLocomotionPick UVCLocomotionClipSet::ChooseTurn(float OffsetDegrees) const
{
	FVCLocomotionPick Pick;
	const float Offset = FRotator::NormalizeAxis(OffsetDegrees);
	if (FMath::IsNearlyZero(Offset))
	{
		return Pick;
	}
	// The facing is to the body's right when the offset is negative: a turn to the right takes it back.
	const bool bRight = Offset < 0.f;
	UAnimSequence* Quarter = bRight ? Turns.Right90 : Turns.Left90;
	UAnimSequence* Half = bRight ? Turns.Right180 : Turns.Left180;
	const bool bHalf = (FMath::Abs(Offset) >= 135.f && Half) || !Quarter;
	Pick.Clip = bHalf ? Half : Quarter;
	if (Pick.Clip)
	{
		Pick.TurnDegrees = (bHalf ? 180.f : 90.f) * (bRight ? 1.f : -1.f);
	}
	return Pick;
}

void UVCLocomotionClipSet::GetSprintSamples(TArray<UAnimSequence*>& OutClips, TArray<float>& OutPositions) const
{
	OutClips.Reset();
	OutPositions.Reset();
	for (const FVCLocomotionLoops* Loops : { &Run, &RunFast })
	{
		if (Loops->HasAny() && Loops->Forward)
		{
			OutClips.Add(Loops->Forward);
			OutPositions.Add(Loops->AuthoredSpeed);
		}
	}
}

float UVCLocomotionClipSet::RootYawAt(const UAnimSequenceBase* Clip, float Time)
{
	if (!Clip || Clip->GetPlayLength() <= 0.f)
	{
		return 0.f;
	}
	const FQuat Turn = RootAt(Clip, 0.f).GetRotation().Inverse() * RootAt(Clip, Time).GetRotation();
	return FMath::RadiansToDegrees(Turn.GetTwistAngle(FVector::UpVector));
}

float UVCLocomotionClipSet::RootDistanceAt(const UAnimSequenceBase* Clip, float Time)
{
	if (!Clip || Clip->GetPlayLength() <= 0.f)
	{
		return 0.f;
	}
	return static_cast<float>((RootAt(Clip, Time).GetTranslation() - RootAt(Clip, 0.f).GetTranslation()).Size2D());
}

float UVCLocomotionClipSet::FindStopEntryTime(TFunctionRef<float(float)> DistanceAt, float Length, float Remaining)
{
	if (Length <= 0.f)
	{
		return 0.f;
	}
	const float Wanted = DistanceAt(Length) - FMath::Max(Remaining, 0.f);
	if (Wanted <= 0.f)
	{
		return 0.f;
	}
	// The earliest time the clip has travelled Wanted (bisection: the travel of a stop only grows).
	float Low = 0.f;
	float High = Length;
	for (int32 Step = 0; Step < 20; ++Step)
	{
		const float Mid = 0.5f * (Low + High);
		if (DistanceAt(Mid) >= Wanted)
		{
			High = Mid;
		}
		else
		{
			Low = Mid;
		}
	}
	return High;
}

const UVCLocomotionClipSet* UVCLocomotionClipSet::GetCrouchSet(bool bBorrow) const
{
	if (HasCrouch())
	{
		return this;
	}
	return bBorrow && BorrowedCrouch && BorrowedCrouch->HasCrouch() ? BorrowedCrouch.Get() : nullptr;
}

const FVCLocomotionLoops& UVCLocomotionClipSet::CrouchSlowRow(bool bAim) const
{
	return bAim && AimCrouchWalk.HasAny() ? AimCrouchWalk : CrouchWalkSlow;
}

FVCLocomotionPace UVCLocomotionClipSet::ComputeCrouchPace(float Speed, bool bAim) const
{
	TArray<FVector2f, TInlineAllocator<2>> Points;
	const FVCLocomotionLoops& SlowRow = CrouchSlowRow(bAim);
	if (SlowRow.HasAny() && CrouchSlowSpeed > 0.f)
	{
		Points.Add(FVector2f(CrouchSlowSpeed, SlowRow.AuthoredSpeed));
	}
	if (CrouchWalk.HasAny() && CrouchSpeed > 0.f && (Points.Num() == 0 || CrouchSpeed > Points[0].X))
	{
		Points.Add(FVector2f(CrouchSpeed, CrouchWalk.AuthoredSpeed));
	}
	return ComputePace(Speed, Points, true);
}

bool UVCLocomotionClipSet::PlaysSlowCrouchAtCap(bool bAim) const
{
	const FVCLocomotionLoops& SlowRow = CrouchSlowRow(bAim);
	if (!SlowRow.HasAny() || !CrouchWalk.HasAny())
	{
		return SlowRow.HasAny();
	}
	const float Blend = ComputeCrouchPace(CrouchSpeed, bAim).BlendSpeed;
	return FMath::Abs(Blend - SlowRow.AuthoredSpeed) < FMath::Abs(Blend - CrouchWalk.AuthoredSpeed);
}

FVCLocomotionPick UVCLocomotionClipSet::ChooseCrouchStart(float Degrees, bool bAim) const
{
	const float Rate = ComputeCrouchPace(CrouchSpeed, bAim).PlayRate;
	const FVCLocomotionLoops* Slow = bAim && AimCrouchWalkStart.HasAny() ? &AimCrouchWalkStart : &CrouchWalkSlowStart;
	return PlaysSlowCrouchAtCap(bAim) ? PickFirst({ Slow, &CrouchWalkStart }, Degrees, Rate)
		: PickFirst({ &CrouchWalkStart, Slow }, Degrees, Rate);
}

FVCLocomotionPick UVCLocomotionClipSet::ChooseCrouchStop(float Degrees, bool bAim) const
{
	const float Rate = ComputeCrouchPace(CrouchSpeed, bAim).PlayRate;
	const FVCLocomotionLoops* Slow = bAim && AimCrouchWalkStop.HasAny() ? &AimCrouchWalkStop : &CrouchWalkSlowStop;
	return PlaysSlowCrouchAtCap(bAim) ? PickFirst({ Slow, &CrouchWalkStop }, Degrees, Rate)
		: PickFirst({ &CrouchWalkStop, Slow }, Degrees, Rate);
}
