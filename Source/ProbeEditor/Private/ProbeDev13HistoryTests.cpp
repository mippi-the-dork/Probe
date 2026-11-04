#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ProbeDev13HistoryModel.h"
#include "ProbeDev13HistoryBrowser.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace
{
    ProbeDev13History::FSnapshot MakeRun(int32 Minute, Probe::EResultStatus Status,
        int32 Variation = INDEX_NONE, const FString& Asset = TEXT("/Game/QA/Case.Case"))
    {
        ProbeDev13History::FSnapshot Run;
        Run.TimestampUtc = FDateTime(2026, 11, 3, 10, Minute, 0);
        Run.EngineVersion = TEXT("5.8.3-Test");
        Run.PackPath = TEXT("/Game/QA/Pack.Pack");
        Probe::FTestResult Result;
        Result.AssetPath = Asset;
        Result.Name = TEXT("Case");
        Result.Status = Status;
        Result.VariationIndex = Variation;
        if (Variation != INDEX_NONE) { Result.VariationName = TEXT("Alternate"); }
        Run.Results.Add(MoveTemp(Result));
        return Run;
    }

    Probe::FStepResult BadStep(int32 Index, const FString& Detail, const FString& Group = TEXT("Verify"))
    {
        Probe::FStepResult Step;
        Step.Index = Index;
        Step.AssertionId = TEXT("Probe.TestProperty");
        Step.GroupName = Group;
        Step.Detail = Detail;
        Step.Status = Probe::EResultStatus::Failed;
        return Step;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev13HistoryQuery,
    "Probe.Editor.Dev13HistoryQuery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev13HistoryQuery::RunTest(const FString& Parameters)
{
    auto Run = MakeRun(0, Probe::EResultStatus::Passed);
    Run.Results.Add(MakeRun(0, Probe::EResultStatus::Failed, 1).Results[0]);
    Run.Results.Add(MakeRun(0, Probe::EResultStatus::Skipped, 2).Results[0]);
    Run.Results.Add(MakeRun(0, Probe::EResultStatus::Error, 3).Results[0]);
    const auto Row = ProbeDev13History::MakeRun(TEXT("/unused"), Run, 1024);
    TestEqual(TEXT("Counts pass"), Row.Passed, 1);
    TestEqual(TEXT("Counts both error and failed as problem"), Row.Problems, 2);
    TestEqual(TEXT("Separately counts errors"), Row.Errors, 1);
    TestEqual(TEXT("Counts skipped"), Row.Skipped, 1);
    TestTrue(TEXT("Timestamp matches browser search"), ProbeDev13History::MatchesWithTimestamp(Row,
        TEXT("2026-11-03 10:00"), ProbeDev13History::EStatusFilter::All));
    TestTrue(TEXT("Pack matches browser search"), ProbeDev13History::MatchesWithTimestamp(Row,
        TEXT("Pack.Pack"), ProbeDev13History::EStatusFilter::Problems));
    TestFalse(TEXT("Problem run excluded from passing-only filter"), ProbeDev13History::MatchesWithTimestamp(Row,
        FString(), ProbeDev13History::EStatusFilter::PassedOnly));
    TestFalse(TEXT("Unknown search excluded"), ProbeDev13History::MatchesWithTimestamp(Row,
        TEXT("does-not-exist"), ProbeDev13History::EStatusFilter::All));
    TestTrue(TEXT("Browser label differentiates runs by seconds"), Row.Label().Contains(TEXT("10:00:00 UTC")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev13HistoricalDiff,
    "Probe.Editor.Dev13HistoricalDiff",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev13HistoricalDiff::RunTest(const FString& Parameters)
{
    auto Before = MakeRun(0, Probe::EResultStatus::Passed);
    Before.Results.Add(MakeRun(0, Probe::EResultStatus::Failed, 1).Results[0]);
    Before.Results.Add(MakeRun(0, Probe::EResultStatus::Failed, 2).Results[0]);
    Before.Results.Add(MakeRun(0, Probe::EResultStatus::Passed, 3).Results[0]);
    auto After = MakeRun(1, Probe::EResultStatus::Failed);
    After.Results.Add(MakeRun(1, Probe::EResultStatus::Passed, 1).Results[0]);
    After.Results.Add(MakeRun(1, Probe::EResultStatus::Error, 2).Results[0]);
    After.Results.Add(MakeRun(1, Probe::EResultStatus::Skipped, 3).Results[0]);
    After.Results.Add(MakeRun(1, Probe::EResultStatus::Passed, 4).Results[0]);
    const auto Deltas = ProbeDev13History::Compare(Before, After);
    TestEqual(TEXT("Unique comparison rows"), Deltas.Num(), 5);
    int32 Regression = 0, Recovered = 0, Still = 0, Skipped = 0, New = 0;
    for (const auto& Row : Deltas)
    {
        using K = ProbeDev13History::EDeltaKind;
        Regression += Row.Kind == K::NewFailure;
        Recovered += Row.Kind == K::Recovered;
        Still += Row.Kind == K::StillFailing;
        Skipped += Row.Kind == K::Skipped;
        New += Row.Kind == K::NewlyObserved;
    }
    TestEqual(TEXT("Pass-to-fail is a regression"), Regression, 1);
    TestEqual(TEXT("Fail-to-pass is a recovery"), Recovered, 1);
    TestEqual(TEXT("Error following failure remains failing"), Still, 1);
    TestEqual(TEXT("Skipped does not become a false regression"), Skipped, 1);
    TestEqual(TEXT("Newly seen variation is separate"), New, 1);
    if (Deltas.IsValidIndex(0))
    {
        TestTrue(TEXT("Stable variation IDs kept distinct"), Deltas[0].Identity.Contains(TEXT("#")));
    }
    else
    {
        AddError(TEXT("No comparison rows were produced; cannot inspect variation identity"));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev13RecurringFailures,
    "Probe.Editor.Dev13RecurringFailures",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev13RecurringFailures::RunTest(const FString& Parameters)
{
    TArray<ProbeDev13History::FSnapshot> Runs;
    auto First = MakeRun(0, Probe::EResultStatus::Failed, 1);
    First.Results[0].Steps.Add(BadStep(4, TEXT("Expected 1, got 2")));
    // Duplicate the first failure, but copy it BEFORE mutating its owning TArray.
    // Passing an element of this same array directly to Add can reallocate it
    // and trips UE's container aliasing assertion (Array.h).
    const Probe::FTestResult DuplicateFirst = First.Results[0];
    First.Results.Add(DuplicateFirst);
    TestEqual(TEXT("Duplicate fixture contains two entries"), First.Results.Num(), 2);
    Runs.Add(First);
    auto Second = MakeRun(1, Probe::EResultStatus::Failed, 1);
    Second.Results[0].Steps.Add(BadStep(4, TEXT("Expected 1, got 3")));
    Second.Results.Add(MakeRun(1, Probe::EResultStatus::Failed, 2).Results[0]);
    Second.Results[1].Steps.Add(BadStep(4, TEXT("Different variation")));
    Runs.Add(Second);
    auto Third = MakeRun(2, Probe::EResultStatus::Failed, 1);
    Third.Results[0].Steps.Add(BadStep(4, TEXT("Different group"), TEXT("Cleanup")));
    Runs.Add(Third);
    const auto Incidents = ProbeDev13History::RecurringFailures(Runs);
    TestEqual(TEXT("Variations and groups produce distinct signatures"), Incidents.Num(), 3);
    if (Incidents.IsValidIndex(0))
    {
        TestEqual(TEXT("Two affected runs for stable signature"), Incidents[0].RunsAffected, 2);
        TestEqual(TEXT("Latest detail preserved despite changed observed value"),
            Incidents[0].MostRecentDetail, FString(TEXT("Expected 1, got 3")));
    }
    else
    {
        AddError(TEXT("No recurring failure signatures were produced; cannot inspect first signature"));
    }
    TArray<ProbeDev13History::FRun> NewestFirst;
    NewestFirst.Add(ProbeDev13History::MakeRun(TEXT("newest"), Runs[2]));
    NewestFirst.Add(ProbeDev13History::MakeRun(TEXT("middle"), Runs[1]));
    NewestFirst.Add(ProbeDev13History::MakeRun(TEXT("oldest"), Runs[0]));
    TestTrue(TEXT("Investigation report includes recurring signatures"),
        ProbeDev13History::InvestigationReport(NewestFirst).Contains(TEXT("RECURRING FAILURE SIGNATURES")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev13HistorySafetyAndExport,
    "Probe.Editor.Dev13HistorySafetyAndExport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev13HistorySafetyAndExport::RunTest(const FString& Parameters)
{
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Probe"),
        TEXT("Dev13HistoryTest-") + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const auto Saved = MakeRun(0, Probe::EResultStatus::Passed);
    FString Path;
    TestTrue(TEXT("Valid isolated run saved"), ProbeDev11RunHistory::Save(Directory, Saved, &Path, 10));
    TestTrue(TEXT("Pinned baseline created"), ProbeDev12History::Pin(Directory, Saved));
    const FString Stranger = FPaths::Combine(Directory, TEXT("Unrelated.txt"));
    FFileHelper::SaveStringToFile(TEXT("do-not-delete"), *Stranger);
    TestFalse(TEXT("No arbitrary deletion by path"), ProbeDev13History::DeleteSavedRun(Directory, Stranger));
    TestFalse(TEXT("No deletion outside history directory"), ProbeDev13History::DeleteSavedRun(
        Directory, FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Unrelated.txt"))));
    TestTrue(TEXT("Exact Probe-owned run deletion"), ProbeDev13History::DeleteSavedRun(Directory, Path));
    TestTrue(TEXT("Other files unaffected"), IFileManager::Get().FileExists(*Stranger));
    ProbeDev11RunHistory::FSnapshot Pinned;
    TestTrue(TEXT("Pinned baseline unaffected"), ProbeDev12History::LoadPinned(Directory, Pinned));
    ProbeDev12History::FTrend Trend;
    Trend.AssetPath = TEXT("/Game/Test,\"A\".Test");
    Trend.VariationName = TEXT("A,B");
    Trend.Passes = 2;
    Trend.Problems = 1;
    TArray<ProbeDev12History::FTrend> Trends;
    Trends.Add(Trend);
    const FString Csv = ProbeDev13History::ExportTrendCsv(Trends);
    TestTrue(TEXT("CSV escapes quotes and commas"), Csv.Contains(TEXT("\"/Game/Test,\"\"A\"\".Test\"")));
    TestTrue(TEXT("CSV includes accurate failure rate"), Csv.Contains(TEXT("33.33")));
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev13HistoryBrowserConstruct,
    "Probe.Editor.Dev13HistoryBrowserConstruct",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev13HistoryBrowserConstruct::RunTest(const FString& Parameters)
{
    // Instantiates actual native Slate browser without opening a window or changing any history.
    TSharedRef<ProbeDev13HistoryUI::SBrowser> Browser = SNew(ProbeDev13HistoryUI::SBrowser);
    TestTrue(TEXT("Native browser created and widget tree built"), Browser->GetChildren()->Num() > 0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
