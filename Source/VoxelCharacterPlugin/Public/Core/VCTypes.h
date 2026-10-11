// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Types/CGFCombatTypes.h"
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

/** What a combatant does when Health reaches zero (UVCCombatComponent::OutOfHealthPolicy). */
UENUM(BlueprintType)
enum class EVCOutOfHealthPolicy : uint8
{
	/** Die immediately: State.Dead, OnDied, no recovery short of Revive/respawn. */
	Die,
	/** Knocked out: State.Downed, OnDowned; recoverable with Revive, promoted to death by DownedTimeout. */
	Downed,
};

/** Where AVCGameModeBase puts a player back after death. */
UENUM(BlueprintType)
enum class EVCRespawnPolicy : uint8
{
	/** Respawn where the player died (short test loops; the default for now). */
	AtDeathLocation,
	/** Respawn at a player start chosen by the game mode. */
	AtPlayerStart,
	/** Respawn at the last rest point (campsite) the player rested at; the death location until there is one (feature 8). */
	AtLastRest,
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

/** One inventory slot captured when a player dies, as the storage subsystem's JSON so it carries instance fragments. */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCSnapshotInventoryEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Items")
	int32 SlotIndex = -1;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Items")
	FString ItemJson;
};

/** One equipped item captured when a player dies. */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCSnapshotEquipmentEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Items")
	FGameplayTag SlotTag;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Items")
	FString ItemJson;
};

/**
 * Everything a player was carrying, captured on the player state when the avatar dies and
 * restored onto the next avatar. Pawn-owned inventory/equipment components die with the pawn;
 * this keeps the player's items across the respawn. Same serialization as the storage
 * subsystem, so a save system can persist it as-is.
 */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCItemSnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Items")
	TArray<FVCSnapshotInventoryEntry> Inventory;

	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Items")
	TArray<FVCSnapshotEquipmentEntry> Equipment;

	bool IsEmpty() const { return Inventory.Num() == 0 && Equipment.Num() == 0; }
	void Reset() { Inventory.Reset(); Equipment.Reset(); }
};

/**
 * Player progression counters (feature 4: dungeon objectives). Replicated on AVCPlayerState so
 * they survive death / respawn and reach the owning client for HUD toasts. Plain data so a save
 * system can persist it as-is; XP / levels are deliberately not here (not on the roadmap).
 */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCProgressionStats
{
	GENERATED_BODY()

	/** Dungeons whose objective this player completed. */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Progression")
	int32 DungeonsCleared = 0;

	/** Dungeon bosses this player killed (the killing blow). */
	UPROPERTY(BlueprintReadOnly, Category = "VoxelCharacter|Progression")
	int32 BossesKilled = 0;
};

// ---------------------------------------------------------------------------
// Delegates
// ---------------------------------------------------------------------------

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVCViewModeChanged, EVCViewMode, OldMode, EVCViewMode, NewMode);

/** Health attribute changed on a combatant (fires on server and on every client via attribute replication). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnVCHealthChanged, float, NewHealth, float, OldHealth, float, MaxHealth);

/** A combatant died or was knocked out. Context is the killing hit on the server; empty on clients. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVCCombatantStateChanged, const FCGFDamageContext&, Context);

/** A combatant was revived from Dead/Downed (server only). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnVCCombatantRevived);

/** Voxel edit mode (dig / place on the mouse actions) was turned on or off. Local, never replicated. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVCEditModeChanged, bool, bEnabled);

/**
 * Everything a save system keeps per player (feature 9): the items the avatar carried (JSON per
 * slot, as the death snapshot), progression, the rest point, where the avatar stood and its vitals.
 * Produced / consumed by AVCPlayerState::ExportSaveState / ImportSaveState.
 */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCPlayerSaveState
{
	GENERATED_BODY()

	/** AVCPlayerState::GetSaveKey (unique net id, or Local0). */
	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|Save")
	FString PlayerKey;

	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|Save")
	TArray<FVCSnapshotInventoryEntry> Inventory;

	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|Save")
	TArray<FVCSnapshotEquipmentEntry> Equipment;

	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|Save")
	FVCProgressionStats Progression;

	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|Save")
	bool bHasRespawnPoint = false;

	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|Save")
	FTransform RespawnPoint;

	/** Where the avatar stood when saved (the next login spawns there). */
	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|Save")
	bool bHasLastTransform = false;

	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|Save")
	FTransform LastTransform;

	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|Save")
	bool bHasVitals = false;

	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|Save")
	float Health = 0.f;

	UPROPERTY(BlueprintReadWrite, Category = "VoxelCharacter|Save")
	float Stamina = 0.f;
};

/** The carried light changed (feature 7): lit / dark, fuel seconds left and capacity (0 / 0 when it needs no fuel). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnVCCarriedLightChanged, bool, bLit, float, FuelSeconds, float, MaxFuel);

/** The player state's progression counters changed (server on write, clients on replication). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnVCProgressionChanged, const FVCProgressionStats&, Stats);
