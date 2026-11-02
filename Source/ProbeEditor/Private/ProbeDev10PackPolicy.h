#pragma once

#include "ProbeTestRunner.h"

// Pure, side-effect-free pack contract shared by synchronous RunPack, the
// asynchronous workspace queue, and native UE Automation Framework tests.
namespace ProbeDev10PackPolicy
{
    inline bool IsProblem(Probe::EResultStatus Status)
    {
        return Status == Probe::EResultStatus::Failed || Status == Probe::EResultStatus::Error;
    }

    inline bool ShouldStop(const UProbeTestPack& Pack, Probe::EResultStatus Status)
    {
        return Pack.FailurePolicy == EProbePackFailurePolicy::StopOnFailure && IsProblem(Status);
    }

    inline bool Validate(const UProbeTestPack& Pack, FString& Error)
    {
        Error.Reset();
        TMap<FString, int32> Indices;
        for (int32 Index = 0; Index < Pack.Tests.Num(); ++Index)
        {
            const FString Path = Pack.Tests[Index].ToString();
            if (Path.IsEmpty())
            {
                // Preserve legacy behavior: a Pack without dependencies may
                // report individual missing references while continuing others.
                if (!Pack.Prerequisites.IsEmpty())
                {
                    Error = FString::Printf(TEXT("Pack test %d has an empty reference."), Index + 1);
                    return false;
                }
                continue;
            }
            if (Indices.Contains(Path))
            {
                // Legacy Packs may intentionally repeat a member. This is
                // ambiguous only when explicit prerequisites are configured.
                if (!Pack.Prerequisites.IsEmpty())
                {
                    Error = FString::Printf(TEXT("Duplicate Pack member is ambiguous with prerequisites: %s"), *Path);
                    return false;
                }
                continue;
            }
            Indices.Add(Path, Index);
        }
        TSet<FString> Pairs;
        for (const FProbePackPrerequisite& Dependency : Pack.Prerequisites)
        {
            const FString TestPath = Dependency.Test.ToString();
            const FString RequiredPath = Dependency.Requires.ToString();
            const int32* TestIndex = Indices.Find(TestPath);
            const int32* RequiredIndex = Indices.Find(RequiredPath);
            if (!TestIndex || !RequiredIndex)
            {
                Error = FString::Printf(TEXT("Prerequisite references a test outside this Pack: %s requires %s"),
                    *TestPath, *RequiredPath);
                return false;
            }
            if (*RequiredIndex >= *TestIndex)
            {
                Error = FString::Printf(TEXT("Prerequisite must appear before its dependent test: %s requires %s"),
                    *TestPath, *RequiredPath);
                return false;
            }
            const FString Pair = TestPath + TEXT("|") + RequiredPath;
            if (Pairs.Contains(Pair))
            {
                Error = FString::Printf(TEXT("Duplicate prerequisite: %s requires %s"), *TestPath, *RequiredPath);
                return false;
            }
            Pairs.Add(Pair);
        }
        return true;
    }

    inline bool DependenciesPassed(const UProbeTestPack& Pack, const FString& Path,
        const TArray<Probe::FTestResult>& Results, FString& Reason)
    {
        Reason.Reset();
        for (const FProbePackPrerequisite& Dependency : Pack.Prerequisites)
        {
            if (Dependency.Test.ToString() != Path) { continue; }
            const FString RequiredPath = Dependency.Requires.ToString();
            int32 Matches = 0;
            for (const Probe::FTestResult& Result : Results)
            {
                if (Result.AssetPath != RequiredPath) { continue; }
                ++Matches;
                if (Result.Status != Probe::EResultStatus::Passed)
                {
                    Reason = FString::Printf(TEXT("Prerequisite not passed: %s (%s)."),
                        *RequiredPath, Probe::StatusName(Result.Status));
                    return false;
                }
            }
            if (!Matches)
            {
                Reason = FString::Printf(TEXT("Prerequisite has no execution result: %s."), *RequiredPath);
                return false;
            }
        }
        return true;
    }

    // A retry must not repeat any live Editor input, selection operation, actor
    // fixture mutation, object fixture mutation, or provider-defined assertion.
    // Conservative rejection is preferable to replaying an action twice.
    inline bool CanRetryReadOnly(const UProbeTestAsset& Test)
    {
        if (Test.bAllowEditorUIInput || Test.bAllowTransientEditorActors) { return false; }
        for (const FProbeTestStep& Step : Test.Steps)
        {
            if (!Step.bEnabled) { continue; }
            if (Step.AssertionId != NAME_None) { return false; }
            const int32 Type = static_cast<int32>(Step.Type);
            if (Type >= static_cast<int32>(EProbeStepType::FixtureCreateObject)
                && Type <= static_cast<int32>(EProbeStepType::FixtureDestroyObject)) { return false; }
            if (Type >= static_cast<int32>(EProbeStepType::SelectionClear)
                && Type <= static_cast<int32>(EProbeStepType::ActorFixtureScaleNotNearlyEquals)) { return false; }
            if (Step.Type == EProbeStepType::UIWidgetClick || Step.Type == EProbeStepType::UIWidgetTypeText
                || Step.Type == EProbeStepType::UIWidgetAppendText || Step.Type == EProbeStepType::UIWidgetClearText) { return false; }
        }
        return true;
    }

    inline bool ShouldRetry(const UProbeTestPack& Pack, const UProbeTestAsset& Test,
        Probe::EResultStatus Status, int32 CompletedAttempts)
    {
        return Pack.bRetryReadOnlyFailuresOnce && Status == Probe::EResultStatus::Failed
            && CompletedAttempts == 1 && CanRetryReadOnly(Test);
    }
}
