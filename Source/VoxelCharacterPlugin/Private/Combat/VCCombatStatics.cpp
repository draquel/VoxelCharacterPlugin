// Copyright Daniel Raquel. All Rights Reserved.

#include "Combat/VCCombatStatics.h"
#include "Combat/VCDamageReceiverInterface.h"
#include "Combat/VCCombatComponent.h"
#include "Utilities/CGFCombatStatics.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

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

FVCViewSweep UVCCombatStatics::SweepHostileUnderView(AActor* Viewer, float Range, float ProbeRadius, const FCollisionQueryParams& Params)
{
	FVCViewSweep Sweep;
	UWorld* World = Viewer ? Viewer->GetWorld() : nullptr;
	if (!World)
	{
		return Sweep;
	}
	Viewer->GetActorEyesViewPoint(Sweep.ViewLocation, Sweep.ViewRotation);
	const FVector End = Sweep.ViewLocation + Sweep.ViewRotation.Vector() * Range;
	Sweep.bHit = World->SweepSingleByChannel(Sweep.Hit, Sweep.ViewLocation, End, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(ProbeRadius), Params);
	AActor* HitActor = Sweep.bHit ? Sweep.Hit.GetActor() : nullptr;
	if (HitActor && UCGFCombatStatics::FindDamageable(HitActor).GetObject() && UCGFCombatStatics::AreHostile(Viewer, HitActor))
	{
		Sweep.Hostile = HitActor;
	}
	return Sweep;
}
