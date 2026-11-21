// Copyright Daniel Raquel. All Rights Reserved.

#include "Combat/VCArrowProjectile.h"
#include "Combat/VCArrowMovementComponent.h"
#include "Combat/VCCombatStatics.h"
#include "Combat/VCDamageReceiverInterface.h"
#include "Utilities/CGFCombatStatics.h"
#include "Tags/CGFGameplayTags.h"
#include "VoxelCharacterPlugin.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

AVCArrowProjectile::AVCArrowProjectile()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicatingMovement(true);

	Collision = CreateDefaultSubobject<USphereComponent>(TEXT("Collision"));
	Collision->InitSphereRadius(4.f);
	// Meets terrain, props and pawns (whose capsules block WorldDynamic); camera and visibility traces pass it.
	Collision->SetCollisionProfileName(UCollisionProfile::BlockAllDynamic_ProfileName);
	Collision->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Collision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Collision->SetCanEverAffectNavigation(false);
	Collision->CanCharacterStepUpOn = ECB_No;
	RootComponent = Collision;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Collision);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	// LowPolyRPGWeapons' arrows (D53) run 60 uu along +Z from the nock to the tip: turned onto +X with the tip 12 uu
	// ahead of the centre, 8 uu past the sphere, so a stuck arrow's head is in what it hit.
	Mesh->SetRelativeLocationAndRotation(FVector(-48.0, 0.0, 0.0), FRotator(-90.0, 0.0, 0.0));

	Movement = CreateDefaultSubobject<UVCArrowMovementComponent>(TEXT("Movement"));
	Movement->SetUpdatedComponent(Collision);
}

void AVCArrowProjectile::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	Movement->OnProjectileStop.AddUniqueDynamic(this, &AVCArrowProjectile::OnArrowStopped);
}

void AVCArrowProjectile::BeginPlay()
{
	Super::BeginPlay();
	IgnoreShooter();
	if (HasAuthority())
	{
		SetLifeSpan(FlightLifetime);
	}
}

void AVCArrowProjectile::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AVCArrowProjectile, ArrowMesh);
	DOREPLIFETIME(AVCArrowProjectile, bStuck);
}

void AVCArrowProjectile::OnRep_Instigator()
{
	Super::OnRep_Instigator();
	IgnoreShooter();
}

void AVCArrowProjectile::IgnoreShooter()
{
	if (APawn* Shooter = GetInstigator())
	{
		Collision->IgnoreActorWhenMoving(Shooter, true);
	}
}

AVCArrowProjectile* AVCArrowProjectile::SpawnArrow(APawn* Shooter, TSubclassOf<AVCArrowProjectile> ArrowClass, const FVector& Location,
	const FVector& Velocity, const FVCArrowShot& InShot, UStaticMesh* MeshOverride)
{
	UWorld* World = Shooter ? Shooter->GetWorld() : nullptr;
	if (!World || !Shooter->HasAuthority())
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("SpawnArrow: needs a shooter on the server (%s)"), *GetNameSafe(Shooter));
		return nullptr;
	}
	const FTransform Transform(Velocity.Rotation(), Location);
	AVCArrowProjectile* Arrow = World->SpawnActorDeferred<AVCArrowProjectile>(ArrowClass ? *ArrowClass : StaticClass(), Transform,
		Shooter, Shooter, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Arrow)
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("SpawnArrow: %s's arrow did not spawn"), *GetNameSafe(Shooter));
		return nullptr;
	}
	Arrow->FinishSpawning(Transform);
	Arrow->SetArrowMesh(MeshOverride);
	Arrow->Launch(Velocity, InShot);
	return Arrow;
}

void AVCArrowProjectile::Launch(const FVector& Velocity, const FVCArrowShot& InShot)
{
	if (!HasAuthority())
	{
		UE_LOG(LogVoxelCharacter, Warning, TEXT("%s: Launch is the server's; ignored here"), *GetName());
		return;
	}
	Shot = InShot;
	ShooterFaction = UCGFCombatStatics::GetFactionTag(GetInstigator());
	IgnoreShooter();
	SetActorRotation(Velocity.Rotation());
	Movement->Velocity = Velocity;
	// The replicated movement sends the root's velocity, which the movement only sets on its first move.
	Movement->UpdateComponentVelocity();
	ForceNetUpdate();
}

void AVCArrowProjectile::PostNetReceiveVelocity(const FVector& NewVelocity)
{
	// A client flies its copy from the server's velocity (a projectile movement has no other source on a client).
	if (!bStuck && Movement && Movement->UpdatedComponent)
	{
		Movement->Velocity = NewVelocity;
	}
}

