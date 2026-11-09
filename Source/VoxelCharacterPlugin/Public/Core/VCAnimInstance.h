// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "GameplayTagContainer.h"
#include "Core/VCTypes.h"
#include "VCAnimInstance.generated.h"

class UAnimSequenceBase;
class UBlendSpace;
class UVCActionMontageComponent;
class UVCAppearanceComponent;
class UVCLocomotionAnimSet;

/**
 * What UVCAnimInstance reads from its character on the game thread, every vector in world space. The rest of the update
 * works from this alone, on any thread (character animation P2).
 */
struct FVCAnimInput
{
	/** Seconds since the last update. */
	float DeltaSeconds = 0.f;

	/** Unit gravity direction: world -Z without custom gravity. */
	FVector GravityDirection = FVector::DownVector;

	/** Gravity's magnitude (uu/s^2, positive). */
	float GravityAcceleration = 980.f;

	FVector Velocity = FVector::ZeroVector;

	/** The movement component's input acceleration (GetCurrentAcceleration). */
	FVector Acceleration = FVector::ZeroVector;

	float MaxAcceleration = 0.f;
	float MaxBrakingDeceleration = 0.f;

	FQuat ActorRotation = FQuat::Identity;

	/** Where the character looks: the controller's rotation, else the replicated base aim rotation. */
	FRotator ControlRotation = FRotator::ZeroRotator;

	/** Walking on a floor (IsMovingOnGround); false while falling, swimming, flying or held (MOVE_None). */
	bool bOnGround = false;

	bool bFalling = false;
	bool bCrouching = false;
	bool bSwimming = false;
	bool bSprinting = false;

	/** The walking floor's surface normal; zero when there is no walkable floor. */
	FVector FloorNormal = FVector::ZeroVector;

	/** Distance from the capsule's bottom to the ground along gravity while falling: 0 on the ground, -1 when nothing is in range. */
	float GroundDistance = 0.f;

	EVCSurfaceType SurfaceType = EVCSurfaceType::Default;
	float WaterDepth = 0.f;

	/** How far the capsule is under water, 0 to 1 (UCharacterMovementComponent::ImmersionDepth). */
	float Immersion = 0.f;

	EVCViewMode ViewMode = EVCViewMode::ThirdPerson;
	FGameplayTag ActionSet;

	/** The body's own locomotion (IVCAnimatedCharacter::GetAnimLocomotionSet: an animal's), or null. A data asset. */
	const UVCLocomotionAnimSet* LocomotionSet = nullptr;

	bool bDead = false;
	bool bDowned = false;

	/** Debug flight (MOVE_Flying): the feet stand on nothing. */
	bool bFlying = false;

	/** Where the character looks from: its eyes (GetPawnViewLocation). */
	FVector EyeLocation = FVector::ZeroVector;

	/** Where it looks, when bHasLookTarget: a player's view a LookAtDistance ahead, an AI controller's focus. */
	FVector LookTarget = FVector::ZeroVector;
	bool bHasLookTarget = false;

	/** The held action set's off-hand grip (UVCActionAnimSet::bOffHandIK, OffHandGrip): the off wrist in hand_r's space. */
	bool bOffHandIK = false;
	FVector OffHandGrip = FVector::ZeroVector;

	/**
	 * The worn body has no ik_foot bones (UVCAppearanceComponent::HasIKFootBones false: a MetaHuman's), so its feet plant
	 * through the foot bones (FVCAnimNode_FootBoneIK).
	 */
	bool bFootBonesOnly = false;

	/** The IK and pace switches (vc.Anim.FootIK, SprintPace, LookAt, HandIK), read on the game thread. */
	bool bFootIKEnabled = true;
	bool bSprintPaceEnabled = true;
	bool bLookAtEnabled = true;
	bool bHandIKEnabled = true;

	/**
	 * vc.Anim.Stance: 0 no held stance, 1 the static idle over the upper body (as before C2), 2 the moving stance, faded
	 * by speed where the layer asks (D85), 3 the moving stance at every gait (C2, before D85).
	 */
	int32 StanceMode = 2;

