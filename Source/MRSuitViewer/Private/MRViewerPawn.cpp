#include "MRViewerPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "MotionControllerComponent.h"
#include "MRSuit.h"
#include "MRSuitViewer.h"
#include "MRWristMenuComponent.h"
#include "OculusXRHandComponent.h"
#include "OculusXRInputFunctionLibrary.h"

namespace MRViewerPawn
{
	// Thumb-tip to index-tip distance (cm) that starts a pinch, and the wider distance that ends it.
	constexpr float PinchStartDistance = 2.5f;
	constexpr float PinchReleaseDistance = 4.0f;
	// Runtime pinch strength (0..1) that starts a pinch, and the lower strength that ends it.
	constexpr float PinchStartStrength = 0.9f;
	constexpr float PinchReleaseStrength = 0.5f;
	// Grab by pointing at the suit from a distance. Off: the suit only moves when a hand pinches on it.
	constexpr bool bAllowDistanceGrab = false;
}

AMRViewerPawn::AMRViewerPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
	RootComponent = VROrigin;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(VROrigin);

	LeftAim = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("LeftAim"));
	LeftAim->SetupAttachment(VROrigin);
	LeftAim->SetTrackingMotionSource(FName("LeftAim"));

	RightAim = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightAim"));
	RightAim->SetupAttachment(VROrigin);
	RightAim->SetTrackingMotionSource(FName("RightAim"));

	LeftGrip = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("LeftGrip"));
	LeftGrip->SetupAttachment(VROrigin);
	LeftGrip->SetTrackingMotionSource(FName("LeftGrip"));

	RightGrip = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightGrip"));
	RightGrip->SetupAttachment(VROrigin);
	RightGrip->SetTrackingMotionSource(FName("RightGrip"));

	LeftHandComponent = CreateDefaultSubobject<UOculusXRHandComponent>(TEXT("LeftHandMesh"));
	LeftHandComponent->SetupAttachment(VROrigin);
	LeftHandComponent->SkeletonType = EOculusXRHandType::HandLeft;
	LeftHandComponent->MeshType = EOculusXRHandType::HandLeft;
	LeftHandComponent->ConfidenceBehavior = EOculusXRConfidenceBehavior::None;

	RightHandComponent = CreateDefaultSubobject<UOculusXRHandComponent>(TEXT("RightHandMesh"));
	RightHandComponent->SetupAttachment(VROrigin);
	RightHandComponent->SkeletonType = EOculusXRHandType::HandRight;
	RightHandComponent->MeshType = EOculusXRHandType::HandRight;
	RightHandComponent->ConfidenceBehavior = EOculusXRConfidenceBehavior::None;

	// The user's real hands are visible through passthrough, so no virtual hand meshes are drawn over them.
	// Hand tracking stays on: grabbing reads the hand joints through GetTrackedHand().
	for (UOculusXRHandComponent* HandMesh : { LeftHandComponent.Get(), RightHandComponent.Get() })
	{
		HandMesh->SetVisibility(false);
		HandMesh->SetHiddenInGame(true);
		HandMesh->SetCastShadow(false);
		HandMesh->PrimaryComponentTick.bStartWithTickEnabled = false;
	}

	// Model menu beside the left hand; it places itself in the world every frame.
	WristMenu = CreateDefaultSubobject<UMRWristMenuComponent>(TEXT("WristMenu"));
	WristMenu->SetupAttachment(VROrigin);
}

void AMRViewerPawn::BeginPlay()
{
	Super::BeginPlay();

	for (TActorIterator<AMRSuitViewer> It(GetWorld()); It; ++It)
	{
		CachedViewer = *It;
		break;
	}

	if (WristMenu)
	{
		WristMenu->OnModelSelected.AddDynamic(this, &AMRViewerPawn::HandleModelSelected);
		WristMenu->OnClearRoom.AddDynamic(this, &AMRViewerPawn::HandleClearRoom);
		WristMenu->OnResetPose.AddDynamic(this, &AMRViewerPawn::HandleResetPose);
	}
}

void AMRViewerPawn::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateSuitInteraction(DeltaTime);
}

bool AMRViewerPawn::IsSuitGrabbed() const
{
	return GetSuitsInRoom().ContainsByPredicate([](const AMRSuit* Suit) { return Suit->IsGrabbed(); });
}

bool AMRViewerPawn::IsTwoHandScaling() const
{
	return GetSuitsInRoom().ContainsByPredicate([](const AMRSuit* Suit) { return Suit->IsTwoHandGrabbed(); });
}

