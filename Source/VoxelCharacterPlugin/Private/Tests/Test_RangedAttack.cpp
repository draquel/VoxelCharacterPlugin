// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS && WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN

#include "Camera/VCAimCameraMode.h"
#include "Combat/VCArrowMovementComponent.h"
#include "UI/VCReticleWidget.h"
#include "Combat/VCArrowProjectile.h"
#include "Combat/VCCombatStatics.h"
#include "Combat/VCCombatComponent.h"
#include "Combat/VCRangedAttackAbility.h"
#include "Core/VCCharacterBase.h"
#include "Core/VCGameplayTags.h"
#include "Core/VCNPCCharacterBase.h"
#include "Components/EquipmentManagerComponent.h"
#include "Components/InventoryComponent.h"
#include "Data/Fragments/ItemFragment_Durability.h"
#include "Data/Fragments/ItemFragment_Equipment.h"
#include "Data/Fragments/ItemFragment_RangedWeapon.h"
#include "Data/Fragments/ItemFragment_Weapon.h"
#include "Data/ItemDefinition.h"
#include "Subsystems/ItemDatabaseSubsystem.h"
#include "Tags/CGFGameplayTags.h"
#include "Testing/CGFTestGameInstance.h"
#include "Types/CGFEquipmentTypes.h"
#include "Types/ItemInstanceFragments.h"
#include "AbilitySystemComponent.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"

// ---------------------------------------------------------------------------
// The bow's attack (ranged combat plan B3): the draw's power and its minimum, no draw without an arrow, one arrow a shot
// (and a bow's wear), a release refused when the server finds no arrow left. The shooter is an NPC (it owns its ability
// system) given a main hand and a bag; a game instance holds the definitions, and its world begins play here.
// ---------------------------------------------------------------------------
namespace VCRangedTestHelpers
{
	/** A game instance whose world has begun play: the item database for the definitions, BeginPlay for the NPC. */
	struct FRangedWorld
	{
		CGFTesting::FTestGameInstance Game;
		UWorld* World = nullptr;
		UItemDatabaseSubsystem* Database = nullptr;
		TArray<UItemDefinition*> Definitions;

		FRangedWorld()
		{
			World = Game.World;
			World->InitializeActorsForPlay(FURL());
			if (AWorldSettings* Settings = World->GetWorldSettings())
			{
				Settings->NotifyBeginPlay();
			}
			Database = Game.GetSubsystem<UItemDatabaseSubsystem>();
		}

		~FRangedWorld()
		{
			// EndPlay while the world still exists (VCTesting::FPlayWorld's reason).
			TArray<AActor*> Actors;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				Actors.Add(*It);
			}
			for (AActor* Actor : Actors)
			{
				World->DestroyActor(Actor);
			}
			for (UItemDefinition* Definition : Definitions)
			{
				Definition->RemoveFromRoot();
			}
		}

