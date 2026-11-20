// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimMetaData.h"
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
	/** Raising the shield and holding the guard (D94): its montage loops until the guard is lowered or broken. */
	Block,
	/** Lowering the guard. */
	BlockEnd,
	/** A hit taken on the guard, then the guard held again (its last section loops, as Block's). */
	BlockHit,
	/** The guard broken by a hit the stamina could not pay for: a stagger. */
	GuardBreak,
	/**
	 * Taking an arrow and nocking it (the bow reload plan, D98): its variant is where the arrow comes from
	 * (EVCReloadFrom). The grab is the montage's UVCAnimNotify_ItemHandOff, the nock its UVCAnimNotify_ActionHit.
	 */
	Reload,
	/**
	 * A dodge step (the sword moves plan, D170): its variant is the side it steps to relative to the facing, an
	 * EVCHitDirection (Back: the step back with no movement input). Its root motion carries the capsule (D168).
	 */
	Dodge,
	/** A roll forward, the capsule turned toward the movement input first (D170); its root motion carries the capsule. */
	Roll,
	/**
	 * Holding the sword back to charge a strike (the sword moves plan, D167): the windup, then the held pose looping
	 * until the release or the charged strike. Variant 1 plays its lowering (the charge given up without a strike).
	 */
	Charge,
};

/** Where a reload takes its arrow from: the variant of EVCAnimAction::Reload (D99). */
UENUM(BlueprintType)
enum class EVCReloadFrom : uint8
{
	/** A quiver on the back (Equipment.Slot.Quiver): over the right shoulder. */
	Quiver,
	/** No quiver: from the bag at the hip. */
	Bag,
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
 * Marks where an attack's follow-through ends and its recovery begins (the sword attack flow plan, D102): the body has
 * settled after the strike and the pack's next attack starts from this pose. From here the next attack of the combo
 * plays at once (D104) and movement breaks the recovery off (D103). Read from the asset (UVCActionAnimSet::GetRecoveryTime)
 * as the hit is; nothing happens when the notify itself fires.
 */
UCLASS(meta = (DisplayName = "VC Action Recovery"))
class VOXELCHARACTERPLUGIN_API UVCAnimNotify_ActionRecovery : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual FString GetNotifyName_Implementation() const override;
};

/**
 * Marks where a dodge's or a roll's travel is 90 % done (the sword moves plan, D170): the server holds
 * State.Invulnerable from the move's start to here. Read from the asset (UVCActionAnimSet::GetTravelledTime) as the hit
 * is; nothing happens when the notify itself fires.
 */
UCLASS(meta = (DisplayName = "VC Action Travelled"))
class VOXELCHARACTERPLUGIN_API UVCAnimNotify_ActionTravelled : public UAnimNotify
{
	GENERATED_BODY()

public:
	virtual FString GetNotifyName_Implementation() const override;
};

/**
 * A montage that carries the capsule by its root motion on every move, with movement input or without (the sword moves
 * plan, D168): the one exception to montage root motion only standing (D106, UVCMovementComponent::
 * bMontageRootMotionOnlyStanding), for moves authored to travel (the dodge and the roll, the run attack). A flag on the
 * asset, so every machine that plays the montage decides alike (UVCActionAnimSet::CarriesCapsule). The layers script
 * adds it.
 */
