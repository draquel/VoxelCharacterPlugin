// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VoxelCoreTypes.h"
#include "VCAppearanceComponent.generated.h"

class AActor;
class UAnimInstance;
class UMaterialInterface;
class USkeletalMesh;
class USkeletalMeshComponent;
class UVCCharacterAppearance;

/**
 * Dresses a character in a UVCCharacterAppearance (character animation P6), on both character classes. The character's
 * Mesh stays the animated mesh: it takes the appearance's body mesh and materials, and the parts actor's other skeletal
 * meshes attach to it and follow it by leader pose (a MetaHuman's face, and an outfit part that simulates, run their own
 * AnimBP, which copies the pose from the animated mesh; the grooms ride on the face). On a dedicated server only the
 * body mesh changes. A null appearance gives the character its class's own body back.
 *
 * The outfit's simulated parts (the rigid-body nodes in the worn meshes' AnimBPs, a MetaHuman hoodie's strings, and their
 * cloth) fall along the movement's gravity, refreshed each tick while there are any: the engine's node and cloth take
 * world Z, a planet's gravity points to its centre.
 */
UCLASS(ClassGroup = (VoxelCharacter), meta = (BlueprintSpawnableComponent))
class VOXELCHARACTERPLUGIN_API UVCAppearanceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVCAppearanceComponent();

	/**
	 * Dresses the owning character's mesh, loading the appearance's assets synchronously.
	 * @param Appearance The appearance; null for the character class's own body
	 * @return True when the character wears it afterwards (false: no character mesh, or the parts actor did not spawn)
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Appearance")
	bool ApplyAppearance(const UVCCharacterAppearance* Appearance);

	/** @return The appearance worn; null for the class's own body */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Appearance")
	const UVCCharacterAppearance* GetAppearance() const { return Current; }

	/** @return The anim class the worn appearance names; null when it names none */
	UClass* GetAppearanceAnimClass() const;

	/** @return Whether the worn body has the UE5 Mannequin's ik_foot bones (the class's own body counts as having them) */
	bool HasIKFootBones() const;

	/**
	 * Hides the parts from the owning player as the mesh is hidden in first person.
	 * @param bOwnerNoSee       Hidden from the owner
	 * @param bCastHiddenShadow Their shadows still cast while hidden
	 */
	void SetPartsOwnerNoSee(bool bOwnerNoSee, bool bCastHiddenShadow);

	/**
	 * Pure: which appearance a player wears, the first of: the player's own choice, the game mode's for its map, the
	 * project settings' for the world's meshing mode, the settings' default.
	 * @param Chosen      The player's own choice (saved), or null
	 * @param ForGameMode The game mode's appearance, or null
	 * @param ByMeshing   The settings' appearance per meshing mode
	 * @param Default     The settings' default appearance
	 * @param bHasWorld   Whether there is a voxel world (false: no meshing mode to look up)
	 * @param Meshing     The world's meshing mode
	 * @return The first that names an appearance; null when none does (the character class's own body)
	 */
	static TSoftObjectPtr<UVCCharacterAppearance> PickAppearance(const TSoftObjectPtr<UVCCharacterAppearance>& Chosen,
		const TSoftObjectPtr<UVCCharacterAppearance>& ForGameMode, const TMap<EMeshingMode, TSoftObjectPtr<UVCCharacterAppearance>>& ByMeshing,
		const TSoftObjectPtr<UVCCharacterAppearance>& Default, bool bHasWorld, EMeshingMode Meshing);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** @return How many rigid-body nodes and cloth meshes the worn meshes run (their gravity follows the movement's) */
	int32 GetSimulatedPartCount() const { return Simulations.Num() + ClothMeshes.Num(); }

	/**
	 * Finds the rigid-body nodes in the worn meshes' anim and post-process instances, and the meshes with cloth, again
	 * after something replaced an instance (the character's anim class); the component ticks while there are any.
	 */
	void RefreshSimulations();

private:
	/** Points each rigid-body node's gravity along the owner's movement's gravity. */
	void UpdateSimulationGravity();

	/** A rigid-body node in a worn mesh's anim instance. */
	struct FSimulation
	{
		TWeakObjectPtr<UAnimInstance> Instance;
		void* Node = nullptr;
	};
	TArray<FSimulation> Simulations;

	/** The worn meshes with cloth. */
	TArray<TWeakObjectPtr<USkeletalMeshComponent>> ClothMeshes;

	/** Destroys the parts actor (its components go with it, wherever they are attached). */
	void RemoveParts();

	UPROPERTY(Transient)
	TObjectPtr<const UVCCharacterAppearance> Current;

	/** The spawned parts actor (the appearance's PartsActorClass), owned by the character. */
	UPROPERTY(Transient)
	TObjectPtr<AActor> PartsActor;

	/** The class's own body and its material overrides, kept on the first change. */
	UPROPERTY(Transient)
	TObjectPtr<USkeletalMesh> OriginalMesh;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> OriginalMaterials;

	bool bCapturedOriginal = false;
};
