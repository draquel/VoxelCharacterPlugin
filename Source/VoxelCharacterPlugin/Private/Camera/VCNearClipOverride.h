// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * The near clip plane lowered for a while and put back as it was found (G192).
 *
 * The engine has one near plane for the process (GNearClippingPlane, set at start from the project's
 * [/Script/Engine.Engine] NearClipPlane), not one per view. Whatever lowers it for a moment therefore puts back the
 * value it found, never a constant: the project's value is not the engine's default of 10 uu. Holders are counted,
 * since two local pawns (split screen, several PIE clients in one process) can need it at once: the first remembers
 * the plane it finds, the last puts it back. Game thread only.
 */
class FVCNearClipOverride
{
public:
	/**
	 * One more holder needs the near plane at MaxNearPlane or nearer. The first holder remembers the plane it finds;
	 * the plane is lowered only if it is farther than MaxNearPlane, so a project already at 1 uu is left alone.
	 * @param MaxNearPlane The farthest near plane the holder can use, in uu (0 or less: no need, the hold only counts)
	 */
	static void Acquire(float MaxNearPlane);

	/**
	 * One holder is done. The last one puts back the plane the first found, unless something else set the plane in
	 * between (r.SetNearClipPlane, a VR editor mode): that value is then the one to keep. A release with no holder
	 * does nothing.
	 */
	static void Release();

	/** @return How many holders there are now (0: the plane is the project's, or whatever was last set) */
	static int32 NumHolders();
};
