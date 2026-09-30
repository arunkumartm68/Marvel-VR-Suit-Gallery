#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MRSuitViewer.generated.h"

class AMRSuit;
class AMRUKAnchor;
class UMaterialInterface;
class UMRModelCatalog;
class UMRSuitConfiguration;
class UMRUKSubsystem;
class UStaticMeshComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EMRSessionState : uint8
{
	Starting,
	WaitingForScenePermission,
	LoadingRoom,
	Ready,
};

UENUM(BlueprintType)
enum class EMRFloorSource : uint8
{
	/** Floor not known yet. */
	None,
	/** FLOOR anchor from the headset's room scan (Space Setup), loaded through MR Utility Kit. */
	RoomScan,
	/** Z = 0 of the Stage tracking space, i.e. the floor calibrated with the Quest boundary. */
	BoundaryFloor,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMRFloorReadySignature, EMRFloorSource, Source, float, FloorHeight);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMRActiveModelChangedSignature, int32, ModelIndex);

/**
 * Mixed-reality session for the life-size suit viewer.
 *
 * Place one in the level. On BeginPlay it:
 *   1. starts the passthrough underlay so the real room is visible,
 *   2. requests the Horizon OS spatial-data permission and loads the room scan with MR Utility Kit,
 *   3. resolves the real floor height (room-scan floor, falling back to the boundary floor),
 *   4. stands the suit on that floor in front of the user, facing them,
 *   5. optionally shows developer test aids: a floor grid, a 100 cm reference cube and a status panel.
 */
UCLASS()
class MRSUITVIEWER_API AMRSuitViewer : public AActor
{
	GENERATED_BODY()

public:
	AMRSuitViewer();

	/** Show the real room through the Quest cameras behind all virtual content. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MR|Passthrough")
	bool bEnablePassthrough = true;

	/** Load the headset's room scan to find the real floor. When off (or unavailable), the boundary floor is used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MR|Room")
	bool bLoadRoomScan = true;

	/** When the headset has no room scan, open Space Setup so the user can create one. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MR|Room")
	bool bLaunchSpaceSetupIfNoRoom = false;

	/** Seconds to wait for the room scan before using the boundary floor. A late room scan still replaces it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MR|Room", meta = (ClampMin = "1.0", Units = "s"))
	float RoomLoadTimeout = 8.f;

	/** Suit actor to spawn (BP_MRSuit). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MR|Suit")
	TSubclassOf<AMRSuit> SuitClass;

	/** Suit calibration (DA_SuitConfiguration). When empty, the suit class's own configuration is used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MR|Suit")
	TObjectPtr<UMRSuitConfiguration> SuitConfiguration;

	/** Models offered in the hand menu (DA_ModelCatalog). When empty, SuitClass / SuitConfiguration is the only model. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MR|Models")
	TObjectPtr<UMRModelCatalog> ModelCatalog;

	/** Fired when a model is put in the room (its catalog index) or the room is cleared (INDEX_NONE). */
	UPROPERTY(BlueprintAssignable, Category = "MR|Models")
	FMRActiveModelChangedSignature OnActiveModelChanged;

	UFUNCTION(BlueprintPure, Category = "MR|Models")
	int32 GetModelCount() const;

	UFUNCTION(BlueprintPure, Category = "MR|Models")
	FText GetModelName(int32 ModelIndex) const;

	UFUNCTION(BlueprintPure, Category = "MR|Models")
	FText GetModelDescription(int32 ModelIndex) const;

	/** Catalog index of the model in the room, or INDEX_NONE when the room is empty. */
	UFUNCTION(BlueprintPure, Category = "MR|Models")
	int32 GetActiveModelIndex() const { return ActiveModelIndex; }

	/**
	 * Puts catalog model ModelIndex in the room, standing on the floor in front of the user, replacing any other model.
	 * Choosing the model already in the room brings it back in front of the user at life size.
	 * Before the floor is known the choice is remembered and applied once it is.
	 */
	UFUNCTION(BlueprintCallable, Category = "MR|Models")
	bool ShowModel(int32 ModelIndex);

	/** Removes the model so the room is empty. */
	UFUNCTION(BlueprintCallable, Category = "MR|Models")
	void ClearModel();

