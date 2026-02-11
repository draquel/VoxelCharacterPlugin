// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Core/VCTypes.h"
#include "VCPlayerController.generated.h"

class UVCInputConfig;
class UInputMappingContext;

/**
 * Player controller for the voxel character system.
 *
 * Manages Enhanced Input mapping contexts, input mode switching
 * (gameplay vs UI), and server-authoritative voxel modification RPCs.
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API AVCPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AVCPlayerController();

	/** Input configuration DataAsset (assign in Blueprint or GameMode defaults). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "VoxelCharacter|Input")
	TObjectPtr<UVCInputConfig> InputConfig;

	/** Accessor for the InputConfig (used by character for input binding). */
	const UVCInputConfig* GetInputConfig() const { return InputConfig; }

	// --- Input Mode ---

	/** Switch to game input (hide cursor, capture mouse). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Input")
	void SetGameInputMode();

	/** Switch to UI input (show cursor, release mouse). */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Input")
	void SetUIInputMode(UUserWidget* FocusWidget = nullptr);

	// --- UI Stubs ---

	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|UI")
	void ToggleInventoryUI();

	// --- Server RPCs ---

	/** Request a server-authoritative voxel modification. */
	UFUNCTION(Server, Reliable, Category = "VoxelCharacter|Voxel")
	void Server_RequestVoxelModification(const FIntVector& VoxelCoord, EVoxelModificationType ModType, uint8 MaterialID);

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

	/** Add a mapping context with the given priority. */
	void AddInputMappingContext(const UInputMappingContext* Context, int32 Priority);

	/** Remove a mapping context. */
	void RemoveInputMappingContext(const UInputMappingContext* Context);

	/** Current input mode state. */
	bool bIsInUIMode = false;
};
