// Copyright Daniel Raquel. All Rights Reserved.

#include "Combat/VCCombatStatics.h"
#include "Combat/VCCombatComponent.h"
#include "GameFramework/Actor.h"

UVCCombatComponent* UVCCombatStatics::FindCombatComponent(AActor* Actor)
{
	return IsValid(Actor) ? Actor->FindComponentByClass<UVCCombatComponent>() : nullptr;
}

ECGFDamageResult UVCCombatStatics::ApplyDamageToActor(AActor* Target, const FCGFDamageContext& Context)
{
	UVCCombatComponent* Combat = FindCombatComponent(Target);
	return Combat ? Combat->ApplyDamage(Context) : ECGFDamageResult::Rejected_NoTarget;
}

FCGFDamageContext UVCCombatStatics::MakeDamageContext(AActor* Instigator, AActor* Causer, float BaseDamage,
	FGameplayTag DamageType, bool bIgnoreFaction)
{
	FCGFDamageContext Context;
	Context.InstigatorActor = Instigator;
	Context.CauserActor = Causer ? Causer : Instigator;
	Context.BaseDamage = BaseDamage;
	Context.DamageType = DamageType;
	Context.bIgnoreFaction = bIgnoreFaction;
	return Context;
}