void AMRViewerPawn::SpawnModel(int32 ModelIndex)
{
	if (CachedViewer.IsValid())
	{
		CachedViewer->ShowModel(ModelIndex);
	}
}

void AMRViewerPawn::ClearRoom()
{
	if (CachedViewer.IsValid())
	{
		CachedViewer->ClearModel();
	}
}

void AMRViewerPawn::HandleModelSelected(int32 ModelIndex)
{
	// A model button puts that model in the room, or takes it out again; the other models stay.
	if (CachedViewer.IsValid())
	{
		CachedViewer->ToggleModel(ModelIndex);
	}
}

void AMRViewerPawn::HandleClearRoom()
{
	ClearRoom();
}

void AMRViewerPawn::HandleResetPose()
{
	ResetIronMan();
}

TArray<AMRSuit*> AMRViewerPawn::GetSuitsInRoom() const
{
	if (CachedViewer.IsValid())
	{
		return CachedViewer->GetShownSuits();
	}

	TArray<AMRSuit*> Suits;
	for (TActorIterator<AMRSuit> It(GetWorld()); It; ++It)
	{
		Suits.Add(*It);
	}
	return Suits;
}

bool AMRViewerPawn::GetTrackedHand(bool bRightHand, FXRHandTrackingState& OutHand) const
{
	UHeadMountedDisplayFunctionLibrary::GetHandTrackingState(const_cast<AMRViewerPawn*>(this), EXRSpaceType::UnrealWorldSpace,
		bRightHand ? EControllerHand::Right : EControllerHand::Left, OutHand);

	return OutHand.bValid
		&& OutHand.TrackingStatus == ETrackingStatus::Tracked
		&& OutHand.HandKeyLocations.Num() > static_cast<int32>(EHandKeypoint::IndexTip)
		&& OutHand.HandKeyRotations.Num() > static_cast<int32>(EHandKeypoint::Palm);
}

bool AMRViewerPawn::IsHandTracked(bool bRightHand) const
{
	FXRHandTrackingState Hand;
	if (GetTrackedHand(bRightHand, Hand))
	{
		return true;
	}
	const UMotionControllerComponent* Grip = bRightHand ? RightGrip : LeftGrip;
	const UMotionControllerComponent* Aim = bRightHand ? RightAim : LeftAim;
	return (Grip && Grip->IsTracked()) || (Aim && Aim->IsTracked());
}

FVector AMRViewerPawn::GetHandLocation(bool bRightHand) const
{
	// Tracked hand: grab from the pinch point between thumb and index tips.
	FXRHandTrackingState Hand;
	if (GetTrackedHand(bRightHand, Hand))
	{
		return (Hand.HandKeyLocations[static_cast<int32>(EHandKeypoint::ThumbTip)]
			+ Hand.HandKeyLocations[static_cast<int32>(EHandKeypoint::IndexTip)]) * 0.5f;
	}

	const UMotionControllerComponent* Grip = bRightHand ? RightGrip : LeftGrip;
	if (Grip && Grip->IsTracked())
	{
		return Grip->GetComponentLocation();
	}

	const UMotionControllerComponent* Aim = bRightHand ? RightAim : LeftAim;
	if (Aim && Aim->IsTracked())
	{
		return Aim->GetComponentLocation();
	}

	return GetActorLocation();
}

FQuat AMRViewerPawn::GetHandRotation(bool bRightHand) const
{
	FXRHandTrackingState Hand;
	if (GetTrackedHand(bRightHand, Hand))
	{
		return Hand.HandKeyRotations[static_cast<int32>(EHandKeypoint::Palm)];
	}

	const UMotionControllerComponent* Grip = bRightHand ? RightGrip : LeftGrip;
	if (Grip && Grip->IsTracked())
	{
		return Grip->GetComponentQuat();
	}

	const UMotionControllerComponent* Aim = bRightHand ? RightAim : LeftAim;
	if (Aim && Aim->IsTracked())
	{
		return Aim->GetComponentQuat();
	}

	return GetActorQuat();
}

