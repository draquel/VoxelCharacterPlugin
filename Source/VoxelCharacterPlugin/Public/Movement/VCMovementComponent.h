// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Core/VCTypes.h"
#include "VCMovementComponent.generated.h"

struct FGameplayAttributeData;
struct FOnAttributeChangeData;
enum class EEditSource : uint8;

/**
 * Extended CharacterMovementComponent with voxel terrain awareness.
 *
 * Caches terrain context (surface type, friction, water state) and adjusts
 * movement parameters accordingly.  Voxel queries are stubbed until Gate 5
 * wires them to VoxelWorlds.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UVCMovementComponent();

	// --- Voxel-Aware Movement ---

	/** Re-query voxel terrain data beneath the character. */
	void UpdateVoxelTerrainContext();

	/** Current cached terrain context (read by animation, audio, etc.). */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Movement")
	const FVoxelTerrainContext& GetTerrainContext() const { return CachedTerrainContext; }

	/** Map a voxel MaterialID to a logical surface type. */
	static EVoxelSurfaceType MaterialIDToSurfaceType(uint8 MaterialID);

	/** Get friction multiplier for a given surface type. */
	static float GetSurfaceFriction(EVoxelSurfaceType Surface);

	// --- Custom Movement Mode Properties ---

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Voxel")
	float VoxelClimbingSpeed = 200.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Voxel")
	float VoxelSwimmingSpeedMultiplier = 0.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Movement|Voxel")
	float VoxelSurfaceGripMultiplier = 1.0f;

	/** Current logical surface type (derived from voxel material). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Movement|Voxel")
	EVoxelSurfaceType CurrentSurfaceType = EVoxelSurfaceType::Default;

	// --- GAS Attribute Callbacks ---

	/** Called when MoveSpeedMultiplier attribute changes. */
	void OnMoveSpeedAttributeChanged(const FOnAttributeChangeData& Data);

	// --- Overrides ---
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
	virtual void FindFloor(const FVector& CapsuleLocation, FFindFloorResult& OutFloorResult, bool bCanUseCachedLocation, const FHitResult* DownwardSweepResult = nullptr) const override;

protected:
	/** Cached terrain data, refreshed every TerrainCacheDuration seconds. */
	FVoxelTerrainContext CachedTerrainContext;

	/** Time accumulator for terrain cache refresh. */
	float TerrainContextCacheTimer = 0.f;

	/** How often to re-query voxel terrain (seconds). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Movement|Voxel")
	float TerrainCacheDuration = 0.1f;

	/** Base MaxWalkSpeed before GAS multiplier. Captured on BeginPlay. */
	float BaseMaxWalkSpeed = 0.f;

	/** Current GAS speed multiplier (default 1.0). */
	float GASSpeedMultiplier = 1.f;

	/** Was the character grounded last frame? Used for async mesh rebuild tolerance. */
	bool bWasGroundedLastFrame = false;

	/** Grace period remaining when floor temporarily disappears during async mesh rebuild. */
	mutable float FloorGraceTimer = 0.f;

	/** Max grace period (seconds) to maintain grounded state during async mesh rebuilds. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Movement|Voxel")
	float FloorGraceDuration = 0.15f;

	virtual void BeginPlay() override;

	/** Delegate handler for chunk modification (invalidates terrain cache). */
	void OnVoxelChunkModified(const FIntVector& ChunkCoord, EEditSource Source, const FVector& EditCenter, float EditRadius);
};
