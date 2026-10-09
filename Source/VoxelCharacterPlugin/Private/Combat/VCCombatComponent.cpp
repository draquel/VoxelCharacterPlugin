// Copyright Daniel Raquel. All Rights Reserved.

#include "Combat/VCCombatComponent.h"
#include "Combat/VCDamageEffect.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Utilities/CGFCombatStatics.h"
#include "Tags/CGFGameplayTags.h"
#include "VoxelCharacterPlugin.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameplayEffect.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

UVCCombatComponent::UVCCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	DamageEffectClass = UVCDamageEffect::StaticClass();
}

void UVCCombatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION(UVCCombatComponent, FactionTag, COND_InitialOnly);
	DOREPLIFETIME(UVCCombatComponent, bIsDead);
	DOREPLIFETIME(UVCCombatComponent, bIsDowned);
}

void UVCCombatComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UninitializeFromAbilitySystem();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DownedTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

bool UVCCombatComponent::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

UAbilitySystemComponent* UVCCombatComponent::GetAbilitySystemComponent() const
{
	if (BoundASC.IsValid())
	{
		return BoundASC.Get();
	}
	// Honours IAbilitySystemInterface, so a pawn whose ASC lives on its player state resolves too.
	return UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner(), /*LookForComponent*/ false);
}

void UVCCombatComponent::InitializeWithAbilitySystem(UAbilitySystemComponent* ASC)
{
	UninitializeFromAbilitySystem();

	if (!ASC)
	{
		return;
	}

	BoundASC = ASC;
	HealthChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(UVCCharacterAttributeSet::GetHealthAttribute())
		.AddUObject(this, &UVCCombatComponent::HandleHealthChanged);
}

void UVCCombatComponent::UninitializeFromAbilitySystem()
{
	if (UAbilitySystemComponent* ASC = BoundASC.Get())
	{
		if (HealthChangedHandle.IsValid())
		{
			ASC->GetGameplayAttributeValueChangeDelegate(UVCCharacterAttributeSet::GetHealthAttribute()).Remove(HealthChangedHandle);
		}
	}
	HealthChangedHandle.Reset();
	BoundASC.Reset();
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

float UVCCombatComponent::GetHealth() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	return ASC ? ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetHealthAttribute()) : 0.f;
}

float UVCCombatComponent::GetMaxHealth() const
{
	const UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	return ASC ? ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetMaxHealthAttribute()) : 0.f;
}

float UVCCombatComponent::GetHealthNormalized() const
{
	const float Max = GetMaxHealth();
	return Max > 0.f ? FMath::Clamp(GetHealth() / Max, 0.f, 1.f) : 0.f;
}

// ---------------------------------------------------------------------------
// Damage
// ---------------------------------------------------------------------------