bool AMRViewerPawn::IsHandGrabbing(bool bRightHand, float& OutStrength)
{
	OutStrength = 0.f;
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return false;
	}

	// 1. Hand tracking: thumb + index pinch.
	// Not gated on UOculusXRInputFunctionLibrary::IsHandTrackingEnabled(): that only answers for the legacy OVR backend
	// and is always false with the native OpenXR backend this project uses, while the pinch keys below still arrive.
	bool& bPinching = bRightHand ? bRightPinching : bLeftPinching;

	const float KeyStrength = PC->GetInputAnalogKeyState(
		FKey(bRightHand ? TEXT("OculusHand_Right_IndexPinchStrength") : TEXT("OculusHand_Left_IndexPinchStrength")));
	const bool bKeyPinch = PC->IsInputKeyDown(
		FKey(bRightHand ? TEXT("OculusHand_Right_IndexPinch") : TEXT("OculusHand_Left_IndexPinch")));

	// Backup: measure the fingertips directly, in case the runtime sends no pinch keys.
	bool bTipsPinch = false;
	float TipStrength = 0.f;
	FXRHandTrackingState Hand;
	if (GetTrackedHand(bRightHand, Hand))
	{
		const float TipDistance = FVector::Dist(
			Hand.HandKeyLocations[static_cast<int32>(EHandKeypoint::ThumbTip)],
			Hand.HandKeyLocations[static_cast<int32>(EHandKeypoint::IndexTip)]);
		bTipsPinch = TipDistance < (bPinching ? MRViewerPawn::PinchReleaseDistance : MRViewerPawn::PinchStartDistance);
		TipStrength = 1.f - FMath::GetRangePct(MRViewerPawn::PinchStartDistance, MRViewerPawn::PinchReleaseDistance * 2.f,
			FMath::Clamp(TipDistance, MRViewerPawn::PinchStartDistance, MRViewerPawn::PinchReleaseDistance * 2.f));
	}

	const bool bKeysPinch = bKeyPinch
		|| KeyStrength >= (bPinching ? MRViewerPawn::PinchReleaseStrength : MRViewerPawn::PinchStartStrength);
	bPinching = bKeysPinch || bTipsPinch;

	if (bPinching)
	{
		OutStrength = 1.f;
		return true;
	}
	OutStrength = FMath::Max(KeyStrength, TipStrength);

	// 2. Controller Grip & Trigger (Fallback)
	const FName GripAxis = bRightHand ? TEXT("OculusTouch_Right_Grip_Axis") : TEXT("OculusTouch_Left_Grip_Axis");
	const FName TriggerAxis = bRightHand ? TEXT("OculusTouch_Right_Trigger_Axis") : TEXT("OculusTouch_Left_Trigger_Axis");
	const FKey GripClick = bRightHand ? FKey(TEXT("OculusTouch_Right_Grip_Click")) : FKey(TEXT("OculusTouch_Left_Grip_Click"));
	const FKey TriggerClick = bRightHand ? FKey(TEXT("OculusTouch_Right_Trigger_Click")) : FKey(TEXT("OculusTouch_Left_Trigger_Click"));

	const float GripVal = PC->GetInputAnalogKeyState(FKey(GripAxis));
	const float TriggerVal = PC->GetInputAnalogKeyState(FKey(TriggerAxis));
	const bool bGripClick = PC->IsInputKeyDown(GripClick);
	const bool bTriggerClick = PC->IsInputKeyDown(TriggerClick);

	OutStrength = FMath::Max(GripVal, TriggerVal);
	return bGripClick || bTriggerClick || OutStrength > 0.45f;
}

bool AMRViewerPawn::FindGrabPoint(bool bRightHand, const AMRSuit* Suit, FVector& OutGrabPoint) const
{
	const UBoxComponent* Box = Suit ? Suit->GetInteractionBox() : nullptr;
	if (!Box)
	{
		return false;
	}

	// Direct grab: the hand touches (or is inside) the model's outline; hold it at the nearest spot on the outline.
	if (Suit->FindTouchPoint(GetHandLocation(bRightHand), DirectGrabReachDistance, OutGrabPoint))
	{
		return true;
	}

	// Distance grab: hold the suit where the hand's aim ray enters its outline box.
	const UMotionControllerComponent* Aim = bRightHand ? RightAim : LeftAim;
	if (!MRViewerPawn::bAllowDistanceGrab || !Aim || !Aim->IsTracked())
	{
		return false;
	}

	// Work in the outline box's own unscaled space; world distances are divided by the suit's scale.
	const FTransform BoxTransform = Box->GetComponentTransform();
	const float Scale = FMath::Max(BoxTransform.GetScale3D().X, KINDA_SMALL_NUMBER);
	const FVector Extent = Box->GetUnscaledBoxExtent();

	const FVector RayOrigin = BoxTransform.InverseTransformPosition(Aim->GetComponentLocation());
	const FVector RayDir = BoxTransform.InverseTransformVectorNoScale(Aim->GetForwardVector());
	const FVector Expanded = Extent + FVector(GrabAimTolerance / Scale);

	float Entry = 0.f;
	float Exit = MaxGrabRange / Scale;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (FMath::IsNearlyZero(RayDir[Axis]))
		{
			if (FMath::Abs(RayOrigin[Axis]) > Expanded[Axis])
			{
				return false;
			}
			continue;
		}
		float Near = (-Expanded[Axis] - RayOrigin[Axis]) / RayDir[Axis];
		float Far = (Expanded[Axis] - RayOrigin[Axis]) / RayDir[Axis];
		if (Near > Far)
		{
			Swap(Near, Far);
		}
		Entry = FMath::Max(Entry, Near);
		Exit = FMath::Min(Exit, Far);
		if (Entry > Exit)
		{
			return false;
		}
	}

	const FVector HitLocal = RayOrigin + RayDir * Entry;
	OutGrabPoint = BoxTransform.TransformPosition(HitLocal.BoundToBox(-Extent, Extent));
	return true;
}

