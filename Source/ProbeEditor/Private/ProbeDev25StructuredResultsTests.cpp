#include "ProbeDev25StructuredResults.h"
#include "ProbeDev23NativeBridge.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev25StateMapping,
    "Probe.Editor.Dev25StructuredStateMapping", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev25StateMapping::RunTest(const FString&)
{
    TestTrue(TEXT("Success is terminal"), ProbeDev25::IsTerminalState(TEXT("Success")));
    TestTrue(TEXT("Fail is terminal"), ProbeDev25::IsTerminalState(TEXT("Fail")));
    TestTrue(TEXT("Skipped is terminal"), ProbeDev25::IsTerminalState(TEXT("Skipped")));
    TestFalse(TEXT("InProcess is not a result"), ProbeDev25::IsTerminalState(TEXT("InProcess")));
    TestFalse(TEXT("NotRun is not a result"), ProbeDev25::IsTerminalState(TEXT("NotRun")));
    TestEqual(TEXT("Success"), ProbeDev25::ConvertState(TEXT("Success")), Probe::EResultStatus::Passed);
    TestEqual(TEXT("Fail"), ProbeDev25::ConvertState(TEXT("Fail")), Probe::EResultStatus::Failed);
    TestEqual(TEXT("Skipped"), ProbeDev25::ConvertState(TEXT("Skipped")), Probe::EResultStatus::Skipped);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev25ReportOwnership,
    "Probe.Editor.Dev25ExactReportOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev25ReportOwnership::RunTest(const FString&)
{
    ProbeDev25::FStructuredResult Result;
    TestFalse(TEXT("Empty identity rejected"), ProbeDev25::TryReadCompletedReport(TEXT(""), Result));
    TestFalse(TEXT("No invented report for unmatched identity"),
        ProbeDev25::TryReadCompletedReport(TEXT("Probe.Editor.__MissingDev25Report_987654"), Result));
    TestFalse(TEXT("Unmatched result remains unavailable"), Result.bAvailable);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev25StatusConsistency,
    "Probe.Editor.Dev25OutcomeConsistency", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev25StatusConsistency::RunTest(const FString&)
{
    TestEqual(TEXT("Success matches bridge"), ProbeDev25::ConvertState(TEXT("Success")), ProbeDev23::StatusFromOutcome(TEXT("Success")));
    TestEqual(TEXT("Fail matches bridge"), ProbeDev25::ConvertState(TEXT("Fail")), ProbeDev23::StatusFromOutcome(TEXT("Fail")));
    TestEqual(TEXT("Skipped matches bridge"), ProbeDev25::ConvertState(TEXT("Skipped")), ProbeDev23::StatusFromOutcome(TEXT("Skipped")));
    return true;
}
#endif
