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
// - E, the air and plunge attacks (D169): only while falling (or landed within the allowance), one per fall; the
//   landing section from the landing; the plunge's slam at the server's landing reaches the hostiles within the radius
//   of the feet and not beyond, at 1.5 times the damage; no montage steers a fall.
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

	/**
	 * A transient air attack montage: the swing (Begin), the fall's loop (Loop, looping) and the landing (Landing), as
	 * the SwordShield set's _Air_02 and _Plunge_01 are built, its hit and its recovery marked in montage time.
	 */
	UAnimMontage* MakeAirMontage(float Begin, float Loop, float Landing, float HitTime, float RecoveryTime)
	{
		UAnimMontage* Montage = MakeSwordAttack(Begin + Loop + Landing, HitTime, RecoveryTime);
		const auto AddSection = [Montage](FName Name, float Time)
		{
			FCompositeSection Section;
			Section.SectionName = Name;
			Section.Link(Montage, Time);
			Montage->CompositeSections.Add(Section);
		};
		AddSection(TEXT("Default"), 0.f);
		AddSection(UVCActionAnimSet::AirLoopSection, Begin);
		AddSection(UVCActionAnimSet::LandingSection, Begin + Loop);
		return Montage;
	}

	/**
	 * The running set with the SwordShield set's air attacks: _Air_02 (combo index 8: the swing 0.5 s hitting at 0.37,
	 * the loop 0.83, the landing 2.17 recovering 0.73 into it) and _Plunge_01 (index 9: 0.4, 0.83, 2.0; the slam 0.13
	 * into its landing, recovering 0.53 into it) at 1.5 times the damage, a press after either starting the combo.
	 */
	UVCActionAnimSet* MakeAirSet()
	{
		UVCActionAnimSet* Set = MakeRunningSet();
		Set->AttackCombo.Add(MakeAirMontage(0.5f, 0.83f, 2.17f, 0.37f, 0.5f + 0.83f + 0.73f));
		Set->AttackCombo.Add(MakeAirMontage(0.4f, 0.83f, 2.f, 0.4f + 0.83f + 0.13f, 0.4f + 0.83f + 0.53f));
		for (const float Multiplier : { 1.f, 1.5f })
		{
			FVCAttackComboStep& Step = Set->AttackComboSteps.AddDefaulted_GetRef();
			Step.NextPrimary = 0;
			Step.NextSecondary = 3;
			Step.DamageMultiplier = Multiplier;
		}
		Set->AirAttack = 8;
		Set->PlungeAttack = 9;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSword_AirAttackRule, "VoxelCharacter.Anim.Sword.AirAttackRule", VC_SWORD_FLAGS)
