// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "VCPauseMenuWidget.generated.h"

class UButton;
class UTextBlock;

/**
 * Pause menu (QA R20): Resume, Save and Quit. Opened by AVCPlayerController::TogglePauseMenu (the pause key);
 * every button goes back through the controller, which pauses only standalone / single-player games and routes
 * Save to the server. Code-built, centred on the screen.
 */
UCLASS(BlueprintType, Blueprintable)
class VOXELCHARACTERPLUGIN_API UVCPauseMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Show whether the world is actually paused (a client in a multiplayer game keeps playing behind the menu).
	 * @param bPaused True when the game is paused
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void SetPausedState(bool bPaused);

protected:
	virtual void NativeOnInitialized() override;

	void BuildWidgetTree();

	UFUNCTION()
	void HandleResumeClicked();

	UFUNCTION()
	void HandleSaveClicked();

	UFUNCTION()
	void HandleQuitClicked();

	UPROPERTY()
	TObjectPtr<UTextBlock> TitleText;

	UPROPERTY()
	TObjectPtr<UButton> ResumeButton;

	UPROPERTY()
	TObjectPtr<UButton> SaveButton;

	UPROPERTY()
	TObjectPtr<UButton> QuitButton;
};
