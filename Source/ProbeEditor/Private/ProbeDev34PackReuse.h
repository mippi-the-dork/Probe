#pragma once

#include "ProbeDev33PackAuthoring.h"

// Plan-only reusable Pack import. Imports snapshots of references, never runtime
// nested execution or implicit provider permissions. No UObjects are mutated.
namespace ProbeDev34
{
    struct FImportReport
    {
        int32 Added = 0;
        int32 Existing = 0;
        int32 DependencyLinks = 0;
        FString Error;
    };

    inline bool Normalize(const UProbeTestPack& Source, TArray<FProbePackMixedEntry>& Out, FString& Error)
    {
        Out.Reset();
        Error.Reset();
        if (!Source.MixedEntries.IsEmpty())
        {
            if (!ProbeDev27::ValidatePack(Source, Error)) { return false; }
            Out = Source.MixedEntries;
            return true;
        }
        TSet<FString> Seen;
        for (const TSoftObjectPtr<UProbeTestAsset>& Ref : Source.Tests)
        {
            FProbePackMixedEntry Member;
            Member.Source = EProbePackMixedSource::AuthoredTest;
            Member.Locator = Ref.ToString();
            if (!ProbeDev27::ValidateEntry(Member, Error)) { return false; }
            if (Seen.Contains(ProbeDev27::Identity(Member)))
            { Error = TEXT("Source Pack has duplicate authored members."); return false; }
            Seen.Add(ProbeDev27::Identity(Member));
            Out.Add(MoveTemp(Member));
        }
        for (const FProbePackPrerequisite& Rule : Source.Prerequisites)
        {
            const FString Target = Rule.Test.ToString();
            const FString Requires = Rule.Requires.ToString();
            const int32 TargetIndex = Out.IndexOfByPredicate([&](const FProbePackMixedEntry& Member) { return Member.Locator == Target; });
            const int32 RequiresIndex = Out.IndexOfByPredicate([&](const FProbePackMixedEntry& Member) { return Member.Locator == Requires; });
            if (TargetIndex == INDEX_NONE || RequiresIndex == INDEX_NONE || RequiresIndex >= TargetIndex)
            { Error = TEXT("Source authored Pack has a missing or out-of-order prerequisite."); return false; }
            const FString RequiredId = ProbeDev27::Identity(Out[RequiresIndex]);
            if (Out[TargetIndex].Requires.Contains(RequiredId))
            { Error = TEXT("Source authored Pack has duplicate prerequisites."); return false; }
            Out[TargetIndex].Requires.Add(RequiredId);
        }
        return ProbeDev33::HasValidOrder(Out, Error);
    }

    inline bool PlanImport(const UProbeTestPack& Destination, const UProbeTestPack& Source,
        TArray<FProbePackMixedEntry>& Out, FImportReport& Report)
    {
        Report = FImportReport();
        Out.Reset();
        if (&Destination == &Source)
        { Report.Error = TEXT("Cannot import a Pack into itself."); return false; }
        TArray<FProbePackMixedEntry> Incoming;
        if (!Normalize(Source, Incoming, Report.Error)) { return false; }
        if (!Normalize(Destination, Out, Report.Error)) { return false; }
        if (Incoming.IsEmpty())
        { Report.Error = TEXT("Source Pack contains no members."); return false; }
        TSet<FString> Existing;
        for (const FProbePackMixedEntry& Member : Out) { Existing.Add(ProbeDev27::Identity(Member)); }
        for (const FProbePackMixedEntry& Member : Incoming)
        {
            const FString Id = ProbeDev27::Identity(Member);
            if (Existing.Contains(Id)) { ++Report.Existing; continue; }
            Existing.Add(Id);
            Out.Add(Member);
            ++Report.Added;
            Report.DependencyLinks += Member.Requires.Num();
        }
        if (!Report.Added)
        { Report.Error = TEXT("Every member from this Pack already exists in the destination."); return false; }
        // Existing duplicate definitions must not be silently overwritten. If a
        // source member requires edges absent from the destination copy, refuse
        // reuse rather than accidentally weakening the imported dependency graph.
        for (const FProbePackMixedEntry& Member : Incoming)
        {
            const FString Id = ProbeDev27::Identity(Member);
            const int32 Found = Out.IndexOfByPredicate([&](const FProbePackMixedEntry& Other)
            { return ProbeDev27::Identity(Other) == Id; });
            if (Found == INDEX_NONE) { Report.Error = TEXT("Import identity resolution failed."); return false; }
            for (const FString& Required : Member.Requires)
            {
                if (!Out[Found].Requires.Contains(Required))
                {
                    Report.Error = FString::Printf(TEXT("Existing member %s has different prerequisites; import refused."), *Id);
                    return false;
                }
            }
        }
        if (!ProbeDev33::HasValidOrder(Out, Report.Error)) { return false; }
        return true;
    }

    // Resolve transitive upstream/downstream closure. Original selections are
    // retained. A bounded fixed-point pass also terminates for corrupt cycles.
    inline TSet<FString> ExpandSelection(const TArray<FProbePackMixedEntry>& Entries,
        const TSet<FString>& Selected, bool bSelectPrerequisites)
    {
        TSet<FString> Result = Selected;
        for (int32 Pass = 0; Pass < Entries.Num(); ++Pass)
        {
            bool bChanged = false;
            for (const FProbePackMixedEntry& Member : Entries)
            {
                const FString Id = ProbeDev27::Identity(Member);
                if (bSelectPrerequisites && Result.Contains(Id))
                {
                    for (const FString& Required : Member.Requires)
                    {
                        if (!Result.Contains(Required)) { Result.Add(Required); bChanged = true; }
                    }
                }
                else if (!bSelectPrerequisites && !Result.Contains(Id))
                {
                    for (const FString& Required : Member.Requires)
                    {
                        if (Result.Contains(Required)) { Result.Add(Id); bChanged = true; break; }
                    }
                }
            }
            if (!bChanged) { break; }
        }
        return Result;
    }

    inline FString DependencySummary(const TArray<FProbePackMixedEntry>& Entries, int32 Index)
    {
        if (!Entries.IsValidIndex(Index)) { return FString(); }
        const FProbePackMixedEntry& Member = Entries[Index];
        TArray<FString> PrerequisiteNames;
        for (const FString& Id : Member.Requires)
        {
            const int32 Found = Entries.IndexOfByPredicate([&](const FProbePackMixedEntry& Other)
            { return ProbeDev27::Identity(Other) == Id; });
            PrerequisiteNames.Add(Found == INDEX_NONE ? FString::Printf(TEXT("Missing: %s"), *Id) :
                FString::Printf(TEXT("#%d %s"), Found+1, *ProbeDev29::DisplayName(Entries[Found])));
        }
        int32 DependentCount = 0;
        const FString OwnId = ProbeDev27::Identity(Member);
        for (const FProbePackMixedEntry& Other : Entries)
        { if (Other.Requires.Contains(OwnId)) { ++DependentCount; } }
        FString Text = PrerequisiteNames.IsEmpty() ? TEXT("No prerequisites") :
            FString::Printf(TEXT("Requires: %s"), *FString::Join(PrerequisiteNames, TEXT(", ")));
        Text += FString::Printf(TEXT(" | Required by %d"), DependentCount);
        return Text;
    }
}
