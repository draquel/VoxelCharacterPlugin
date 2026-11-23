// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/LocalPlayer.h"
#include "VCLocalPlayer.generated.h"

/**
 * The project's local player (DefaultEngine.ini's LocalPlayerClassName). In a play-in-editor instance after the first it
 * tells the server its instance number at login (LoginOption), so AVCGameModeBase can keep the save entries of two
 * players on one machine apart: the Null online subsystem gives them one save key otherwise (G271). Outside the editor
 * it adds nothing.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCLocalPlayer : public ULocalPlayer
{
	GENERATED_BODY()

public:
	/** The login option that carries a play-in-editor instance's number. */
	static const TCHAR* LoginOption;

	/** @return "VCPIEInstance=<n>" in a play-in-editor instance after the first; empty otherwise. */
	virtual FString GetGameLoginOptions() const override;
};
