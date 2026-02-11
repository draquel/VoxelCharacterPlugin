// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "VCTypes.generated.h"

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------

/** Camera / view perspective mode. */
UENUM(BlueprintType)
enum class EVCViewMode : uint8
{
	FirstPerson,
	ThirdPerson,
};

/** Logical surface type derived from the voxel material beneath the character.
 *  Drives movement friction, footstep sounds, and animation. */
UENUM(BlueprintType)
enum class EVoxelSurfaceType : uint8
{
	Default,
	Stone,
	Dirt,
	Grass,
	Sand,
	Snow,
	Ice,
	Mud,
	Wood,
	Metal,
	Water,
};

/** Type of voxel modification requested through the server RPC. */
UENUM(BlueprintType)
enum class EVoxelModificationType : uint8
{
	Destroy,
	Place,
	Paint,
};

/** Animation archetype for the currently equipped item.
 *  Selects the upper-body animation layer in the AnimBP. */
UENUM(BlueprintType)
enum class EVCEquipmentAnimType : uint8
{
	Unarmed,
	OneHandMelee,
	TwoHandMelee,
	Pickaxe,
	Axe,
	Bow,
	Shield,
	Tool,
};

/** Interaction scanner tuning profile, switched per view mode. */
UENUM(BlueprintType)
enum class EVCInteractionScanProfile : uint8
{
	FirstPerson,
	ThirdPerson,
};

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------

/** Cached terrain data beneath / around the character.
 *  Populated by UVCMovementComponent, consumed by movement, animation, and audio. */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVoxelTerrainContext
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Terrain")
	EVoxelSurfaceType SurfaceType = EVoxelSurfaceType::Default;

	/** Raw voxel MaterialID at the character's feet. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Terrain")
	uint8 VoxelMaterialID = 0;

	/** Surface hardness — affects footstep audio volume / impact feel. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Terrain")
	float SurfaceHardness = 1.f;

	/** Ground friction multiplier derived from surface type. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Terrain")
	float FrictionMultiplier = 1.f;

	/** True when the character is below the voxel water level. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Terrain")
	bool bIsUnderwater = false;

	/** Depth below the water surface (0 when above water). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Terrain")
	float WaterDepth = 0.f;

	/** Chunk coordinate the character currently occupies (for event subscription). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Terrain")
	FIntVector CurrentChunkCoord = FIntVector::ZeroValue;
};

/** Maps an equipment slot tag to skeleton sockets on the TP body and FP arms meshes. */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCEquipmentSocketMapping
{
	GENERATED_BODY()

	/** The equipment slot this mapping applies to (e.g. Equipment.Slot.MainHand). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Equipment")
	FGameplayTag SlotTag;

	/** Socket name on the third-person body mesh. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Equipment")
	FName BodySocket;

	/** Socket name on the first-person arms mesh (NAME_None if not visible in FP). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Equipment")
	FName ArmsSocket;
};

// ---------------------------------------------------------------------------
// Delegates
// ---------------------------------------------------------------------------

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVCViewModeChanged, EVCViewMode, OldMode, EVCViewMode, NewMode);
