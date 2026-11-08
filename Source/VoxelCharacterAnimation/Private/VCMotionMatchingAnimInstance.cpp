// Copyright Daniel Raquel. All Rights Reserved.

#include "VCMotionMatchingAnimInstance.h"

#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "HAL/IConsoleManager.h"
#include "Movement/VCMovementComponent.h"
#include "PoseSearch/AnimNode_MotionMatching.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "UObject/UnrealType.h"
#include "VCGravityTrajectoryPredictor.h"
#include "VoxelCharacterAnimation.h"

namespace
{
	TAutoConsoleVariable<int32> CVarAnimGravityTrajectory(
		TEXT("vc.Anim.GravityTrajectory"), 1,
		TEXT("The player's motion-matching trajectory: 1 predicted in the gravity frame, 0 the engine's world-Z prediction (A/B; character animation P5)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarAnimTrajectoryRotateSpeed(
		TEXT("vc.Anim.TrajectoryRotateSpeed"), -1.f,
		TEXT("The motion-matching trajectory's RotateTowardsMovementSpeed, live (how fast the predicted facing turns to the movement; GASP 10): negative = the anim instance's RotateTowardsMovementSpeed, itself negative for the graph's own (the character refinement plan's C4)."),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarAnimTrajectoryYawRate(
		TEXT("vc.Anim.TrajectoryYawRate"), -1.f,
		TEXT("The motion-matching trajectory's MaxControllerYawRate, live (degrees a second the prediction caps the view's turn at; GASP 70): negative = the anim instance's MaxControllerYawRate, itself negative for the graph's own (C4)."),
		ECVF_Default);

	/** A field of a Blueprint struct by the name its author gave it (the property's own name carries a GUID). */
	FProperty* FindAuthoredField(const UStruct* Struct, const TCHAR* Name)
	{
		if (Struct)
		{
			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				if (Struct->GetAuthoredNameForField(*It) == Name)
				{
					return *It;
				}
			}
		}
		return nullptr;
	}

	/** The value of a byte or enum property's enumerator by its display name (GASP's enums are Blueprint enums). */
	int64 EnumValueByDisplayName(const FProperty* Property, const TCHAR* DisplayName, int64 Fallback)
	{
		const UEnum* Enum = nullptr;
		if (const FByteProperty* Byte = CastField<FByteProperty>(Property))
		{
			Enum = Byte->Enum;
		}
		else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			Enum = EnumProperty->GetEnum();
		}
		if (Enum)
		{
			for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
			{
				if (Enum->GetDisplayNameTextByIndex(Index).ToString() == DisplayName)
				{
					return Enum->GetValueByIndex(Index);
				}
			}
		}
		return Fallback;
	}

	template <typename T>
	void SetStructField(void* Container, FProperty* Property, const T& Value)
	{
		const FStructProperty* Struct = CastField<FStructProperty>(Property);
		if (Struct && Struct->Struct == TBaseStructure<T>::Get())
		{
			*Struct->ContainerPtrToValuePtr<T>(Container) = Value;
		}
	}

	void SetBoolField(void* Container, FProperty* Property, bool bValue)
	{
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
		{
			Bool->SetPropertyValue_InContainer(Container, bValue);
		}
	}

	void SetRealField(void* Container, FProperty* Property, double Value)
	{
		const FNumericProperty* Numeric = CastField<FNumericProperty>(Property);
		if (Numeric && Numeric->IsFloatingPoint())
		{
			Numeric->SetFloatingPointPropertyValue(Numeric->ContainerPtrToValuePtr<void>(Container), Value);
		}
	}

	void SetEnumField(void* Container, FProperty* Property, int64 Value)
	{
		if (const FByteProperty* Byte = CastField<FByteProperty>(Property))
		{
			Byte->SetIntPropertyValue(Byte->ContainerPtrToValuePtr<void>(Container), static_cast<uint64>(Value));
		}
		else if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
		{
			Enum->GetUnderlyingProperty()->SetIntPropertyValue(Enum->ContainerPtrToValuePtr<void>(Container), Value);
		}
	}
}

