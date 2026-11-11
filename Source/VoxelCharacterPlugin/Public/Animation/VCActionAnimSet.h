// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VCActionAnimSet.generated.h"

class UAnimMontage;
class UVCLocomotionClipSet;

/** An action a character plays a montage for (character animation P3). */
UENUM(BlueprintType)
enum class EVCAnimAction : uint8
{
	Attack,
	Dig,
	Chop,
	Place,
	Use,
	HitReact,
	Death,
	Revive,
	/** Drawing a bow and holding it drawn (ranged combat plan B3): its montage loops until the release or a stop. */
	Draw,
	/** Loosing the arrow. */
	Release,
	/**
	 * Taking a carried weapon from its sheath into the hand (the equipment sheathing plan, D79). Not "Draw": that is the
	 * bow's string. The montage depends on the sheath (UVCActionAnimSet::SheathMontages).
	 */
	Unsheathe,
	/** Putting the weapon in the hand back in its sheath. */
	Sheathe,
};

/** The side a hit came from, in the victim's gravity frame (the variant of a hit react or a death). */
UENUM(BlueprintType)
enum class EVCHitDirection : uint8
{
	Front,
	Back,
	Left,
	Right,
};

/**
 * Marks the moment an action lands in its montage: the frame a swing hits, a pickaxe strikes, an axe bites. The server
 * reads its time from the asset (UVCActionAnimSet::GetHitTime) and applies the action's effect then; nothing happens
 * when the notify itself fires, so a server that never ticks the pose still times the hit.
 */
UCLASS(meta = (DisplayName = "VC Action Hit"))
class VOXELCHARACTERPLUGIN_API UVCAnimNotify_ActionHit : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual FString GetNotifyName_Implementation() const override;
};

/**
 * Marks the frame of an Unsheathe where the hand closes on the grip, or of a Sheathe where it lets go at the sheath: the
 * weapon's visual moves between the sheath and the hand there on every machine, and the server puts the drawn weapon in
 * the hand at an Unsheathe's (UVCActionAnimSet::GetHandOffTime, read from the asset as the hit time is).
 */
UCLASS(meta = (DisplayName = "VC Item Hand-off"))
class VOXELCHARACTERPLUGIN_API UVCAnimNotify_ItemHandOff : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual FString GetNotifyName_Implementation() const override;
};

/** The unsheathe and the sheathe of one sheath (Equipment.Slot.Sheath.*) for an action set (D79). */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCSheathMontages
{
	GENERATED_BODY()

	/** The sheath these montages draw from and put back into. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation", meta = (Categories = "Equipment.Slot.Sheath"))
	FGameplayTag Sheath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimMontage> Unsheathe;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimMontage> Sheathe;
};

/** One montage for each side a hit can come from; an empty side falls back to Front. Its members are EditAnywhere so a
 *  script can fill a value of it; the asset property holding it is what limits editing to defaults. */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCDirectionalMontages
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimMontage> Front;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimMontage> Back;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimMontage> Left;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimMontage> Right;

	/**
	 * The montage for a side.
	 * @param Direction Where the hit came from
	 * @return That side's montage, Front's when it has none, or null
	 */
	UAnimMontage* Get(EVCHitDirection Direction) const;
};

/**
 * The montages of one action set (Equipment.AnimSet.*): what a character plays to attack, dig, chop, place, use, react
 * to a hit, die and get up while it holds that kind of item. The layer that changes its stance comes with the item
 * (the Equipment fragment's AnimLayerClass); this asset is the montage half. An empty montage means the action plays
 * nothing for this set. Character animation P3 (CHARACTER_ANIMATION_PLAN.md).
 */
