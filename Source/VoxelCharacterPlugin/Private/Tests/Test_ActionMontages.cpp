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
#include "Movement/VCMovementComponent.h"
#include "Tags/CGFGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

// ---------------------------------------------------------------------------
// Action montages (character animation P3, CHARACTER_ANIMATION_PLAN.md):
// - The hit time comes from the montage: its first action-hit notify, else a fraction of its length.
// - A set gives each action's montage: the combo wraps, a missing side falls back to the front; a character without a
//   set for its tag plays the Unarmed one.
// - The side a hit comes from, about local up, on a plane and on a tilted frame.
// - The combo (the sword attack flow plan, D104): a press is nothing before the strike, kept until the recovery, the
//   next attack from the recovery to the window past the end; the weapon's speed is the play rate (D107); the recovery's
//   time comes from its marker (D102); movement breaks only the recovery (D103); the server takes the owner's pick when
//   its own rule allows it and never picks twice on a machine that is both (G179); root motion only standing (D106).
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

	/** A transient attack montage of a length with its hit and its recovery (the sword attack flow plan, D102) marked. */
	UAnimMontage* MakeAttack(float Length, float HitTime, float RecoveryTime)
	{
		UAnimMontage* Montage = MakeMontage(Length, { HitTime });
		FAnimNotifyEvent Event;
		Event.Notify = NewObject<UVCAnimNotify_ActionRecovery>(Montage);
		Event.Link(Montage, RecoveryTime);
		Event.TriggerTimeOffset = 0.f;
		Montage->Notifies.Add(Event);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_RecoveryTime, "VoxelCharacter.Anim.RecoveryTime", VC_ACTION_FLAGS)
