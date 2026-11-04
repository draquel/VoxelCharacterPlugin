// Copyright Daniel Raquel. All Rights Reserved.

#include "AI/VCNPCCost.h"

FVCNPCCost& FVCNPCCost::Get()
{
	static FVCNPCCost Cost;
	return Cost;
}
