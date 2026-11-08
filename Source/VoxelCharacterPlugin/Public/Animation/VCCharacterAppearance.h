// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VCCharacterAppearance.generated.h"

class AActor;
class UAnimInstance;
class USkeletalMesh;

/**
 * A character's body as data (character animation P6): the mesh the character animates and what dresses it. Which body a
 * character wears is the project's choice, never this plugin's: UVCSettings names a default and one per world meshing
 * mode, a game mode may name one for its map, and a player may choose one (saved). A MetaHuman arrives as content: its
 * Blueprint is the parts actor, and nothing here links to the MetaHuman modules.
 */
UCLASS(BlueprintType)
class VOXELCHARACTERPLUGIN_API UVCCharacterAppearance : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/**
	 * The mesh the character animates. Empty: the parts actor's body component's (read from the parts class where no parts
	 * are spawned), else the character class's own.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	TSoftObjectPtr<USkeletalMesh> BodyMesh;

	/**
	 * An actor whose skeletal mesh components dress the body: a MetaHuman's Blueprint (body, face, outfit, grooms). Spawned
	 * on every machine that draws the character, never on a dedicated server; its parts attach to the animated mesh.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	TSoftClassPtr<AActor> PartsActorClass;

	/** The parts actor's component whose mesh and materials the character animates (it is hidden; a MetaHuman's Body). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	FName PartsBodyComponent = TEXT("Body");

	/**
	 * Parts that animate themselves instead of following the animated mesh by leader pose: a MetaHuman's Face (its AnimBP
	 * copies the head from its parent and runs RigLogic) and an outfit part whose post-process AnimBP copies its pose from
	 * its parent and simulates (Kellan's Torso: the hoodie's strings). Every other part follows by leader pose.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	TArray<FName> PartsWithOwnAnimation = { TEXT("Face") };

	/** The anim class this body plays. Empty: UVCSettings::PlayerLocomotionAnimClass, else the character Blueprint's own. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	TSoftClassPtr<UAnimInstance> AnimClass;

	/**
	 * Whether the body has the UE5 Mannequin's ik_foot bones, which foot placement moves. A MetaHuman's body has none: its
	 * feet plant through the foot bones themselves (FVCAnimNode_FootBoneIK, weighted by UVCAnimInstance::FootBoneIKAlpha).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	bool bHasIKFootBones = true;

	/**
	 * Whether the body's post-process AnimBP runs: a MetaHuman's corrective pose drivers, the shapes of its shoulders,
	 * elbows and knees, about 2 ms of animation worker a body at LOD 0 (G148). It never runs while the body is hidden from its
	 * own player's view (first person).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	bool bBodyPostProcess = true;

	/** The highest LOD the body's post-process AnimBP runs at: 0 near the camera only, -1 at every LOD. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance", meta = (ClampMin = "-1"))
	int32 BodyPostProcessMaxLOD = -1;
};
