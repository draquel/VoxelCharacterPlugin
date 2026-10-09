// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Types/CGFCombatTypes.h"
#include "VCCombatStatics.generated.h"

class UVCCombatComponent;

/**
 * Blueprint-callable entry points into the combat pipeline for anything that
 * deals damage without being an ability: traps, hazards, debug commands, scripts.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCCombatStatics : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** The combat component on an actor, or null. */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	static UVCCombatComponent* FindCombatComponent(AActor* Actor);

	/**
	 * Server-only. Apply a hit to an actor through its combat component.
	 * @param Target  Actor to damage.
	 * @param Context The hit.
	 * @return Rejected_NoTarget if the actor has no combat component; otherwise the component's result.
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
