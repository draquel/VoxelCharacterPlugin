// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Testing/VCTestWorld.h"

#if WITH_AUTOMATION_TESTS

#include "Animation/VCActionAnimSet.h"
#include "Animation/VCActionMontageComponent.h"
#include "Animation/AnimMontage.h"
#include "Combat/VCCombatComponent.h"
#include "Combat/VCMeleeAttackAbility.h"
#include "Core/VCGameplayTags.h"
#include "Core/VCNPCCharacterBase.h"
#include "Tags/CGFGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

// ---------------------------------------------------------------------------
// Action montages (character animation P3, CHARACTER_ANIMATION_PLAN.md):
// - The hit time comes from the montage: its first action-hit notify, else a fraction of its length.
// - A set gives each action's montage: the combo wraps, a missing side falls back to the front; a character without a
//   set for its tag plays the Unarmed one.
// - The side a hit comes from, about local up, on a plane and on a tilted frame.
// - The combo advances within its window and the play rate fits a swing into one attack's interval.
// - The melee ability lands its damage at the montage's hit time, not at activation, and immediately without a montage;
//   the victim flinches through its own component.
// ---------------------------------------------------------------------------

namespace VCActionMontageTestHelpers
{
	/** A transient montage of a length, with action-hit notifies at the given times. */
	UAnimMontage* MakeMontage(float Length, const TArray<float>& HitTimes = TArray<float>())
	{
		UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage());
		Montage->SetCompositeLength(Length);
		for (const float Time : HitTimes)
		{
			FAnimNotifyEvent Event;
			Event.Notify = NewObject<UVCAnimNotify_ActionHit>(Montage);
			Event.Link(Montage, Time);
			Event.TriggerTimeOffset = 0.f;
			Montage->Notifies.Add(Event);
		}
		return Montage;
	}

	/** One world tick as its own engine frame: the timer manager (the ability's wait) ticks once a frame. */
	void TickFrame(UWorld* World, float DeltaSeconds)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, DeltaSeconds);
	}

	UVCActionAnimSet* MakeSet(const FGameplayTag& Tag)
	{
		UVCActionAnimSet* Set = NewObject<UVCActionAnimSet>(GetTransientPackage());
		Set->ActionSet = Tag;
		return Set;
	}

	AVCNPCCharacterBase* SpawnFighter(UWorld* World, const FGameplayTag& Faction, const FVector& Location, bool bMelee, UVCActionAnimSet* Set)
	{
		const FTransform Transform(Location);
		AVCNPCCharacterBase* NPC = World->SpawnActorDeferred<AVCNPCCharacterBase>(AVCNPCCharacterBase::StaticClass(), Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!NPC)
		{
			return nullptr;
		}
		NPC->StartingMaxHealth = 100.f;
		NPC->DestroyAfterDeathDelay = 0.f;
		NPC->bAIEnabled = false;
		NPC->CombatComponent->FactionTag = Faction;
		if (bMelee)
		{
			NPC->DefaultAbilities.Add(UVCMeleeAttackAbility::StaticClass());
		}
		if (Set)
		{
			NPC->ActionMontages->ActionAnimSets.Add(Set);
		}
		NPC->FinishSpawning(Transform);
		if (UCharacterMovementComponent* Move = NPC->GetCharacterMovement())
		{
			Move->DisableMovement();
		}
		return NPC;
	}
}