	/** The character aims a ranged weapon (IVCAnimatedCharacter::IsAnimAiming): the bow raised. */
	bool bAiming = false;
};

/**
 * The anim instance of every character: the player (AVCCharacterBase) and the NPCs (AVCNPCCharacterBase), or any
 * ACharacter whose movement component is a UCharacterMovementComponent (UVCMovementComponent adds the surface and the
 * sprint). What is not movement comes through IVCAnimatedCharacter.
 *
 * Every value is measured in the character's gravity frame: local up is minus the gravity direction, so a value is the
 * same walking on a plane and on any part of a planet. Nothing gravity-relative is kept across updates (the frame's yaw
 * twists near the south pole); the state that is kept is world vectors, re-projected each update.
 *
 * NativeUpdateAnimation gathers FVCAnimInput on the game thread; NativeThreadSafeUpdateAnimation computes the values
 * from it (ComputeValues). The anim graph reads the BlueprintReadOnly properties.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual void NativeThreadSafeUpdateAnimation(float DeltaSeconds) override;

	/**
	 * Computes every value from an input and the state kept from the last call. Touches only this instance, so it runs on
	 * an animation worker thread; the tests call it directly.
	 * @param Input What the character's state was this update
	 */
	void ComputeValues(const FVCAnimInput& Input);

	/** Forgets the state kept across updates (the body heading, the last facing): the next update starts afresh. */
	void ResetKeptState();

	/**
	 * Pure: the walk/run clips' play rate for a speed (character animation P4): the locomotion's top authored speed plays
	 * its clips as authored; a faster pawn (a sprint) takes quicker steps, so its feet keep pace with the ground. The steps
	 * quicken rather than lengthen: a jog's legs are near full reach at each end of the stride.
	 * @param InSpeed        The speed across local up (uu/s)
	 * @param ReferenceSpeed The locomotion's top authored speed (uu/s)
	 * @return 1 up to ReferenceSpeed (or a reference of 0), else InSpeed / ReferenceSpeed
	 */
	static float ComputeLocomotionPlayRate(float InSpeed, float ReferenceSpeed);

	// ==================== Locomotion ====================

	/** Speed across local up (uu/s). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float Speed = 0.f;

	/** The horizontal velocity's angle from the facing (degrees, positive to the right): the strafe direction. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float Direction = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	bool bIsFalling = false;

	/** True for one update when the character goes from falling to grounded. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	bool bJustLanded = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	bool bIsCrouching = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	bool bIsAccelerating = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	bool bIsSprinting = false;

	/** Velocity in the facing's frame: X forward, Y right, Z up (uu/s). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	FVector LocalVelocity = FVector::ZeroVector;

	/** Input acceleration in the facing's frame: X forward, Y right, Z up (uu/s^2). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	FVector LocalAcceleration = FVector::ZeroVector;

	/**
	 * The lean input: LocalAcceleration over the maximum acceleration while speeding up, over the braking deceleration
	 * while slowing down, each axis -1 to 1 (Lyra's and GASP's relative acceleration).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	FVector RelativeAcceleration = FVector::ZeroVector;

	/** Sideways lean, -1 (left) to 1 (right): RelativeAcceleration.Y. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float LeanAmount = 0.f;

	/** How fast the facing turns about local up (degrees per second, positive to the right). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float YawRate = 0.f;

	// ==================== Ground ====================

	/** The floor's rise ahead of the facing (degrees, positive uphill); 0 off the ground. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float SlopePitch = 0.f;

	/** The floor's rise to the facing's right (degrees, positive when the right side is higher); 0 off the ground. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float SlopeRoll = 0.f;

	/** Distance from the feet to the ground along gravity (uu): 0 on the ground, -1 falling with nothing in range. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float GroundDistance = 0.f;

	/** Seconds until a falling character lands at its current velocity and gravity; -1 when not falling or nothing is in range. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float TimeToLand = -1.f;

	// ==================== Turn in place ====================

	/**
	 * The body's heading from the facing (degrees, positive to the right): while the character stands on the ground, the
	 * body keeps its heading as the facing turns, up to TurnInPlaceMaxOffset; once it moves, or leaves the ground, the
	 * offset returns to 0. A turn past TurnInPlaceMaxOffset in one update is a rotation set by code (a teleport, a
	 * restored save) and seats the body on the facing. The graph turns the root by it and plays a turn when it grows
	 * (Lyra's root yaw offset).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float RootYawOffset = 0.f;

	// ==================== View Mode ====================

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	EVCViewMode ViewMode = EVCViewMode::ThirdPerson;

	// ==================== Aim ====================

	/** The look direction's pitch above the facing (degrees, -90 to 90). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float AimPitch = 0.f;

	/** The look direction's yaw from the facing (degrees, -90 to 90, positive to the right). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float AimYaw = 0.f;

	// ==================== Equipment ====================

	/** The action set of what the character holds (Equipment.AnimSet.*): which layer and montages animate it. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	FGameplayTag ActionSet;

	// ==================== Combat ====================

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	bool bIsDead = false;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	bool bIsDowned = false;

	// ==================== Surface ====================

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	EVCSurfaceType SurfaceType = EVCSurfaceType::Default;

	// ==================== Swimming ====================

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	bool bIsSwimming = false;

	/** Swimming with the whole capsule under the water (a dive); false while swimming at the surface. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	bool bIsUnderwater = false;

	/** Depth below water surface (world units). 0 when not underwater. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float WaterDepth = 0.f;

	// ==================== IK and pace (character animation P4) ====================

	/**
	 * How far foot placement and the leg IK apply (0 to 1): 1 on the ground, 0 while falling, swimming, flying, dead or
	 * downed, eased between at IKBlendSpeed (vc.Anim.FootIK).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float FootIKAlpha = 0.f;

	/**
	 * How far the foot-bone placement (FVCAnimNode_FootBoneIK) applies: FootIKAlpha on a body without ik_foot bones (a
	 * MetaHuman's), else 0, so no body is placed twice (character animation P6).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float FootBoneIKAlpha = 0.f;

	/**
	 * How far a held item's stance layer lays its upper body over the locomotion (the character refinement plan's C2, D72):
	 * 1 on the ground, alive and not downed, not swimming or flying; StanceSprintWeight while sprinting (the sprint's arms
	 * show through); 0 otherwise and with vc.Anim.Stance 0; eased at IKBlendSpeed. The layer copies it into its blend's weight.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float StanceWeight = 0.f;

	/** vc.Anim.Stance as of the last update (the layer plays its stance blend space at speed 0 under mode 1, fades it under 2). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	int32 StanceMode = 2;

	/**
	 * How far the aim raises a held ranged weapon (0 to 1): toward 1 while the character aims (the bow up, the string
	 * undrawn; a draw then starts from there), 0 otherwise and dead or downed, moving at 1 / AimRaiseTime a second. A layer
	 * with a raised pose blends it in by this (ABP_VCLayer_Bow).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float AimRaiseWeight = 0.f;

	// ==================== A body's own locomotion (character animation P7) ====================

	/** The body's locomotion blend space by speed (UVCLocomotionAnimSet::Locomotion), or null: the creature template plays it. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UBlendSpace> LocomotionBlendSpace;

	/** What the body does now and then while it stands (UVCLocomotionAnimSet::IdleVariant: a deer grazes), or null. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimSequenceBase> IdleVariantAnim;

	/**
	 * Whether the idle variant plays now: standing on the ground, alive, after the set's IdleVariantAfter seconds, for its
	 * IdleVariantFor seconds, and again after the same wait.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	bool bIdleVariant = false;

	/** The walk/run clips' play rate: ComputeLocomotionPlayRate on the ground, 1 otherwise (vc.Anim.SprintPace). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float LocomotionPlayRate = 1.f;

	/** Where the head looks (world): the player's view LookAtDistance ahead, an NPC's focus; kept when there is none. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	FVector LookAtLocation = FVector::ZeroVector;

	/**
	 * How far the head turns to LookAtLocation (0 to 1): 0 with no target, in first person (the head is hidden), dead or
	 * downed, or with the target more than LookAtMaxAngle from the facing (vc.Anim.LookAt).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float LookAtAlpha = 0.f;

	/** How far the off hand's IK applies (0 to 1): 1 while the held set grips with both hands (vc.Anim.HandIK). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	float HandIKAlpha = 0.f;

	/** Where the off hand's wrist goes, in hand_r's space: the held set's OffHandGrip. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	FVector HandIKEffector = FVector::ZeroVector;

	// ==================== Tuning ====================

	/** The largest turn-in-place offset (degrees): past it the body is dragged round with the facing. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0", ClampMax = "180"))
	float TurnInPlaceMaxOffset = 90.f;

	/** How fast the turn-in-place offset returns to 0 once the character moves (FInterpTo speed). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0"))
	float TurnInPlaceRecoverySpeed = 10.f;

	/** Speed across local up below which the character counts as standing (uu/s). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0"))
	float MovingSpeedThreshold = 10.f;

	/** How far below a falling character the ground is looked for (uu). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0"))
	float MaxGroundTraceDistance = 1000.f;

	/** The immersion (0 to 1) at which a swimmer counts as under water. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0", ClampMax = "1"))
	float UnderwaterImmersion = 0.95f;

	/** How fast the IK alphas move toward on or off (per second): 8 turns one fully in an eighth of a second. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0"))
	float IKBlendSpeed = 8.f;

	/** Seconds the aim takes to raise a held ranged weapon fully, and to lower it (AimRaiseWeight). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0.01"))
	float AimRaiseTime = 0.3f;

	/** StanceWeight while sprinting: how much of the held stance stays over the sprint's arms (C2). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0", ClampMax = "1"))
	float StanceSprintWeight = 0.35f;

	/** The locomotion's top authored speed (uu/s): the walk/run blend space's jog, above which the steps quicken. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0"))
	float LocomotionReferenceSpeed = 600.f;

	/** How far ahead along the view a player's head looks (uu). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0"))
	float LookAtDistance = 1000.f;

	/** The look-at fades out for a target more than this from the facing (degrees across local up). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Animation|Tuning", meta = (ClampMin = "0", ClampMax = "180"))
	float LookAtMaxAngle = 110.f;

protected:
	/** @return The input NativeUpdateAnimation gathered this update; null when there was no character to read */
	const FVCAnimInput* GetGatheredInput() const { return bHasGatheredInput ? &GatheredInput : nullptr; }

private:
	/** Fills Input from the owning character (game thread). @return False when there is no character to read */
	bool GatherInput(float DeltaSeconds, FVCAnimInput& OutInput) const;

	/** The input gathered this update, for NativeThreadSafeUpdateAnimation. */
	FVCAnimInput GatheredInput;
	bool bHasGatheredInput = false;

	/** The facing at the last update, in world space (for YawRate). */
	FVector LastFacing = FVector::ZeroVector;

	/** The body's heading in world space (for RootYawOffset), re-projected onto the local horizontal each update. */
	FVector BodyHeading = FVector::ZeroVector;

	bool bHasKeptState = false;

	/** Seconds the body has stood still on the ground (the idle variant's clock). */
	float StandingSeconds = 0.f;

	/** The character's montage component (its held action set), found once. */
	mutable TWeakObjectPtr<const UVCActionMontageComponent> CachedMontages;

	/** The character's appearance component (whether its body has ik_foot bones), found once. */
	mutable TWeakObjectPtr<const UVCAppearanceComponent> CachedAppearance;

	/** vc.Anim.Debug: the look-at target and the IK values drawn over the character (game thread). */
	void DrawDebug() const;
};
