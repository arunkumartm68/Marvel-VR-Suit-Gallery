#include "MRSuit.h"

#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "MRSuitConfiguration.h"
#include "UObject/ConstructorHelpers.h"

namespace MRSuit
{
	// /Engine/BasicShapes/Plane is 100 cm across.
	constexpr float PlaneSize = 100.f;
	// Keeps the contact shadow just above the floor plane it lies on.
	constexpr float ContactShadowLift = 0.2f;
	// How far (cm) the feet may be from the placement floor, and how upright the suit must be, to show the contact shadow.
	constexpr float OnFloorTolerance = 3.f;
	constexpr float UprightMinDot = 0.98f;
}

AMRSuit::AMRSuit()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("FloorPoint"));

	SuitMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SuitMesh"));
	SuitMesh->SetupAttachment(RootComponent);
	SuitMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SuitMesh->SetCastShadow(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	ContactShadow = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ContactShadow"));
	ContactShadow->SetupAttachment(RootComponent);
	ContactShadow->SetStaticMesh(PlaneMesh.Object);
	ContactShadow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ContactShadow->SetCastShadow(false);

	// Interaction collision volume covering the 190 cm suit
	InteractionBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBox"));
	InteractionBox->SetupAttachment(RootComponent);
	InteractionBox->SetBoxExtent(FVector(50.f, 50.f, 95.f));
	InteractionBox->SetRelativeLocation(FVector(0.f, 0.f, 95.f));
	InteractionBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionBox->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
}

void AMRSuit::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyConfiguration();
}

void AMRSuit::BeginPlay()
{
	Super::BeginPlay();
	ApplyConfiguration();
	CacheLightDefaults();
	SetInitialPlacement(GetActorTransform());
}

void AMRSuit::CacheLightDefaults()
{
	LightDefaults.Reset();

	TArray<ULocalLightComponent*> Lights;
	GetComponents<ULocalLightComponent>(Lights);
	for (ULocalLightComponent* Light : Lights)
	{
		FSuitLightDefaults& Defaults = LightDefaults.AddDefaulted_GetRef();
		Defaults.Light = Light;
		Defaults.Intensity = Light->Intensity;
		Defaults.AttenuationRadius = Light->AttenuationRadius;
		if (const UPointLightComponent* PointLight = Cast<UPointLightComponent>(Light))
		{
			Defaults.SourceRadius = PointLight->SourceRadius;
		}
	}
}

void AMRSuit::ScaleLights()
{
	// Light falls off with the square of distance, and the lights sit CurrentScale times closer or farther.
	const float IntensityScale = CurrentScale * CurrentScale;
	for (const FSuitLightDefaults& Defaults : LightDefaults)
	{
		ULocalLightComponent* Light = Defaults.Light.Get();
		if (!Light)
		{
			continue;
		}
		Light->SetIntensity(Defaults.Intensity * IntensityScale);
		Light->SetAttenuationRadius(Defaults.AttenuationRadius * CurrentScale);
		if (UPointLightComponent* PointLight = Cast<UPointLightComponent>(Light))
		{
			PointLight->SetSourceRadius(Defaults.SourceRadius * CurrentScale);
		}
	}
}

void AMRSuit::UpdateContactShadowVisibility()
{
	const bool bShadowEnabled = Configuration && Configuration->bShowContactShadow && Configuration->ContactShadowMaterial;
	const bool bOnFloor = FMath::Abs(GetActorLocation().Z - InitialTransform.GetLocation().Z) <= MRSuit::OnFloorTolerance
		&& GetActorUpVector().Z >= MRSuit::UprightMinDot;
	ContactShadow->SetVisibility(bShadowEnabled && bOnFloor);
}

