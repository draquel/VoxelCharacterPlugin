// Copyright Daniel Raquel. All Rights Reserved.

// The gait table and the eased face-aim flip (the character refinement plan's C4, decision D75): the per-gait rows, the
// presets, their application every tick inside the predicted movement, the turn that replaces the one-frame rotation
// mode flip, and the vc.Move.* console surface. The rest of the component is in VCMovementComponent.cpp.

#include "Movement/VCMovementComponent.h"
#include "Movement/VCGravityFrame.h"
#include "VoxelCharacterPlugin.h"

#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

// ==================== FVCGaitSettings ====================

float FVCGaitSettings::MaxSpeedAtAngle(float AngleDegrees) const
{
	// GASP's Curve_StrafeSpeedMap: forward within 45 degrees, the strafe speed at 90, backward from 135, linear between
	const float Angle = FMath::Clamp(FMath::Abs(AngleDegrees), 0.f, 180.f);
	if (Angle <= 45.f)
	{
		return MaxSpeedForward;
	}
	if (Angle <= 90.f)
	{
		return FMath::Lerp(MaxSpeedForward, MaxSpeedStrafe, (Angle - 45.f) / 45.f);
	}
	if (Angle <= 135.f)
	{
		return FMath::Lerp(MaxSpeedStrafe, MaxSpeedBackward, (Angle - 90.f) / 45.f);
	}
	return MaxSpeedBackward;
}

FString FVCGaitSettings::ToString() const
{
	auto Num = [](float Value) { return FString::SanitizeFloat(Value, 0); };
	return FString::Printf(TEXT("{ %s, %s, %s, %s, %s, %s, %s, %s }"), *Num(MaxSpeedForward), *Num(MaxSpeedStrafe), *Num(MaxSpeedBackward),
		*Num(MaxAcceleration), *Num(BrakingDecelerationMoving), *Num(BrakingDecelerationStopping), *Num(GroundFriction),
		*Num(BrakingFrictionFactor));
}

bool FVCGaitSettings::operator==(const FVCGaitSettings& Other) const
{
	return MaxSpeedForward == Other.MaxSpeedForward && MaxSpeedStrafe == Other.MaxSpeedStrafe && MaxSpeedBackward == Other.MaxSpeedBackward
		&& MaxAcceleration == Other.MaxAcceleration && BrakingDecelerationMoving == Other.BrakingDecelerationMoving
		&& BrakingDecelerationStopping == Other.BrakingDecelerationStopping && GroundFriction == Other.GroundFriction
		&& BrakingFrictionFactor == Other.BrakingFrictionFactor;
}

FVCGaitSettings FVCGaitSettings::WithForwardSpeed(float Forward) const
{
	FVCGaitSettings Out = *this;
	if (Forward <= 0.f || MaxSpeedForward <= 0.f)
	{
		return Out;
	}
	const float Scale = Forward / MaxSpeedForward;
	Out.MaxSpeedForward = Forward;
	Out.MaxSpeedStrafe *= Scale;
	Out.MaxSpeedBackward *= Scale;
	return Out;
}

// ==================== FVCMovementTable ====================

FVCMovementTable FVCMovementTable::WithForwardCaps(const FVCGaitCaps& Caps) const
{
	FVCMovementTable Out = *this;
	Out.Walk = Walk.WithForwardSpeed(Caps.Walk);
	Out.Run = Run.WithForwardSpeed(Caps.Run);
	Out.Sprint = Sprint.WithForwardSpeed(Caps.Sprint);
	return Out;
}

const FVCGaitSettings& FVCMovementTable::Row(EVCGait Gait) const
{
	switch (Gait)
	{
	case EVCGait::Walk:   return Walk;
	case EVCGait::Sprint: return Sprint;
	case EVCGait::Crouch: return Crouch;
	default:              return Run;
	}
}

FVCGaitSettings& FVCMovementTable::Row(EVCGait Gait)
{
	return const_cast<FVCGaitSettings&>(static_cast<const FVCMovementTable*>(this)->Row(Gait));
}

