#pragma once

#include "CoreMinimal.h"
#include "ProbeTestAsset.h"
#include "ProbeTestPack.h"

namespace Probe
{
    enum class EResultStatus : uint8
    {
        Passed,
        Failed,
        Skipped,
        Error
    };

    // Inclusive zero-based range. The default runs every step. A scoped run
    // executes only its own range; it does not simulate preceding fixture setup.
    struct FRunOptions
    {
        int32 FirstStepIndex = 0;
        int32 LastStepIndex = MAX_int32;
        // INDEX_NONE means the original, unparameterized Test Asset.
        int32 VariationIndex = INDEX_NONE;
    };

    struct FStepResult
    {
        int32 Index = 0;
        EResultStatus Status = EResultStatus::Error;
        FString Description;
        FString GroupName;
        FString AssertionId;
        FString Detail;
        double DurationMs = 0.0;
    };

    struct FTestResult
    {
        FString Name;
        FString Id;
        FString AssetPath;
        int32 VariationIndex = INDEX_NONE;
        FString VariationName;
        EResultStatus Status = EResultStatus::Error;
        FString Detail;
        double DurationMs = 0.0;
        int32 Attempts = 1; // Dev 10: original execution plus any safe retry.
        TArray<FStepResult> Steps;
    };

    PROBEEDITOR_API const TCHAR* StatusName(EResultStatus Status);
    PROBEEDITOR_API FTestResult RunTest(const UProbeTestAsset* Asset, const FRunOptions& Options = FRunOptions());
    // Completion always runs on the Editor game thread. Required for native
    // UI steps; synchronous RunTest explicitly refuses live UI input.
    PROBEEDITOR_API void RunTestAsync(const UProbeTestAsset* Asset,
        TFunction<void(FTestResult)> OnComplete, const FRunOptions& Options = FRunOptions());
    PROBEEDITOR_API TArray<FTestResult> RunPack(const UProbeTestPack* Pack);
}
