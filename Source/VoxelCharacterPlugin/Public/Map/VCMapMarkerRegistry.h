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

	/**
	 * Marker position in world space: a heightmap world's maps draw its X and Y (its Z is theirs to ignore), a planet's its
	 * direction from the centre (XY means nothing on a sphere; spherical Phase 4, WM-SY9). One location for every kind of
	 * world (code review CR2, VCP-21).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FVector WorldLocation = FVector::ZeroVector;

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
 * Marker sources append their markers within a distance of a point: on a heightmap world the square of that half extent
 * around the point's XY (a map's view), on a planet the 3D distance (its view is not an XY box). Fired on the game thread;
 * sources must be cheap (called at map refresh cadence).
 */
DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnVCGatherMapMarkers, const FVector& /*Center*/, double /*Radius*/, TArray<FVCMapMarker>& /*InOutMarkers*/);

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
	FOnVCGatherMapMarkers OnGatherMarkers;

	/**
	 * Collect markers within Radius of Center from every bound source (appended in bind order): on a heightmap world the
	 * square of half extent Radius around Center's XY, on a planet the 3D distance.
	 * @param Center World point (a map's view centre)
	 * @param Radius Half extent or distance (uu)
	 * @param OutMarkers Appended markers
	 */
	void GatherMarkers(const FVector& Center, double Radius, TArray<FVCMapMarker>& OutMarkers) const
	{
		OnGatherMarkers.Broadcast(Center, Radius, OutMarkers);
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
