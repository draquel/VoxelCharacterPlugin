// Copyright Daniel Raquel. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VCMapMarkerRegistry.generated.h"

/**
 * One marker drawn on the minimap / world map. Generic presentation data — the map system
 * attaches no semantics to who produced a marker (POIs, quests, party members, ...).
 */
USTRUCT(BlueprintType)
struct FVCMapMarker
{
	GENERATED_BODY()

	/** Marker position in world XY. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FVector2D WorldPosition = FVector2D::ZeroVector;

	/**
	 * Marker position in 3D, used when bHasWorldLocation (a planet's maps draw only such markers: XY means nothing on a
	 * sphere; spherical Phase 4, WM-SY9).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FVector WorldLocation = FVector::ZeroVector;

	/** WorldLocation is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	bool bHasWorldLocation = false;

	/** Label shown next to the marker on the world map (minimap draws dots only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FText Label;

	/** Dot/label tint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FLinearColor Color = FLinearColor::White;

	/** Higher priority draws later (on top) and wins the minimap's marker budget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	int32 Priority = 0;
};

/**
 * Marker sources append their markers for a queried world area.
 * Fired on the game thread; sources must be cheap (called at map refresh cadence).
 */
DECLARE_MULTICAST_DELEGATE_TwoParams(FVCOnGatherMapMarkers, const FBox2D& /*WorldArea*/, TArray<FVCMapMarker>& /*InOutMarkers*/);

/**
 * Marker sources append their markers within a distance of a point (a planet's maps: their view is not an XY box), with
 * WorldLocation set. Same rules as FVCOnGatherMapMarkers.
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FVCOnGatherMapMarkersNear, const FVector& /*Center*/, double /*Radius*/, TArray<FVCMapMarker>& /*InOutMarkers*/);

/**
 * Registration point between the map widgets and anything that wants markers on them.
 *
 * The map system (VCMinimapWidget / VCWorldMapWidget) stays source-agnostic and reusable: it asks
 * this registry for markers in its view area and draws whatever comes back. Game-level systems
 * (POIs, quests, party) bind OnGatherMarkers and append theirs — the same seam pattern as the
 * rest of the project (the map knows markers, never their producers).
 */
UCLASS()
class VOXELCHARACTERPLUGIN_API UVCMapMarkerRegistry : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Bind a source here (game thread). Keep the FDelegateHandle to unbind on teardown. */
	FVCOnGatherMapMarkers OnGatherMarkers;

	/** Collect markers from every bound source for WorldArea (appended in bind order). */
	void GatherMarkers(const FBox2D& WorldArea, TArray<FVCMapMarker>& OutMarkers) const
	{
		OnGatherMarkers.Broadcast(WorldArea, OutMarkers);
	}

	/** Planet sources bind here (spherical Phase 4): markers within a distance of a point, with WorldLocation set. */
	FVCOnGatherMapMarkersNear OnGatherMarkersNear;

	/**
	 * Collect markers within Radius of Center from every OnGatherMarkersNear source (appended in bind order); markers
	 * without bHasWorldLocation are dropped.
	 * @param Center World point
	 * @param Radius Distance (uu)
	 * @param OutMarkers Appended markers
	 */
	void GatherMarkersNear(const FVector& Center, double Radius, TArray<FVCMapMarker>& OutMarkers) const
	{
		const int32 First = OutMarkers.Num();
		OnGatherMarkersNear.Broadcast(Center, Radius, OutMarkers);
		for (int32 i = OutMarkers.Num() - 1; i >= First; --i)
		{
			if (!OutMarkers[i].bHasWorldLocation)
			{
				OutMarkers.RemoveAt(i);
			}
		}
	}

protected:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override
	{
		if (const UWorld* World = Cast<UWorld>(Outer))
		{
			return World->IsGameWorld();
		}
		return false;
	}
};
