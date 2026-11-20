// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Testing/VCTestWorld.h"

#if WITH_AUTOMATION_TESTS

#include "Animation/VCActionAnimSet.h"
#include "Animation/VCActionMontageComponent.h"
#include "Animation/AnimMontage.h"
#include "Combat/VCCombatComponent.h"
#include "Combat/VCCombatStatics.h"
#include "Combat/VCDodgeAbility.h"
#include "Combat/VCMeleeAttackAbility.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Core/VCGameplayTags.h"
#include "Core/VCNPCCharacterBase.h"
#include "Movement/VCMovementComponent.h"
#include "Tags/CGFGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/UObjectGlobals.h"

// ---------------------------------------------------------------------------
// The dodge and the roll (the sword moves plan, phase F, D165, D168, D170):
// - The move a press makes: a step back with no input, a roll toward the input, a step to the input's side of the
//   facing while the facing is locked (or the set has no roll); the input's up part ignored on a tilted frame; a step
//   carries the owner's facing for the server to turn to.
// - A montage carries the capsule with input only with the flag (D168), and the rule stays D106's without it.
// - The cost: refused short of stamina, spent at once when taken (15 a dodge, 20 a roll); a roll turns the capsule to
//   the request's direction on the server.
// - The immunity: the server holds State.Invulnerable from the start to the travelled marker; a hit in it is refused,
//   one after it lands.
// - From an attack: a dodge during the wind-up cancels the swing, whose hit does not land.
// - Into an attack: no attack before the move's recovery (the server's allowance taken off), a press kept during the
//   move is due at its recovery, and the combo starts over after it.
// ---------------------------------------------------------------------------

namespace VCDodgeTestHelpers
{
	/** A transient montage of a length with a recovery and a travelled marker (D170), and a hit at HitTime if positive. */
	UAnimMontage* MakeMove(float Length, float RecoveryTime, float TravelledTime, float HitTime = -1.f)
	{
		UAnimMontage* Montage = NewObject<UAnimMontage>(GetTransientPackage());
		Montage->SetCompositeLength(Length);
		const auto Add = [Montage](UAnimNotify* Notify, float Time)
		{
			FAnimNotifyEvent Event;
			Event.Notify = Notify;
			Event.Link(Montage, Time);
			Event.TriggerTimeOffset = 0.f;
			Montage->Notifies.Add(Event);
		};
		if (RecoveryTime >= 0.f)
		{
			Add(NewObject<UVCAnimNotify_ActionRecovery>(Montage), RecoveryTime);
		}
		if (TravelledTime >= 0.f)
		{
			Add(NewObject<UVCAnimNotify_ActionTravelled>(Montage), TravelledTime);
		}
		if (HitTime >= 0.f)
		{
			Add(NewObject<UVCAnimNotify_ActionHit>(Montage), HitTime);
		}
		return Montage;
	}

	/** An Unarmed set with a step to every side, a roll and one attack (1 s, its hit at 0.3 s, its recovery at 0.6 s). */
	UVCActionAnimSet* MakeMoveSet()
	{
		UVCActionAnimSet* Set = NewObject<UVCActionAnimSet>(GetTransientPackage());
		Set->ActionSet = VCGameplayTags::Equipment_AnimSet_Unarmed;
		Set->Dodge.Front = MakeMove(1.f, 0.7f, 0.3f);
		Set->Dodge.Back = MakeMove(1.f, 0.7f, 0.3f);
		Set->Dodge.Left = MakeMove(1.f, 0.7f, 0.3f);
		Set->Dodge.Right = MakeMove(1.f, 0.7f, 0.3f);
		Set->Roll = MakeMove(1.08f, 0.6f, 0.44f);
		Set->AttackCombo = { MakeMove(1.f, 0.6f, -1.f, 0.3f) };
		return Set;
	}

	void TickFrame(UWorld* World, float DeltaSeconds)
	{
		++GFrameCounter;
		World->Tick(LEVELTICK_All, DeltaSeconds);
	}