// ==================== The frame ====================

bool UVCMotionMatchingAnimInstance::IsWorldUp(const FVector& Up)
{
	return Up.Equals(FVector::UpVector, 1e-6);
}

FQuat UVCMotionMatchingAnimInstance::FrameBasis(const FVector& Up)
{
	return FQuat::FindBetweenNormals(FVector::UpVector, Up.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector));
}

FVector UVCMotionMatchingAnimInstance::ToFrame(const FVector& Vector, const FVector& Up)
{
	return IsWorldUp(Up) ? Vector : FrameBasis(Up).UnrotateVector(Vector);
}

double UVCMotionMatchingAnimInstance::GroundSpeed(const FVector& Vector, const FVector& Up)
{
	return IsWorldUp(Up) ? Vector.Size2D() : FVector::VectorPlaneProject(Vector, Up.GetSafeNormal()).Size();
}

float UVCMotionMatchingAnimInstance::DirectionInFrame(const FVector& Velocity, const FRotator& BaseRotation, const FVector& Up)
{
	FVector LocalVelocity = Velocity;
	FRotator LocalRotation = BaseRotation;
	if (!IsWorldUp(Up))
	{
		const FQuat Basis = FrameBasis(Up);
		LocalVelocity = Basis.UnrotateVector(Velocity);
		LocalRotation = (Basis.Inverse() * BaseRotation.Quaternion()).Rotator();
	}

	// UKismetAnimationLibrary::CalculateDirection, on the frame's horizontal
	if (LocalVelocity.IsNearlyZero())
	{
		return 0.f;
	}
	const FMatrix RotationMatrix = FRotationMatrix(LocalRotation);
	const FVector Forward = RotationMatrix.GetScaledAxis(EAxis::X);
	const FVector Right = RotationMatrix.GetScaledAxis(EAxis::Y);
	const FVector Direction = LocalVelocity.GetSafeNormal2D();
	float Degrees = FMath::RadiansToDegrees(FMath::Acos(static_cast<float>(FVector::DotProduct(Forward, Direction))));
	if (static_cast<float>(FVector::DotProduct(Right, Direction)) < 0.f)
	{
		Degrees *= -1.f;
	}
	return Degrees;
}

FRotator UVCMotionMatchingAnimInstance::DeltaRotatorInFrame(const FRotator& A, const FRotator& B, const FVector& Up)
{
	FRotator Delta;
	if (IsWorldUp(Up))
	{
		Delta = A - B;
	}
	else
	{
		const FQuat ToLocal = FrameBasis(Up).Inverse();
		Delta = (ToLocal * A.Quaternion()).Rotator() - (ToLocal * B.Quaternion()).Rotator();
	}
	Delta.Normalize();
	return Delta;
}

FRotator UVCMotionMatchingAnimInstance::AddYawInFrame(const FRotator& Rotation, float YawDegrees, const FVector& Up)
{
	if (IsWorldUp(Up))
	{
		return FRotator(Rotation.Pitch, Rotation.Yaw + YawDegrees, Rotation.Roll);
	}
	const FQuat Basis = FrameBasis(Up);
	FRotator Local = (Basis.Inverse() * Rotation.Quaternion()).Rotator();
	Local.Yaw += YawDegrees;
	return (Basis * Local.Quaternion()).Rotator();
}

// ==================== The graph's functions ====================

double UVCMotionMatchingAnimInstance::GravityVSizeXY(FVector A) const
{
	return GroundSpeed(A, GravityUp);
}

float UVCMotionMatchingAnimInstance::GravityCalculateDirection(const FVector& Velocity, const FRotator& BaseRotation) const
{
	return DirectionInFrame(Velocity, BaseRotation, GravityUp);
}

FRotator UVCMotionMatchingAnimInstance::GravityNormalizedDeltaRotator(FRotator A, FRotator B) const
{
	return DeltaRotatorInFrame(A, B, GravityUp);
}