namespace
{
	/** A value mapped from one range onto another and clamped to it (FMath::GetMappedRangeValueClamped, in floats). */
	float MapRangeClamped(const FVector2D& InRange, float OutFrom, float OutTo, float Value)
	{
		const float Span = static_cast<float>(InRange.Y - InRange.X);
		if (FMath::IsNearlyZero(Span))
		{
			return Value < InRange.X ? OutFrom : OutTo;
		}
		const float T = FMath::Clamp((Value - static_cast<float>(InRange.X)) / Span, 0.f, 1.f);
		return FMath::Lerp(OutFrom, OutTo, T);
	}
}

float FVCMovementTable::SprintAccelerationAtSpeed(float Speed) const
{
	return MapRangeClamped(SprintAccelerationRamp, Sprint.MaxAcceleration, SprintTopSpeedAcceleration, Speed);
}

float FVCMovementTable::SprintFrictionAtSpeed(float Speed) const
{
	return MapRangeClamped(SprintFrictionRamp, Sprint.GroundFriction, SprintTopSpeedFriction, Speed);
}

FVCMovementTable FVCMovementTable::Preset(EVCMovePreset Preset)
{
	FVCMovementTable Table; // the struct's defaults are GASP's
	switch (Preset)
	{
	case EVCMovePreset::Legacy:
		// What the player ran on before C4: the engine's defaults but the braking (4096, the factor 3) and the yaw rate
		// (500) this component set, the sprint at 1.6 times the walk, the crouch at 150, no ramps.
		Table.Walk = FVCGaitSettings(600.f, 600.f, 600.f, 2048.f, 4096.f, 4096.f, 8.f, 3.f);
		Table.Run = Table.Walk;
		Table.Sprint = FVCGaitSettings(960.f, 960.f, 960.f, 2048.f, 4096.f, 4096.f, 8.f, 3.f);
		Table.Crouch = FVCGaitSettings(150.f, 150.f, 150.f, 2048.f, 4096.f, 4096.f, 8.f, 3.f);
		Table.GroundOrientRotationRate = 500.f;
		Table.GroundStrafeRotationRate = 500.f;
		Table.AirRotationRate = 500.f;
		Table.SwimRotationRate = 500.f;
		Table.SprintTopSpeedAcceleration = 2048.f;
		Table.SprintTopSpeedFriction = 8.f;
		break;
	case EVCMovePreset::GASP:
		break;
	case EVCMovePreset::Tuned:
		// GASP's numbers as tuned on the character test lane: after C4's pass they are GASP's verbatim (every change tried
		// fell within the runs' spread; CHARACTER_SYSTEM.md "Movement"). The preset is where the next pass's changes go.
		break;
	}
	return Table;
}

FString FVCMovementTable::ToString() const
{
	auto Num = [](float Value) { return FString::SanitizeFloat(Value, 0); };
	return FString::Printf(TEXT("Walk %s\nRun %s\nSprint %s\nCrouch %s\nRotation orient %s strafe %s air %s swim %s\nSprint ramps: acceleration to %s over %s..%s, friction to %s over %s..%s"),
		*Walk.ToString(), *Run.ToString(), *Sprint.ToString(), *Crouch.ToString(),
		*Num(GroundOrientRotationRate), *Num(GroundStrafeRotationRate), *Num(AirRotationRate), *Num(SwimRotationRate),
		*Num(SprintTopSpeedAcceleration), *Num(SprintAccelerationRamp.X), *Num(SprintAccelerationRamp.Y),
		*Num(SprintTopSpeedFriction), *Num(SprintFrictionRamp.X), *Num(SprintFrictionRamp.Y));
}

// ==================== The session's table (vc.Move.Preset, vc.Move.Set) ====================

namespace
{
	/** The table the console set for this session, applied to every player's component and to the ones spawned later. */
	TOptional<FVCMovementTable> GSessionTable;
	EVCMovePreset GSessionPreset = EVCMovePreset::Tuned;
	/** The component properties vc.Move.Set changed this session (the eased flip's), by name. */
	TMap<FName, float> GSessionComponentOverrides;

