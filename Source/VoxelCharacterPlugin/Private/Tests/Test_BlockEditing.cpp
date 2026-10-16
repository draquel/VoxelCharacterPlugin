// Copyright Daniel Raquel. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_TESTS

#include "Core/VCCharacterBase.h"
#include "VoxelWorldConfiguration.h"
#include "VoxelCoordinates.h"

#define VC_BLOCKEDIT_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

// ---------------------------------------------------------------------------
// Block-accurate targeting (world modes review WM-C2, KC5). A trace hits a cubic block exactly on one of
// its faces, so floor(ImpactPoint) picks the air voxel in front of +X/+Y/+Z faces and lets floating-point
// noise decide on the others. A dig must take the block behind the face and a place the voxel in front of
// it, from any face, with the impact point a hair either side of the face plane.
// ---------------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVCBlockEdit_Targeting, "VoxelCharacter.Edit.BlockTargeting", VC_BLOCKEDIT_FLAGS)
bool FVCBlockEdit_Targeting::RunTest(const FString& Parameters)
{
	UVoxelWorldConfiguration* Cubic = NewObject<UVoxelWorldConfiguration>();
	Cubic->MeshingMode = EMeshingMode::Cubic;
	Cubic->VoxelSize = 75.0f;
	Cubic->WorldOrigin = FVector(12345.0, 54321.0, 0.0); // the demo's origin: not a multiple of the voxel size

	const FIntVector Block(-86, 17, -3);
	const double VS = Cubic->VoxelSize;
	const FVector BlockMin = Cubic->WorldOrigin + FVector(Block) * VS;
	const FVector BlockCentre = BlockMin + FVector(0.5 * VS);

	static const FIntVector Normals[6] = {
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0),
		FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };
	// Off-centre on the face, so the point is not on a block edge in the other two axes either.
	const FVector InFace(0.3 * VS, -0.2 * VS, 0.15 * VS);
	for (const FIntVector& N : Normals)
	{
		const FVector Normal(N);
		for (const double Noise : { -0.001, 0.0, 0.001 })
		{
			FVector Impact = BlockCentre + Normal * (0.5 * VS + Noise);
			// Move within the face plane only.
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				if (N[Axis] == 0)
				{
					Impact[Axis] += InFace[Axis];
				}
			}
			const FString Where = FString::Printf(TEXT("face %s, noise %+.3f"), *N.ToString(), Noise);
			TestEqual(*FString::Printf(TEXT("Cubic dig takes the hit block (%s)"), *Where),
				AVCCharacterBase::GetEditVoxelCoord(Impact, Normal, Cubic, /*bPlace*/ false), Block);
			TestEqual(*FString::Printf(TEXT("Cubic place fills the voxel in front of the face (%s)"), *Where),
				AVCCharacterBase::GetEditVoxelCoord(Impact, Normal, Cubic, /*bPlace*/ true), Block + N);
		}
	}

	// Smooth worlds keep the old dig target (the voxel containing the impact point) and the same place target.
	UVoxelWorldConfiguration* Smooth = NewObject<UVoxelWorldConfiguration>();
	Smooth->MeshingMode = EMeshingMode::DualContouring;
	Smooth->VoxelSize = 75.0f;
	Smooth->WorldOrigin = Cubic->WorldOrigin;
	const FVector Impact = BlockCentre + FVector(0.0, 0.0, 0.5 * VS) + FVector(10.0, -5.0, 0.0);
	TestEqual(TEXT("Smooth dig is the voxel containing the impact point"),
		AVCCharacterBase::GetEditVoxelCoord(Impact, FVector::UpVector, Smooth, /*bPlace*/ false),
		FVoxelCoordinates::WorldToVoxel(Impact - Smooth->WorldOrigin, VS));
	TestEqual(TEXT("Smooth place is half a voxel along the normal"),
		AVCCharacterBase::GetEditVoxelCoord(Impact, FVector::UpVector, Smooth, /*bPlace*/ true),
		FVoxelCoordinates::WorldToVoxel(Impact + FVector(0.0, 0.0, 0.5 * VS) - Smooth->WorldOrigin, VS));

	TestEqual(TEXT("No configuration gives zero"), AVCCharacterBase::GetEditVoxelCoord(Impact, FVector::UpVector, nullptr, false), FIntVector::ZeroValue);
	return true;
}

#endif // WITH_AUTOMATION_TESTS
