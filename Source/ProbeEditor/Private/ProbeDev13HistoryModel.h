#pragma once

#include "ProbeDev12History.h"

// Read-only project history investigation and safe per-run maintenance.
// No Test Asset, Pack, or authorization state is modified here.
namespace ProbeDev13History
{
    using FSnapshot = ProbeDev11RunHistory::FSnapshot;
    using EStatusFilter = ProbeDev12History::EHistoryStatusFilter;

    inline bool IsProblem(Probe::EResultStatus Status)
    { return Status == Probe::EResultStatus::Failed || Status == Probe::EResultStatus::Error; }

    struct FRun
    {
        FString Path;
        FSnapshot Snapshot;
        int64 Bytes = 0;
        int32 Passed = 0;
        int32 Problems = 0;
        int32 Skipped = 0;
        int32 Errors = 0;
        bool HasProblems() const { return Problems > 0; }
        FString Label() const
        {
            return FString::Printf(TEXT("%s  [%s]  |  %d passed, %d problems, %d skipped"),
                *Snapshot.TimestampUtc.ToString(TEXT("%Y-%m-%d %H:%M:%S UTC")),
                *FPaths::GetBaseFilename(Path).Right(8), Passed, Problems, Skipped);
        }
    };

    inline FRun MakeRun(const FString& Path, const FSnapshot& Snapshot, int64 Bytes = 0)
    {
        FRun Run;
        Run.Path = Path;
        Run.Snapshot = Snapshot;
        Run.Bytes = Bytes;
        for (const Probe::FTestResult& Result : Snapshot.Results)
        {
            if (Result.Status == Probe::EResultStatus::Passed) { ++Run.Passed; }
            else if (Result.Status == Probe::EResultStatus::Skipped) { ++Run.Skipped; }
            else if (IsProblem(Result.Status))
            {
                ++Run.Problems;
                if (Result.Status == Probe::EResultStatus::Error) { ++Run.Errors; }
            }
        }
        return Run;
    }

    inline TArray<FRun> Load(const FString& Directory)
    {
        TArray<FRun> Rows;
        for (const FString& Path : ProbeDev11RunHistory::ListPaths(Directory))
        {
            FSnapshot Snapshot;
            if (ProbeDev11RunHistory::Read(Path, Snapshot))
            { Rows.Add(MakeRun(Path, Snapshot, IFileManager::Get().FileSize(*Path))); }
        }
        return Rows; // Newest first, just like Probe's history file listing.
    }

    inline bool MatchesWithTimestamp(const FRun& Run, const FString& Search, EStatusFilter Filter)
    {
        if (Filter == EStatusFilter::Problems && !Run.HasProblems()) { return false; }
        if (Filter == EStatusFilter::PassedOnly && Run.HasProblems()) { return false; }
        if (Search.TrimStartAndEnd().IsEmpty()) { return true; }
        return Run.Snapshot.TimestampUtc.ToString(TEXT("%Y-%m-%d %H:%M:%S")).Contains(Search, ESearchCase::IgnoreCase)
            || ProbeDev12History::Matches(Run.Snapshot, Search, EStatusFilter::All);
    }

    enum class EDeltaKind : uint8 { NewFailure, Recovered, StillFailing, NewlyObserved, Unchanged, Skipped };
    struct FDelta
    {
        FString Identity;
        FString Name;
        FString AssetPath;
        FString VariationName;
        EDeltaKind Kind = EDeltaKind::Unchanged;
        Probe::EResultStatus Current = Probe::EResultStatus::Skipped;
        FString Detail;
    };
    inline FString DeltaLabel(EDeltaKind Kind)
    {
        switch (Kind)
        {
        case EDeltaKind::NewFailure: return TEXT("REGRESSION");
        case EDeltaKind::Recovered: return TEXT("RECOVERED");
        case EDeltaKind::StillFailing: return TEXT("STILL FAILING");
        case EDeltaKind::NewlyObserved: return TEXT("NEW TEST");
        case EDeltaKind::Skipped: return TEXT("SKIPPED");
        default: return TEXT("UNCHANGED");
        }
    }
    inline TArray<FDelta> Compare(const FSnapshot& Previous, const FSnapshot& Current)
    {
        TMap<FString, Probe::EResultStatus> Prior;
        for (const Probe::FTestResult& Result : Previous.Results)
        { if (!Result.AssetPath.IsEmpty()) { Prior.Add(ProbeDev10RunHistory::Identity(Result), Result.Status); } }
        TArray<FDelta> Deltas;
        TSet<FString> Seen;
        for (const Probe::FTestResult& Result : Current.Results)
        {
            if (Result.AssetPath.IsEmpty()) { continue; }
            const FString Id = ProbeDev10RunHistory::Identity(Result);
            if (Seen.Contains(Id)) { continue; }
            Seen.Add(Id);
            FDelta Row;
            Row.Identity = Id; Row.Name = Result.Name; Row.AssetPath = Result.AssetPath;
            Row.VariationName = Result.VariationName; Row.Current = Result.Status; Row.Detail = Result.Detail;
            const Probe::EResultStatus* Before = Prior.Find(Id);
            if (Result.Status == Probe::EResultStatus::Skipped) { Row.Kind = EDeltaKind::Skipped; }
            else if (!Before) { Row.Kind = EDeltaKind::NewlyObserved; }
            else if (IsProblem(Result.Status) && *Before == Probe::EResultStatus::Passed)
            { Row.Kind = EDeltaKind::NewFailure; }
            else if (Result.Status == Probe::EResultStatus::Passed && IsProblem(*Before))
            { Row.Kind = EDeltaKind::Recovered; }
            else if (IsProblem(Result.Status) && IsProblem(*Before))
            { Row.Kind = EDeltaKind::StillFailing; }
            Deltas.Add(MoveTemp(Row));
        }
        Deltas.Sort([](const FDelta& A, const FDelta& B)
        {
            if (A.Kind != B.Kind) { return static_cast<uint8>(A.Kind) < static_cast<uint8>(B.Kind); }
            return A.Identity < B.Identity;
        });
        return Deltas;
    }