AMRSuit* AMRViewerPawn::FindTouchedSuit(bool bRightHand, const TArray<AMRSuit*>& Suits, FVector& OutGrabPoint) const
{
	const FVector Hand = GetHandLocation(bRightHand);
	AMRSuit* Touched = nullptr;
	float BestDistance = TNumericLimits<float>::Max();
	for (AMRSuit* Suit : Suits)
	{
		FVector GrabPoint;
		if (FindGrabPoint(bRightHand, Suit, GrabPoint))
		{
			const float Distance = FVector::Dist(Hand, GrabPoint);
			if (Distance < BestDistance)
			{
				Touched = Suit;
				BestDistance = Distance;
				OutGrabPoint = GrabPoint;
			}
		}
	}
	return Touched;
}

void AMRViewerPawn::DrawGrabFeedback(const AMRSuit* Suit, bool bRightHand) const
{
	// A small marker on the spot being held, joined to the hand by a thin line.
	const FVector GrabPoint = Suit->GetActorTransform().TransformPosition(bRightHand ? RightGrabLocal : LeftGrabLocal);
	DrawDebugLine(GetWorld(), GetHandLocation(bRightHand), GrabPoint, FColor(0, 220, 255), false, -1.f, 0, 0.3f);
	DrawDebugSphere(GetWorld(), GrabPoint, 2.f, 8, FColor(0, 220, 255), false, -1.f, 0, 0.3f);
}

void AMRViewerPawn::ResetIronMan()
{
	bLeftGrabbing = false;
	bRightGrabbing = false;
	LeftHeldSuit.Reset();
	RightHeldSuit.Reset();
	for (AMRSuit* Suit : GetSuitsInRoom())
	{
		Suit->ResetIronMan();
	}
}

void AMRViewerPawn::ReleaseHand(bool bRightHand, const FVector& OtherHandLocation, const FQuat& OtherHandRotation)
{
	(bRightHand ? bRightGrabbing : bLeftGrabbing) = false;
	TWeakObjectPtr<AMRSuit>& Held = bRightHand ? RightHeldSuit : LeftHeldSuit;
	AMRSuit* Suit = Held.Get();
	Held.Reset();
	if (!Suit)
	{
		return;
	}

	Suit->EndGrab(bRightHand);
	const bool bOtherHandHolds = bRightHand ? (bLeftGrabbing && LeftHeldSuit.Get() == Suit) : (bRightGrabbing && RightHeldSuit.Get() == Suit);
	if (bOtherHandHolds)
	{
		// Seamlessly fall back to a one-hand hold with the other hand
		Suit->StartOneHandGrab(!bRightHand, OtherHandLocation, OtherHandRotation);
	}
}

void AMRViewerPawn::UpdateOneHandHold(AMRSuit* Suit, bool bRightHand, const FVector& HandLocation, const FQuat& HandRotation, float DeltaTime) const
{
	// Phase 3, 4, 5: One-Hand Grab & Move & Rotate
	if (!Suit->IsGrabbed() || Suit->IsTwoHandGrabbed() || Suit->IsGrabbedWithRightHand() != bRightHand)
	{
		Suit->StartOneHandGrab(bRightHand, HandLocation, HandRotation);
	}
	Suit->UpdateOneHandGrab(bRightHand, HandLocation, HandRotation, DeltaTime);
	DrawGrabFeedback(Suit, bRightHand);
}