bool FVCSword_AirAttackRule::RunTest(const FString& Parameters)
{
	using namespace VCSwordMovesTestHelpers;
	// The fall and its allowance (D169): falling, or landed within the allowance; one per fall; a set with one.
	TestTrue(TEXT("Falling: the air attack"), UVCActionMontageComponent::CanAirAttackFrom(true, false, true, -1.0, 0.2f));
	TestFalse(TEXT("One per fall"), UVCActionMontageComponent::CanAirAttackFrom(true, true, true, -1.0, 0.2f));
	TestTrue(TEXT("Landed 0.1 s ago, within the server's allowance"), UVCActionMontageComponent::CanAirAttackFrom(true, false, false, 0.1, 0.2f));
	TestFalse(TEXT("Landed 0.3 s ago: on the ground"), UVCActionMontageComponent::CanAirAttackFrom(true, false, false, 0.3, 0.2f));
	TestFalse(TEXT("On the ground, the owner (no allowance)"), UVCActionMontageComponent::CanAirAttackFrom(true, false, false, 0.1, 0.f));
	TestFalse(TEXT("Never landed and not falling"), UVCActionMontageComponent::CanAirAttackFrom(true, false, false, -1.0, 0.2f));
	TestFalse(TEXT("A set without one"), UVCActionMontageComponent::CanAirAttackFrom(false, false, true, -1.0, 0.2f));

	// The slam's reach from the feet, across local up and along it, on a plane and on a planet's slope.
	const FVector Feet(0.0, 0.0, 0.0);
	TestTrue(TEXT("150 uu across: hit"), UVCMeleeAttackAbility::IsInSlam(Feet, FVector::UpVector, FVector(150.0, 0.0, 90.0), 200.f));
	TestTrue(TEXT("200 uu across: hit"), UVCMeleeAttackAbility::IsInSlam(Feet, FVector::UpVector, FVector(0.0, 200.0, 90.0), 200.f));
	TestFalse(TEXT("260 uu across: missed"), UVCMeleeAttackAbility::IsInSlam(Feet, FVector::UpVector, FVector(-260.0, 0.0, 90.0), 200.f));
	TestFalse(TEXT("250 uu above: missed"), UVCMeleeAttackAbility::IsInSlam(Feet, FVector::UpVector, FVector(0.0, 0.0, 250.0), 200.f));
	const FVector Tilted = FRotator(30.0, 40.0, 0.0).RotateVector(FVector::UpVector);
	const FVector Across = FVector::CrossProduct(Tilted, FVector::RightVector).GetSafeNormal();
	TestTrue(TEXT("A planet's up: 150 across it hits"), UVCMeleeAttackAbility::IsInSlam(Feet, Tilted, Across * 150.0 + Tilted * 90.0, 200.f));
	TestFalse(TEXT("A planet's up: 260 across it misses"), UVCMeleeAttackAbility::IsInSlam(Feet, Tilted, Across * 260.0 + Tilted * 90.0, 200.f));

	// The set: the button's air attack, the landing's start, the table after them.
	const UVCActionAnimSet* Set = MakeAirSet();
	TestEqual(TEXT("The primary's air attack"), Set->GetAirAttack(false), 8);
	TestEqual(TEXT("The secondary's: the plunge"), Set->GetAirAttack(true), 9);
	TestTrue(TEXT("Both are air attacks"), Set->IsAirAttack(8) && Set->IsAirAttack(9) && !Set->IsAirAttack(0));
	TestFalse(TEXT("The plunge is no ground secondary"), Set->IsSecondaryAttack(9));
	TestEqual(TEXT("A press after the plunge: _01"), Set->GetNextAttack(9, false), 0);
	TestTrue(TEXT("The air attack's landing at 1.33 s"), FMath::IsNearlyEqual(Set->GetLandingTime(8), 1.33f, 1.0e-4f));
	TestTrue(TEXT("The plunge's slam at 1.5 times"), FMath::IsNearlyEqual(Set->GetAttackDamageMultiplier(9), 1.5f));
	TestEqual(TEXT("No air attack for a list"), MakeBranchedSet()->GetAirAttack(false), -1);

	// The fall stays the fall: no montage's root motion in it, the capsule's carriers included.
	TestFalse(TEXT("Falling, standing still: ignored"), UVCMovementComponent::ShouldTakeMontageRootMotion(true, false, false, true));
	TestFalse(TEXT("Falling, a carrier: ignored"), UVCMovementComponent::ShouldTakeMontageRootMotion(true, true, true, true));
	TestTrue(TEXT("On the ground, standing: taken"), UVCMovementComponent::ShouldTakeMontageRootMotion(true, false, false, false));

	const UVCActionAnimSet* Sword = LoadObject<UVCActionAnimSet>(nullptr,
		TEXT("/Game/CharacterAnimation/ActionSets/DA_VCActionSet_SwordShield.DA_VCActionSet_SwordShield"), nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Sword)
	{
		// The layers script's set: both with their three sections; the air attack hits in its swing, the plunge at its
		// landing; both recover in their landing.
		for (const bool bSecondary : { false, true })
		{
			const int32 Index = Sword->GetAirAttack(bSecondary);
			if (!TestTrue(FString::Printf(TEXT("The sword's set has its %s"), bSecondary ? TEXT("plunge") : TEXT("air attack")), Index >= 0))
			{
				continue;
			}
			const UAnimMontage* Montage = Sword->AttackCombo[Index];
			const float Landing = Sword->GetLandingTime(Index);
			TestTrue(TEXT("Its loop"), Montage->GetSectionIndex(UVCActionAnimSet::AirLoopSection) != INDEX_NONE);
			TestTrue(TEXT("Its landing"), Montage->GetSectionIndex(UVCActionAnimSet::LandingSection) != INDEX_NONE);
			const float Hit = UVCActionAnimSet::GetHitTime(Montage);
			TestTrue(TEXT("Its hit"), bSecondary ? Hit > Landing : Hit < Landing);
			TestTrue(TEXT("Its recovery in its landing"), UVCActionAnimSet::GetRecoveryTime(Montage) > Landing);
			TestFalse(TEXT("It does not carry the capsule"), UVCActionAnimSet::CarriesCapsule(Montage));
		}
		TestTrue(TEXT("The slam's reach 200 uu"), FMath::IsNearlyEqual(Sword->PlungeRadius, 200.f));
	}
	else
	{
		AddInfo(TEXT("No DA_VCActionSet_SwordShield (the CharacterAnimation pack is absent): the asset check skipped"));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCSword_PlungeServer, "VoxelCharacter.Combat.Melee.Plunge", VC_SWORD_FLAGS)
bool FVCSword_PlungeServer::RunTest(const FString& Parameters)
{
	using namespace VCSwordMovesTestHelpers;
	VCTesting::FPlayWorld W(TEXT("VCSwordPlungeWorld"), /*bDestroyActorsFirst=*/true);
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("The engine cube"), Cube))
	{
		return false;
	}
	AStaticMeshActor* Floor = W.World->SpawnActor<AStaticMeshActor>(FVector(0.0, 0.0, -50.0), FRotator::ZeroRotator);
	Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
	Floor->GetStaticMeshComponent()->SetStaticMesh(Cube);
	Floor->SetActorScale3D(FVector(100.0, 100.0, 1.0));
	Floor->GetStaticMeshComponent()->SetCollisionProfileName(TEXT("BlockAll"));

	AVCNPCCharacterBase* NPC = SpawnSwordFighter(W.World, MakeAirSet(), true, FVector(0.0, 0.0, 100.0));
	// Two within the slam's reach of where the fighter lands, one beyond it.
	AVCNPCCharacterBase* Near = SpawnSwordFighter(W.World, nullptr, false, FVector(150.0, 0.0, 92.0), CGFGameplayTags::Faction_Monster);
	AVCNPCCharacterBase* Side = SpawnSwordFighter(W.World, nullptr, false, FVector(0.0, 190.0, 92.0), CGFGameplayTags::Faction_Monster);
	AVCNPCCharacterBase* Far = SpawnSwordFighter(W.World, nullptr, false, FVector(-260.0, 0.0, 92.0), CGFGameplayTags::Faction_Monster);
	if (!TestNotNull(TEXT("Fighter"), NPC) || !TestNotNull(TEXT("Near"), Near) || !TestNotNull(TEXT("Side"), Side) || !TestNotNull(TEXT("Far"), Far))
	{
		return false;
	}
	Near->SetActorRotation(FRotator(0.0, 180.0, 0.0));
	// The attacker swings only when the test asks: its AI would fight the hostiles around it on its own.
	if (AController* Brain = NPC->GetController())
	{
		Brain->UnPossess();
		Brain->Destroy();
	}
	UCharacterMovementComponent* Move = NPC->GetCharacterMovement();
	UVCActionMontageComponent* Montages = NPC->ActionMontages;
	UAbilitySystemComponent* ASC = NPC->GetAbilitySystemComponent();
	if (!TestNotNull(TEXT("ASC"), ASC))
	{
		return false;
	}
	Move->bRunPhysicsWithNoController = true;
	Move->SetMovementMode(MOVE_Walking);
	const auto Advance = [&W](float Seconds)
	{
		for (float Elapsed = 0.f; Elapsed < Seconds - UE_KINDA_SMALL_NUMBER; Elapsed += 1.f / 30.f)
		{
			TickSwordFrame(W.World, 1.f / 30.f);
		}
	};
	const auto Request = [&](int32 Index)
	{
		Montages->SetRequestedAttack(Index);
		return ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(CGFGameplayTags::Ability_Attack_Melee));
	};
	const auto Health = [](const AVCNPCCharacterBase* Who) { return Who->CombatComponent->GetHealth(); };
	Advance(0.5f);
	TestTrue(TEXT("Standing on the floor"), Move->IsMovingOnGround());

	// A ground swing at the near dummy, the measure of the weapon's damage.
	const float Before = Health(Near);
	TestFalse(TEXT("On the ground: no air attack"), Montages->CanAirAttack(false, UVCActionMontageComponent::AttackRequestTolerance));
	TestTrue(TEXT("A swing"), Request(0));
	Advance(0.8f);
	const float SwingDamage = Before - Health(Near);
	AddInfo(FString::Printf(TEXT("_01: %.1f damage"), SwingDamage));
	TestTrue(TEXT("_01 hits the near dummy"), SwingDamage > 0.f);
	Advance(2.f);

	// Up into the air: the plunge, once a fall; nothing lands before the ground.
	NPC->SetActorLocation(FVector(0.0, 0.0, 600.0), false, nullptr, ETeleportType::TeleportPhysics);
	Move->SetMovementMode(MOVE_Falling);
	Advance(1.f / 30.f);
	TestTrue(TEXT("Falling"), Montages->IsFalling());
	TestTrue(TEXT("Falling: an air attack may start"), Montages->CanAirAttack(true));
	const float NearBefore = Health(Near), SideBefore = Health(Side), FarBefore = Health(Far);
	TestTrue(TEXT("The plunge taken"), Request(9));
	TestEqual(TEXT("The plunge"), Montages->GetLastAttackIndex(), 9);
	TestFalse(TEXT("One air attack per fall"), Montages->CanAirAttack(false, UVCActionMontageComponent::AttackRequestTolerance));
	TestTrue(TEXT("Nothing hit in the air"), FMath::IsNearlyEqual(Health(Near), NearBefore));
	for (int32 Frame = 0; Frame < 120 && Montages->IsFalling(); ++Frame)
	{
		Advance(1.f / 30.f);
	}
	TestFalse(TEXT("Landed"), Montages->IsFalling());
	const float NearSlam = NearBefore - Health(Near), SideSlam = SideBefore - Health(Side), FarSlam = FarBefore - Health(Far);
	AddInfo(FString::Printf(TEXT("The slam: near %.1f, side %.1f, far %.1f"), NearSlam, SideSlam, FarSlam));
	TestTrue(TEXT("The near dummy (150 uu) at 1.5 times the swing"), FMath::IsNearlyEqual(NearSlam, 1.5f * SwingDamage, 0.01f));
	TestTrue(TEXT("The side dummy (190 uu) at 1.5 times the swing"), FMath::IsNearlyEqual(SideSlam, 1.5f * SwingDamage, 0.01f));
	TestTrue(TEXT("Not the far one (260 uu)"), FMath::IsNearlyEqual(FarSlam, 0.f));

	// From the landing: nothing in the slam's wind-up, kept before the landing's recovery, the combo after it; the next
	// fall is free.
	TestTrue(TEXT("In the slam's wind-up: nothing"), Montages->ClassifyAttackPress() == EVCAttackPress::Ignore);
	TestTrue(TEXT("Landed just now: within the allowance"), Montages->CanAirAttack(false, UVCActionMontageComponent::AttackRequestTolerance));
	TestFalse(TEXT("Landed just now: not for the owner"), Montages->CanAirAttack(false));
	Advance(0.2f);
	TestTrue(TEXT("After the slam: a press is kept"), Montages->ClassifyAttackPress() == EVCAttackPress::Buffer);
	Advance(0.4f);
	TestTrue(TEXT("After the landing's recovery: the combo"), Montages->ClassifyAttackPress() == EVCAttackPress::Continue);
	TestFalse(TEXT("Past the allowance: no air attack"), Montages->CanAirAttack(false, UVCActionMontageComponent::AttackRequestTolerance));

	// The air attack in another fall: taken, a second refused; its swing in the air, the landing from the landing.
	Advance(3.f);
	NPC->SetActorLocation(FVector(0.0, 0.0, 3000.0), false, nullptr, ETeleportType::TeleportPhysics);
	Move->SetMovementMode(MOVE_Falling);
	Advance(1.f / 30.f);
	TestTrue(TEXT("The air attack taken"), Request(8));
	TestEqual(TEXT("The air attack"), Montages->GetLastAttackIndex(), 8);
	Advance(0.5f);
	TestTrue(TEXT("Still falling"), Montages->IsFalling());
	Request(9);
	TestEqual(TEXT("A second in the same fall refused"), Montages->GetLastAttackIndex(), 8);
	for (int32 Frame = 0; Frame < 120 && Montages->IsFalling(); ++Frame)
	{
		Advance(1.f / 30.f);
	}
	TestTrue(TEXT("In its landing: kept"), Montages->ClassifyAttackPress() == EVCAttackPress::Buffer);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
