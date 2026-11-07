// Copyright Daniel Raquel. All Rights Reserved.

#include "VCAnimAuthoringLibrary.h"
#include "AnimationGraph.h"
#include "AnimationGraphSchema.h"
#include "AnimGraphNode_Base.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
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

bool UVCAnimAuthoringLibrary::AppendMontageSegment(UAnimMontage* Montage, UAnimSequenceBase* Sequence, FName SectionName, bool bLoopSection)
{
	if (!Montage || !Sequence || SectionName.IsNone() || Montage->SlotAnimTracks.Num() == 0 || Montage->CompositeSections.Num() == 0)
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("AppendMontageSegment: no montage, no sequence, no section name, or %s has no track"),
			*GetNameSafe(Montage));
		return false;
	}
	if (Montage->GetSectionIndex(SectionName) != INDEX_NONE)
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("AppendMontageSegment: %s already has a section %s"), *Montage->GetName(), *SectionName.ToString());
		return false;
	}
	FAnimTrack& Track = Montage->SlotAnimTracks[0].AnimTrack;
	const float Start = Track.GetLength();

	// The section playing at the montage's end so far: the one the new section follows.
	FName Previous = Montage->CompositeSections[0].SectionName;
	float PreviousTime = -1.f;
	for (const FCompositeSection& Section : Montage->CompositeSections)
	{
		if (Section.GetTime() < Start && Section.GetTime() > PreviousTime)
		{
			Previous = Section.SectionName;
			PreviousTime = Section.GetTime();
		}
	}

	Montage->Modify();
	FAnimSegment Segment;
	Segment.SetAnimReference(Sequence, /*bInitialize*/ true);
	Segment.StartPos = Start;
	Track.AnimSegments.Add(Segment);
	Montage->SetCompositeLength(Track.GetLength());
	if (Montage->AddAnimCompositeSection(SectionName, Start) == INDEX_NONE)
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("AppendMontageSegment: %s refused the section %s"), *Montage->GetName(), *SectionName.ToString());
		return false;
	}
	// By name: adding a section may reorder them.
	Montage->CompositeSections[Montage->GetSectionIndex(Previous)].NextSectionName = SectionName;
	if (bLoopSection)
	{
		Montage->CompositeSections[Montage->GetSectionIndex(SectionName)].NextSectionName = SectionName;
	}
	Montage->UpdateLinkableElements();
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return true;
}

TArray<FString> UVCAnimAuthoringLibrary::ListGraphs(UBlueprint* Blueprint)
{
	TArray<FString> Names;
	if (!Blueprint)
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("ListGraphs: no blueprint"));
		return Names;
	}
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (const UEdGraph* Graph : Graphs)
	{
		if (!Graph)
		{
			continue;
		}
		// A state's or a transition's graph sits inside its state machine's graph.
		const UEdGraph* Outer = Graph->GetOuter() ? Graph->GetOuter()->GetTypedOuter<UEdGraph>() : nullptr;
		Names.Add(Outer ? Outer->GetName() + TEXT("/") + Graph->GetName() : Graph->GetName());
	}
	return Names;
}

FString UVCAnimAuthoringLibrary::ExportGraphNodes(UBlueprint* Blueprint, FName GraphName, const TArray<FString>& NodeNames)
{
	using namespace VCAnimAuthoring;
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph)
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("ExportGraphNodes: no graph %s in %s"), *GraphName.ToString(), *GetNameSafe(Blueprint));
		return FString();
	}
	TSet<UObject*> Nodes;
	if (NodeNames.Num() == 0)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->CanDuplicateNode())
			{
				Nodes.Add(Node);
			}
		}
	}
	for (const FString& Name : NodeNames)
	{
		if (UEdGraphNode* Node = FindNode(Graph, Name))
		{
			Nodes.Add(Node);
		}
		else
		{
			UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("ExportGraphNodes: no node %s in %s.%s"), *Name, *GetNameSafe(Blueprint), *GraphName.ToString());
		}
	}
	if (Nodes.Num() == 0)
	{
		return FString();
	}
	// What the editor's copy does before it exports.
	for (UObject* Object : Nodes)
	{
		CastChecked<UEdGraphNode>(Object)->PrepareForCopying();
	}
	FString Text;
	FEdGraphUtilities::ExportNodesToText(Nodes, Text);
	return Text;
}

TArray<FString> UVCAnimAuthoringLibrary::ImportGraphNodes(UBlueprint* Blueprint, FName GraphName, const FString& Text, int32 OffsetX, int32 OffsetY)
{
	using namespace VCAnimAuthoring;
	TArray<FString> Pasted;
	UEdGraph* Graph = FindGraph(Blueprint, GraphName);
	if (!Graph || !FEdGraphUtilities::CanImportNodesFromText(Graph, Text))
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("ImportGraphNodes: no graph %s in %s, or it refuses the text"), *GraphName.ToString(),
			*GetNameSafe(Blueprint));
		return Pasted;
	}
	Graph->Modify();
	TSet<UEdGraphNode*> Nodes;
	FEdGraphUtilities::ImportNodesFromText(Graph, Text, Nodes);
	// What the blueprint editor's paste does with each node (FBlueprintEditor::PasteNodesHere), less the selection.
	for (UEdGraphNode* Node : Nodes)
	{
		Node->NodePosX += OffsetX;
		Node->NodePosY += OffsetY;
		Node->CreateNewGuid();
		Pasted.Add(FString::Printf(TEXT("%s (%s) '%s'"), *Node->GetName(), *Node->GetClass()->GetName(),
			*Node->GetNodeTitle(ENodeTitleType::ListView).ToString()));
	}
	Graph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return Pasted;
}

bool UVCAnimAuthoringLibrary::SetNodePinVisible(UBlueprint* Blueprint, FName GraphName, const FString& NodeName, FName PropertyName, bool bVisible)
{
	using namespace VCAnimAuthoring;
	UAnimGraphNode_Base* Node = Cast<UAnimGraphNode_Base>(FindNode(FindGraph(Blueprint, GraphName), NodeName));
	if (!Node)
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("SetNodePinVisible: no anim node %s in %s.%s"), *NodeName, *GetNameSafe(Blueprint), *GraphName.ToString());
		return false;
	}
	for (FOptionalPinFromProperty& Option : Node->ShowPinForProperties)
	{
		if (Option.PropertyName == PropertyName)
		{
			Node->Modify();
			Option.bShowPin = bVisible;
			Node->ReconstructNode();
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
			return true;
		}
	}
	UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("SetNodePinVisible: %s has no optional pin for %s"), *NodeName, *PropertyName.ToString());
	return false;
}

bool UVCAnimAuthoringLibrary::SetMontageSlot(UAnimMontage* Montage, FName SlotName)
{
	if (!Montage || SlotName.IsNone() || Montage->SlotAnimTracks.Num() == 0)
	{
		UE_LOG(LogVoxelCharacterEditor, Warning, TEXT("SetMontageSlot: no montage, no slot name, or %s has no track"), *GetNameSafe(Montage));
		return false;
	}
	// The slot is not registered with the skeleton (a content pack's asset, left unchanged): the layer's slot node
	// registers it in memory when it is built or loaded, and the montage plays in the skeleton's default slot group.
	Montage->Modify();
	Montage->SlotAnimTracks[0].SlotName = SlotName;
	Montage->PostEditChange();
	Montage->MarkPackageDirty();
	return true;
}
