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

	/**
	 * The anim class this body plays under vc.Anim.Locomotion 1 or 2. Empty: the class the switch names
	 * (UVCSettings::PlayerLocomotionAnimClass or PlayerStateMachineAnimClass), else the character Blueprint's own.
	 */
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

	/**
	 * How this body's hands differ from the Mannequin's, by bone (the character refinement plan's D71): an item is fitted
	 * once to the Mannequin's hand (its Equipment fragment's AttachOffset) and a body whose hand is shaped or sized
	 * differently (a MetaHuman's) corrects every item on that bone here, in the bone's space, after the item's fit. Empty: the
	 * item's fit alone. Measured on the character test level with vc.Equip.Nudge and vc.Equip.PrintFit.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Appearance")
	TMap<FName, FTransform> AttachCorrections;

	/**
	 * The correction for a bone: the entry for it, else identity.
	 * @param Bone The bone an item attaches to
	 * @return The correction in the bone's space
	 */
	FTransform GetAttachCorrection(FName Bone) const
	{
		const FTransform* Found = AttachCorrections.Find(Bone);
		return Found ? *Found : FTransform::Identity;
	}
};