	const TCHAR* PresetName(EVCMovePreset Preset)
	{
		switch (Preset)
		{
		case EVCMovePreset::Legacy: return TEXT("Legacy");
		case EVCMovePreset::GASP:   return TEXT("GASP");
		default:                    return TEXT("Tuned");
		}
	}

	bool ParsePreset(const FString& Name, EVCMovePreset& Out)
	{
		if (Name.Equals(TEXT("Legacy"), ESearchCase::IgnoreCase)) { Out = EVCMovePreset::Legacy; return true; }
		if (Name.Equals(TEXT("GASP"), ESearchCase::IgnoreCase)) { Out = EVCMovePreset::GASP; return true; }
		if (Name.Equals(TEXT("Tuned"), ESearchCase::IgnoreCase)) { Out = EVCMovePreset::Tuned; return true; }
		return false;
	}

	bool ParseGait(const FString& Name, EVCGait& Out)
	{
		if (Name.Equals(TEXT("Walk"), ESearchCase::IgnoreCase)) { Out = EVCGait::Walk; return true; }
		if (Name.Equals(TEXT("Run"), ESearchCase::IgnoreCase)) { Out = EVCGait::Run; return true; }
		if (Name.Equals(TEXT("Sprint"), ESearchCase::IgnoreCase)) { Out = EVCGait::Sprint; return true; }
		if (Name.Equals(TEXT("Crouch"), ESearchCase::IgnoreCase)) { Out = EVCGait::Crouch; return true; }
		return false;
	}

	/** Sets a float (or a vector's X/Y) field of a struct by name. */
	bool SetStructFieldByName(const UScriptStruct* Struct, void* Container, const FString& Field, float Value)
	{
		FString Name = Field, Component;
		Field.Split(TEXT("."), &Name, &Component);
		FProperty* Property = Struct->FindPropertyByName(FName(*Name));
		if (const FFloatProperty* Float = CastField<FFloatProperty>(Property))
		{
			Float->SetPropertyValue_InContainer(Container, Value);
			return true;
		}
		if (const FDoubleProperty* Double = CastField<FDoubleProperty>(Property))
		{
			Double->SetPropertyValue_InContainer(Container, Value);
			return true;
		}
		if (const FStructProperty* Vector = CastField<FStructProperty>(Property); Vector && Vector->Struct == TBaseStructure<FVector2D>::Get())
		{
			FVector2D* V = Vector->ContainerPtrToValuePtr<FVector2D>(Container);
			if (Component.Equals(TEXT("X"), ESearchCase::IgnoreCase)) { V->X = Value; return true; }
			if (Component.Equals(TEXT("Y"), ESearchCase::IgnoreCase)) { V->Y = Value; return true; }
		}
		return false;
	}

	/** Every player's live component with the table on (not the CDO, not an NPC's). */
	void ForEachTableComponent(TFunctionRef<void(UVCMovementComponent&)> Fn)
	{
		for (TObjectIterator<UVCMovementComponent> It; It; ++It)
		{
			UVCMovementComponent* Movement = *It;
			if (Movement && !Movement->IsTemplate() && Movement->bUseGaitTable && Movement->GetWorld() && Movement->GetWorld()->IsGameWorld())
			{
				Fn(*Movement);
			}
		}
	}

	void LogTable(const TCHAR* Prefix)
	{
		const FVCMovementTable& Table = GSessionTable.IsSet() ? GSessionTable.GetValue() : FVCMovementTable::Preset(GSessionPreset);
		UE_LOG(LogVoxelCharacter, Log, TEXT("%s (%s):\n%s"), Prefix, PresetName(GSessionPreset), *Table.ToString());
	}

