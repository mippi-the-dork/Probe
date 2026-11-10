#include "ProbeDev23NativeBridge.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev23NativeCompletion,
    "Probe.Editor.Dev23NativeCompletionParsing", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev23NativeCompletion::RunTest(const FString&)
{
    FString Path, Outcome;
    TestTrue(TEXT("Controller completion parsed"), ProbeDev23::ParseCompleted(
        TEXT("Test Completed. Result={Success} Name={Example} Path={Probe.Editor.Example}"), Path, Outcome));
    TestEqual(TEXT("Full path retained"), Path, FString(TEXT("Probe.Editor.Example")));
    TestEqual(TEXT("Outcome retained"), Outcome, FString(TEXT("Success")));
    TestEqual(TEXT("Success maps to passed"), ProbeDev23::StatusFromOutcome(Outcome), Probe::EResultStatus::Passed);
    TestEqual(TEXT("Failure maps to failed"), ProbeDev23::StatusFromOutcome(TEXT("Fail")), Probe::EResultStatus::Failed);
    TestEqual(TEXT("Skipped maps to skipped"), ProbeDev23::StatusFromOutcome(TEXT("Skipped")), Probe::EResultStatus::Skipped);
    TestEqual(TEXT("Unexpected status is error"), ProbeDev23::StatusFromOutcome(TEXT("Unknown")), Probe::EResultStatus::Error);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev23NativeSafety,
    "Probe.Editor.Dev23NativeRecordSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev23NativeSafety::RunTest(const FString&)
{
    FString Path, Outcome;
    TestTrue(TEXT("Controller start parsed"), ProbeDev23::ParseStarted(
        TEXT("Test Started. Name={Probe} Path={Probe.Editor.Example}"), Path));
    TestFalse(TEXT("Submission never counts as completion"), ProbeDev23::ParseCompleted(
        TEXT("Automation: RunTests='Probe.Editor.Example' Queued."), Path, Outcome));
    TestFalse(TEXT("Unrelated pass not a matching controller completion"), ProbeDev23::ParseCompleted(
        TEXT("Test 'Probe.Editor.Example' completed with result 'Success'"), Path, Outcome));
    TestFalse(TEXT("Malformed completion rejected"), ProbeDev23::ParseCompleted(
        TEXT("Test Completed. Result={Success} Name={Example}"), Path, Outcome));
    return true;
}
#endif
