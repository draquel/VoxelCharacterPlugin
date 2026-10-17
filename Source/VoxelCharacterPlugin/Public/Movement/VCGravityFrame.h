// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UCharacterMovementComponent;

/**
 * Conversions between world space and a character's gravity frame, the frame whose +Z is local up (minus the movement
 * component's gravity direction): what camera, input, facing and animation code reads on a planet (VoxelWorlds
 * SPHERICAL_PLANET_REQUIREMENTS.md SP-F3; WORLD_MODES_REVIEW_PLAN.md §17, WM-W3).
 *
 * The frame's rotation is the engine's own (UCharacterMovementComponent::GetWorldToGravityTransform: the shortest
 * rotation taking +Z to local up), so it agrees with the movement component's gravity space. That rotation spins fast
 * near the antipode of +Z (a planet's south pole), so callers only do frame-independent things in it: zero a pitch or a
 * roll, take a yaw-only heading, clamp a pitch, and convert straight back.
 *
 * Every movement-component overload returns its input unchanged while the component has no custom gravity (the
 * engine's default, world -Z: every heightmap world), so heightmap code keeps its old arithmetic exactly.
 */
struct VOXELCHARACTERPLUGIN_API FVCGravityFrame
{
	/**
	 * The rotation from the gravity frame to world space for a gravity direction (the engine's WorldToGravityTransform).
	 * @param GravityDirection Unit gravity direction
	 * @return Rotation taking +Z to -GravityDirection
	 */
	static FQuat GetGravityToWorld(const FVector& GravityDirection);

	/**
	 * A world rotation in the gravity frame.
	 * @param GravityDirection Unit gravity direction
	 * @param WorldRotation World rotation
	 * @return The rotation relative to the gravity frame (its pitch measured from the local horizontal)
	 */
	static FRotator ToGravityRelative(const FVector& GravityDirection, const FRotator& WorldRotation);

	/**
	 * A gravity-relative rotation in world space (inverse of ToGravityRelative).
	 * @param GravityDirection Unit gravity direction
	 * @param GravityRotation Rotation relative to the gravity frame
	 * @return World rotation
	 */
	static FRotator ToWorld(const FVector& GravityDirection, const FRotator& GravityRotation);

	/**
	 * A world rotation reduced to its gravity-relative yaw: the heading in the local horizontal, upright.
	 * @param GravityDirection Unit gravity direction
	 * @param WorldRotation World rotation (a control rotation)
	 * @return World rotation with zero gravity-relative pitch and roll
	 */
	static FRotator GetYawRotation(const FVector& GravityDirection, const FRotator& WorldRotation);

	/** ToGravityRelative for a movement component; the input itself without custom gravity. */
	static FRotator ToGravityRelative(const UCharacterMovementComponent* Movement, const FRotator& WorldRotation);

	/** ToWorld for a movement component; the input itself without custom gravity. */
	static FRotator ToWorld(const UCharacterMovementComponent* Movement, const FRotator& GravityRotation);

	/** GetYawRotation for a movement component; FRotator(0, Yaw, 0) without custom gravity (the old heading). */
	static FRotator GetYawRotation(const UCharacterMovementComponent* Movement, const FRotator& WorldRotation);

	/**
	 * A vector given in the gravity frame (+Z local up), in world space; the vector itself without custom gravity.
	 * @param Movement The character's movement component (null = no custom gravity)
	 * @param GravityVector Vector in the gravity frame
	 * @return World vector
	 */
	static FVector VectorToWorld(const UCharacterMovementComponent* Movement, const FVector& GravityVector);

	/**
	 * Local up: minus the gravity direction ((0, 0, 1) for the default gravity).
	 * @param Movement The character's movement component (null = +Z)
	 * @return Unit up vector
	 */
	static FVector GetUp(const UCharacterMovementComponent* Movement);

	/**
	 * Speed across local up (a walker's ground speed): Velocity.Size2D() for the default gravity.
	 * @param Movement The character's movement component (null = default gravity)
	 * @param Velocity World velocity
	 * @return Speed in the local horizontal
	 */
	static double GetHorizontalSpeed(const UCharacterMovementComponent* Movement, const FVector& Velocity);

	// --- The local horizontal (spherical Phase 4, WM-SY12: NPC steering) ---

	/**
	 * A world vector's part across local up: FVector(X, Y, 0) without custom gravity.
	 * @param Movement The character's movement component (null = default gravity)
	 * @param WorldVector World vector
	 * @return Its horizontal part
	 */
	static FVector Horizontal(const UCharacterMovementComponent* Movement, const FVector& WorldVector);

	/**
	 * The unit direction of a world vector's horizontal part (zero when it lies along up): WorldVector.GetSafeNormal2D()
	 * without custom gravity.
	 */
	static FVector HorizontalDirection(const UCharacterMovementComponent* Movement, const FVector& WorldVector);

	/** The distance between two points across local up: FVector::Dist2D without custom gravity. */
	static double HorizontalDistance(const UCharacterMovementComponent* Movement, const FVector& A, const FVector& B);

	/** A world vector's component along local up: its Z without custom gravity. */
	static double UpComponent(const UCharacterMovementComponent* Movement, const FVector& WorldVector);

	/**
	 * An upright rotation facing along a world direction's horizontal part: FRotator(0, Direction.Rotation().Yaw, 0)
	 * without custom gravity, the heading in the gravity frame on a planet.
	 */
	static FRotator FacingRotation(const UCharacterMovementComponent* Movement, const FVector& WorldDirection);

	/**
	 * The unit horizontal direction at an angle: (cos, sin, 0) without custom gravity, the gravity frame's X and Y axes in
	 * world space on a planet.
	 * @param Movement The character's movement component (null = default gravity)
	 * @param AngleRadians Angle from the frame's X axis toward its Y axis
	 * @return Unit world direction across local up
	 */
	static FVector HorizontalAtAngle(const UCharacterMovementComponent* Movement, float AngleRadians);
};
