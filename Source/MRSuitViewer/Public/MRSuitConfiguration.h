#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MRSuitConfiguration.generated.h"

class UMaterialInterface;
class UStaticMesh;

/** One capsule of a model's touch outline, in the suit mesh's own space (cm). */
USTRUCT(BlueprintType)
struct FMRGrabCapsule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab")
	FVector Start = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab")
	FVector End = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Grab", meta = (Units = "cm", ClampMin = "0.0"))
	float Radius = 10.f;
};

/**
 * Developer calibration for the complete suit (DA_SuitConfiguration).
 *
 * The suit mesh is authored at real-world size in centimetres, with its pivot on the floor between the feet,
 * so the defaults already give a true 1:1 suit. Only change these values to calibrate a specific model.
 */
UCLASS(BlueprintType)
class MRSUITVIEWER_API UMRSuitConfiguration : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** The complete suit as a single static mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suit")
	TObjectPtr<UStaticMesh> SuitMesh;

	/** Target real-world height of the suit in cm (default 190.0 cm for Iron Man Mark 85). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calibration", meta = (Units = "cm"))
	float TargetHeight = 190.f;

	/** Uniform scale on top of the mesh's authored size. Keep at 1.0 for true life size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calibration", meta = (ClampMin = "0.1", ClampMax = "3.0", UIMin = "0.5", UIMax = "1.5"))
	float SuitScale = 1.f;

	/** Initial scale when spawned or reset (1.0 = life size). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Limits", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float InitialScale = 1.f;

	/** Minimum scale limit for two-hand scaling (e.g. 0.2 = ~38 cm tabletop display). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Limits", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float MinimumScale = 0.2f;

	/** Maximum scale limit for two-hand scaling (e.g. 3.0 = ~5.7 meters giant). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction|Limits", meta = (ClampMin = "1.0", ClampMax = "10.0"))
	float MaximumScale = 3.0f;

	/**
	 * Touch outline that follows the model's shape, in the mesh's own space. A pinch only takes hold on (or near) these
	 * capsules. Empty: the box around the whole mesh is used, which is enough for a compact model like Iron Man but
	 * would grab from the empty space around parts that stick far out, like the Iron Spider's legs.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interaction")
	TArray<FMRGrabCapsule> GrabCapsules;

	/** Offset of the mesh from its placement point, in the suit's own frame (X forward, Y right, Z up). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calibration", meta = (Units = "cm"))
	FVector PositionOffset = FVector::ZeroVector;

	/** Rotation of the mesh so that the suit faces its actor's forward axis (+X). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calibration")
	FRotator RotationOffset = FRotator::ZeroRotator;

	/** Raises (+) or lowers (-) the suit relative to the detected floor, e.g. to fix floating or sinking feet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Calibration", meta = (Units = "cm", UIMin = "-10.0", UIMax = "10.0"))
	float FloorOffset = 0.f;

	/** Soft baked ground occlusion under the feet (the model's own ground AO), so the suit reads as standing on the floor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact Shadow")
	bool bShowContactShadow = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact Shadow", meta = (EditCondition = "bShowContactShadow"))
	TObjectPtr<UMaterialInterface> ContactShadowMaterial;

	/** World width covered by the contact shadow texture at SuitScale 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact Shadow", meta = (EditCondition = "bShowContactShadow", Units = "cm", ClampMin = "10.0"))
	float ContactShadowSize = 300.f;

	/** Rotation of the contact shadow around the vertical axis, to line its footprints up with the feet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contact Shadow", meta = (EditCondition = "bShowContactShadow"))
	float ContactShadowYaw = 0.f;

	/** Height of the suit in cm with the current calibration. */
	UFUNCTION(BlueprintPure, Category = "Suit")
	float GetSuitHeight() const;
};
