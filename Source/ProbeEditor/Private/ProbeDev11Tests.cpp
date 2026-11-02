#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ProbeDev11RunHistory.h"
#include "ProbeDev10RunHistory.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
    Probe::FTestResult MakeResult(const FString& AssetPath, Probe::EResultStatus Status, int32 Variation = INDEX_NONE)
    {
        Probe::FTestResult Result;
        Result.Id = TEXT("Probe.Dev11.Fixture");
        Result.Name = TEXT("Dev11 History Fixture");
        Result.AssetPath = AssetPath;
        Result.Status = Status;
        Result.VariationIndex = Variation;
        Result.VariationName = Variation == INDEX_NONE ? FString() : TEXT("Alternate");
        Result.Attempts = 2;
        Result.DurationMs = 15.25;
        Result.Detail = TEXT("Expected diagnostics after an explicitly permitted retry");
        Probe::FStepResult Step;
        Step.Index = 3;
        Step.AssertionId = TEXT("Plugin.Installed");
        Step.Status = Status;
        Step.Description = TEXT("Check plugin installation");
        Step.GroupName = TEXT("Verify");
        Step.Detail = TEXT("Observed plugin mismatch");
        Step.DurationMs = 1.75;
        Result.Steps.Add(Step);
        return Result;
    }

    ProbeDev11RunHistory::FSnapshot MakeSnapshot(Probe::EResultStatus Status)
    {
        ProbeDev11RunHistory::FSnapshot Snapshot;
        Snapshot.TimestampUtc = FDateTime::UtcNow();
        Snapshot.EngineVersion = TEXT("5.8.3-Test");
        Snapshot.Results.Add(MakeResult(TEXT("/Game/Probe/HistoryFixture.HistoryFixture"), Status, 2));
        return Snapshot;
    }

    FString TestDirectory()
    {
        return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Probe"),
            FString(TEXT("Dev11HistoryTest-")) + FGuid::NewGuid().ToString(EGuidFormats::Digits));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev11HistorySerialization,
    "Probe.Editor.Dev11HistorySerialization",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev11HistorySerialization::RunTest(const FString& Parameters)
{
    const ProbeDev11RunHistory::FSnapshot Original = MakeSnapshot(Probe::EResultStatus::Failed);
    FString Json;
    TestTrue(TEXT("Versioned history serializes"), ProbeDev11RunHistory::Serialize(Original, Json));
    TestTrue(TEXT("Format marker present"), Json.Contains(TEXT("ProbeRunHistory/1")));
    ProbeDev11RunHistory::FSnapshot Restored;
    TestTrue(TEXT("History deserializes"), ProbeDev11RunHistory::Deserialize(Json, Restored));
    TestEqual(TEXT("One test retained"), Restored.Results.Num(), 1);
    if (Restored.Results.Num() == 1)
    {
        const Probe::FTestResult& Result = Restored.Results[0];
        TestEqual(TEXT("Status retained"), static_cast<int32>(Result.Status), static_cast<int32>(Probe::EResultStatus::Failed));
        TestEqual(TEXT("Variation identity retained"), Result.VariationIndex, 2);
        TestEqual(TEXT("Variation name retained"), Result.VariationName, FString(TEXT("Alternate")));
        TestEqual(TEXT("Attempts retained"), Result.Attempts, 2);
        TestEqual(TEXT("Duration retained"), Result.DurationMs, 15.25);
        TestEqual(TEXT("Detail retained"), Result.Detail, Original.Results[0].Detail);
        TestEqual(TEXT("Step count retained"), Result.Steps.Num(), 1);
        if (Result.Steps.Num() == 1)
        {
            TestEqual(TEXT("Original step number retained"), Result.Steps[0].Index, 3);
            TestEqual(TEXT("Step group retained"), Result.Steps[0].GroupName, FString(TEXT("Verify")));
            TestEqual(TEXT("Step failure retained"), static_cast<int32>(Result.Steps[0].Status), static_cast<int32>(Probe::EResultStatus::Failed));
        }
    }
    TestFalse(TEXT("Corrupt JSON is rejected"), ProbeDev11RunHistory::Deserialize(TEXT("{not-json"), Restored));
    const FString Unsupported = Json.Replace(TEXT("ProbeRunHistory/1"), TEXT("ProbeRunHistory/99"));
    TestFalse(TEXT("Future unknown format is rejected"), ProbeDev11RunHistory::Deserialize(Unsupported, Restored));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev11HistoryRecovery,
    "Probe.Editor.Dev11HistoryRecovery",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev11HistoryRecovery::RunTest(const FString& Parameters)
{
    const FString Directory = TestDirectory();
    const FDateTime Base = FDateTime::UtcNow() - FTimespan::FromDays(2);
    for (int32 Index = 0; Index < ProbeDev11RunHistory::RetainedRuns + 3; ++Index)
    {
        ProbeDev11RunHistory::FSnapshot Snapshot = MakeSnapshot(Index % 2 == 0
            ? Probe::EResultStatus::Passed : Probe::EResultStatus::Failed);
        Snapshot.TimestampUtc = Base + FTimespan::FromSeconds(Index);
        if (!ProbeDev11RunHistory::Save(Directory, Snapshot))
        {
            AddError(TEXT("History fixture failed to save a project-local run."));
            break;
        }
    }
    const TArray<FString> Retained = ProbeDev11RunHistory::ListPaths(Directory);
    TestEqual(TEXT("History directory capped to 30 runs"), Retained.Num(), ProbeDev11RunHistory::RetainedRuns);
    if (Retained.Num() == ProbeDev11RunHistory::RetainedRuns)
    {
        ProbeDev11RunHistory::FSnapshot Latest;
        TestTrue(TEXT("Latest persisted run reloads"), ProbeDev11RunHistory::ReadLatest(Directory, Latest));
        TestEqual(TEXT("Latest run outcome is preserved"), static_cast<int32>(Latest.Results[0].Status),
            static_cast<int32>(Probe::EResultStatus::Passed));
    }
    const FString Corrupt = FPaths::Combine(Directory, TEXT("ProbeHistory-99999999-235959-corrupt.json"));
    FFileHelper::SaveStringToFile(FString(TEXT("{\"broken\":")), *Corrupt);
    ProbeDev11RunHistory::FSnapshot Recovered;
    TestFalse(TEXT("Corrupt newest run is rejected"), ProbeDev11RunHistory::Read(Corrupt, Recovered));
    TestTrue(TEXT("Corrupt newest run does not block last valid run"),
        ProbeDev11RunHistory::ReadLatest(Directory, Recovered));
    TestEqual(TEXT("Recovered run has valid fixture"), Recovered.Results.Num(), 1);
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev11PersistedComparison,
    "Probe.Editor.Dev11PersistedComparison",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev11PersistedComparison::RunTest(const FString& Parameters)
{
    const FString Directory = TestDirectory();
    ProbeDev11RunHistory::FSnapshot Previous = MakeSnapshot(Probe::EResultStatus::Passed);
    Previous.Results.Add(MakeResult(TEXT("/Game/Probe/B.B"), Probe::EResultStatus::Failed));
    Previous.Results.Add(MakeResult(TEXT("/Game/Probe/C.C"), Probe::EResultStatus::Failed));
    TestTrue(TEXT("Baseline persists to disk"), ProbeDev11RunHistory::Save(Directory, Previous));
    ProbeDev11RunHistory::FSnapshot Reloaded;
    TestTrue(TEXT("Baseline restores across simulated workspace restart"),
        ProbeDev11RunHistory::ReadLatest(Directory, Reloaded));
    TArray<Probe::FTestResult> Current = Reloaded.Results;
    Current[0].Status = Probe::EResultStatus::Failed;
    Current[1].Status = Probe::EResultStatus::Passed;
    Current[2].Status = Probe::EResultStatus::Error;
    const ProbeDev10RunHistory::FComparison Comparison = ProbeDev10RunHistory::Compare(Reloaded.Results, Current);
    TestTrue(TEXT("Persisted baseline participates in comparisons"), Comparison.bHasPreviousRun);
    TestEqual(TEXT("One persisted-to-current regression"), Comparison.Regressions, 1);
    TestEqual(TEXT("One persisted-to-current recovery"), Comparison.Recovered, 1);
    TestEqual(TEXT("One previously failing case remains failing"), Comparison.StillFailing, 1);
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