		UItemDefinition* Define(const TCHAR* Name, int32 StackSize, const FGameplayTagContainer& Tags)
		{
			UItemDefinition* Definition = NewObject<UItemDefinition>(GetTransientPackage(), Name);
			Definition->AddToRoot();
			Definition->MaxStackSize = StackSize;
			Definition->ItemTags = Tags;
			Database->RegisterDefinition(Definition->GetPrimaryAssetId(), Definition);
			Definitions.Add(Definition);
			return Definition;
		}
	};

	/** The ammo's tag in these tests: any tag will do, so a native one. */
	FGameplayTag AmmoTag()
	{
		return CGFGameplayTags::Item_Category_Misc;
	}

	/** A bow as the demo's (B1): 15 damage at full draw, 6000 uu/s, the draw from 0.25 s (30%) to 1 s, 150 shots of wear. */
	UItemDefinition* DefineBow(FRangedWorld& W)
	{
		UItemDefinition* Bow = W.Define(TEXT("RangedTestBow"), 1, FGameplayTagContainer(CGFGameplayTags::Item_Category_Weapon));
		UItemFragment_Equipment* Equip = NewObject<UItemFragment_Equipment>(Bow);
		Equip->EquipmentSlotTag = CGFGameplayTags::Equipment_Slot_MainHand;
		UItemFragment_Weapon* Weapon = NewObject<UItemFragment_Weapon>(Bow);
		Weapon->BaseDamage = 15.f;
		Weapon->CritChance = 0.f;
		UItemFragment_RangedWeapon* Ranged = NewObject<UItemFragment_RangedWeapon>(Bow);
		Ranged->AmmoTag = AmmoTag();
		UItemFragment_Durability* Durability = NewObject<UItemFragment_Durability>(Bow);
		Durability->MaxDurability = 150.f;
		Durability->DegradeRate = 1.f;
		UInstanceFragment_DurabilityState* State = NewObject<UInstanceFragment_DurabilityState>(Bow);
		State->CurrentDurability = 150.f;
		Bow->Fragments = { Equip, Weapon, Ranged, Durability };
		Bow->DefaultInstanceFragments.Add(State);
		return Bow;
	}

	/** A shooter: an NPC that stands still, with the ranged attack, a main hand and a four-slot bag. */
	struct FShooter
	{
		AVCNPCCharacterBase* NPC = nullptr;
		UEquipmentManagerComponent* Equipment = nullptr;
		UInventoryComponent* Bag = nullptr;
		UAbilitySystemComponent* ASC = nullptr;
	};

	FShooter SpawnShooter(UWorld* World, const FVector& Location)
	{
		FShooter Out;
		const FTransform Transform(Location);
		Out.NPC = World->SpawnActorDeferred<AVCNPCCharacterBase>(AVCNPCCharacterBase::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Out.NPC)
		{
			return Out;
		}
		Out.NPC->bAIEnabled = false;
		Out.NPC->CombatComponent->FactionTag = CGFGameplayTags::Faction_Player;
		Out.NPC->DefaultAbilities.Add(UVCRangedAttackAbility::StaticClass());
		Out.NPC->FinishSpawning(Transform);
		if (UCharacterMovementComponent* Move = Out.NPC->GetCharacterMovement())
		{
			Move->DisableMovement();
		}
		// Registered after BeginPlay, so each begins play at once: the main hand from its definition, the bag's slots.
		Out.Equipment = NewObject<UEquipmentManagerComponent>(Out.NPC, TEXT("RangedTestEquipment"));
		FEquipmentSlotDefinition MainHand;
		MainHand.SlotTag = CGFGameplayTags::Equipment_Slot_MainHand;
		Out.Equipment->AvailableSlots.Add(MainHand);
		Out.Equipment->RegisterComponent();
		Out.Bag = NewObject<UInventoryComponent>(Out.NPC, TEXT("RangedTestBag"));
		Out.Bag->MaxSlots = 4;
		Out.Bag->RegisterComponent();
		Out.ASC = Out.NPC->GetAbilitySystemComponent();
		return Out;
	}

	/** One world tick as its own engine frame (the timer manager ticks once a frame). */
	void TickFor(UWorld* World, float Seconds)
	{
		for (float Elapsed = 0.f; Elapsed < Seconds - UE_KINDA_SMALL_NUMBER; Elapsed += 0.05f)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, 0.05f);
		}
	}

	bool StartDraw(const FShooter& Shooter)
	{
		return Shooter.ASC && Shooter.ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(VCGameplayTags::Ability_Attack_Ranged));
	}

	UVCRangedAttackAbility* Instance(const FShooter& Shooter)
	{
		FGameplayAbilitySpec* Spec = Shooter.ASC ? Shooter.ASC->FindAbilitySpecFromClass(UVCRangedAttackAbility::StaticClass()) : nullptr;
		return Spec ? Cast<UVCRangedAttackAbility>(Spec->GetPrimaryInstance()) : nullptr;
	}

	int32 CountArrows(UWorld* World)
	{
		int32 Count = 0;
		for (TActorIterator<AVCArrowProjectile> It(World); It; ++It)
		{
			Count += IsValid(*It) ? 1 : 0;
		}
		return Count;
	}

	float BowDurability(const FShooter& Shooter)
	{
		const FItemInstance Bow = Shooter.Equipment->GetEquippedItem(CGFGameplayTags::Equipment_Slot_MainHand);
		const UInstanceFragment_DurabilityState* State = Bow.FindFragment<UInstanceFragment_DurabilityState>();
		return State ? State->CurrentDurability : -1.f;
	}
}