    struct FIncident
    {
        FString Signature;
        FString AssetPath;
        FString VariationName;
        FString Assertion;
        FString Group;
        FString MostRecentDetail;
        int32 RunsAffected = 0;
        FDateTime LatestUtc;
    };

    // Signature is based on stable authored identity (not unstable observed values).
    // Each run contributes at most one observation for a particular signature.
    inline TArray<FIncident> RecurringFailures(const TArray<FSnapshot>& OldestFirst)
    {
        TArray<FIncident> Incidents;
        TMap<FString, int32> Lookup;
        for (const FSnapshot& Snapshot : OldestFirst)
        {
            TSet<FString> SeenInRun;
            for (const Probe::FTestResult& Result : Snapshot.Results)
            {
                if (Result.AssetPath.IsEmpty() || !IsProblem(Result.Status)) { continue; }
                const Probe::FStepResult* First = nullptr;
                for (const Probe::FStepResult& Step : Result.Steps)
                { if (IsProblem(Step.Status)) { First = &Step; break; } }
                const FString Assertion = First ? First->AssertionId : TEXT("<test/infrastructure>");
                const FString Group = First ? First->GroupName : FString();
                const int32 StepIndex = First ? First->Index : INDEX_NONE;
                const FString Signature = FString::Printf(TEXT("%s|%d|%d|%s|%s"),
                    *Result.AssetPath, Result.VariationIndex, StepIndex, *Assertion, *Group);
                if (SeenInRun.Contains(Signature)) { continue; }
                SeenInRun.Add(Signature);
                int32* Found = Lookup.Find(Signature);
                if (!Found)
                {
                    const int32 Index = Incidents.AddDefaulted();
                    Lookup.Add(Signature, Index);
                    FIncident& Incident = Incidents[Index];
                    Incident.Signature = Signature; Incident.AssetPath = Result.AssetPath;
                    Incident.VariationName = Result.VariationName;
                    Incident.Assertion = Assertion; Incident.Group = Group;
                    Found = Lookup.Find(Signature);
                }
                FIncident& Incident = Incidents[*Found];
                ++Incident.RunsAffected;
                Incident.LatestUtc = Snapshot.TimestampUtc;
                Incident.MostRecentDetail = First ? First->Detail : Result.Detail;
            }
        }
        Incidents.Sort([](const FIncident& A, const FIncident& B)
        {
            if (A.RunsAffected != B.RunsAffected) { return A.RunsAffected > B.RunsAffected; }
            return A.Signature < B.Signature;
        });
        return Incidents;
    }

    inline FString CsvEscape(const FString& Text)
    {
        FString Out = Text.Replace(TEXT("\""), TEXT("\"\""));
        return TEXT("\"") + Out + TEXT("\"");
    }
    inline FString ExportTrendCsv(const TArray<ProbeDev12History::FTrend>& Trends)
    {
        FString Result = TEXT("Asset,Variation,Passes,Failures,Skipped,Failure Rate %,Transitions,Flaky\r\n");
        for (const auto& Trend : Trends)
        {
            Result += FString::Printf(TEXT("%s,%s,%d,%d,%d,%.2f,%d,%s\r\n"),
                *CsvEscape(Trend.AssetPath), *CsvEscape(Trend.VariationName), Trend.Passes,
                Trend.Problems, Trend.Skipped, Trend.FailureRate(), Trend.Transitions,
                Trend.IsFlaky() ? TEXT("true") : TEXT("false"));
        }
        return Result;
    }

    inline FString InvestigationReport(const TArray<FRun>& NewestFirst)
    {
        TArray<FSnapshot> Chronological;
        for (int32 Index = NewestFirst.Num() - 1; Index >= 0; --Index)
        { Chronological.Add(NewestFirst[Index].Snapshot); }
        const TArray<ProbeDev12History::FTrend> Trends = ProbeDev12History::Analyze(Chronological);
        const TArray<FIncident> Incidents = RecurringFailures(Chronological);
        FString Output = ProbeDev12History::TrendReport(Trends, Chronological.Num());
        Output += TEXT("\nRECURRING FAILURE SIGNATURES\n");
        if (Incidents.IsEmpty()) { Output += TEXT("No failure signatures in the retained runs.\n"); }
        for (const FIncident& Incident : Incidents)
        {
            Output += FString::Printf(TEXT("%d run(s) | %s%s | %s | %s\n"), Incident.RunsAffected,
                *Incident.AssetPath,
                Incident.VariationName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [%s]"), *Incident.VariationName),
                *Incident.Assertion, *Incident.MostRecentDetail);
        }
        return Output;
    }

    // Only delete an exact path returned by Probe's own history enumerator.
    // Callers must obtain explicit user confirmation before calling this method.
    inline bool DeleteSavedRun(const FString& Directory, const FString& SelectedPath)
    {
        if (SelectedPath.IsEmpty()) { return false; }
        if (!ProbeDev11RunHistory::ListPaths(Directory).Contains(SelectedPath)) { return false; }
        return IFileManager::Get().Delete(*SelectedPath, false, false, true);
    }
}
