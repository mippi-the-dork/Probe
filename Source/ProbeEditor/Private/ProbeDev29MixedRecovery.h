#pragma once

#include "ProbeDev27MixedPolicy.h"

// Dev 29: deterministic, data-only execution and recovery decisions.
// Never consider an empty result set successful, and never reuse an interrupted,
// failed, skipped, or incomplete member. Portable Packs may return many results.
namespace ProbeDev29
{
    enum class EMemberState : uint8 { Queued, Starting, Running, Passed, Failed, Skipped, Interrupted, Error, Reused };

    inline const TCHAR* StateLabel(EMemberState State)
    {
        switch (State)
        {
        case EMemberState::Queued: return TEXT("Queued");
        case EMemberState::Starting: return TEXT("Starting");
        case EMemberState::Running: return TEXT("Running");
        case EMemberState::Passed: return TEXT("Passed");
        case EMemberState::Failed: return TEXT("Failed");
        case EMemberState::Skipped: return TEXT("Skipped");
        case EMemberState::Interrupted: return TEXT("Interrupted");
        case EMemberState::Error: return TEXT("Error");
        case EMemberState::Reused: return TEXT("Passed (reused)");
        }
        return TEXT("Unknown");
    }

    inline FString DisplayName(const FProbePackMixedEntry& Entry)
    {
        if (Entry.Source == EProbePackMixedSource::AuthoredTest)
        {
            FString Name = Entry.Locator;
            int32 Dot = INDEX_NONE;
            if (Name.FindLastChar(TEXT('.'), Dot)) { Name = Name.Mid(Dot + 1); }
            return Name;
        }
        return Entry.Locator;
    }

    inline FString ReadableIdentity(const FString& Identity, const TArray<FProbePackMixedEntry>& Entries)
    {
        for (int32 Index = 0; Index < Entries.Num(); ++Index)
        {
            if (ProbeDev27::Identity(Entries[Index]) == Identity)
            { return FString::Printf(TEXT("#%d %s"), Index + 1, *DisplayName(Entries[Index])); }
        }
        return Identity;
    }

    inline bool SameDefinition(const TArray<FProbePackMixedEntry>& A, const TArray<FProbePackMixedEntry>& B)
    {
        if (A.Num() != B.Num()) { return false; }
        for (int32 I = 0; I < A.Num(); ++I)
        {
            if (A[I].Source != B[I].Source || A[I].Locator != B[I].Locator || A[I].Requires != B[I].Requires)
            { return false; }
        }
        return true;
    }

    inline bool AllPassed(const TArray<Probe::FTestResult>& Results)
    {
        if (Results.IsEmpty()) { return false; }
        for (const Probe::FTestResult& Result : Results)
        { if (Result.Status != Probe::EResultStatus::Passed) { return false; } }
        return true;
    }

    inline EMemberState Outcome(const TArray<Probe::FTestResult>& Results)
    {
        if (Results.IsEmpty()) { return EMemberState::Error; }
        if (AllPassed(Results)) { return EMemberState::Passed; }
        for (const Probe::FTestResult& Result : Results)
        { if (Result.Status == Probe::EResultStatus::Error) { return EMemberState::Error; } }
        for (const Probe::FTestResult& Result : Results)
        { if (Result.Status == Probe::EResultStatus::Failed) { return EMemberState::Failed; } }
        for (const Probe::FTestResult& Result : Results)
        { if (Result.Status == Probe::EResultStatus::Interrupted) { return EMemberState::Interrupted; } }
        return EMemberState::Skipped;
    }

    // Recovery is intentionally all-or-nothing: incomplete history cannot be
    // mistaken for an authoritative pass. A changed Pack is never replayed.
    inline bool BuildRetryPlan(const TArray<FProbePackMixedEntry>& CompletedDefinition,
        const TArray<FProbePackMixedEntry>& CurrentDefinition,
        const TMap<FString, TArray<Probe::FTestResult>>& MemberResults,
        TSet<FString>& Reuse, FString& Reason)
    {
        Reuse.Reset(); Reason.Reset();
        if (CompletedDefinition.IsEmpty() || !SameDefinition(CompletedDefinition, CurrentDefinition))
        { Reason = TEXT("The Pack membership, order or dependencies changed since the completed run."); return false; }
        bool bHasWork = false;
        for (const FProbePackMixedEntry& Member : CurrentDefinition)
        {
            const FString Key = ProbeDev27::Identity(Member);
            const TArray<Probe::FTestResult>* Recorded = MemberResults.Find(Key);
            if (!Recorded || Recorded->IsEmpty())
            { Reason = TEXT("The previous Pack run does not have a complete result for every member."); Reuse.Reset(); return false; }
            if (AllPassed(*Recorded)) { Reuse.Add(Key); }
            else { bHasWork = true; }
        }
        if (!bHasWork)
        { Reason = TEXT("All Pack members passed; there is nothing to retry."); Reuse.Reset(); return false; }
        return true;
    }
}
