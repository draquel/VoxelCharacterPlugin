// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Types/CGFCombatTypes.h"
#include "Engine/HitResult.h"
#include "CollisionQueryParams.h"
#include "VCCombatStatics.generated.h"

class UVCCombatComponent;

/**
 * What a sweep from an actor's view met (UVCCombatStatics::SweepHostileUnderView): the view it ran from, whether and what it
 * hit, and the hostile damageable among it.
 */
struct FVCViewSweep
{
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	/** The sweep met something (Hit is its hit). */
	bool bHit = false;
	FHitResult Hit;
	/** The hostile damageable the sweep met, else null. */
	AActor* Hostile = nullptr;
};

/**
 * Blueprint-callable entry points into the combat pipeline for anything that
 * deals damage without being an ability: traps, hazards, debug commands, scripts.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCCombatStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The hostile damageable under an actor's view (code review CR2, VCP-12): a thin sphere swept from the actor's eyes along
	 * its control rotation on ECC_Pawn (the engine's Pawn and CharacterMesh profiles ignore Visibility, so a visibility trace
	 * passes straight through every character; terrain still blocks Pawn; a ray exactly through a capsule's axis misses it,
	 * WM-C9), kept when what it meets is damageable and hostile to the viewer. The client's target check
	 * (AVCCharacterBase::FindMeleeTargetUnderCrosshair) and the server's melee ability run this one sweep.
	 * @param Viewer The attacker (its eyes and control rotation)
	 * @param Range How far the sweep reaches (uu)
	 * @param ProbeRadius The sphere's radius (uu)
	 * @param Params Query params (the viewer ignored, the caller's stat)
	 * @return The view, the hit and the hostile actor among it
	 */
	static FVCViewSweep SweepHostileUnderView(AActor* Viewer, float Range, float ProbeRadius, const FCollisionQueryParams& Params);

	/** The combat component on an actor, or null. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	static UVCCombatComponent* FindCombatComponent(AActor* Actor);

	/**
	 * Server-only. Apply a hit to an actor through its combat component, or through
	 * IVCDamageReceiver when it has none (breakable props).
	 * @param Target  Actor to damage.
	 * @param Context The hit.
	 * @return Rejected_NoTarget if the actor has neither; otherwise the receiver's result.
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Combat")
	static ECGFDamageResult ApplyDamageToActor(AActor* Target, const FCGFDamageContext& Context);

	/**
	 * Convenience builder for the common case.
	 * @param Instigator Responsible actor (null for hazards).
	 * @param Causer     Weapon / trap / projectile (null = Instigator).
	 * @param BaseDamage Damage before modifiers.
	 * @param DamageType Damage.Type.*; empty = Physical.
	 * @param bIgnoreFaction Skip the hostility check (hazards).
	 */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	static FCGFDamageContext MakeDamageContext(AActor* Instigator, AActor* Causer, float BaseDamage,
		FGameplayTag DamageType, bool bIgnoreFaction = false);
};
