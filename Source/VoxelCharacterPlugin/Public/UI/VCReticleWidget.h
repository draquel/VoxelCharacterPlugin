// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VCReticleWidget.generated.h"

class UBorder;
class UCanvasPanel;
class AVCCharacterBase;

/**
 * The aim reticle (the character refinement plan's C3): a dot at the screen's centre with four ticks around it, shown
 * while the owning player's character aims a ranged weapon (AVCCharacterBase::IsAiming), in both views. The ticks close
 * on the dot as the owner's draw gains power (GetOwnDrawPower: nothing below the shortest draw, full at the full one);
 * the reticle reddens over a hostile (IsAimOnHostile). Built in code like the other HUD widgets; owned and placed by
 * AVCPlayerController (centre-anchored). It marks the line the arrow leaves on: the arrow flies along the aim ray, and
 * the ray and the aim camera are one computation (UVCCombatStatics::ComputeAimRay, D74, D83); the drop and the wind are
 * the player's to allow for.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCReticleWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** The ticks' distance from the centre with no draw (pixels). */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|UI")
	float OpenSpread = 18.f;

	/** The ticks' distance from the centre at a full draw. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|UI")
	float ClosedSpread = 6.f;

	/** The reticle's colour, and over a hostile. */
	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|UI")
	FLinearColor Color = FLinearColor(1.f, 1.f, 1.f, 0.9f);

	UPROPERTY(EditDefaultsOnly, Category = "VoxelCharacter|UI")
	FLinearColor HostileColor = FLinearColor(1.f, 0.25f, 0.2f, 0.95f);

	/** Pure: the ticks' spread for a draw's power (0 to 1). */
	static float ComputeSpread(float DrawPower, float InOpenSpread, float InClosedSpread);

	/** @return True while the reticle shows. */
	bool IsShown() const { return bShown; }

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;

private:
	void BuildWidgetTree();

	/**
	 * Watches the pawn (shown, the ticks' spread, the colour) on a timer: a C++ widget with no Blueprint tick is not
	 * ticked under the engine's Auto frequency, and the switch is private.
	 */
	void UpdateReticle();

	FTimerHandle UpdateTimer;
	void PlaceTicks(float Spread);
	void SetColor(const FLinearColor& InColor);

	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> Canvas;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> Dot;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> Ticks;

	bool bShown = false;
	float LastSpread = -1.f;
	bool bLastHostile = false;
	float HostileCheckAccumulator = 0.f;
};
