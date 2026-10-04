#include "MRSuitViewer.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "MRModelCatalog.h"
#include "MRSuit.h"
#include "MRSuitConfiguration.h"
#include "MRSuitViewerModule.h"
#include "MRUtilityKitAnchor.h"
#include "MRUtilityKitRoom.h"
#include "MRUtilityKitSubsystem.h"
#include "OculusXRPassthroughSubsystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#if PLATFORM_ANDROID
#include "AndroidPermissionCallbackProxy.h"
#include "AndroidPermissionFunctionLibrary.h"
#endif

namespace MRSuitViewer
{
	// Horizon OS runtime permission required to read the room scan (Space Setup data).
	static const FString ScenePermission(TEXT("com.oculus.permission.USE_SCENE"));

	// /Engine/BasicShapes meshes are 100 cm across.
	constexpr float BasicShapeSize = 100.f;
	constexpr float FloorGridSize = 300.f;

	// Suit placement: the suit is ~40 cm deep, so keep its centre this far from walls and furniture.
	constexpr float SuitClearance = 60.f;
	constexpr float MinSuitDistance = 100.f;
	// Knee height catches beds, tables and sofas; chest height catches walls.
	constexpr float ClearanceProbeHeights[] = { 40.f, 130.f };
	// A model put in the room beside others: tried in steps to the right and left of straight ahead, this far apart.
	constexpr float SideStep = 25.f;
	constexpr int32 MaxSideSteps = 16;
	constexpr float ModelGap = 20.f;
}

AMRSuitViewer::AMRSuitViewer()
{
	PrimaryActorTick.bCanEverTick = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));

	FloorGrid = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FloorGrid"));
	FloorGrid->SetupAttachment(RootComponent);
	FloorGrid->SetStaticMesh(PlaneMesh.Object);
	FloorGrid->SetRelativeScale3D(FVector(MRSuitViewer::FloorGridSize / MRSuitViewer::BasicShapeSize, MRSuitViewer::FloorGridSize / MRSuitViewer::BasicShapeSize, 1.f));

	ScaleReference = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ScaleReference"));
	ScaleReference->SetupAttachment(RootComponent);
	ScaleReference->SetStaticMesh(CubeMesh.Object);
	ScaleReference->SetRelativeLocation(FVector(MRSuitViewer::FloorGridSize * 0.5f, 0.f, MRSuitViewer::BasicShapeSize * 0.5f));

	StatusPanel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("StatusPanel"));
	StatusPanel->SetupAttachment(RootComponent);
	StatusPanel->SetRelativeLocation(FVector(MRSuitViewer::FloorGridSize * 0.5f, 0.f, 140.f));
	StatusPanel->SetHorizontalAlignment(EHTA_Center);
	StatusPanel->SetVerticalAlignment(EVRTA_TextCenter);
	StatusPanel->SetWorldSize(3.f);
	StatusPanel->SetTextRenderColor(FColor::White);
	StatusPanel->SetText(FText::FromString(TEXT("MR status")));

	UPrimitiveComponent* TestAids[] = { FloorGrid.Get(), ScaleReference.Get(), StatusPanel.Get() };
	for (UPrimitiveComponent* TestAid : TestAids)
	{
		TestAid->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TestAid->SetCastShadow(false);
		TestAid->SetHiddenInGame(true); // shown once the floor is known
	}
}

void AMRSuitViewer::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyTestAidMaterials();
}

void AMRSuitViewer::BeginPlay()
{
	Super::BeginPlay();

	ApplyTestAidMaterials();
	StartPassthrough();
	StartRoomDetection();
}

void AMRSuitViewer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(RoomLoadTimeoutHandle);

	if (UMRUKSubsystem* MRUK = GetMRUK())
	{
		MRUK->OnSceneLoaded.RemoveDynamic(this, &AMRSuitViewer::HandleSceneLoaded);
		MRUK->OnCaptureComplete.RemoveDynamic(this, &AMRSuitViewer::HandleSpaceSetupComplete);
	}