void AVCArrowProjectile::SetArrowMesh(UStaticMesh* NewMesh)
{
	if (!HasAuthority() || !NewMesh)
	{
		return;
	}
	ArrowMesh = NewMesh;
	OnRep_ArrowMesh();
}

void AVCArrowProjectile::OnRep_ArrowMesh()
{
	if (ArrowMesh)
	{
		Mesh->SetStaticMesh(ArrowMesh);
	}
}

void AVCArrowProjectile::OnArrowStopped(const FHitResult& Hit)
{
	// A client's copy stops on its own hit and waits for the server's outcome.
	if (!HasAuthority() || bStuck)
	{
		return;
	}
	AActor* Other = Hit.GetActor();
	if (Other && (Other->IsA<APawn>() || UVCCombatStatics::FindCombatComponent(Other)))
	{
		// A body: the damage, and the arrow goes (it does not stay in a creature).
		ApplyHit(Other, Hit);
		Destroy();
		return;
	}
	if (Other && Other->Implements<UVCDamageReceiver>())
	{
		ApplyHit(Other, Hit);
	}
	Stick(Hit);
}

ECGFDamageResult AVCArrowProjectile::ApplyHit(AActor* Target, const FHitResult& Hit)
{
	if (!HasAuthority())
	{
		return ECGFDamageResult::Rejected_NotAuthority;
	}
	if (!Target)
	{
		return ECGFDamageResult::Rejected_NoTarget;
	}

	// Without its shooter the pipeline would treat the arrow as a hazard and hurt anyone: the launch's faction decides.
	APawn* Shooter = IsValid(GetInstigator()) ? GetInstigator() : nullptr;
	if (!Shooter && ShooterFaction.IsValid()
		&& !UCGFCombatStatics::AreHostileFactions(ShooterFaction, UCGFCombatStatics::GetFactionTag(Target)))
	{
		return ECGFDamageResult::Rejected_Friendly;
	}

	float Damage = Shot.GetDamage();
	const bool bCritical = Shot.CritChance > 0.f && FMath::FRand() < Shot.CritChance;
	if (bCritical)
	{
		Damage *= FMath::Max(1.f, Shot.CritMultiplier);
	}
	const FGameplayTag DamageType = Shot.DamageType.IsValid() ? Shot.DamageType : CGFGameplayTags::Damage_Type_Physical;
	FCGFDamageContext Context = UVCCombatStatics::MakeDamageContext(Shooter, this, Damage, DamageType);
	Context.HitLocation = Hit.ImpactPoint;
	Context.HitNormal = Hit.ImpactNormal;
	Context.SourceItemInstanceId = Shot.SourceItemInstanceId;
	if (bCritical)
	{
		Context.ContextTags.AddTag(CGFGameplayTags::Damage_Critical);
	}

	const ECGFDamageResult Result = UVCCombatStatics::ApplyDamageToActor(Target, Context);
	UE_LOG(LogVoxelCharacter, Log, TEXT("%s arrow -> %s: %.1f %s (draw %.2f)%s (%s)"), *GetNameSafe(Shooter), *GetNameSafe(Target),
		Damage, *DamageType.ToString(), Shot.DrawPower, bCritical ? TEXT(" CRIT") : TEXT(""), *UEnum::GetValueAsString(Result));
	return Result;
}

void AVCArrowProjectile::Stick(const FHitResult& Hit)
{
	bStuck = true;
	ApplyStuck();
	// A movable prop every machine knows (replicated, or loaded with the level) carries the arrow, so clients can follow the
	// attachment. Anything else keeps it where it stuck: the terrain's collision is static, and each machine's own.
	UPrimitiveComponent* Surface = Hit.GetComponent();
	const AActor* Prop = Hit.GetActor();
	if (IsValid(Surface) && Surface->Mobility == EComponentMobility::Movable && Prop && (Prop->GetIsReplicated() || Prop->IsNetStartupActor()))
	{
		AttachToComponent(Surface, FAttachmentTransformRules::KeepWorldTransform, Hit.BoneName);
	}
	SetLifeSpan(StuckLifetime);
	ForceNetUpdate();
}

void AVCArrowProjectile::OnRep_Stuck()
{
	if (bStuck)
	{
		ApplyStuck();
	}
}

void AVCArrowProjectile::ApplyStuck()
{
	Collision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (Movement->UpdatedComponent)
	{
		Movement->StopSimulating(FHitResult());
	}
}