	void VCCmd_MovePreset(const TArray<FString>& Args)
	{
		EVCMovePreset Preset;
		if (Args.Num() < 1 || !ParsePreset(Args[0], Preset))
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("vc.Move.Preset Legacy|GASP|Tuned (now %s)"), PresetName(GSessionPreset));
			return;
		}
		GSessionPreset = Preset;
		GSessionTable = FVCMovementTable::Preset(Preset);
		int32 Count = 0;
		ForEachTableComponent([&Count](UVCMovementComponent& Movement) { Movement.GaitTable = GSessionTable.GetValue(); ++Count; });
		UE_LOG(LogVoxelCharacter, Log, TEXT("vc.Move.Preset: %s on %d player component(s)"), PresetName(Preset), Count);
		LogTable(TEXT("vc.Move table"));
	}

	void VCCmd_MoveSet(const TArray<FString>& Args)
	{
		// vc.Move.Set <Walk|Run|Sprint|Crouch|All> <Field> <Value>, or vc.Move.Set <TableField> <Value> for the rates and ramps
		if (!GSessionTable.IsSet())
		{
			GSessionTable = FVCMovementTable::Preset(GSessionPreset);
		}
		FVCMovementTable& Table = GSessionTable.GetValue();
		bool bSet = false;
		EVCGait Gait = EVCGait::Run;
		if (Args.Num() >= 3 && (ParseGait(Args[0], Gait) || Args[0].Equals(TEXT("All"), ESearchCase::IgnoreCase)))
		{
			const float Value = FCString::Atof(*Args[2]);
			if (Args[0].Equals(TEXT("All"), ESearchCase::IgnoreCase))
			{
				bSet = true;
				for (EVCGait Each : { EVCGait::Walk, EVCGait::Run, EVCGait::Sprint, EVCGait::Crouch })
				{
					bSet &= SetStructFieldByName(FVCGaitSettings::StaticStruct(), &Table.Row(Each), Args[1], Value);
				}
			}
			else
			{
				bSet = SetStructFieldByName(FVCGaitSettings::StaticStruct(), &Table.Row(Gait), Args[1], Value);
			}
		}
		else if (Args.Num() >= 2)
		{
			bSet = SetStructFieldByName(FVCMovementTable::StaticStruct(), &Table, Args[0], FCString::Atof(*Args[1]));
			if (!bSet)
			{
				// The eased flip's own properties (AimTurnRate, AimTurnTolerance, AimTurnMaxTime): on every player's
				// component, and kept for the ones spawned later this session
				const float Value = FCString::Atof(*Args[1]);
				const FName Field(*Args[0]);
				if (const FFloatProperty* Property = CastField<FFloatProperty>(UVCMovementComponent::StaticClass()->FindPropertyByName(Field));
					Property && Field.ToString().StartsWith(TEXT("AimTurn")))
				{
					GSessionComponentOverrides.Add(Field, Value);
					ForEachTableComponent([Property, Value](UVCMovementComponent& Movement) { Property->SetPropertyValue_InContainer(&Movement, Value); });
					UE_LOG(LogVoxelCharacter, Log, TEXT("vc.Move.Set: %s %s on every player"), *Field.ToString(), *FString::SanitizeFloat(Value));
					return;
				}
			}
		}
		if (!bSet)
		{
			UE_LOG(LogVoxelCharacter, Warning, TEXT("vc.Move.Set <Walk|Run|Sprint|Crouch|All> <MaxSpeedForward|MaxSpeedStrafe|MaxSpeedBackward|MaxAcceleration|BrakingDecelerationMoving|BrakingDecelerationStopping|GroundFriction|BrakingFrictionFactor> <value>, or vc.Move.Set <GroundOrientRotationRate|GroundStrafeRotationRate|AirRotationRate|SwimRotationRate|SprintTopSpeedAcceleration|SprintAccelerationRamp.X|SprintAccelerationRamp.Y|SprintTopSpeedFriction|SprintFrictionRamp.X|SprintFrictionRamp.Y|AimTurnRate|AimTurnTolerance|AimTurnMaxTime> <value>"));
			return;
		}
		ForEachTableComponent([&Table](UVCMovementComponent& Movement) { Movement.GaitTable = Table; });
		LogTable(TEXT("vc.Move.Set: the table in force"));
	}

	void VCCmd_MovePrint(const TArray<FString>& Args)
	{
		LogTable(TEXT("vc.Move.Print: the table in force"));
		ForEachTableComponent([](UVCMovementComponent& Movement)
		{
			UE_LOG(LogVoxelCharacter, Log, TEXT("  %s: gait %s, %s, row %s, MaxWalkSpeed %.0f MaxAcceleration %.0f Braking %.0f GroundFriction %.2f RotationRate %.0f%s"),
				*GetNameSafe(Movement.GetOwner()), *StaticEnum<EVCGait>()->GetNameStringByValue(static_cast<int64>(Movement.GetGait())),
				Movement.GetRotationMode() == EVCRotationMode::Strafe ? TEXT("strafe") : TEXT("orient"), *Movement.GetGaitRow().ToString(),
				Movement.MaxWalkSpeed, Movement.MaxAcceleration, Movement.BrakingDecelerationWalking, Movement.GroundFriction,
				Movement.RotationRate.Yaw, Movement.IsAimTurnPending() ? TEXT(" (aim turn pending)") : TEXT(""));
		});
	}

	FAutoConsoleCommand GVCCmdMovePreset(
		TEXT("vc.Move.Preset"),
		TEXT("The player's movement table, swapped live: vc.Move.Preset Legacy|GASP|Tuned (the engine defaults the player ran on before C4; the Game Animation Sample's per-gait tuning; GASP's tuned on the lane, the default). Applies to every player and to the pawns spawned afterwards this session; the table is logged."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&VCCmd_MovePreset));

	FAutoConsoleCommand GVCCmdMoveSet(
		TEXT("vc.Move.Set"),
		TEXT("One value of the player's movement table, live: vc.Move.Set <Walk|Run|Sprint|Crouch|All> <Field> <value> for a row's field (MaxSpeedForward, MaxSpeedStrafe, MaxSpeedBackward, MaxAcceleration, BrakingDecelerationMoving, BrakingDecelerationStopping, GroundFriction, BrakingFrictionFactor), or vc.Move.Set <Field> <value> for a rate or ramp (GroundOrientRotationRate, GroundStrafeRotationRate, AirRotationRate, SwimRotationRate, SprintTopSpeedAcceleration, SprintAccelerationRamp.X/.Y, SprintTopSpeedFriction, SprintFrictionRamp.X/.Y). The table in force is logged as the rows to commit."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&VCCmd_MoveSet));

	FAutoConsoleCommand GVCCmdMovePrint(
		TEXT("vc.Move.Print"),
		TEXT("Log the player's movement table in force (the rows as C++ initializers) and each player's gait, rotation mode and the engine values set this tick."),
		FConsoleCommandWithArgsDelegate::CreateStatic(&VCCmd_MovePrint));
}

