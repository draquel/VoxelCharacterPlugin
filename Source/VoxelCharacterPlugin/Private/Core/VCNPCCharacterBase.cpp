// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCNPCCharacterBase.h"
#include "Movement/VCMovementComponent.h"
#include "AI/VCNPCAIController.h"
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
	// The voxel-aware movement component (same as the player): voxel collision is double-sided trimesh, which
	// returns inverted and edge normals; the stock component then fails to land on walkable slopes, stays in
	// Falling with no ground friction and slides downhill (feature 10 surface creatures skied into the sea).
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UVCMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// Tick only while toppling after death (enabled in HandleDied).
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bReplicates = true;

	AbilitySystemComponent = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystemComponent"));
	AbilitySystemComponent->SetIsReplicated(true);
	// NPCs have no owning client: Minimal replicates only what simulated proxies need (tags, cues).
	AbilitySystemComponent->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

	CharacterAttributes = CreateDefaultSubobject<UVCCharacterAttributeSet>(TEXT("CharacterAttributes"));
	CombatAttributes = CreateDefaultSubobject<UVCCombatAttributeSet>(TEXT("CombatAttributes"));

	CombatComponent = CreateDefaultSubobject<UVCCombatComponent>(TEXT("CombatComponent"));
	CombatComponent->FactionTag = CGFGameplayTags::Faction_Monster;

	// Feature 5: every NPC gets the patrol / chase / attack brain unless bAIEnabled is cleared.
	AIControllerClass = AVCNPCAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	bUseControllerRotationYaw = false;
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->bOrientRotationToMovement = true;
		Move->RotationRate = FRotator(0.f, 360.f, 0.f);
		Move->MaxWalkSpeed = 300.f;
	}
}

void AVCNPCCharacterBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bToppling)
	{
		SetActorTickEnabled(false);
		return;
	}
	ToppleElapsed += DeltaSeconds;
	const float Alpha = ToppleSeconds > 0.f ? FMath::Clamp(ToppleElapsed / ToppleSeconds, 0.f, 1.f) : 1.f;
	const float Eased = FMath::InterpEaseIn(0.f, 1.f, Alpha, 2.f);
	FRotator Rot = ToppleStartRotation;
	Rot.Roll = ToppleStartRotation.Roll + 85.f * Eased;
	SetActorRotation(Rot);
	SetActorLocation(ToppleStartLocation - FVector(0.f, 0.f, 20.f * Eased), false, nullptr, ETeleportType::TeleportPhysics);
	if (Alpha >= 1.f)
	{
		bToppling = false;
		SetActorTickEnabled(false);
	}
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
	if (Faction.IsValid() && CombatComponent)
	{
		CombatComponent->FactionTag = Faction;
	}
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
	bool bRagdolled = false;
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		if (Body->GetPhysicsAsset())
		{
			Body->SetCollisionProfileName(TEXT("Ragdoll"));
			Body->SetSimulatePhysics(true);
			bRagdolled = true;
		}
	}
	if (!bRagdolled && ToppleSeconds > 0.f)
	{
		// Static-mesh placeholders: fall over instead of standing dead.
		bToppling = true;
		ToppleElapsed = 0.f;
		ToppleStartRotation = GetActorRotation();
		ToppleStartLocation = GetActorLocation();
		SetActorTickEnabled(true);
	}
	// The brain stops with the body (the controller also idles on a dead pawn).
	if (AVCNPCAIController* AI = Cast<AVCNPCAIController>(GetController()))
	{
		AI->SetAIEnabled(false);
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
