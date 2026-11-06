#pragma once

#include "ProbeStepGroups.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

// Project-local, author-controlled reusable step recipes. Reuses the validated
// ProbeStepGroup/1 format; no Test flags or execution permissions are serialized.
namespace ProbeDev15Recipes
{
    inline FString Directory()
    { return FPaths::ProjectSavedDir() / TEXT("Probe/Recipes"); }

    inline bool CanonicalName(const FString& InName, FString& OutName)
    {
        OutName = InName.TrimStartAndEnd();
        if (OutName.IsEmpty() || OutName.Len() > 64) { return false; }
        for (int32 I = 0; I < OutName.Len(); ++I)
        {
            TCHAR& C = OutName[I];
            if (C == TEXT(' ')) { C = TEXT('_'); continue; }
            if ((C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('a') && C <= TEXT('z'))
                || (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('_') || C == TEXT('-'))
            { continue; }
            return false;
        }
        const FString Upper = OutName.ToUpper();
        if (Upper == TEXT("CON") || Upper == TEXT("PRN") || Upper == TEXT("AUX")
            || Upper == TEXT("NUL") || Upper == TEXT("COM1") || Upper == TEXT("COM2")
            || Upper == TEXT("COM3") || Upper == TEXT("COM4") || Upper == TEXT("COM5")
            || Upper == TEXT("COM6") || Upper == TEXT("COM7") || Upper == TEXT("COM8")
            || Upper == TEXT("COM9") || Upper == TEXT("LPT1") || Upper == TEXT("LPT2")
            || Upper == TEXT("LPT3") || Upper == TEXT("LPT4") || Upper == TEXT("LPT5")
            || Upper == TEXT("LPT6") || Upper == TEXT("LPT7") || Upper == TEXT("LPT8")
            || Upper == TEXT("LPT9")) { return false; }
        return true;
    }

    inline FString FilePath(const FString& Canonical)
    { return Directory() / (Canonical + TEXT(".json")); }

    inline bool Save(const TArray<FProbeTestStep>& Steps, const ProbeStepGroups::FRange& Range,
        const FString& Name, bool bOverwrite, FString& Error)
    {
        Error.Reset();
        FString Canonical;
        if (!CanonicalName(Name, Canonical))
        { Error = TEXT("Recipe name must be 1-64 characters (letters, numbers, spaces, _ or -)."); return false; }
        FString JSON;
        if (!ProbeStepGroups::Encode(Steps, Range, JSON, Error)) { return false; }
        const FString Path = FilePath(Canonical);
        if (IFileManager::Get().FileExists(*Path) && !bOverwrite)
        { Error = TEXT("Recipe exists: " ) + Canonical; return false; }
        if (!IFileManager::Get().MakeDirectory(*Directory(), true))
        { Error = TEXT("Unable to create Probe recipe directory."); return false; }
        if (!FFileHelper::SaveStringToFile(JSON, *Path, FFileHelper::EEncodingOptions::ForceUTF8))
        { Error = TEXT("Unable to save Probe recipe."); return false; }
        return true;
    }

    inline bool Read(const FString& Name, TArray<FProbeTestStep>& OutSteps, FString& Error)
    {
        OutSteps.Reset();
        FString Canonical;
        if (!CanonicalName(Name, Canonical))
        { Error = TEXT("Invalid Probe recipe name."); return false; }
        FString JSON;
        if (!FFileHelper::LoadFileToString(JSON, *FilePath(Canonical)))
        { Error = TEXT("Unable to read saved Probe recipe."); return false; }
        return ProbeStepGroups::Decode(JSON, OutSteps, Error);
    }

    inline TArray<FString> List()
    {
        TArray<FString> Names;
        TArray<FString> Files;
        IFileManager::Get().FindFiles(Files, *(Directory() / TEXT("*.json")), true, false);
        for (const FString& File : Files)
        {
            const FString Name = FPaths::GetBaseFilename(File);
            FString Valid;
            if (CanonicalName(Name, Valid)) { Names.Add(Valid); }
        }
        Names.Sort();
        return Names;
    }

    inline bool Append(UProbeTestAsset& Test, const TArray<FProbeTestStep>& Recipe, FString& Error)
    {
        Error.Reset();
        if (Recipe.IsEmpty() || Recipe.Num() > 256 || Test.Steps.Num() + Recipe.Num() > 4096)
        { Error = TEXT("Recipe is empty or exceeds the authoring step limit."); return false; }
        if (!Test.Steps.IsEmpty() && !Test.CleanupGroupName.TrimStartAndEnd().IsEmpty())
        { Error = TEXT("Cannot append steps after an existing Cleanup lifecycle group."); return false; }
        // Nothing is applied until all constraints are satisfied.
        Test.Steps.Append(Recipe);
        Test.bNeedsConfiguration = true;
        return true;
    }
}