void UVCMovementComponent::ApplySessionTableOverride()
{
	if (!bUseGaitTable)
	{
		return;
	}
	if (GSessionTable.IsSet())
	{
		GaitTable = GSessionTable.GetValue();
	}
	for (const TPair<FName, float>& Override : GSessionComponentOverrides)
	{
		if (const FFloatProperty* Property = CastField<FFloatProperty>(GetClass()->FindPropertyByName(Override.Key)))
		{
			Property->SetPropertyValue_InContainer(this, Override.Value);
		}
	}
}

// ==================== The gait and its application ====================

EVCGait UVCMovementComponent::GetGait() const
{
	if (IsCrouching())
	{
		return EVCGait::Crouch;
	}
	if (bWantsToSprint && !bSprintExhausted)
	{
		return EVCGait::Sprint;
	}
	return bWantsToWalk ? EVCGait::Walk : EVCGait::Run;
}

void UVCMovementComponent::SetWalking(bool bWantsWalk)
{
	bWantsToWalk = bWantsWalk;
}

float UVCMovementComponent::AngleAboutUp(const FVector& Forward, const FVector& Direction, const FVector& Up)
{
	const FVector F = FVector::VectorPlaneProject(Forward, Up).GetSafeNormal();
	const FVector D = FVector::VectorPlaneProject(Direction, Up).GetSafeNormal();
	if (F.IsNearlyZero() || D.IsNearlyZero())
	{
		return 0.f;
	}
	const float Dot = FMath::Clamp(static_cast<float>(FVector::DotProduct(F, D)), -1.f, 1.f);
	const float Degrees = FMath::RadiansToDegrees(FMath::Acos(Dot));
	return FVector::DotProduct(FVector::CrossProduct(F, D), Up) < 0.0 ? -Degrees : Degrees;
}

