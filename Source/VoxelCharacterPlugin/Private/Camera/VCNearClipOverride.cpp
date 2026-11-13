// Copyright Daniel Raquel. All Rights Reserved.

#include "Camera/VCNearClipOverride.h"
#include "RenderCore.h" // SetNearClipPlaneGlobals: the game thread's plane and the render thread's copy together

namespace
{
	/** Holders now. */
	int32 GVCNearClipHolders = 0;
	/** The plane the first holder found: what the last one puts back. */
	float GVCNearClipFound = 0.f;
	/** The plane as the holders left it: a different value at a release or a later hold is somebody else's. */
	float GVCNearClipHeld = 0.f;
}

void FVCNearClipOverride::Acquire(float MaxNearPlane)
{
	check(IsInGameThread());

	// The first holder, or a plane something else set since the last hold: that is the value to put back.
	if (GVCNearClipHolders == 0 || GNearClippingPlane != GVCNearClipHeld)
	{
		GVCNearClipFound = GNearClippingPlane;
		GVCNearClipHeld = GNearClippingPlane;
	}
	++GVCNearClipHolders;

	if (MaxNearPlane > 0.f && GNearClippingPlane > MaxNearPlane)
	{
		GVCNearClipHeld = MaxNearPlane;
		SetNearClipPlaneGlobals(MaxNearPlane);
	}
}

void FVCNearClipOverride::Release()
{
	check(IsInGameThread());

	if (GVCNearClipHolders <= 0)
	{
		return;
	}
	if (--GVCNearClipHolders == 0 && GNearClippingPlane == GVCNearClipHeld && GVCNearClipHeld != GVCNearClipFound)
	{
		SetNearClipPlaneGlobals(GVCNearClipFound);
	}
}

int32 FVCNearClipOverride::NumHolders()
{
	return GVCNearClipHolders;
}
