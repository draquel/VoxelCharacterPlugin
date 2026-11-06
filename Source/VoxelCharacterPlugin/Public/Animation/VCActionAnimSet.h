// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VCActionAnimSet.generated.h"

class UAnimMontage;

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
