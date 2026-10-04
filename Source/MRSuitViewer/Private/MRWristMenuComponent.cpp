#include "MRWristMenuComponent.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MotionControllerComponent.h"
#include "MRSuitViewer.h"
#include "MRViewerPawn.h"
#include "UObject/ConstructorHelpers.h"

namespace MRWristMenu
{
	// Button actions besides model buttons, whose action is the model's catalog index (>= 0).
	constexpr int32 ClearAction = -1;
	constexpr int32 ResetAction = -2;

	// Layout in cm. The menu faces the user along its +X axis; Y is across, Z is up.
	constexpr float Width = 16.f;
	constexpr float Padding = 0.8f;
	constexpr float Gap = 0.6f;
	constexpr float TitleHeight = 2.2f;
	constexpr float ModelButtonHeight = 3.4f;
	constexpr float ActionButtonHeight = 2.6f;
	constexpr float ButtonDepth = 0.6f;               // buttons stand this far in front of the panel face
	constexpr float TextDepth = ButtonDepth + 0.1f;

	// Placement beside the left hand: this far to the side of the palm, towards the user's right.
	constexpr float SideGap = 4.f;
	constexpr float TowardsHead = 3.f;
	constexpr float FollowSpeed = 18.f;
	constexpr float TurnSpeed = 12.f;

	// How squarely the left palm must face the head to show the menu, and the lower value that hides it again.
	constexpr float ShowFacing = 0.55f;
	constexpr float HideFacing = 0.25f;

	// Fingertip taps, measured from the button face (positive = in front of it).
	constexpr float HoverDepth = 4.f;
	constexpr float ArmDepth = 1.f;                   // the fingertip must come from at least this far in front
	constexpr float PressDepth = 0.3f;                // a press registers when the fingertip reaches this depth
	constexpr float PushThroughDepth = 4.f;
	constexpr float TapTolerance = 0.5f;              // extra margin around each button for fingertips
	constexpr float RayTolerance = 0.2f;
	constexpr float MaxRayDistance = 300.f;
	constexpr float PressCooldownTime = 0.4f;
	constexpr float FlashDuration = 0.2f;

	const FLinearColor PanelColor(0.006f, 0.010f, 0.016f);
	const FLinearColor ModelColor(0.020f, 0.045f, 0.070f);
	const FLinearColor ModelHoverColor(0.000f, 0.220f, 0.330f);
	const FLinearColor ActiveModelColor(0.010f, 0.160f, 0.080f);
	const FLinearColor ClearColor(0.110f, 0.015f, 0.015f);
	const FLinearColor ResetColor(0.100f, 0.065f, 0.010f);
	const FLinearColor FlashColor(0.100f, 0.600f, 0.900f);
	constexpr float ActionHoverBoost = 3.f;
}

UMRWristMenuComponent::UMRWristMenuComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	PlateMesh = CubeMesh.Object;

	// Used when PlateMaterial is not set: unlit, with the same "Color" parameter.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ReferenceMaterial(TEXT("/Game/MRSuitViewer/Materials/M_MR_ReferenceUnlit.M_MR_ReferenceUnlit"));
	FallbackMaterial = ReferenceMaterial.Object;
}

void UMRWristMenuComponent::BeginPlay()
{
	Super::BeginPlay();

	OwningPawn = Cast<AMRViewerPawn>(GetOwner());
	SetMenuVisible(false, /*bForce=*/true);
}

void UMRWristMenuComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bMenuBuilt)
	{
		if (const AMRSuitViewer* Viewer = GetViewer())
		{
			BuildMenu(*Viewer);
		}
	}

	UpdatePlacement(DeltaTime);
	UpdatePress(DeltaTime);
	if (bMenuVisible)
	{
		UpdateVisuals();
	}
}

