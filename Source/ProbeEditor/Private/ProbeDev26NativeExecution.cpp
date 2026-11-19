#include "ProbeDev26NativeExecution.h"
#include "IAutomationControllerModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/App.h"
#include "HAL/PlatformTime.h"

namespace ProbeDev26
{
bool FDecision::ExactSelection(const TArray<FString>& Enabled, const FString& Path)
{
    return !Path.IsEmpty() && Enabled.Num() == 1 && Enabled[0].Equals(Path, ESearchCase::CaseSensitive);
}
bool FDecision::CanStart(EAutomationControllerModuleState::Type State)
{
    return State != EAutomationControllerModuleState::Running;
}
bool FDecision::ShouldProbeSelection(bool bRequested, bool bRefreshed, double SecondsSinceAttempt)
{
    // The UI-filtered report view is not discovery authority.  A controller
    // notification can prompt an immediate attempt, but also probe periodically
    // because some engine paths do not broadcast a refresh for test discovery.
    return bRequested && (bRefreshed || SecondsSinceAttempt >= 0.5);
}
bool FDecision::IsTerminal(EPhase Phase)
{
    return Phase == EPhase::Complete || Phase == EPhase::Interrupted || Phase == EPhase::Error;
}
FNativeExecution::~FNativeExecution() { Reset(); }
void FNativeExecution::OnTestsComplete() { bTestsComplete = true; }
void FNativeExecution::OnTestsRefreshed() { bTestRefreshObserved = true; }
void FNativeExecution::Fail(const FString& Reason)
{
    LastError = Reason;
    CurrentPhase = EPhase::Error;
}
void FNativeExecution::Reset()
{
    if (Controller.IsValid() && CompletionHandle.IsValid())
    {
        Controller->OnTestsComplete().Remove(CompletionHandle);
    }
    CompletionHandle.Reset();
    if (Controller.IsValid() && RefreshHandle.IsValid())
    {
        Controller->OnTestsRefreshed().Remove(RefreshHandle);
    }
    RefreshHandle.Reset();
    // Preserve Session Frontend's previous selection once our own run is finished.
    // Do not change selection while a worker remains active (including shutdown).
    if (Controller.IsValid() && bChangedSelection && Controller->GetTestState() != EAutomationControllerModuleState::Running)
    {
        Controller->SetEnabledTests(PreviousEnabled);
        Controller->SetNumPasses(PreviousPasses);
    }
    Controller.Reset();
    Path.Empty();
    LastError.Empty();
    CurrentPhase = EPhase::Idle;
    bOwnsRun = false;
    bTestsComplete = false;
    bRequestedTests = false;
    bTestRefreshObserved = false;
    SelectionAttempts = 0;
    LastSelectionAttempt = 0.0;
    bChangedSelection = false;
    PreviousEnabled.Reset();
    PreviousPasses = 1;
    PhaseStart = 0.0;
}
bool FNativeExecution::Start(const FString& FullPath, FString& OutError)
{
    Reset();
    if (!IsInGameThread() || FullPath.IsEmpty())
    {
        OutError = TEXT("Native controller dispatch requires the game thread and a non-empty test path.");
        return false;
    }
    IAutomationControllerModule* Module = FModuleManager::GetModulePtr<IAutomationControllerModule>(TEXT("AutomationController"));
    if (!Module)
    {
        OutError = TEXT("Unreal AutomationController module is unavailable.");
        return false;
    }
    Controller = Module->GetAutomationController();
    if (!Controller.IsValid() || !FDecision::CanStart(Controller->GetTestState()))
    {
        OutError = TEXT("Unreal AutomationController is already running tests or is unavailable; Probe will not take over its selection.");
        Reset();
        return false;
    }
    Path = FullPath;
    PhaseStart = FPlatformTime::Seconds();
    CompletionHandle = Controller->OnTestsComplete().AddRaw(this, &FNativeExecution::OnTestsComplete);
    RefreshHandle = Controller->OnTestsRefreshed().AddRaw(this, &FNativeExecution::OnTestsRefreshed);
    if (Controller->GetNumDeviceClusters() > 0 && Controller->IsReadyForTests())
    {
        Controller->RequestTests();
        LastSelectionAttempt = PhaseStart;
        CurrentPhase = EPhase::DiscoveringTests;
        bRequestedTests = true;
    }
    else
    {
        Controller->RequestAvailableWorkers(FApp::GetSessionId());
        CurrentPhase = EPhase::DiscoveringWorkers;
    }
    return true;
}
void FNativeExecution::Tick()
{
    if (!Controller.IsValid() || FDecision::IsTerminal(CurrentPhase) || CurrentPhase == EPhase::Idle) { return; }
    const double Now = FPlatformTime::Seconds();
    if (CurrentPhase == EPhase::DiscoveringWorkers)
    {
        if (Controller->GetNumDeviceClusters() > 0 && Controller->IsReadyForTests())
        {
            Controller->RequestTests();
            bRequestedTests = true;
            PhaseStart = Now;
            LastSelectionAttempt = Now;
            CurrentPhase = EPhase::DiscoveringTests;
        }
        else if (Now - PhaseStart > 30.0)
        { Fail(TEXT("No local Automation worker became available within 30 seconds. Check Session Frontend and editor worker availability.")); }
        return;
    }
    if (CurrentPhase == EPhase::DiscoveringTests)
    {
        if (!FDecision::CanStart(Controller->GetTestState()))
        { Fail(TEXT("Another AutomationController run started during test discovery; Probe did not take over.")); return; }
        // Never gate discovery on GetFilteredReports/GetFilteredTestNames: both
        // are Session Frontend presentation views and can remain empty with a
        // populated controller test tree.  Selection is the authoritative test.
        // The refresh delegate accelerates polling, but selection also gets a
        // bounded retry cadence for controller versions without that callback.
        if (FDecision::ShouldProbeSelection(bRequestedTests, bTestRefreshObserved,
                                           Now - LastSelectionAttempt))
        {
            bTestRefreshObserved = false;
            LastSelectionAttempt = Now;
            ++SelectionAttempts;
            if (!bChangedSelection)
            {
                Controller->GetEnabledTestNames(PreviousEnabled);
                PreviousPasses = Controller->GetNumPasses();
                bChangedSelection = true;
            }
            Controller->SetEnabledTests({Path});
            TArray<FString> Enabled;
            Controller->GetEnabledTestNames(Enabled);
            if (FDecision::ExactSelection(Enabled, Path))
            {
                Controller->SetNumPasses(1);
                bOwnsRun = true;
                bTestsComplete = false;
                Controller->RunTests(true);
                CurrentPhase = EPhase::Running;
                PhaseStart = Now;
                return;
            }
        }
        if (Now - PhaseStart > 30.0)
        {
            TArray<FString> Enabled;
            Controller->GetEnabledTestNames(Enabled);
            const int32 FilteredCount = Controller->GetFilteredReports().Num();
            const int32 EnabledReportCount = Controller->GetEnabledReports().Num();
            Fail(FString::Printf(TEXT("AutomationController did not confirm the requested native test after %d selection attempts (workers: %d; enabled names: %d; enabled reports: %d; filtered reports: %d). Full path: %s. Check requested test flags and worker availability."),
                SelectionAttempts, Controller->GetNumDeviceClusters(), Enabled.Num(), EnabledReportCount, FilteredCount, *Path));
        }
        return;
    }
    if (CurrentPhase == EPhase::Running && bTestsComplete)
    {
        CurrentPhase = EPhase::Complete;
    }
}
void FNativeExecution::Stop()
{
    if (!Controller.IsValid() || CurrentPhase == EPhase::Idle || FDecision::IsTerminal(CurrentPhase)) { return; }
    if (bOwnsRun && CurrentPhase == EPhase::Running && !bTestsComplete)
    {
        Controller->StopTests();
    }
    CurrentPhase = bTestsComplete ? EPhase::Complete : EPhase::Interrupted;
}
FString FNativeExecution::PhaseLabel() const
{
    switch (CurrentPhase)
    {
    case EPhase::DiscoveringWorkers: return TEXT("Discovering local worker");
    case EPhase::DiscoveringTests: return TEXT("Discovering native tests");
    case EPhase::Running: return TEXT("Running via AutomationController");
    case EPhase::Complete: return TEXT("Controller completed");
    case EPhase::Interrupted: return TEXT("Controller stopped");
    case EPhase::Error: return TEXT("Controller error");
    default: return TEXT("Idle");
    }
}
}
