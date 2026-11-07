// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "VCAnimAuthoringLibrary.generated.h"

class UAnimBlueprint;
class UAnimMontage;
class UAnimSequenceBase;
class UBlueprint;

/**
 * Editor scripting for anim blueprints and montages that the engine does not expose to Python (character animation P3,
 * ranged combat plan B4): add an animation layer to an anim layer interface, implement an interface, add graph nodes of
 * any class, set a node's properties or a pin's default from text, connect pins and describe a graph; append a montage's
 * sequences in sections and choose its slot. Scripts/setup_character_layers.py builds the character's layers and
 * montages with it. Every function returns false (or an empty string) and logs a warning when it cannot do what it was
 * asked; none compiles or saves (the caller does, once).
 */
UCLASS()
class VOXELCHARACTEREDITOR_API UVCAnimAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Adds an animation layer to an anim layer interface (or an anim blueprint): a graph with its output pose and, when
	 * named, one input pose. What the editor's "Add Animation Layer" makes.
	 * @param Blueprint     The interface or anim blueprint
	 * @param LayerName     The layer's name
	 * @param InputPoseName The input pose's name; None for a layer with no input
	 * @return True when the layer was added; false when one of that name exists or the blueprint is not an anim one
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static bool AddAnimLayer(UAnimBlueprint* Blueprint, FName LayerName, FName InputPoseName);

	/**
	 * Makes a blueprint implement an interface; for an anim layer interface each layer becomes a graph to fill.
	 * @param Blueprint      The blueprint
	 * @param InterfaceClass The interface's class (an anim layer interface's generated class)
	 * @return True when it implements the interface afterwards
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static bool ImplementInterface(UBlueprint* Blueprint, UClass* InterfaceClass);

	/**
	 * Adds a node of a class to a graph, its pins allocated as when placed in the editor.
	 * @param Blueprint     The blueprint
	 * @param GraphName     The graph (AnimGraph, a layer, a state)
	 * @param NodeClassPath The node class's path ("/Script/AnimGraph.AnimGraphNode_SequencePlayer")
	 * @param X             Position in the graph
	 * @param Y             Position in the graph
	 * @return The node's name, or empty on failure
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static FString AddGraphNode(UBlueprint* Blueprint, FName GraphName, const FString& NodeClassPath, int32 X, int32 Y);

	/**
	 * Sets a property of a node from its text form, then reconstructs the node (its pins follow the property).
	 * @param Blueprint    The blueprint
	 * @param GraphName    The graph
	 * @param NodeName     The node's name (or its title)
	 * @param PropertyPath Dotted path from the node ("Node.Layer", "Node.LayerSetup", "InterfaceGuid")
	 * @param ValueText    The value as property text ("UpperBodyStance", "((BranchFilters=((BoneName=\"spine_01\"))))")
	 * @return True when the property was found and took the value
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static bool SetNodeProperty(UBlueprint* Blueprint, FName GraphName, const FString& NodeName, const FString& PropertyPath,
		const FString& ValueText);

	/**
	 * Points a linked anim layer node at an interface's layer, with the class it runs when nothing else is linked: what
	 * the node's details panel sets (its layer name lives in a member reference besides the node's own fields).
	 * @param Blueprint      The blueprint
	 * @param GraphName      The graph holding the node
	 * @param NodeName       The linked anim layer node
	 * @param InterfaceClass The anim layer interface's generated class
	 * @param LayerName      The layer
	 * @param InstanceClass  The layer's default implementation (an anim blueprint's generated class); null for none
	 * @return True when the node refers to the layer afterwards
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static bool SetLinkedAnimLayer(UBlueprint* Blueprint, FName GraphName, const FString& NodeName, UClass* InterfaceClass, FName LayerName,
		UClass* InstanceClass);

	/**
	 * Sets an input pin's default: an object path for an object pin (a sequence, a blend space), text otherwise.
	 * @param Blueprint The blueprint
	 * @param GraphName The graph
	 * @param NodeName  The node's name (or its title)
	 * @param PinName   The pin's name (or its display name)
	 * @param Value     The object path or the value text
	 * @return True when the pin took the value
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static bool SetPinDefault(UBlueprint* Blueprint, FName GraphName, const FString& NodeName, const FString& PinName, const FString& Value);

	/**
	 * Connects an output pin to an input pin as the graph's schema allows (an input's old link is replaced).
	 * @param Blueprint The blueprint
	 * @param GraphName The graph
	 * @param FromNode  The output's node
	 * @param FromPin   The output pin
	 * @param ToNode    The input's node
	 * @param ToPin     The input pin
	 * @return True when connected
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static bool ConnectPins(UBlueprint* Blueprint, FName GraphName, const FString& FromNode, const FString& FromPin,
		const FString& ToNode, const FString& ToPin);

	/**
	 * Sets any property of an object from its text form, edit flags aside (a data asset's instanced fragment, whose
	 * EditDefaultsOnly properties Python refuses), and marks it changed. The caller saves its package.
	 * @param Object       The object
	 * @param PropertyPath Dotted path from the object ("ActionSetTag", "Node.Layer")
	 * @param ValueText    The value as property text
	 * @return True when the property was found and took the value
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static bool SetObjectProperty(UObject* Object, const FString& PropertyPath, const FString& ValueText);

	/**
	 * A graph's nodes, one a line: "Name (Class) 'Title': pin<in|out>[*connected] ...".
	 * @param Blueprint The blueprint
	 * @param GraphName The graph
	 * @return The description, or empty when there is no such graph
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static FString DescribeGraph(UBlueprint* Blueprint, FName GraphName);

	/**
	 * A graph's guid, as text: what a linked anim layer node's InterfaceGuid names for a layer.
	 * @param Blueprint The blueprint (an anim layer interface)
	 * @param GraphName The graph (the layer)
	 * @return The guid, or empty when there is no such graph
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static FString GetGraphGuid(UBlueprint* Blueprint, FName GraphName);

	/**
	 * Appends a sequence to a montage's first slot track, after what it plays already, in a section of its own that the
	 * section before it leads into (ranged combat plan B4: a draw's start, then its hold looping until it is stopped).
	 * @param Montage      The montage
	 * @param Sequence     The sequence to play next (the montage's skeleton)
	 * @param SectionName  The new section's name (unique in the montage)
	 * @param bLoopSection Whether the new section leads into itself, playing until the montage is stopped or jumps
	 * @return True when the sequence and its section were added
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static bool AppendMontageSegment(UAnimMontage* Montage, UAnimSequenceBase* Sequence, FName SectionName, bool bLoopSection);

	/**
	 * Sets the slot a montage's first track plays in (a montage the factory makes plays in DefaultSlot).
	 * @param Montage  The montage
	 * @param SlotName The slot ("UpperBody"); a slot node of that name plays it
	 * @return True when the montage plays in the slot afterwards
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static bool SetMontageSlot(UAnimMontage* Montage, FName SlotName);
};