#if PLATFORM_ANDROID
	if (UAndroidPermissionCallbackProxy* Proxy = UAndroidPermissionCallbackProxy::GetInstance())
	{
		Proxy->OnPermissionsGrantedDynamicDelegate.RemoveDynamic(this, &AMRSuitViewer::HandleScenePermission);
	}
#endif

	Super::EndPlay(EndPlayReason);
}

float AMRSuitViewer::GetFloorHeight() const
{
	if (FloorSource == EMRFloorSource::RoomScan && FloorAnchor.IsValid())
	{
		return static_cast<float>(FloorAnchor->GetActorLocation().Z);
	}

	// Stage tracking space: the boundary floor is Z = 0 of the pawn's tracking origin.
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		return static_cast<float>(Pawn->GetActorLocation().Z);
	}
	return static_cast<float>(GetActorLocation().Z);
}

void AMRSuitViewer::RefreshRoom()
{
	if (UMRUKSubsystem* MRUK = GetMRUK())
	{
		if (MRUK->SceneLoadStatus == EMRUKInitStatus::Busy)
		{
			return;
		}
		MRUK->ClearScene();
	}

	FloorAnchor.Reset();
	FloorSource = EMRFloorSource::None;
	StartRoomDetection();
}

void AMRSuitViewer::StartPassthrough()
{
	if (!bEnablePassthrough)
	{
		return;
	}

	UOculusXRPassthroughSubsystem* Passthrough = UOculusXRPassthroughSubsystem::GetPassthroughSubsystem(GetWorld());
	if (!Passthrough)
	{
		UE_LOG(LogMRSuitViewer, Warning, TEXT("Passthrough subsystem missing: enable 'Passthrough Enabled' in Project Settings > Plugins > Meta XR."));
		return;
	}

	// A persistent reconstructed underlay: the real room is composited behind every virtual pixel and survives level loads.
	FOculusXRPassthrough_LayerResumed_Single OnResumed;
	OnResumed.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(AMRSuitViewer, HandlePassthroughResumed));
	Passthrough->InitializePersistentPassthrough(FOculusXRPersistentPassthroughParameters(), OnResumed);
	bPassthroughRequested = true;
}

void AMRSuitViewer::HandlePassthroughResumed()
{
	bPassthroughVisible = true;
	UE_LOG(LogMRSuitViewer, Log, TEXT("Passthrough layer is visible."));
	UpdateStatusPanel();
}

void AMRSuitViewer::StartRoomDetection()
{
	if (!bLoadRoomScan || !GetMRUK())
	{
		UseBoundaryFloor(bLoadRoomScan ? TEXT("MR Utility Kit unavailable") : TEXT("room scan disabled"));
		return;
	}

#if PLATFORM_ANDROID
	if (!UAndroidPermissionFunctionLibrary::CheckPermission(MRSuitViewer::ScenePermission))
	{
		if (UAndroidPermissionCallbackProxy* Proxy = UAndroidPermissionFunctionLibrary::AcquirePermissions({ MRSuitViewer::ScenePermission }))
		{
			SessionState = EMRSessionState::WaitingForScenePermission;
			RoomStatus = TEXT("waiting for spatial data permission...");
			Proxy->OnPermissionsGrantedDynamicDelegate.AddUniqueDynamic(this, &AMRSuitViewer::HandleScenePermission);
			return;
		}
		UE_LOG(LogMRSuitViewer, Warning, TEXT("Could not request %s."), *MRSuitViewer::ScenePermission);
	}
#endif

	LoadRoom();
}

