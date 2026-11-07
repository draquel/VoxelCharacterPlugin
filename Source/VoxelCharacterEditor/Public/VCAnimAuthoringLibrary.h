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
 * Editor scripting for anim blueprints and montages that the engine does not expose to Python (character animation P3
 * to P5, ranged combat plan B4): add an animation layer to an anim layer interface, implement an interface, add graph
 * nodes of any class, set a node's properties or a pin's default from text, show a property's pin, connect pins, list
 * and describe graphs, copy nodes between blueprints as text; copy assets with the references among them, rename a
 * member variable, remove nodes, swap one function's calls for another's, recombine a split pin, read a pin's links;
 * append a montage's sequences in sections and choose its slot. Scripts/setup_character_layers.py builds the
 * character's layers and montages with it, Scripts/setup_character_ik.py the IK, Scripts/setup_character_locomotion.py
 * the player's motion matching. Every function returns false (or an empty string) and logs a warning when it cannot do what it was
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

	/**
	 * Every graph of a blueprint by name: the event and anim graphs, functions, layers, state machines and their states
	 * and transitions (character animation P4: reading another anim blueprint's graphs).
	 * @param Blueprint The blueprint
	 * @return The graphs' names, "Outer/Name" for a graph inside another (a state inside a state machine)
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static TArray<FString> ListGraphs(UBlueprint* Blueprint);

	/**
	 * A graph's nodes as the editor copies them (Ctrl+C): every property of each node, as text.
	 * @param Blueprint The blueprint
	 * @param GraphName The graph
	 * @param NodeNames The nodes by name or title; empty for all of them
	 * @return The text, or empty when there is no such graph or none of the nodes
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static FString ExportGraphNodes(UBlueprint* Blueprint, FName GraphName, const TArray<FString>& NodeNames);

	/**
	 * Pastes nodes copied with ExportGraphNodes (or the editor's Ctrl+C) into a graph, as the editor's Ctrl+V does: the
	 * links among the pasted nodes kept, links to nodes left behind dropped, every node given a new name and guid.
	 * @param Blueprint The blueprint
	 * @param GraphName The graph to paste into
	 * @param Text      The copied nodes
	 * @param OffsetX   Moves the pasted nodes by this much
	 * @param OffsetY   Moves the pasted nodes by this much
	 * @return The pasted nodes, one a line as DescribeGraph names them ("Name (Class) 'Title'"); empty when the graph
	 *         refuses the text
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static TArray<FString> ImportGraphNodes(UBlueprint* Blueprint, FName GraphName, const FString& Text, int32 OffsetX, int32 OffsetY);

	/**
	 * Shows or hides an anim node's pin for one of its properties, as the node's details panel does (a property marked
	 * PinHiddenByDefault, such as a look-at node's location, needs its pin shown before a variable can drive it).
	 * @param Blueprint    The anim blueprint
	 * @param GraphName    The graph
	 * @param NodeName     The anim node
	 * @param PropertyName The node's property ("LookAtLocation")
	 * @param bVisible     Whether the pin shows
	 * @return True when the node has an optional pin for the property and it took the setting
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static bool SetNodePinVisible(UBlueprint* Blueprint, FName GraphName, const FString& NodeName, FName PropertyName, bool bVisible);

	/**
	 * Copies assets together and points every reference among them at the copies (character animation P5: GASP's CMC
	 * AnimBP and the choosers that name its class as their context): a copied blueprint's class and default object stand
	 * for the source's. A destination that exists is not copied again, and its references are pointed the same way.
	 * @param SourceToDestination Each source asset's package path to its copy's ("/Game/A/X" to "/Game/B/Y")
	 * @return The copies by package path, in the map's order; empty when a source does not load or a copy fails
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static TArray<FString> CopyAssetsWithReferences(const TMap<FString, FString>& SourceToDestination);

	/**
	 * Renames a blueprint's member variable and every node that reads or writes it.
	 * @param Blueprint The blueprint
	 * @param OldName   The variable
	 * @param NewName   Its new name
	 * @return True when the blueprint has a variable of the new name afterwards
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static bool RenameMemberVariable(UBlueprint* Blueprint, FName OldName, FName NewName);

	/**
	 * Removes nodes from a graph, with their links.
	 * @param Blueprint The blueprint
	 * @param GraphName The graph
	 * @param NodeNames The nodes by name or title
	 * @return How many were removed
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static int32 RemoveGraphNodes(UBlueprint* Blueprint, FName GraphName, const TArray<FString>& NodeNames);

	/**
	 * Swaps every call to one function in a blueprint's graphs for a call to another whose pins have the same names: each
	 * pin's links and default move across, a split pin is split the same way (character animation P5: GASP's world-Z
	 * arithmetic for the gravity frame's). A member of the blueprint's own class or a parent is called on self.
	 * @param Blueprint       The blueprint
	 * @param OldFunctionName The function called now ("VSizeXY")
	 * @param NewOwner        The class that declares the new function
	 * @param NewFunctionName The function to call instead
	 * @return The calls swapped; -1 when the new function does not exist. A pin the new function lacks is logged and its
	 *         links dropped
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static int32 ReplaceFunctionCalls(UBlueprint* Blueprint, FName OldFunctionName, UClass* NewOwner, FName NewFunctionName);

	/**
	 * Recombines a split struct pin (a rotator shown as roll, pitch and yaw) into one pin.
	 * @param Blueprint The blueprint
	 * @param GraphName The graph
	 * @param NodeName  The node
	 * @param PinName   The struct pin ("ReturnValue"), either direction
	 * @return True when the pin is whole afterwards
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static bool RecombinePin(UBlueprint* Blueprint, FName GraphName, const FString& NodeName, const FString& PinName);

	/**
	 * Where a pin leads: the pins linked to it, and to its split parts.
	 * @param Blueprint The blueprint
	 * @param GraphName The graph
	 * @param NodeName  The node
	 * @param PinName   The pin, either direction (a split pin's parts count as its own)
	 * @return "Node.Pin" for each linked pin (a part of a split pin by its own name, "TargetRotation_Yaw"); empty when
	 *         none or no such pin
	 */
	UFUNCTION(BlueprintCallable, Category = "VoxelCharacter|AnimAuthoring")
	static TArray<FString> GetPinLinks(UBlueprint* Blueprint, FName GraphName, const FString& NodeName, const FString& PinName);
};