ECGFDamageResult UVCCombatComponent::ApplyDamage(const FCGFDamageContext& Context)
{
	if (!HasAuthority())
	{
		return ECGFDamageResult::Rejected_NotAuthority;
	}
	if (bIsDead)
	{
		return ECGFDamageResult::Rejected_Dead;
	}

	UAbilitySystemComponent* TargetASC = GetAbilitySystemComponent();
	if (!TargetASC)
	{
		return ECGFDamageResult::Rejected_NoAbilitySystem;
	}
	if (TargetASC->HasMatchingGameplayTag(CGFGameplayTags::State_Invulnerable))
	{
		return ECGFDamageResult::Rejected_Invulnerable;
	}

	// Faction check: only when the instigator is itself a combatant. Hazards (no instigator or
	// an instigator with no faction) and contexts that opt out skip it.
	if (!Context.bIgnoreFaction)
	{
		AActor* Instigator = Context.InstigatorActor.Get();
		const FGameplayTag InstigatorFaction = UCGFCombatStatics::GetFactionTag(Instigator);
		if (InstigatorFaction.IsValid() && !UCGFCombatStatics::AreHostileFactions(InstigatorFaction, FactionTag))
		{
			return ECGFDamageResult::Rejected_Friendly;
		}
	}

	if (ICGFDamageableInterface::Execute_IsImmuneToDamage(this, Context))
	{
		return ECGFDamageResult::Rejected_ByTarget;
	}

	if (!DamageEffectClass)
	{
		UE_LOG(LogVoxelCharacter, Error, TEXT("%s: DamageEffectClass is unset; damage cannot be applied."), *GetNameSafe(GetOwner()));
		return ECGFDamageResult::Rejected_NoAbilitySystem;
	}

	// Source = the instigator's ASC when it has one (so its AttackPower is captured);
	// otherwise the target applies the spec to itself and the execution ignores AttackPower.
	AActor* InstigatorActor = Context.InstigatorActor.Get();
	UAbilitySystemComponent* SourceASC = InstigatorActor
		? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(InstigatorActor, false)
		: nullptr;
	if (!SourceASC)
	{
		SourceASC = TargetASC;
	}

	FGameplayEffectContextHandle EffectContext = SourceASC->MakeEffectContext();
	EffectContext.AddInstigator(InstigatorActor, Context.CauserActor.Get());
	EffectContext.AddSourceObject(GetOwner());
	if (!Context.HitLocation.IsZero() || !Context.HitNormal.IsZero())
	{
		FHitResult Hit;
		Hit.Location = Context.HitLocation;
		Hit.ImpactPoint = Context.HitLocation;
		Hit.Normal = Context.HitNormal;
		Hit.ImpactNormal = Context.HitNormal;
		Hit.HitObjectHandle = FActorInstanceHandle(GetOwner());
		EffectContext.AddHitResult(Hit);
	}

	const FGameplayEffectSpecHandle SpecHandle = SourceASC->MakeOutgoingSpec(DamageEffectClass, 1.f, EffectContext);
	if (!SpecHandle.IsValid())
	{
		UE_LOG(LogVoxelCharacter, Error, TEXT("%s: failed to build damage spec from %s."), *GetNameSafe(GetOwner()), *GetNameSafe(DamageEffectClass));
		return ECGFDamageResult::Rejected_NoAbilitySystem;
	}

	FGameplayEffectSpec& Spec = *SpecHandle.Data;
	Spec.SetSetByCallerMagnitude(CGFGameplayTags::SetByCaller_Damage, FMath::Max(0.f, Context.BaseDamage));
	Spec.AddDynamicAssetTag(Context.DamageType.IsValid() ? Context.DamageType : CGFGameplayTags::Damage_Type_Physical);
	for (const FGameplayTag& Tag : Context.ContextTags)
	{
		Spec.AddDynamicAssetTag(Tag);
	}

	// Recorded before applying: the Health delegate fires synchronously inside the apply and
	// reports this as the killing hit.
	LastDamageContext = Context;

	SourceASC->ApplyGameplayEffectSpecToTarget(Spec, TargetASC);

	FGameplayEventData EventData;
	EventData.EventTag = CGFGameplayTags::Event_Combat_Damaged;
	EventData.Instigator = InstigatorActor;
	EventData.Target = GetOwner();
	EventData.EventMagnitude = Context.BaseDamage;
	EventData.ContextHandle = EffectContext;
	TargetASC->HandleGameplayEvent(EventData.EventTag, &EventData);

	return ECGFDamageResult::Applied;
}

void UVCCombatComponent::Kill(const FCGFDamageContext& Context)
{
	if (!HasAuthority() || bIsDead)
	{
		return;
	}
	LastDamageContext = Context;
	EnterDead();
}