	void Advance(UWorld* World, float Seconds)
	{
		for (float Elapsed = 0.f; Elapsed < Seconds - UE_KINDA_SMALL_NUMBER; Elapsed += 0.05f)
		{
			TickFrame(World, 0.05f);
		}
	}

	/**
	 * An NPC fighter with the dodge (and the melee attack when asked), standing: walking with its movement's tick off, so it
	 * counts as on the ground without a floor to stand on and never falls.
	 */
	AVCNPCCharacterBase* SpawnDodger(UWorld* World, const FGameplayTag& Faction, const FVector& Location, bool bMelee, UVCActionAnimSet* Set)
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
		NPC->DefaultAbilities.Add(UVCDodgeAbility::StaticClass());
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
			Move->SetMovementMode(MOVE_Walking);
			Move->SetComponentTickEnabled(false);
		}
		return NPC;
	}

	/** Sets the NPC's stamina (and its maximum to 100). */
	void SetStamina(AVCNPCCharacterBase* NPC, float Stamina)
	{
		UAbilitySystemComponent* ASC = NPC ? NPC->GetAbilitySystemComponent() : nullptr;
		if (ASC)
		{
			ASC->SetNumericAttributeBase(UVCCharacterAttributeSet::GetMaxStaminaAttribute(), 100.f);
			ASC->SetNumericAttributeBase(UVCCharacterAttributeSet::GetStaminaAttribute(), Stamina);
		}
	}

	float GetStamina(AVCNPCCharacterBase* NPC)
	{
		const UAbilitySystemComponent* ASC = NPC ? NPC->GetAbilitySystemComponent() : nullptr;
		return ASC ? ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetStaminaAttribute()) : -1.f;
	}
}

