// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Types/CGFCombatTypes.h"
#include "VCArrowProjectile.generated.h"

class USphereComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UVCArrowMovementComponent;

/**
 * One shot's damage, carried by an arrow from the bow to what it hits (ranged combat plan B2). The bow's attack fills it
 * from the bow's Weapon fragment and the draw's power.
 */
USTRUCT(BlueprintType)
struct VOXELCHARACTERPLUGIN_API FVCArrowShot
{
	GENERATED_BODY()

	/** Damage at full draw (the bow's Weapon fragment BaseDamage). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0"))
	float FullDamage = 0.f;

	/** The draw's power, 0 to 1 (UItemFragment_RangedWeapon::GetDrawPower): the damage scales by it, as the launch speed does. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0", ClampMax = "1"))
	float DrawPower = 1.f;

	/** Damage.Type.*; empty = Physical. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|Combat", meta = (Categories = "Damage.Type"))
	FGameplayTag DamageType;

	/** Chance of a critical hit, 0 to 1, rolled when the arrow hits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0", ClampMax = "1"))
	float CritChance = 0.f;

	/** A critical hit's damage multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|Combat", meta = (ClampMin = "1"))
	float CritMultiplier = 1.f;

	/** The bow's instance (FCGFDamageContext::SourceItemInstanceId). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VoxelCharacter|Combat")
	FGuid SourceItemInstanceId;

	/** @return The damage before a crit: FullDamage scaled by the draw's power */
	float GetDamage() const { return FullDamage * FMath::Clamp(DrawPower, 0.f, 1.f); }
};

/**
 * An arrow in flight and after it (ranged combat plan B2, D54): spawned and replicated by the server, not predicted. It
 * flies under local gravity (UVCArrowMovementComponent), sweeping a small sphere that ignores its shooter. What it meets:
 * - a pawn, or anything with a combat component, takes the shot's damage through the combat pipeline (factions, crits;
 *   the arrow is the causer, so the hit react turns to the side the arrow came from), and the arrow goes;
 * - terrain or a prop: the arrow sticks for StuckLifetime with its collision off (a movable prop that every machine knows,
 *   replicated or loaded with the level, carries it; a damageable prop, IVCDamageReceiver, takes the damage too).
 * An arrow that meets nothing goes after FlightLifetime. Damage, sticking and removal are the server's; a client flies its
 * copy from the replicated velocity and takes the server's stuck state and transform.
 *
 * Its mesh comes from the ammo (SetArrowMesh, replicated) or a subclass's mesh component; the mesh component's relative
 * transform holds the tip along the actor's X just ahead of the collision sphere, so a stuck arrow's tip sits in the
 * surface it hit.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API AVCArrowProjectile : public AActor
{
	GENERATED_BODY()

public:
	AVCArrowProjectile();

	/**
	 * Server: spawn an arrow and send it off, what the bow's attack does (a script or a trap can too).
	 * @param Shooter Who shoots: the arrow's Owner and Instigator, ignored by its sweep, and its world
	 * @param ArrowClass The arrow's class (the bow's ProjectileClass); null for this class
	 * @param Location Where it starts
	 * @param Velocity The launch velocity (uu/s)
	 * @param InShot The damage it carries
	 * @param MeshOverride Its mesh (the ammo's world mesh); null keeps the class's
	 * @return The arrow; null off the server, without a shooter, or when the spawn failed
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Combat")
	static AVCArrowProjectile* SpawnArrow(APawn* Shooter, TSubclassOf<AVCArrowProjectile> ArrowClass, const FVector& Location,
		const FVector& Velocity, const FVCArrowShot& InShot, UStaticMesh* MeshOverride);

	/**
	 * Server: send the arrow off. Call once, right after spawning it with its shooter as Instigator.
	 * @param Velocity The launch velocity (uu/s): the aim direction times the bow's launch speed and the draw's power
	 * @param InShot The damage it carries
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Combat")
	void Launch(const FVector& Velocity, const FVCArrowShot& InShot);

	/**
	 * Server: the arrow's mesh, sent to every client (the ammo's world mesh). Null keeps the mesh component's own.
	 * @param NewMesh The mesh
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Combat")
	void SetArrowMesh(UStaticMesh* NewMesh);

	/**
	 * Server: the shot's damage to an actor the arrow hit, through the combat pipeline, a crit rolled here. When the
	 * shooter is gone (dead, left) the shooter's faction at the launch still decides who it may hurt.
	 * @param Target What it hit
	 * @param Hit The hit (its impact point and normal go in the context)
	 * @return The pipeline's result; Rejected_NotAuthority off the server
	 */
	ECGFDamageResult ApplyHit(AActor* Target, const FHitResult& Hit);

	/** @return True once the arrow has stopped in terrain or a prop */
	UFUNCTION(BlueprintPure, Category = "VoxelCharacter|Combat")
	bool IsStuck() const { return bStuck; }

	/** @return The damage it carries (server) */
	const FVCArrowShot& GetShot() const { return Shot; }

	/** @return The flight */
	UVCArrowMovementComponent* GetArrowMovement() const { return Movement; }

	/** Seconds an arrow that meets nothing flies before it goes. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0.1"))
	float FlightLifetime = 10.f;

	/** Seconds a stuck arrow stays. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|Combat", meta = (ClampMin = "0.1"))
	float StuckLifetime = 30.f;

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostNetReceiveVelocity(const FVector& NewVelocity) override;
	virtual void OnRep_Instigator() override;

	/** The flight stopped on a hit (OnProjectileStop): damage, stick or go (server). */
	UFUNCTION()
	void OnArrowStopped(const FHitResult& Hit);

	UFUNCTION()
	void OnRep_ArrowMesh();

	UFUNCTION()
	void OnRep_Stuck();

	/** The sweep ignores the shooter (every machine, once the Instigator is known). */
	void IgnoreShooter();

	/** Server: stop where it hit, riding a movable component, and stay StuckLifetime. */
	void Stick(const FHitResult& Hit);

	/** Stuck: no collision, no flight (every machine). */
	void ApplyStuck();

	UPROPERTY(VisibleAnywhere, Category = "VoxelCharacter|Combat")
	TObjectPtr<USphereComponent> Collision;

	UPROPERTY(VisibleAnywhere, Category = "VoxelCharacter|Combat")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, Category = "VoxelCharacter|Combat")
	TObjectPtr<UVCArrowMovementComponent> Movement;

	/** The mesh sent to clients (SetArrowMesh). */
	UPROPERTY(ReplicatedUsing = OnRep_ArrowMesh)
	TObjectPtr<UStaticMesh> ArrowMesh;

	/** Stopped in terrain or a prop. */
	UPROPERTY(ReplicatedUsing = OnRep_Stuck)
	bool bStuck = false;

	/** The damage it carries (server). */
	FVCArrowShot Shot;

	/** The shooter's faction at the launch, for a hit after the shooter is gone (server). */
	FGameplayTag ShooterFaction;
};
