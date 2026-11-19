#pragma once

#include "ProbeTestPack.h"
#include "ProbeTestRunner.h"

// Data-only mixed execution decisions; no loading, worker mutation, or provider
// execution is performed by validation. Missing providers fail during dispatch.
namespace ProbeDev27
{
    inline bool HasRunMembers(const UProbeTestPack* Pack)
    {
        return Pack && (!Pack->MixedEntries.IsEmpty() || !Pack->Tests.IsEmpty());
    }

    inline FString Identity(const FProbePackMixedEntry& Entry)
    {
        return FString::Printf(TEXT("%d:%s"), static_cast<int32>(Entry.Source), *Entry.Locator);
    }

    inline bool ValidateEntry(const FProbePackMixedEntry& Entry, FString& Error)
    {
        Error.Reset();
        if (Entry.Locator.TrimStartAndEnd().IsEmpty())
        { Error = TEXT("Mixed Pack member has an empty locator."); return false; }
        if (Entry.Locator != Entry.Locator.TrimStartAndEnd())
        { Error = TEXT("Mixed Pack locator contains leading/trailing whitespace."); return false; }
        if (Entry.Locator.Contains(TEXT("\n")) || Entry.Locator.Contains(TEXT("\r")))
        { Error = TEXT("Mixed Pack locator must be a single line."); return false; }
        switch (Entry.Source)
        {
        case EProbePackMixedSource::AuthoredTest:
            if (!Entry.Locator.StartsWith(TEXT("/")) || !Entry.Locator.Contains(TEXT(".")))
            { Error = TEXT("Authored member requires a full Test asset object path."); return false; }
            break;
        case EProbePackMixedSource::PortableTest:
        case EProbePackMixedSource::PortablePack:
            if (Entry.Locator.StartsWith(TEXT("/")) || Entry.Locator.StartsWith(TEXT("probe-json://")))
            { Error = TEXT("Portable member requires a provider definition ID, not an asset/URI path."); return false; }
            break;
        case EProbePackMixedSource::NativeAutomation:
            if (Entry.Locator.StartsWith(TEXT("/")) || Entry.Locator.StartsWith(TEXT("unreal-automation://")))
            { Error = TEXT("Native member requires an exact Automation full path."); return false; }
            break;
        default: Error = TEXT("Unknown mixed source type."); return false;
        }
        return true;
    }

    inline bool ValidateMemberDependencies(const UProbeTestPack& Pack, int32 Index, FString& Error)
    {
        Error.Reset();
        if (!Pack.MixedEntries.IsValidIndex(Index)) { Error = TEXT("Invalid member index"); return false; }
        const FProbePackMixedEntry& Member = Pack.MixedEntries[Index];
        TSet<FString> Earlier;
        for (int32 Previous = 0; Previous < Index; ++Previous) { Earlier.Add(Identity(Pack.MixedEntries[Previous])); }
        TSet<FString> Seen;
        for (const FString& Dependency : Member.Requires)
        {
            if (Dependency.IsEmpty() || Dependency == Identity(Member) || Seen.Contains(Dependency) || !Earlier.Contains(Dependency))
            { Error = FString::Printf(TEXT("Prerequisite '%s' must be a distinct, preceding Pack member"), *Dependency); return false; }
            Seen.Add(Dependency);
        }
        return true;
    }

    inline bool ValidatePack(const UProbeTestPack& Pack, FString& Error)
    {
        Error.Reset();
        if (Pack.MixedEntries.IsEmpty()) { return true; }
        if (!Pack.Tests.IsEmpty() || !Pack.Prerequisites.IsEmpty())
        { Error = TEXT("Mixed Pack cannot also contain legacy Tests or prerequisites. Convert the authored list first."); return false; }
        TSet<FString> Identities;
        for (int32 Index=0; Index<Pack.MixedEntries.Num(); ++Index)
        {
            if (!ValidateEntry(Pack.MixedEntries[Index], Error))
            { Error = FString::Printf(TEXT("Entry %d: %s"), Index+1, *Error); return false; }
            const FString Id = Identity(Pack.MixedEntries[Index]);
            if (Identities.Contains(Id))
            { Error = FString::Printf(TEXT("Duplicate mixed Pack entry at position %d: %s"), Index+1, *Id); return false; }
            if (!ValidateMemberDependencies(Pack, Index, Error))
            { Error = FString::Printf(TEXT("Entry %d: %s"), Index+1, *Error); return false; }
            Identities.Add(Id);
        }
        return true;
    }

    // Distinguish dependency-blocked skips from ordinary failures. Do not infer
    // success from presence of a result or from accepting an execution request.
    inline FString FailedDependency(const FProbePackMixedEntry& Member, const TMap<FString, bool>& Outcomes)
    {
        for (const FString& Dependency : Member.Requires)
        {
            const bool* bPassed = Outcomes.Find(Dependency);
            if (!bPassed || !*bPassed) { return Dependency; }
        }
        return FString();
    }

    inline bool ShouldStop(EProbePackFailurePolicy Policy, Probe::EResultStatus Status)
    {
        return Policy == EProbePackFailurePolicy::StopOnFailure &&
            (Status == Probe::EResultStatus::Failed || Status == Probe::EResultStatus::Error);
    }
}