void AMRViewerPawn::UpdateSuitInteraction(float DeltaTime)
{
	const TArray<AMRSuit*> Suits = GetSuitsInRoom();

	// A model taken out of the room (menu or Clear) lets go of the hand holding it.
	if (bLeftGrabbing && !LeftHeldSuit.IsValid())
	{
		bLeftGrabbing = false;
	}
	if (bRightGrabbing && !RightHeldSuit.IsValid())
	{
		bRightGrabbing = false;
	}

	const APlayerController* PC = Cast<APlayerController>(GetController());

	// Reset Mechanism check (Controller A/X button or UI callable)
	if (PC)
	{
		const bool bAButton = PC->IsInputKeyDown(FKey(TEXT("OculusTouch_Right_FaceButton1")));
		const bool bXButton = PC->IsInputKeyDown(FKey(TEXT("OculusTouch_Left_FaceButton1")));
		const bool bResetNow = (bAButton || bXButton);
		if (bResetNow && !bPrevResetButton)
		{
			ResetIronMan();
		}
		bPrevResetButton = bResetNow;
	}

	// A hand that loses tracking lets go: its location would fall back to the room origin and drag the model there.
	float LeftStrength = 0.f;
	float RightStrength = 0.f;
	const bool bLeftWantsGrab = IsHandGrabbing(false, LeftStrength) && IsHandTracked(false);
	const bool bRightWantsGrab = IsHandGrabbing(true, RightStrength) && IsHandTracked(true);

	const FVector LeftPos = GetHandLocation(false);
	const FQuat LeftRot = GetHandRotation(false);
	const FVector RightPos = GetHandLocation(true);
	const FQuat RightRot = GetHandRotation(true);

	// Evaluate Left Hand Grab transitions: take hold of the model the hand touches, or let go
	if (bLeftWantsGrab && !bPrevLeftGrabIntent)
	{
		FVector GrabPoint;
		if (AMRSuit* Touched = FindTouchedSuit(false, Suits, GrabPoint))
		{
			bLeftGrabbing = true;
			LeftHeldSuit = Touched;
			LeftGrabLocal = Touched->GetActorTransform().InverseTransformPosition(GrabPoint);
		}
	}
	else if (!bLeftWantsGrab && bLeftGrabbing)
	{
		ReleaseHand(false, RightPos, RightRot);
	}

	// Evaluate Right Hand Grab transitions
	if (bRightWantsGrab && !bPrevRightGrabIntent)
	{
		FVector GrabPoint;
		if (AMRSuit* Touched = FindTouchedSuit(true, Suits, GrabPoint))
		{
			bRightGrabbing = true;
			RightHeldSuit = Touched;
			RightGrabLocal = Touched->GetActorTransform().InverseTransformPosition(GrabPoint);
		}
	}
	else if (!bRightWantsGrab && bRightGrabbing)
	{
		ReleaseHand(true, LeftPos, LeftRot);
	}

	bPrevLeftGrabIntent = bLeftWantsGrab;
	bPrevRightGrabIntent = bRightWantsGrab;

	// Execute appropriate interaction phase
	AMRSuit* LeftSuit = bLeftGrabbing ? LeftHeldSuit.Get() : nullptr;
	AMRSuit* RightSuit = bRightGrabbing ? RightHeldSuit.Get() : nullptr;
	if (LeftSuit && LeftSuit == RightSuit)
	{
		// Phase 6 & Phase 7: Two-Hand Scaling and Multi-Axis Transformation of the model both hands hold
		if (!LeftSuit->IsTwoHandGrabbed())
		{
			LeftSuit->StartTwoHandGrab(LeftPos, RightPos);
		}
		LeftSuit->UpdateTwoHandGrab(LeftPos, RightPos, DeltaTime);

		DrawGrabFeedback(LeftSuit, false);
		DrawGrabFeedback(LeftSuit, true);
	}
	else
	{
		// Each hand moves the model it holds (two different models can be moved at once).
		if (LeftSuit)
		{
			UpdateOneHandHold(LeftSuit, false, LeftPos, LeftRot, DeltaTime);
		}
		if (RightSuit)
		{
			UpdateOneHandHold(RightSuit, true, RightPos, RightRot, DeltaTime);
		}
	}

	// Hover: a small dim marker on the spot each free hand would take hold of.
	for (const bool bRightHand : { false, true })
	{
		FVector HoverPoint;
		if (!(bRightHand ? RightSuit : LeftSuit) && FindTouchedSuit(bRightHand, Suits, HoverPoint))
		{
			DrawDebugSphere(GetWorld(), HoverPoint, 1.5f, 8, FColor(0, 180, 255), false, -1.f, 0, 0.2f);
		}
	}
}
