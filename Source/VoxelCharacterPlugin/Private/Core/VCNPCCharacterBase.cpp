// Copyright Daniel Raquel. All Rights Reserved.

#include "Core/VCNPCCharacterBase.h"
#include "Movement/VCGravityFrame.h"
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
#include "Components/WidgetComponent.h"
#include "UI/VCNPCHealthBarWidget.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarNPCHealthBars(
	TEXT("vc.UI.NPCHealthBars"),
	1,
	TEXT("Floating NPC health bars: 0 = never, 1 = NPCs with bShowHealthBar (default), 2 = every NPC."),
	ECVF_Default);

AVCNPCCharacterBase::AVCNPCCharacterBase(const FObjectInitializer& ObjectInitializer)
	// The voxel-aware movement component (same as the player): capsule sweeps against the voxel trimesh can return
	// edge normals that the stock component rejects as unwalkable, so it fails to land on walkable slopes, stays in
	// Falling with no ground friction and slides downhill (feature 10 surface creatures skied into the sea). The
	// inverted face normals that once made this worse are fixed at the source (VoxelCollisionManager winding).
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UVCMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// Enough jump to clear a one-voxel step when the AI hops an obstacle (the default 420 tops out around 90 uu).
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->JumpZVelocity = FMath::Max(Movement->JumpZVelocity, 520.f);
	}

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

FVector AVCNPCCharacterBase::GetPawnViewLocation() const
{
	return GetActorLocation() + FVCGravityFrame::VectorToWorld(GetCharacterMovement(), FVector(0.f, 0.f, BaseEyeHeight));
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
	const UCharacterMovementComponent* ToppleMovement = GetCharacterMovement();
	if (ToppleMovement && ToppleMovement->HasCustomGravity())
	{
		// On a planet (spherical Phase 4, WM-SY13): roll about the body's own forward axis and sink along local down.
		SetActorRotation(ToppleStartRotation.Quaternion() * FQuat(FVector::ForwardVector, FMath::DegreesToRadians(85.f * Eased)));
		SetActorLocation(ToppleStartLocation - FVCGravityFrame::GetUp(ToppleMovement) * (20.f * Eased), false, nullptr, ETeleportType::TeleportPhysics);
	}
	else
	{
		FRotator Rot = ToppleStartRotation;
		Rot.Roll = ToppleStartRotation.Roll + 85.f * Eased;
		SetActorRotation(Rot);
		SetActorLocation(ToppleStartLocation - FVector(0.f, 0.f, 20.f * Eased), false, nullptr, ETeleportType::TeleportPhysics);
	}
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
	UpdateHealthBar();
}

void AVCNPCCharacterBase::SetTemperament(EVCNPCTemperament InTemperament)
{
	Temperament = InTemperament;
	if (AVCNPCAIController* AI = Cast<AVCNPCAIController>(GetController()))
	{
		AI->SetTemperament(InTemperament);
	}
}

void AVCNPCCharacterBase::SetShowHealthBar(bool bShow)
{
	bShowHealthBar = bShow;
	UpdateHealthBar();
}

void AVCNPCCharacterBase::UpdateHealthBar()
{
	const int32 Mode = CVarNPCHealthBars.GetValueOnGameThread();
	const bool bWanted = !IsRunningDedicatedServer() && !IsDead() && CombatComponent
		&& (Mode == 2 || (Mode == 1 && bShowHealthBar));

	if (bWanted && !HealthBarComponent)
	{
		HealthBarComponent = NewObject<UWidgetComponent>(this, TEXT("HealthBar"));
		HealthBarComponent->SetupAttachment(GetRootComponent());
		HealthBarComponent->SetWidgetSpace(EWidgetSpace::Screen);
		HealthBarComponent->SetWidgetClass(UVCNPCHealthBarWidget::StaticClass());
		HealthBarComponent->SetDrawSize(FVector2D(80.f, 8.f));
		const float HalfHeight = GetCapsuleComponent() ? GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : 90.f;
		HealthBarComponent->SetRelativeLocation(FVector(0.f, 0.f, HalfHeight + 35.f));
		HealthBarComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		HealthBarComponent->SetVisibility(false); // until the first health change
		HealthBarComponent->RegisterComponent();
		CombatComponent->OnHealthChanged.AddUniqueDynamic(this, &AVCNPCCharacterBase::HandleHealthBarHealthChanged);
	}
	else if (!bWanted && HealthBarComponent)
	{
		if (CombatComponent)
		{
			CombatComponent->OnHealthChanged.RemoveDynamic(this, &AVCNPCCharacterBase::HandleHealthBarHealthChanged);
		}
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(HealthBarHideTimer);
		}
		HealthBarComponent->DestroyComponent();
		HealthBarComponent = nullptr;
	}
}

void AVCNPCCharacterBase::HandleHealthBarHealthChanged(float NewHealth, float /*OldHealth*/, float MaxHealth)
{
	if (!HealthBarComponent)
	{
		return;
	}
	if (UVCNPCHealthBarWidget* Bar = Cast<UVCNPCHealthBarWidget>(HealthBarComponent->GetUserWidgetObject()))
	{
		Bar->SetHealth(NewHealth, MaxHealth);
	}
	HealthBarComponent->SetVisibility(true);
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(HealthBarHideTimer, this, &AVCNPCCharacterBase::HideHealthBar, HealthBarVisibleSeconds, false);
	}
}

void AVCNPCCharacterBase::HideHealthBar()
{
	if (HealthBarComponent)
	{
		HealthBarComponent->SetVisibility(false);
	}
}

void AVCNPCCharacterBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DestroyTimerHandle);
		World->GetTimerManager().ClearTimer(HealthBarHideTimer);
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
	// A dead NPC has no health to show.
	UpdateHealthBar();

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
