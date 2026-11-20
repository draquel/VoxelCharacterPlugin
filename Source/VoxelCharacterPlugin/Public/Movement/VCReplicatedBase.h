// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ACharacter;
struct FBasedMovementInfo;

/**
 * A character's replicated movement base, kept to what a client can resolve (G221).
 *
 * The engine sends a simulated proxy the component its character stands on (`ACharacter::ReplicatedBasedMovement`,
 * filled in `PreReplication`). The voxel terrain's collision is cooked by each machine for itself (VoxelWorlds'
 * `UVoxelCollisionManager`, an unreplicated container), so its components have no network id: the server logged
 * `FNetGUIDCache::SupportsObject: ... NOT Supported` for every such reference it tried to send, and the client, told
 * the server had a base it could not find, skipped simulating the character between updates. A chunk's collision is
 * static (G215), so no position is relative to it: the base carries nothing a client needs. Both character classes
 * (`AVCCharacterBase`, `AVCNPCCharacterBase`) clear such a base from what they replicate after the engine fills it,
 * and `UVCMovementComponent`'s move response leaves it out of a correction of a player's move, which the engine fills
 * with the server's base whether or not the position is relative to it.
 */
struct VOXELCHARACTERPLUGIN_API FVCReplicatedBase
{
	/**
	 * Whether a client can resolve a reference to an object: the test the net driver makes before it sends one
	 * (`FNetGUIDCache::SupportsObject`): a name stable for networking, or an object the server replicates.
	 * @param Object The object a replicated property names
	 * @return True for null, and for an object a client can resolve
	 */
	static bool IsResolvableByClients(const UObject* Object);

	/**
	 * Clears a replicated base no client can resolve: no base, no bone, no relative rotation, and the server no longer
	 * says it has one, so a client simulates the character on its own floor as on any base it resolved to nothing. The
	 * location and the rotation are kept: with no base a client reads its position from the character's replicated
	 * movement, which always holds the world position, as it did from the base it could not resolve.
	 * @param Info A character's replicated based movement, as `ACharacter::PreReplication` filled it
	 * @return True when the base was cleared
	 */
	static bool ClearUnresolvableBase(FBasedMovementInfo& Info);

	/**
	 * What a character calls in `PreReplication`, after the engine's: ClearUnresolvableBase unless
	 * `vc.Net.ClearUnresolvableBase` is 0 (the engine's behaviour, the comparison). A base no client can resolve is
	 * logged at Verbose (LogVoxelCharacter) with the character that stands on it, cleared or not.
	 * @param Character The character replicating
	 * @param Info      The character's replicated based movement
	 * @return True when the base was cleared
	 */
	static bool ApplyBeforeReplication(const ACharacter& Character, FBasedMovementInfo& Info);

	/** @return False when `vc.Net.ClearUnresolvableBase` is 0: the engine's replicated base and corrections, the comparison */
	static bool IsEnabled();
};
