// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UPrimitiveComponent;
class USkeletalMeshComponent;

/** What a ragdoll's buoyancy remembers between ticks (one per body, on each machine). */
struct FVCRagdollBuoyancyState
{
	/** The last tick found the body in water */
	bool bInWater = false;

	/** The root's depth under the surface at the last tick (meaningful while bInWater) */
	double RootDepth = 0.0;

	/** Where the body first met the water: the column it is held near, so the machines' ragdolls rest together */
	bool bAnchored = false;
	FVector Anchor = FVector::ZeroVector;

	/**
	 * The body's meshes this rule turned custom depth on for while it is in the water (as a swimmer's are,
	 * UVCWaterEffectsComponent): the water material draws no shore foam over a body. They stay on when the corpse
	 * freezes at the surface, and go off if the body leaves the water.
	 */
	TArray<TWeakObjectPtr<UPrimitiveComponent>> CustomDepthMeshes;
};

/**
 * The floating rule for a dead body (the water plan's W2B, D130): a ragdoll in water rises to the surface and rests
 * awash there instead of falling through the water to the bed.
 *
 * Each machine simulates its own ragdoll (nothing of it replicates), so the rule runs on each: the water is read
 * through the water levels' sample at the body, per tick, and the surface is the same on every machine. Each part
 * under the surface gets an upward acceleration that grows with its depth to more than gravity's, the water's drag,
 * and a pull toward the column where the body first met the water, so the copies come to rest within a body's length
 * of one another. Nothing floats where the sample says there is no water (a dry cave below the sea's level).
 */
struct VOXELCHARACTERPLUGIN_API FVCRagdollBuoyancy
{
	/** A fully submerged part's upward acceleration, in gravities (1 = it hangs, more = it rises) */
	float Buoyancy = 1.6f;

	/** The depth over which a part goes from dry to fully buoyant (world units) */
	float SubmergeDepth = 40.0f;

	/** The water's drag on a part's velocity (per second) */
	float LinearDrag = 2.0f;

	/** The water's drag on a part's spin (per second) */
	float AngularDrag = 1.5f;

	/** The pull toward the anchor column per unit of sideways offset (per second squared), and its cap */
	float AnchorPull = 4.0f;
	float MaxAnchorAcceleration = 300.0f;

	/** How far above the surface the body's root may be while parts of it are still in the water (world units) */
	float ReachAbove = 150.0f;

	/** The rule every body uses. */
	static const FVCRagdollBuoyancy& Get();

	/** Whether bodies float at all (the cvar vc.Buoyancy.Bodies; on by default). */
	static bool IsEnabled();

	/**
	 * The acceleration the water gives one part of a body (gravity is the simulation's own and is not in it).
	 *
	 * @param Depth The part's depth under the surface (positive below)
	 * @param Up The surface's up (unit)
	 * @param Velocity The part's velocity
	 * @param ToAnchor From the body's root to its anchor column, across the up (zero: no pull)
	 * @param Gravity The world's gravity (positive, world units per second squared)
	 * @return The acceleration; zero at or above the surface
	 */
	FVector ComputeAcceleration(double Depth, const FVector& Up, const FVector& Velocity, const FVector& ToAnchor, float Gravity) const;

	/** The depth at which a still part hangs: where the buoyancy equals gravity. */
	double GetRestDepth() const { return SubmergeDepth / FMath::Max(Buoyancy, 1.0e-3f); }

	/**
	 * One tick of a simulating ragdoll: sample the water at its root and push every part that is under the surface.
	 * Does nothing to a body that is not simulating, or where the sample says there is no water.
	 *
	 * @param Body The body's mesh
	 * @param State The body's state between ticks
	 * @return True when the body is in water
	 */
	bool Apply(USkeletalMeshComponent* Body, FVCRagdollBuoyancyState& State) const;

private:
	/** The lift itself (Apply adds the custom depth around it). */
	bool ApplyForces(USkeletalMeshComponent* Body, FVCRagdollBuoyancyState& State) const;

	/** Custom depth on the body's visible meshes (its own and its attached actors') while it is in the water. */
	static void SetCustomDepth(const USkeletalMeshComponent* Body, FVCRagdollBuoyancyState& State, bool bOn);
};
