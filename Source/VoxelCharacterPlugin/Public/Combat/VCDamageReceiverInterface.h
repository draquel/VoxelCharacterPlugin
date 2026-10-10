// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Types/CGFCombatTypes.h"
#include "VCDamageReceiverInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UVCDamageReceiver : public UInterface
{
	GENERATED_BODY()
};

/**
 * Takes hits without an ability system (feature 5: breakable crates, later destructible props).
 * UVCCombatStatics::ApplyDamageToActor routes to the combat component when the actor has one and
 * to this interface otherwise, so the melee ability, traps and debug commands need no special
 * case. Implementers also implement ICGFDamageableInterface (faction / dead / immune) so the
 * target rules (AreHostile, FindDamageable) see them.
 */
class VOXELCHARACTERPLUGIN_API IVCDamageReceiver
{
	GENERATED_BODY()

public:
	/**
	 * Apply a hit. Authority only (callers gate).
	 * @param Context The hit.
	 * @return Applied, or a Rejected_* reason.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "VoxelCharacter|Combat")
	ECGFDamageResult ReceiveDamage(const FCGFDamageContext& Context);
};
