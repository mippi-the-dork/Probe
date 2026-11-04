#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ProbeDev12History.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

namespace
{
    FString Dev12Directory()
    {
        return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Probe"),
            FString(TEXT("Dev12HistoryTest-")) + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    }
    ProbeDev12History::FSnapshot Snapshot(int32 Offset, Probe::EResultStatus Status,
        int32 Variation = INDEX_NONE)
    {
        ProbeDev12History::FSnapshot Run;
        Run.TimestampUtc = FDateTime::UtcNow() - FTimespan::FromMinutes(300 - Offset);
        Run.EngineVersion = TEXT("5.8.3-Test");
        Run.PackPath = TEXT("/Game/QA/ProbePack.ProbePack");
        Probe::FTestResult Result;
        Result.Name = TEXT("Actor Test");
        Result.AssetPath = TEXT("/Game/QA/ActorTest.ActorTest");
        Result.VariationIndex = Variation;
        Result.VariationName = Variation == INDEX_NONE ? FString() : TEXT("Alternative");
        Result.Status = Status;
        Run.Results.Add(MoveTemp(Result));
        return Run;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev12RetentionAndClear,
    "Probe.Editor.Dev12RetentionAndClear",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev12RetentionAndClear::RunTest(const FString& Parameters)
{
    const FString Directory = Dev12Directory();
    for (int32 Index = 0; Index < 16; ++Index)
    {
        TestTrue(TEXT("Test snapshot saved"), ProbeDev11RunHistory::Save(Directory,
            Snapshot(Index, Probe::EResultStatus::Passed), nullptr, 10));
    }
    TestEqual(TEXT("Ten recent runs retained"), ProbeDev11RunHistory::ListPaths(Directory).Num(), 10);
    ProbeDev12History::FSnapshot Latest;
    TestTrue(TEXT("Newest retained run readable"), ProbeDev11RunHistory::ReadLatest(Directory, Latest));
    TestTrue(TEXT("Newest run from most recent time"), Latest.TimestampUtc > FDateTime::UtcNow() - FTimespan::FromMinutes(287));
    // An unrelated file and an intentionally pinned run are never pruned.
    const FString Unrelated = FPaths::Combine(Directory, TEXT("Unrelated.txt"));
    FFileHelper::SaveStringToFile(TEXT("keep"), *Unrelated);
    TestTrue(TEXT("Pinned baseline written"), ProbeDev12History::Pin(Directory,
        Snapshot(0, Probe::EResultStatus::Failed)));
    ProbeDev11RunHistory::Prune(Directory, 5);
    TestEqual(TEXT("Reducing limit prunes oldest runs"), ProbeDev11RunHistory::ListPaths(Directory).Num(), 5);
    ProbeDev12History::FSnapshot Pinned;
    TestTrue(TEXT("Pinned baseline survives pruning"), ProbeDev12History::LoadPinned(Directory, Pinned));
    TestTrue(TEXT("Unrelated files survive pruning"), IFileManager::Get().FileExists(*Unrelated));
    ProbeDev11RunHistory::Prune(Directory, 50, 1);
    TestEqual(TEXT("Disk budget prevents accumulation"), ProbeDev11RunHistory::ListPaths(Directory).Num(), 0);
    TestTrue(TEXT("Budget prune preserves pin"), ProbeDev12History::LoadPinned(Directory, Pinned));
    TestTrue(TEXT("Clear history deletes owned records"), ProbeDev12History::ClearHistory(Directory));
    TestEqual(TEXT("All history records removed"), ProbeDev11RunHistory::ListPaths(Directory).Num(), 0);
    TestFalse(TEXT("Pinned baseline cleared"), ProbeDev12History::LoadPinned(Directory, Pinned));
    TestTrue(TEXT("Unrelated files preserved by clear"), IFileManager::Get().FileExists(*Unrelated));
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev12Trends,
    "Probe.Editor.Dev12HistoricalTrends",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev12Trends::RunTest(const FString& Parameters)
{
    TArray<ProbeDev12History::FSnapshot> Runs;
    Runs.Add(Snapshot(0, Probe::EResultStatus::Passed, 1));
    Runs.Add(Snapshot(1, Probe::EResultStatus::Failed, 1));
    Runs.Add(Snapshot(2, Probe::EResultStatus::Skipped, 1));
    Runs.Add(Snapshot(3, Probe::EResultStatus::Passed, 1));
    // Independent variation must not change the first variation's trend.
    Runs[3].Results.Add(Snapshot(3, Probe::EResultStatus::Error, 2).Results[0]);
    const TArray<ProbeDev12History::FTrend> Trends = ProbeDev12History::Analyze(Runs);
    TestEqual(TEXT("Each variation analyzed separately"), Trends.Num(), 2);
    for (const ProbeDev12History::FTrend& Trend : Trends)
    {
        if (Trend.Identity.EndsWith(TEXT("#1")))
        {
            TestEqual(TEXT("Two passing observations"), Trend.Passes, 2);
            TestEqual(TEXT("One failing observation"), Trend.Problems, 1);
            TestEqual(TEXT("Skipped observation excluded"), Trend.Skipped, 1);
            TestEqual(TEXT("Two opposite transitions"), Trend.Transitions, 2);
            TestTrue(TEXT("Alternating test is identified as flaky"), Trend.IsFlaky());
            TestTrue(TEXT("Failure rate excludes skipped"), FMath::IsNearlyEqual(Trend.FailureRate(), 100.0 / 3.0));
        }
        else { TestFalse(TEXT("Single observation not flaky"), Trend.IsFlaky()); }
    }
    const FString Report = ProbeDev12History::TrendReport(Trends, Runs.Num());
    TestTrue(TEXT("Report identifies flakiness"), Report.Contains(TEXT("[FLAKY]")));
    TestTrue(TEXT("Report includes sample totals"), Report.Contains(TEXT("2 passed, 1 failed/error")));
    TestEqual(TEXT("Non-matching filter gives zero identities"),
        ProbeDev12History::Analyze(Runs, TEXT("no-such-asset")).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev12FilteringAndPin,
    "Probe.Editor.Dev12FilteringAndPin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev12FilteringAndPin::RunTest(const FString& Parameters)
{
    const FString Directory = Dev12Directory();
    const ProbeDev12History::FSnapshot Failed = Snapshot(0, Probe::EResultStatus::Failed, 3);
    const ProbeDev12History::FSnapshot Passed = Snapshot(1, Probe::EResultStatus::Passed);
    using Filter = ProbeDev12History::EHistoryStatusFilter;
    TestTrue(TEXT("Pack path matches filter"), ProbeDev12History::Matches(Failed, TEXT("ProbePack"), Filter::All));
    TestTrue(TEXT("Variation name matches filter"), ProbeDev12History::Matches(Failed, TEXT("Alternative"), Filter::All));
    TestTrue(TEXT("Asset path matches filter"), ProbeDev12History::Matches(Passed, TEXT("ActorTest"), Filter::All));
    TestFalse(TEXT("Non-matching name excluded"), ProbeDev12History::Matches(Failed, TEXT("Missing"), Filter::All));
    TestTrue(TEXT("Problem status matches failures"), ProbeDev12History::Matches(Failed, TEXT(""), Filter::Problems));
    TestFalse(TEXT("Passing run excludes failure filter"), ProbeDev12History::Matches(Passed, TEXT(""), Filter::Problems));
    TestTrue(TEXT("Passing run matches success filter"), ProbeDev12History::Matches(Passed, TEXT(""), Filter::PassedOnly));
    TestTrue(TEXT("Pinned run written"), ProbeDev12History::Pin(Directory, Failed));
    ProbeDev12History::FSnapshot Reloaded;
    TestTrue(TEXT("Pinned run restored"), ProbeDev12History::LoadPinned(Directory, Reloaded));
    TestEqual(TEXT("Pinned Pack provenance retained"), Reloaded.PackPath, Failed.PackPath);
    TestTrue(TEXT("Unpin succeeds"), ProbeDev12History::Unpin(Directory));
    TestFalse(TEXT("Unpinned baseline unavailable"), ProbeDev12History::LoadPinned(Directory, Reloaded));
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
