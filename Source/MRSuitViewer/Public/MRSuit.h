#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MRSuit.generated.h"

class UBoxComponent;
class ULocalLightComponent;
class UMRSuitConfiguration;
class UStaticMeshComponent;

/**
 * Iron Man Mark 85 life-size Mixed Reality grabbable actor.
 * Supports 1-hand direct and distance grab, 3D translation & rotation,
 * 2-hand scaling and multi-axis transformation, and reset.
 */
UCLASS()
class MRSUITVIEWER_API AMRSuit : public AActor
{
	GENERATED_BODY()

public:
	AMRSuit();

	/** Which suit to show and how it is calibrated (DA_SuitConfiguration). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Suit")
	TObjectPtr<UMRSuitConfiguration> Configuration;

	/** Re-applies the configuration to the mesh and contact shadow. */
	UFUNCTION(BlueprintCallable, Category = "Suit")
	void ApplyConfiguration();

	UFUNCTION(BlueprintPure, Category = "Suit")
	float GetSuitHeight() const;

	/** Resets the Iron Man suit back to its original placement location, rotation, and life-size scale. */
	UFUNCTION(BlueprintCallable, Category = "IronMan|Interaction")
	void ResetIronMan();

	/** Sets the uniform scale of the suit, clamped between MinimumScale and MaximumScale. */
	UFUNCTION(BlueprintCallable, Category = "IronMan|Scale")
	void SetSuitScale(float NewScale);

	UFUNCTION(BlueprintPure, Category = "IronMan|Scale")
	float GetCurrentScale() const { return CurrentScale; }

	UFUNCTION(BlueprintPure, Category = "IronMan|Scale")
	float GetMinimumScale() const;

	UFUNCTION(BlueprintPure, Category = "IronMan|Scale")
	float GetMaximumScale() const;

	UFUNCTION(BlueprintPure, Category = "IronMan|Scale")
	float GetTargetHeight() const;

	UFUNCTION(BlueprintPure, Category = "IronMan|Interaction")
	bool IsGrabbed() const { return bIsGrabbed; }

	UFUNCTION(BlueprintPure, Category = "IronMan|Interaction")
	bool IsTwoHandGrabbed() const { return bIsTwoHandGrabbed; }

	UFUNCTION(BlueprintPure, Category = "IronMan|Interaction")
	bool IsGrabbedWithRightHand() const { return bGrabbedWithRightHand; }

	UFUNCTION(BlueprintPure, Category = "IronMan|Interaction")
	UBoxComponent* GetInteractionBox() const { return InteractionBox; }

	/** Records the initial spawn/floor transform for ResetIronMan(). */
	void SetInitialPlacement(const FTransform& InTransform);

	void StartOneHandGrab(bool bRightHand, const FVector& HandLocation, const FQuat& HandRotation);
	void UpdateOneHandGrab(bool bRightHand, const FVector& HandLocation, const FQuat& HandRotation, float DeltaTime);
	void StartTwoHandGrab(const FVector& LeftHandLocation, const FVector& RightHandLocation);
	void UpdateTwoHandGrab(const FVector& LeftHandLocation, const FVector& RightHandLocation, float DeltaTime);
	void EndGrab(bool bRightHand);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Suit")
	TObjectPtr<UStaticMeshComponent> SuitMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Suit")
	TObjectPtr<UStaticMeshComponent> ContactShadow;

	/** Collision volume for hand proximity, grabbing, and raycast hit detection. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Suit|Interaction")
	TObjectPtr<UBoxComponent> InteractionBox;

private:
	/** Lights attached to the suit move closer when it shrinks; keep the light on its surface the same at any scale. */
	void CacheLightDefaults();
	void ScaleLights();

	/** The contact shadow only makes sense while the suit stands upright on the floor, not while it is held in the air. */
	void UpdateContactShadowVisibility();

	struct FSuitLightDefaults
	{
		TWeakObjectPtr<ULocalLightComponent> Light;
		float Intensity = 0.f;
		float AttenuationRadius = 0.f;
		float SourceRadius = 0.f;
	};
	TArray<FSuitLightDefaults> LightDefaults;

	FTransform InitialTransform;
	float CurrentScale = 1.f;
	bool bIsGrabbed = false;
	bool bIsTwoHandGrabbed = false;
	bool bGrabbedWithRightHand = true;

	// Single hand grab offsets
	FVector OneHandLocalOffset = FVector::ZeroVector;
	FQuat OneHandRotationOffset = FQuat::Identity;

	// Two hand transformer state
	float TwoHandInitialDistance = 100.f;
	float TwoHandInitialScale = 1.f;
	FVector TwoHandInitialMidpoint = FVector::ZeroVector;
	FVector TwoHandInitialOffset = FVector::ZeroVector;
	FVector TwoHandInitialHandVector = FVector::ForwardVector;
	FQuat TwoHandInitialSuitRotation = FQuat::Identity;
};