FRotator UVCMotionMatchingAnimInstance::GravityAddYaw(FRotator Rotation, float Yaw) const
{
	return AddYawInFrame(Rotation, Yaw, GravityUp);
}

FTransform UVCMotionMatchingAnimInstance::GravityTransformAddYaw(const FTransform& InTransform, float Yaw) const
{
	return FTransform(AddYawInFrame(InTransform.Rotator(), Yaw, GravityUp), InTransform.GetLocation(), FVector::OneVector);
}

FPoseSearchTrajectoryData UVCMotionMatchingAnimInstance::TunedTrajectoryData(const FPoseSearchTrajectoryData& InData) const
{
	FPoseSearchTrajectoryData Data = InData;
	const float RotateSpeedInForce = CVarAnimTrajectoryRotateSpeed.GetValueOnAnyThread() >= 0.f
		? CVarAnimTrajectoryRotateSpeed.GetValueOnAnyThread() : RotateTowardsMovementSpeed;
	if (RotateSpeedInForce >= 0.f)
	{
		Data.RotateTowardsMovementSpeed = RotateSpeedInForce;
	}
	const float YawRateInForce = CVarAnimTrajectoryYawRate.GetValueOnAnyThread() >= 0.f
		? CVarAnimTrajectoryYawRate.GetValueOnAnyThread() : MaxControllerYawRate;
	if (YawRateInForce >= 0.f)
	{
		Data.MaxControllerYawRate = YawRateInForce;
	}
	return Data;
}

void UVCMotionMatchingAnimInstance::GravityGenerateTransformTrajectory(const UObject* InAnimInstance,
	const FPoseSearchTrajectoryData& InTrajectoryData, float InDeltaTime, FTransformTrajectory& InOutTrajectory,
	float& InOutDesiredControllerYawLastUpdate, FTransformTrajectory& OutTrajectory, float InHistorySamplingInterval,
	int32 InTrajectoryHistoryCount, float InPredictionSamplingInterval, int32 InTrajectoryPredictionCount)
{
	const FPoseSearchTrajectoryData Tuned = TunedTrajectoryData(InTrajectoryData);
	if (CVarAnimGravityTrajectory.GetValueOnAnyThread() == 0 || !GravityPredictor)
	{
		// The A/B: the engine's prediction, turning about world Z
		UPoseSearchTrajectoryLibrary::PoseSearchGenerateTransformTrajectory(InAnimInstance, Tuned, InDeltaTime,
			InOutTrajectory, InOutDesiredControllerYawLastUpdate, OutTrajectory, InHistorySamplingInterval,
			InTrajectoryHistoryCount, InPredictionSamplingInterval, InTrajectoryPredictionCount);
		return;
	}
	GravityPredictor->SetPrediction(Tuned, true);
	UPoseSearchTrajectoryLibrary::PoseSearchGenerateTransformTrajectoryWithPredictor(
		TScriptInterface<IPoseSearchTrajectoryPredictorInterface>(GravityPredictor.Get()), InDeltaTime, InOutTrajectory,
		InOutDesiredControllerYawLastUpdate, OutTrajectory, InHistorySamplingInterval, InTrajectoryHistoryCount,
		InPredictionSamplingInterval, InTrajectoryPredictionCount);
}

// ==================== The update ====================

void UVCMotionMatchingAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	if (!GravityPredictor)
	{
		// Outer = this anim instance: the engine's montage prediction finds the anim instance through the outer
		GravityPredictor = NewObject<UVCGravityTrajectoryPredictor>(this);
	}
	FindGASPFields();
}