using namespace VCRangedTestHelpers;

#define VC_RANGED_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCRanged_DrawRules, "VoxelCharacter.Combat.Ranged.DrawRules", VC_RANGED_FLAGS)
bool FVCRanged_DrawRules::RunTest(const FString& Parameters)
{
	FRangedWorld W;
	if (!TestNotNull(TEXT("The item database"), W.Database))
	{
		return false;
	}
	UItemDefinition* Bow = DefineBow(W);
	UItemDefinition* Arrow = W.Define(TEXT("RangedTestArrow"), 50, FGameplayTagContainer(AmmoTag()));
	FShooter S = SpawnShooter(W.World, FVector(0, 0, 1000));
	if (!S.NPC || !S.ASC) { AddError(TEXT("Spawn failed")); return false; }

	TestFalse(TEXT("Empty hands: no ranged weapon"), UVCRangedAttackAbility::HoldsRangedWeapon(S.NPC));
	TestFalse(TEXT("Empty hands: no draw"), UVCRangedAttackAbility::CanStartDraw(S.NPC));
	TestEqual(TEXT("Empty hands: no power"), UVCRangedAttackAbility::GetDrawPower(S.NPC, 1.f), 0.f);

	TestTrue(TEXT("The bow equips"), S.Equipment->TryEquipToSlot(W.Database->CreateItemInstance(Bow->GetPrimaryAssetId()),
		CGFGameplayTags::Equipment_Slot_MainHand) == EEquipmentResult::Success);
	TestTrue(TEXT("A bow in hand"), UVCRangedAttackAbility::HoldsRangedWeapon(S.NPC));
	TestFalse(TEXT("No arrow: no draw"), UVCRangedAttackAbility::CanStartDraw(S.NPC));
	TestFalse(TEXT("No arrow: the server does not start one"), StartDraw(S));

	TestTrue(TEXT("Arrows in the bag"), S.Bag->TryAddItem(W.Database->CreateItemInstance(Arrow->GetPrimaryAssetId(), 3)) == EInventoryOperationResult::Success);
	TestTrue(TEXT("An arrow: a draw may start"), UVCRangedAttackAbility::CanStartDraw(S.NPC));
	TestEqual(TEXT("Shorter than the shortest draw: nothing"), UVCRangedAttackAbility::GetDrawPower(S.NPC, 0.1f), 0.f);
	TestEqual(TEXT("The shortest draw: its power"), UVCRangedAttackAbility::GetDrawPower(S.NPC, 0.25f), 0.3f);
	TestEqual(TEXT("Halfway: halfway between"), UVCRangedAttackAbility::GetDrawPower(S.NPC, 0.625f), 0.65f, 1e-4f);
	TestEqual(TEXT("A full draw: full power"), UVCRangedAttackAbility::GetDrawPower(S.NPC, 1.f), 1.f);
	TestEqual(TEXT("Held longer: still full"), UVCRangedAttackAbility::GetDrawPower(S.NPC, 3.f), 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCRanged_OneArrowPerShot, "VoxelCharacter.Combat.Ranged.OneArrowPerShot", VC_RANGED_FLAGS)
bool FVCRanged_OneArrowPerShot::RunTest(const FString& Parameters)
{
	FRangedWorld W;
	if (!TestNotNull(TEXT("The item database"), W.Database))
	{
		return false;
	}
	UItemDefinition* Bow = DefineBow(W);
	UItemDefinition* Arrow = W.Define(TEXT("RangedTestArrow"), 50, FGameplayTagContainer(AmmoTag()));
	FShooter S = SpawnShooter(W.World, FVector(0, 0, 1000));
	if (!S.NPC || !S.ASC) { AddError(TEXT("Spawn failed")); return false; }
	S.Equipment->TryEquipToSlot(W.Database->CreateItemInstance(Bow->GetPrimaryAssetId()), CGFGameplayTags::Equipment_Slot_MainHand);
	S.Bag->TryAddItem(W.Database->CreateItemInstance(Arrow->GetPrimaryAssetId(), 3));
	const FPrimaryAssetId ArrowId = Arrow->GetPrimaryAssetId();

	// A full draw: one arrow flies at the bow's full speed, one leaves the bag, the bow wears by one.
	TestTrue(TEXT("The draw starts"), StartDraw(S));
	UVCRangedAttackAbility* Ability = Instance(S);
	TestTrue(TEXT("It is drawing"), Ability && Ability->IsDrawing());
	TickFor(W.World, 1.f);
	TestEqual(TEXT("Nothing flies while drawn"), CountArrows(W.World), 0);
	TestTrue(TEXT("The release reaches the ability"), UVCRangedAttackAbility::ReleaseDraw(S.ASC));
	TestEqual(TEXT("One arrow flies"), CountArrows(W.World), 1);
	TestEqual(TEXT("One arrow left the bag"), S.Bag->GetItemCount(ArrowId), 2);
	TestEqual(TEXT("The bow wore by its rate"), BowDurability(S), 149.f);
	AVCArrowProjectile* Shot = Ability ? Ability->GetLastArrow() : nullptr;
	if (TestNotNull(TEXT("The shot's arrow"), Shot))
	{
		TestTrue(TEXT("Its shooter"), Shot->GetInstigator() == S.NPC);
		TestEqual(TEXT("The full draw's power"), Shot->GetShot().DrawPower, 1.f);
		TestEqual(TEXT("The bow's damage"), Shot->GetShot().GetDamage(), 15.f);
		TestTrue(*FString::Printf(TEXT("The bow's full speed (%.0f uu/s)"), Shot->GetArrowMovement()->Velocity.Size()),
			FMath::IsNearlyEqual(Shot->GetArrowMovement()->Velocity.Size(), 6000.0, 1.0));
	}
	TestFalse(TEXT("The ability ended"), Ability && Ability->IsActive());

	// Let go too soon: no arrow, none used, no wear.
	TestTrue(TEXT("A second draw starts"), StartDraw(S));
	TickFor(W.World, 0.1f);
	UVCRangedAttackAbility::ReleaseDraw(S.ASC);
	TestEqual(TEXT("A short draw shoots nothing"), CountArrows(W.World), 1);
	TestEqual(TEXT("and uses no arrow"), S.Bag->GetItemCount(ArrowId), 2);
	TestEqual(TEXT("and wears nothing"), BowDurability(S), 149.f);

	// A half draw: the arrow flies at the draw's power.
	TestTrue(TEXT("A third draw starts"), StartDraw(S));
	TickFor(W.World, 0.65f);
	UVCRangedAttackAbility::ReleaseDraw(S.ASC);
	TestEqual(TEXT("A second arrow flies"), CountArrows(W.World), 2);
	TestEqual(TEXT("One arrow left"), S.Bag->GetItemCount(ArrowId), 1);
	AVCArrowProjectile* Half = Ability ? Ability->GetLastArrow() : nullptr;
	if (TestNotNull(TEXT("The half draw's arrow"), Half))
	{
		const float Power = Half->GetShot().DrawPower;
		TestTrue(*FString::Printf(TEXT("A partial power (%.3f)"), Power), Power > 0.3f && Power < 1.f);
		TestTrue(TEXT("and the speed with it"), FMath::IsNearlyEqual(Half->GetArrowMovement()->Velocity.Size(), 6000.0 * Power, 1.0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCRanged_ReleaseWithoutArrowRefused, "VoxelCharacter.Combat.Ranged.ReleaseWithoutArrowRefused", VC_RANGED_FLAGS)
bool FVCRanged_ReleaseWithoutArrowRefused::RunTest(const FString& Parameters)
{
	FRangedWorld W;
	if (!TestNotNull(TEXT("The item database"), W.Database))
	{
		return false;
	}
	UItemDefinition* Bow = DefineBow(W);
	UItemDefinition* Arrow = W.Define(TEXT("RangedTestArrow"), 50, FGameplayTagContainer(AmmoTag()));
	FShooter S = SpawnShooter(W.World, FVector(0, 0, 1000));
	if (!S.NPC || !S.ASC) { AddError(TEXT("Spawn failed")); return false; }
	S.Equipment->TryEquipToSlot(W.Database->CreateItemInstance(Bow->GetPrimaryAssetId()), CGFGameplayTags::Equipment_Slot_MainHand);
	const FItemInstance Last = W.Database->CreateItemInstance(Arrow->GetPrimaryAssetId(), 1);
	S.Bag->TryAddItem(Last);

	// The last arrow leaves the bag while the bow is drawn (dropped, traded): the server refuses the release.
	TestTrue(TEXT("The draw starts with one arrow"), StartDraw(S));
	TickFor(W.World, 1.f);
	TestTrue(TEXT("The arrow leaves the bag"), S.Bag->TryRemoveItem(Last.InstanceId, -1) == EInventoryOperationResult::Success);
	UVCRangedAttackAbility::ReleaseDraw(S.ASC);
	TestEqual(TEXT("No arrow flies"), CountArrows(W.World), 0);
	TestEqual(TEXT("The bow did not wear"), BowDurability(S), 150.f);
	const UVCRangedAttackAbility* Ability = Instance(S);
	TestFalse(TEXT("The ability ended"), Ability && Ability->IsActive());
	TestFalse(TEXT("And no draw starts now"), StartDraw(S));
	return true;
}

// ---------------------------------------------------------------------------
// While a bow is drawn (B4) the player's body turns toward the view in third person, and back to facing where it moves
// once the draw ends.
// ---------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCRanged_FacesAimWhileDrawn, "VoxelCharacter.Combat.Ranged.FacesAimWhileDrawn", VC_RANGED_FLAGS)
bool FVCRanged_FacesAimWhileDrawn::RunTest(const FString& Parameters)
{
	FRangedWorld W;
	const FTransform Stand(FVector(0, 0, 1000));
	AVCCharacterBase* Player = W.World->SpawnActorDeferred<AVCCharacterBase>(AVCCharacterBase::StaticClass(), Stand, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("The player spawns"), Player))
	{
		return false;
	}
	Player->bWaitForTerrain = false;
	Player->FinishSpawning(Stand);
	const UCharacterMovementComponent* Move = Player->GetCharacterMovement();
	TestTrue(TEXT("Third person: the body faces where it moves"), Move->bOrientRotationToMovement && !Move->bUseControllerDesiredRotation);
	Player->SetFacesAim(true);
	TestTrue(TEXT("Drawn: the body turns toward the view"), !Move->bOrientRotationToMovement && Move->bUseControllerDesiredRotation);
	Player->SetFacesAim(false);
	TestTrue(TEXT("Released: back to where it moves"), Move->bOrientRotationToMovement && !Move->bUseControllerDesiredRotation);
	return true;
}

// ---------------------------------------------------------------------------
// The aim camera's place is a function of the pawn and the view alone (the character refinement plan's D74): the pivot
// is the pawn plus the offset turned by the view's yaw, the camera the arm back along the view; pitch moves the camera,
// not the pivot; without a movement component up is world Z. The reticle's ticks close with the draw's power.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCRanged_AimCameraLocation, "VoxelCharacter.Combat.AimCameraLocation", VC_RANGED_FLAGS)
bool FVCRanged_AimCameraLocation::RunTest(const FString& Parameters)
{
	const FVector Actor(1000.0, -500.0, 200.0);
	const FVector Offset(0.0, 60.0, 55.0);
	const float Arm = 150.f;

	// Looking along +X, level: the pivot is 60 to the right (+Y) and 55 up, the camera 150 back along -X.
	FVector Camera = UVCAimCameraMode::ComputeCameraLocation(Actor, FRotator(0.f, 0.f, 0.f), nullptr, Offset, Arm);
	TestTrue(TEXT("Level +X"), Camera.Equals(Actor + FVector(-150.0, 60.0, 55.0), 1.0e-3));

	// Looking along +Y: the offset turns with the yaw (the right is now -X).
	Camera = UVCAimCameraMode::ComputeCameraLocation(Actor, FRotator(0.f, 90.f, 0.f), nullptr, Offset, Arm);
	TestTrue(TEXT("Level +Y"), Camera.Equals(Actor + FVector(-60.0, -150.0, 55.0), 1.0e-3));

	// Pitched 30 degrees up along +X: the pivot stays, the camera goes back and down along the view.
	const FRotator Up(30.f, 0.f, 0.f);
	Camera = UVCAimCameraMode::ComputeCameraLocation(Actor, Up, nullptr, Offset, Arm);
	TestTrue(TEXT("Pitched: pivot unmoved, camera along the view"), Camera.Equals(Actor + FVector(0.0, 60.0, 55.0) - Up.Vector() * Arm, 1.0e-3));

	// Deterministic: the same inputs twice give the same place (no state, no lag).
	TestTrue(TEXT("Deterministic"), Camera.Equals(UVCAimCameraMode::ComputeCameraLocation(Actor, Up, nullptr, Offset, Arm), 0.0));

	// The class defaults are the ones the owner's camera uses.
	const UVCAimCameraMode* Defaults = GetDefault<UVCAimCameraMode>();
	TestTrue(TEXT("Default location uses the class defaults"),
		UVCAimCameraMode::ComputeDefaultCameraLocation(Actor, Up, nullptr).Equals(
			UVCAimCameraMode::ComputeCameraLocation(Actor, Up, nullptr, Defaults->TargetOffset, Defaults->ArmLength), 0.0));
	TestTrue(TEXT("The aim camera has no lag"), Defaults->LagSpeed <= 0.f);

	// The reticle: open with no draw, closed at a full one, between in between, clamped beyond.
	TestEqual(TEXT("Reticle open"), UVCReticleWidget::ComputeSpread(0.f, 18.f, 6.f), 18.f);
	TestEqual(TEXT("Reticle closed"), UVCReticleWidget::ComputeSpread(1.f, 18.f, 6.f), 6.f);
	TestEqual(TEXT("Reticle half"), UVCReticleWidget::ComputeSpread(0.5f, 18.f, 6.f), 12.f);
	TestEqual(TEXT("Reticle clamped"), UVCReticleWidget::ComputeSpread(2.f, 18.f, 6.f), 6.f);
	return true;
}

// ---------------------------------------------------------------------------
// The shot over the drop (G159): the launch direction the ballistic solve gives makes a projectile at that speed, under
// that gravity, pass through the target (the flight in closed form, as the projectile movement integrates a constant
// acceleration); level, up and down a slope, under a tilted pull (a planet's), the low arc; beyond the speed's reach the
// solve says so and keeps the straight line; no gravity flies straight.
// ---------------------------------------------------------------------------
namespace VCBallisticTestHelpers
{
	/** How far the flight from Start along Direction at Speed under Gravity passes from Target: the miss at the closest pass. */
	double MissDistance(const FVector& Start, const FVector& Direction, float Speed, const FVector& Gravity, const FVector& Target)
	{
		const FVector Velocity = Direction * Speed;
		double Best = TNumericLimits<double>::Max();
		const double Duration = 2.0 * (Target - Start).Size() / FMath::Max(1.f, Speed) + 1.0;
		for (double T = 0.0; T <= Duration; T += 1.0e-4)
		{
			const FVector At = Start + Velocity * T + 0.5 * Gravity * T * T;
			Best = FMath::Min(Best, (At - Target).Size());
		}
		return Best;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCRanged_BallisticShot, "VoxelCharacter.Combat.BallisticShot", VC_RANGED_FLAGS)
bool FVCRanged_BallisticShot::RunTest(const FString& Parameters)
{
	using namespace VCBallisticTestHelpers;
	const FVector Gravity(0.0, 0.0, -980.0);
	const FVector Start(-1500.0, -1200.0, 150.0);
	const float Speed = 6000.f;
	FVector Direction;

	// The range's posts at 10, 20 and 40 m, level: each reached, launched above the line by the drop it makes up.
	for (const double Distance : {1000.0, 2000.0, 4000.0})
	{
		const FVector Target = Start + FVector(Distance, 0.0, 0.0);
		TestTrue(FString::Printf(TEXT("Level %.0f m reachable"), Distance / 100.0), UVCCombatStatics::SolveLaunchDirection(Start, Target, Speed, Gravity, Direction));
		TestTrue(FString::Printf(TEXT("Level %.0f m unit"), Distance / 100.0), FMath::IsNearlyEqual(Direction.Size(), 1.0, 1.0e-6));
		TestTrue(FString::Printf(TEXT("Level %.0f m lands on the target"), Distance / 100.0), MissDistance(Start, Direction, Speed, Gravity, Target) < 0.5);
		// Straight at it, the arrow would fall g x^2 / (2 v^2) below it: 13.6, 54.4 and 217.8 uu at 10, 20 and 40 m.
		const double Drop = 980.0 * Distance * Distance / (2.0 * Speed * Speed);
		TestTrue(FString::Printf(TEXT("Level %.0f m: the straight line misses by the drop"), Distance / 100.0),
			FMath::IsNearlyEqual(MissDistance(Start, FVector::ForwardVector, Speed, Gravity, Target), Drop, 0.05 * Drop + 1.0));
		TestTrue(FString::Printf(TEXT("Level %.0f m: aimed up, the low arc"), Distance / 100.0), Direction.Z > 0.0 && Direction.Z < UE_INV_SQRT_2);
	}

	// Up and down a slope, off the axis.
	for (const FVector& Offset : {FVector(2500.0, 900.0, 600.0), FVector(1800.0, -1300.0, -700.0)})
	{
		TestTrue(TEXT("Slope reachable"), UVCCombatStatics::SolveLaunchDirection(Start, Start + Offset, Speed, Gravity, Direction));
		TestTrue(TEXT("Slope lands on the target"), MissDistance(Start, Direction, Speed, Gravity, Start + Offset) < 0.5);
	}

	// A planet's pull, tilted: the same solve in the plane of the pull.
	const FVector Tilted = FVector(0.3, -0.2, -1.0).GetSafeNormal() * 980.0;
	const FVector Far = Start + FVector(3000.0, 500.0, -200.0);
	TestTrue(TEXT("Tilted pull reachable"), UVCCombatStatics::SolveLaunchDirection(Start, Far, Speed, Tilted, Direction));
	TestTrue(TEXT("Tilted pull lands on the target"), MissDistance(Start, Direction, Speed, Tilted, Far) < 0.5);

	// A weak draw (0.3 of the speed) reaches about v^2 / g = 33 m on the level: 40 m is out of reach, the line kept.
	const FVector Post40 = Start + FVector(4000.0, 0.0, 0.0);
	TestFalse(TEXT("Weak draw: 40 m out of reach"), UVCCombatStatics::SolveLaunchDirection(Start, Post40, Speed * 0.3f, Gravity, Direction));
	TestTrue(TEXT("Out of reach keeps the straight line"), Direction.Equals(FVector::ForwardVector, 1.0e-6));

	// No gravity: straight; no speed or no distance: not solvable.
	TestTrue(TEXT("No gravity reachable"), UVCCombatStatics::SolveLaunchDirection(Start, Post40, Speed, FVector::ZeroVector, Direction));
	TestTrue(TEXT("No gravity straight"), Direction.Equals(FVector::ForwardVector, 1.0e-6));
	TestFalse(TEXT("No speed"), UVCCombatStatics::SolveLaunchDirection(Start, Post40, 0.f, Gravity, Direction));
	TestFalse(TEXT("No distance"), UVCCombatStatics::SolveLaunchDirection(Start, Start, Speed, Gravity, Direction));

	// No world, no gravity to solve for.
	TestTrue(TEXT("No world, no gravity"), UVCRangedAttackAbility::GetArrowGravity(nullptr, Start).IsZero());
	return true;
}

#endif // WITH_AUTOMATION_TESTS && WITH_EQUIPMENT_PLUGIN && WITH_INVENTORY_PLUGIN
