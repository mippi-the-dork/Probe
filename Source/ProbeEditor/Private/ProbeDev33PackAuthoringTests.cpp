#include "ProbeDev33PackAuthoring.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    FProbePackMixedEntry Native(const TCHAR* Path)
    {
        FProbePackMixedEntry Entry;
        Entry.Source = EProbePackMixedSource::NativeAutomation;
        Entry.Locator = Path;
        return Entry;
    }
    TArray<FProbePackMixedEntry> WithDependency()
    {
        TArray<FProbePackMixedEntry> Entries = { Native(TEXT("Probe.A")), Native(TEXT("Probe.B")), Native(TEXT("Probe.C")), Native(TEXT("Probe.D")) };
        Entries[2].Requires.Add(ProbeDev27::Identity(Entries[0]));
        return Entries;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev33VisibleSelection,
    "Probe.Editor.Dev33VisibleSelection", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev33VisibleSelection::RunTest(const FString&)
{
    TArray<FProbePackMixedEntry> Entries = { Native(TEXT("Probe.Automation.One")), Native(TEXT("Probe.Automation.Two")) };
    FProbePackMixedEntry Authored;
    Authored.Source = EProbePackMixedSource::AuthoredTest;
    Authored.Locator = TEXT("/Game/Tests/PT_Check.PT_Check");
    Entries.Add(Authored);
    FProbePackMixedEntry Portable;
    Portable.Source = EProbePackMixedSource::PortablePack;
    Portable.Locator = TEXT("Provider.Sample");
    Entries.Add(Portable);
    TestEqual(TEXT("All source filter"), ProbeDev33::VisibleIdentities(Entries, TEXT(""), 0).Num(), 4);
    TestEqual(TEXT("Native filter"), ProbeDev33::VisibleIdentities(Entries, TEXT(""), 3).Num(), 2);
    TestEqual(TEXT("Portable filter"), ProbeDev33::VisibleIdentities(Entries, TEXT(""), 2).Num(), 1);
    TestEqual(TEXT("Authored short-name search"), ProbeDev33::VisibleIdentities(Entries, TEXT("PT_Check"), 1).Num(), 1);
    TestEqual(TEXT("Search and filter intersect"), ProbeDev33::VisibleIdentities(Entries, TEXT("Probe.Automation"), 1).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev33BulkMoveStable,
    "Probe.Editor.Dev33BulkMoveStable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev33BulkMoveStable::RunTest(const FString&)
{
    const TArray<FProbePackMixedEntry> Entries = { Native(TEXT("Probe.A")), Native(TEXT("Probe.B")), Native(TEXT("Probe.C")), Native(TEXT("Probe.D")), Native(TEXT("Probe.E")) };
    TSet<FString> Selected = { ProbeDev27::Identity(Entries[1]), ProbeDev27::Identity(Entries[2]) };
    TArray<FProbePackMixedEntry> Out;
    FString Error;
    TestTrue(TEXT("Move selected block down"), ProbeDev33::PlanMove(Entries, Selected, 1, Out, Error));
    TestEqual(TEXT("First member stays"), Out[0].Locator, FString(TEXT("Probe.A")));
    TestEqual(TEXT("Unselected item crosses group"), Out[1].Locator, FString(TEXT("Probe.D")));
    TestEqual(TEXT("Selected B retains ordering"), Out[2].Locator, FString(TEXT("Probe.B")));
    TestEqual(TEXT("Selected C retains ordering"), Out[3].Locator, FString(TEXT("Probe.C")));
    TArray<FProbePackMixedEntry> Restored;
    TestTrue(TEXT("Move selected block up"), ProbeDev33::PlanMove(Out, Selected, -1, Restored, Error));
    TestEqual(TEXT("Block returns to original slot"), Restored[1].Locator, FString(TEXT("Probe.B")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev33BulkMoveDependencySafety,
    "Probe.Editor.Dev33BulkMoveDependencySafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev33BulkMoveDependencySafety::RunTest(const FString&)
{
    const TArray<FProbePackMixedEntry> Entries = WithDependency();
    TSet<FString> Selected = { ProbeDev27::Identity(Entries[0]) };
    TArray<FProbePackMixedEntry> Out;
    FString Error;
    TestTrue(TEXT("First move earlier than dependent valid"), ProbeDev33::PlanMove(Entries, Selected, 1, Out, Error));
    TArray<FProbePackMixedEntry> Rejected;
    TestFalse(TEXT("Second move across dependent rejected"), ProbeDev33::PlanMove(Out, Selected, 1, Rejected, Error));
    TestEqual(TEXT("Rejected move retains original order"), Rejected[1].Locator, FString(TEXT("Probe.A")));
    TestTrue(TEXT("Explains prerequisite obstruction"), Error.Contains(TEXT("prerequisite")));
    TSet<FString> MoveDependent = { ProbeDev27::Identity(Entries[2]) };
    TArray<FProbePackMixedEntry> FirstDependentMove;
    TestTrue(TEXT("Dependent can move within valid ordering"), ProbeDev33::PlanMove(Entries, MoveDependent, -1, FirstDependentMove, Error));
    TestFalse(TEXT("Dependent cannot move before prerequisite"), ProbeDev33::PlanMove(FirstDependentMove, MoveDependent, -1, Rejected, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev33BulkRemoval,
    "Probe.Editor.Dev33BulkRemoval", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev33BulkRemoval::RunTest(const FString&)
{
    TArray<FProbePackMixedEntry> Entries = WithDependency();
    Entries[3].Requires.Add(ProbeDev27::Identity(Entries[1]));
    TSet<FString> Selected = { ProbeDev27::Identity(Entries[0]), ProbeDev27::Identity(Entries[1]) };
    TestEqual(TEXT("Two members removed"), ProbeDev33::PlanRemoval(Entries, Selected), 2);
    TestEqual(TEXT("Unselected entries retained"), Entries.Num(), 2);
    TestTrue(TEXT("First dependent cleaned"), Entries[0].Requires.IsEmpty());
    TestTrue(TEXT("Second dependent cleaned"), Entries[1].Requires.IsEmpty());
    FString Error;
    TestTrue(TEXT("Remaining order valid"), ProbeDev33::HasValidOrder(Entries, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev33EmptyAndBoundary,
    "Probe.Editor.Dev33EmptyAndBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev33EmptyAndBoundary::RunTest(const FString&)
{
    TArray<FProbePackMixedEntry> Entries = { Native(TEXT("Probe.A")), Native(TEXT("Probe.B")) };
    TSet<FString> Selected = { ProbeDev27::Identity(Entries[0]) };
    TArray<FProbePackMixedEntry> Out;
    FString Error;
    TestFalse(TEXT("Top member cannot move above boundary"), ProbeDev33::PlanMove(Entries, Selected, -1, Out, Error));
    TestEqual(TEXT("Boundary doesn't mutate"), Out[0].Locator, FString(TEXT("Probe.A")));
    Selected.Reset();
    TestFalse(TEXT("Empty selection doesn't move"), ProbeDev33::PlanMove(Entries, Selected, 1, Out, Error));
    TestEqual(TEXT("Empty selection doesn't remove"), ProbeDev33::PlanRemoval(Entries, Selected), 0);
    return true;
}
#endif