AMRSuitViewer* UMRWristMenuComponent::GetViewer() const
{
	if (OwningPawn.IsValid() && OwningPawn->GetViewer())
	{
		return OwningPawn->GetViewer();
	}
	for (TActorIterator<AMRSuitViewer> It(GetWorld()); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

void UMRWristMenuComponent::BuildMenu(const AMRSuitViewer& Viewer)
{
	using namespace MRWristMenu;

	const int32 ModelCount = Viewer.GetModelCount();
	const float Height = Padding + TitleHeight + ModelCount * (ModelButtonHeight + Gap) + ActionButtonHeight + Padding;
	MenuHalfHeight = Height * 0.5f;

	UMaterialInterface* Material = PlateMaterial ? PlateMaterial.Get() : FallbackMaterial.Get();

	Backplate = AddPlate(FVector(-0.2f, 0.f, 0.f), FVector(0.4f, Width, Height));
	BackplateMaterial = Material ? UMaterialInstanceDynamic::Create(Material, this) : nullptr;
	if (BackplateMaterial)
	{
		BackplateMaterial->SetVectorParameterValue(TEXT("Color"), PanelColor);
		Backplate->SetMaterial(0, BackplateMaterial);
	}

	float Top = MenuHalfHeight - Padding;
	AddText(FText::FromString(TEXT("MODELS")), FVector(TextDepth, 0.f, Top - TitleHeight * 0.5f), 1.1f, FColor(0, 220, 255));
	Top -= TitleHeight;

	const FVector2D ModelHalfSize((Width - 2.f * Padding) * 0.5f, ModelButtonHeight * 0.5f);
	for (int32 ModelIndex = 0; ModelIndex < ModelCount; ++ModelIndex)
	{
		AddButton(ModelIndex, Viewer.GetModelName(ModelIndex), Viewer.GetModelDescription(ModelIndex),
			FVector2D(0.f, Top - ModelHalfSize.Y), ModelHalfSize, FColor::White);
		Top -= ModelButtonHeight + Gap;
	}

	// Clear and Reset side by side. The menu faces the user along +X, so +Y is on the user's left.
	const FVector2D ActionHalfSize((Width - 2.f * Padding - Gap) * 0.25f, ActionButtonHeight * 0.5f);
	const float ActionOffset = ActionHalfSize.X + Gap * 0.5f;
	AddButton(ClearAction, FText::FromString(TEXT("CLEAR")), FText::GetEmpty(),
		FVector2D(ActionOffset, Top - ActionHalfSize.Y), ActionHalfSize, FColor(255, 120, 120));
	AddButton(ResetAction, FText::FromString(TEXT("RESET")), FText::GetEmpty(),
		FVector2D(-ActionOffset, Top - ActionHalfSize.Y), ActionHalfSize, FColor(255, 205, 90));

	bMenuBuilt = true;
	SetMenuVisible(bMenuVisible, /*bForce=*/true); // hide or show the components just created
}

FMRMenuButton& UMRWristMenuComponent::AddButton(int32 Action, const FText& Label, const FText& Description, const FVector2D& Center, const FVector2D& HalfSize, const FColor& LabelColor)
{
	using namespace MRWristMenu;

	FMRMenuButton& Button = Buttons.AddDefaulted_GetRef();
	Button.Action = Action;
	Button.Center = Center;
	Button.HalfSize = HalfSize;
	Button.Description = Description;

	Button.Plate = AddPlate(FVector(ButtonDepth * 0.5f, Center.X, Center.Y), FVector(ButtonDepth, HalfSize.X * 2.f, HalfSize.Y * 2.f));
	UMaterialInterface* Material = PlateMaterial ? PlateMaterial.Get() : FallbackMaterial.Get();
	Button.Material = Material ? UMaterialInstanceDynamic::Create(Material, this) : nullptr;
	if (Button.Material)
	{
		Button.Plate->SetMaterial(0, Button.Material);
	}

	// Model buttons have a second line (description, or "In the room" while shown); action buttons have one line.
	if (Action >= 0)
	{
		Button.Label = AddText(Label, FVector(TextDepth, Center.X, Center.Y + 0.55f), 1.1f, LabelColor);
		Button.Detail = AddText(Description, FVector(TextDepth, Center.X, Center.Y - 0.85f), 0.7f, FColor(170, 190, 205));
	}
	else
	{
		Button.Label = AddText(Label, FVector(TextDepth, Center.X, Center.Y), 1.0f, LabelColor);
	}
	return Button;
}

UStaticMeshComponent* UMRWristMenuComponent::AddPlate(const FVector& Location, const FVector& Size)
{
	UStaticMeshComponent* Plate = NewObject<UStaticMeshComponent>(GetOwner());
	Plate->SetMobility(EComponentMobility::Movable);
	Plate->SetStaticMesh(PlateMesh);
	Plate->SetupAttachment(this);
	Plate->SetRelativeLocation(Location);
	Plate->SetRelativeScale3D(Size / 100.f); // the engine cube is 100 cm
	Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Plate->SetGenerateOverlapEvents(false);
	Plate->SetCastShadow(false);
	Plate->RegisterComponent();
	return Plate;
}

UTextRenderComponent* UMRWristMenuComponent::AddText(const FText& Text, const FVector& Location, float Size, const FColor& Color)
{
	UTextRenderComponent* TextComponent = NewObject<UTextRenderComponent>(GetOwner());
	TextComponent->SetMobility(EComponentMobility::Movable);
	TextComponent->SetupAttachment(this);
	TextComponent->SetRelativeLocation(Location); // text faces its +X axis, i.e. the user
	TextComponent->SetHorizontalAlignment(EHTA_Center);
	TextComponent->SetVerticalAlignment(EVRTA_TextCenter);
	TextComponent->SetWorldSize(Size);
	TextComponent->SetTextRenderColor(Color);
	TextComponent->SetText(Text);
	TextComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TextComponent->SetCastShadow(false);
	TextComponent->RegisterComponent();
	return TextComponent;
}

void UMRWristMenuComponent::UpdatePlacement(float DeltaTime)
{
	using namespace MRWristMenu;

	const UCameraComponent* Head = OwningPawn.IsValid() ? OwningPawn->GetHeadCamera() : nullptr;
	if (!Head || !bMenuBuilt)
	{
		SetMenuVisible(false);
		return;
	}
	const FVector HeadLocation = Head->GetComponentLocation();

	bool bWantVisible = false;
	FVector Anchor = FVector::ZeroVector;

	FXRHandTrackingState LeftHand;
	UHeadMountedDisplayFunctionLibrary::GetHandTrackingState(this, EXRSpaceType::UnrealWorldSpace, EControllerHand::Left, LeftHand);
	const UMotionControllerComponent* LeftGrip = OwningPawn->GetLeftGrip();

	if (LeftHand.bValid && LeftHand.TrackingStatus == ETrackingStatus::Tracked
		&& LeftHand.HandKeyLocations.Num() > static_cast<int32>(EHandKeypoint::LittleProximal))
	{
		const FVector Wrist = LeftHand.HandKeyLocations[static_cast<int32>(EHandKeypoint::Wrist)];
		const FVector IndexKnuckle = LeftHand.HandKeyLocations[static_cast<int32>(EHandKeypoint::IndexProximal)];
		const FVector LittleKnuckle = LeftHand.HandKeyLocations[static_cast<int32>(EHandKeypoint::LittleProximal)];
		Anchor = LeftHand.HandKeyLocations[static_cast<int32>(EHandKeypoint::Palm)];

		// Out of the left palm: seen from the palm side, wrist -> index knuckle -> little knuckle turns clockwise.
		const FVector PalmNormal = FVector::CrossProduct(IndexKnuckle - Wrist, LittleKnuckle - Wrist).GetSafeNormal();
		const float Facing = FVector::DotProduct(PalmNormal, (HeadLocation - Anchor).GetSafeNormal());
		bWantVisible = Facing > (bMenuVisible ? HideFacing : ShowFacing);
		bControllerToggle = false;
	}
	else if (LeftGrip && LeftGrip->IsTracked())
	{
		// Controllers: the left menu button shows and hides the menu.
		const APlayerController* PC = Cast<APlayerController>(OwningPawn->GetController());
		const bool bMenuButton = PC && PC->IsInputKeyDown(FKey(TEXT("OculusTouch_Left_Menu_Click")));
		if (bMenuButton && !bPrevMenuButton)
		{
			bControllerToggle = !bControllerToggle;
		}
		bPrevMenuButton = bMenuButton;

		Anchor = LeftGrip->GetComponentLocation();
		bWantVisible = bControllerToggle;
	}

	SetMenuVisible(bWantVisible);
	if (!bMenuVisible)
	{
		bSnapNextPlacement = true;
		return;
	}

	// Beside the hand, towards the user's right so the right hand can reach it, facing the user.
	FVector ViewRight = Head->GetRightVector();
	ViewRight.Z = 0.f;
	ViewRight = ViewRight.GetSafeNormal();
	const FVector ToHead = (HeadLocation - Anchor).GetSafeNormal();
	const FVector TargetLocation = Anchor + ViewRight * (Width * 0.5f + SideGap) + ToHead * TowardsHead;
	const FQuat TargetRotation = FRotationMatrix::MakeFromXZ((HeadLocation - TargetLocation).GetSafeNormal(), FVector::UpVector).ToQuat();

	if (bSnapNextPlacement)
	{
		SetWorldLocationAndRotation(TargetLocation, TargetRotation);
		bSnapNextPlacement = false;
	}
	else
	{
		SetWorldLocationAndRotation(
			FMath::VInterpTo(GetComponentLocation(), TargetLocation, DeltaTime, FollowSpeed),
			FMath::QInterpTo(GetComponentQuat(), TargetRotation, DeltaTime, TurnSpeed));
	}
}

void UMRWristMenuComponent::SetMenuVisible(bool bShow, bool bForce)
{
	if (bShow != bMenuVisible || bForce)
	{
		bMenuVisible = bShow;
		SetVisibility(bShow, /*bPropagateToChildren=*/true);
	}
}

int32 UMRWristMenuComponent::FindButtonAt(const FVector& LocalPoint, float Tolerance) const
{
	for (int32 Index = 0; Index < Buttons.Num(); ++Index)
	{
		const FMRMenuButton& Button = Buttons[Index];
		if (FMath::Abs(LocalPoint.Y - Button.Center.X) <= Button.HalfSize.X + Tolerance
			&& FMath::Abs(LocalPoint.Z - Button.Center.Y) <= Button.HalfSize.Y + Tolerance)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void UMRWristMenuComponent::UpdatePress(float DeltaTime)
{
	using namespace MRWristMenu;

	PressCooldown = FMath::Max(0.f, PressCooldown - DeltaTime);
	FlashTime = FMath::Max(0.f, FlashTime - DeltaTime);
	HoveredButton = INDEX_NONE;

	if (!bMenuVisible || !bMenuBuilt || !OwningPawn.IsValid())
	{
		bTapArmed = false;
		return;
	}

	const FTransform MenuTransform = GetComponentTransform();

	// Hands: tap with the right index fingertip.
	FXRHandTrackingState RightHand;
	UHeadMountedDisplayFunctionLibrary::GetHandTrackingState(this, EXRSpaceType::UnrealWorldSpace, EControllerHand::Right, RightHand);
	if (RightHand.bValid && RightHand.TrackingStatus == ETrackingStatus::Tracked
		&& RightHand.HandKeyLocations.Num() > static_cast<int32>(EHandKeypoint::IndexTip))
	{
		const FVector Tip = MenuTransform.InverseTransformPosition(RightHand.HandKeyLocations[static_cast<int32>(EHandKeypoint::IndexTip)]);
		const float Depth = Tip.X - ButtonDepth;
		const int32 Under = FindButtonAt(Tip, TapTolerance);

		if (Under != INDEX_NONE && Depth < HoverDepth && Depth > -PushThroughDepth)
		{
			HoveredButton = Under;
		}

		if (Depth > ArmDepth)
		{
			bTapArmed = true; // in front of the menu: the next touch is a deliberate tap
		}
		else if (Under == INDEX_NONE)
		{
			bTapArmed = false; // at or behind the face but not over a button, e.g. coming round from behind
		}

		if (bTapArmed && Under != INDEX_NONE && Depth <= PressDepth && PressCooldown <= 0.f)
		{
			bTapArmed = false; // pull the finger back before the next tap
			Press(Under);
		}
		return;
	}

	// Controllers: point the right controller at a button and pull the trigger.
	const UMotionControllerComponent* RightAim = OwningPawn->GetRightAim();
	const APlayerController* PC = Cast<APlayerController>(OwningPawn->GetController());
	if (!RightAim || !RightAim->IsTracked() || !PC)
	{
		return;
	}

	const FVector RayOrigin = MenuTransform.InverseTransformPosition(RightAim->GetComponentLocation());
	const FVector RayDirection = MenuTransform.InverseTransformVectorNoScale(RightAim->GetForwardVector());
	if (RayDirection.X < -KINDA_SMALL_NUMBER)
	{
		const float Distance = (ButtonDepth - RayOrigin.X) / RayDirection.X;
		if (Distance > 0.f && Distance < MaxRayDistance)
		{
			const FVector Hit = RayOrigin + RayDirection * Distance;
			HoveredButton = FindButtonAt(Hit, RayTolerance);
			if (HoveredButton != INDEX_NONE)
			{
				DrawDebugLine(GetWorld(), RightAim->GetComponentLocation(), MenuTransform.TransformPosition(Hit), FColor(0, 220, 255), false, -1.f, 0, 0.3f);
			}
		}
	}

	const bool bTrigger = PC->IsInputKeyDown(FKey(TEXT("OculusTouch_Right_Trigger_Click")))
		|| PC->GetInputAnalogKeyState(FKey(TEXT("OculusTouch_Right_Trigger_Axis"))) > 0.6f;
	if (bTrigger && !bPrevTrigger && HoveredButton != INDEX_NONE && PressCooldown <= 0.f)
	{
		Press(HoveredButton);
	}
	bPrevTrigger = bTrigger;
}

void UMRWristMenuComponent::Press(int32 ButtonIndex)
{
	using namespace MRWristMenu;

	PressCooldown = PressCooldownTime;
	FlashButton = ButtonIndex;
	FlashTime = FlashDuration;

	const int32 Action = Buttons[ButtonIndex].Action;
	if (Action >= 0)
	{
		OnModelSelected.Broadcast(Action);
	}
	else if (Action == ClearAction)
	{
		OnClearRoom.Broadcast();
	}
	else if (Action == ResetAction)
	{
		OnResetPose.Broadcast();
	}
}

void UMRWristMenuComponent::UpdateVisuals()
{
	using namespace MRWristMenu;

	const AMRSuitViewer* Viewer = GetViewer();

	for (int32 Index = 0; Index < Buttons.Num(); ++Index)
	{
		FMRMenuButton& Button = Buttons[Index];
		const bool bIsModel = Button.Action >= 0;
		const bool bActive = bIsModel && Viewer && Viewer->IsModelShown(Button.Action);

		FLinearColor Color = bIsModel ? ModelColor : (Button.Action == ClearAction ? ClearColor : ResetColor);
		if (FlashTime > 0.f && FlashButton == Index)
		{
			Color = FlashColor;
		}
		else if (HoveredButton == Index)
		{
			Color = bIsModel ? ModelHoverColor : Color * ActionHoverBoost;
		}
		else if (bActive)
		{
			Color = ActiveModelColor;
		}

		if (Button.Material && !Color.Equals(Button.ShownColor))
		{
			Button.Material->SetVectorParameterValue(TEXT("Color"), Color);
			Button.ShownColor = Color;
		}

		if (Button.Detail && (bActive != Button.bShowsInRoom || !Button.bDetailInitialised))
		{
			Button.Detail->SetText(bActive ? FText::FromString(TEXT("In the room")) : Button.Description);
			Button.Detail->SetTextRenderColor(bActive ? FColor(90, 255, 150) : FColor(170, 190, 205));
			Button.bShowsInRoom = bActive;
			Button.bDetailInitialised = true;
		}
	}
}
