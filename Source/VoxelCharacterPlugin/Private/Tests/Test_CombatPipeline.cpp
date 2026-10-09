// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Core/VCNPCCharacterBase.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Combat/VCCombatComponent.h"
#include "Combat/VCCombatStatics.h"
#include "Combat/VCDamageExecution.h"
#include "Utilities/CGFCombatStatics.h"
#include "Tags/CGFGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

// ---------------------------------------------------------------------------
// Harness: a standalone Game world (authority) with begun play, torn down per test.
// ---------------------------------------------------------------------------
namespace VCCombatTestHelpers
{
	struct FWorldHarness
	{
		UWorld* World = nullptr;

		FWorldHarness()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("VCCombatTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			if (AWorldSettings* WorldSettings = World->GetWorldSettings())
			{
				WorldSettings->NotifyBeginPlay();
			}
		}

		~FWorldHarness()
		{
			if (World)
			{
				GEngine->DestroyWorldContext(World);
				World->DestroyWorld(false);
			}
		}
	};

	/** Spawn an NPC combatant with the given stats; BeginPlay has run on return. */
	AVCNPCCharacterBase* SpawnCombatant(UWorld* World, FGameplayTag Faction, float MaxHealth, float AttackPower, float Defense,
		EVCOutOfHealthPolicy Policy = EVCOutOfHealthPolicy::Die, const FVector& Location = FVector::ZeroVector)
	{
		const FTransform Transform(Location);
		AVCNPCCharacterBase* NPC = World->SpawnActorDeferred<AVCNPCCharacterBase>(AVCNPCCharacterBase::StaticClass(), Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!NPC)
		{
			return nullptr;
		}
		NPC->StartingMaxHealth = MaxHealth;
		NPC->StartingAttackPower = AttackPower;
		NPC->StartingDefense = Defense;
		NPC->DestroyAfterDeathDelay = 0.f;
		NPC->CombatComponent->FactionTag = Faction;
		NPC->CombatComponent->OutOfHealthPolicy = Policy;
		NPC->FinishSpawning(Transform);
		return NPC;
	}

	FCGFDamageContext Hit(AActor* Instigator, float Base, FGameplayTag Type = FGameplayTag(), bool bIgnoreFaction = false)
	{
		return UVCCombatStatics::MakeDamageContext(Instigator, Instigator, Base, Type, bIgnoreFaction);
	}
}

using namespace VCCombatTestHelpers;

