// Copyright Daniel Raquel. All Rights Reserved.

#include "Combat/VCCombatStatics.h"
#include "Combat/VCDamageReceiverInterface.h"
#include "Combat/VCCombatComponent.h"
#include "GameFramework/Actor.h"

UVCCombatComponent* UVCCombatStatics::FindCombatComponent(AActor* Actor)
{
	return IsValid(Actor) ? Actor->FindComponentByClass<UVCCombatComponent>() : nullptr;
}

ECGFDamageResult UVCCombatStatics::ApplyDamageToActor(AActor* Target, const FCGFDamageContext& Context)
{
	if (UVCCombatComponent* Combat = FindCombatComponent(Target))
	{
		return Combat->ApplyDamage(Context);
	}
	// No ability system: breakable props and the like take hits through the receiver interface.
	if (IsValid(Target) && Target->Implements<UVCDamageReceiver>())
	{
		return IVCDamageReceiver::Execute_ReceiveDamage(Target, Context);
	}
	return ECGFDamageResult::Rejected_NoTarget;
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