void AMRSuitViewer::HandleScenePermission(const TArray<FString>& Permissions, const TArray<bool>& GrantResults)
{
	const int32 Index = Permissions.IndexOfByKey(MRSuitViewer::ScenePermission);
	if (Index == INDEX_NONE)
	{
		return; // result of some other permission request
	}

#if PLATFORM_ANDROID
	if (UAndroidPermissionCallbackProxy* Proxy = UAndroidPermissionCallbackProxy::GetInstance())
	{
		Proxy->OnPermissionsGrantedDynamicDelegate.RemoveDynamic(this, &AMRSuitViewer::HandleScenePermission);
	}
#endif

	if (GrantResults.IsValidIndex(Index) && GrantResults[Index])
	{
		LoadRoom();
	}
	else
	{
		UseBoundaryFloor(TEXT("spatial data permission denied"));
	}
}

void AMRSuitViewer::LoadRoom()
{
	UMRUKSubsystem* MRUK = GetMRUK();
	if (!MRUK)
	{
		UseBoundaryFloor(TEXT("MR Utility Kit unavailable"));
		return;
	}

	SessionState = EMRSessionState::LoadingRoom;
	RoomStatus = TEXT("loading room scan...");
	MRUK->OnSceneLoaded.AddUniqueDynamic(this, &AMRSuitViewer::HandleSceneLoaded);
	GetWorldTimerManager().SetTimer(RoomLoadTimeoutHandle, this, &AMRSuitViewer::HandleRoomLoadTimeout, RoomLoadTimeout, false);

	if (MRUK->SceneLoadStatus == EMRUKInitStatus::Complete)
	{
		HandleSceneLoaded(true); // already loaded, e.g. after a level reload
	}
	else if (MRUK->SceneLoadStatus != EMRUKInitStatus::Busy)
	{
		MRUK->LoadSceneFromDevice();
	}
}

void AMRSuitViewer::HandleSceneLoaded(bool bSuccess)
{
	GetWorldTimerManager().ClearTimer(RoomLoadTimeoutHandle);

	UMRUKSubsystem* MRUK = GetMRUK();
	if (AMRUKAnchor* Anchor = (bSuccess && MRUK) ? FindFloorAnchor() : nullptr)
	{
		RoomStatus = FString::Printf(TEXT("room scan loaded (%d room%s)"), MRUK->Rooms.Num(), MRUK->Rooms.Num() == 1 ? TEXT("") : TEXT("s"));
		SetFloor(EMRFloorSource::RoomScan, Anchor);
		return;
	}

	const bool bHasRooms = bSuccess && MRUK && MRUK->Rooms.Num() > 0;
	FString Reason = bHasRooms ? TEXT("room scan has no floor") : TEXT("no room scan on this headset");
	if (bLaunchSpaceSetupIfNoRoom && !bSpaceSetupLaunched && MRUK)
	{
		bSpaceSetupLaunched = true;
		MRUK->OnCaptureComplete.AddUniqueDynamic(this, &AMRSuitViewer::HandleSpaceSetupComplete);
		if (MRUK->LaunchSceneCapture())
		{
			Reason += TEXT(" - Space Setup opened");
		}
	}
	UseBoundaryFloor(Reason);
}

void AMRSuitViewer::HandleSpaceSetupComplete(bool bSuccess)
{
	if (UMRUKSubsystem* MRUK = GetMRUK())
	{
		MRUK->OnCaptureComplete.RemoveDynamic(this, &AMRSuitViewer::HandleSpaceSetupComplete);
	}
	if (bSuccess)
	{
		LoadRoom();
	}
}

void AMRSuitViewer::HandleRoomLoadTimeout()
{
	if (!IsFloorReady())
	{
		UseBoundaryFloor(TEXT("room scan timed out"));
	}
}

void AMRSuitViewer::UseBoundaryFloor(const FString& Reason)
{
	RoomStatus = Reason;
	SetFloor(EMRFloorSource::BoundaryFloor, nullptr);
}

