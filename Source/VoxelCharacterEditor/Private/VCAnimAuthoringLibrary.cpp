// Copyright Daniel Raquel. All Rights Reserved.

#include "VCAnimAuthoringLibrary.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/MemberReference.h"
#include "Kismet2/BlueprintEditorUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogVoxelCharacterEditor, Log, All);

namespace VCAnimAuthoring
{
	UEdGraph* FindGraph(UBlueprint* Blueprint, FName GraphName)
	{
		if (!Blueprint)
		{
			return nullptr;
		}
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetFName() == GraphName)
			{
				return Graph;
			}
		}
		return nullptr;
	}

	/** A node by its object name, else by its title. */
	UEdGraphNode* FindNode(UEdGraph* Graph, const FString& NodeName)
	{
		if (!Graph)
		{
			return nullptr;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->GetName() == NodeName)
			{
				return Node;
			}
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Equals(NodeName, ESearchCase::IgnoreCase))
			{
				return Node;
			}
		}
		return nullptr;
	}

	/** A pin by its name, else by its display name. */
	UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction)
	{
		if (!Node)
		{
			return nullptr;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction && Pin->GetName() == PinName)
			{
				return Pin;
			}
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == Direction && Pin->GetDisplayName().ToString().Equals(PinName, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	/** Walks a dotted path of struct members from an object to the last property and its address. */
	bool ResolvePropertyPath(UObject* Object, const FString& Path, FProperty*& OutProperty, void*& OutAddress)
	{
		TArray<FString> Parts;
		Path.ParseIntoArray(Parts, TEXT("."));
		const UStruct* Struct = Object->GetClass();
		void* Container = Object;
		for (int32 Index = 0; Index < Parts.Num(); ++Index)
		{
			FProperty* Property = FindFProperty<FProperty>(Struct, *Parts[Index]);
			if (!Property)
			{
				return false;
			}
			void* Address = Property->ContainerPtrToValuePtr<void>(Container);
			if (Index == Parts.Num() - 1)
			{
				OutProperty = Property;
				OutAddress = Address;
				return true;
			}
			const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
			if (!StructProperty)
			{
				return false;
			}
			Struct = StructProperty->Struct;
			Container = Address;
		}
		return false;
	}

	UEdGraphNode* AddNode(UEdGraph* Graph, UClass* NodeClass, int32 X, int32 Y)
	{
		Graph->Modify();
		UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NodeClass, NAME_None, RF_Transactional);
		Graph->AddNode(Node, /*bFromUI=*/ false, /*bSelectNewNode=*/ false);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
		Node->NodePosX = X;
		Node->NodePosY = Y;
		return Node;
	}
}

bool UVCAnimAuthoringLibrary::AddAnimLayer(UAnimBlueprint* Blueprint, FName LayerName, FName InputPoseName)
{
	using namespace VCAnimAuthoring;
	if (!Blueprint || LayerName.IsNone())
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("AddAnimLayer: no blueprint or no layer name"));
		return false;
	}
	if (FindGraph(Blueprint, LayerName))
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("AddAnimLayer: %s already has a graph %s"), *Blueprint->GetName(), *LayerName.ToString());
		return false;
	}
	// What the editor's "Add Animation Layer" does (FBlueprintEditor::NewDocument_OnClick, CGT_NewAnimationLayer).
	UEdGraph* Layer = FBlueprintEditorUtils::CreateNewGraph(Blueprint, LayerName, UAnimationGraph::StaticClass(), UAnimationGraphSchema::StaticClass());
	FBlueprintEditorUtils::AddDomainSpecificGraph(Blueprint, Layer);
	if (!InputPoseName.IsNone())
	{
		UClass* InputClass = FindObject<UClass>(nullptr, TEXT("/Script/AnimGraph.AnimGraphNode_LinkedInputPose"));
		if (!InputClass)
		{
			UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("AddAnimLayer: no linked input pose node class"));
			return false;
		}
		UEdGraphNode* Input = AddNode(Layer, InputClass, -400, 0);
		FProperty* Property = nullptr;
		void* Address = nullptr;
		if (!ResolvePropertyPath(Input, TEXT("Node.Name"), Property, Address) || !Property->ImportText_Direct(*InputPoseName.ToString(), Address, Input, PPF_None))
		{
			UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("AddAnimLayer: could not name the input pose %s"), *InputPoseName.ToString());
			return false;
		}
		Input->ReconstructNode();
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UVCAnimAuthoringLibrary::ImplementInterface(UBlueprint* Blueprint, UClass* InterfaceClass)
{
	if (!Blueprint || !InterfaceClass)
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("ImplementInterface: no blueprint or no interface"));
		return false;
	}
	for (const FBPInterfaceDescription& Implemented : Blueprint->ImplementedInterfaces)
	{
		if (Implemented.Interface == InterfaceClass)
		{
			return true;
		}
	}
	const bool bDone = FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfaceClass->GetClassPathName());
	if (!bDone)
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("ImplementInterface: %s refused %s"), *Blueprint->GetName(), *InterfaceClass->GetName());
	}
	return bDone;
}

