#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ProbeTestPack.generated.h"

class UProbeTestAsset;

// Default preserves the behavior of every existing Probe Pack.
UENUM(BlueprintType)
enum class EProbePackFailurePolicy : uint8
{
    Continue UMETA(DisplayName="Continue After Failure"),
    StopOnFailure UMETA(DisplayName="Stop After Failed Test")
};

// An explicit, order-constrained dependency within the same Pack. No implicit
// asset loading or external pack execution is authorized by a prerequisite.
USTRUCT(BlueprintType)
struct FProbePackPrerequisite
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="Pack Dependency")
    TSoftObjectPtr<UProbeTestAsset> Test;

    UPROPERTY(EditAnywhere, Category="Pack Dependency")
    TSoftObjectPtr<UProbeTestAsset> Requires;
};

/** An optional group of Probe Test assets; no copy of assertion data is stored. */
UCLASS(BlueprintType)
class PROBEEDITOR_API UProbeTestPack : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Probe Pack")
    FString Description;

    UPROPERTY(EditAnywhere, Category="Probe Pack")
    TArray<TSoftObjectPtr<UProbeTestAsset>> Tests;

    UPROPERTY(EditAnywhere, Category="Probe Pack|Execution",
        meta=(ToolTip="Continue (default) or skip remaining tests after the first failure/error. The current test is never interrupted."))
    EProbePackFailurePolicy FailurePolicy = EProbePackFailurePolicy::Continue;

    UPROPERTY(EditAnywhere, Category="Probe Pack|Execution",
        meta=(ToolTip="Retry a failed test once only when every enabled assertion is demonstrably read-only. Never repeats actions, external providers, or live UI input."))
    bool bRetryReadOnlyFailuresOnce = false;

    UPROPERTY(EditAnywhere, Category="Probe Pack|Execution",
        meta=(ToolTip="Dependent Test must appear later in the Pack than Requires. All variations of Requires must pass before the dependent runs."))
    TArray<FProbePackPrerequisite> Prerequisites;
};