bool FVCAnim_RecoveryTime::RunTest(const FString& Parameters)
{
	using namespace VCActionMontageTestHelpers;
	TestEqual(TEXT("No montage: 0"), UVCActionAnimSet::GetRecoveryTime(nullptr), 0.f);
	TestTrue(TEXT("The marker's time"), FMath::IsNearlyEqual(UVCActionAnimSet::GetRecoveryTime(MakeAttack(1.67f, 0.41f, 0.65f)), 0.65f, 1.0e-4f));
	TestTrue(TEXT("No marker: the end (no recovery to break or chain from)"), FMath::IsNearlyEqual(UVCActionAnimSet::GetRecoveryTime(MakeMontage(1.2f, { 0.4f })), 1.2f, 1.0e-4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_AttackPressRule, "VoxelCharacter.Anim.AttackPressRule", VC_ACTION_FLAGS)
bool FVCAnim_AttackPressRule::RunTest(const FString& Parameters)
{
	// An attack hitting at 0.4 s, recovering from 0.65 s, ending at 1.67 s, the combo open 0.2 s past its end.
	const auto Press = [](double Since) { return UVCActionMontageComponent::ClassifyAttackPress(Since, 0.4f, 0.65f, 1.67f, 0.2f); };
	TestTrue(TEXT("No attack: a new combo"), Press(-1.0) == EVCAttackPress::NewCombo);
	TestTrue(TEXT("In the wind-up: nothing"), Press(0.2) == EVCAttackPress::Ignore);
	TestTrue(TEXT("Just past the strike: kept"), Press(0.41) == EVCAttackPress::Buffer);
	TestTrue(TEXT("In the follow-through: kept"), Press(0.6) == EVCAttackPress::Buffer);
	TestTrue(TEXT("Just past the recovery: the next"), Press(0.66) == EVCAttackPress::Continue);
	TestTrue(TEXT("Just past the end: the next"), Press(1.8) == EVCAttackPress::Continue);
	TestTrue(TEXT("Past the window: a new combo"), Press(1.9) == EVCAttackPress::NewCombo);

	const auto Break = [](double Since, bool bMove) { return UVCActionMontageComponent::ShouldBreakRecovery(Since, 0.65f, 1.67f, bMove); };
	TestFalse(TEXT("Moving in the swing: the swing plays"), Break(0.3, true));
	TestFalse(TEXT("Moving in the follow-through: it plays"), Break(0.6, true));
	TestTrue(TEXT("Moving in the recovery: broken off"), Break(0.7, true));
	TestFalse(TEXT("Standing in the recovery: it plays out"), Break(0.7, false));
	TestFalse(TEXT("After the end: nothing to break"), Break(1.7, true));

	TestTrue(TEXT("Attack speed 1: as authored"), FMath::IsNearlyEqual(UVCActionMontageComponent::GetAttackPlayRate(1.f), 1.f));
	TestTrue(TEXT("Attack speed 1.1: 10% fast"), FMath::IsNearlyEqual(UVCActionMontageComponent::GetAttackPlayRate(1.1f), 1.1f));
	TestTrue(TEXT("Clamped at 2"), FMath::IsNearlyEqual(UVCActionMontageComponent::GetAttackPlayRate(5.f), 2.f));
	TestTrue(TEXT("Clamped at 0.5"), FMath::IsNearlyEqual(UVCActionMontageComponent::GetAttackPlayRate(0.1f), 0.5f));

	TestTrue(TEXT("Root motion standing"), UVCMovementComponent::ShouldTakeMontageRootMotion(true, false));
	TestFalse(TEXT("No root motion moving"), UVCMovementComponent::ShouldTakeMontageRootMotion(true, true));
	TestTrue(TEXT("Switched off: root motion moving too"), UVCMovementComponent::ShouldTakeMontageRootMotion(false, true));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_AttackCombo, "VoxelCharacter.Anim.AttackCombo", VC_ACTION_FLAGS)
bool FVCAnim_AttackCombo::RunTest(const FString& Parameters)
{
	using namespace VCActionMontageTestHelpers;
	VCTesting::FPlayWorld W(TEXT("VCAttackComboWorld"), /*bDestroyActorsFirst=*/true);
	// Three attacks of 1 s, each hitting at 0.25 s and recovering from 0.4 s; the combo open 0.2 s past the end.
	UVCActionAnimSet* Unarmed = MakeSet(VCGameplayTags::Equipment_AnimSet_Unarmed);
	Unarmed->AttackCombo = { MakeAttack(1.f, 0.25f, 0.4f), MakeAttack(1.f, 0.25f, 0.4f), MakeAttack(1.f, 0.25f, 0.4f) };
	Unarmed->ComboWindow = 0.2f;
	AVCNPCCharacterBase* NPC = SpawnFighter(W.World, CGFGameplayTags::Faction_Monster, FVector::ZeroVector, false, Unarmed);
	if (!TestNotNull(TEXT("NPC"), NPC))
	{
		return false;
	}
	UVCActionMontageComponent* Montages = NPC->ActionMontages;
	const auto Advance = [&W](float Seconds)
	{
		for (float Elapsed = 0.f; Elapsed < Seconds - UE_KINDA_SMALL_NUMBER; Elapsed += 0.05f)
		{
			TickFrame(W.World, 0.05f);
		}
	};

	float Rate = 0.f;
	TestTrue(TEXT("Before any attack: a new combo"), Montages->ClassifyAttackPress() == EVCAttackPress::NewCombo);
	TestEqual(TEXT("The first attack opens the combo"), Montages->ChooseAttack(1.f, Rate), 0);
	TestTrue(TEXT("At attack speed 1, as authored"), FMath::IsNearlyEqual(Rate, 1.f, 1.0e-4f));
	Advance(0.1f);
	TestTrue(TEXT("In its wind-up: nothing"), Montages->ClassifyAttackPress() == EVCAttackPress::Ignore);
	Advance(0.2f);
	TestTrue(TEXT("After its strike: kept"), Montages->ClassifyAttackPress() == EVCAttackPress::Buffer);
	Montages->BufferAttack();
	TestFalse(TEXT("Kept, not yet due"), Montages->TakeDueBufferedAttack());
	Advance(0.15f);
	TestTrue(TEXT("Due at the recovery"), Montages->TakeDueBufferedAttack());
	TestFalse(TEXT("Taken once"), Montages->TakeDueBufferedAttack());
	TestEqual(TEXT("The kept press: the next"), Montages->ChooseAttack(1.f, Rate), 1);
	Advance(0.45f);
	TestTrue(TEXT("In its recovery: the next at once"), Montages->ClassifyAttackPress() == EVCAttackPress::Continue);
	TestEqual(TEXT("And the next"), Montages->ChooseAttack(1.f, Rate), 2);
	Advance(0.45f);
	TestEqual(TEXT("After the last: the first again"), Montages->ChooseAttack(1.f, Rate), 0);

	// Twice as fast: every time halves (the recovery at 0.2 s).
	Advance(1.5f);
	TestEqual(TEXT("Past the window: the first"), Montages->ChooseAttack(2.f, Rate), 0);
	TestTrue(TEXT("At attack speed 2, twice as fast"), FMath::IsNearlyEqual(Rate, 2.f, 1.0e-4f));
	Advance(0.25f);
	TestTrue(TEXT("Its recovery at half the time"), Montages->ClassifyAttackPress() == EVCAttackPress::Continue);
	Advance(0.5f);
	TestTrue(TEXT("Its window past half the length"), Montages->ClassifyAttackPress() == EVCAttackPress::NewCombo);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCAnim_AcceptAttack, "VoxelCharacter.Combat.Melee.AcceptAttack", VC_ACTION_FLAGS)
bool FVCAnim_AcceptAttack::RunTest(const FString& Parameters)
{
	using namespace VCActionMontageTestHelpers;
	VCTesting::FPlayWorld W(TEXT("VCAcceptAttackWorld"), /*bDestroyActorsFirst=*/true);
	UVCActionAnimSet* Unarmed = MakeSet(VCGameplayTags::Equipment_AnimSet_Unarmed);
	Unarmed->AttackCombo = { MakeAttack(1.f, 0.1f, 0.6f), MakeAttack(1.f, 0.1f, 0.6f), MakeAttack(1.f, 0.1f, 0.6f) };
	AVCNPCCharacterBase* NPC = SpawnFighter(W.World, CGFGameplayTags::Faction_Player, FVector::ZeroVector, true, Unarmed);
	if (!TestNotNull(TEXT("Fighter"), NPC))
	{
		return false;
	}
	UVCActionMontageComponent* Montages = NPC->ActionMontages;
	UAbilitySystemComponent* ASC = NPC->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("ASC"), ASC))
	{
		return false;
	}
	const FGameplayTagContainer Melee(CGFGameplayTags::Ability_Attack_Melee);
	const auto Advance = [&W](float Seconds)
	{
		for (float Elapsed = 0.f; Elapsed < Seconds - UE_KINDA_SMALL_NUMBER; Elapsed += 0.05f)
		{
			TickFrame(W.World, 0.05f);
		}
	};
	const auto Request = [&](int32 Index)
	{
		Montages->SetRequestedAttack(Index);
		return ASC->TryActivateAbilitiesByTag(Melee);
	};

	// G179: the owner's pick on a machine that is also the server is the attack the server takes, not a second pick.
	float Rate = 0.f;
	TestEqual(TEXT("The owner picks the first"), Montages->ChooseAttack(1.f, Rate), 0);
	TestTrue(TEXT("The server takes it"), Request(0));
	TestEqual(TEXT("Still the first"), Montages->GetLastAttackIndex(), 0);
	Advance(0.65f);
	TestEqual(TEXT("The owner's next"), Montages->ChooseAttack(1.f, Rate), 1);
	TestTrue(TEXT("Taken"), Request(1));
	TestEqual(TEXT("The second, not the third"), Montages->GetLastAttackIndex(), 1);

	// A request in the swing is refused (the swing before ended its ability at its hit, 0.1 s): the attack before has not
	// reached its recovery (less the allowance).
	Advance(0.2f);
	const int32 Played = Montages->GetActionsPlayed();
	Request(2);
	TestEqual(TEXT("Too early: no attack"), Montages->GetActionsPlayed(), Played);
	TestEqual(TEXT("Too early: the index kept"), Montages->GetLastAttackIndex(), 1);

	// Within the allowance of the recovery the request is taken; an index the rule does not allow is the server's own.
	Advance(0.25f);
	TestTrue(TEXT("Near the recovery, an index out of the combo"), Request(7));
	TestEqual(TEXT("The server's own: the next"), Montages->GetLastAttackIndex(), 2);
	Advance(0.65f);
	TestTrue(TEXT("The owner starts over"), Request(0));
	TestEqual(TEXT("A new combo is the owner's to start"), Montages->GetLastAttackIndex(), 0);
	Advance(0.65f);
	TestTrue(TEXT("No request (an NPC)"), Request(-1));
	TestEqual(TEXT("The server picks the next"), Montages->GetLastAttackIndex(), 1);
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