void AMRSuitViewer::SetFloor(EMRFloorSource Source, AMRUKAnchor* Anchor)
{
	FloorSource = Source;
	FloorAnchor = Anchor;
	SessionState = EMRSessionState::Ready;

	const float FloorHeight = GetFloorHeight();
	UE_LOG(LogMRSuitViewer, Log, TEXT("Floor ready: %s, Z = %.1f cm (%s)"), *StaticEnum<EMRFloorSource>()->GetNameStringByValue(static_cast<int64>(Source)), FloorHeight, *RoomStatus);

	PlaceTestAids();
	if (PendingModels.Num() > 0)
	{
		const TArray<int32> Chosen = PendingModels; // chosen before the floor was known
		for (const int32 ModelIndex : Chosen)
		{
			ShowModel(ModelIndex);
		}
	}
	else if (bAutoPlaceSuit)
	{
		ShowModel(0);
	}
	UpdateStatusPanel();
	OnFloorReady.Broadcast(Source, FloorHeight);
}

AMRUKAnchor* AMRSuitViewer::FindFloorAnchor() const
{
	const UMRUKSubsystem* MRUK = GetMRUK();
	if (!MRUK)
	{
		return nullptr;
	}

	AMRUKRoom* Room = MRUK->GetCurrentRoom();
	for (int32 Index = 0; !Room && Index < MRUK->Rooms.Num(); ++Index)
	{
		Room = MRUK->Rooms[Index];
	}
	if (!Room)
	{
		return nullptr;
	}

	// A room can contain several floor planes; use the one the user is standing over (closest horizontally).
	const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const FVector Head = CameraManager ? CameraManager->GetCameraLocation() : GetActorLocation();
	AMRUKAnchor* Best = nullptr;
	double BestDistanceSq = TNumericLimits<double>::Max();
	for (AMRUKAnchor* Anchor : Room->FloorAnchors)
	{
		const double DistanceSq = Anchor ? FVector::DistSquaredXY(Anchor->GetActorLocation(), Head) : TNumericLimits<double>::Max();
		if (DistanceSq < BestDistanceSq)
		{
			Best = Anchor;
			BestDistanceSq = DistanceSq;
		}
	}
	return Best ? Best : Room->GetFloorAnchor();
}

UMRUKSubsystem* AMRSuitViewer::GetMRUK() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	return GameInstance ? GameInstance->GetSubsystem<UMRUKSubsystem>() : nullptr;
}

void AMRSuitViewer::ApplyTestAidMaterials()
{
	if (FloorGridMaterial)
	{
		FloorGrid->SetMaterial(0, FloorGridMaterial);
	}
	if (ScaleReferenceMaterial)
	{
		ScaleReference->SetMaterial(0, ScaleReferenceMaterial);
	}
}

void AMRSuitViewer::PlaceTestAids()
{
	const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const FVector Head = CameraManager ? CameraManager->GetCameraLocation() : GetActorLocation();
	const FRotator Facing(0.f, CameraManager ? CameraManager->GetCameraRotation().Yaw : GetActorRotation().Yaw, 0.f);
	const FVector Forward = Facing.Vector();
	const FVector Right = FRotationMatrix(Facing).GetUnitAxis(EAxis::Y);
	const float FloorHeight = GetFloorHeight();

	// Grid centred under the user, lying exactly on the detected floor.
	FloorGrid->SetWorldLocationAndRotation(FVector(Head.X, Head.Y, FloorHeight), FRotator::ZeroRotator);
	FloorGrid->SetHiddenInGame(!bShowFloorGrid);

	// 100 cm cube standing on the floor in front of the user.
	const FVector CubeBase = Head + Forward * ScaleReferenceDistance;
	ScaleReference->SetWorldLocationAndRotation(FVector(CubeBase.X, CubeBase.Y, FloorHeight + MRSuitViewer::BasicShapeSize * 0.5f), Facing);
	ScaleReference->SetHiddenInGame(!bShowScaleReference);

	// Status panel beside the cube at chest height, facing the user.
	const FVector PanelLocation = FVector(Head.X, Head.Y, Head.Z - 30.f) + Forward * (ScaleReferenceDistance - 20.f) - Right * 80.f;
	const FRotator PanelFacing(0.f, (Head - PanelLocation).Rotation().Yaw, 0.f);
	StatusPanel->SetWorldLocationAndRotation(PanelLocation, PanelFacing);
	StatusPanel->SetHiddenInGame(!bShowStatusPanel);
}

