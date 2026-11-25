#include "ProbeDev35Impact.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FProbePackMixedEntry Dev35Member(const TCHAR* Path)
{
    FProbePackMixedEntry Entry;
    Entry.Source = EProbePackMixedSource::NativeAutomation;
    Entry.Locator = Path;
    return Entry;
}
TArray<FProbePackMixedEntry> Dev35Fixture()
{
    TArray<FProbePackMixedEntry> Entries = {
        Dev35Member(TEXT("Probe.A")), Dev35Member(TEXT("Probe.B")),
        Dev35Member(TEXT("Probe.C")), Dev35Member(TEXT("Probe.D")), Dev35Member(TEXT("Probe.E")) };
    Entries[1].Requires.Add(ProbeDev27::Identity(Entries[0]));
    Entries[2].Requires.Add(ProbeDev27::Identity(Entries[1]));
    Entries[3].Requires.Add(ProbeDev27::Identity(Entries[0]));
    Entries[3].Requires.Add(ProbeDev27::Identity(Entries[2]));
    return Entries; // A->B->C->D; A->D; E isolated
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev35GraphMetrics, "Probe.Editor.Dev35GraphMetrics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev35GraphMetrics::RunTest(const FString&)
{
    const ProbeDev35::FGraphSummary S = ProbeDev35::Analyze(Dev35Fixture());
    TestEqual(TEXT("Members"), S.Members, 5);
    TestEqual(TEXT("Links"), S.Links, 4);
    TestEqual(TEXT("Roots"), S.Roots, 2);
    TestEqual(TEXT("Leaves"), S.Leaves, 2);
    TestEqual(TEXT("Longest chain including endpoints"), S.LongestChain, 4);
    TestEqual(TEXT("Valid links"), S.UnresolvedLinks, 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev35SelectionImpact, "Probe.Editor.Dev35SelectionImpact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev35SelectionImpact::RunTest(const FString&)
{
    const TArray<FProbePackMixedEntry> E = Dev35Fixture();
    const TSet<FString> Selection = {ProbeDev27::Identity(E[1])};
    const ProbeDev35::FImpact R = ProbeDev35::AnalyzeRemoval(E, Selection);
    TestEqual(TEXT("Selected"), R.Selected.Num(), 1);
    TestEqual(TEXT("B C D affected"), R.Affected.Num(), 3);
    TestEqual(TEXT("Two outside selection"), R.AffectedOutsideSelection, 2);
    TestEqual(TEXT("One direct edge detached by selected-only removal"), R.DetachedLinks, 1);
    TestEqual(TEXT("Two names"), R.AffectedNames.Num(), 2);
    TestTrue(TEXT("Readable impact"), ProbeDev35::ImpactText(R, ProbeDev35::Analyze(E)).Contains(TEXT("Probe.C")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev35BranchRemoval, "Probe.Editor.Dev35BranchRemoval", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev35BranchRemoval::RunTest(const FString&)
{
    const TArray<FProbePackMixedEntry> E = Dev35Fixture();
    TArray<FProbePackMixedEntry> Out; ProbeDev35::FImpact R; FString Error;
    TSet<FString> Selection = {ProbeDev27::Identity(E[1])};
    TestEqual(TEXT("B and downstream C,D removed"), ProbeDev35::PlanRemoveBranch(E, Selection, Out, R, Error), 3);
    TestEqual(TEXT("A and E remain"), Out.Num(), 2);
    TestEqual(TEXT("Original input untouched"), E.Num(), 5);
    TestEqual(TEXT("A stays first"), ProbeDev27::Identity(Out[0]), ProbeDev27::Identity(E[0]));
    TestEqual(TEXT("E stays last"), ProbeDev27::Identity(Out[1]), ProbeDev27::Identity(E[4]));
    TestTrue(TEXT("Valid remaining order"), ProbeDev33::HasValidOrder(Out, Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev35DiamondImpact, "Probe.Editor.Dev35DiamondImpact", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev35DiamondImpact::RunTest(const FString&)
{
    const TArray<FProbePackMixedEntry> E = Dev35Fixture();
    const TSet<FString> Selection = {ProbeDev27::Identity(E[0])};
    TArray<FProbePackMixedEntry> Out; ProbeDev35::FImpact R; FString Error;
    TestEqual(TEXT("Diamond branch deduplicated"), ProbeDev35::PlanRemoveBranch(E, Selection, Out, R, Error), 4);
    TestEqual(TEXT("Independent leaf preserved"), Out.Num(), 1);
    TestEqual(TEXT("No dangling links"), Out[0].Requires.Num(), 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev35NoSelection, "Probe.Editor.Dev35NoSelection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev35NoSelection::RunTest(const FString&)
{
    const TArray<FProbePackMixedEntry> E = Dev35Fixture();
    TArray<FProbePackMixedEntry> Out; ProbeDev35::FImpact R; FString Error;
    TestEqual(TEXT("No selection no removal"), ProbeDev35::PlanRemoveBranch(E, TSet<FString>(), Out, R, Error), 0);
    TestEqual(TEXT("No mutation"), Out.Num(), E.Num());
    TestTrue(TEXT("Explained"), Error.Contains(TEXT("Select")));
    TSet<FString> Unknown = {TEXT("99:absent")};
    TestEqual(TEXT("Unknown identity not removed"), ProbeDev35::PlanRemoveBranch(E, Unknown, Out, R, Error), 0);
    TestEqual(TEXT("Unknown identity does not affect member count"), Out.Num(), E.Num());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev35EmptyAndInvalid, "Probe.Editor.Dev35EmptyAndInvalid", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev35EmptyAndInvalid::RunTest(const FString&)
{
    TArray<FProbePackMixedEntry> E;
    const ProbeDev35::FGraphSummary Empty = ProbeDev35::Analyze(E);
    TestEqual(TEXT("Empty chain"), Empty.LongestChain, 0);
    TestEqual(TEXT("Empty leaves"), Empty.Leaves, 0);
    E = Dev35Fixture();
    E[1].Requires.Add(TEXT("99:missing"));
    const ProbeDev35::FGraphSummary Invalid = ProbeDev35::Analyze(E);
    TestEqual(TEXT("Missing dependency diagnosed"), Invalid.UnresolvedLinks, 1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev35SelectedOnlySemantics, "Probe.Editor.Dev35SelectedOnlySemantics", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev35SelectedOnlySemantics::RunTest(const FString&)
{
    const TArray<FProbePackMixedEntry> E = Dev35Fixture();
    const TSet<FString> Selection = {ProbeDev27::Identity(E[1])};
    const ProbeDev35::FImpact Impact = ProbeDev35::AnalyzeRemoval(E, Selection);
    TArray<FProbePackMixedEntry> Out = E;
    TestEqual(TEXT("Existing removal retains dependents"), ProbeDev33::PlanRemoval(Out, Selection), 1);
    TestEqual(TEXT("One link detached"), Impact.DetachedLinks, 1);
    TestEqual(TEXT("Survivors"), Out.Num(), 4);
    FString Error;
    TestTrue(TEXT("Surviving Pack still validates"), ProbeDev33::HasValidOrder(Out, Error));
    return true;
}
#endif