#define VC_DODGE_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCDodge_ChooseMove, "VoxelCharacter.Combat.Dodge.ChooseMove", VC_DODGE_FLAGS)
bool FVCDodge_ChooseMove::RunTest(const FString& Parameters)
{
	const FVector Forward(1.0, 0.0, 0.0);
	const FVector Up(0.0, 0.0, 1.0);
	const auto Side = [](const FVCMoveRequest& R) { return static_cast<EVCHitDirection>(R.Index); };

	FVCMoveRequest R = UVCDodgeAbility::ChooseMove(FVector::ZeroVector, Forward, Up, false, true);
	TestTrue(TEXT("No input: a step back"), R.Kind == EVCMoveKind::Dodge && Side(R) == EVCHitDirection::Back);
	R = UVCDodgeAbility::ChooseMove(FVector::ZeroVector, Forward, Up, true, true);
	TestTrue(TEXT("No input, facing locked: a step back"), R.Kind == EVCMoveKind::Dodge && Side(R) == EVCHitDirection::Back);

	R = UVCDodgeAbility::ChooseMove(FVector(0.0, 1.0, 0.0), Forward, Up, false, true);
	TestTrue(TEXT("Input to the right: a roll"), R.Kind == EVCMoveKind::Roll);
	TestTrue(TEXT("Toward the input"), FVector(R.Direction).Equals(FVector(0.0, 1.0, 0.0), 0.01));
	R = UVCDodgeAbility::ChooseMove(FVector(-1.0, -1.0, 0.0), Forward, Up, false, true);
	TestTrue(TEXT("Input back and left: a roll that way"), R.Kind == EVCMoveKind::Roll
		&& FVector(R.Direction).Equals(FVector(-1.0, -1.0, 0.0).GetSafeNormal(), 0.01));

	R = UVCDodgeAbility::ChooseMove(FVector(0.0, 1.0, 0.0), Forward, Up, true, true);
	TestTrue(TEXT("Facing locked, input right: a step right"), R.Kind == EVCMoveKind::Dodge && Side(R) == EVCHitDirection::Right);
	TestTrue(TEXT("A step carries the facing it starts with (the server turns to it)"), FVector(R.Direction).Equals(Forward, 0.01));
	R = UVCDodgeAbility::ChooseMove(FVector(0.0, -1.0, 0.0), Forward, Up, true, true);
	TestTrue(TEXT("Facing locked, input left: a step left"), R.Kind == EVCMoveKind::Dodge && Side(R) == EVCHitDirection::Left);
	R = UVCDodgeAbility::ChooseMove(FVector(1.0, 0.2, 0.0), Forward, Up, true, true);
	TestTrue(TEXT("Facing locked, input forward: a step forward"), R.Kind == EVCMoveKind::Dodge && Side(R) == EVCHitDirection::Front);
	R = UVCDodgeAbility::ChooseMove(FVector(-1.0, 0.0, 0.0), Forward, Up, true, true);
	TestTrue(TEXT("Facing locked, input back: a step back"), R.Kind == EVCMoveKind::Dodge && Side(R) == EVCHitDirection::Back);
	R = UVCDodgeAbility::ChooseMove(FVector(0.0, 1.0, 0.0), Forward, Up, false, false);
	TestTrue(TEXT("No roll in the set: a step to the input's side"), R.Kind == EVCMoveKind::Dodge && Side(R) == EVCHitDirection::Right);

	// A tilted frame (on a planet away from the pole): only the input across local up counts.
	const FVector TiltUp = FVector(0.0, 1.0, 1.0).GetSafeNormal();
	const FVector TiltForward(1.0, 0.0, 0.0);
	R = UVCDodgeAbility::ChooseMove(TiltUp * 0.8, TiltForward, TiltUp, false, true);
	TestTrue(TEXT("Input along local up only: a step back"), R.Kind == EVCMoveKind::Dodge && Side(R) == EVCHitDirection::Back);
	const FVector TiltRight = FVector::CrossProduct(TiltUp, TiltForward).GetSafeNormal();
	R = UVCDodgeAbility::ChooseMove(TiltRight + TiltUp * 0.5, TiltForward, TiltUp, false, true);
	TestTrue(TEXT("A tilted roll runs across local up"), R.Kind == EVCMoveKind::Roll && FMath::Abs(FVector::DotProduct(FVector(R.Direction), TiltUp)) < 0.02);
	R = UVCDodgeAbility::ChooseMove(TiltRight + TiltUp * 0.5, TiltForward, TiltUp, true, true);
	AddInfo(FString::Printf(TEXT("Tilted, locked: side %d"), R.Index));
	TestTrue(TEXT("A tilted locked step is to a side"), R.Kind == EVCMoveKind::Dodge && (Side(R) == EVCHitDirection::Left || Side(R) == EVCHitDirection::Right));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCDodge_CarriesCapsule, "VoxelCharacter.Movement.MontageCarriesCapsule", VC_DODGE_FLAGS)
