// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCNPCCharacterBase.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Combat/VCCombatAttributeSet.h"
#include "Combat/VCCombatComponent.h"
#include "Tags/CGFGameplayTags.h"
#include "VoxelCharacterPlugin.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

AVCNPCCharacterBase::AVCNPCCharacterBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	// NPCs have no owning client: Minimal replicates only what simulated proxies need (tags, cues).
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	CharacterAttributes = CreateDefaultSubobject<UVCCharacterAttributeSet>(TEXT("CharacterAttributes"));
	CombatAttributes = CreateDefaultSubobject<UVCCombatAttributeSet>(TEXT("CombatAttributes"));

	CombatComponent = CreateDefaultSubobject<UVCCombatComponent>(TEXT("CombatComponent"));
	CombatComponent->FactionTag = CGFGameplayTags::Faction_Monster;
}

UAbilitySystemComponent* AVCNPCCharacterBase::GetAbilitySystemComponent() const
{
	return AbilitySystemComponent;
}

bool AVCNPCCharacterBase::IsDead() const
{
	return CombatComponent && ICGFDamageableInterface::Execute_IsDead(CombatComponent);
}

void AVCNPCCharacterBase::BeginPlay()
{
	Super::BeginPlay();

	if (AbilitySystemComponent)
	{
		AbilitySystemComponent->InitAbilityActorInfo(this, this);

		if (HasAuthority())
		{
			// Direct init is the right tool before play starts; afterwards changes go through effects.
			if (CharacterAttributes)
			{
				CharacterAttributes->InitMaxHealth(StartingMaxHealth);
				CharacterAttributes->InitHealth(StartingMaxHealth);
			}
			if (CombatAttributes)
			{
				CombatAttributes->InitAttackPower(StartingAttackPower);
				CombatAttributes->InitDefense(StartingDefense);
			}

			for (const TSubclassOf<UGameplayAbility>& AbilityClass : DefaultAbilities)
			{
				if (AbilityClass)
				{
					AbilitySystemComponent->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, INDEX_NONE, this));
				}
			}
		}
	}

	if (CombatComponent)
	{
		CombatComponent->OnDied.AddDynamic(this, &AVCNPCCharacterBase::HandleDied);
		CombatComponent->InitializeWithAbilitySystem(AbilitySystemComponent);
	}
}

void AVCNPCCharacterBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DestroyTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void AVCNPCCharacterBase::HandleDied(const FCGFDamageContext& Context)
{
	// Stop being an obstacle and a target; keep the body visible for presentation.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		Capsule->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		if (Body->GetPhysicsAsset())
		{
			Body->SetCollisionProfileName(TEXT("Ragdoll"));
			Body->SetSimulatePhysics(true);
		}
	}

	BP_OnDied(Context);

	if (HasAuthority() && DestroyAfterDeathDelay > 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(DestroyTimerHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				Destroy();
			}), DestroyAfterDeathDelay, false);
		}
	}
}
