// Copyright Daniel Raquel. All Rights Reserved.

// AVCCharacterBase: the bow's reload (the bow reload plan, D98-D101). The server keeps the state: an arrow nocked or not,
// replicated. It reloads on its own once the weapon is in the hand and empty, plays the reload to everyone and nocks at the
// montage's nock time, which it reads from the asset (a dedicated server plays nothing). Every machine shows the arrow in
// the hand from the reload's grab to the loose, and the arrows standing in a worn quiver.

#include "Core/VCCharacterBase.h"
#include "Animation/VCActionAnimSet.h"
#include "Animation/VCActionMontageComponent.h"
#include "Combat/VCRangedAttackAbility.h"
#include "Combat/VCCombatComponent.h"
#include "Tags/CGFGameplayTags.h"
#include "VoxelCharacterPlugin.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Animation/AnimMontage.h"

#if WITH_EQUIPMENT_PLUGIN
#include "Components/EquipmentManagerComponent.h"
#include "Types/EquipmentSystemTypes.h"
#endif

// ---------------------------------------------------------------------------
// The state (server)
// ---------------------------------------------------------------------------

EVCReloadFrom AVCCharacterBase::GetReloadFrom() const
{
#if WITH_EQUIPMENT_PLUGIN
	if (EquipmentManager && EquipmentManager->IsSlotOccupied(CGFGameplayTags::Equipment_Slot_Quiver))
	{
		return EVCReloadFrom::Quiver;
	}
#endif
	return EVCReloadFrom::Bag;
}

bool AVCCharacterBase::CanReload() const
{
	const UWorld* World = GetWorld();
	if (!World || !bRangedWeaponInHand || bArrowNocked || bReloading || bServerDrawing || IsIncapacitated()
		|| World->GetTimeSeconds() < NextReloadTime)
	{
		return false;
	}
	// A weapon coming out of or going back to its sheath is not loaded mid-way.
	const FTimerManager& Timers = World->GetTimerManager();
	if (Timers.IsTimerActive(SheathStepTimer) || Timers.IsTimerActive(SheathHandOffTimer))
	{
		return false;
	}
	return UVCRangedAttackAbility::HasAmmoToLoad(this);
}

void AVCCharacterBase::UpdateBowReload()
{
	if (HasAuthority() && CanReload())
	{
		StartReload();
	}
}

void AVCCharacterBase::StartReload()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	bReloading = true;
	const EVCReloadFrom From = GetReloadFrom();
	const UAnimMontage* Montage = ActionMontages ? ActionMontages->GetMontage(EVCAnimAction::Reload, static_cast<int32>(From)) : nullptr;
	// The nock is the montage's action point, read from the asset as a swing's hit is (the server need not play it).
	const float NockTime = Montage ? UVCActionAnimSet::GetHitTime(Montage, 0.45f) : 0.f;
	if (ActionMontages)
	{
		ActionMontages->PlayActionForEveryone(EVCAnimAction::Reload, static_cast<int32>(From));
	}
	UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s reloads from the %s (nock in %.2f s)"), *GetName(),
		From == EVCReloadFrom::Quiver ? TEXT("quiver") : TEXT("bag"), NockTime);
	if (NockTime > 0.f)
	{
		World->GetTimerManager().SetTimer(ReloadNockTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { FinishReload(); }), NockTime, false);
	}
	else
	{
		FinishReload();
	}
}

void AVCCharacterBase::FinishReload()
{
	if (!bReloading)
	{
		return;
	}
	bReloading = false;
	// The weapon left the hand, the archer went down or the last arrow went: no nock (the next reload decides again).
	if (!bRangedWeaponInHand || IsIncapacitated() || !UVCRangedAttackAbility::HasAmmoToLoad(this))
	{
		return;
	}
	SetArrowNocked(true);
}

void AVCCharacterBase::CancelReload(float RetryDelay)
{
	if (const UWorld* World = GetWorld())
	{
		NextReloadTime = FMath::Max(NextReloadTime, World->GetTimeSeconds() + RetryDelay);
		GetWorldTimerManager().ClearTimer(ReloadNockTimer);
	}
	if (!bReloading)
	{
		return;
	}
	bReloading = false;
	if (ActionMontages)
	{
		ActionMontages->StopActionForEveryone(EVCAnimAction::Reload);
	}
}

void AVCCharacterBase::HandleArrowLoosed()
{
	if (!HasAuthority())
	{
		return;
	}
	SetArrowNocked(false);
	// The release's loose plays first; then the hand goes for the next arrow (D101).
	if (const UWorld* World = GetWorld())
	{
		NextReloadTime = FMath::Max(NextReloadTime, World->GetTimeSeconds() + ReloadAfterShotSeconds);
	}
}

void AVCCharacterBase::SetArrowNocked(bool bNocked)
{
	if (!HasAuthority() || bArrowNocked == bNocked)
	{
		return;
	}
	bArrowNocked = bNocked;
	HandleNockChanged();
}

void AVCCharacterBase::OnRep_ArrowNocked()
{
	HandleNockChanged();
}

void AVCCharacterBase::HandleNockChanged()
{
	// The owner pressed during the reload and still holds the press: the draw starts now (D98).
	if (bArrowNocked && bDrawWaitsForNock && IsLocallyControlled())
	{
		bDrawWaitsForNock = false;
		TryStartRangedAttack();
	}
}

void AVCCharacterBase::HandleDamagedForReload(const FCGFDamageContext& Context)
{
	// A hit breaks a reload off; the flinch plays before the next (D101). A hit on the guard is no flinch.
	if (HasAuthority() && bReloading && !Context.ContextTags.HasTagExact(CGFGameplayTags::Damage_Blocked))
	{
		CancelReload(ReloadAfterHitSeconds);
	}
}