bool FVCDodge_CarriesCapsule::RunTest(const FString& Parameters)
{
	// D106 unchanged without the flag: standing takes root motion, moving ignores it (only standing on), both off.
	TestTrue(TEXT("Standing takes it"), UVCMovementComponent::ShouldTakeMontageRootMotion(true, false));
	TestFalse(TEXT("Moving ignores it"), UVCMovementComponent::ShouldTakeMontageRootMotion(true, true));
	TestTrue(TEXT("The rule off: moving takes it"), UVCMovementComponent::ShouldTakeMontageRootMotion(false, true));
	// D168: a montage that carries the capsule takes it on the move too.
	TestTrue(TEXT("Carries: moving takes it"), UVCMovementComponent::ShouldTakeMontageRootMotion(true, true, true));
	TestTrue(TEXT("Carries: standing takes it"), UVCMovementComponent::ShouldTakeMontageRootMotion(true, false, true));

	TestFalse(TEXT("No montage carries nothing"), UVCActionAnimSet::CarriesCapsule(nullptr));
	UAnimMontage* Plain = VCDodgeTestHelpers::MakeMove(1.f, 0.6f, 0.3f);
	TestFalse(TEXT("No flag: does not carry"), UVCActionAnimSet::CarriesCapsule(Plain));
	UAnimMontage* Flagged = VCDodgeTestHelpers::MakeMove(1.f, 0.6f, 0.3f);
	Flagged->AddMetaData(NewObject<UVCAnimMetaData_CarriesCapsule>(Flagged));
	TestNotNull(TEXT("The flag is found"), Flagged->FindMetaDataByClass<UVCAnimMetaData_CarriesCapsule>());
	TestFalse(TEXT("The flag without root motion: nothing to carry"), UVCActionAnimSet::CarriesCapsule(Flagged));

	// The layers script's roll, when the content pack is here: it has root motion and the flag.
	const UAnimMontage* Roll = LoadObject<UAnimMontage>(nullptr, TEXT("/Game/CharacterAnimation/Montages/SwordShield/AM_SwordShield_Roll.AM_SwordShield_Roll"),
		nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (Roll)
	{
		TestTrue(TEXT("The sword's roll carries the capsule"), UVCActionAnimSet::CarriesCapsule(Roll));
		TestTrue(TEXT("Its immunity ends before its recovery"), UVCActionAnimSet::GetTravelledTime(Roll) < UVCActionAnimSet::GetRecoveryTime(Roll));
	}
	else
	{
		AddInfo(TEXT("No AM_SwordShield_Roll (the CharacterAnimation pack is absent): the asset check skipped"));
	}

	TestTrue(TEXT("The travelled marker's time"), FMath::IsNearlyEqual(UVCActionAnimSet::GetTravelledTime(Plain), 0.3f, 1.0e-4f));
	TestTrue(TEXT("No marker: 30 % of the length"), FMath::IsNearlyEqual(UVCActionAnimSet::GetTravelledTime(VCDodgeTestHelpers::MakeMove(2.f, 1.f, -1.f)), 0.6f, 1.0e-4f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCDodge_Cost, "VoxelCharacter.Combat.Dodge.Cost", VC_DODGE_FLAGS)
bool FVCDodge_Cost::RunTest(const FString& Parameters)
{
	using namespace VCDodgeTestHelpers;
	VCTesting::FPlayWorld W(TEXT("VCDodgeCostWorld"), /*bDestroyActorsFirst=*/true);
	AVCNPCCharacterBase* NPC = SpawnDodger(W.World, CGFGameplayTags::Faction_Monster, FVector::ZeroVector, false, MakeMoveSet());
	UAbilitySystemComponent* ASC = NPC ? NPC->GetAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("Dodger"), NPC) || !TestNotNull(TEXT("ASC"), ASC))
	{
		return false;
	}
	UVCActionMontageComponent* Montages = NPC->ActionMontages;
	const FGameplayTagContainer Dodge(VCGameplayTags::Ability_Dodge);

	// Short of the step's 15: refused, nothing spent, nothing played.
	SetStamina(NPC, 10.f);
	TestFalse(TEXT("Short of stamina: refused"), ASC->TryActivateAbilitiesByTag(Dodge) && Montages->IsMoveBeforeRecovery());
	TestEqual(TEXT("Nothing played"), Montages->GetActionsPlayed(), 0);
	TestEqual(TEXT("Nothing spent"), GetStamina(NPC), 10.f);

	// Enough: the step back plays and costs 15 at once.
	SetStamina(NPC, 50.f);
	TestTrue(TEXT("The step starts"), ASC->TryActivateAbilitiesByTag(Dodge));
	TestEqual(TEXT("It played"), Montages->GetActionsPlayed(), 1);
	TestTrue(TEXT("A step"), Montages->GetLastAction() == EVCAnimAction::Dodge);
	TestEqual(TEXT("15 spent"), GetStamina(NPC), 35.f);
	TestTrue(TEXT("Before its recovery"), Montages->IsMoveBeforeRecovery());

	// No second move before the recovery.
	Advance(W.World, 0.4f);
	TestFalse(TEXT("A second before the recovery: refused"), ASC->TryActivateAbilitiesByTag(Dodge) && GetStamina(NPC) < 35.f);
	TestEqual(TEXT("Still 35"), GetStamina(NPC), 35.f);

	// After it, a roll the owner asked for: 20, and the capsule turned to its direction.
	Advance(W.World, 0.4f);
	Montages->SetRequestedMove(FVCMoveRequest(EVCMoveKind::Roll, 0, FVector(0.0, 1.0, 0.0)));
	TestTrue(TEXT("The roll starts"), ASC->TryActivateAbilitiesByTag(Dodge));
	TestTrue(TEXT("A roll"), Montages->GetLastAction() == EVCAnimAction::Roll);
	TestEqual(TEXT("20 spent"), GetStamina(NPC), 15.f);
	TestTrue(TEXT("Turned to the request's direction"), FMath::IsNearlyEqual(NPC->GetActorRotation().Yaw, 90.0, 0.5));

	// A roll short of its 20 (15 left) is refused, a step (15) is not.
	Advance(W.World, 0.8f);
	Montages->SetRequestedMove(FVCMoveRequest(EVCMoveKind::Roll, 0, FVector(1.0, 0.0, 0.0)));
	ASC->TryActivateAbilitiesByTag(Dodge);
	TestEqual(TEXT("Short of a roll: nothing spent"), GetStamina(NPC), 15.f);
	TestTrue(TEXT("Short of a roll: not turned"), FMath::IsNearlyEqual(NPC->GetActorRotation().Yaw, 90.0, 0.5));
	Montages->SetRequestedMove(FVCMoveRequest(EVCMoveKind::Dodge, static_cast<int32>(EVCHitDirection::Left)));
	TestTrue(TEXT("A step on the last 15"), ASC->TryActivateAbilitiesByTag(Dodge));
	TestEqual(TEXT("None left"), GetStamina(NPC), 0.f);

	// In the air: refused whatever the stamina.
	Advance(W.World, 0.8f);
	SetStamina(NPC, 100.f);
	NPC->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	ASC->TryActivateAbilitiesByTag(Dodge);
	TestEqual(TEXT("In the air: nothing spent"), GetStamina(NPC), 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCDodge_Immunity, "VoxelCharacter.Combat.Dodge.Immunity", VC_DODGE_FLAGS)
bool FVCDodge_Immunity::RunTest(const FString& Parameters)
{
	using namespace VCDodgeTestHelpers;
	VCTesting::FPlayWorld W(TEXT("VCDodgeImmunityWorld"), /*bDestroyActorsFirst=*/true);
	AVCNPCCharacterBase* Dodger = SpawnDodger(W.World, CGFGameplayTags::Faction_Monster, FVector::ZeroVector, false, MakeMoveSet());
	AVCNPCCharacterBase* Attacker = SpawnDodger(W.World, CGFGameplayTags::Faction_Player, FVector(150.0, 0.0, 0.0), false, nullptr);
	UAbilitySystemComponent* ASC = Dodger ? Dodger->GetAbilitySystemComponent() : nullptr;
	if (!TestNotNull(TEXT("Dodger"), Dodger) || !TestNotNull(TEXT("Attacker"), Attacker) || !TestNotNull(TEXT("ASC"), ASC))
	{
		return false;
	}
	SetStamina(Dodger, 100.f);
	const auto Hit = [&]()
	{
		const FCGFDamageContext Context = UVCCombatStatics::MakeDamageContext(Attacker, Attacker, 10.f, CGFGameplayTags::Damage_Type_Physical);
		return UVCCombatStatics::ApplyDamageToActor(Dodger, Context);
	};

	TestTrue(TEXT("The step starts"), ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(VCGameplayTags::Ability_Dodge)));
	TestTrue(TEXT("Immune at the start"), ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Invulnerable));
	Advance(W.World, 0.1f);
	const float Before = Dodger->CombatComponent->GetHealth();
	TestTrue(TEXT("A hit in the window is refused"), Hit() == ECGFDamageResult::Rejected_Invulnerable);
	TestEqual(TEXT("No damage in the window"), Dodger->CombatComponent->GetHealth(), Before);
	Advance(W.World, 0.15f);
	TestTrue(TEXT("Still immune at 0.25 s"), ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Invulnerable));

	// The travelled marker is at 0.3 s: past it the immunity is gone (the move still plays to its recovery).
	Advance(W.World, 0.15f);
	TestFalse(TEXT("No longer immune at 0.4 s"), ASC->HasMatchingGameplayTag(CGFGameplayTags::State_Invulnerable));
	TestTrue(TEXT("Still before the recovery"), Dodger->ActionMontages->IsMoveBeforeRecovery());
	TestTrue(TEXT("A hit after the window lands"), Hit() == ECGFDamageResult::Applied);
	TestTrue(TEXT("Damage after the window"), Dodger->CombatComponent->GetHealth() < Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCDodge_CancelsSwing, "VoxelCharacter.Combat.Dodge.CancelsSwing", VC_DODGE_FLAGS)
