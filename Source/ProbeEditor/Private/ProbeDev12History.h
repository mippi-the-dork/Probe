#pragma once

#include "ProbeDev11RunHistory.h"
#include "ProbeDev10RunHistory.h"

// Project-local historical analysis. The saved run format remains ProbeRunHistory/1.
// Deliberately independent of the editor UI and never mutates authored assets.
namespace ProbeDev12History
{
    using FSnapshot = ProbeDev11RunHistory::FSnapshot;
    enum class EHistoryStatusFilter : uint8 { All, Problems, PassedOnly };

    inline FString PinnedPath(const FString& Directory)
    { return FPaths::Combine(Directory, TEXT("ProbePinnedBaseline.json")); }

    inline bool Pin(const FString& Directory, const FSnapshot& Snapshot)
    {
        FString Json;
        if (!ProbeDev11RunHistory::Serialize(Snapshot, Json) || Json.Len() > ProbeDev11RunHistory::MaxFileBytes
            || !IFileManager::Get().MakeDirectory(*Directory, true)) { return false; }
        return FFileHelper::SaveStringToFile(Json, *PinnedPath(Directory),
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    inline bool LoadPinned(const FString& Directory, FSnapshot& Out)
    { return ProbeDev11RunHistory::Read(PinnedPath(Directory), Out); }

    inline bool Unpin(const FString& Directory)
    {
        const FString Path = PinnedPath(Directory);
        return !IFileManager::Get().FileExists(*Path) || IFileManager::Get().Delete(*Path, false, false, true);
    }

    // Called only after affirmative user confirmation. Targets exact Probe-owned
    // filenames, not the entire directory or other reports and fixtures.
    inline bool ClearHistory(const FString& Directory)
    {
        bool bSuccess = true;
        for (const FString& Path : ProbeDev11RunHistory::ListPaths(Directory))
        { bSuccess &= IFileManager::Get().Delete(*Path, false, false, true); }
        bSuccess &= Unpin(Directory);
        return bSuccess;
    }

    inline bool HasProblem(const FSnapshot& Snapshot)
    {
        for (const Probe::FTestResult& Result : Snapshot.Results)
        {
            if (Result.Status == Probe::EResultStatus::Failed || Result.Status == Probe::EResultStatus::Error)
            { return true; }
        }
        return false;
    }

    inline bool Matches(const FSnapshot& Snapshot, const FString& Search,
        EHistoryStatusFilter StatusFilter)
    {
        if (StatusFilter == EHistoryStatusFilter::Problems && !HasProblem(Snapshot)) { return false; }
        if (StatusFilter == EHistoryStatusFilter::PassedOnly && HasProblem(Snapshot)) { return false; }
        if (Search.TrimStartAndEnd().IsEmpty()
            || Snapshot.PackPath.Contains(Search, ESearchCase::IgnoreCase)) { return true; }
        for (const Probe::FTestResult& Result : Snapshot.Results)
        {
            if (Result.Name.Contains(Search, ESearchCase::IgnoreCase)
                || Result.AssetPath.Contains(Search, ESearchCase::IgnoreCase)
                || Result.VariationName.Contains(Search, ESearchCase::IgnoreCase)) { return true; }
        }
        return false;
    }

    struct FTrend
    {
        FString Identity;
        FString Name;
        FString AssetPath;
        FString VariationName;
        int32 Passes = 0;
        int32 Problems = 0;
        int32 Skipped = 0;
        int32 Transitions = 0; // Between adjacent observed pass/problem outcomes.
        bool bHasPrior = false;
        bool bPriorProblem = false;

        int32 Samples() const { return Passes + Problems; }
        double FailureRate() const { return Samples() > 0 ? 100.0 * Problems / Samples() : 0.0; }
        bool IsFlaky() const { return Passes > 0 && Problems > 0 && Transitions >= 2; }
    };

    // Entries must be oldest-first; skipped steps do not create false state
    // transitions. Each run contributes at most one outcome per identity.
    inline TArray<FTrend> Analyze(const TArray<FSnapshot>& OldestFirst, const FString& Search = FString())
    {
        TArray<FTrend> Rows;
        TMap<FString, int32> ByIdentity;
        for (const FSnapshot& Snapshot : OldestFirst)
        {
            TSet<FString> SeenInRun;
            for (const Probe::FTestResult& Result : Snapshot.Results)
            {
                if (Result.AssetPath.IsEmpty()) { continue; }
                if (!Search.TrimStartAndEnd().IsEmpty()
                    && !Result.Name.Contains(Search, ESearchCase::IgnoreCase)
                    && !Result.AssetPath.Contains(Search, ESearchCase::IgnoreCase)
                    && !Result.VariationName.Contains(Search, ESearchCase::IgnoreCase)) { continue; }
                const FString Key = ProbeDev10RunHistory::Identity(Result);
                if (SeenInRun.Contains(Key)) { continue; }
                SeenInRun.Add(Key);
                int32* Existing = ByIdentity.Find(Key);
                int32 Index = INDEX_NONE;
                if (!Existing)
                {
                    Index = Rows.AddDefaulted();
                    ByIdentity.Add(Key, Index);
                    FTrend& Row = Rows[Index];
                    Row.Identity = Key; Row.Name = Result.Name; Row.AssetPath = Result.AssetPath;
                    Row.VariationName = Result.VariationName;
                }
                else { Index = *Existing; }
                FTrend& Row = Rows[Index];
                if (Result.Status == Probe::EResultStatus::Skipped) { ++Row.Skipped; continue; }
                const bool bProblem = Result.Status == Probe::EResultStatus::Error || Result.Status == Probe::EResultStatus::Failed;
                if (bProblem) { ++Row.Problems; } else { ++Row.Passes; }
                if (Row.bHasPrior && Row.bPriorProblem != bProblem) { ++Row.Transitions; }
                Row.bHasPrior = true;
                Row.bPriorProblem = bProblem;
            }
        }
        Rows.Sort([](const FTrend& A, const FTrend& B)
        {
            if (A.IsFlaky() != B.IsFlaky()) { return A.IsFlaky(); }
            if (A.FailureRate() != B.FailureRate()) { return A.FailureRate() > B.FailureRate(); }
            return A.Identity < B.Identity;
        });
        return Rows;
    }

    inline TArray<FSnapshot> LoadChronological(const FString& Directory)
    {
        TArray<FSnapshot> Snapshots;
        const TArray<FString> Paths = ProbeDev11RunHistory::ListPaths(Directory);
        for (const FString& Path : Paths)
        {
            FSnapshot Snapshot;
            if (ProbeDev11RunHistory::Read(Path, Snapshot)) { Snapshots.Add(MoveTemp(Snapshot)); }
        }
        Snapshots.Sort([](const FSnapshot& A, const FSnapshot& B)
        { return A.TimestampUtc < B.TimestampUtc; });
        return Snapshots;
    }

    inline FString TrendReport(const TArray<FTrend>& Trends, int32 RunCount)
    {
        FString Output = FString::Printf(TEXT("Probe Historical Trends | %d saved run(s) | %d test identity/variation(s)\n"),
            RunCount, Trends.Num());
        Output += TEXT("Skipped outcomes are excluded from failure rates. Flaky requires 2+ pass/fail transitions.\n\n");
        if (Trends.IsEmpty()) { return Output + TEXT("No matching historical test results.\n"); }
        for (const FTrend& Trend : Trends)
        {
            Output += FString::Printf(TEXT("%s %s%s | %d passed, %d failed/error, %d skipped | %.1f%% failures | %d transitions\n"),
                Trend.IsFlaky() ? TEXT("[FLAKY]") : TEXT("[TREND]"), *Trend.AssetPath,
                Trend.VariationName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [%s]"), *Trend.VariationName),
                Trend.Passes, Trend.Problems, Trend.Skipped, Trend.FailureRate(), Trend.Transitions);
        }
        return Output;
    }
}
