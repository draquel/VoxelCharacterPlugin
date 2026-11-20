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
// The sword's moves (Documentation/SWORD_MOVES_PLAN.md):
// - B, the secondary branch (D166): the combo's table walked by both buttons in every order, the press classes on a
//   secondary step, the server's acceptance of a secondary index and its own pick for the button pressed.
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

	void TickSwordFrame(UWorld* World, float DeltaSeconds)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, DeltaSeconds);
	}

	AVCNPCCharacterBase* SpawnSwordFighter(UWorld* World, UVCActionAnimSet* Set, bool bMelee)
	{
		const FTransform Transform(FVector::ZeroVector);
		AVCNPCCharacterBase* NPC = World->SpawnActorDeferred<AVCNPCCharacterBase>(AVCNPCCharacterBase::StaticClass(), Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!NPC)
		{
			return nullptr;
		}
		NPC->StartingMaxHealth = 100.f;
		NPC->DestroyAfterDeathDelay = 0.f;
		NPC->bAIEnabled = false;
		NPC->CombatComponent->FactionTag = CGFGameplayTags::Faction_Player;
		if (bMelee)
		{
			NPC->DefaultAbilities.Add(UVCMeleeAttackAbility::StaticClass());
		}
		NPC->ActionMontages->ActionAnimSets.Add(Set);
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

#endif // WITH_AUTOMATION_TESTS