void AMRSuit::ApplyConfiguration()
{
	if (!Configuration)
	{
		return;
	}

	SuitMesh->SetStaticMesh(Configuration->SuitMesh);
	SuitMesh->SetRelativeTransform(FTransform(
		Configuration->RotationOffset,
		Configuration->PositionOffset + FVector(0.f, 0.f, Configuration->FloorOffset),
		FVector(Configuration->SuitScale)));

	// Fit the grab outline to the suit itself, so a pinch only takes hold on the model.
	if (Configuration->SuitMesh)
	{
		const FBox SuitBounds = Configuration->SuitMesh->GetBoundingBox().TransformBy(SuitMesh->GetRelativeTransform());
		InteractionBox->SetRelativeLocation(SuitBounds.GetCenter());
		InteractionBox->SetBoxExtent(SuitBounds.GetExtent(), false);
	}

	// The shadow stays on the real floor (ignores FloorOffset) and follows the suit's footprint.
	const bool bShowShadow = Configuration->bShowContactShadow && Configuration->ContactShadowMaterial;
	ContactShadow->SetVisibility(bShowShadow);
	if (bShowShadow)
	{
		const float PlaneScale = Configuration->ContactShadowSize * Configuration->SuitScale / MRSuit::PlaneSize;
		ContactShadow->SetMaterial(0, Configuration->ContactShadowMaterial);
		ContactShadow->SetRelativeTransform(FTransform(
			FRotator(0.f, Configuration->RotationOffset.Yaw + Configuration->ContactShadowYaw, 0.f),
			FVector(Configuration->PositionOffset.X, Configuration->PositionOffset.Y, MRSuit::ContactShadowLift),
			FVector(PlaneScale, PlaneScale, 1.f)));
	}
}

float AMRSuit::GetSuitHeight() const
{
	return Configuration ? Configuration->GetSuitHeight() : 190.f;
}

float AMRSuit::GetMinimumScale() const
{
	return Configuration ? Configuration->MinimumScale : 0.2f;
}

float AMRSuit::GetMaximumScale() const
{
	return Configuration ? Configuration->MaximumScale : 3.0f;
}

float AMRSuit::GetTargetHeight() const
{
	return Configuration ? Configuration->TargetHeight : 190.f;
}

void AMRSuit::SetSuitScale(float NewScale)
{
	CurrentScale = FMath::Clamp(NewScale, GetMinimumScale(), GetMaximumScale());
	SetActorScale3D(FVector(CurrentScale));
	ScaleLights();
}

void AMRSuit::ResetIronMan()
{
	bIsGrabbed = false;
	bIsTwoHandGrabbed = false;
	SetSuitScale(Configuration ? Configuration->InitialScale : 1.f);
	SetActorTransform(InitialTransform);
	UpdateContactShadowVisibility();
}

void AMRSuit::SetInitialPlacement(const FTransform& InTransform)
{
	InitialTransform = InTransform;
	SetActorTransform(InitialTransform);
	SetSuitScale(Configuration ? Configuration->InitialScale : 1.f);
	UpdateContactShadowVisibility();
}

void AMRSuit::StartOneHandGrab(bool bRightHand, const FVector& HandLocation, const FQuat& HandRotation)
{
	bIsGrabbed = true;
	bIsTwoHandGrabbed = false;
	bGrabbedWithRightHand = bRightHand;

	const FVector SuitPos = GetActorLocation();
	const FQuat SuitRot = GetActorQuat();

	// Calculate grab offset relative to the grabbing hand's coordinate frame
	OneHandLocalOffset = HandRotation.Inverse() * (SuitPos - HandLocation);
	OneHandRotationOffset = HandRotation.Inverse() * SuitRot;
}

void AMRSuit::UpdateOneHandGrab(bool bRightHand, const FVector& HandLocation, const FQuat& HandRotation, float DeltaTime)
{
	if (!bIsGrabbed || bIsTwoHandGrabbed)
	{
		return;
	}

	const FVector TargetLocation = HandLocation + (HandRotation * OneHandLocalOffset);
	const FQuat TargetRotation = HandRotation * OneHandRotationOffset;

	// High-frequency jitter filter for rock-solid stability in hand tracking
	constexpr float InterpSpeed = 25.f;
	const FVector SmoothLocation = FMath::VInterpTo(GetActorLocation(), TargetLocation, DeltaTime, InterpSpeed);
	const FQuat SmoothRotation = FMath::QInterpTo(GetActorQuat(), TargetRotation, DeltaTime, InterpSpeed);

	SetActorLocationAndRotation(SmoothLocation, SmoothRotation);
	UpdateContactShadowVisibility();
}