float UVCMovementComponent::MovementAngleToFacing(const FVector& Velocity, const FVector& InAcceleration, const FVector& Forward, const FVector& Up)
{
	// Moving: the velocity's angle (GASP's); starting from rest: the input's, so a backpedal starts at the backward cap.
	if (FVector::VectorPlaneProject(Velocity, Up).SizeSquared() > 1.0)
	{
		return AngleAboutUp(Forward, Velocity, Up);
	}
	return InAcceleration.IsNearlyZero() ? 0.f : AngleAboutUp(Forward, InAcceleration, Up);
}

FVCGaitValues UVCMovementComponent::ComputeGaitValues(const FVCMovementTable& Table, EVCGait Gait, EVCRotationMode Mode, bool bOnGround,
	bool bInAir, const FVector& Velocity, const FVector& InAcceleration, const FVector& Forward, const FVector& Up, float SpeedMultiplier,
	float FrictionScale)
{
	FVCGaitValues Out;
	// The yaw rate by mode: instant on the ground (the graph turns the body), slow in the air, the old rate in the water.
	Out.RotationRate = bOnGround
		? (Mode == EVCRotationMode::OrientToMovement ? Table.GroundOrientRotationRate : Table.GroundStrafeRotationRate)
		: bInAir ? Table.AirRotationRate : Table.SwimRotationRate;
	Out.bRowApplies = bOnGround || bInAir;
	if (!Out.bRowApplies)
	{
		return Out; // the swim's and the flight's values are their own
	}

	const FVCGaitSettings& Row = Table.Row(Gait);
	// The cap: forward in orient mode (the body turns to where it goes), by the angle to the facing in strafe mode.
	const float Cap = Mode == EVCRotationMode::OrientToMovement
		? Row.MaxSpeedForward : Row.MaxSpeedAtAngle(MovementAngleToFacing(Velocity, InAcceleration, Forward, Up));
	Out.MaxSpeed = Cap * SpeedMultiplier;

	// The sprint's acceleration and friction fall with speed (GASP's ramps); the friction keeps the surface and wet scale.
	Out.MaxAcceleration = Row.MaxAcceleration;
	float Friction = Row.GroundFriction;
	if (Gait == EVCGait::Sprint)
	{
		const float Speed = static_cast<float>(FVector::VectorPlaneProject(Velocity, Up).Size());
		Out.MaxAcceleration = Table.SprintAccelerationAtSpeed(Speed);
		Friction = Table.SprintFrictionAtSpeed(Speed);
	}
	Out.GroundFriction = Friction * FrictionScale;
	Out.BrakingDeceleration = InAcceleration.IsNearlyZero() ? Row.BrakingDecelerationStopping : Row.BrakingDecelerationMoving;
	Out.BrakingFrictionFactor = Row.BrakingFrictionFactor;
	return Out;
}

FVCMovementTable UVCMovementComponent::ResolveGaitTable(const FVCMovementTable& Table, const FVCGaitCaps& Caps, bool bApply)
{
	return bApply && Caps.IsSet() ? Table.WithForwardCaps(Caps) : Table;
}

