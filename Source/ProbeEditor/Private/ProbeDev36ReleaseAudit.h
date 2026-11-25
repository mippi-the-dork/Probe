#pragma once

#include "ProbeDev35Impact.h"

// Release-oriented, read-only Pack audit. Distinguish what the Editor can
// establish now from portable/provider and AutomationController dispatch-time
// conditions. The callback may inspect authored assets, but may not execute.
namespace ProbeDev36
{
    enum class EAvailability : uint8
    {
        Available,       // Resolved and preflighted in this process
        Missing,         // Explicitly proven absent or invalid
        NeedsConfiguration,
        Deferred         // Runtime/provider/worker state cannot be proven here
    };

    enum class ESeverity : uint8 { Blocker, Warning, Information };

    struct FFinding
    {
        ESeverity Severity = ESeverity::Information;
        int32 MemberIndex = INDEX_NONE; // 0-based; INDEX_NONE is Pack-level
        FString Code;
        FString Detail;
    };

    struct FReport
    {
        FString PackName;
        int32 Members = 0;
        int32 Blockers = 0;
        int32 Warnings = 0;
        int32 Deferred = 0;
        int32 Validated = 0;
        int32 PrerequisiteLinks = 0;
        bool bLegacy = false;
        TArray<FFinding> Findings;

        void Add(ESeverity Severity, int32 Index, const TCHAR* Code, const FString& Detail)
        {
            FFinding& Item = Findings.AddDefaulted_GetRef();
            Item.Severity = Severity;
            Item.MemberIndex = Index;
            Item.Code = Code;
            Item.Detail = Detail;
            if (Severity == ESeverity::Blocker) { ++Blockers; }
            else if (Severity == ESeverity::Warning) { ++Warnings; }
        }

        bool IsStructurallyReady() const { return Blockers == 0 && Members > 0; }
        FString ToText() const
        {
            FString Out = TEXT("PROBE PACK RELEASE AUDIT\n");
            Out += FString::Printf(TEXT("Pack: %s\nFormat: %s\nMembers: %d | Prerequisites: %d\nBlockers: %d | Warnings: %d | Deferred checks: %d | Locally validated: %d\n"),
                *PackName, bLegacy ? TEXT("Legacy authored") : TEXT("Mixed sources"), Members, PrerequisiteLinks,
                Blockers, Warnings, Deferred, Validated);
            Out += FString::Printf(TEXT("Definition gate: %s\n\n"), IsStructurallyReady() ? TEXT("PASS") : TEXT("BLOCKED"));
            for (const FFinding& Finding : Findings)
            {
                const TCHAR* Label = Finding.Severity == ESeverity::Blocker ? TEXT("BLOCKER") :
                    Finding.Severity == ESeverity::Warning ? TEXT("WARNING") : TEXT("INFO");
                const FString Location = Finding.MemberIndex == INDEX_NONE ? TEXT("PACK") :
                    FString::Printf(TEXT("#%d"), Finding.MemberIndex + 1);
                Out += FString::Printf(TEXT("[%s] %s %s: %s\n"), Label, *Location, *Finding.Code, *Finding.Detail);
            }
            Out += TEXT("\nScope: read-only definition audit. Deferred means NOT VERIFIED, never passed. This does not execute tests, grant permissions, or establish worker/provider availability.\n");
            return Out;
        }
    };

    template <typename AvailabilityFn>
    FReport Audit(const UProbeTestPack& Pack, AvailabilityFn&& Resolve)
    {
        FReport R;
        R.PackName = Pack.GetName();
        R.bLegacy = Pack.MixedEntries.IsEmpty();
        TArray<FProbePackMixedEntry> Members;
        if (R.bLegacy)
        {
            // Legacy normalization validates duplicate references and ordering,
            // without mutating the original asset or converting its schema.
            FString Error;
            if (!ProbeDev34::Normalize(Pack, Members, Error))
            {
                R.Add(ESeverity::Blocker, INDEX_NONE, TEXT("LEGACY_SCHEMA"), Error);
                // Still inspect individual authored refs where possible.
                Members.Reset();
                for (const TSoftObjectPtr<UProbeTestAsset>& Ref : Pack.Tests)
                {
                    FProbePackMixedEntry Entry;
                    Entry.Source = EProbePackMixedSource::AuthoredTest;
                    Entry.Locator = Ref.ToString();
                    Members.Add(MoveTemp(Entry));
                }
            }
        }
        else
        {
            Members = Pack.MixedEntries;
            if (!Pack.Tests.IsEmpty() || !Pack.Prerequisites.IsEmpty())
            { R.Add(ESeverity::Blocker, INDEX_NONE, TEXT("MIXED_LEGACY_CONFLICT"), TEXT("Mixed members cannot coexist with legacy Tests or prerequisites.")); }
        }
        R.Members = Members.Num();
        if (Members.IsEmpty())
        { R.Add(ESeverity::Blocker, INDEX_NONE, TEXT("EMPTY_PACK"), TEXT("Pack contains no executable members.")); }

        TSet<FString> Seen;
        for (int32 Index = 0; Index < Members.Num(); ++Index)
        {
            const FProbePackMixedEntry& Member = Members[Index];
            FString Error;
            const bool bDefinitionValid = ProbeDev27::ValidateEntry(Member, Error);
            if (!bDefinitionValid)
            { R.Add(ESeverity::Blocker, Index, TEXT("INVALID_MEMBER"), Error); }
            const FString Identity = ProbeDev27::Identity(Member);
            if (Seen.Contains(Identity))
            { R.Add(ESeverity::Blocker, Index, TEXT("DUPLICATE_MEMBER"), Identity); }
            Seen.Add(Identity);
            TSet<FString> LocalRequires;
            for (const FString& Required : Member.Requires)
            {
                ++R.PrerequisiteLinks;
                if (Required == Identity || LocalRequires.Contains(Required) || !Seen.Contains(Required))
                { R.Add(ESeverity::Blocker, Index, TEXT("INVALID_PREREQUISITE"), FString::Printf(TEXT("%s must be a distinct preceding member"), *Required)); }
                LocalRequires.Add(Required);
            }
            if (!bDefinitionValid) { continue; }
            const EAvailability State = Resolve(Member);
            switch (State)
            {
            case EAvailability::Available: ++R.Validated; break;
            case EAvailability::Missing:
                R.Add(ESeverity::Blocker, Index, TEXT("MISSING_TARGET"), Member.Locator); break;
            case EAvailability::NeedsConfiguration:
                R.Add(ESeverity::Blocker, Index, TEXT("NEEDS_CONFIGURATION"), Member.Locator); break;
            case EAvailability::Deferred:
                ++R.Deferred;
                R.Add(ESeverity::Information, Index, TEXT("DISPATCH_CHECK_PENDING"),
                    FString::Printf(TEXT("%s: provider/Automation worker will be verified when dispatched"), *Member.Locator));
                break;
            }
        }
        if (R.bLegacy && !Pack.Prerequisites.IsEmpty() && R.PrerequisiteLinks == 0 && R.Blockers > 0)
        { R.Add(ESeverity::Information, INDEX_NONE, TEXT("LEGACY_LINKS"), TEXT("Legacy prerequisite rules could not be normalized; see schema blocker.")); }
        return R;
    }
}