	/** Stand the suit on the floor in front of the user as soon as the floor is known. Set to false for clean empty start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MR|Suit")
	bool bAutoPlaceSuit = false;

	/** Preferred distance from the user to the suit; shortened when the room scan shows a wall or furniture closer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MR|Suit", meta = (ClampMin = "80.0", Units = "cm"))
	float SuitPlacementDistance = 200.f;

	/** Developer aid: a grid drawn on the detected floor. It should look painted onto the real floor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MR|Test Aids")
	bool bShowFloorGrid = false;

	/** Developer aid: a 100 cm cube standing on the detected floor, to verify 1:1 scale with a tape measure. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MR|Test Aids")
	bool bShowScaleReference = false;

	/** Developer aid: a floating panel describing passthrough / room / floor status. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MR|Test Aids")
	bool bShowStatusPanel = false;

	/** Horizontal distance in front of the user where the reference cube is placed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MR|Test Aids", meta = (ClampMin = "50.0", Units = "cm"))
	float ScaleReferenceDistance = 150.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MR|Test Aids")
	TObjectPtr<UMaterialInterface> FloorGridMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MR|Test Aids")
	TObjectPtr<UMaterialInterface> ScaleReferenceMaterial;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "MR|State")
	EMRSessionState SessionState = EMRSessionState::Starting;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "MR|State")
	EMRFloorSource FloorSource = EMRFloorSource::None;

	/** Fired whenever the floor becomes known or changes source (e.g. a room scan arrives after the fallback). */
	UPROPERTY(BlueprintAssignable, Category = "MR")
	FMRFloorReadySignature OnFloorReady;

	/** World-space Z (cm) of the real floor. Valid once IsFloorReady() returns true. */
	UFUNCTION(BlueprintPure, Category = "MR")
	float GetFloorHeight() const;

	UFUNCTION(BlueprintPure, Category = "MR")
	bool IsFloorReady() const { return FloorSource != EMRFloorSource::None; }

	UFUNCTION(BlueprintPure, Category = "MR")
	bool IsPassthroughVisible() const { return bPassthroughVisible; }

	UFUNCTION(BlueprintPure, Category = "MR|Suit")
	AMRSuit* GetSuit() const { return Suit; }

	UFUNCTION(BlueprintPure, Category = "MR|Suit")
	bool IsSuitSpawned() const { return Suit != nullptr; }

	/** Spawns the suit in front of the user at the real floor height. */
	UFUNCTION(BlueprintCallable, Category = "MR|Suit")
	void SpawnSuit(TSubclassOf<AMRSuit> InSuitClass = nullptr, UMRSuitConfiguration* InConfig = nullptr);

	/** Removes the suit from the room so the room is empty. */
	UFUNCTION(BlueprintCallable, Category = "MR|Suit")
	void DespawnSuit();

	/** Discards the loaded room data and detects the floor again. */
	UFUNCTION(BlueprintCallable, Category = "MR")
	void RefreshRoom();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(VisibleAnywhere, Category = "MR|Test Aids")
	TObjectPtr<UStaticMeshComponent> FloorGrid;

	UPROPERTY(VisibleAnywhere, Category = "MR|Test Aids")
	TObjectPtr<UStaticMeshComponent> ScaleReference;

	UPROPERTY(VisibleAnywhere, Category = "MR|Test Aids")
	TObjectPtr<UTextRenderComponent> StatusPanel;

private:
	void StartPassthrough();
	void StartRoomDetection();
	void LoadRoom();
	void UseBoundaryFloor(const FString& Reason);
	void SetFloor(EMRFloorSource Source, AMRUKAnchor* Anchor);
	AMRUKAnchor* FindFloorAnchor() const;
	UMRUKSubsystem* GetMRUK() const;
	void ApplyTestAidMaterials();
	void PlaceTestAids();
	void PlaceSuit();
	bool ResolveModel(int32 ModelIndex, TSubclassOf<AMRSuit>& OutClass, UMRSuitConfiguration*& OutConfiguration) const;
	void SetActiveModel(int32 ModelIndex);
	float FindClearDistance(const FVector& Head, const FVector& Forward, float FloorHeight) const;
	void UpdateStatusPanel();

	UFUNCTION()
	void HandlePassthroughResumed();

	UFUNCTION()
	void HandleScenePermission(const TArray<FString>& Permissions, const TArray<bool>& GrantResults);

	UFUNCTION()
	void HandleSceneLoaded(bool bSuccess);

	UFUNCTION()
	void HandleSpaceSetupComplete(bool bSuccess);

	void HandleRoomLoadTimeout();

	UPROPERTY(Transient)
	TObjectPtr<AMRSuit> Suit;

	int32 ActiveModelIndex = INDEX_NONE;
	int32 PendingModelIndex = INDEX_NONE;

	TWeakObjectPtr<AMRUKAnchor> FloorAnchor;
	FTimerHandle RoomLoadTimeoutHandle;
	FString RoomStatus;
	bool bPassthroughRequested = false;
	bool bPassthroughVisible = false;
	bool bSpaceSetupLaunched = false;
};