#define VC_ACTION_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_ActionHitTime, "VoxelCharacter.Anim.ActionHitTime", VC_ACTION_FLAGS)
bool FVCAnim_ActionHitTime::RunTest(const FString& Parameters)
{
	using namespace VCActionMontageTestHelpers;
	TestEqual(TEXT("No montage: 0"), UVCActionAnimSet::GetHitTime(nullptr), 0.f);
	TestTrue(TEXT("The notify's time"), FMath::IsNearlyEqual(UVCActionAnimSet::GetHitTime(MakeMontage(1.2f, { 0.45f })), 0.45f, 1.0e-4f));
	TestTrue(TEXT("The earliest of two"), FMath::IsNearlyEqual(UVCActionAnimSet::GetHitTime(MakeMontage(1.2f, { 0.9f, 0.3f })), 0.3f, 1.0e-4f));
	TestTrue(TEXT("No notify: 40% of the length"), FMath::IsNearlyEqual(UVCActionAnimSet::GetHitTime(MakeMontage(2.f)), 0.8f, 1.0e-4f));
	TestTrue(TEXT("No notify, another fraction"), FMath::IsNearlyEqual(UVCActionAnimSet::GetHitTime(MakeMontage(2.f), 0.25f), 0.5f, 1.0e-4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_ActionSetLookup, "VoxelCharacter.Anim.ActionSetLookup", VC_ACTION_FLAGS)
bool FVCAnim_ActionSetLookup::RunTest(const FString& Parameters)
{
	using namespace VCActionMontageTestHelpers;
	UVCActionAnimSet* Sword = MakeSet(VCGameplayTags::Equipment_AnimSet_SwordShield);
	UAnimMontage* A = MakeMontage(1.f);
	UAnimMontage* B = MakeMontage(1.f);
	UAnimMontage* Front = MakeMontage(0.5f);
	UAnimMontage* Back = MakeMontage(0.5f);
	Sword->AttackCombo = { A, B };
	Sword->HitReact.Front = Front;
	Sword->HitReact.Back = Back;
	Sword->Death.Front = Front;

	TestTrue(TEXT("Combo 0"), Sword->GetMontage(EVCAnimAction::Attack, 0) == A);
	TestTrue(TEXT("Combo 1"), Sword->GetMontage(EVCAnimAction::Attack, 1) == B);
	TestTrue(TEXT("The combo wraps"), Sword->GetMontage(EVCAnimAction::Attack, 2) == A);
	TestTrue(TEXT("Hit from behind"), Sword->GetMontage(EVCAnimAction::HitReact, int32(EVCHitDirection::Back)) == Back);
	TestTrue(TEXT("No left: the front"), Sword->GetMontage(EVCAnimAction::HitReact, int32(EVCHitDirection::Left)) == Front);
	TestTrue(TEXT("Death from the right: the front"), Sword->GetMontage(EVCAnimAction::Death, int32(EVCHitDirection::Right)) == Front);
	TestNull(TEXT("No dig in this set"), Sword->GetMontage(EVCAnimAction::Dig));

	UVCActionAnimSet* Unarmed = MakeSet(VCGameplayTags::Equipment_AnimSet_Unarmed);
	UVCActionMontageComponent* Component = NewObject<UVCActionMontageComponent>(GetTransientPackage());
	Component->ActionAnimSets = { Sword, Unarmed };
	TestTrue(TEXT("The tag's set"), Component->FindActionAnimSet(VCGameplayTags::Equipment_AnimSet_SwordShield) == Sword);
	TestTrue(TEXT("A tag with no set: Unarmed"), Component->FindActionAnimSet(VCGameplayTags::Equipment_AnimSet_Bow) == Unarmed);
	Component->ActionAnimSets = { Sword };
	TestNull(TEXT("No Unarmed set: nothing"), Component->FindActionAnimSet(VCGameplayTags::Equipment_AnimSet_Bow));

	// The side of a hit, on a plane and on a frame tilted to a planet's 60 degrees south, twisted about up.
	const FQuat Frames[] = { FQuat::Identity, FQuat(FVector(0.3, -0.8, 0.5).GetSafeNormal(), FMath::DegreesToRadians(150.0)) };
	int32 Wrong = 0;
	for (const FQuat& Q : Frames)
	{
		const FVector Up = Q.RotateVector(FVector::UpVector);
		const FVector Forward = Q.RotateVector(FVector::ForwardVector);
		const auto From = [&Q](double Yaw) { return Q.RotateVector(FRotator(0.0, Yaw, 0.0).Vector() * 300.0 + FVector(0, 0, 80)); };
		Wrong += UVCActionAnimSet::DirectionOfHit(Forward, Up, From(10.0)) != EVCHitDirection::Front ? 1 : 0;
		Wrong += UVCActionAnimSet::DirectionOfHit(Forward, Up, From(-170.0)) != EVCHitDirection::Back ? 1 : 0;
		Wrong += UVCActionAnimSet::DirectionOfHit(Forward, Up, From(90.0)) != EVCHitDirection::Right ? 1 : 0;
		Wrong += UVCActionAnimSet::DirectionOfHit(Forward, Up, From(-60.0)) != EVCHitDirection::Left ? 1 : 0;
	}
	TestEqual(TEXT("Hit sides on a plane and a tilted frame"), Wrong, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_AttackCombo, "VoxelCharacter.Anim.AttackCombo", VC_ACTION_FLAGS)
bool FVCAnim_AttackCombo::RunTest(const FString& Parameters)
{
	using namespace VCActionMontageTestHelpers;
	VCTesting::FPlayWorld W(TEXT("VCAttackComboWorld"), /*bDestroyActorsFirst=*/true);
	UVCActionAnimSet* Unarmed = MakeSet(VCGameplayTags::Equipment_AnimSet_Unarmed);
	Unarmed->AttackCombo = { MakeMontage(2.f), MakeMontage(0.5f), MakeMontage(5.f) };
	Unarmed->ComboWindow = 0.5f;
	AVCNPCCharacterBase* NPC = SpawnFighter(W.World, CGFGameplayTags::Faction_Monster, FVector::ZeroVector, false, Unarmed);
	if (!TestNotNull(TEXT("NPC"), NPC))
	{
		return false;
	}
	UVCActionMontageComponent* Montages = NPC->ActionMontages;

	float Rate = 0.f;
	TestEqual(TEXT("The first attack opens the combo"), Montages->ChooseAttack(1.f, Rate), 0);
	TestTrue(TEXT("A 2 s swing at one attack a second plays at 2x"), FMath::IsNearlyEqual(Rate, 2.f, 1.0e-4f));
	TestEqual(TEXT("Within the window: the next"), Montages->ChooseAttack(1.f, Rate), 1);
	TestTrue(TEXT("A 0.5 s swing plays at its own pace"), FMath::IsNearlyEqual(Rate, 1.f, 1.0e-4f));
	TestEqual(TEXT("And the next"), Montages->ChooseAttack(1.f, Rate), 2);
	TestTrue(TEXT("A 5 s swing is capped at 3x"), FMath::IsNearlyEqual(Rate, 3.f, 1.0e-4f));

	// Past the window (the 5 s swing at 3x is 1.67 s, plus 0.5): the combo starts over.
	for (int32 i = 0; i < 25; ++i)
	{
		TickFrame(W.World, 0.1f);
	}
	TestEqual(TEXT("After the window: the first again"), Montages->ChooseAttack(1.f, Rate), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_MeleeHitTime, "VoxelCharacter.Anim.MeleeHitTime", VC_ACTION_FLAGS)
bool FVCAnim_MeleeHitTime::RunTest(const FString& Parameters)
{
	using namespace VCActionMontageTestHelpers;
	VCTesting::FPlayWorld W(TEXT("VCMeleeHitTimeWorld"), /*bDestroyActorsFirst=*/true);

	// A swing of 1 s that lands at 0.3 s; the victim's set has a hit react.
	UVCActionAnimSet* AttackerSet = MakeSet(VCGameplayTags::Equipment_AnimSet_Unarmed);
	AttackerSet->AttackCombo = { MakeMontage(1.f, { 0.3f }) };
	UVCActionAnimSet* VictimSet = MakeSet(VCGameplayTags::Equipment_AnimSet_Unarmed);
	VictimSet->HitReact.Front = MakeMontage(0.6f);

	AVCNPCCharacterBase* Attacker = SpawnFighter(W.World, CGFGameplayTags::Faction_Player, FVector::ZeroVector, true, AttackerSet);
	AVCNPCCharacterBase* Victim = SpawnFighter(W.World, CGFGameplayTags::Faction_Monster, FVector(150.0, 0.0, 0.0), false, VictimSet);
	if (!TestNotNull(TEXT("Attacker"), Attacker) || !TestNotNull(TEXT("Victim"), Victim))
	{
		return false;
	}
	Victim->SetActorRotation(FRotator(0.0, 180.0, 0.0)); // facing the attacker

	UAbilitySystemComponent* ASC = Attacker->GetAbilitySystemComponent();
	const float Before = Victim->CombatComponent->GetHealth();
	TestTrue(TEXT("The swing starts"), ASC && ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(CGFGameplayTags::Ability_Attack_Melee)));
	TestEqual(TEXT("The attacker's montage played"), Attacker->ActionMontages->GetActionsPlayed(), 1);
	TestEqual(TEXT("No damage at the swing's start"), Victim->CombatComponent->GetHealth(), Before);

	double Elapsed = 0.0;
	for (; Elapsed < 0.2; Elapsed += 0.05)
	{
		TickFrame(W.World, 0.05f);
	}
	TestEqual(TEXT("No damage before the hit time"), Victim->CombatComponent->GetHealth(), Before);
	for (; Elapsed < 0.4; Elapsed += 0.05)
	{
		TickFrame(W.World, 0.05f);
	}
	const float After = Victim->CombatComponent->GetHealth();
	AddInfo(FString::Printf(TEXT("Health %.1f -> %.1f by %.2f s"), Before, After, Elapsed));
	TestTrue(TEXT("Damage at the hit time"), After < Before);
	TestEqual(TEXT("The victim flinched"), Victim->ActionMontages->GetActionsPlayed(), 1);
	TestTrue(TEXT("From the front"), Victim->ActionMontages->GetLastAction() == EVCAnimAction::HitReact);

	// Without a montage the hit lands at once, as before P3 (and as for NPCs, which list no sets yet).
	AVCNPCCharacterBase* Plain = SpawnFighter(W.World, CGFGameplayTags::Faction_Player, FVector(0.0, 600.0, 0.0), true,
		MakeSet(VCGameplayTags::Equipment_AnimSet_Unarmed));
	AVCNPCCharacterBase* PlainVictim = SpawnFighter(W.World, CGFGameplayTags::Faction_Monster, FVector(150.0, 600.0, 0.0), false, nullptr);
	if (!TestNotNull(TEXT("Plain attacker"), Plain) || !TestNotNull(TEXT("Plain victim"), PlainVictim))
	{
		return false;
	}
	const float PlainBefore = PlainVictim->CombatComponent->GetHealth();
	UAbilitySystemComponent* PlainASC = Plain->GetAbilitySystemComponent();
	TestTrue(TEXT("The plain swing starts"), PlainASC && PlainASC->TryActivateAbilitiesByTag(FGameplayTagContainer(CGFGameplayTags::Ability_Attack_Melee)));
	TestTrue(TEXT("No montage: damage at once"), PlainVictim->CombatComponent->GetHealth() < PlainBefore);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