void UVCMovementComponent::ApplyGaitTable()
{
	if (!bUseGaitTable || !UpdatedComponent)
	{
		return;
	}
	const FVCMovementTable Table = ResolveGaitTable(GaitTable, ClipGaitCaps, bWantsClipGaitCaps);
	const FVCGaitValues Values = ComputeGaitValues(Table, GetGait(), GetRotationMode(), IsMovingOnGround(), IsFalling(), Velocity,
		Acceleration, UpdatedComponent->GetForwardVector(), FVCGravityFrame::GetUp(this), GASSpeedMultiplier, GroundFrictionScale);
	RotationRate.Yaw = Values.RotationRate;
	if (!Values.bRowApplies)
	{
		return;
	}
	MaxWalkSpeed = Values.MaxSpeed;
	MaxWalkSpeedCrouched = Values.MaxSpeed;
	MaxAcceleration = Values.MaxAcceleration;
	GroundFriction = Values.GroundFriction;
	BrakingDecelerationWalking = Values.BrakingDeceleration;
	BrakingFrictionFactor = Values.BrakingFrictionFactor;
}

// ==================== The eased face-aim flip ====================

void UVCMovementComponent::SetFacesAim(bool bFaceAim)
{
	bFacesAimTarget = bFaceAim;
	const bool bAlready = bUseControllerDesiredRotation == bFaceAim && bOrientRotationToMovement == !bFaceAim;
	if (bAlready && !bAimTurnPending)
	{
		return;
	}
	bAimTurnPending = true;
	AimTurnElapsed = 0.f;
	// Within tolerance already (or nothing to turn toward): the flags change now, as before C4.
	AdvanceAimTurn(0.f);
}

bool UVCMovementComponent::AdvanceAimTurn(float DeltaTime)
{
	if (!bAimTurnPending)
	{
		return false;
	}
	auto Complete = [this]()
	{
		bOrientRotationToMovement = !bFacesAimTarget;
		bUseControllerDesiredRotation = bFacesAimTarget;
		bAimTurnPending = false;
		AimTurnElapsed = 0.f;
		return false;
	};
	ACharacter* Character = GetCharacterOwner();
	if (!Character || !UpdatedComponent)
	{
		return Complete();
	}

	// The new mode's target: the view (a draw), or where the body moves (the release; standing still, there is none).
	const FVector Up = FVCGravityFrame::GetUp(this);
	FVector Target = FVector::ZeroVector;
	if (bFacesAimTarget)
	{
		const AController* Controller = Character->GetController();
		Target = Controller ? Controller->GetDesiredRotation().Vector() : Character->GetBaseAimRotation().Vector();
	}
	else if (GetHorizontalSpeedSquared(Velocity) > 1.0)
	{
		Target = Velocity;
	}
	else if (!Acceleration.IsNearlyZero())
	{
		Target = Acceleration;
	}
	Target = FVector::VectorPlaneProject(Target, Up);
	if (Target.IsNearlyZero())
	{
		return Complete();
	}

	AimTurnElapsed += FMath::Max(DeltaTime, 0.f);
	const float Remaining = AngleAboutUp(UpdatedComponent->GetForwardVector(), Target, Up);
	if (FMath::Abs(Remaining) <= AimTurnTolerance || AimTurnElapsed >= AimTurnMaxTime)
	{
		return Complete();
	}
	if (DeltaTime <= 0.f)
	{
		return true;
	}

	// Turn about local up by at most AimTurnRate this frame (the gravity frame's yaw: FVCGravityFrame keeps the body upright).
	const float Step = FMath::Clamp(Remaining, -AimTurnRate * DeltaTime, AimTurnRate * DeltaTime);
	const FQuat NewRotation = FQuat(Up, FMath::DegreesToRadians(Step)) * UpdatedComponent->GetComponentQuat();
	MoveUpdatedComponent(FVector::ZeroVector, NewRotation, /*bSweep=*/false);
	return true;
}

void UVCMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (bAimTurnPending && HasValidData() && AdvanceAimTurn(DeltaTime))
	{
		return;
	}
	Super::PhysicsRotation(DeltaTime);
}
