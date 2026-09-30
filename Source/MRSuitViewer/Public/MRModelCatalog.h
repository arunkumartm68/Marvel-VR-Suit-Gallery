#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MRModelCatalog.generated.h"

class AMRSuit;
class UMRSuitConfiguration;

/** One model the user can choose from the hand menu. */
USTRUCT(BlueprintType)
struct FMRModelEntry
{
	GENERATED_BODY()

	/** Name on the menu button. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FText DisplayName;

	/** Short second line on the menu button, e.g. the model's real-world height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	FText Description;

	/** Actor to put in the room (a Blueprint subclass of AMRSuit). Loaded only when the model is chosen. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	TSoftClassPtr<AMRSuit> ActorClass;

	/** Calibration for this model. When empty, the actor class's own configuration is used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Model")
	TSoftObjectPtr<UMRSuitConfiguration> Configuration;
};

/**
 * The models offered in the hand menu (DA_ModelCatalog). Adding a model is adding an entry here; no code changes.
 * Entries are soft references, so a model is only loaded into memory once the user chooses it.
 */
UCLASS(BlueprintType)
class MRSUITVIEWER_API UMRModelCatalog : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Models", meta = (TitleProperty = "DisplayName"))
	TArray<FMRModelEntry> Models;
};
