#pragma once

#include "CoreMinimal.h"
#include "IAutomationControllerManager.h"

// Single-test, local-session controller dispatch. No implicit console fallback:
// failures to discover workers/select an exact test must remain visible.
namespace ProbeDev26
{
enum class EPhase : uint8 { Idle, DiscoveringWorkers, DiscoveringTests, Running, Complete, Error, Interrupted };
struct FDecision
{
    static bool ExactSelection(const TArray<FString>& Enabled, const FString& Path);
    static bool CanStart(EAutomationControllerModuleState::Type State);
    static bool IsTerminal(EPhase Phase);
};
class FNativeExecution
{
public:
    ~FNativeExecution();
    bool Start(const FString& FullPath, FString& OutError);
    void Tick();
    void Stop();
    void Reset();
    EPhase Phase() const { return CurrentPhase; }
    bool OwnsRun() const { return bOwnsRun; }
    bool CompletionNotified() const { return bTestsComplete; }
    const FString& Error() const { return LastError; }
    FString PhaseLabel() const;
private:
    void Fail(const FString& Reason);
    void OnTestsComplete();
    IAutomationControllerManagerPtr Controller;
    FDelegateHandle CompletionHandle;
    FString Path;
    FString LastError;
    EPhase CurrentPhase = EPhase::Idle;
    bool bOwnsRun = false;
    bool bTestsComplete = false;
    bool bRequestedTests = false;
    bool bChangedSelection = false;
    int32 PreviousPasses = 1;
    TArray<FString> PreviousEnabled;
    double PhaseStart = 0.0;
};
}