void AMRSuit::StartTwoHandGrab(const FVector& LeftHandLocation, const FVector& RightHandLocation)
{
	bIsGrabbed = true;
	bIsTwoHandGrabbed = true;

	TwoHandInitialDistance = FMath::Max(FVector::Dist(LeftHandLocation, RightHandLocation), 5.f);
	TwoHandInitialScale = CurrentScale;

	TwoHandInitialMidpoint = (LeftHandLocation + RightHandLocation) * 0.5f;
	TwoHandInitialOffset = GetActorLocation() - TwoHandInitialMidpoint;

	TwoHandInitialHandVector = RightHandLocation - LeftHandLocation;
	if (TwoHandInitialHandVector.IsNearlyZero())
	{
		TwoHandInitialHandVector = FVector::ForwardVector;
	}
	TwoHandInitialSuitRotation = GetActorQuat();
}

void AMRSuit::UpdateTwoHandGrab(const FVector& LeftHandLocation, const FVector& RightHandLocation, float DeltaTime)
{
	if (!bIsTwoHandGrabbed)
	{
		return;
	}

	const float CurrentDistance = FMath::Max(FVector::Dist(LeftHandLocation, RightHandLocation), 5.f);
	const FVector CurrentMidpoint = (LeftHandLocation + RightHandLocation) * 0.5f;
	FVector CurrentHandVector = RightHandLocation - LeftHandLocation;
	if (CurrentHandVector.IsNearlyZero())
	{
		CurrentHandVector = FVector::ForwardVector;
	}

	// 1. Two-Hand Scaling (Hands move apart -> bigger, move closer -> smaller)
	const float DistanceRatio = CurrentDistance / TwoHandInitialDistance;
	const float TargetScale = FMath::Clamp(TwoHandInitialScale * DistanceRatio, GetMinimumScale(), GetMaximumScale());
	const float SmoothScale = FMath::FInterpTo(CurrentScale, TargetScale, DeltaTime, 20.f);
	SetSuitScale(SmoothScale);

	// 2. Two-Hand Rotation
	const FQuat DeltaHandsQuat = FQuat::FindBetweenVectors(TwoHandInitialHandVector, CurrentHandVector);
	const FQuat TargetRotation = DeltaHandsQuat * TwoHandInitialSuitRotation;

	// 3. Two-Hand Translation (pivoted around hands midpoint)
	const float ScaleRatio = (TwoHandInitialScale > 0.001f) ? (SmoothScale / TwoHandInitialScale) : 1.f;
	const FVector TargetLocation = CurrentMidpoint + (DeltaHandsQuat * (TwoHandInitialOffset * ScaleRatio));

	constexpr float InterpSpeed = 25.f;
	const FVector SmoothLocation = FMath::VInterpTo(GetActorLocation(), TargetLocation, DeltaTime, InterpSpeed);
	const FQuat SmoothRotation = FMath::QInterpTo(GetActorQuat(), TargetRotation, DeltaTime, InterpSpeed);

	SetActorLocationAndRotation(SmoothLocation, SmoothRotation);
	UpdateContactShadowVisibility();
}

void AMRSuit::EndGrab(bool bRightHand)
{
	if (!bIsGrabbed)
	{
		return;
	}

	if (bIsTwoHandGrabbed)
	{
		// Fall back to one-hand grab with the other hand
		bIsTwoHandGrabbed = false;
		bGrabbedWithRightHand = !bRightHand;
	}
	else
	{
		// Fully released: stays exactly where placed (no gravity, no physics)
		bIsGrabbed = false;
		bIsTwoHandGrabbed = false;
	}
}