bool UVCCombatComponent::Revive(float HealthFraction)
{
	if (!HasAuthority())
	{
		return false;
	}

	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	bool bChanged = bIsDead || bIsDowned;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DownedTimerHandle);
	}

	if (ASC)
	{
		// Clear persistent-ASC leftovers too (player state keeps its tags across avatars).
		if (ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Dead))
		{
			ASC->RemoveLooseGameplayTag(CGFGameplayTags::State_Dead, 1, EGameplayTagReplicationState::TagOnly);
			bChanged = true;
		}
		if (ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Downed))
		{
			ASC->RemoveLooseGameplayTag(CGFGameplayTags::State_Downed, 1, EGameplayTagReplicationState::TagOnly);
			bChanged = true;
		}

		const float Max = ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetMaxHealthAttribute());
		const float NewHealth = Max * FMath::Clamp(HealthFraction, 0.f, 1.f);
		if (NewHealth > 0.f)
		{
			ASC->SetNumericAttributeBase(UVCCharacterAttributeSet::GetHealthAttribute(), NewHealth);
			bChanged = true;
		}
	}

	bIsDead = false;
	bIsDowned = false;

	if (bChanged)
	{
		OnRevived.Broadcast();
	}
	return bChanged;
}

// ---------------------------------------------------------------------------
// Health → state
// ---------------------------------------------------------------------------

void UVCCombatComponent::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	OnHealthChanged.Broadcast(Data.NewValue, Data.OldValue, GetMaxHealth());

	if (HasAuthority() && Data.NewValue <= 0.f && !bIsDead && !bIsDowned)
	{
		HandleOutOfHealth();
	}
}

void UVCCombatComponent::HandleOutOfHealth()
{
	switch (OutOfHealthPolicy)
	{
	case EVCOutOfHealthPolicy::Downed:
		EnterDowned();
		break;
	case EVCOutOfHealthPolicy::Die:
	default:
		EnterDead();
		break;
	}
}

void UVCCombatComponent::EnterDowned()
{
	bIsDowned = true;

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		ASC->AddLooseGameplayTag(CGFGameplayTags::State_Downed, 1, EGameplayTagReplicationState::TagOnly);

		FGameplayEventData EventData;
		EventData.EventTag = CGFGameplayTags::Event_Combat_Downed;
		EventData.Instigator = LastDamageContext.InstigatorActor.Get();
		EventData.Target = GetOwner();
		ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
	}

	if (DownedTimeout > 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(DownedTimerHandle, this, &UVCCombatComponent::HandleDownedTimeout, DownedTimeout, false);
		}
	}

	UE_LOG(LogVoxelCharacter, Log, TEXT("%s is downed."), *GetNameSafe(GetOwner()));
	OnDowned.Broadcast(LastDamageContext);
}

void UVCCombatComponent::HandleDownedTimeout()
{
	if (bIsDowned && !bIsDead)
	{
		EnterDead();
	}
}

void UVCCombatComponent::EnterDead()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DownedTimerHandle);
	}

	bIsDowned = false;
	bIsDead = true;

	if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
	{
		if (ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Downed))
		{
			ASC->RemoveLooseGameplayTag(CGFGameplayTags::State_Downed, 1, EGameplayTagReplicationState::TagOnly);
		}
		ASC->AddLooseGameplayTag(CGFGameplayTags::State_Dead, 1, EGameplayTagReplicationState::TagOnly);
		ASC->CancelAllAbilities();

		FGameplayEventData EventData;
		EventData.EventTag = CGFGameplayTags::Event_Combat_Died;
		EventData.Instigator = LastDamageContext.InstigatorActor.Get();
		EventData.Target = GetOwner();
		ASC->HandleGameplayEvent(EventData.EventTag, &EventData);
	}

	UE_LOG(LogVoxelCharacter, Log, TEXT("%s died (instigator %s)."), *GetNameSafe(GetOwner()), *GetNameSafe(LastDamageContext.InstigatorActor.Get()));
	OnDied.Broadcast(LastDamageContext);
}

// ---------------------------------------------------------------------------
// Client replication
// ---------------------------------------------------------------------------

void UVCCombatComponent::OnRep_IsDead()
{
	if (bIsDead)
	{
		OnDied.Broadcast(FCGFDamageContext());
	}
}

void UVCCombatComponent::OnRep_IsDowned()
{
	if (bIsDowned)
	{
		OnDowned.Broadcast(FCGFDamageContext());
	}
}