#define VC_COMBAT_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// ===========================================================================
// Pure rules
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCCombat_HostilityRule, "VoxelCharacter.Combat.HostilityRule", VC_COMBAT_FLAGS)
bool FVCCombat_HostilityRule::RunTest(const FString& Parameters)
{
	using namespace CGFGameplayTags;
	TestTrue(TEXT("Player vs Monster hostile"), UCGFCombatStatics::AreHostileFactions(Faction_Player, Faction_Monster));
	TestTrue(TEXT("Monster vs Player hostile"), UCGFCombatStatics::AreHostileFactions(Faction_Monster, Faction_Player));
	TestFalse(TEXT("Same faction not hostile"), UCGFCombatStatics::AreHostileFactions(Faction_Monster, Faction_Monster));
	TestFalse(TEXT("Neutral never hostile (A)"), UCGFCombatStatics::AreHostileFactions(Faction_Neutral, Faction_Player));
	TestFalse(TEXT("Neutral never hostile (B)"), UCGFCombatStatics::AreHostileFactions(Faction_Monster, Faction_Neutral));
	TestFalse(TEXT("Empty faction never hostile"), UCGFCombatStatics::AreHostileFactions(FGameplayTag(), Faction_Player));
	TestFalse(TEXT("Both empty never hostile"), UCGFCombatStatics::AreHostileFactions(FGameplayTag(), FGameplayTag()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCCombat_DamageFormula, "VoxelCharacter.Combat.DamageFormula", VC_COMBAT_FLAGS)
bool FVCCombat_DamageFormula::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Base + AP - Def"), UVCDamageExecution::ComputeDamage(10.f, 2.f, 3.f, false), 9.f);
	TestEqual(TEXT("Floors at zero"), UVCDamageExecution::ComputeDamage(2.f, 0.f, 10.f, false), 0.f);
	TestEqual(TEXT("Pure ignores both"), UVCDamageExecution::ComputeDamage(10.f, 2.f, 3.f, true), 10.f);
	TestEqual(TEXT("Negative base is nothing"), UVCDamageExecution::ComputeDamage(-5.f, 2.f, 0.f, false), 0.f);
	TestEqual(TEXT("Zero base is nothing"), UVCDamageExecution::ComputeDamage(0.f, 50.f, 0.f, false), 0.f);
	return true;
}

// ===========================================================================
// Pipeline (world)
// ===========================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCCombat_DamageReducesHealth, "VoxelCharacter.Combat.DamageReducesHealth", VC_COMBAT_FLAGS)
bool FVCCombat_DamageReducesHealth::RunTest(const FString& Parameters)
{
	FWorldHarness H;
	AVCNPCCharacterBase* Attacker = SpawnCombatant(H.World, CGFGameplayTags::Faction_Player, 100.f, 2.f, 0.f);
	AVCNPCCharacterBase* Target = SpawnCombatant(H.World, CGFGameplayTags::Faction_Monster, 100.f, 0.f, 3.f, EVCOutOfHealthPolicy::Die, FVector(500, 0, 0));
	if (!Attacker || !Target) { AddError(TEXT("Spawn failed")); return false; }

	TestEqual(TEXT("Starts at max"), Target->CombatComponent->GetHealth(), 100.f);

	const ECGFDamageResult Result = UVCCombatStatics::ApplyDamageToActor(Target, Hit(Attacker, 10.f));
	TestEqual(TEXT("Applied"), Result, ECGFDamageResult::Applied);
	TestEqual(TEXT("100 - (10 + 2 - 3)"), Target->CombatComponent->GetHealth(), 91.f);
	TestFalse(TEXT("Still alive"), Target->IsDead());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCCombat_PureIgnoresDefense, "VoxelCharacter.Combat.PureIgnoresDefense", VC_COMBAT_FLAGS)
bool FVCCombat_PureIgnoresDefense::RunTest(const FString& Parameters)
{
	FWorldHarness H;
	AVCNPCCharacterBase* Attacker = SpawnCombatant(H.World, CGFGameplayTags::Faction_Player, 100.f, 2.f, 0.f);
	AVCNPCCharacterBase* Target = SpawnCombatant(H.World, CGFGameplayTags::Faction_Monster, 100.f, 0.f, 3.f, EVCOutOfHealthPolicy::Die, FVector(500, 0, 0));
	if (!Attacker || !Target) { AddError(TEXT("Spawn failed")); return false; }

	UVCCombatStatics::ApplyDamageToActor(Target, Hit(Attacker, 10.f, CGFGameplayTags::Damage_Type_Pure));
	TestEqual(TEXT("Pure: 100 - 10"), Target->CombatComponent->GetHealth(), 90.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCCombat_SelfAppliedIgnoresAttackPower, "VoxelCharacter.Combat.SelfAppliedIgnoresAttackPower", VC_COMBAT_FLAGS)
bool FVCCombat_SelfAppliedIgnoresAttackPower::RunTest(const FString& Parameters)
{
	FWorldHarness H;
	// A hazard with no instigator applies the spec from the target's own ASC; the target's
	// attack power must not inflate the damage it takes.
	AVCNPCCharacterBase* Target = SpawnCombatant(H.World, CGFGameplayTags::Faction_Monster, 100.f, 5.f, 0.f);
	if (!Target) { AddError(TEXT("Spawn failed")); return false; }

	const ECGFDamageResult Result = UVCCombatStatics::ApplyDamageToActor(Target, Hit(nullptr, 10.f));
	TestEqual(TEXT("Hazard applied"), Result, ECGFDamageResult::Applied);
	TestEqual(TEXT("100 - 10, not 100 - 15"), Target->CombatComponent->GetHealth(), 90.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCCombat_HazardHitsNeutral, "VoxelCharacter.Combat.HazardHitsNeutral", VC_COMBAT_FLAGS)
bool FVCCombat_HazardHitsNeutral::RunTest(const FString& Parameters)
{
	FWorldHarness H;
	AVCNPCCharacterBase* Target = SpawnCombatant(H.World, CGFGameplayTags::Faction_Neutral, 100.f, 0.f, 0.f);
	if (!Target) { AddError(TEXT("Spawn failed")); return false; }

	// No instigator → no faction check at all, even against Neutral.
	TestEqual(TEXT("Applied"), UVCCombatStatics::ApplyDamageToActor(Target, Hit(nullptr, 10.f)), ECGFDamageResult::Applied);
	TestEqual(TEXT("90"), Target->CombatComponent->GetHealth(), 90.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCCombat_InvulnerableRejected, "VoxelCharacter.Combat.InvulnerableRejected", VC_COMBAT_FLAGS)
bool FVCCombat_InvulnerableRejected::RunTest(const FString& Parameters)
{
	FWorldHarness H;
	AVCNPCCharacterBase* Attacker = SpawnCombatant(H.World, CGFGameplayTags::Faction_Player, 100.f, 0.f, 0.f);
	AVCNPCCharacterBase* Target = SpawnCombatant(H.World, CGFGameplayTags::Faction_Monster, 100.f, 0.f, 0.f, EVCOutOfHealthPolicy::Die, FVector(500, 0, 0));
	if (!Attacker || !Target) { AddError(TEXT("Spawn failed")); return false; }

	Target->GetAbilitySystemComponent()->AddLooseGameplayTag(CGFGameplayTags::State_Invulnerable);
	TestEqual(TEXT("Rejected"), UVCCombatStatics::ApplyDamageToActor(Target, Hit(Attacker, 10.f)), ECGFDamageResult::Rejected_Invulnerable);
	TestEqual(TEXT("Health untouched"), Target->CombatComponent->GetHealth(), 100.f);

	Target->GetAbilitySystemComponent()->RemoveLooseGameplayTag(CGFGameplayTags::State_Invulnerable);
	TestEqual(TEXT("Applied after removal"), UVCCombatStatics::ApplyDamageToActor(Target, Hit(Attacker, 10.f)), ECGFDamageResult::Applied);
	TestEqual(TEXT("90"), Target->CombatComponent->GetHealth(), 90.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCCombat_FriendlyFireRejected, "VoxelCharacter.Combat.FriendlyFireRejected", VC_COMBAT_FLAGS)
bool FVCCombat_FriendlyFireRejected::RunTest(const FString& Parameters)
{
	FWorldHarness H;
	AVCNPCCharacterBase* Attacker = SpawnCombatant(H.World, CGFGameplayTags::Faction_Monster, 100.f, 0.f, 0.f);
	AVCNPCCharacterBase* Target = SpawnCombatant(H.World, CGFGameplayTags::Faction_Monster, 100.f, 0.f, 0.f, EVCOutOfHealthPolicy::Die, FVector(500, 0, 0));
	AVCNPCCharacterBase* Neutral = SpawnCombatant(H.World, CGFGameplayTags::Faction_Neutral, 100.f, 0.f, 0.f, EVCOutOfHealthPolicy::Die, FVector(1000, 0, 0));
	if (!Attacker || !Target || !Neutral) { AddError(TEXT("Spawn failed")); return false; }

	TestEqual(TEXT("Same faction rejected"), UVCCombatStatics::ApplyDamageToActor(Target, Hit(Attacker, 10.f)), ECGFDamageResult::Rejected_Friendly);
	TestEqual(TEXT("Health untouched"), Target->CombatComponent->GetHealth(), 100.f);

	TestEqual(TEXT("Neutral target rejected"), UVCCombatStatics::ApplyDamageToActor(Neutral, Hit(Attacker, 10.f)), ECGFDamageResult::Rejected_Friendly);

	TestEqual(TEXT("bIgnoreFaction applies"), UVCCombatStatics::ApplyDamageToActor(Target, Hit(Attacker, 10.f, FGameplayTag(), true)), ECGFDamageResult::Applied);
	TestEqual(TEXT("90"), Target->CombatComponent->GetHealth(), 90.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCCombat_LethalHitKillsOnce, "VoxelCharacter.Combat.LethalHitKillsOnce", VC_COMBAT_FLAGS)
bool FVCCombat_LethalHitKillsOnce::RunTest(const FString& Parameters)
{
	FWorldHarness H;
	AVCNPCCharacterBase* Attacker = SpawnCombatant(H.World, CGFGameplayTags::Faction_Player, 100.f, 0.f, 0.f);
	AVCNPCCharacterBase* Target = SpawnCombatant(H.World, CGFGameplayTags::Faction_Monster, 50.f, 0.f, 0.f, EVCOutOfHealthPolicy::Die, FVector(500, 0, 0));
	if (!Attacker || !Target) { AddError(TEXT("Spawn failed")); return false; }

	TestEqual(TEXT("Lethal applied"), UVCCombatStatics::ApplyDamageToActor(Target, Hit(Attacker, 1000.f)), ECGFDamageResult::Applied);
	TestEqual(TEXT("Health clamped to 0"), Target->CombatComponent->GetHealth(), 0.f);
	TestTrue(TEXT("IsDead"), Target->IsDead());
	TestTrue(TEXT("Statics see it dead"), UCGFCombatStatics::IsActorDead(Target));
	TestTrue(TEXT("State.Dead on ASC"), Target->GetAbilitySystemComponent()->HasMatchingGameplayTag(CGFGameplayTags::State_Dead));
	TestFalse(TEXT("Not downed"), Target->CombatComponent->IsDowned());

	TestEqual(TEXT("Second hit rejected"), UVCCombatStatics::ApplyDamageToActor(Target, Hit(Attacker, 10.f)), ECGFDamageResult::Rejected_Dead);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCCombat_DownedPolicyRecoverable, "VoxelCharacter.Combat.DownedPolicyRecoverable", VC_COMBAT_FLAGS)
bool FVCCombat_DownedPolicyRecoverable::RunTest(const FString& Parameters)
{
	FWorldHarness H;
	AVCNPCCharacterBase* Attacker = SpawnCombatant(H.World, CGFGameplayTags::Faction_Player, 100.f, 0.f, 0.f);
	AVCNPCCharacterBase* Target = SpawnCombatant(H.World, CGFGameplayTags::Faction_Monster, 50.f, 0.f, 0.f, EVCOutOfHealthPolicy::Downed, FVector(500, 0, 0));
	if (!Attacker || !Target) { AddError(TEXT("Spawn failed")); return false; }

	UAbilitySystemComponent* ASC = Target->GetAbilitySystemComponent();
	TestEqual(TEXT("Lethal applied"), UVCCombatStatics::ApplyDamageToActor(Target, Hit(Attacker, 1000.f)), ECGFDamageResult::Applied);
	TestFalse(TEXT("Not dead"), Target->IsDead());
	TestTrue(TEXT("Downed"), Target->CombatComponent->IsDowned());
	TestTrue(TEXT("State.Downed on ASC"), ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Downed));
	TestFalse(TEXT("No State.Dead"), ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Dead));

	// Downed combatants can still be hit (finishing blows are a later design decision; today they re-enter the policy
	// only after a revive, so a second lethal hit is Applied but changes no state).
	TestTrue(TEXT("Revive succeeds"), Target->CombatComponent->Revive(0.5f));
	TestFalse(TEXT("Not downed after revive"), Target->CombatComponent->IsDowned());
	TestFalse(TEXT("Tag cleared"), ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Downed));
	TestEqual(TEXT("Health at half"), Target->CombatComponent->GetHealth(), 25.f);

	// Kill() promotes to death regardless of health.
	Target->CombatComponent->Kill(FCGFDamageContext());
	TestTrue(TEXT("Killed"), Target->IsDead());
	TestTrue(TEXT("State.Dead on ASC"), ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Dead));

	// Revive from death restores full health and clears the tag.
	TestTrue(TEXT("Revive from death"), Target->CombatComponent->Revive(1.f));
	TestFalse(TEXT("Alive again"), Target->IsDead());
	TestFalse(TEXT("Dead tag cleared"), ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Dead));
	TestEqual(TEXT("Full health"), Target->CombatComponent->GetHealth(), 50.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCCombat_NoTargetRejected, "VoxelCharacter.Combat.NoTargetRejected", VC_COMBAT_FLAGS)
bool FVCCombat_NoTargetRejected::RunTest(const FString& Parameters)
{
	FWorldHarness H;
	AActor* Plain = H.World->SpawnActor<AActor>();
	TestEqual(TEXT("Plain actor"), UVCCombatStatics::ApplyDamageToActor(Plain, Hit(nullptr, 10.f)), ECGFDamageResult::Rejected_NoTarget);
	TestEqual(TEXT("Null actor"), UVCCombatStatics::ApplyDamageToActor(nullptr, Hit(nullptr, 10.f)), ECGFDamageResult::Rejected_NoTarget);
	TestNull(TEXT("FindDamageable on plain actor"), UCGFCombatStatics::FindDamageable(Plain).GetObject());
	TestFalse(TEXT("Plain actor has no faction"), UCGFCombatStatics::GetFactionTag(Plain).IsValid());
	return true;
}

#undef VC_COMBAT_FLAGS

#endif // WITH_AUTOMATION_TESTS