void AVCCharacterBase::HandleInventoryChangedForReload()
{
	if (HasAuthority())
	{
		RefreshArrowMesh();
	}
}

// ---------------------------------------------------------------------------
// What is seen (every machine)
// ---------------------------------------------------------------------------

void AVCCharacterBase::UpdateHeldArrow()
{
	bool bShow = false;
	bool bOnString = false;
	const UVCActionAnimSet* Set = bRangedWeaponInHand && ActionMontages ? ActionMontages->GetCurrentActionAnimSet() : nullptr;
	if (Set && Set->bShowsHeldArrow && ArrowMesh)
	{
		float Position = 0.f;
		if (const UAnimMontage* Reload = ActionMontages->GetPlayingActionMontage(EVCAnimAction::Reload, Position))
		{
			// In the hand from the grab (the reload's hand-off), on the string from its nock (its action point).
			bShow = Position >= UVCActionAnimSet::GetHandOffTime(Reload, 0.15f);
			bOnString = Position >= UVCActionAnimSet::GetHitTime(Reload, 0.45f);
		}
		else
		{
			// Nocked, until the loose: the release's montage carries the empty hand back.
			bShow = bOnString = bArrowNocked && !ActionMontages->IsActionPlaying(EVCAnimAction::Release);
		}
	}
	if (!bShow)
	{
		if (HeldArrowComponent && HeldArrowComponent->IsVisible())
		{
			HeldArrowComponent->SetVisibility(false);
		}
		return;
	}
	if (!HeldArrowComponent)
	{
		HeldArrowComponent = NewObject<UStaticMeshComponent>(this, TEXT("HeldArrow"));
		HeldArrowComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		HeldArrowComponent->SetGenerateOverlapEvents(false);
		HeldArrowComponent->SetCanEverAffectNavigation(false);
		HeldArrowComponent->RegisterComponent();
	}
	if (HeldArrowComponent->GetStaticMesh() != ArrowMesh)
	{
		HeldArrowComponent->SetStaticMesh(ArrowMesh);
	}
	if (HeldArrowComponent->GetAttachParent() != GetMesh() || HeldArrowComponent->GetAttachSocketName() != Set->HeldArrowBone)
	{
		HeldArrowComponent->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, Set->HeldArrowBone);
	}
	if (bOnString)
	{
		// The nock with the drawing hand, the shaft toward the bow hand: it follows the string through the draw.
		const USkeletalMeshComponent* Body = GetMesh();
		const FVector Nock = Body->GetSocketTransform(Set->HeldArrowBone).TransformPosition(Set->NockedArrowOffset);
		const FVector Bow = Body->GetSocketLocation(Set->BowHandBone);
		const FVector Along = (Bow - Nock).GetSafeNormal();
		if (!Along.IsNearlyZero())
		{
			HeldArrowComponent->SetWorldLocationAndRotation(Nock, FRotationMatrix::MakeFromZ(Along).ToQuat());
		}
	}
	else
	{
		HeldArrowComponent->SetRelativeTransform(Set->HeldArrowGrip);
	}
	HeldArrowComponent->SetVisibility(true);
}

void AVCCharacterBase::RefreshArrowMesh()
{
	UStaticMesh* AmmoMesh = UVCRangedAttackAbility::LoadAmmoMesh(this);
	if (ArrowMesh != AmmoMesh)
	{
		ArrowMesh = AmmoMesh;
		UpdateQuiverArrows();
	}
}

void AVCCharacterBase::OnRep_ArrowMesh()
{
	UpdateQuiverArrows();
}

void AVCCharacterBase::UpdateQuiverArrows()
{
	USceneComponent* Quiver = nullptr;
#if WITH_EQUIPMENT_PLUGIN
	if (EquipmentManager)
	{
		for (const FEquipmentSlot& Slot : EquipmentManager->EquipmentSlots)
		{
			if (Slot.SlotTag == CGFGameplayTags::Equipment_Slot_Quiver.GetTag() && Slot.bIsOccupied)
			{
				Quiver = Slot.AttachedVisualComponent;
			}
		}
	}
#endif
	const bool bShow = ArrowMesh && Quiver && QuiverArrowLayout.Num() > 0;
	if (!bShow)
	{
		for (UStaticMeshComponent* Arrow : QuiverArrowComponents)
		{
			if (Arrow)
			{
				Arrow->SetVisibility(false);
			}
		}
		return;
	}
	while (QuiverArrowComponents.Num() < QuiverArrowLayout.Num())
	{
		UStaticMeshComponent* Arrow = NewObject<UStaticMeshComponent>(this);
		Arrow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Arrow->SetGenerateOverlapEvents(false);
		Arrow->SetCanEverAffectNavigation(false);
		Arrow->RegisterComponent();
		QuiverArrowComponents.Add(Arrow);
	}
	for (int32 Index = 0; Index < QuiverArrowComponents.Num(); ++Index)
	{
		UStaticMeshComponent* Arrow = QuiverArrowComponents[Index];
		if (!Arrow)
		{
			continue;
		}
		const bool bUsed = Index < QuiverArrowLayout.Num();
		if (bUsed)
		{
			if (Arrow->GetStaticMesh() != ArrowMesh)
			{
				Arrow->SetStaticMesh(ArrowMesh);
			}
			Arrow->AttachToComponent(Quiver, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
			Arrow->SetRelativeTransform(QuiverArrowLayout[Index]);
		}
		Arrow->SetVisibility(bUsed);
	}
}
