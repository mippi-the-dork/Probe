#pragma once

#include "ProbeStepGroups.h"
#include "ProbeTestRunner.h"
#include "ProbeDev9Workflow.h"

// Pure decision/formatting helpers shared by the Results Explorer and native
// regression tests. No permissions, execution or persistent state are changed.
namespace ProbeDev8Workflow
{
    inline bool IsProblem(Probe::EResultStatus Status)
    {
        return Status == Probe::EResultStatus::Failed || Status == Probe::EResultStatus::Error;
    }

    struct FFailureScope
    {
        Probe::FRunOptions Options;
        int32 FailedStepNumber = INDEX_NONE;
        FString Explanation;
        bool bValid = false;
    };

    // Index is displayed one-based in Probe results but is zero-based in an asset.
    // An error without a recorded failed step is not silently rerun as a group.
    inline FFailureScope FirstFailedGroup(const Probe::FTestResult& Result, const UProbeTestAsset* Asset)
    {
        FFailureScope Output;
        if (!Asset || !IsProblem(Result.Status) || Result.AssetPath != Asset->GetPathName())
        {
            Output.Explanation = TEXT("Retry unavailable: result is not a failure from this Test Asset.");
            return Output;
        }
        TArray<FProbeTestStep> ResolvedSteps;
        if (Result.VariationIndex != INDEX_NONE)
        {
            FString Name, Error;
            if (!ProbeDev9Workflow::Resolve(*Asset, Result.VariationIndex, ResolvedSteps, Name, Error)
                || Name != Result.VariationName)
            {
                Output.Explanation = TEXT("Retry unavailable: the recorded variation has changed or is invalid.");
                return Output;
            }
        }
        for (const Probe::FStepResult& Step : Result.Steps)
        {
            if (!IsProblem(Step.Status)) { continue; }
            const int32 Index = Step.Index - 1;
            if (!Asset->Steps.IsValidIndex(Index))
            {
                Output.Explanation = TEXT("Retry unavailable: failed step no longer exists in the Test Asset.");
                return Output;
            }
            // Prevent a stale result from silently executing a different assertion
            // after steps have been reordered or their type has changed.
            const FProbeTestStep& Authored = Result.VariationIndex == INDEX_NONE
                ? Asset->Steps[Index] : ResolvedSteps[Index];
            if (Authored.GroupName != Step.GroupName ||
                (!Authored.Description.IsEmpty() && Authored.Description != Step.Description))
            {
                Output.Explanation = TEXT("Retry unavailable: the recorded step or group has changed.");
                return Output;
            }
            if (!Authored.bEnabled)
            {
                Output.Explanation = TEXT("Retry unavailable: failed step is currently disabled.");
                return Output;
            }
            const FString CurrentId = Authored.AssertionId.IsNone()
                ? FString::Printf(TEXT("Probe.BuiltIn.%s"),
                    *StaticEnum<EProbeStepType>()->GetNameStringByValue(static_cast<int64>(Authored.Type)))
                : Authored.AssertionId.ToString();
            if (Step.AssertionId != CurrentId)
            {
                Output.Explanation = TEXT("Retry unavailable: the failed assertion type has changed.");
                return Output;
            }
            const ProbeStepGroups::FRange Range = ProbeStepGroups::FindContiguous(Asset->Steps, Index);
            if (!Range.IsValid())
            {
                Output.Explanation = TEXT("Retry unavailable: cannot resolve the failed group.");
                return Output;
            }
            Output.Options.FirstStepIndex = Range.First;
            Output.Options.LastStepIndex = Range.Last;
            Output.Options.VariationIndex = Result.VariationIndex;
            Output.FailedStepNumber = Step.Index;
            Output.Explanation = FString::Printf(TEXT("Retry steps %d-%d only. Earlier setup steps will NOT execute."),
                Range.First + 1, Range.Last + 1);
            Output.bValid = true;
            return Output;
        }
        Output.Explanation = TEXT("Retry unavailable: the test failed before any individual step failed. Rerun the full test instead.");
        return Output;
    }

    // Compact, actionable clipboard report. Does not copy successful fixtures,
    // authorization flags, or entire verbose logs. Run-order is preserved.
    inline FString BuildFailureSummary(const TArray<Probe::FTestResult>& Results)
    {
        FString Text = TEXT("Probe failure summary\n");
        int32 Count = 0;
        for (const Probe::FTestResult& Test : Results)
        {
            if (!IsProblem(Test.Status)) { continue; }
            ++Count;
            Text += FString::Printf(TEXT("\n[%s] %s\nAsset: %s\nDuration: %.2f ms\n%s\n"),
                Probe::StatusName(Test.Status), *Test.Name, *Test.AssetPath,
                Test.DurationMs, *Test.Detail);
            for (const Probe::FStepResult& Step : Test.Steps)
            {
                if (!IsProblem(Step.Status)) { continue; }
                Text += FString::Printf(TEXT("  [%s] Step %d: %s%s (%.2f ms)\n  %s\n"),
                    Probe::StatusName(Step.Status), Step.Index, *Step.Description,
                    Step.GroupName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [Group: %s]"), *Step.GroupName),
                    Step.DurationMs, *Step.Detail);
            }
            if (Text.Len() > 60000)
            {
                Text += TEXT("\n[Output truncated. Use Export for full JSON diagnostics.]\n");
                break;
            }
        }
        if (Count == 0) { return TEXT("Probe failure summary: no failures or errors in this run."); }
        return Text;
    }

    // Duplicate the source block directly after itself, snapshotting it before
    // TArray insertion. Make named copied groups distinct to avoid merging with
    // an adjacent original group. Permissions belong to the asset, not its steps.
    inline bool DuplicateGroup(UProbeTestAsset& Asset, int32 Index, ProbeStepGroups::FRange& Out)
    {
        Out = {};
        const ProbeStepGroups::FRange Range = ProbeStepGroups::FindContiguous(Asset.Steps, Index);
        if (!Range.IsValid() || Range.Num() > 256 || Asset.Steps.Num() + Range.Num() > 4096) { return false; }
        TArray<FProbeTestStep> Copies;
        for (int32 I = Range.First; I <= Range.Last; ++I) { Copies.Add(Asset.Steps[I]); }
        const FString Name = Copies[0].GroupName.TrimStartAndEnd();
        if (!Name.IsEmpty())
        {
            TSet<FString> Existing;
            for (const FProbeTestStep& Step : Asset.Steps) { Existing.Add(Step.GroupName.TrimStartAndEnd()); }
            FString NewName = Name.Left(88) + TEXT(" Copy");
            for (int32 Suffix = 2; Existing.Contains(NewName) && Suffix <= 10000; ++Suffix)
            {
                NewName = FString::Printf(TEXT("%s Copy %d"), *Name.Left(80), Suffix);
            }
            if (Existing.Contains(NewName)) { return false; }
            for (FProbeTestStep& Step : Copies) { Step.GroupName = NewName; }
        }
        const int32 InsertAt = Range.Last + 1;
        for (int32 I = 0; I < Copies.Num(); ++I)
        {
            Asset.Steps.Insert(Copies[I], InsertAt + I);
        }
        Out.First = InsertAt;
        Out.Last = InsertAt + Copies.Num() - 1;
        return true;
    }
}
