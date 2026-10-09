// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Core/VCNPCCharacterBase.h"
#include "Core/VCCharacterAttributeSet.h"
#include "Combat/VCCombatAttributeSet.h"
#include "Combat/VCCombatComponent.h"
#include "Combat/VCCombatStatics.h"
#include "Combat/VCEquipmentStatEffect.h"
#include "Utilities/CGFGameplayEffectStatics.h"
#include "Tags/CGFGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

namespace VCLoopTestHelpers
{
	struct FWorldHarness
	{
		UWorld* World = nullptr;

		FWorldHarness()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("VCEquipmentLoopTestWorld"));
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

	AVCNPCCharacterBase* SpawnCombatant(UWorld* World, float MaxHealth, float Defense)
	{
		const FTransform Transform(FVector::ZeroVector);
		AVCNPCCharacterBase* NPC = World->SpawnActorDeferred<AVCNPCCharacterBase>(AVCNPCCharacterBase::StaticClass(), Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!NPC)
		{
			return nullptr;
		}
		NPC->StartingMaxHealth = MaxHealth;
		NPC->StartingDefense = Defense;
		NPC->DestroyAfterDeathDelay = 0.f;
		NPC->FinishSpawning(Transform);
		return NPC;
	}

	FCGFAttributeModifier Mod(const FGameplayAttribute& Attribute, float Magnitude)
	{
		FCGFAttributeModifier M;
		M.Attribute = Attribute;
		M.Magnitude = Magnitude;
		return M;
	}
}

#define VC_LOOP_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// Equipment stats: the SetByCaller-keyed infinite effect applies and removes cleanly.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCLoop_StatEffectAppliesAndRemoves, "VoxelCharacter.EquipmentLoop.StatEffectAppliesAndRemoves", VC_LOOP_FLAGS)
bool FVCLoop_StatEffectAppliesAndRemoves::RunTest(const FString& Parameters)
{
	VCLoopTestHelpers::FWorldHarness H;
	AVCNPCCharacterBase* Pawn = VCLoopTestHelpers::SpawnCombatant(H.World, 100.f, 1.f);
	if (!Pawn) { AddError(TEXT("Spawn failed")); return false; }
	UAbilitySystemComponent* ASC = Pawn->GetAbilitySystemComponent();

	TArray<FCGFAttributeModifier> Mods;
	Mods.Add(VCLoopTestHelpers::Mod(UVCCombatAttributeSet::GetDefenseAttribute(), 3.f));
	Mods.Add(VCLoopTestHelpers::Mod(UVCCharacterAttributeSet::GetMaxHealthAttribute(), 10.f));
	Mods.Add(VCLoopTestHelpers::Mod(UVCCombatAttributeSet::GetDefenseAttribute(), 2.f));   // same attribute twice accumulates

	const FActiveGameplayEffectHandle Handle = UCGFGameplayEffectStatics::ApplyStatModifierEffect(ASC, UVCEquipmentStatEffect::StaticClass(), Mods, nullptr);
	TestTrue(TEXT("Handle valid"), Handle.IsValid());
	TestEqual(TEXT("Defense 1 + 3 + 2"), ASC->GetNumericAttribute(UVCCombatAttributeSet::GetDefenseAttribute()), 6.f);
	TestEqual(TEXT("MaxHealth 100 + 10"), ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetMaxHealthAttribute()), 110.f);
	TestEqual(TEXT("Unrequested stat untouched (AttackPower)"), ASC->GetNumericAttribute(UVCCombatAttributeSet::GetAttackPowerAttribute()), 0.f);

	// Mitigation now uses the modified Defense: 10 base - 6 = 4.
	UVCCombatStatics::ApplyDamageToActor(Pawn, UVCCombatStatics::MakeDamageContext(nullptr, nullptr, 10.f, FGameplayTag(), true));
	TestEqual(TEXT("Health 100 - (10 - 6)"), Pawn->CombatComponent->GetHealth(), 96.f);

	ASC->RemoveActiveGameplayEffect(Handle);
	TestEqual(TEXT("Defense restored"), ASC->GetNumericAttribute(UVCCombatAttributeSet::GetDefenseAttribute()), 1.f);
	TestEqual(TEXT("MaxHealth restored"), ASC->GetNumericAttribute(UVCCharacterAttributeSet::GetMaxHealthAttribute()), 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCLoop_StatEffectCoverage, "VoxelCharacter.EquipmentLoop.StatEffectCoversAllStats", VC_LOOP_FLAGS)
bool FVCLoop_StatEffectCoverage::RunTest(const FString& Parameters)
{
	const TArray<FGameplayAttribute> Covered = UCGFGameplayEffectStatics::GetStatEffectAttributes(UVCEquipmentStatEffect::StaticClass());
	TestEqual(TEXT("Seven stats covered"), Covered.Num(), 7);
	TestTrue(TEXT("Defense tag resolves"), UCGFGameplayEffectStatics::MakeStatSetByCallerTag(UVCCombatAttributeSet::GetDefenseAttribute()).IsValid());
	TestFalse(TEXT("Health is not an equipment stat"), UCGFGameplayEffectStatics::MakeStatSetByCallerTag(UVCCharacterAttributeSet::GetHealthAttribute()).IsValid());
	return true;
}

// ---------------------------------------------------------------------------
// Consumables: instant modifiers heal and are clamped by MaxHealth.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCLoop_InstantModifiersHeal, "VoxelCharacter.EquipmentLoop.InstantModifiersHealClamped", VC_LOOP_FLAGS)
bool FVCLoop_InstantModifiersHeal::RunTest(const FString& Parameters)
{
	VCLoopTestHelpers::FWorldHarness H;
	AVCNPCCharacterBase* Pawn = VCLoopTestHelpers::SpawnCombatant(H.World, 100.f, 0.f);
	if (!Pawn) { AddError(TEXT("Spawn failed")); return false; }
	UAbilitySystemComponent* ASC = Pawn->GetAbilitySystemComponent();

	UVCCombatStatics::ApplyDamageToActor(Pawn, UVCCombatStatics::MakeDamageContext(nullptr, nullptr, 30.f, FGameplayTag(), true));
	TestEqual(TEXT("Damaged to 70"), Pawn->CombatComponent->GetHealth(), 70.f);

	TArray<FCGFAttributeModifier> Potion;
	Potion.Add(VCLoopTestHelpers::Mod(UVCCharacterAttributeSet::GetHealthAttribute(), 25.f));
	TestTrue(TEXT("Applied"), UCGFGameplayEffectStatics::ApplyInstantAttributeModifiers(ASC, Potion, nullptr));
	TestEqual(TEXT("Healed to 95"), Pawn->CombatComponent->GetHealth(), 95.f);

	TestTrue(TEXT("Applied again"), UCGFGameplayEffectStatics::ApplyInstantAttributeModifiers(ASC, Potion, nullptr));
	TestEqual(TEXT("Clamped at max"), Pawn->CombatComponent->GetHealth(), 100.f);

	TArray<FCGFAttributeModifier> Empty;
	TestFalse(TEXT("Nothing to apply"), UCGFGameplayEffectStatics::ApplyInstantAttributeModifiers(ASC, Empty, nullptr));
	return true;
}

#undef VC_LOOP_FLAGS

#endif // WITH_AUTOMATION_TESTS
