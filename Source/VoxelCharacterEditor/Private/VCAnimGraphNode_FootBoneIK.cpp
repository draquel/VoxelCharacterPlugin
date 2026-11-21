// Copyright Daniel Raquel. All Rights Reserved.

#include "VCAnimGraphNode_FootBoneIK.h"

#define LOCTEXT_NAMESPACE "VCAnimGraphNode_FootBoneIK"

FText UVCAnimGraphNode_FootBoneIK::GetControllerDescription() const
{
	return LOCTEXT("ControllerDescription", "Foot Bone IK");
}

FText UVCAnimGraphNode_FootBoneIK::GetTooltipText() const
{
	return LOCTEXT("Tooltip", "Places the feet on the ground through the foot bones themselves, for a body without ik_foot bones "
		"(a MetaHuman's): traces under each foot along the mesh's down, drops the pelvis to the lower foot and bends each leg to its ground.");
}

FText UVCAnimGraphNode_FootBoneIK::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return GetControllerDescription();
}

#undef LOCTEXT_NAMESPACE
