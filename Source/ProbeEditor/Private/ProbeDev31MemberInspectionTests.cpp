#include "ProbeDev31MemberInspection.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FProbePackMixedEntry Native(const TCHAR* Name)
{
    FProbePackMixedEntry E;
    E.Source = EProbePackMixedSource::NativeAutomation;
    E.Locator = Name;
    return E;
}
Probe::FTestResult Result(const TCHAR* Name, Probe::EResultStatus Status = Probe::EResultStatus::Passed)
{
    Probe::FTestResult R;
    R.Name = Name;
    R.Status = Status;
    return R;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev31MultiResultOffset,
    "Probe.Editor.Dev31MultiResultOffset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev31MultiResultOffset::RunTest(const FString&)
{
    const TArray<FProbePackMixedEntry> Members = {Native(TEXT("A")), Native(TEXT("B")), Native(TEXT("C"))};
    TMap<FString, TArray<Probe::FTestResult>> Groups;
    Groups.Add(ProbeDev27::Identity(Members[0]), {Result(TEXT("A"))});
    Groups.Add(ProbeDev27::Identity(Members[1]), {Result(TEXT("B/1")), Result(TEXT("B/2")), Result(TEXT("B/3"))});
    Groups.Add(ProbeDev27::Identity(Members[2]), {Result(TEXT("C"))});
    TestEqual(TEXT("First member selects first result"), ProbeDev31::ResultOffset(Members, Groups, 0), 0);
    TestEqual(TEXT("Portable-style group selects its first result"), ProbeDev31::ResultOffset(Members, Groups, 1), 1);
    TestEqual(TEXT("Later member skips all three previous group results"), ProbeDev31::ResultOffset(Members, Groups, 2), 4);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev31IncompleteResultOffset,
    "Probe.Editor.Dev31IncompleteResultOffset", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev31IncompleteResultOffset::RunTest(const FString&)
{
    const TArray<FProbePackMixedEntry> Members = {Native(TEXT("A")), Native(TEXT("B"))};
    TMap<FString, TArray<Probe::FTestResult>> Groups;
    Groups.Add(ProbeDev27::Identity(Members[0]), {Result(TEXT("A"))});
    TestTrue(TEXT("Completed member is inspectable"), ProbeDev31::IsInspectable(Members, Groups, 0));
    TestFalse(TEXT("Undispatched member is not inspectable"), ProbeDev31::IsInspectable(Members, Groups, 1));
    TestEqual(TEXT("Out of bounds refused"), ProbeDev31::ResultOffset(Members, Groups, 99), INDEX_NONE);
    Groups.Add(ProbeDev27::Identity(Members[1]), TArray<Probe::FTestResult>{});
    TestEqual(TEXT("No result is not a success"), ProbeDev31::ResultOffset(Members, Groups, 1), INDEX_NONE);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev31SkippedAndReusedSelection,
    "Probe.Editor.Dev31SkippedAndReusedSelection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev31SkippedAndReusedSelection::RunTest(const FString&)
{
    const TArray<FProbePackMixedEntry> Members = {Native(TEXT("A")), Native(TEXT("B")), Native(TEXT("C"))};
    TMap<FString, TArray<Probe::FTestResult>> Groups;
    Groups.Add(ProbeDev27::Identity(Members[0]), {Result(TEXT("A"), Probe::EResultStatus::Failed)});
    Groups.Add(ProbeDev27::Identity(Members[1]), {Result(TEXT("B"), Probe::EResultStatus::Skipped)});
    Groups.Add(ProbeDev27::Identity(Members[2]), {Result(TEXT("C"))});
    TestEqual(TEXT("Skipped is selectable"), ProbeDev31::ResultOffset(Members, Groups, 1), 1);
    TestEqual(TEXT("Reused/previous success selects correctly"), ProbeDev31::ResultOffset(Members, Groups, 2), 2);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev31InspectableSummary,
    "Probe.Editor.Dev31InspectableSummary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev31InspectableSummary::RunTest(const FString&)
{
    TArray<Probe::FTestResult> Records = {Result(TEXT("A")), Result(TEXT("B"), Probe::EResultStatus::Failed)};
    Records[1].Detail = TEXT("Intentional failure");
    const FString Summary = ProbeDev31::MemberSummary(&Records);
    TestTrue(TEXT("Reports all results in group"), Summary.Contains(TEXT("2 result(s)")));
    TestTrue(TEXT("Keeps failure status"), Summary.Contains(TEXT("FAILED")));
    TestTrue(TEXT("Keeps failure explanation"), Summary.Contains(TEXT("Intentional failure")));
    TestTrue(TEXT("Empty member reports unavailable"), ProbeDev31::MemberSummary(nullptr).Contains(TEXT("no completed")));
    return true;
}
#endif
