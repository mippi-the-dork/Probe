#pragma once

#include "CoreMinimal.h"
#include "ProbeTestRunner.h"

// Read-only snapshot of Unreal's controller-owned test report. Never initiates,
// resets, or changes the test selection managed by AutomationController.
namespace ProbeDev25
{
struct FStructuredResult
{
    FString Path;
    FString State;
    FString Device;
    FString GameInstance;
    double DurationMs = 0.0;
    int32 Warnings = 0;
    int32 Errors = 0;
    TArray<FString> Events;
    bool bAvailable = false;
};

// The caller must first observe a completion record for this exact run/path.
// Without that gate, cached controller reports from earlier runs are unsafe.
PROBEEDITOR_API bool TryReadCompletedReport(const FString& FullPath, FStructuredResult& Out);
PROBEEDITOR_API bool IsTerminalState(const FString& State);
PROBEEDITOR_API Probe::EResultStatus ConvertState(const FString& State);
}