void UVCMotionMatchingAnimInstance::FindGASPFields()
{
	Fields = FGASPFields();
	const UClass* Class = GetClass();
	Fields.Root = FindFProperty<FStructProperty>(Class, TEXT("CharacterProperties"));
	if (!Fields.Root)
	{
		Fields.Missing.Add(TEXT("CharacterProperties"));
	}
	else
	{
		const UScriptStruct* Struct = Fields.Root->Struct;
		auto Find = [this, Struct](const TCHAR* Name)
		{
			FProperty* Field = FindAuthoredField(Struct, Name);
			if (!Field)
			{
				Fields.Missing.Add(Name);
			}
			return Field;
		};
		Fields.ActorTransform = Find(TEXT("ActorTransform"));
		Fields.Velocity = Find(TEXT("Velocity"));
		Fields.InputAcceleration = Find(TEXT("InputAcceleration"));
		Fields.CurrentMaxAcceleration = Find(TEXT("CurrentMaxAcceleration"));
		Fields.CurrentMaxDeceleration = Find(TEXT("CurrentMaxDeceleration"));
		Fields.OrientationIntent = Find(TEXT("OrientationIntent"));
		Fields.AimingRotation = Find(TEXT("AimingRotation"));
		Fields.JustLanded = Find(TEXT("JustLanded"));
		Fields.LandVelocity = Find(TEXT("LandVelocity"));
		Fields.GroundNormal = Find(TEXT("GroundNormal"));
		Fields.Gait = Find(TEXT("Gait"));
		Fields.MovementMode = Find(TEXT("MovementMode"));
		Fields.RotationMode = Find(TEXT("RotationMode"));
		Fields.Stance = Find(TEXT("Stance"));
		Fields.InputState = CastField<FStructProperty>(Find(TEXT("InputState")));
		if (Fields.InputState)
		{
			const UScriptStruct* InputStruct = Fields.InputState->Struct;
			auto FindInput = [this, InputStruct](const TCHAR* Name)
			{
				FProperty* Field = FindAuthoredField(InputStruct, Name);
				if (!Field)
				{
					Fields.Missing.Add(FString(TEXT("InputState.")) + Name);
				}
				return Field;
			};
			Fields.WantsToSprint = FindInput(TEXT("WantsToSprint"));
			Fields.WantsToWalk = FindInput(TEXT("WantsToWalk"));
			Fields.WantsToStrafe = FindInput(TEXT("WantsToStrafe"));
			Fields.WantsToAim = FindInput(TEXT("WantsToAim"));
		}
		Fields.GaitWalk = EnumValueByDisplayName(Fields.Gait, TEXT("Walk"), 0);
		Fields.GaitRun = EnumValueByDisplayName(Fields.Gait, TEXT("Run"), 1);
		Fields.GaitSprint = EnumValueByDisplayName(Fields.Gait, TEXT("Sprint"), 2);
		Fields.ModeOnGround = EnumValueByDisplayName(Fields.MovementMode, TEXT("OnGround"), 0);
		Fields.ModeInAir = EnumValueByDisplayName(Fields.MovementMode, TEXT("InAir"), 1);
		Fields.RotationOrient = EnumValueByDisplayName(Fields.RotationMode, TEXT("OrientToMovement"), 0);
		Fields.RotationStrafe = EnumValueByDisplayName(Fields.RotationMode, TEXT("Strafe"), 1);
		Fields.StanceStand = EnumValueByDisplayName(Fields.Stance, TEXT("Stand"), 0);
		Fields.StanceCrouch = EnumValueByDisplayName(Fields.Stance, TEXT("Crouch"), 1);
	}

	auto FindTuning = [Class](const TCHAR* Name)
	{
		FStructProperty* Property = FindFProperty<FStructProperty>(Class, Name);
		return Property && Property->Struct == FPoseSearchTrajectoryData::StaticStruct() ? Property : nullptr;
	};
	for (TFieldIterator<FStructProperty> It(Class); It; ++It)
	{
		if (It->Struct->IsChildOf(FAnimNode_MotionMatching::StaticStruct()))
		{
			Fields.MotionMatchingNode = *It;
			break;
		}
	}
	Fields.TrajectoryDataIdle = FindTuning(TEXT("TrajectoryGenerationData_Idle"));
	Fields.TrajectoryDataMoving = FindTuning(TEXT("TrajectoryGenerationData_Moving"));

	if (!Fields.Missing.IsEmpty() && !IsTemplate())
	{
		UE_LOG(LogVoxelCharacterAnimation, Warning, TEXT("%s: GASP's CharacterProperties lacks %s; those stay at their defaults"),
			*Class->GetName(), *FString::Join(Fields.Missing, TEXT(", ")));
	}
}

void UVCMotionMatchingAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	const ACharacter* Character = Cast<ACharacter>(TryGetPawnOwner());
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}
	GravityUp = -Movement->GetGravityDirection();

	// The landing: GASP's pawn reports it a short while, with the velocity it landed at (its Z along local up here)
	const bool bFalling = Movement->IsFalling();
	if (bWasFalling && !bFalling && Movement->IsMovingOnGround() && GetWorld())
	{
		LandTime = GetWorld()->GetTimeSeconds();
		LandVelocityInFrame = ToFrame(FallingVelocity, GravityUp);
	}
	if (bFalling)
	{
		FallingVelocity = Movement->Velocity;
	}
	bWasFalling = bFalling;

	if (Fields.Root)
	{
		FillCharacterProperties(*Character);
	}

	// The predictor's state, with the tuning GASP's graph picks by speed (Speed2D > 0: moving)
	if (GravityPredictor)
	{
		const FStructProperty* Tuning = GroundSpeed(Movement->Velocity, GravityUp) > 0.0 ? Fields.TrajectoryDataMoving : Fields.TrajectoryDataIdle;
		const FPoseSearchTrajectoryData TrajectoryData = TunedTrajectoryData(Tuning
			? *Tuning->ContainerPtrToValuePtr<FPoseSearchTrajectoryData>(this) : FPoseSearchTrajectoryData());
		GravityPredictor->Capture(Character, DeltaSeconds, TrajectoryData);
	}

	static const IConsoleVariable* DebugVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("vc.Anim.Debug"));
	if (DebugVariable && DebugVariable->GetInt() != 0)
	{
		DrawTrajectory();
	}
}

void UVCMotionMatchingAnimInstance::FillCharacterProperties(const ACharacter& Character)
{
	const UCharacterMovementComponent* Movement = Character.GetCharacterMovement();
	const UWorld* World = GetWorld();
	if (!Movement || !World)
	{
		return;
	}
	void* Properties = Fields.Root->ContainerPtrToValuePtr<void>(this);
	const FVCAnimInput* Input = GetGatheredInput();

	SetStructField(Properties, Fields.ActorTransform, Character.GetActorTransform());
	SetStructField(Properties, Fields.Velocity, Movement->Velocity);
	SetStructField(Properties, Fields.InputAcceleration, Movement->GetCurrentAcceleration());
	SetRealField(Properties, Fields.CurrentMaxAcceleration, Movement->GetMaxAcceleration());
	// The braking in force (GASP passes GetMaxBrakingDeceleration; the gait table sets the walking one by whether there is input, C4)
	SetRealField(Properties, Fields.CurrentMaxDeceleration, Movement->GetMaxBrakingDeceleration());
	SetStructField(Properties, Fields.OrientationIntent, Character.GetActorRotation());
	const AController* Controller = Character.GetController();
	SetStructField(Properties, Fields.AimingRotation,
		Character.IsLocallyControlled() && Controller ? Controller->GetControlRotation() : Character.GetBaseAimRotation());
	SetBoolField(Properties, Fields.JustLanded, LandTime >= 0.0 && World->GetTimeSeconds() - LandTime < JustLandedDuration);
	SetStructField(Properties, Fields.LandVelocity, LandVelocityInFrame);
	SetStructField(Properties, Fields.GroundNormal, FVector(Movement->CurrentFloor.HitResult.ImpactNormal));

	// The gait: the voxel movement's own where there is one (its table's row: the crouch is the stance, its gait the walk's
	// pace; C4), else by the max speed.
	const bool bSprinting = Input && Input->bSprinting;
	int64 Gait;
	if (const UVCMovementComponent* VoxelMovement = Cast<UVCMovementComponent>(Movement))
	{
		switch (VoxelMovement->GetGait())
		{
		case EVCGait::Sprint: Gait = Fields.GaitSprint; break;
		case EVCGait::Walk:
		case EVCGait::Crouch: Gait = Fields.GaitWalk; break;
		default:              Gait = Fields.GaitRun; break;
		}
	}
	else
	{
		Gait = bSprinting ? Fields.GaitSprint : (Movement->GetMaxSpeed() <= WalkGaitMaxSpeed ? Fields.GaitWalk : Fields.GaitRun);
	}
	SetEnumField(Properties, Fields.Gait, Gait);
	// Swimming counts as the ground: the graph's own pose under a swimmer is replaced by the swim (the script's branch)
	SetEnumField(Properties, Fields.MovementMode, Movement->IsFalling() || Movement->IsFlying() ? Fields.ModeInAir : Fields.ModeOnGround);
	SetEnumField(Properties, Fields.RotationMode, Movement->bOrientRotationToMovement ? Fields.RotationOrient : Fields.RotationStrafe);
	SetEnumField(Properties, Fields.Stance, Movement->IsCrouching() ? Fields.StanceCrouch : Fields.StanceStand);
	if (Fields.InputState)
	{
		void* InputState = Fields.InputState->ContainerPtrToValuePtr<void>(Properties);
		SetBoolField(InputState, Fields.WantsToSprint, bSprinting);
		SetBoolField(InputState, Fields.WantsToWalk, Gait == Fields.GaitWalk);
		SetBoolField(InputState, Fields.WantsToStrafe, !Movement->bOrientRotationToMovement);
		SetBoolField(InputState, Fields.WantsToAim, false);
	}
}

