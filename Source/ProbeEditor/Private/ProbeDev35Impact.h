#pragma once

#include "ProbeDev34PackReuse.h"

// Pure analysis and edit planning for large mixed-source Packs. The Pack's
// execution order and prerequisite identities remain the authoritative graph.
namespace ProbeDev35
{
    struct FGraphSummary
    {
        int32 Members = 0;
        int32 Links = 0;
        int32 Roots = 0;
        int32 Leaves = 0;
        int32 LongestChain = 0; // count of members, not edges
        int32 UnresolvedLinks = 0;
    };

    inline FGraphSummary Analyze(const TArray<FProbePackMixedEntry>& Entries)
    {
        FGraphSummary Result;
        Result.Members = Entries.Num();
        TMap<FString, int32> IndexByIdentity;
        for (int32 Index = 0; Index < Entries.Num(); ++Index)
        { IndexByIdentity.Add(ProbeDev27::Identity(Entries[Index]), Index); }
        TArray<int32> Depth;
        TArray<int32> DependentCounts;
        Depth.Init(1, Entries.Num());
        DependentCounts.Init(0, Entries.Num());
        for (int32 Index = 0; Index < Entries.Num(); ++Index)
        {
            const FProbePackMixedEntry& Entry = Entries[Index];
            if (Entry.Requires.IsEmpty()) { ++Result.Roots; }
            for (const FString& Dependency : Entry.Requires)
            {
                ++Result.Links;
                const int32* Parent = IndexByIdentity.Find(Dependency);
                if (!Parent || *Parent >= Index)
                { ++Result.UnresolvedLinks; continue; }
                ++DependentCounts[*Parent];
                Depth[Index] = FMath::Max(Depth[Index], Depth[*Parent] + 1);
            }
            Result.LongestChain = FMath::Max(Result.LongestChain, Depth[Index]);
        }
        for (const int32 Count : DependentCounts)
        { if (Count == 0) { ++Result.Leaves; } }
        return Result;
    }

    struct FImpact
    {
        TSet<FString> Selected;
        TSet<FString> Affected; // selected + all transitive dependents
        int32 AffectedOutsideSelection = 0;
        int32 DetachedLinks = 0; // references cleared by selected-only removal
        TArray<FString> AffectedNames; // stable ordered readable report
    };

    inline FImpact AnalyzeRemoval(const TArray<FProbePackMixedEntry>& Entries, const TSet<FString>& Selection)
    {
        FImpact Impact;
        TSet<FString> Known;
        for (const FProbePackMixedEntry& Entry : Entries)
        { Known.Add(ProbeDev27::Identity(Entry)); }
        for (const FString& Id : Selection)
        { if (Known.Contains(Id)) { Impact.Selected.Add(Id); } }
        Impact.Affected = ProbeDev34::ExpandSelection(Entries, Impact.Selected, false);
        for (int32 Index = 0; Index < Entries.Num(); ++Index)
        {
            const FProbePackMixedEntry& Entry = Entries[Index];
            const FString Id = ProbeDev27::Identity(Entry);
            if (Impact.Affected.Contains(Id) && !Impact.Selected.Contains(Id))
            {
                ++Impact.AffectedOutsideSelection;
                Impact.AffectedNames.Add(FString::Printf(TEXT("#%d %s"), Index + 1, *ProbeDev29::DisplayName(Entry)));
            }
            if (!Impact.Selected.Contains(Id))
            {
                for (const FString& Dependency : Entry.Requires)
                { if (Impact.Selected.Contains(Dependency)) { ++Impact.DetachedLinks; } }
            }
        }
        return Impact;
    }

    // Remove the selected members and all downstream dependents, so no retained
    // member silently loses a prerequisite. The plan is computed on a copy;
    // callers own the confirmation, undo transaction, and asset mutation.
    inline int32 PlanRemoveBranch(const TArray<FProbePackMixedEntry>& Entries,
        const TSet<FString>& Selection, TArray<FProbePackMixedEntry>& Out, FImpact& Impact, FString& Error)
    {
        Out = Entries;
        Impact = AnalyzeRemoval(Entries, Selection);
        if (Impact.Selected.IsEmpty())
        { Error = TEXT("Select at least one existing member."); return 0; }
        const int32 Removed = ProbeDev33::PlanRemoval(Out, Impact.Affected);
        if (!ProbeDev33::HasValidOrder(Out, Error))
        { Out = Entries; return 0; }
        Error.Reset();
        return Removed;
    }

    inline FString ImpactText(const FImpact& Impact, const FGraphSummary& Summary)
    {
        FString Result = FString::Printf(TEXT("Pack: %d members, %d prerequisite links, %d roots, %d leaves. Longest chain: %d members.\n\n"),
            Summary.Members, Summary.Links, Summary.Roots, Summary.Leaves, Summary.LongestChain);
        Result += FString::Printf(TEXT("Selected members: %d\nDownstream affected members: %d\nPrerequisite links cleared by Remove Selected: %d\n"),
            Impact.Selected.Num(), Impact.AffectedOutsideSelection, Impact.DetachedLinks);
        if (Summary.UnresolvedLinks > 0)
        { Result += FString::Printf(TEXT("Invalid or unordered links: %d\n"), Summary.UnresolvedLinks); }
        if (!Impact.AffectedNames.IsEmpty())
        {
            Result += TEXT("\nDownstream dependents:\n");
            const int32 Limit = FMath::Min(Impact.AffectedNames.Num(), 24);
            for (int32 Index = 0; Index < Limit; ++Index)
            { Result += FString::Printf(TEXT("  %s\n"), *Impact.AffectedNames[Index]); }
            if (Impact.AffectedNames.Num() > Limit)
            { Result += FString::Printf(TEXT("  ...and %d more\n"), Impact.AffectedNames.Num() - Limit); }
        }
        return Result;
    }
}
