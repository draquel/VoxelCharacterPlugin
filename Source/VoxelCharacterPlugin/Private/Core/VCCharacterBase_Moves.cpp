// Copyright Daniel Raquel. All Rights Reserved.

// AVCCharacterBase: the moves the owner plays at its input and the server plays from one request (the sword moves plan,
// D165): the request's dispatch, and the dodge and the roll (D170).

#include "Core/VCCharacterBase.h"
#include "Core/VCPlayerState.h"
#include "Core/VCGameplayTags.h"
#include "Animation/VCActionAnimSet.h"
#include "Animation/VCActionMontageComponent.h"
#include "Combat/VCDodgeAbility.h"
#include "Movement/VCGravityFrame.h"
#include "Movement/VCMovementComponent.h"
#include "VoxelCharacterPlugin.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/IConsoleManager.h"
#include "InputActionValue.h"

namespace
{
	TAutoConsoleVariable<int32> CVarMoveFlushBeforeRequest(
		TEXT("vc.Net.MoveFlush"), 1,
		TEXT("The owner sends its pending moves before a dodge's or a roll's request (the sword moves plan, D170), so the server replays the moves made before the press without the move's root motion: 1 on (the default), 0 off (phase A's form; the measurement's A/B)."),
		ECVF_Default);
}

bool AVCCharacterBase::TryStartDodge()
{
	if (!CombatComponent || IsIncapacitated() || !ActionMontages)
	{
		return false;
	}
	const AVCPlayerState* PS = GetPlayerState<AVCPlayerState>();
	const UVCDodgeAbility* Dodge = PS && PS->DodgeAbilityClass ? Cast<UVCDodgeAbility>(PS->DodgeAbilityClass->GetDefaultObject()) : nullptr;
	UCharacterMovementComponent* Move = GetCharacterMovement();
	if (!Dodge || !Move)
	{
		return false;
	}

	// The movement input of this frame, else the last one, else what the movement accelerates by.
	FVector Input = GetPendingMovementInputVector();
	if (Input.IsNearlyZero())
	{
		Input = GetLastMovementInputVector();
	}
	if (Input.IsNearlyZero())
	{
		Input = Move->GetCurrentAcceleration();
	}
	const UVCActionAnimSet* Set = ActionMontages->GetCurrentActionAnimSet();
	const FVCMoveRequest Request = UVCDodgeAbility::ChooseMove(Input, GetActorForwardVector(), FVCGravityFrame::GetUp(Move),
		IsFacingLockedToView(), Set && Set->Roll);
	const int32 Index = Request.Kind == EVCMoveKind::Dodge ? Request.Index : 0;
	// The server's check, from what this machine holds (the stamina replicates).
	if (!Dodge->CanStartMove(this, Request.Kind, Index, 0.f))
	{
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s: no %s now"), *GetName(), Request.Kind == EVCMoveKind::Roll ? TEXT("roll") : TEXT("dodge"));
		return false;
	}
	// No guard during the move: the raised one comes down (the server's ability cancels its block). A charge ends with
	// the move (D167): its montage gives way to the move's.
	StopBlock();
	ActionMontages->EndCharge();
	if (HasAuthority())
	{
		// A listen server's or a standalone player: the ability turns, plays and holds the immunity on this machine.
		return ActivateMove(Request);
	}
	// The moves made before the press reach the server before the request, so it replays them without the move's root
	// motion, as an ability's activation does (GAS flushes them too).
	if (CVarMoveFlushBeforeRequest.GetValueOnGameThread() != 0)
	{
		Move->FlushServerMoves();
	}
	if (Request.Kind == EVCMoveKind::Roll)
	{
		SetActorRotation(FVCGravityFrame::FacingRotation(Move, Request.Direction));
	}
	ActionMontages->RecordMove(Request.Kind, Index);
	ActionMontages->PlayActionLocal(Request.Kind == EVCMoveKind::Roll ? EVCAnimAction::Roll : EVCAnimAction::Dodge, Index, 1.f);
	Server_RequestMove(Request);
	return true;
}

void AVCCharacterBase::Input_Dodge(const FInputActionValue& Value)
{
	TryStartDodge();
}

void AVCCharacterBase::Server_RequestMove_Implementation(const FVCMoveRequest& Request)
{
	// The abilities check the rest (the combo's timing, the move's cost and state); an index out of range is theirs to fix.
	if (!ActivateMove(Request))
	{
		UE_LOG(LogVoxelCharacter, Verbose, TEXT("%s: the move %d (%d) was not taken"), *GetName(), static_cast<int32>(Request.Kind), Request.Index);
	}
}

bool AVCCharacterBase::ActivateMove(const FVCMoveRequest& Request)
{
	switch (Request.Kind)
	{
	case EVCMoveKind::Attack:
		return ActivateMeleeSwing(Request.Index);
	case EVCMoveKind::Charge:
		// The charge's begin, lowering or stop (D167): the server's clock and what everyone else sees.
		if (Request.Index < 0 || Request.Index > static_cast<int32>(EVCChargeChange::Stop))
		{
			return false;
		}
		ApplyChargeChange(static_cast<EVCChargeChange>(Request.Index));
		return true;
	case EVCMoveKind::Dodge:
	case EVCMoveKind::Roll:
		// A dodge or a roll ends a charge (D167): the server's clock with it.
		if (ActionMontages)
		{
			ActionMontages->EndCharge();
		}
		break;
	default:
		return false;
	}
	UAbilitySystemComponent* ASC = GetAbilitySystemComponent();
	if (!ASC || !ActionMontages || IsIncapacitated())
	{
		return false;
	}
	ActionMontages->SetRequestedMove(Request);
	const bool bActivated = ASC->TryActivateAbilitiesByTag(FGameplayTagContainer(VCGameplayTags::Ability_Dodge));
	// A refused activation leaves no request for a later one.
	FVCMoveRequest Unused;
	ActionMontages->TakeRequestedMove(Unused);
	return bActivated;
}

bool AVCCharacterBase::IsFacingLockedToView() const
{
	const UVCMovementComponent* Move = Cast<UVCMovementComponent>(GetCharacterMovement());
	return bUseControllerRotationYaw || CurrentViewMode == EVCViewMode::FirstPerson || (Move && Move->WantsToFaceAim());
}

bool AVCCharacterBase::CanJumpInternal_Implementation() const
{
	// The server allows a remote owner's jump a trip's allowance early: the owner's own clock started first.
	if (ActionMontages && ActionMontages->IsMoveBeforeRecovery(UVCActionMontageComponent::GetRequestTolerance(this)))
	{
		return false;
	}
	return Super::CanJumpInternal_Implementation();
}
