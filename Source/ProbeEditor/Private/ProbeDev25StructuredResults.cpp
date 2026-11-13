#include "ProbeDev25StructuredResults.h"
#include "Modules/ModuleManager.h"
#include "IAutomationControllerModule.h"
#include "IAutomationControllerManager.h"
#include "IAutomationReport.h"
#include "Misc/AutomationEvent.h"
#include "AutomationState.h"

namespace ProbeDev25
{
// Avoid AutomationStateToString(): UE 5.8's helper uses a reflected
// StaticEnum<EAutomationState>() symbol that is not exported to this module.
// Keep this conversion local so the structured report reader links correctly.
static FString ProbeAutomationStateToString(EAutomationState State)
{
    switch (State)
    {
    case EAutomationState::NotRun: return TEXT("NotRun");
    case EAutomationState::InProcess: return TEXT("InProcess");
    case EAutomationState::Fail: return TEXT("Fail");
    case EAutomationState::Success: return TEXT("Success");
    case EAutomationState::Skipped: return TEXT("Skipped");
    default: return TEXT("Unknown");
    }
}
bool IsTerminalState(const FString& State)
{
    return State.Equals(TEXT("Success"), ESearchCase::IgnoreCase)
        || State.Equals(TEXT("Fail"), ESearchCase::IgnoreCase)
        || State.Equals(TEXT("Failed"), ESearchCase::IgnoreCase)
        || State.Equals(TEXT("Skipped"), ESearchCase::IgnoreCase);
}
Probe::EResultStatus ConvertState(const FString& State)
{
    if (State.Equals(TEXT("Success"), ESearchCase::IgnoreCase)) { return Probe::EResultStatus::Passed; }
    if (State.Equals(TEXT("Skipped"), ESearchCase::IgnoreCase)) { return Probe::EResultStatus::Skipped; }
    if (State.Equals(TEXT("Fail"), ESearchCase::IgnoreCase) || State.Equals(TEXT("Failed"), ESearchCase::IgnoreCase))
    { return Probe::EResultStatus::Failed; }
    return Probe::EResultStatus::Error;
}

static TSharedPtr<IAutomationReport> FindExactReport(const TArray<TSharedPtr<IAutomationReport>>& Roots, const FString& FullPath)
{
    // Iterative walk avoids recursion depth assumptions and does not mutate the controller's tree.
    TArray<TSharedPtr<IAutomationReport>> Pending = Roots;
    int32 Visits = 0;
    while (Pending.Num() && ++Visits <= 50000)
    {
        const TSharedPtr<IAutomationReport> Report = Pending.Pop(EAllowShrinking::No);
        if (!Report.IsValid()) { continue; }
        if (Report->GetFullTestPath().Equals(FullPath, ESearchCase::CaseSensitive)) { return Report; }
        for (const TSharedPtr<IAutomationReport>& Child : Report->GetChildReports()) { Pending.Add(Child); }
    }
    return nullptr;
}

bool TryReadCompletedReport(const FString& FullPath, FStructuredResult& Out)
{
    Out = FStructuredResult();
    if (FullPath.IsEmpty() || !IsInGameThread()) { return false; }
    IAutomationControllerModule* Module = FModuleManager::GetModulePtr<IAutomationControllerModule>(TEXT("AutomationController"));
    if (!Module) { return false; }
    const IAutomationControllerManagerRef Controller = Module->GetAutomationController();
    const TSharedPtr<IAutomationReport> Report = FindExactReport(Controller->GetEnabledReports(), FullPath);
    if (!Report.IsValid()) { return false; }

    // A report may contain cached results from another pass/worker. Gather only terminal
    // results; the enclosing workspace requires a matching current-run completion signal.
    bool bFound = false;
    Out.Path = FullPath;
    for (int32 Cluster = 0; Cluster < Controller->GetNumDeviceClusters(); ++Cluster)
    {
        const int32 Count = FMath::Min(Report->GetNumResults(Cluster), FMath::Max(1, Controller->GetNumPasses()));
        for (int32 Pass = 0; Pass < Count; ++Pass)
        {
            const FAutomationTestResults& Item = Report->GetResults(Cluster, Pass);
            const FString State = ProbeAutomationStateToString(Item.State);
            if (!IsTerminalState(State)) { continue; }
            if (!bFound || ConvertState(State) == Probe::EResultStatus::Failed)
            { Out.State = State; Out.DurationMs = FMath::Max(0.0, static_cast<double>(Item.Duration) * 1000.0); }
            bFound = true;
            Out.Warnings += Item.GetWarningTotal();
            Out.Errors += Item.GetErrorTotal();
            if (!Item.GameInstance.IsEmpty()) { Out.GameInstance = Item.GameInstance; }
            if (Out.Device.IsEmpty()) { Out.Device = Controller->GetDeviceTypeName(Cluster); }
            for (const FAutomationExecutionEntry& Event : Item.GetEntries())
            {
                if (Out.Events.Num() >= 128) { break; }
                Out.Events.Add(Event.ToString().Left(1500));
            }
        }
    }
    Out.bAvailable = bFound;
    return bFound;
}
}
