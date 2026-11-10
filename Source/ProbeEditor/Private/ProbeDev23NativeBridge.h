#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"
#include "HAL/CriticalSection.h"
#include "ProbeTestRunner.h"

// Captures authoritative AutomationController Test Started/Completed records.
// Does not synthesize a pass from command acceptance, and never suppresses logs.
namespace ProbeDev23
{
struct FObservedRun
{
    FString Path;
    FString Outcome;
    TArray<FString> Events;
    double DurationMs = 0;
    bool bStarted = false;
    bool bCompleted = false;
};

PROBEEDITOR_API bool ParseStarted(const FString& Line, FString& Path);
PROBEEDITOR_API bool ParseCompleted(const FString& Line, FString& Path, FString& Outcome);
PROBEEDITOR_API Probe::EResultStatus StatusFromOutcome(const FString& Outcome);

class FNativeLogBridge final : public FOutputDevice
{
public:
    ~FNativeLogBridge() override { Stop(); }
    bool Start(const FString& FullPath);
    void Stop();
    bool Read(FObservedRun& Out) const;
    void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;
private:
    mutable FCriticalSection Guard;
    FObservedRun Current;
    bool bListening = false;
    double StartSeconds = 0;
    double TestStartSeconds = 0;
};
}