void AMRSuitViewer::UpdateStatusPanel()
{
	const TCHAR* Passthrough = !bEnablePassthrough ? TEXT("off")
		: bPassthroughVisible ? TEXT("ON")
		: bPassthroughRequested ? TEXT("starting...")
		: TEXT("unavailable");

	FString Floor = TEXT("detecting...");
	if (FloorSource == EMRFloorSource::RoomScan)
	{
		Floor = FString::Printf(TEXT("room scan, Z = %.1f cm"), GetFloorHeight());
	}
	else if (FloorSource == EMRFloorSource::BoundaryFloor)
	{
		Floor = FString::Printf(TEXT("boundary floor, Z = %.1f cm"), GetFloorHeight());
	}

	StatusPanel->SetText(FText::FromString(FString::Printf(
		TEXT("LIFE-SIZE SUIT  -  MR CHECK\nPassthrough: %s\nRoom: %s\nFloor: %s\nCube 100 cm tall  |  grid 25 cm"),
		Passthrough, *RoomStatus, *Floor)));
}

float AMRSuitViewer::FindClearDistance(const FVector& Head, const FVector& Forward, float FloorHeight) const
{
	float ClearDistance = SuitPlacementDistance;
	const UMRUKSubsystem* MRUK = GetMRUK();
	AMRUKRoom* Room = MRUK ? MRUK->GetCurrentRoom() : nullptr;
	if (Room)
	{
		for (float ProbeZ : MRSuitViewer::ClearanceProbeHeights)
		{
			const FVector Start(Head.X, Head.Y, FloorHeight + ProbeZ);
			const float MaxDist = SuitPlacementDistance + MRSuitViewer::SuitClearance;
			FMRUKHit Hit;
			if (Room->Raycast(Start, Forward, MaxDist, FMRUKLabelFilter(), Hit))
			{
				const float HitDist = FMath::Max(MRSuitViewer::MinSuitDistance, Hit.HitDistance - MRSuitViewer::SuitClearance);
				ClearDistance = FMath::Min(ClearDistance, HitDist);
			}
		}
	}
	return ClearDistance;
}

void AMRSuitViewer::PlaceInFront(AMRSuit* ModelSuit)
{
	const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const FVector Head = CameraManager ? CameraManager->GetCameraLocation() : GetActorLocation();
	const FRotator Facing(0.f, CameraManager ? CameraManager->GetCameraRotation().Yaw : GetActorRotation().Yaw, 0.f);
	const FVector Forward = Facing.Vector();
	const FVector Right = FRotationMatrix(Facing).GetUnitAxis(EAxis::Y);
	const float FloorHeight = GetFloorHeight();
	const float Distance = FindClearDistance(Head, Forward, FloorHeight);
	const FVector Ahead(Head.X + Forward.X * Distance, Head.Y + Forward.Y * Distance, FloorHeight);

	// Floor footprints of the other models in the room: this one stands beside them, not inside them.
	TArray<FBox2D> Taken;
	for (const TPair<int32, TObjectPtr<AMRSuit>>& Pair : ShownModels)
	{
		if (Pair.Value && Pair.Value != ModelSuit)
		{
			Taken.Add(Pair.Value->GetFootprint(Pair.Value->GetActorTransform()).ExpandBy(MRSuitViewer::ModelGap));
		}
	}

	// Straight ahead if that is free, else the nearest free spot to the right or left of it, facing the user.
	FTransform Placement(FRotator(0.f, Facing.Yaw + 180.f, 0.f), Ahead);
	for (int32 Step = 0; Step <= 2 * MRSuitViewer::MaxSideSteps; ++Step)
	{
		const float Offset = ((Step + 1) / 2) * MRSuitViewer::SideStep * ((Step % 2) ? 1.f : -1.f);
		const FVector Spot = Ahead + Right * Offset;
		const FTransform Candidate(FRotator(0.f, (Head - Spot).Rotation().Yaw, 0.f), Spot);
		const FBox2D Footprint = ModelSuit->GetFootprint(Candidate);
		const bool bFree = !Taken.ContainsByPredicate([&Footprint](const FBox2D& Other) { return Other.Intersect(Footprint); });
		if (bFree && (Step == 0 || IsSpotClear(Head, Spot, FloorHeight)))
		{
			Placement = Candidate;
			break;
		}
	}

	ModelSuit->SetActorLocationAndRotation(Placement.GetLocation(), Placement.GetRotation());
	ModelSuit->SetInitialPlacement(FTransform(Placement.GetRotation(), Placement.GetLocation(), FVector(1.f)));
}

