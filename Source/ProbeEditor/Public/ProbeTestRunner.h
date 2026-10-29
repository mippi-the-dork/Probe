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

    struct FStepResult
    {
        int32 Index = 0;
        EResultStatus Status = EResultStatus::Error;
        FString Description;
        FString AssertionId;
        FString Detail;
        double DurationMs = 0.0;
    };

    struct FTestResult
    {
        FString Name;
        FString Id;
        FString AssetPath;
        EResultStatus Status = EResultStatus::Error;
        FString Detail;
        double DurationMs = 0.0;
        TArray<FStepResult> Steps;
    };

    PROBEEDITOR_API const TCHAR* StatusName(EResultStatus Status);
    PROBEEDITOR_API FTestResult RunTest(const UProbeTestAsset* Asset);
    // Completion always runs on the Editor game thread. Required for native
    // UI steps; synchronous RunTest explicitly refuses live UI input.
    PROBEEDITOR_API void RunTestAsync(const UProbeTestAsset* Asset,
        TFunction<void(FTestResult)> OnComplete);
    PROBEEDITOR_API TArray<FTestResult> RunPack(const UProbeTestPack* Pack);
}
