#include "ProbeDev23NativeBridge.h"
#include "ProbeDev11RunHistory.h"
#include "ProbeDev10RunHistory.h"
#include "ProbeDev13HistoryModel.h"
#include "ProbeDev19Authoring.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev24InterruptedStatus,
    "Probe.Editor.Dev24InterruptedStatus", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev24InterruptedStatus::RunTest(const FString&)
{
    const Probe::EResultStatus Status = Probe::EResultStatus::Interrupted;
    TestEqual(TEXT("Distinct exported status"), FString(Probe::StatusName(Status)), FString(TEXT("INTERRUPTED")));
    Probe::EResultStatus Parsed = Probe::EResultStatus::Error;
    TestTrue(TEXT("History can parse interrupted"), ProbeDev11RunHistory::ParseStatus(TEXT("INTERRUPTED"), Parsed));
    TestEqual(TEXT("History preserves interrupted"), Parsed, Status);
    TestFalse(TEXT("Intentional stop not a failure"), ProbeDev13History::IsProblem(Status));
    Probe::FTestResult Item;
    Item.Name = TEXT("Probe.Editor.Sample");
    Item.AssetPath = TEXT("unreal-automation://Probe.Editor.Sample");
    Item.Status = Status;
    ProbeDev11RunHistory::FSnapshot Snapshot;
    Snapshot.TimestampUtc = FDateTime::UtcNow();
    Snapshot.Results.Add(Item);
    FString Json;
    TestTrue(TEXT("History serializes interrupted"), ProbeDev11RunHistory::Serialize(Snapshot, Json));
    TestTrue(TEXT("History uses explicit interrupted status"), Json.Contains(TEXT("\"INTERRUPTED\"")));
    const ProbeDev13History::FRun Run = ProbeDev13History::MakeRun(TEXT("Example.json"), Snapshot);
    TestEqual(TEXT("Interrupted counted independently"), Run.Interrupted, 1);
    TestEqual(TEXT("Interrupted not counted as error"), Run.Errors, 0);
    TestEqual(TEXT("Interrupted not counted as problem"), Run.Problems, 0);
    TestEqual(TEXT("Report summary tracks interruption"), ProbeDev19::Summarize({Item}).Interrupted, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev24NoFalseRegression,
    "Probe.Editor.Dev24InterruptedComparison", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev24NoFalseRegression::RunTest(const FString&)
{
    Probe::FTestResult A;
    A.AssetPath = TEXT("unreal-automation://Probe.Editor.Sample");
    A.Status = Probe::EResultStatus::Passed;
    Probe::FTestResult B = A;
    B.Status = Probe::EResultStatus::Interrupted;
    const auto Stopped = ProbeDev10RunHistory::Compare({A}, {B});
    TestEqual(TEXT("Interrupted is not regression"), Stopped.Regressions, 0);
    const auto Resumed = ProbeDev10RunHistory::Compare({B}, {A});
    TestEqual(TEXT("Later pass is not false recovery"), Resumed.Recovered, 0);
    ProbeDev11RunHistory::FSnapshot Previous, Current;
    Previous.Results.Add(A);
    Current.Results.Add(B);
    const auto Delta = ProbeDev13History::Compare(Previous, Current);
    TestTrue(TEXT("Interrupt delta retained"), Delta.Num() == 1);
    if (Delta.Num() == 1)
    { TestEqual(TEXT("Interrupted is its own history delta"), Delta[0].Kind, ProbeDev13History::EDeltaKind::Interrupted); }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev24ControllerDiagnostics,
    "Probe.Editor.Dev24ControllerDiagnostics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev24ControllerDiagnostics::RunTest(const FString&)
{
    TestTrue(TEXT("Controller warning retained"), ProbeDev23::IsControllerDiagnostic(TEXT("Warning: unexpected value")));
    TestTrue(TEXT("Controller event retained"), ProbeDev23::IsControllerDiagnostic(TEXT("BeginEvents: 1")));
    TestFalse(TEXT("Submission not a diagnostic"), ProbeDev23::IsControllerDiagnostic(TEXT("Automation RunTests Queued.")));
    TestFalse(TEXT("Unrelated output not a diagnostic"), ProbeDev23::IsControllerDiagnostic(TEXT("LogTexture: Streaming completed.")));
    return true;
}
#endif
