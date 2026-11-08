// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Testing/VCTestWorld.h"

#if WITH_AUTOMATION_TESTS

#include "Animation/VCLocomotionAnimSet.h"
#include "Core/VCAnimInstance.h"
#include "Core/VCGameplayTags.h"
#include "Core/VCNPCCharacterBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "SkeletalMeshComponentBudgeted.h"

#if WITH_INVENTORY_PLUGIN
#include "Data/Fragments/ItemFragment_Equipment.h"
#include "Data/ItemDefinition.h"
#endif

// ---------------------------------------------------------------------------
// The NPCs' bodies (character animation P7, CHARACTER_ANIMATION_PLAN.md):
// - A body's own locomotion: the creature template's blend space and idle variant come from the set; the variant takes
//   turns with the idle while the body stands, not while it moves or once it is dead; the run's own speed paces it.
// - An NPC's mesh is budgeted (the Animation Budget Allocator) with its significance by distance.
// - The item an NPC holds: its mesh on the hand at the item's offset, its action set the NPC's.
// ---------------------------------------------------------------------------

#define VC_NPC_BODY_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCNPCBodies_CreatureLocomotion, "VoxelCharacter.Anim.CreatureLocomotion", VC_NPC_BODY_FLAGS)
bool FVCNPCBodies_CreatureLocomotion::RunTest(const FString& Parameters)
{
	UVCLocomotionAnimSet* Set = NewObject<UVCLocomotionAnimSet>();
	Set->Locomotion = NewObject<UBlendSpace1D>();
	Set->IdleVariant = NewObject<UAnimSequence>();
	Set->ReferenceSpeed = 445.f;
	Set->IdleVariantAfter = 2.f;
	Set->IdleVariantFor = 1.f;

	USkeletalMeshComponent* Mesh = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	UVCAnimInstance* Anim = NewObject<UVCAnimInstance>(Mesh);
	FVCAnimInput Standing;
	Standing.DeltaSeconds = 0.1f;
	Standing.bOnGround = true;
	Standing.LocomotionSet = Set;
	const auto StandFor = [&](float Seconds)
	{
		for (float Elapsed = 0.f; Elapsed < Seconds - 1e-3f; Elapsed += Standing.DeltaSeconds)
		{
			Anim->ComputeValues(Standing);
		}
	};

	Anim->ComputeValues(Standing);
	TestTrue(TEXT("The set's blend space"), Anim->LocomotionBlendSpace == Set->Locomotion);
	TestTrue(TEXT("The set's idle variant"), Anim->IdleVariantAnim == Set->IdleVariant);
	StandFor(1.8f);
	TestFalse(TEXT("Standing under the wait: the idle"), Anim->bIdleVariant);
	StandFor(0.4f);
	TestTrue(TEXT("Standing past the wait: the variant"), Anim->bIdleVariant);
	StandFor(1.0f);
	TestFalse(TEXT("Past the variant's time: the idle again"), Anim->bIdleVariant);
	StandFor(2.0f);
	TestTrue(TEXT("And the variant again after the same wait"), Anim->bIdleVariant);

	FVCAnimInput Moving = Standing;
	Moving.Velocity = FVector(890.0, 0.0, 0.0);
	Anim->ComputeValues(Moving);
	TestFalse(TEXT("Moving: no variant"), Anim->bIdleVariant);
	TestEqual(TEXT("Twice the run's own speed: twice the pace"), Anim->LocomotionPlayRate, 2.f, 1e-3f);
	StandFor(1.0f);
	TestFalse(TEXT("Stopping starts the wait over"), Anim->bIdleVariant);

	FVCAnimInput Dead = Standing;
	Dead.bDead = true;
	for (int32 Update = 0; Update < 40; ++Update)
	{
		Anim->ComputeValues(Dead);
	}
	TestFalse(TEXT("Dead: no variant"), Anim->bIdleVariant);

	FVCAnimInput Mannequin = Standing;
	Mannequin.LocomotionSet = nullptr;
	Anim->ComputeValues(Mannequin);
	TestTrue(TEXT("No set (a Mannequin body): no blend space for the template"), Anim->LocomotionBlendSpace == nullptr);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCNPCBodies_BudgetAndHeldItem, "VoxelCharacter.NPC.BodyBudgetAndHeldItem", VC_NPC_BODY_FLAGS)
bool FVCNPCBodies_BudgetAndHeldItem::RunTest(const FString& Parameters)
{
	VCTesting::FPlayWorld Play(TEXT("VCNPCBodiesWorld"), /*bDestroyActorsFirst=*/true);
	UWorld* World = Play.World;
	if (!TestNotNull(TEXT("A world"), World))
	{
		return false;
	}

#if WITH_INVENTORY_PLUGIN
	UItemDefinition* Sword = NewObject<UItemDefinition>(GetTransientPackage(), TEXT("VCTest_HeldSword"));
	Sword->AddToRoot();
	UItemFragment_Equipment* Equipment = NewObject<UItemFragment_Equipment>(Sword);
	Equipment->EquipMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Equipment->ActionSetTag = VCGameplayTags::Equipment_AnimSet_SwordShield.GetTag();
	Equipment->AttachOffset = FTransform(FVector(1.0, 2.0, 3.0));
	Sword->Fragments.Add(Equipment);
#endif

	const FTransform Transform(FVector(0.0, 0.0, 200.0));
	AVCNPCCharacterBase* NPC = World->SpawnActorDeferred<AVCNPCCharacterBase>(AVCNPCCharacterBase::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!TestNotNull(TEXT("An NPC"), NPC))
	{
		return false;
	}
	NPC->bAIEnabled = false;
#if WITH_INVENTORY_PLUGIN
	NPC->HeldItem = Sword;
#endif
	NPC->FinishSpawning(Transform);

	const USkeletalMeshComponentBudgeted* Budgeted = Cast<USkeletalMeshComponentBudgeted>(NPC->GetMesh());
	TestNotNull(TEXT("The NPC's mesh is budgeted"), Budgeted);
	TestTrue(TEXT("Its significance by distance"), Budgeted && Budgeted->GetAutoCalculateSignificance());

#if WITH_INVENTORY_PLUGIN
	TestEqual(TEXT("The held item's action set is the NPC's"), NPC->GetAnimActionSet(), VCGameplayTags::Equipment_AnimSet_SwordShield.GetTag());
	UStaticMeshComponent* Held = nullptr;
	for (UActorComponent* Component : NPC->GetComponents())
	{
		if (UStaticMeshComponent* Static = Cast<UStaticMeshComponent>(Component); Static && Static->GetAttachParent() == NPC->GetMesh())
		{
			Held = Static;
		}
	}
	if (TestNotNull(TEXT("The held item's mesh"), Held))
	{
		TestTrue(TEXT("Its mesh"), Held->GetStaticMesh() == Equipment->EquipMesh.Get());
		TestEqual(TEXT("On the main hand"), Held->GetAttachSocketName(), FName(TEXT("hand_r")));
		TestTrue(TEXT("At the item's offset"), Held->GetRelativeLocation().Equals(FVector(1.0, 2.0, 3.0)));
	}
	NPC->ActionSet = VCGameplayTags::Equipment_AnimSet_Unarmed.GetTag();
	TestEqual(TEXT("A set the NPC names comes first"), NPC->GetAnimActionSet(), VCGameplayTags::Equipment_AnimSet_Unarmed.GetTag());
	Sword->RemoveFromRoot();
#endif
	return true;
}

#undef VC_NPC_BODY_FLAGS

#endif // WITH_AUTOMATION_TESTS