bool AMRSuitViewer::IsSpotClear(const FVector& Head, const FVector& Spot, float FloorHeight) const
{
	const UMRUKSubsystem* MRUK = GetMRUK();
	AMRUKRoom* Room = MRUK ? MRUK->GetCurrentRoom() : nullptr;
	const FVector Flat(Spot.X - Head.X, Spot.Y - Head.Y, 0.f);
	const float Distance = Flat.Size();
	if (!Room || Distance < KINDA_SMALL_NUMBER)
	{
		return true;
	}
	for (const float ProbeZ : MRSuitViewer::ClearanceProbeHeights)
	{
		FMRUKHit Hit;
		if (Room->Raycast(FVector(Head.X, Head.Y, FloorHeight + ProbeZ), Flat / Distance, Distance + MRSuitViewer::SuitClearance, FMRUKLabelFilter(), Hit))
		{
			return false;
		}
	}
	return true;
}

int32 AMRSuitViewer::GetModelCount() const
{
	if (ModelCatalog)
	{
		return ModelCatalog->Models.Num();
	}
	return SuitClass ? 1 : 0;
}

FText AMRSuitViewer::GetModelName(int32 ModelIndex) const
{
	if (ModelCatalog)
	{
		return ModelCatalog->Models.IsValidIndex(ModelIndex) ? ModelCatalog->Models[ModelIndex].DisplayName : FText::GetEmpty();
	}
	return (ModelIndex == 0 && SuitClass) ? FText::FromString(SuitClass->GetName().LeftChop(2)) : FText::GetEmpty(); // drop "_C"
}

FText AMRSuitViewer::GetModelDescription(int32 ModelIndex) const
{
	if (ModelCatalog && ModelCatalog->Models.IsValidIndex(ModelIndex))
	{
		return ModelCatalog->Models[ModelIndex].Description;
	}
	return FText::GetEmpty();
}

bool AMRSuitViewer::ResolveModel(int32 ModelIndex, TSubclassOf<AMRSuit>& OutClass, UMRSuitConfiguration*& OutConfiguration) const
{
	if (ModelCatalog)
	{
		if (!ModelCatalog->Models.IsValidIndex(ModelIndex))
		{
			return false;
		}
		const FMRModelEntry& Entry = ModelCatalog->Models[ModelIndex];
		OutClass = Entry.ActorClass.LoadSynchronous();
		OutConfiguration = Entry.Configuration.LoadSynchronous();
		return OutClass != nullptr;
	}

	// No catalog: the viewer's own suit is the only model.
	OutClass = SuitClass;
	OutConfiguration = SuitConfiguration;
	return ModelIndex == 0 && OutClass != nullptr;
}

bool AMRSuitViewer::IsModelShown(int32 ModelIndex) const
{
	const TObjectPtr<AMRSuit>* Found = ShownModels.Find(ModelIndex);
	return Found && *Found;
}

