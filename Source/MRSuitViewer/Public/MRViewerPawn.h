#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "OculusXRInputFunctionLibrary.h"
#include "MRViewerPawn.generated.h"

class UCameraComponent;
class UMotionControllerComponent;
class UOculusXRHandComponent;
class AMRSuitViewer;
class AMRSuit;
struct FXRHandTrackingState;

/**
 * Mixed-reality pawn for Meta Quest 3S:
 * Supports hand tracking and controller tracking, one-hand grab & move & rotate,
 * two-hand scaling and multi-axis transformation, and reset functionality.
 */
UCLASS()
class MRSUITVIEWER_API AMRViewerPawn : public APawn
{
	GENERATED_BODY()

public:
	AMRViewerPawn();

	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintPure, Category = "MR")
	UCameraComponent* GetHeadCamera() const { return Camera; }

	UFUNCTION(BlueprintPure, Category = "MR")
	UMotionControllerComponent* GetLeftAim() const { return LeftAim; }

	UFUNCTION(BlueprintPure, Category = "MR")
	UMotionControllerComponent* GetRightAim() const { return RightAim; }

	UFUNCTION(BlueprintPure, Category = "MR")
	UMotionControllerComponent* GetLeftGrip() const { return LeftGrip; }

	UFUNCTION(BlueprintPure, Category = "MR")
	UMotionControllerComponent* GetRightGrip() const { return RightGrip; }

	UFUNCTION(BlueprintPure, Category = "MR")
	UOculusXRHandComponent* GetLeftHand() const { return LeftHandComponent; }

	UFUNCTION(BlueprintPure, Category = "MR")
	UOculusXRHandComponent* GetRightHand() const { return RightHandComponent; }

	UFUNCTION(BlueprintPure, Category = "MR")
	class UMRWristMenuComponent* GetWristMenu() const { return WristMenu; }

	UFUNCTION(BlueprintPure, Category = "MR")
	AMRSuitViewer* GetViewer() const { return CachedViewer.Get(); }

	UFUNCTION(BlueprintPure, Category = "MR|Interaction")
	bool IsSuitGrabbed() const;

	UFUNCTION(BlueprintPure, Category = "MR|Interaction")
	bool IsTwoHandScaling() const;

	/** Puts every model in the room back where it was placed, at life size. */
	UFUNCTION(BlueprintCallable, Category = "MR|Interaction")
	void ResetIronMan();

	/** Puts model ModelIndex of the viewer's model catalog in the room, beside any already there (0 = Iron Man Mk 85). */
	UFUNCTION(BlueprintCallable, Category = "MR|Suit")
	void SpawnModel(int32 ModelIndex = 0);

	/** Clears any spawned model so the room is empty. */
	UFUNCTION(BlueprintCallable, Category = "MR|Suit")
	void ClearRoom();

protected:
	virtual void BeginPlay() override;

	/** Tracking-space origin. With Stage origin, Z = 0 is the physical floor calibrated by the Quest boundary. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MR")
	TObjectPtr<USceneComponent> VROrigin;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MR")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MR")
	TObjectPtr<UMotionControllerComponent> LeftAim;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MR")
	TObjectPtr<UMotionControllerComponent> RightAim;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MR")
	TObjectPtr<UMotionControllerComponent> LeftGrip;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MR")
	TObjectPtr<UMotionControllerComponent> RightGrip;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MR")
	TObjectPtr<UOculusXRHandComponent> LeftHandComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MR")
	TObjectPtr<UOculusXRHandComponent> RightHandComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MR|Menu")
	TObjectPtr<class UMRWristMenuComponent> WristMenu;

	/** Maximum distance from hand/controller to suit allowed to initiate a grab (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MR|Interaction", meta = (ClampMin = "100.0", Units = "cm"))
	float MaxGrabRange = 800.f;

	/** How far outside the suit's outline a hand can pinch and still grab it (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MR|Interaction", meta = (ClampMin = "0.0", Units = "cm"))
	float DirectGrabReachDistance = 5.f;

	/** How far the aim ray may pass beside the suit's outline and still grab it from a distance (cm). Only used with distance grab. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MR|Interaction", meta = (ClampMin = "0.0", Units = "cm"))
	float GrabAimTolerance = 20.f;

	/** Degrees per second to spin the suit with thumbstick X while holding grip (controllers). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MR|Interaction", meta = (ClampMin = "10.0", Units = "deg/s"))
	float ThumbstickTurnSpeed = 120.f;

	/** Speed to push/pull the suit with thumbstick Y while holding grip (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MR|Interaction", meta = (ClampMin = "50.0", Units = "cm/s"))
	float ThumbstickPushPullSpeed = 150.f;

private:
	UFUNCTION()
	void HandleModelSelected(int32 ModelIndex);

	UFUNCTION()
	void HandleClearRoom();

	UFUNCTION()
	void HandleResetPose();

	void UpdateSuitInteraction(float DeltaTime);
	bool IsHandGrabbing(bool bRightHand, float& OutStrength);
	bool GetTrackedHand(bool bRightHand, FXRHandTrackingState& OutHand) const;
	/** The hand (or its controller) is tracked, so GetHandLocation is where the hand really is. */
	bool IsHandTracked(bool bRightHand) const;
	/** Finds where this hand would take hold of the suit: the touched spot on its outline, or where the aim ray meets it. */
	bool FindGrabPoint(bool bRightHand, const AMRSuit* Suit, FVector& OutGrabPoint) const;
	/** The model this hand touches; with several close together, the one whose outline is nearest to the hand. */
	AMRSuit* FindTouchedSuit(bool bRightHand, const TArray<AMRSuit*>& Suits, FVector& OutGrabPoint) const;
	/** Lets go with this hand; the other hand, if it holds the same model, carries on holding it alone. */
	void ReleaseHand(bool bRightHand, const FVector& OtherHandLocation, const FQuat& OtherHandRotation);
	void UpdateOneHandHold(AMRSuit* Suit, bool bRightHand, const FVector& HandLocation, const FQuat& HandRotation, float DeltaTime) const;
	void DrawGrabFeedback(const AMRSuit* Suit, bool bRightHand) const;
	FVector GetHandLocation(bool bRightHand) const;
	FQuat GetHandRotation(bool bRightHand) const;
	TArray<AMRSuit*> GetSuitsInRoom() const;

	TWeakObjectPtr<AMRSuitViewer> CachedViewer;

	/** The model each hand holds. Both on one model scale it; one on each move two models at once. */
	TWeakObjectPtr<AMRSuit> LeftHeldSuit;
	TWeakObjectPtr<AMRSuit> RightHeldSuit;

	bool bLeftGrabbing = false;
	bool bRightGrabbing = false;
	bool bLeftPinching = false;
	bool bRightPinching = false;

	// Where each hand took hold, in the suit's own space, so the spot stays on the suit while it moves and scales.
	FVector LeftGrabLocal = FVector::ZeroVector;
	FVector RightGrabLocal = FVector::ZeroVector;
	bool bPrevLeftGrabIntent = false;
	bool bPrevRightGrabIntent = false;
	bool bPrevResetButton = false;
};