bool FVCDodge_CancelsSwing::RunTest(const FString& Parameters)
{
	using namespace VCDodgeTestHelpers;
	VCTesting::FPlayWorld W(TEXT("VCDodgeCancelWorld"), /*bDestroyActorsFirst=*/true);
	AVCNPCCharacterBase* Attacker = SpawnDodger(W.World, CGFGameplayTags::Faction_Player, FVector::ZeroVector, true, MakeMoveSet());
	AVCNPCCharacterBase* Victim = SpawnDodger(W.World, CGFGameplayTags::Faction_Monster, FVector(150.0, 0.0, 0.0), false, nullptr);
	AVCNPCCharacterBase* Control = SpawnDodger(W.World, CGFGameplayTags::Faction_Player, FVector(0.0, 600.0, 0.0), true, MakeMoveSet());
	AVCNPCCharacterBase* ControlVictim = SpawnDodger(W.World, CGFGameplayTags::Faction_Monster, FVector(150.0, 600.0, 0.0), false, nullptr);
	if (!TestNotNull(TEXT("Attacker"), Attacker) || !TestNotNull(TEXT("Victim"), Victim) || !TestNotNull(TEXT("Control"), Control)
		|| !TestNotNull(TEXT("Control victim"), ControlVictim))
	{
		return false;
	}
	SetStamina(Attacker, 100.f);
	const FGameplayTagContainer Melee(CGFGameplayTags::Ability_Attack_Melee);

	// The control swing (hit at 0.3 s) lands; the same swing with a step at 0.1 s does not.
	const float ControlBefore = ControlVictim->CombatComponent->GetHealth();
	const float Before = Victim->CombatComponent->GetHealth();
	TestTrue(TEXT("The control swing starts"), Control->GetAbilitySystemComponent()->TryActivateAbilitiesByTag(Melee));
	TestTrue(TEXT("The swing starts"), Attacker->GetAbilitySystemComponent()->TryActivateAbilitiesByTag(Melee));
	Advance(W.World, 0.1f);
	TestTrue(TEXT("The step starts in the wind-up"), Attacker->GetAbilitySystemComponent()->TryActivateAbilitiesByTag(FGameplayTagContainer(VCGameplayTags::Ability_Dodge)));
	TestEqual(TEXT("The combo is forgotten"), Attacker->ActionMontages->GetLastAttackIndex(), -1);
	Advance(W.World, 0.5f);
	TestTrue(TEXT("The control swing landed"), ControlVictim->CombatComponent->GetHealth() < ControlBefore);
	TestEqual(TEXT("The cancelled swing did not land"), Victim->CombatComponent->GetHealth(), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCDodge_AttackAfter, "VoxelCharacter.Combat.Dodge.AttackAfter", VC_DODGE_FLAGS)
bool FVCDodge_AttackAfter::RunTest(const FString& Parameters)
{
	using namespace VCDodgeTestHelpers;
	VCTesting::FPlayWorld W(TEXT("VCDodgeAttackAfterWorld"), /*bDestroyActorsFirst=*/true);
	AVCNPCCharacterBase* NPC = SpawnDodger(W.World, CGFGameplayTags::Faction_Player, FVector::ZeroVector, true, MakeMoveSet());
	if (!TestNotNull(TEXT("Fighter"), NPC))
	{
		return false;
	}
	UVCActionMontageComponent* Montages = NPC->ActionMontages;

	// The pure rule.
	TestFalse(TEXT("No move"), UVCActionMontageComponent::IsBeforeRecovery(-1.0, 0.7f));
	TestTrue(TEXT("At the start"), UVCActionMontageComponent::IsBeforeRecovery(0.0, 0.7f));
	TestTrue(TEXT("Before the recovery"), UVCActionMontageComponent::IsBeforeRecovery(0.69, 0.7f));
	TestFalse(TEXT("At the recovery"), UVCActionMontageComponent::IsBeforeRecovery(0.7, 0.7f));
	TestFalse(TEXT("Within the allowance"), UVCActionMontageComponent::IsBeforeRecovery(0.55, 0.7f, 0.2f));

	// An attack, then a step back at its recovery (0.6 s): the combo starts over after it.
	float Rate = 0.f;
	TestEqual(TEXT("The first attack"), Montages->ChooseAttack(1.f, Rate), 0);
	Advance(W.World, 0.6f);
	Montages->RecordMove(EVCMoveKind::Dodge, static_cast<int32>(EVCHitDirection::Back));
	TestTrue(TEXT("The step is before its recovery"), Montages->IsMoveBeforeRecovery());

	// A press in the step is kept until its recovery (0.7 s), then due.
	Montages->BufferAttack();
	Advance(W.World, 0.4f);
	TestFalse(TEXT("Not due in the step"), Montages->TakeDueBufferedAttack());
	TestTrue(TEXT("Still kept"), Montages->HasBufferedAttack());
	Advance(W.World, 0.35f);
	TestFalse(TEXT("The step is past its recovery"), Montages->IsMoveBeforeRecovery());
	TestTrue(TEXT("Due at the recovery"), Montages->TakeDueBufferedAttack());
	TestEqual(TEXT("The combo's first attack, not its second"), Montages->ChooseAttack(1.f, Rate), 0);

	// The server: no attack before a step's recovery less the allowance (0.7 - 0.2 s), then the first of the combo.
	UAbilitySystemComponent* ASC = NPC->GetAbilitySystemComponent();
	SetStamina(NPC, 100.f);
	Advance(W.World, 1.f);
	TestTrue(TEXT("A step"), ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(VCGameplayTags::Ability_Dodge)));
	const int32 Played = Montages->GetActionsPlayed();
	Advance(W.World, 0.3f);
	Montages->SetRequestedAttack(0);
	ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(CGFGameplayTags::Ability_Attack_Melee));
	TestEqual(TEXT("An attack in the step is refused"), Montages->GetActionsPlayed(), Played);
	Advance(W.World, 0.25f);
	Montages->SetRequestedAttack(0);
	TestTrue(TEXT("Within the allowance of its recovery: taken"), ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(CGFGameplayTags::Ability_Attack_Melee)));
	TestEqual(TEXT("The first attack"), Montages->GetLastAttackIndex(), 0);
	TestFalse(TEXT("The attack ends the step's record"), Montages->IsMoveBeforeRecovery());
	return true;
}

#endif // WITH_AUTOMATION_TESTS
