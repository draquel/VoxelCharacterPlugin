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
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"

// ---------------------------------------------------------------------------
// The sword's moves (Documentation/SWORD_MOVES_PLAN.md):
// - B, the secondary branch (D166): the combo's table walked by both buttons in every order, the press classes on a
//   secondary step, the server's acceptance of a secondary index and its own pick for the button pressed.
// - C, the charge (D167): from rest only (not in a combo, not sprinting, not in the air); the charge time from the
//   montage's windup; a release before it plays the first attack, at it the charged strike; the server takes the charged
//   strike only once its own clock has run the charge time less the allowance; the strike deals twice the damage and a
//   press in its recovery continues with _02.
// - D, the run attack (D168): a fresh primary press while sprinting faster than the set's speed plays _12 instead of the
//   combo, never a kept press; the server takes it only while its own character sprints that fast; a press after it
//   starts the combo again; its montage carries the capsule on the move.
// ---------------------------------------------------------------------------

namespace VCSwordMovesTestHelpers
{
	/** A transient attack montage of a length with its hit and its recovery marked. */
	UAnimMontage* MakeSwordAttack(float Length, float HitTime, float RecoveryTime)
	{
		UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage());
		Montage->SetCompositeLength(Length);
		const auto AddNotify = [Montage](UAnimNotify* Notify, float Time)
		{
			FAnimNotifyEvent Event;
			Event.Notify = Notify;
			Event.Link(Montage, Time);
			Event.TriggerTimeOffset = 0.f;
			Montage->Notifies.Add(Event);
		};
		AddNotify(NewObject<UVCAnimNotify_ActionHit>(Montage), HitTime);
		AddNotify(NewObject<UVCAnimNotify_ActionRecovery>(Montage), RecoveryTime);
		return Montage;
	}

	/**
	 * The SwordShield set's shape (the layers script's table): _01, _02, _04 on the primary, _08, _09, _10 on the
	 * secondary, every attack 1 s long, hitting at 0.25 s and recovering from 0.4 s.
	 */
	UVCActionAnimSet* MakeBranchedSet()
	{
		UVCActionAnimSet* Set = NewObject<UVCActionAnimSet>(GetTransientPackage());
		Set->ActionSet = VCGameplayTags::Equipment_AnimSet_Unarmed;
		for (int32 i = 0; i < 6; ++i)
		{
			Set->AttackCombo.Add(MakeSwordAttack(1.f, 0.25f, 0.4f));
		}
		const int32 Steps[6][2] = { { 1, 4 }, { 2, 5 }, { 0, 3 }, { 1, 4 }, { 2, 5 }, { 0, 3 } };
		for (const auto& Step : Steps)
		{
			FVCAttackComboStep& Added = Set->AttackComboSteps.AddDefaulted_GetRef();
			Added.NextPrimary = Step[0];
			Added.NextSecondary = Step[1];
		}
		Set->SecondaryAttackStart = 3;
		Set->ComboWindow = 0.2f;
		return Set;
	}

	/**
	 * The branched set with the SwordShield set's charge: the windup 0.5 s, the held loop 1.5 s, the lowering 1 s (one
	 * montage, three sections), and the charged strike _15 (combo index 6: 3 s, hitting at 0.8 s, recovering from 1.27 s)
	 * at twice the damage, continuing with _02.
	 */
	UVCActionAnimSet* MakeChargingSet()
	{
		UVCActionAnimSet* Set = MakeBranchedSet();
		Set->AttackCombo.Add(MakeSwordAttack(3.f, 0.8f, 1.27f));
		FVCAttackComboStep& Charged = Set->AttackComboSteps.AddDefaulted_GetRef();
		Charged.NextPrimary = 1;
		Charged.NextSecondary = 3;
		Charged.DamageMultiplier = 2.f;
		Set->ChargedAttack = 6;
		Set->ChargeHoldTime = 1.f;
		UAnimMontage* Charge = NewObject<UAnimMontage>(GetTransientPackage());
		Charge->SetCompositeLength(3.f);
		const auto AddSection = [Charge](FName Name, float Time)
		{
			FCompositeSection Section;
			Section.SectionName = Name;
			Section.Link(Charge, Time);
			Charge->CompositeSections.Add(Section);
		};
		AddSection(TEXT("Default"), 0.f);
		AddSection(UVCActionAnimSet::ChargeLoopSection, 0.5f);
		AddSection(UVCActionAnimSet::ChargeEndSection, 2.f);
		Set->Charge = Charge;
		return Set;
	}

	/**
	 * The charging set with the SwordShield set's run attack: _12 (combo index 7: 2.5 s, hitting at 0.77 s, recovering
	 * from 1.07 s), a press after it starting the combo again (_01 on the primary, _08 on the secondary).
	 */
	UVCActionAnimSet* MakeRunningSet()
	{
		UVCActionAnimSet* Set = MakeChargingSet();
		Set->AttackCombo.Add(MakeSwordAttack(2.5f, 0.77f, 1.07f));
		FVCAttackComboStep& Run = Set->AttackComboSteps.AddDefaulted_GetRef();
		Run.NextPrimary = 0;
		Run.NextSecondary = 3;
		Set->RunAttack = 7;
		return Set;
	}

	void TickSwordFrame(UWorld* World, float DeltaSeconds)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, DeltaSeconds);
	}

	AVCNPCCharacterBase* SpawnSwordFighter(UWorld* World, UVCActionAnimSet* Set, bool bMelee,
		const FVector& Location = FVector::ZeroVector, const FGameplayTag& Faction = CGFGameplayTags::Faction_Player)
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