TArray<AMRSuit*> AMRSuitViewer::GetShownSuits() const
{
	TArray<AMRSuit*> Suits;
	for (const TPair<int32, TObjectPtr<AMRSuit>>& Pair : ShownModels)
	{
		if (Pair.Value)
		{
			Suits.Add(Pair.Value);
		}
	}
	return Suits;
}

AMRSuit* AMRSuitViewer::GetSuit() const
{
	const TObjectPtr<AMRSuit>* Found = ShownModels.Find(ActiveModelIndex);
	return Found ? Found->Get() : nullptr;
}

bool AMRSuitViewer::ShowModel(int32 ModelIndex)
{
	TSubclassOf<AMRSuit> ModelClass;
	UMRSuitConfiguration* ModelConfiguration = nullptr;
	if (!ResolveModel(ModelIndex, ModelClass, ModelConfiguration))
	{
		UE_LOG(LogMRSuitViewer, Warning, TEXT("Model %d is not in the model catalog, or its actor class could not be loaded."), ModelIndex);
		return false;
	}

	if (!IsFloorReady())
	{
		// Wait for the floor so the model's feet land on it; SetFloor shows it.
		PendingModels.AddUnique(ModelIndex);
		return true;
	}
	PendingModels.Remove(ModelIndex);

	AMRSuit* ModelSuit = IsModelShown(ModelIndex) ? ShownModels[ModelIndex].Get() : nullptr;
	if (!ModelSuit)
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ModelSuit = GetWorld()->SpawnActor<AMRSuit>(ModelClass, FVector(0.f, 0.f, GetFloorHeight()), FRotator::ZeroRotator, SpawnParams);
		if (!ModelSuit)
		{
			return false;
		}
		// An entry without a configuration uses its actor class's own, not another model's.
		if (ModelConfiguration)
		{
			ModelSuit->Configuration = ModelConfiguration;
			ModelSuit->ApplyConfiguration();
		}
		ShownModels.Add(ModelIndex, ModelSuit);
	}
	PlaceInFront(ModelSuit); // a new model beside the others, or one already in the room back in front of the user

	UE_LOG(LogMRSuitViewer, Log, TEXT("Showing model %d (%s), %d in the room."), ModelIndex, *GetModelName(ModelIndex).ToString(), ShownModels.Num());
	SetActiveModel(ModelIndex);
	return true;
}

void AMRSuitViewer::HideModel(int32 ModelIndex)
{
	PendingModels.Remove(ModelIndex);
	TObjectPtr<AMRSuit> Removed;
	if (ShownModels.RemoveAndCopyValue(ModelIndex, Removed) && Removed)
	{
		Removed->Destroy();
		UE_LOG(LogMRSuitViewer, Log, TEXT("Removed model %d (%s), %d in the room."), ModelIndex, *GetModelName(ModelIndex).ToString(), ShownModels.Num());
	}
	if (ModelIndex == ActiveModelIndex)
	{
		int32 Remaining = INDEX_NONE;
		for (const TPair<int32, TObjectPtr<AMRSuit>>& Pair : ShownModels)
		{
			Remaining = Pair.Key;
		}
		SetActiveModel(Remaining);
	}
}

bool AMRSuitViewer::ToggleModel(int32 ModelIndex)
{
	if (IsModelShown(ModelIndex) || PendingModels.Contains(ModelIndex))
	{
		HideModel(ModelIndex);
		return false;
	}
	return ShowModel(ModelIndex);
}

void AMRSuitViewer::ClearModel()
{
	for (const TPair<int32, TObjectPtr<AMRSuit>>& Pair : ShownModels)
	{
		if (Pair.Value)
		{
			Pair.Value->Destroy();
		}
	}
	ShownModels.Reset();
	PendingModels.Reset();
	SetActiveModel(INDEX_NONE);
}

void AMRSuitViewer::SetActiveModel(int32 ModelIndex)
{
	if (ModelIndex != ActiveModelIndex)
	{
		ActiveModelIndex = ModelIndex;
		OnActiveModelChanged.Broadcast(ActiveModelIndex);
	}
}