UCLASS(meta = (DisplayName = "VC Carries Capsule"))
class VOXELCHARACTERPLUGIN_API UVCAnimMetaData_CarriesCapsule : public UAnimMetaData
{
	GENERATED_BODY()
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
 * One attack of a combo's table (the sword moves plan, D166): the attack a press of each attack button continues with
 * from this one, by the combo's press rule (D104). A set without a table walks its combo as a list.
 */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCAttackComboStep
{
	GENERATED_BODY()

	/** The attack the primary continues with, an index into AttackCombo; -1: the next in the list. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	int32 NextPrimary = -1;

	/** The attack the secondary attack continues with, an index into AttackCombo; -1: SecondaryAttackStart. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	int32 NextSecondary = -1;

	/** This attack's damage, times the weapon's. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VoxelCharacter|Animation", meta = (ClampMin = "0"))
	float DamageMultiplier = 1.f;
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

	/**
	 * The attack combo, played in turn: the next attack continues the combo from the current one's recovery time to
	 * ComboWindow past its end (D104).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TArray<TObjectPtr<UAnimMontage>> AttackCombo;

	/**
	 * The combo's table (D166), one step per attack of AttackCombo: the attack each button continues with. Empty, or
	 * shorter than the combo: the primary walks the list and the secondary starts at SecondaryAttackStart.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TArray<FVCAttackComboStep> AttackComboSteps;

	/** The attack a press of the secondary attack (IA_SecondaryAttack) starts a combo with; -1: the set has none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	int32 SecondaryAttackStart = -1;

	/**
	 * The charge (D167): the windup (its first section), the held pose (ChargeLoopSection, looping) and the lowering
	 * (ChargeEndSection). A primary press from rest plays it; the release plays the first attack, and a hold of
	 * ChargeHoldTime into the loop plays ChargedAttack by itself. Empty: the set has no charge.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Charge")
	TObjectPtr<UAnimMontage> Charge;

	/** The charged strike, an index into AttackCombo (its step names its damage multiplier and what follows it); -1: none. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Charge")
	int32 ChargedAttack = -1;

	/** Seconds held in the charge's loop before the charged strike fires by itself. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Charge", meta = (ClampMin = "0"))
	float ChargeHoldTime = 1.f;

	/** The charge montage's held pose, looping. */
	static const FName ChargeLoopSection;

	/** The charge montage's lowering, played when the charge is given up. */
	static const FName ChargeEndSection;

	/** Whether the set can charge: a charge montage and a charged strike in the combo. */
	bool HasCharge() const { return Charge != nullptr && AttackCombo.IsValidIndex(ChargedAttack); }

	/**
	 * Seconds from the press to the charged strike (D167): the windup's length (the charge montage's first section) and
	 * ChargeHoldTime.
	 * @return The time, 0 without a charge
	 */
	float GetChargeTime() const;

	/** Seconds after an attack's montage ends within which the next attack still continues the combo (D104). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation", meta = (ClampMin = "0"))
	float ComboWindow = 0.2f;

	/** The blend-out when movement breaks an attack's recovery off (D103), seconds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation", meta = (ClampMin = "0"))
	float RecoveryBlendOut = 0.2f;

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

	/** Taking an arrow from a quiver on the back and nocking it (D98, EVCReloadFrom::Quiver). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Reload")
	TObjectPtr<UAnimMontage> ReloadFromQuiver;

	/** Taking an arrow from the bag at the hip and nocking it (no quiver, D99). Empty: ReloadFromQuiver. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Reload")
	TObjectPtr<UAnimMontage> ReloadFromBag;

	/**
	 * Whether the set shows the arrow (D100), with the ammo's own mesh (its nock at the pivot, the head along +Z): in the
	 * drawing hand from a reload's grab to its nock (HeldArrowGrip on HeldArrowBone), then on the string from the nock to
	 * the loose (from NockedArrowOffset toward BowHandBone).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Reload")
	bool bShowsHeldArrow = false;

	/** The drawing hand's bone: the arrow rides it from the grab and its nock stays with it on the string. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Reload", meta = (EditCondition = "bShowsHeldArrow"))
	FName HeldArrowBone = TEXT("hand_r");

	/** The arrow's fit on HeldArrowBone between the grab and the nock (the bone's space): the pack's arrow_hand_r there. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Reload", meta = (EditCondition = "bShowsHeldArrow"))
	FTransform HeldArrowGrip = FTransform::Identity;

	/** Where the nock sits once nocked, in HeldArrowBone's space (the fingers on the string, as the pack's draw has it). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Reload", meta = (EditCondition = "bShowsHeldArrow"))
	FVector NockedArrowOffset = FVector::ZeroVector;

	/** The bow hand's bone: a nocked arrow points from its nock toward it (the arrow rests on the bow). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Reload", meta = (EditCondition = "bShowsHeldArrow"))
	FName BowHandBone = TEXT("hand_l");

	/**
	 * The set's own full-body locomotion under ABP_VCPlayerLocomotion (vc.Anim.Locomotion 2; the locomotion state machine
	 * plan's D87): while it is held the graph plays these clips and the set's stance layer gives way to them (its
	 * StanceWeight 0). Empty: the base set's (Unarmed's) clips with the stance laid over them, as under the other graphs.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation")
	TObjectPtr<UVCLocomotionClipSet> LocomotionClips;

	/** Raising the shield and holding the guard (D94): the raise, then a hold that loops until the guard comes down. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Block")
	TObjectPtr<UAnimMontage> Block;

	/** Lowering the guard. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Block")
	TObjectPtr<UAnimMontage> BlockEnd;

	/** A hit on the guard, then the hold again (looping, as Block's). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Block")
	TObjectPtr<UAnimMontage> BlockHit;

	/** The guard broken: a stagger. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Block")
	TObjectPtr<UAnimMontage> GuardBreak;

	/**
	 * The dodge steps per side, relative to the facing (the sword moves plan, D170): Back is the step with no movement
	 * input, the four the dodge toward the input while the facing is locked to the view. An empty side plays Front's.
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Moves")
	FVCDirectionalMontages Dodge;

	/** The roll forward, toward the movement input (D170). Empty: the set has no roll (the press steps instead). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Animation|Moves")
	TObjectPtr<UAnimMontage> Roll;

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
	 * The attack a press of a button starts a combo with (D166).
	 * @param bSecondary The secondary attack's button, else the primary's
	 * @return An index into AttackCombo: 0 for the primary, SecondaryAttackStart for the secondary; -1 when the set has none
	 */
	int32 GetFirstAttack(bool bSecondary) const;

	/**
	 * The attack a press of a button continues the combo with from an attack (D166): its step's, else the next in the
	 * list for the primary and the secondary's first for the secondary.
	 * @param Current    The attack played, an index into AttackCombo
	 * @param bSecondary The secondary attack's button, else the primary's
	 * @return An index into AttackCombo, or -1 when the set has no attack for that button
	 */
	int32 GetNextAttack(int32 Current, bool bSecondary) const;

	/**
	 * Whether an attack is one the secondary attack plays: its first, or a step's next on the secondary.
	 * @param Index An index into AttackCombo
	 * @return True for a secondary attack
	 */
	bool IsSecondaryAttack(int32 Index) const;

	/**
	 * An attack's damage multiplier (its step's DamageMultiplier).
	 * @param Index An index into AttackCombo
	 * @return The multiplier, 1 without a step
	 */
	float GetAttackDamageMultiplier(int32 Index) const;

	/**
	 * When an action lands in its montage: the first UVCAnimNotify_ActionHit's time.
	 * @param Montage         The action's montage
	 * @param DefaultFraction The fraction of the montage's length to use when it has no such notify
	 * @return Seconds from the montage's start at play rate 1; 0 for no montage
	 */
	static float GetHitTime(const UAnimMontage* Montage, float DefaultFraction = 0.4f);

	/**
	 * When an attack's follow-through ends and its recovery begins (D102): the first UVCAnimNotify_ActionRecovery's time,
	 * else the montage's end (a montage without the marker has no recovery to break or to chain from).
	 * @param Montage The attack's montage
	 * @return Seconds from the montage's start at play rate 1; 0 for no montage
	 */
	static float GetRecoveryTime(const UAnimMontage* Montage);

	/**
	 * When a dodge's or a roll's travel is 90 % done (D170): the first UVCAnimNotify_ActionTravelled's time, else
	 * DefaultFraction of its length.
	 * @param Montage         The move's montage
	 * @param DefaultFraction The fraction of the montage's length without the marker
	 * @return Seconds from the montage's start at play rate 1; 0 for no montage
	 */
	static float GetTravelledTime(const UAnimMontage* Montage, float DefaultFraction = 0.3f);

	/**
	 * Whether a montage carries the capsule on the move too (D168): it has root motion and UVCAnimMetaData_CarriesCapsule.
	 * @param Montage The montage (null gives false)
	 * @return True when its root motion moves the capsule with movement input as well
	 */
	static bool CarriesCapsule(const UAnimMontage* Montage);

	/**
	 * The side a hit came from, measured about the victim's local up.
	 * @param VictimForward The victim's facing
	 * @param Up            The victim's local up
	 * @param ToSource      From the victim toward the hit's source
	 * @return Front within 45 degrees of the facing, Back within 45 of behind, else Left or Right
	 */
	static EVCHitDirection DirectionOfHit(const FVector& VictimForward, const FVector& Up, const FVector& ToSource);
};
