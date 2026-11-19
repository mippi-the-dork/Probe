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

// Dev 27: alternate ordered Pack membership for mixed-source execution. Existing
// authored-only Packs continue to use Tests until explicitly converted in the UI.
UENUM(BlueprintType)
enum class EProbePackMixedSource : uint8
{
    AuthoredTest UMETA(DisplayName="Authored Probe Test"),
    PortableTest UMETA(DisplayName="Portable JSON Test"),
    PortablePack UMETA(DisplayName="Portable JSON Pack"),
    NativeAutomation UMETA(DisplayName="Unreal Automation Test")
};

USTRUCT(BlueprintType)
struct FProbePackMixedEntry
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category="Mixed Pack")
    EProbePackMixedSource Source = EProbePackMixedSource::AuthoredTest;

    // Object path for authored assets, provider ID for portable definitions,
    // or exact Automation Framework full path. No automatic authorization.
    UPROPERTY(EditAnywhere, Category="Mixed Pack")
    FString Locator;

    // Dev 28: explicit typed identities of preceding mixed members. Every
    // prerequisite must pass; ordering is never silently changed at runtime.
    // Example identity: "3:Probe.Editor.Dev23NativeCompletionParsing".
    UPROPERTY(EditAnywhere, Category="Mixed Pack|Dependencies")
    TArray<FString> Requires;
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

    // When populated, MixedEntries is the authoritative ordered Pack membership.
    // Legacy Tests is not executed in mixed mode. Conversion happens explicitly.
    UPROPERTY(EditAnywhere, Category="Probe Pack|Mixed Sources")
    TArray<FProbePackMixedEntry> MixedEntries;

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
