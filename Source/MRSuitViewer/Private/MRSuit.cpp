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
	// Let go with the model at most this far (cm at life size) above the floor and it lands on it.
	constexpr float LandingDistance = 10.f;
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

float AMRSuit::GetLowestOutlinePoint(const FTransform& ActorTransform) const
{
	float Lowest = TNumericLimits<float>::Max();
	if (Configuration && Configuration->GrabCapsules.Num() > 0)
	{
		const FTransform MeshToWorld = SuitMesh->GetRelativeTransform() * ActorTransform;
		const float Scale = MeshToWorld.GetMaximumAxisScale();
		for (const FMRGrabCapsule& Capsule : Configuration->GrabCapsules)
		{
			const float EndsZ = FMath::Min(MeshToWorld.TransformPosition(Capsule.Start).Z, MeshToWorld.TransformPosition(Capsule.End).Z);
			Lowest = FMath::Min(Lowest, EndsZ - Capsule.Radius * Scale);
		}
		return Lowest;
	}

	const FTransform BoxToWorld = InteractionBox->GetRelativeTransform() * ActorTransform;
	const FVector Extent = InteractionBox->GetUnscaledBoxExtent();
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector Local((Corner & 1) ? Extent.X : -Extent.X, (Corner & 2) ? Extent.Y : -Extent.Y, (Corner & 4) ? Extent.Z : -Extent.Z);
		Lowest = FMath::Min(Lowest, BoxToWorld.TransformPosition(Local).Z);
	}
	return Lowest;
}

float AMRSuit::GetLowestAllowedPoint() const
{
	// Standing upright on the floor is as low as the model goes. Its outline may dip a little below the floor in that
	// pose (round capsule ends, a negative floor offset), so that much is allowed at any orientation.
	const float FloorZ = InitialTransform.GetLocation().Z;
	const float RestingLowest = GetLowestOutlinePoint(FTransform(FQuat::Identity, FVector(0.f, 0.f, FloorZ), FVector(CurrentScale)));
	return FMath::Min(FloorZ, RestingLowest);
}

void AMRSuit::KeepAboveFloor(FVector& Location, FQuat& Rotation) const
{
	const FVector Scale3D(CurrentScale);
	const float LowestAllowed = GetLowestAllowedPoint();
	if (GetLowestOutlinePoint(FTransform(Rotation, Location, Scale3D)) >= LowestAllowed)
	{
		return;
	}

	// Touching the floor: stand upright on it instead of resting on a tilted corner (a claw tip, a heel) with the
	// feet hanging above the floor.
	Rotation = FRotator(0.f, Rotation.Rotator().Yaw, 0.f).Quaternion();
	const float Lowest = GetLowestOutlinePoint(FTransform(Rotation, Location, Scale3D));
	if (Lowest < LowestAllowed)
	{
		Location.Z += LowestAllowed - Lowest;
	}
}

void AMRSuit::SettleOnFloor()
{
	const FQuat Upright = FRotator(0.f, GetActorRotation().Yaw, 0.f).Quaternion();
	FVector Location = GetActorLocation();
	const float Gap = GetLowestOutlinePoint(FTransform(Upright, Location, FVector(CurrentScale))) - GetLowestAllowedPoint();
	if (Gap > MRSuit::LandingDistance * CurrentScale)
	{
		return; // let go in the air: it stays floating where it is
	}

	Location.Z -= Gap;
	SetActorLocationAndRotation(Location, Upright);
	UpdateContactShadowVisibility();
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

bool AMRSuit::FindTouchPoint(const FVector& WorldPoint, float Reach, FVector& OutTouchPoint) const
{
	if (Configuration && Configuration->GrabCapsules.Num() > 0)
	{
		// The capsules are in the mesh's own space; local distances times the suit's scale are world distances.
		const FTransform MeshTransform = SuitMesh->GetComponentTransform();
		const float Scale = FMath::Max(MeshTransform.GetMaximumAxisScale(), KINDA_SMALL_NUMBER);
		const FVector Local = MeshTransform.InverseTransformPosition(WorldPoint);

		float BestGap = TNumericLimits<float>::Max();
		FVector BestLocal = Local;
		for (const FMRGrabCapsule& Capsule : Configuration->GrabCapsules)
		{
			const FVector OnAxis = FMath::ClosestPointOnSegment(Local, Capsule.Start, Capsule.End);
			const FVector ToPoint = Local - OnAxis;
			const float Gap = ToPoint.Size() - Capsule.Radius;
			if (Gap < BestGap)
			{
				BestGap = Gap;
				BestLocal = Gap <= 0.f ? Local : OnAxis + ToPoint.GetSafeNormal() * Capsule.Radius;
			}
		}
		if (BestGap * Scale > Reach)
		{
			return false;
		}
		OutTouchPoint = MeshTransform.TransformPosition(BestLocal);
		return true;
	}

	// No outline given: the box around the whole model, in its own unscaled space.
	const FTransform BoxTransform = InteractionBox->GetComponentTransform();
	const float Scale = FMath::Max(BoxTransform.GetScale3D().X, KINDA_SMALL_NUMBER);
	const FVector Extent = InteractionBox->GetUnscaledBoxExtent();
	const FVector Local = BoxTransform.InverseTransformPosition(WorldPoint);
	const FVector Touch = Local.BoundToBox(-Extent, Extent);
	if (FVector::Dist(Local, Touch) * Scale > Reach)
	{
		return false;
	}
	OutTouchPoint = BoxTransform.TransformPosition(Touch);
	return true;
}

FBox2D AMRSuit::GetFootprint(const FTransform& ActorTransform) const
{
	const FTransform BoxToWorld = InteractionBox->GetRelativeTransform() * ActorTransform;
	const FVector Extent = InteractionBox->GetUnscaledBoxExtent();
	FBox2D Footprint(ForceInit);
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector World = BoxToWorld.TransformPosition(FVector((Corner & 1) ? Extent.X : -Extent.X, (Corner & 2) ? Extent.Y : -Extent.Y, (Corner & 4) ? Extent.Z : -Extent.Z));
		Footprint += FVector2D(World.X, World.Y);
	}
	return Footprint;
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
	FVector SmoothLocation = FMath::VInterpTo(GetActorLocation(), TargetLocation, DeltaTime, InterpSpeed);
	FQuat SmoothRotation = FMath::QInterpTo(GetActorQuat(), TargetRotation, DeltaTime, InterpSpeed);
	KeepAboveFloor(SmoothLocation, SmoothRotation);

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
	FVector SmoothLocation = FMath::VInterpTo(GetActorLocation(), TargetLocation, DeltaTime, InterpSpeed);
	FQuat SmoothRotation = FMath::QInterpTo(GetActorQuat(), TargetRotation, DeltaTime, InterpSpeed);
	KeepAboveFloor(SmoothLocation, SmoothRotation);

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
		// Fully released: stays exactly where placed (no gravity, no physics); just above the floor it lands on it
		bIsGrabbed = false;
		bIsTwoHandGrabbed = false;
		SettleOnFloor();
	}
}
