#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "MRWristMenuComponent.generated.h"

class AMRSuitViewer;
class AMRViewerPawn;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMRModelSelectedSignature, int32, ModelIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnMRMenuActionSignature);

/** One button of the hand menu, built at runtime. */
USTRUCT()
struct FMRMenuButton
{
	GENERATED_BODY()

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Plate;

	UPROPERTY(Transient)
	TObjectPtr<UTextRenderComponent> Label;

	UPROPERTY(Transient)
	TObjectPtr<UTextRenderComponent> Detail;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> Material;

	/** Catalog index of the model this button shows, or one of the menu's action codes. */
	int32 Action = 0;

	/** Centre and half size on the menu face, in cm: X = menu Y axis, Y = menu Z axis. */
	FVector2D Center = FVector2D::ZeroVector;
	FVector2D HalfSize = FVector2D::ZeroVector;

	FText Description;
	FLinearColor ShownColor = FLinearColor::Transparent;

	/** Model buttons: whether the second line currently says the model is in the room. */
	bool bShowsInRoom = false;
	bool bDetailInitialised = false;
};

/**
 * Model menu that floats beside the left hand, facing the user.
 *
 * Hand tracking: turn the left palm towards your face to show it; tap a button with the right index fingertip.
 * Controllers: the left menu button shows or hides it; point the right controller at a button and pull the trigger.
 * There is one button per model in the viewer's model catalog: tap it to put that model in the room (beside any others
 * already there), tap it again to take it out. Clear empties the room; Reset puts every model back where it was placed,
 * at life size.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MRSUITVIEWER_API UMRWristMenuComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMRWristMenuComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** A model button was pressed (its catalog index). */
	UPROPERTY(BlueprintAssignable, Category = "MR|Menu")
	FOnMRModelSelectedSignature OnModelSelected;

	/** The Clear button was pressed. */
	UPROPERTY(BlueprintAssignable, Category = "MR|Menu")
	FOnMRMenuActionSignature OnClearRoom;

	/** The Reset button was pressed. */
	UPROPERTY(BlueprintAssignable, Category = "MR|Menu")
	FOnMRMenuActionSignature OnResetPose;

	UFUNCTION(BlueprintPure, Category = "MR|Menu")
	bool IsMenuVisible() const { return bMenuVisible; }

protected:
	virtual void BeginPlay() override;

	/** Unlit material with a "Color" vector parameter for the panel and buttons (M_MR_MenuPlate). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MR|Menu")
	TObjectPtr<UMaterialInterface> PlateMaterial;

private:
	AMRSuitViewer* GetViewer() const;
	void BuildMenu(const AMRSuitViewer& Viewer);
	FMRMenuButton& AddButton(int32 Action, const FText& Label, const FText& Description, const FVector2D& Center, const FVector2D& HalfSize, const FColor& LabelColor);
	UTextRenderComponent* AddText(const FText& Text, const FVector& Location, float Size, const FColor& Color);
	UStaticMeshComponent* AddPlate(const FVector& Location, const FVector& Size);

	/** Shows the menu while the left palm faces the user (or the controller menu toggle is on) and moves it beside the hand. */
	void UpdatePlacement(float DeltaTime);
	void SetMenuVisible(bool bShow, bool bForce = false);

	/** Right index fingertip taps (hands) or right controller ray + trigger (controllers). */
	void UpdatePress(float DeltaTime);
	int32 FindButtonAt(const FVector& LocalPoint, float Tolerance) const;
	void Press(int32 ButtonIndex);

	void UpdateVisuals();

	UPROPERTY()
	TObjectPtr<UStaticMesh> PlateMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> FallbackMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Backplate;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BackplateMaterial;

	UPROPERTY(Transient)
	TArray<FMRMenuButton> Buttons;

	TWeakObjectPtr<AMRViewerPawn> OwningPawn;

	bool bMenuBuilt = false;
	bool bMenuVisible = false;
	bool bSnapNextPlacement = true;
	bool bControllerToggle = false;
	bool bPrevMenuButton = false;
	bool bPrevTrigger = false;

	/** The fingertip was in front of the menu, so the next touch counts as a press (not a finger arriving from behind). */
	bool bTapArmed = false;
	float PressCooldown = 0.f;
	int32 HoveredButton = INDEX_NONE;
	int32 FlashButton = INDEX_NONE;
	float FlashTime = 0.f;
	float MenuHalfHeight = 0.f;
};
