#include "ProbeDev32Presentation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    using S = ProbeDev29::EMemberState;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev32CompletionPassed,
    "Probe.Editor.Dev32CompletionPassed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev32CompletionPassed::RunTest(const FString&)
{
    const TArray<S> States = {S::Passed, S::Reused, S::Passed};
    TestTrue(TEXT("All confirmed passes complete as solid success"), ProbeDev32::ResolveOutcome(States, false) == ProbeDev32::EBarOutcome::Passed);
    TestTrue(TEXT("Running state remains active blue regardless of completed entries"), ProbeDev32::ResolveOutcome(States, true) == ProbeDev32::EBarOutcome::InProgress);
    TestEqual(TEXT("Completed member count stays stable"), ProbeDev30::Summarize(States).Completed, 3);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev32CompletionProblems,
    "Probe.Editor.Dev32CompletionProblems", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev32CompletionProblems::RunTest(const FString&)
{
    const TArray<S> Failed = {S::Passed, S::Failed, S::Skipped};
    const TArray<S> Interrupted = {S::Passed, S::Interrupted};
    TestTrue(TEXT("Failure is prioritized over skip"), ProbeDev32::ResolveOutcome(Failed, false) == ProbeDev32::EBarOutcome::Problems);
    TestTrue(TEXT("Interrupt is a problem, not success"), ProbeDev32::ResolveOutcome(Interrupted, false) == ProbeDev32::EBarOutcome::Problems);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev32CompletionSkipped,
    "Probe.Editor.Dev32CompletionSkipped", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev32CompletionSkipped::RunTest(const FString&)
{
    const TArray<S> States = {S::Passed, S::Skipped};
    TestTrue(TEXT("Completed skip gets amber state"), ProbeDev32::ResolveOutcome(States, false) == ProbeDev32::EBarOutcome::Skipped);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev32CompletionIncomplete,
    "Probe.Editor.Dev32CompletionIncomplete", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev32CompletionIncomplete::RunTest(const FString&)
{
    const TArray<S> Empty;
    const TArray<S> Unfinished = {S::Passed, S::Starting, S::Queued};
    TestTrue(TEXT("Empty queue cannot claim success"), ProbeDev32::ResolveOutcome(Empty, false) == ProbeDev32::EBarOutcome::Empty);
    TestTrue(TEXT("Pending queue cannot claim success"), ProbeDev32::ResolveOutcome(Unfinished, false) == ProbeDev32::EBarOutcome::Empty);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev32SourceSummary,
    "Probe.Editor.Dev32PackSourceSummary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev32SourceSummary::RunTest(const FString&)
{
    UProbeTestPack* Pack = NewObject<UProbeTestPack>(GetTransientPackage());
    FProbePackMixedEntry A; A.Source = EProbePackMixedSource::AuthoredTest;
    FProbePackMixedEntry B; B.Source = EProbePackMixedSource::NativeAutomation;
    FProbePackMixedEntry C; C.Source = EProbePackMixedSource::PortablePack;
    Pack->MixedEntries = {A, B, C};
    const FString Summary = ProbeDev32::PackSourceSummary(Pack);
    TestTrue(TEXT("Counts authored"), Summary.Contains(TEXT("1 authored")));
    TestTrue(TEXT("Counts native"), Summary.Contains(TEXT("1 native")));
    TestTrue(TEXT("Counts portable Pack"), Summary.Contains(TEXT("1 portable")));
    return true;
}
#endif
