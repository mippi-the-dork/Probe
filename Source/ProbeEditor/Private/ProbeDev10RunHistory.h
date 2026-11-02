#pragma once

#include "ProbeTestRunner.h"
#include "ProbeDev10PackPolicy.h"

// Session-only comparison. Never stores or edits project assets or test data.
namespace ProbeDev10RunHistory
{
    struct FComparison
    {
        int32 Regressions = 0;
        int32 Recovered = 0;
        int32 StillFailing = 0;
        int32 NewlyObserved = 0;
        bool bHasPreviousRun = false;
    };

    inline FString Identity(const Probe::FTestResult& Result)
    {
        return Result.AssetPath + TEXT("#") + FString::FromInt(Result.VariationIndex);
    }

    inline FComparison Compare(const TArray<Probe::FTestResult>& Previous,
        const TArray<Probe::FTestResult>& Current)
    {
        FComparison Comparison;
        if (Previous.IsEmpty()) { return Comparison; }
        Comparison.bHasPreviousRun = true;
        TMap<FString, Probe::EResultStatus> PreviousStatuses;
        for (const Probe::FTestResult& Result : Previous)
        {
            if (!Result.AssetPath.IsEmpty()) { PreviousStatuses.Add(Identity(Result), Result.Status); }
        }
        for (const Probe::FTestResult& Result : Current)
        {
            if (Result.AssetPath.IsEmpty()) { continue; }
            const Probe::EResultStatus* Old = PreviousStatuses.Find(Identity(Result));
            if (!Old) { ++Comparison.NewlyObserved; continue; }
            const bool bPreviousProblem = ProbeDev10PackPolicy::IsProblem(*Old);
            const bool bCurrentProblem = ProbeDev10PackPolicy::IsProblem(Result.Status);
            if (bCurrentProblem && !bPreviousProblem && *Old == Probe::EResultStatus::Passed)
            { ++Comparison.Regressions; }
            else if (bPreviousProblem && Result.Status == Probe::EResultStatus::Passed)
            { ++Comparison.Recovered; }
            else if (bCurrentProblem && bPreviousProblem)
            { ++Comparison.StillFailing; }
        }
        return Comparison;
    }

    inline FString Category(const Probe::FTestResult& Result, const UProbeTestAsset* Asset)
    {
        if (Result.Status == Probe::EResultStatus::Passed) { return TEXT("None"); }
        if (Result.Status == Probe::EResultStatus::Skipped) { return TEXT("Skipped"); }
        for (const Probe::FStepResult& Step : Result.Steps)
        {
            if (!ProbeDev10PackPolicy::IsProblem(Step.Status)) { continue; }
            if (Asset)
            {
                if (!Asset->SetupGroupName.IsEmpty() && Step.GroupName == Asset->SetupGroupName)
                { return TEXT("Setup"); }
                if (!Asset->CleanupGroupName.IsEmpty() && Step.GroupName == Asset->CleanupGroupName)
                { return TEXT("Cleanup"); }
            }
            return Step.Status == Probe::EResultStatus::Error ? TEXT("Infrastructure") : TEXT("Assertion");
        }
        return TEXT("Infrastructure");
    }
}
