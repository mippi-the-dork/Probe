#include "ProbeDev29MixedRecovery.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    FProbePackMixedEntry Member(const TCHAR* Locator)
    {
        FProbePackMixedEntry E;
        E.Source = EProbePackMixedSource::NativeAutomation;
        E.Locator = Locator;
        return E;
    }
    Probe::FTestResult Result(Probe::EResultStatus Status)
    {
        Probe::FTestResult R; R.Status = Status; return R;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev29OutcomePolicy,
    "Probe.Editor.Dev29OutcomePolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev29OutcomePolicy::RunTest(const FString&)
{
    TestFalse(TEXT("Empty member is not success"), ProbeDev29::AllPassed({}));
    TestTrue(TEXT("Single passed member"), ProbeDev29::AllPassed({Result(Probe::EResultStatus::Passed)}));
    TestFalse(TEXT("Portable Pack mixed outcomes not success"), ProbeDev29::AllPassed({Result(Probe::EResultStatus::Passed), Result(Probe::EResultStatus::Failed)}));
    TestEqual(TEXT("Failure wins"), ProbeDev29::Outcome({Result(Probe::EResultStatus::Passed), Result(Probe::EResultStatus::Failed)}), ProbeDev29::EMemberState::Failed);
    TestEqual(TEXT("Interrupted retained"), ProbeDev29::Outcome({Result(Probe::EResultStatus::Interrupted)}), ProbeDev29::EMemberState::Interrupted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev29ReplaySelection,
    "Probe.Editor.Dev29ReplaySelection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev29ReplaySelection::RunTest(const FString&)
{
    TArray<FProbePackMixedEntry> Entries{Member(TEXT("Probe.One")), Member(TEXT("Probe.Two")), Member(TEXT("Probe.Three"))};
    TMap<FString, TArray<Probe::FTestResult>> Outcomes;
    Outcomes.Add(ProbeDev27::Identity(Entries[0]), {Result(Probe::EResultStatus::Passed)});
    Outcomes.Add(ProbeDev27::Identity(Entries[1]), {Result(Probe::EResultStatus::Failed)});
    Outcomes.Add(ProbeDev27::Identity(Entries[2]), {Result(Probe::EResultStatus::Skipped)});
    TSet<FString> Reused; FString Reason;
    TestTrue(TEXT("Valid partial retry"), ProbeDev29::BuildRetryPlan(Entries, Entries, Outcomes, Reused, Reason));
    TestEqual(TEXT("Only the success is reused"), Reused.Num(), 1);
    TestTrue(TEXT("Passed test reused"), Reused.Contains(ProbeDev27::Identity(Entries[0])));
    TestFalse(TEXT("Failed test reruns"), Reused.Contains(ProbeDev27::Identity(Entries[1])));
    TestFalse(TEXT("Dependency skipped test reruns"), Reused.Contains(ProbeDev27::Identity(Entries[2])));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev29DefinitionChange,
    "Probe.Editor.Dev29DefinitionChange", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev29DefinitionChange::RunTest(const FString&)
{
    TArray<FProbePackMixedEntry> Old{Member(TEXT("Probe.A")), Member(TEXT("Probe.B"))};
    TArray<FProbePackMixedEntry> Changed = Old;
    TMap<FString, TArray<Probe::FTestResult>> Outcomes;
    Outcomes.Add(ProbeDev27::Identity(Old[0]), {Result(Probe::EResultStatus::Passed)});
    Outcomes.Add(ProbeDev27::Identity(Old[1]), {Result(Probe::EResultStatus::Failed)});
    TSet<FString> Reuse; FString Reason;
    Changed.Swap(0, 1);
    TestFalse(TEXT("Reordered Pack rejected"), ProbeDev29::BuildRetryPlan(Old, Changed, Outcomes, Reuse, Reason));
    TestTrue(TEXT("Explains invalidated snapshot"), Reason.Contains(TEXT("changed")));
    Changed = Old;
    Changed[1].Requires.Add(ProbeDev27::Identity(Changed[0]));
    TestFalse(TEXT("Changed prerequisites rejected"), ProbeDev29::BuildRetryPlan(Old, Changed, Outcomes, Reuse, Reason));
    Changed = Old; Changed[1].Locator = TEXT("Probe.C");
    TestFalse(TEXT("Changed source locator rejected"), ProbeDev29::BuildRetryPlan(Old, Changed, Outcomes, Reuse, Reason));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev29IncompleteReplay,
    "Probe.Editor.Dev29IncompleteReplay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev29IncompleteReplay::RunTest(const FString&)
{
    TArray<FProbePackMixedEntry> Entries{Member(TEXT("Probe.A")), Member(TEXT("Probe.B"))};
    TMap<FString, TArray<Probe::FTestResult>> Outcomes;
    Outcomes.Add(ProbeDev27::Identity(Entries[0]), {Result(Probe::EResultStatus::Passed)});
    TSet<FString> Reuse; FString Reason;
    TestFalse(TEXT("Missing member outcome prevents replay"), ProbeDev29::BuildRetryPlan(Entries, Entries, Outcomes, Reuse, Reason));
    TestEqual(TEXT("No leaked replay selection"), Reuse.Num(), 0);
    Outcomes.Add(ProbeDev27::Identity(Entries[1]), {});
    TestFalse(TEXT("Empty member outcome prevents replay"), ProbeDev29::BuildRetryPlan(Entries, Entries, Outcomes, Reuse, Reason));
    Outcomes.FindChecked(ProbeDev27::Identity(Entries[1])).Add(Result(Probe::EResultStatus::Passed));
    TestFalse(TEXT("No-op recovery rejected"), ProbeDev29::BuildRetryPlan(Entries, Entries, Outcomes, Reuse, Reason));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev29ReadableDiagnostics,
    "Probe.Editor.Dev29ReadableDiagnostics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev29ReadableDiagnostics::RunTest(const FString&)
{
    FProbePackMixedEntry Authored;
    Authored.Source = EProbePackMixedSource::AuthoredTest;
    Authored.Locator = TEXT("/Game/QA/PT_Ready.PT_Ready");
    TArray<FProbePackMixedEntry> Entries{Authored, Member(TEXT("Probe.Native"))};
    TestEqual(TEXT("Authored path shortened"), ProbeDev29::DisplayName(Authored), FString(TEXT("PT_Ready")));
    TestEqual(TEXT("Dependency identifies row"), ProbeDev29::ReadableIdentity(ProbeDev27::Identity(Authored), Entries), FString(TEXT("#1 PT_Ready")));
    TestEqual(TEXT("Unresolved identity retained"), ProbeDev29::ReadableIdentity(TEXT("77:unknown"), Entries), FString(TEXT("77:unknown")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev29StateLabels,
    "Probe.Editor.Dev29StateLabels", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev29StateLabels::RunTest(const FString&)
{
    TestEqual(TEXT("Starting state"), FString(ProbeDev29::StateLabel(ProbeDev29::EMemberState::Starting)), FString(TEXT("Starting")));
    TestEqual(TEXT("Running state"), FString(ProbeDev29::StateLabel(ProbeDev29::EMemberState::Running)), FString(TEXT("Running")));
    TestEqual(TEXT("Reused pass disclosed"), FString(ProbeDev29::StateLabel(ProbeDev29::EMemberState::Reused)), FString(TEXT("Passed (reused)")));
    TestEqual(TEXT("Queued state"), FString(ProbeDev29::StateLabel(ProbeDev29::EMemberState::Queued)), FString(TEXT("Queued")));
    return true;
}
#endif