FString UVCMotionMatchingAnimInstance::GetMotionMatchingSelection() const
{
	if (!Fields.MotionMatchingNode)
	{
		return FString();
	}
	const FAnimNode_MotionMatching* Node = Fields.MotionMatchingNode->ContainerPtrToValuePtr<FAnimNode_MotionMatching>(this);
	const FPoseSearchBlueprintResult& Result = Node->GetMotionMatchingState().SearchResult;
	if (!Result.SelectedAnim)
	{
		return FString();
	}
	return FString::Printf(TEXT("%s / %s (%.2f)"), *GetNameSafe(Result.SelectedDatabase.Get()), *Result.SelectedAnim->GetName(),
		Result.SearchCost < MAX_flt ? Result.SearchCost : -1.f);
}

void UVCMotionMatchingAnimInstance::DrawTrajectory() const
{
	const UWorld* World = GetWorld();
	if (!World || !GravityPredictor)
	{
		return;
	}
	const FTransformTrajectory Trajectory = GravityPredictor->GetLastTrajectory();
	const FVCTrajectoryPredictionState& State = GravityPredictor->GetState();
	const FQuat MeshToActor = State.MeshRelativeRotation.Inverse();
	for (int32 Index = 0; Index < Trajectory.Samples.Num(); ++Index)
	{
		const FTransformTrajectorySample& Sample = Trajectory.Samples[Index];
		const FColor Colour = Sample.TimeInSeconds > 0.f ? FColor::Cyan : FColor::Orange;
		const FQuat ActorFacing = Sample.Facing * MeshToActor;
		DrawDebugSphere(World, Sample.Position, 4.f, 6, Colour, false, -1.f, SDPG_Foreground);
		DrawDebugLine(World, Sample.Position, Sample.Position + ActorFacing.GetForwardVector() * 25.f, Colour, false, -1.f, SDPG_Foreground, 1.5f);
		if (Index > 0)
		{
			DrawDebugLine(World, Trajectory.Samples[Index - 1].Position, Sample.Position, Colour, false, -1.f, SDPG_Foreground, 1.f);
		}
	}
	DrawDebugLine(World, State.Position, State.Position + State.Up * 60.f, FColor::Green, false, -1.f, SDPG_Foreground, 2.f);
	DrawDebugString(World, State.Position + State.Up * 210.f, GetMotionMatchingSelection(), nullptr, FColor::Cyan, 0.f, true);
}