#define VC_SWORD_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSword_ComboTable, "VoxelCharacter.Anim.Sword.ComboTable", VC_SWORD_FLAGS)
bool FVCSword_ComboTable::RunTest(const FString& Parameters)
{
	using namespace VCSwordMovesTestHelpers;
	const UVCActionAnimSet* Set = MakeBranchedSet();

	TestEqual(TEXT("The primary starts at _01"), Set->GetFirstAttack(false), 0);
	TestEqual(TEXT("The secondary starts at _08"), Set->GetFirstAttack(true), 3);

	// Every order of the two buttons, four presses from rest, each continuing the combo: 'P' the primary, 'S' the secondary.
	// The combo indices are _01 0, _02 1, _04 2, _08 3, _09 4, _10 5.
	const auto Walk = [Set](const TCHAR* Presses)
	{
		TArray<int32> Played;
		int32 Current = -1;
		for (const TCHAR* Press = Presses; *Press; ++Press)
		{
			const bool bSecondary = *Press == TEXT('S');
			Current = Current < 0 ? Set->GetFirstAttack(bSecondary) : Set->GetNextAttack(Current, bSecondary);
			Played.Add(Current);
		}
		return Played;
	};
	struct FWalkCase { const TCHAR* Presses; TArray<int32> Played; };
	const FWalkCase Expected[] = {
		{ TEXT("PPPP"), { 0, 1, 2, 0 } }, { TEXT("PPPS"), { 0, 1, 2, 3 } }, { TEXT("PPSP"), { 0, 1, 5, 0 } }, { TEXT("PPSS"), { 0, 1, 5, 3 } },
		{ TEXT("PSPP"), { 0, 4, 2, 0 } }, { TEXT("PSPS"), { 0, 4, 2, 3 } }, { TEXT("PSSP"), { 0, 4, 5, 0 } }, { TEXT("PSSS"), { 0, 4, 5, 3 } },
		{ TEXT("SPPP"), { 3, 1, 2, 0 } }, { TEXT("SPPS"), { 3, 1, 2, 3 } }, { TEXT("SPSP"), { 3, 1, 5, 0 } }, { TEXT("SPSS"), { 3, 1, 5, 3 } },
		{ TEXT("SSPP"), { 3, 4, 2, 0 } }, { TEXT("SSPS"), { 3, 4, 2, 3 } }, { TEXT("SSSP"), { 3, 4, 5, 0 } }, { TEXT("SSSS"), { 3, 4, 5, 3 } },
	};
	for (const FWalkCase& Case : Expected)
	{
		TestEqual(FString::Printf(TEXT("%s walks the table"), Case.Presses), Walk(Case.Presses), Case.Played);
	}

	for (int32 Index = 0; Index < 6; ++Index)
	{
		TestEqual(FString::Printf(TEXT("Attack %d is the secondary's"), Index), Set->IsSecondaryAttack(Index), Index >= 3);
	}
	TestFalse(TEXT("No attack is out of the combo's"), Set->IsSecondaryAttack(9));
	TestTrue(TEXT("No step's multiplier: 1"), FMath::IsNearlyEqual(Set->GetAttackDamageMultiplier(0), 1.f));

	// The other sets keep their list: no table, no secondary.
	UVCActionAnimSet* List = NewObject<UVCActionAnimSet>(GetTransientPackage());
	List->AttackCombo = { MakeSwordAttack(1.f, 0.25f, 0.4f), MakeSwordAttack(1.f, 0.25f, 0.4f), MakeSwordAttack(1.f, 0.25f, 0.4f) };
	TestEqual(TEXT("A list: the next"), List->GetNextAttack(1, false), 2);
	TestEqual(TEXT("A list wraps"), List->GetNextAttack(2, false), 0);
	TestEqual(TEXT("A list: no secondary to start"), List->GetFirstAttack(true), -1);
	TestEqual(TEXT("A list: no secondary to continue"), List->GetNextAttack(0, true), -1);
	TestFalse(TEXT("A list: no secondary attack"), List->IsSecondaryAttack(1));
	TestEqual(TEXT("No combo: nothing"), NewObject<UVCActionAnimSet>(GetTransientPackage())->GetFirstAttack(false), -1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSword_SecondaryPress, "VoxelCharacter.Anim.Sword.SecondaryPress", VC_SWORD_FLAGS)
bool FVCSword_SecondaryPress::RunTest(const FString& Parameters)
{
	using namespace VCSwordMovesTestHelpers;
	VCTesting::FPlayWorld W(TEXT("VCSwordSecondaryPressWorld"), /*bDestroyActorsFirst=*/true);
	AVCNPCCharacterBase* NPC = SpawnSwordFighter(W.World, MakeBranchedSet(), false);
	if (!TestNotNull(TEXT("Fighter"), NPC))
	{
		return false;
	}
	UVCActionMontageComponent* Montages = NPC->ActionMontages;
	const auto Advance = [&W](float Seconds)
	{
		for (float Elapsed = 0.f; Elapsed < Seconds - UE_KINDA_SMALL_NUMBER; Elapsed += 0.05f)
		{
			TickSwordFrame(W.World, 0.05f);
		}
	};

	// The plan's walk: _01, the secondary, the primary plays _01, _09, _04.
	float Rate = 0.f;
	TestEqual(TEXT("The primary from rest: _01"), Montages->ChooseAttack(1.f, Rate), 0);
	Advance(0.1f);
	TestTrue(TEXT("In its wind-up: nothing"), Montages->ClassifyAttackPress() == EVCAttackPress::Ignore);
	Advance(0.2f);
	TestTrue(TEXT("After its strike: kept"), Montages->ClassifyAttackPress() == EVCAttackPress::Buffer);
	Montages->BufferAttack(/*bSecondary*/ true);
	TestTrue(TEXT("Kept as the secondary's"), Montages->IsBufferedAttackSecondary());
	Advance(0.15f);
	TestTrue(TEXT("Due at the recovery"), Montages->TakeDueBufferedAttack());
	TestTrue(TEXT("Still the secondary's once taken"), Montages->IsBufferedAttackSecondary());
	TestEqual(TEXT("The kept secondary: _09"), Montages->ChooseAttack(1.f, Rate, /*bSecondary*/ true), 4);
	Advance(0.1f);
	TestTrue(TEXT("In _09's wind-up: nothing"), Montages->ClassifyAttackPress() == EVCAttackPress::Ignore);
	Advance(0.35f);
	TestTrue(TEXT("In _09's recovery: at once"), Montages->ClassifyAttackPress() == EVCAttackPress::Continue);
	TestEqual(TEXT("The primary after _09: _04"), Montages->ChooseAttack(1.f, Rate), 2);

	// Past the window, the secondary starts its own branch.
	Advance(1.5f);
	TestTrue(TEXT("Past the window: a new combo"), Montages->ClassifyAttackPress() == EVCAttackPress::NewCombo);
	TestEqual(TEXT("The secondary from rest: _08"), Montages->ChooseAttack(1.f, Rate, true), 3);
	Advance(0.45f);
	TestEqual(TEXT("The primary after _08: _02"), Montages->ChooseAttack(1.f, Rate), 1);
	Advance(0.45f);
	TestEqual(TEXT("The secondary after _02: _10"), Montages->ChooseAttack(1.f, Rate, true), 5);
	Advance(0.45f);
	TestEqual(TEXT("After _10 the secondary starts again: _08"), Montages->ChooseAttack(1.f, Rate, true), 3);

	// A set without a branch: the secondary keeps nothing and plays nothing.
	UVCActionAnimSet* List = NewObject<UVCActionAnimSet>(GetTransientPackage());
	List->ActionSet = VCGameplayTags::Equipment_AnimSet_Unarmed;
	List->AttackCombo = { MakeSwordAttack(1.f, 0.25f, 0.4f), MakeSwordAttack(1.f, 0.25f, 0.4f) };
	Montages->ActionAnimSets = { List };
	Advance(1.5f);
	TestEqual(TEXT("No branch: the secondary is nothing"), Montages->ChooseAttack(1.f, Rate, true), -1);
	TestEqual(TEXT("And the last attack stays"), Montages->GetLastAttackIndex(), 3);
	TestEqual(TEXT("The primary still plays its list"), Montages->ChooseAttack(1.f, Rate), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSword_AcceptSecondary, "VoxelCharacter.Combat.Melee.AcceptSecondary", VC_SWORD_FLAGS)
bool FVCSword_AcceptSecondary::RunTest(const FString& Parameters)
{
	using namespace VCSwordMovesTestHelpers;
	VCTesting::FPlayWorld W(TEXT("VCSwordAcceptSecondaryWorld"), /*bDestroyActorsFirst=*/true);
	AVCNPCCharacterBase* NPC = SpawnSwordFighter(W.World, MakeBranchedSet(), true);
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
	const auto Advance = [&W](float Seconds)
	{
		for (float Elapsed = 0.f; Elapsed < Seconds - UE_KINDA_SMALL_NUMBER; Elapsed += 0.05f)
		{
			TickSwordFrame(W.World, 0.05f);
		}
	};
	const auto Request = [&](int32 Index)
	{
		Montages->SetRequestedAttack(Index);
		return ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(CGFGameplayTags::Ability_Attack_Melee));
	};

	TestTrue(TEXT("The secondary's first from rest"), Request(3));
	TestEqual(TEXT("_08 taken"), Montages->GetLastAttackIndex(), 3);
	Advance(0.45f);
	TestTrue(TEXT("The secondary's next"), Request(4));
	TestEqual(TEXT("_09 taken"), Montages->GetLastAttackIndex(), 4);
	Advance(0.45f);
	// _02 is not _09's next on the primary (that is _04): the server picks the primary's own.
	TestTrue(TEXT("A primary index out of turn"), Request(1));
	TestEqual(TEXT("The server's primary next after _09: _04"), Montages->GetLastAttackIndex(), 2);
	Advance(0.45f);
	TestTrue(TEXT("The secondary's next after _04"), Request(3));
	TestEqual(TEXT("_04 ends the combo: _08"), Montages->GetLastAttackIndex(), 3);
	Advance(1.5f);
	// _10 out of the window: the secondary's first instead, the button kept.
	TestTrue(TEXT("A secondary index past the window"), Request(5));
	TestEqual(TEXT("The server's secondary first: _08"), Montages->GetLastAttackIndex(), 3);
	Advance(0.1f);
	const int32 Played = Montages->GetActionsPlayed();
	Request(4);
	TestEqual(TEXT("Too early in _08: no attack"), Montages->GetActionsPlayed(), Played);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSword_ChargeRule, "VoxelCharacter.Anim.Sword.ChargeRule", VC_SWORD_FLAGS)
bool FVCSword_ChargeRule::RunTest(const FString& Parameters)
{
	using namespace VCSwordMovesTestHelpers;
	// From rest only: a new combo, not sprinting, not in the air, with a set that charges.
	TestTrue(TEXT("From rest"), UVCActionMontageComponent::CanChargeFrom(true, EVCAttackPress::NewCombo, false, false));
	TestFalse(TEXT("Not in a combo's recovery"), UVCActionMontageComponent::CanChargeFrom(true, EVCAttackPress::Continue, false, false));
	TestFalse(TEXT("Not after a strike"), UVCActionMontageComponent::CanChargeFrom(true, EVCAttackPress::Buffer, false, false));
	TestFalse(TEXT("Not in a wind-up"), UVCActionMontageComponent::CanChargeFrom(true, EVCAttackPress::Ignore, false, false));
	TestFalse(TEXT("Not sprinting"), UVCActionMontageComponent::CanChargeFrom(true, EVCAttackPress::NewCombo, true, false));
	TestFalse(TEXT("Not in the air"), UVCActionMontageComponent::CanChargeFrom(true, EVCAttackPress::NewCombo, false, true));
	TestFalse(TEXT("Not without a charge (or with vc.Anim.AttackCharge 0)"), UVCActionMontageComponent::CanChargeFrom(false, EVCAttackPress::NewCombo, false, false));

	const UVCActionAnimSet* Set = MakeChargingSet();
	TestTrue(TEXT("The set charges"), Set->HasCharge());
	TestTrue(TEXT("The charge time: the windup and the hold"), FMath::IsNearlyEqual(Set->GetChargeTime(), 1.5f, 1.0e-4f));
	TestFalse(TEXT("The branched set alone does not charge"), MakeBranchedSet()->HasCharge());

	// The plan's cases: a tap and a release at 1.2 s play the first attack, a hold to the charge time the charged strike.
	TestTrue(TEXT("A tap: the first attack"), UVCActionMontageComponent::ClassifyChargeRelease(0.1, Set->GetChargeTime()) == EVCChargeRelease::FirstAttack);
	TestTrue(TEXT("Released at 1.2 s: the first attack"), UVCActionMontageComponent::ClassifyChargeRelease(1.2, Set->GetChargeTime()) == EVCChargeRelease::FirstAttack);
	TestTrue(TEXT("Held to 1.5 s: the charged strike"), UVCActionMontageComponent::ClassifyChargeRelease(1.5, Set->GetChargeTime()) == EVCChargeRelease::Charged);

	TestEqual(TEXT("After the charged strike the primary goes on with _02"), Set->GetNextAttack(6, false), 1);
	TestFalse(TEXT("The charged strike is no secondary attack"), Set->IsSecondaryAttack(6));
	TestTrue(TEXT("Its damage twice the weapon's"), FMath::IsNearlyEqual(Set->GetAttackDamageMultiplier(6), 2.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSword_ChargeServer, "VoxelCharacter.Combat.Melee.ChargedStrike", VC_SWORD_FLAGS)
bool FVCSword_ChargeServer::RunTest(const FString& Parameters)
{
	using namespace VCSwordMovesTestHelpers;
	VCTesting::FPlayWorld W(TEXT("VCSwordChargeWorld"), /*bDestroyActorsFirst=*/true);
	AVCNPCCharacterBase* NPC = SpawnSwordFighter(W.World, MakeChargingSet(), true);
	AVCNPCCharacterBase* Victim = SpawnSwordFighter(W.World, nullptr, false, FVector(150.0, 0.0, 0.0), CGFGameplayTags::Faction_Monster);
	if (!TestNotNull(TEXT("Fighter"), NPC) || !TestNotNull(TEXT("Victim"), Victim))
	{
		return false;
	}
	Victim->SetActorRotation(FRotator(0.0, 180.0, 0.0));
	// The attacker swings only when the test asks: its AI would fight the hostile before it on its own.
	if (AController* Brain = NPC->GetController())
	{
		Brain->UnPossess();
		Brain->Destroy();
	}
	UVCActionMontageComponent* Montages = NPC->ActionMontages;
	UAbilitySystemComponent* ASC = NPC->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("ASC"), ASC))
	{
		return false;
	}
	const auto Advance = [&W](float Seconds)
	{
		for (float Elapsed = 0.f; Elapsed < Seconds - UE_KINDA_SMALL_NUMBER; Elapsed += 0.05f)
		{
			TickSwordFrame(W.World, 0.05f);
		}
	};
	const auto Request = [&](int32 Index)
	{
		Montages->SetRequestedAttack(Index);
		return ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(CGFGameplayTags::Ability_Attack_Melee));
	};

	// From rest the character may charge; in a combo it may not.
	TestTrue(TEXT("At rest: a charge"), Montages->CanStartCharge());
	float Rate = 0.f;
	TestEqual(TEXT("A first attack"), Montages->ChooseAttack(1.f, Rate), 0);
	Advance(0.3f);
	TestFalse(TEXT("In the combo: no charge"), Montages->CanStartCharge());
	Advance(1.5f);
	TestTrue(TEXT("Past the combo's window: a charge again"), Montages->CanStartCharge());

	// A charged strike sent early (the server's clock at 1.0 s of 1.5, beyond the 0.2 s allowance): the first attack.
	const float Before = Victim->CombatComponent->GetHealth();
	Montages->BeginCharge();
	TestTrue(TEXT("Charging"), Montages->IsCharging());
	Advance(1.f);
	TestTrue(TEXT("Taken"), Request(6));
	TestEqual(TEXT("Too early for the charged strike: _01"), Montages->GetLastAttackIndex(), 0);
	TestFalse(TEXT("The charge ends with the attack"), Montages->IsCharging());
	Advance(0.3f);
	const float AfterFirst = Victim->CombatComponent->GetHealth();
	const float FirstDamage = Before - AfterFirst;
	AddInfo(FString::Printf(TEXT("_01: %.1f damage"), FirstDamage));
	TestTrue(TEXT("_01 hits"), FirstDamage > 0.f);

	// The charge run to its time less the allowance (1.3 s): the charged strike, at twice the damage.
	Advance(1.5f);
	Montages->BeginCharge();
	Advance(1.35f);
	TestTrue(TEXT("Taken"), Request(6));
	TestEqual(TEXT("The charged strike"), Montages->GetLastAttackIndex(), 6);
	Advance(0.9f);
	const float ChargedDamage = AfterFirst - Victim->CombatComponent->GetHealth();
	AddInfo(FString::Printf(TEXT("_15: %.1f damage"), ChargedDamage));
	TestTrue(TEXT("Twice the first attack's damage"), FMath::IsNearlyEqual(ChargedDamage, 2.f * FirstDamage, 0.01f));

	// A press in its recovery goes on with _02 (on the owner's pick and on the server's).
	Advance(0.45f);
	TestTrue(TEXT("In the charged strike's recovery"), Montages->ClassifyAttackPress() == EVCAttackPress::Continue);
	TestTrue(TEXT("The next requested"), Request(1));
	TestEqual(TEXT("_02 after the charged strike"), Montages->GetLastAttackIndex(), 1);

	// No charge kept: a charged request is the first attack.
	Advance(2.f);
	TestTrue(TEXT("Taken"), Request(6));
	TestEqual(TEXT("No charge on the server: _01"), Montages->GetLastAttackIndex(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSword_RunAttackRule, "VoxelCharacter.Anim.Sword.RunAttackRule", VC_SWORD_FLAGS)
bool FVCSword_RunAttackRule::RunTest(const FString& Parameters)
{
	using namespace VCSwordMovesTestHelpers;
	// The gait and the speed (D168): sprinting, faster than the set's speed.
	TestTrue(TEXT("A sprint at 600 over 550: the run attack"), UVCActionMontageComponent::CanRunAttackFrom(true, true, 600.f, 550.f));
	TestFalse(TEXT("At the speed: not yet"), UVCActionMontageComponent::CanRunAttackFrom(true, true, 550.f, 550.f));
	TestFalse(TEXT("A sprint's first steps under it: the combo"), UVCActionMontageComponent::CanRunAttackFrom(true, true, 500.f, 550.f));
	TestFalse(TEXT("The run (no sprint) at any speed: the combo"), UVCActionMontageComponent::CanRunAttackFrom(true, false, 700.f, 550.f));
	TestFalse(TEXT("A set without one: the combo"), UVCActionMontageComponent::CanRunAttackFrom(false, true, 700.f, 550.f));
	TestTrue(TEXT("The server's allowance on an owner's request"),
		UVCActionMontageComponent::CanRunAttackFrom(true, true, 500.f, 550.f - UVCActionMontageComponent::RunAttackSpeedTolerance));

	const UVCActionAnimSet* Set = MakeRunningSet();
	TestTrue(TEXT("The set has a run attack"), Set->HasRunAttack());
	TestFalse(TEXT("The branched set has none"), MakeBranchedSet()->HasRunAttack());
	TestTrue(TEXT("Over halfway from the run to the sprint by default"), FMath::IsNearlyEqual(Set->RunAttackSpeed, 550.f));
	TestFalse(TEXT("The run attack is the primary's"), Set->IsSecondaryAttack(7));
	TestEqual(TEXT("A primary press after it: _01"), Set->GetNextAttack(7, false), 0);
	TestEqual(TEXT("A secondary press after it: _08"), Set->GetNextAttack(7, true), 3);
	TestEqual(TEXT("No other attack leads to it"), Set->GetNextAttack(6, false), 1);

	// Its root motion on the move: the flag takes it with input (the attacks without it keep D106).
	TestTrue(TEXT("Carried: the move takes the root motion"), UVCMovementComponent::ShouldTakeMontageRootMotion(true, true, true));
	TestFalse(TEXT("Not carried: the move ignores it"), UVCMovementComponent::ShouldTakeMontageRootMotion(true, true, false));
	const UVCActionAnimSet* Sword = LoadObject<UVCActionAnimSet>(nullptr,
		TEXT("/Game/CharacterAnimation/ActionSets/DA_VCActionSet_SwordShield.DA_VCActionSet_SwordShield"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Sword)
	{
		// The layers script's set: _12 last, carrying the capsule; the combo's lunges do not.
		if (TestTrue(TEXT("The sword's set has its run attack"), Sword->HasRunAttack()))
		{
			const UAnimMontage* Run = Sword->AttackCombo[Sword->RunAttack];
			TestTrue(TEXT("_12 carries the capsule"), UVCActionAnimSet::CarriesCapsule(Run));
			TestTrue(TEXT("Its hit before its recovery"), UVCActionAnimSet::GetHitTime(Run) < UVCActionAnimSet::GetRecoveryTime(Run));
		}
		TestFalse(TEXT("_01 does not"), UVCActionAnimSet::CarriesCapsule(Sword->AttackCombo.IsEmpty() ? nullptr : Sword->AttackCombo[0].Get()));
	}
	else
	{
		AddInfo(TEXT("No DA_VCActionSet_SwordShield (the CharacterAnimation pack is absent): the asset check skipped"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSword_RunAttackServer, "VoxelCharacter.Combat.Melee.RunAttack", VC_SWORD_FLAGS)
bool FVCSword_RunAttackServer::RunTest(const FString& Parameters)
{
	using namespace VCSwordMovesTestHelpers;
	VCTesting::FPlayWorld W(TEXT("VCSwordRunAttackWorld"), /*bDestroyActorsFirst=*/true);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("The engine cube"), Cube))
	{
		return false;
	}
	// A floor 200 m wide, its top at 0.
	AStaticMeshActor* Floor = W.World->SpawnActor<AStaticMeshActor>(FVector(0.0, 0.0, -50.0), FRotator::ZeroRotator);
	Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Floor->SetActorScale3D(FVector(200.0, 200.0, 1.0));
	Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));

	UVCActionAnimSet* Set = MakeRunningSet();
	AVCNPCCharacterBase* NPC = SpawnSwordFighter(W.World, Set, true, FVector(-9000.0, 0.0, 100.0));
	if (!TestNotNull(TEXT("Fighter"), NPC))
	{
		return false;
	}
	UVCMovementComponent* Move = Cast<UVCMovementComponent>(NPC->GetCharacterMovement());
	UVCActionMontageComponent* Montages = NPC->ActionMontages;
	UAbilitySystemComponent* ASC = NPC->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("Movement"), Move) || !TestNotNull(TEXT("ASC"), ASC))
	{
		return false;
	}
	Move->bRunPhysicsWithNoController = true;
	Move->SetMovementMode(MOVE_Walking);
	// Walks along +X, the world and its movement ticking.
	const auto Walk = [&](float Seconds)
	{
		for (float Elapsed = 0.f; Elapsed < Seconds - UE_KINDA_SMALL_NUMBER; Elapsed += 1.f / 30.f)
		{
			NPC->AddMovementInput(FVector::ForwardVector, 1.f);
			TickSwordFrame(W.World, 1.f / 30.f);
		}
	};
	const auto Speed = [Move]() { return static_cast<float>(FVector(Move->Velocity.X, Move->Velocity.Y, 0.0).Size()); };
	const auto Request = [&](int32 Index)
	{
		Montages->SetRequestedAttack(Index);
		return ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(CGFGameplayTags::Ability_Attack_Melee));
	};
	float Rate = 0.f;

	// Walking, not sprinting: the combo, even under a low speed.
	Walk(1.f);
	TestTrue(TEXT("On the floor"), Move->IsMovingOnGround());
	const float WalkSpeed = Speed();
	AddInfo(FString::Printf(TEXT("Walking at %.0f uu/s"), WalkSpeed));
	Set->RunAttackSpeed = WalkSpeed * 0.5f;
	TestFalse(TEXT("Walking: no run attack"), Montages->CanRunAttack());
	TestEqual(TEXT("A fresh press walking: _01"), Montages->ChooseAttack(1.f, Rate, false, /*bAllowRunAttack*/ true), 0);

	// Sprinting: the run attack once faster than the set's speed.
	Move->bWantsToSprint = true;
	Walk(3.f);
	const float SprintSpeed = Speed();
	AddInfo(FString::Printf(TEXT("Sprinting at %.0f uu/s"), SprintSpeed));
	TestTrue(TEXT("Sprinting"), Move->IsSprinting());
	Set->RunAttackSpeed = SprintSpeed - 50.f;
	TestTrue(TEXT("Sprinting faster than the set's speed"), Montages->CanRunAttack());
	TestEqual(TEXT("A kept press sprinting: the combo, not the run attack"), Montages->ChooseAttack(1.f, Rate, false, /*bAllowRunAttack*/ false), 0);
	Walk(3.f);
	TestEqual(TEXT("A fresh press sprinting: _12"), Montages->ChooseAttack(1.f, Rate, false, /*bAllowRunAttack*/ true), 7);
	TestEqual(TEXT("The secondary sprinting: its own (_08)"), Set->GetFirstAttack(true), 3);

	// The server: _12 while its own character sprints that fast, then the combo from its recovery.
	Walk(3.f);
	TestTrue(TEXT("Taken"), Request(7));
	TestEqual(TEXT("The server takes the run attack"), Montages->GetLastAttackIndex(), 7);
	Walk(0.5f);
	const int32 Played = Montages->GetActionsPlayed();
	Request(0);
	TestEqual(TEXT("Nothing before _12's recovery"), Montages->GetActionsPlayed(), Played);
	Walk(0.7f);
	TestTrue(TEXT("In _12's recovery"), Montages->ClassifyAttackPress() == EVCAttackPress::Continue);
	TestEqual(TEXT("A kept press after _12: _01"), Montages->ChooseAttack(1.f, Rate, false, /*bAllowRunAttack*/ false), 0);

	// Too slow for the server (an NPC's request has no allowance): the primary's own.
	Walk(3.f);
	Set->RunAttackSpeed = SprintSpeed + 50.f;
	TestTrue(TEXT("Taken"), Request(7));
	TestEqual(TEXT("Under the speed on the server: _01"), Montages->GetLastAttackIndex(), 0);

	// The sprint let go: the combo at any speed.
	Move->bWantsToSprint = false;
	Walk(3.f);
	Set->RunAttackSpeed = 10.f;
	TestTrue(TEXT("Taken"), Request(7));
	TestEqual(TEXT("Not sprinting on the server: _01"), Montages->GetLastAttackIndex(), 0);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