FString UVCAnimAuthoringLibrary::AddGraphNode(UBlueprint* Blueprint, FName GraphName, const FString& NodeClassPath, int32 X, int32 Y)
{
	using namespace VCAnimAuthoring;
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	UClass* NodeClass = FindObject<UClass>(nullptr, *NodeClassPath);
	if (!NodeClass)
	{
		NodeClass = LoadObject<UClass>(nullptr, *NodeClassPath);
	}
	if (!Graph || !NodeClass || !NodeClass->IsChildOf(UEdGraphNode::StaticClass()) || NodeClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("AddGraphNode: no graph %s in %s, or %s is no node class"), *GraphName.ToString(),
			*GetNameSafe(Blueprint), *NodeClassPath);
		return FString();
	}
	UEdGraphNode* Node = AddNode(Graph, NodeClass, X, Y);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return Node->GetName();
}

bool UVCAnimAuthoringLibrary::SetNodeProperty(UBlueprint* Blueprint, FName GraphName, const FString& NodeName, const FString& PropertyPath,
	const FString& ValueText)
{
	using namespace VCAnimAuthoring;
	UEdGraphNode* Node = FindNode(FindGraph(Blueprint, GraphName), NodeName);
	FProperty* Property = nullptr;
	void* Address = nullptr;
	if (!Node || !ResolvePropertyPath(Node, PropertyPath, Property, Address))
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("SetNodeProperty: no node %s in %s.%s, or no property %s on it"), *NodeName,
			*GetNameSafe(Blueprint), *GraphName.ToString(), *PropertyPath);
		return false;
	}
	Node->Modify();
	if (!Property->ImportText_Direct(*ValueText, Address, Node, PPF_None))
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("SetNodeProperty: %s.%s refused \"%s\""), *NodeName, *PropertyPath, *ValueText);
		return false;
	}
	Node->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UVCAnimAuthoringLibrary::SetLinkedAnimLayer(UBlueprint* Blueprint, FName GraphName, const FString& NodeName, UClass* InterfaceClass,
	FName LayerName, UClass* InstanceClass)
{
	using namespace VCAnimAuthoring;
	UEdGraphNode* Node = FindNode(FindGraph(Blueprint, GraphName), NodeName);
	UBlueprint* InterfaceBlueprint = InterfaceClass ? Cast<UBlueprint>(InterfaceClass->ClassGeneratedBy) : nullptr;
	const UEdGraph* LayerGraph = FindGraph(InterfaceBlueprint, LayerName);
	FProperty* Interface = nullptr;
	FProperty* Layer = nullptr;
	FProperty* Instance = nullptr;
	FProperty* Guid = nullptr;
	FProperty* Reference = nullptr;
	void* InterfaceAddress = nullptr;
	void* LayerAddress = nullptr;
	void* InstanceAddress = nullptr;
	void* GuidAddress = nullptr;
	void* ReferenceAddress = nullptr;
	if (!Node || !LayerGraph
		|| !ResolvePropertyPath(Node, TEXT("Node.Interface"), Interface, InterfaceAddress)
		|| !ResolvePropertyPath(Node, TEXT("Node.Layer"), Layer, LayerAddress)
		|| !ResolvePropertyPath(Node, TEXT("Node.InstanceClass"), Instance, InstanceAddress)
		|| !ResolvePropertyPath(Node, TEXT("InterfaceGuid"), Guid, GuidAddress)
		|| !ResolvePropertyPath(Node, TEXT("FunctionReference"), Reference, ReferenceAddress)
		|| !CastField<FClassProperty>(Interface) || !CastField<FNameProperty>(Layer) || !CastField<FClassProperty>(Instance))
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("SetLinkedAnimLayer: %s in %s.%s is no linked anim layer node, or %s has no layer %s"),
			*NodeName, *GetNameSafe(Blueprint), *GraphName.ToString(), *GetNameSafe(InterfaceClass), *LayerName.ToString());
		return false;
	}
	Node->Modify();
	CastField<FClassProperty>(Interface)->SetObjectPropertyValue(InterfaceAddress, InterfaceClass);
	CastField<FNameProperty>(Layer)->SetPropertyValue(LayerAddress, LayerName);
	CastField<FClassProperty>(Instance)->SetObjectPropertyValue(InstanceAddress, InstanceClass);
	// As UAnimGraphNode_LinkedAnimLayer::SetLayerName: the member reference names the layer function on the interface.
	*static_cast<FGuid*>(GuidAddress) = LayerGraph->GraphGuid;
	static_cast<FMemberReference*>(ReferenceAddress)->SetExternalMember(LayerName, InterfaceClass, LayerGraph->GraphGuid);
	Node->ReconstructNode();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UVCAnimAuthoringLibrary::SetPinDefault(UBlueprint* Blueprint, FName GraphName, const FString& NodeName, const FString& PinName, const FString& Value)
{
	using namespace VCAnimAuthoring;
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	UEdGraphPin* Pin = FindPin(FindNode(Graph, NodeName), PinName, EGPD_Input);
	const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
	if (!Pin || !Schema)
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("SetPinDefault: no input pin %s on %s in %s.%s"), *PinName, *NodeName, *GetNameSafe(Blueprint), *GraphName.ToString());
		return false;
	}
	const FName Category = Pin->PinType.PinCategory;
	if (Category == UEdGraphSchema_K2::PC_Object || Category == UEdGraphSchema_K2::PC_Class
		|| Category == UEdGraphSchema_K2::PC_SoftObject || Category == UEdGraphSchema_K2::PC_SoftClass)
	{
		UObject* Object = Value.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *Value);
		if (!Value.IsEmpty() && !Object)
		{
			UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("SetPinDefault: %s does not load"), *Value);
			return false;
		}
		Schema->TrySetDefaultObject(*Pin, Object);
	}
	else
	{
		Schema->TrySetDefaultValue(*Pin, Value);
	}
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	return true;
}

