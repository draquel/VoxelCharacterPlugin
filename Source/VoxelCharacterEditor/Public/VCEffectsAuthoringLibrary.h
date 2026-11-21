// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VCEffectsAuthoringLibrary.generated.h"

class UMaterialInterface;
class UNiagaraEmitter;
class UNiagaraSystem;

/**
 * Editor scripting for the character's Niagara effects (the water plan's W1: the splash and the wake). The editor's
 * Python reaches no emitter, renderer or module constant of a Niagara system, so a content script (the parent's
 * Scripts/setup_water_material.py) builds a system here: empty it, add copies of the engine's emitter templates, give
 * each its sprite material and its baked module constants by name, compile. Every function reports what it did as
 * text; none saves (the caller does).
 */
UCLASS()
class VOXELCHARACTEREDITOR_API UVCEffectsAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Removes every emitter from a system, so a rerun builds it again from the templates.
	 * @param System The system
	 * @param OutReport How many emitters were removed
	 * @return True when the system is empty afterwards
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Effects")
	static bool ResetSystem(UNiagaraSystem* System, FString& OutReport);

	/**
	 * Adds a copy of a template emitter to a system and shapes it: its name, local or world space, the material of its
	 * sprite renderers, and its baked module constants (the rapid-iteration parameters of its four scripts). A constant
	 * is found by a substring of its name (the report lists every constant the emitter has, with its type, so a name
	 * that matched nothing can be corrected) and set from the map of its kind: Floats sets float, int (rounded) and
	 * bool (non-zero) constants; Vectors sets 3-vector and position constants, and 2-vector ones from X and Y; Colors
	 * sets colour constants.
	 * @param System The system (a compile is requested by FinishSystem, not here)
	 * @param TemplateEmitter The emitter to copy (an engine template)
	 * @param EmitterName The copy's name in the system
	 * @param Material The sprite renderers' material (null: left as the template's)
	 * @param bLocalSpace Simulate in the component's space (a burst that follows its spawn rotation and scale) or the world's (a trail)
	 * @param Floats Constant name substring -> value
	 * @param Vectors Constant name substring -> value
	 * @param Colors Constant name substring -> value
	 * @param OutReport What was set, what matched nothing, and every constant of the emitter
	 * @return True when the emitter was added and every named constant matched at least one parameter
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Effects")
	static bool AddTemplateEmitter(UNiagaraSystem* System, UNiagaraEmitter* TemplateEmitter, FName EmitterName, UMaterialInterface* Material,
		bool bLocalSpace, const TMap<FString, float>& Floats, const TMap<FString, FVector>& Vectors, const TMap<FString, FLinearColor>& Colors,
		FString& OutReport);

	/**
	 * Compiles a built system and waits for it (a script saves right after: the package must carry the compiled scripts).
	 * @param System The system
	 * @param OutReport The system's shape: one line per emitter with its space, sim target and renderers
	 * @return True when the system has at least one emitter
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|Effects")
	static bool FinishSystem(UNiagaraSystem* System, FString& OutReport);
};
