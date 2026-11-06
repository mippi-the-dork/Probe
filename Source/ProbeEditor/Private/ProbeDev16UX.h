#pragma once

#include "ProbeTestAsset.h"
#include "ProbeDev15Recipes.h"
#include "ProbeDev14Authoring.h"

// Dev 16 presentation-only model. Tests/Packs and their serialized properties remain unchanged.
namespace ProbeDev16
{
    struct FGroup
    {
        FString Name;
        int32 First = INDEX_NONE;
        int32 Last = INDEX_NONE;
        int32 Num() const { return Last >= First && First != INDEX_NONE ? Last - First + 1 : 0; }
    };

    inline bool IsGroupStart(const TArray<FProbeTestStep>& Steps, int32 Index)
    {
        if (!Steps.IsValidIndex(Index) || Steps[Index].GroupName.TrimStartAndEnd().IsEmpty()) { return false; }
        return Index == 0 || Steps[Index - 1].GroupName.TrimStartAndEnd() != Steps[Index].GroupName.TrimStartAndEnd();
    }

    inline FGroup GroupAt(const TArray<FProbeTestStep>& Steps, int32 Index)
    {
        FGroup Group;
        if (!Steps.IsValidIndex(Index)) { return Group; }
        Group.Name = Steps[Index].GroupName.TrimStartAndEnd();
        if (Group.Name.IsEmpty()) { return Group; }
        Group.First = Index;
        Group.Last = Index;
        while (Group.First > 0 && Steps[Group.First - 1].GroupName.TrimStartAndEnd() == Group.Name) { --Group.First; }
        while (Group.Last + 1 < Steps.Num() && Steps[Group.Last + 1].GroupName.TrimStartAndEnd() == Group.Name) { ++Group.Last; }
        return Group;
    }

    // Group instance identity, rather than name alone, supports separate groups with the same label.
    inline FString GroupKey(const FGroup& Group)
    { return FString::Printf(TEXT("%d:%s"), Group.First, *Group.Name); }

    inline bool IsContiguousSelection(const TSet<int32>& Selected, int32 Total, FGroup& OutRange)
    {
        OutRange = FGroup();
        if (Selected.IsEmpty() || Total <= 0) { return false; }
        TArray<int32> Ordered = Selected.Array();
        Ordered.Sort();
        if (Ordered[0] < 0 || Ordered.Last() >= Total) { return false; }
        for (int32 I = 1; I < Ordered.Num(); ++I)
        { if (Ordered[I] != Ordered[I - 1] + 1) { return false; } }
        OutRange.First = Ordered[0];
        OutRange.Last = Ordered.Last();
        return true;
    }

    inline bool RenameGroup(UProbeTestAsset& Test, int32 Index, const FString& RequestedName)
    {
        const FGroup Group = GroupAt(Test.Steps, Index);
        const FString Name = RequestedName.TrimStartAndEnd();
        if (!Group.Num() || Name.IsEmpty() || Name.Len() > 96) { return false; }
        // Keep workflow boundaries attached to the renamed contiguous group.
        // A same-name group elsewhere must not steal the Setup/Cleanup reference.
        const bool bSetup = Group.First == 0 && Test.SetupGroupName.TrimStartAndEnd() == Group.Name;
        const bool bCleanup = Group.Last == Test.Steps.Num() - 1
            && Test.CleanupGroupName.TrimStartAndEnd() == Group.Name;
        for (int32 I = Group.First; I <= Group.Last; ++I) { Test.Steps[I].GroupName = Name; }
        if (bSetup) { Test.SetupGroupName = Name; }
        if (bCleanup) { Test.CleanupGroupName = Name; }
        return true;
    }

    inline FString TestSummary(const UProbeTestAsset& Test)
    {
        const FString Readiness = Test.bNeedsConfiguration ? TEXT("Needs Review")
            : (ProbeDev14::PreflightTest(Test).Blocking > 0 ? TEXT("Invalid") : TEXT("Ready"));
        return FString::Printf(TEXT("%d steps  |  %s  |  %s  |  UI %s  |  Actors %s"),
            Test.Steps.Num(), *Readiness, Test.bEnabled ? TEXT("Enabled") : TEXT("Disabled"),
            Test.bAllowEditorUIInput ? TEXT("Authorized") : TEXT("Not authorized"),
            Test.bAllowTransientEditorActors ? TEXT("Authorized") : TEXT("Not authorized"));
    }

    // The recipe file functions must never mutate Test Assets or authorization flags.
    inline bool RenameRecipe(const FString& OldName, const FString& NewName, FString& Error)
    {
        Error.Reset();
        FString OldCanonical, NewCanonical;
        if (!ProbeDev15Recipes::CanonicalName(OldName, OldCanonical) ||
            !ProbeDev15Recipes::CanonicalName(NewName, NewCanonical))
        { Error = TEXT("Invalid recipe name."); return false; }
        if (OldCanonical.Equals(NewCanonical, ESearchCase::IgnoreCase))
        { Error = TEXT("Choose a different recipe name."); return false; }
        const FString Source = ProbeDev15Recipes::FilePath(OldCanonical);
        const FString Destination = ProbeDev15Recipes::FilePath(NewCanonical);
        if (!IFileManager::Get().FileExists(*Source))
        { Error = TEXT("The original recipe no longer exists."); return false; }
        if (IFileManager::Get().FileExists(*Destination))
        { Error = TEXT("A recipe with that name already exists."); return false; }
        if (!IFileManager::Get().Move(*Destination, *Source, false, false))
        { Error = TEXT("Could not rename the recipe file."); return false; }
        return true;
    }

    inline bool DuplicateRecipe(const FString& OldName, const FString& NewName, FString& Error)
    {
        TArray<FProbeTestStep> Steps;
        if (!ProbeDev15Recipes::Read(OldName, Steps, Error)) { return false; }
        if (Steps.IsEmpty()) { Error = TEXT("Cannot duplicate an empty recipe."); return false; }
        ProbeStepGroups::FRange Range;
        Range.First = 0;
        Range.Last = Steps.Num() - 1;
        return ProbeDev15Recipes::Save(Steps, Range, NewName, false, Error);
    }

    inline bool DeleteRecipe(const FString& Name, FString& Error)
    {
        Error.Reset();
        FString Canonical;
        if (!ProbeDev15Recipes::CanonicalName(Name, Canonical))
        { Error = TEXT("Invalid recipe name."); return false; }
        const FString File = ProbeDev15Recipes::FilePath(Canonical);
        if (!IFileManager::Get().FileExists(*File))
        { Error = TEXT("Recipe no longer exists."); return false; }
        if (!IFileManager::Get().Delete(*File, false, false, true))
        { Error = TEXT("Cannot remove saved recipe."); return false; }
        return true;
    }
}
