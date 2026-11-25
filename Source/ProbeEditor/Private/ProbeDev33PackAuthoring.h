#pragma once

#include "ProbeDev27MixedPolicy.h"
#include "ProbeDev29MixedRecovery.h"

// Data-only authoring operations. All edits are planned on copies before UObjects
// or transactions are touched; invalid dependency order never mutates the Pack.
namespace ProbeDev33
{
    inline bool Matches(const FProbePackMixedEntry& Member, const FString& Query, int32 SourceFilter)
    {
        if (SourceFilter == 1 && Member.Source != EProbePackMixedSource::AuthoredTest) { return false; }
        if (SourceFilter == 2 && Member.Source != EProbePackMixedSource::PortableTest && Member.Source != EProbePackMixedSource::PortablePack) { return false; }
        if (SourceFilter == 3 && Member.Source != EProbePackMixedSource::NativeAutomation) { return false; }
        if (Query.IsEmpty()) { return true; }
        if (Member.Locator.Contains(Query, ESearchCase::IgnoreCase) ||
            ProbeDev29::DisplayName(Member).Contains(Query, ESearchCase::IgnoreCase)) { return true; }
        const TCHAR* Source = Member.Source == EProbePackMixedSource::AuthoredTest ? TEXT("authored") :
            Member.Source == EProbePackMixedSource::NativeAutomation ? TEXT("native unreal automation") :
            Member.Source == EProbePackMixedSource::PortableTest ? TEXT("portable test") : TEXT("portable pack");
        return FString(Source).Contains(Query, ESearchCase::IgnoreCase);
    }

    inline TArray<FString> VisibleIdentities(const TArray<FProbePackMixedEntry>& Entries, const FString& Query, int32 SourceFilter)
    {
        TArray<FString> Out;
        for (const FProbePackMixedEntry& Member : Entries)
        { if (Matches(Member, Query, SourceFilter)) { Out.Add(ProbeDev27::Identity(Member)); } }
        return Out;
    }

    inline bool HasValidOrder(const TArray<FProbePackMixedEntry>& Entries, FString& Error)
    {
        TSet<FString> Earlier;
        for (int32 Index = 0; Index < Entries.Num(); ++Index)
        {
            const FProbePackMixedEntry& Member = Entries[Index];
            const FString Id = ProbeDev27::Identity(Member);
            if (Earlier.Contains(Id)) { Error = TEXT("Duplicate member identity"); return false; }
            for (const FString& Dependency : Member.Requires)
            {
                if (!Earlier.Contains(Dependency))
                {
                    Error = FString::Printf(TEXT("Cannot move #%d (%s) before its prerequisite (%s)."),
                        Index + 1, *ProbeDev29::DisplayName(Member), *Dependency);
                    return false;
                }
            }
            Earlier.Add(Id);
        }
        Error.Reset();
        return true;
    }

    // Simultaneous one-position movement: selected members preserve their
    // relative order, and adjacent selected members travel as a block.
    inline bool PlanMove(const TArray<FProbePackMixedEntry>& Entries, const TSet<FString>& Selected,
        int32 Delta, TArray<FProbePackMixedEntry>& Out, FString& Error)
    {
        Out = Entries;
        if (Delta != -1 && Delta != 1) { Error = TEXT("Invalid move direction."); return false; }
        bool bChanged = false;
        if (Delta < 0)
        {
            for (int32 Index = 1; Index < Out.Num(); ++Index)
            {
                if (Selected.Contains(ProbeDev27::Identity(Out[Index])) &&
                    !Selected.Contains(ProbeDev27::Identity(Out[Index - 1])))
                { Out.Swap(Index, Index - 1); bChanged = true; }
            }
        }
        else
        {
            for (int32 Index = Out.Num() - 2; Index >= 0; --Index)
            {
                if (Selected.Contains(ProbeDev27::Identity(Out[Index])) &&
                    !Selected.Contains(ProbeDev27::Identity(Out[Index + 1])))
                { Out.Swap(Index, Index + 1); bChanged = true; }
            }
        }
        if (!bChanged) { Error = TEXT("Selected members are already at the boundary."); return false; }
        if (!HasValidOrder(Out, Error)) { Out = Entries; return false; }
        Error.Reset();
        return true;
    }

    inline int32 PlanRemoval(TArray<FProbePackMixedEntry>& Entries, const TSet<FString>& Selected)
    {
        const int32 Before = Entries.Num();
        Entries.RemoveAll([&Selected](const FProbePackMixedEntry& Member)
        { return Selected.Contains(ProbeDev27::Identity(Member)); });
        for (FProbePackMixedEntry& Member : Entries)
        { Member.Requires.RemoveAll([&Selected](const FString& Dependency) { return Selected.Contains(Dependency); }); }
        return Before - Entries.Num();
    }
}
