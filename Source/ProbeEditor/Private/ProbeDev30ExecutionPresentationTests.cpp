#include "ProbeDev30ExecutionPresentation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev30ActiveMixedCounts,
    "Probe.Editor.Dev30ActiveMixedCounts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev30ActiveMixedCounts::RunTest(const FString&)
{
    const ProbeDev30::FProgress P = ProbeDev30::Summarize({ProbeDev29::EMemberState::Passed,
        ProbeDev29::EMemberState::Passed, ProbeDev29::EMemberState::Reused, ProbeDev29::EMemberState::Starting});
    TestEqual(TEXT("Four members, not result rows"), P.Total, 4);
    TestEqual(TEXT("Three completed"), P.Completed, 3);
    TestEqual(TEXT("Reused success counts as completed"), P.Passed, 3);
    TestEqual(TEXT("Current member selected"), P.ActiveIndex, 3);
    TestEqual(TEXT("Remaining one"), P.Pending, 1);
    TestTrue(TEXT("Three-quarter progress"), FMath::IsNearlyEqual(P.Fraction(), 0.75f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev30TerminalMixedCounts,
    "Probe.Editor.Dev30TerminalMixedCounts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev30TerminalMixedCounts::RunTest(const FString&)
{
    const ProbeDev30::FProgress P = ProbeDev30::Summarize({ProbeDev29::EMemberState::Passed,
        ProbeDev29::EMemberState::Failed, ProbeDev29::EMemberState::Skipped,
        ProbeDev29::EMemberState::Interrupted, ProbeDev29::EMemberState::Error});
    TestEqual(TEXT("All terminal outcomes count toward completion"), P.Completed, 5);
    TestEqual(TEXT("Only actual pass counted"), P.Passed, 1);
    TestEqual(TEXT("Errors, failures and interrupts grouped as problems"), P.Problems, 3);
    TestEqual(TEXT("Skipped remain separate"), P.Skipped, 1);
    TestEqual(TEXT("No active member"), P.ActiveIndex, INDEX_NONE);
    TestTrue(TEXT("Complete progress, regardless of pass ratio"), FMath::IsNearlyEqual(P.Fraction(), 1.f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev30EmptyMixedCounts,
    "Probe.Editor.Dev30EmptyMixedCounts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev30EmptyMixedCounts::RunTest(const FString&)
{
    const ProbeDev30::FProgress Empty = ProbeDev30::Summarize({});
    TestEqual(TEXT("Empty view total"), Empty.Total, 0);
    TestTrue(TEXT("Empty view does not divide by zero"), FMath::IsNearlyZero(Empty.Fraction()));
    const ProbeDev30::FProgress Queued = ProbeDev30::Summarize({ProbeDev29::EMemberState::Queued,
        ProbeDev29::EMemberState::Running, ProbeDev29::EMemberState::Queued});
    TestEqual(TEXT("In-flight member is not completed"), Queued.Completed, 0);
    TestEqual(TEXT("Running member position"), Queued.ActiveIndex, 1);
    return true;
}
#endif