bool UVCAnimAuthoringLibrary::ConnectPins(UBlueprint* Blueprint, FName GraphName, const FString& FromNode, const FString& FromPin,
	const FString& ToNode, const FString& ToPin)
{
	using namespace VCAnimAuthoring;
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	UEdGraphPin* Output = FindPin(FindNode(Graph, FromNode), FromPin, EGPD_Output);
	UEdGraphPin* Input = FindPin(FindNode(Graph, ToNode), ToPin, EGPD_Input);
	const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;
	if (!Output || !Input || !Schema)
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("ConnectPins: no %s.%s or %s.%s in %s.%s"), *FromNode, *FromPin, *ToNode, *ToPin,
			*GetNameSafe(Blueprint), *GraphName.ToString());
		return false;
	}
	if (!Schema->TryCreateConnection(Output, Input))
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("ConnectPins: the schema refused %s.%s -> %s.%s"), *FromNode, *FromPin, *ToNode, *ToPin);
		return false;
	}
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}

bool UVCAnimAuthoringLibrary::SetObjectProperty(UObject* Object, const FString& PropertyPath, const FString& ValueText)
{
	FProperty* Property = nullptr;
	void* Address = nullptr;
	if (!Object || !VCAnimAuthoring::ResolvePropertyPath(Object, PropertyPath, Property, Address))
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("SetObjectProperty: no property %s on %s"), *PropertyPath, *GetNameSafe(Object));
		return false;
	}
	Object->Modify();
	if (!Property->ImportText_Direct(*ValueText, Address, Object, PPF_None))
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("SetObjectProperty: %s.%s refused \"%s\""), *GetNameSafe(Object), *PropertyPath, *ValueText);
		return false;
	}
	Object->PostEditChange();
	Object->MarkPackageDirty();
	return true;
}

FString UVCAnimAuthoringLibrary::DescribeGraph(UBlueprint* Blueprint, FName GraphName)
{
	using namespace VCAnimAuthoring;
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		return FString();
	}
	TArray<FString> Lines;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}
		FString Line = FString::Printf(TEXT("%s (%s) '%s':"), *Node->GetName(), *Node->GetClass()->GetName(),
			*Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && !Pin->bHidden)
			{
				Line += FString::Printf(TEXT(" %s<%s>%s"), *Pin->GetName(), Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"),
					Pin->LinkedTo.Num() > 0 ? TEXT("*") : TEXT(""));
			}
		}
		Lines.Add(Line);
	}
	return FString::Join(Lines, TEXT("\n"));
}

FString UVCAnimAuthoringLibrary::GetGraphGuid(UBlueprint* Blueprint, FName GraphName)
{
	const UEdGraph* Graph = VCAnimAuthoring::FindGraph(Blueprint, GraphName);
	return Graph ? Graph->GraphGuid.ToString() : FString();
}