UCLASS(BlueprintType)
class VOXELCHARACTERPLUGIN_API UVCActionAnimSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The action set these montages are for. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation", meta = (Categories = "Equipment.AnimSet"))
	FGameplayTag ActionSet;

	/** The attack combo, played in turn while attacks follow each other within ComboWindow. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TArray<TObjectPtr<UAnimMontage>> AttackCombo;

	/** Seconds after an attack's montage ends within which the next attack continues the combo. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation", meta = (ClampMin = "0"))
	float ComboWindow = 0.6f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimMontage> Dig;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimMontage> Chop;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimMontage> Place;

	/** Using an item: eating, drinking, lighting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimMontage> Use;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	FVCDirectionalMontages HitReact;

	/** Played on death by a body with no physics asset; a body with one ragdolls instead. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	FVCDirectionalMontages Death;

	/** Getting up when revived. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimMontage> Revive;

	/**
	 * Whether the off hand grips the main-hand item too (a pickaxe, a two-handed weapon): the anim graph's hand IK puts
	 * the off wrist at OffHandGrip (character animation P4). The set's clips place the main hand; the grip is where its
	 * item's handle runs on from there.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|IK")
	bool bOffHandIK = false;

	/** Where the off hand's wrist goes, in the main hand bone's space (hand_r, uu). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|IK", meta = (EditCondition = "bOffHandIK"))
	FVector OffHandGrip = FVector::ZeroVector;

	/**
	 * Whether the main hand's item sits differently while the set's Draw plays (the character refinement plan's C1): the
	 * pack's bow clips hold the bow on a weapon bone our skeletons lack, so one fit on hand_l cannot be upright both beside
	 * the leg at rest and across the raised fist in the draw. With this set, the character blends the item from its Equipment
	 * fragment's AttachOffset to DrawnGrip by the weight of the montage in the UpperBody slot.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Grip")
	bool bHasDrawnGrip = false;

	/** Where the main hand's item sits on its bone while the Draw plays, in place of its fit (the bone's space). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Grip", meta = (EditCondition = "bHasDrawnGrip"))
	FTransform DrawnGrip = FTransform::Identity;

	/** Drawing a bow and holding it drawn: the draw, then a loop held until the release (ranged combat plan B3). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimMontage> Draw;

	/** Loosing the arrow. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UAnimMontage> Release;

	/**
	 * The set's own full-body locomotion under ABP_VCPlayerLocomotion (vc.Anim.Locomotion 2; the locomotion state machine
	 * plan's D87): while it is held the graph plays these clips and the set's stance layer gives way to them (its
	 * StanceWeight 0). Empty: the base set's (Unarmed's) clips with the stance laid over them, as under the other graphs.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UVCLocomotionClipSet> LocomotionClips;

	/** Taking this set's weapon from a sheath and putting it back, per sheath (the equipment sheathing plan, D79). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TArray<FVCSheathMontages> SheathMontages;

	/**
	 * The unsheathe or the sheathe for a sheath.
	 * @param Action Unsheathe or Sheathe
	 * @param Sheath The sheath (Equipment.Slot.Sheath.*)
	 * @return That sheath's montage, else the first entry's, else null
	 */
	UAnimMontage* GetSheathMontage(EVCAnimAction Action, const FGameplayTag& Sheath) const;

	/**
	 * When the hand-off happens in an unsheathe or a sheathe montage: its first UVCAnimNotify_ItemHandOff, else
	 * DefaultFraction of its length.
	 * @param Montage         The montage (null gives 0)
	 * @param DefaultFraction The fraction without a notify
	 * @return Seconds into the montage at play rate 1
	 */
	static float GetHandOffTime(const UAnimMontage* Montage, float DefaultFraction = 0.4f);

	/**
	 * The montage for an action.
	 * @param Action  The action
	 * @param Variant The combo index for Attack (wrapped), an EVCHitDirection for HitReact and Death; ignored otherwise
	 * @return The montage, or null when the set has none for the action
	 */
	UAnimMontage* GetMontage(EVCAnimAction Action, int32 Variant = 0) const;

	/**
	 * When an action lands in its montage: the first UVCAnimNotify_ActionHit's time.
	 * @param Montage         The action's montage
	 * @param DefaultFraction The fraction of the montage's length to use when it has no such notify
	 * @return Seconds from the montage's start at play rate 1; 0 for no montage
	 */
	static float GetHitTime(const UAnimMontage* Montage, float DefaultFraction = 0.4f);

	/**
	 * The side a hit came from, measured about the victim's local up.
	 * @param VictimForward The victim's facing
	 * @param Up            The victim's local up
	 * @param ToSource      From the victim toward the hit's source
	 * @return Front within 45 degrees of the facing, Back within 45 of behind, else Left or Right
	 */
	static EVCHitDirection DirectionOfHit(const FVector& VictimForward, const FVector& Up, const FVector& ToSource);
};
