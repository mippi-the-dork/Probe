#pragma once

#include "CoreMinimal.h"
#include "ProbeTestAsset.h"

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
}
