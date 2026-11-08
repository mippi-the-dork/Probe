#pragma once

#include "CoreMinimal.h"
#include "ProbePortableProviders.h"
#include "Dom/JsonObject.h"

// Editor-only tooling. All writes are confined to /Game/Probe/Imported or
// <Project>/Saved/Probe. Never modifies another plugin's Resources directory.
namespace ProbeDev19
{
    bool ValidProviderName(const FString& Name);
    bool CreateStarter(const FString& PluginName, FString& OutDirectory, FString& Error);
    bool ValidateInstalled(const FString& PluginName, FString& Summary, FString& Error);
    bool ImportReadOnlyTest(const ProbePortable::FPortableTest& Test, UProbeTestAsset*& Asset, FString& Error);
    bool BuildReadOnlyTestJson(const UProbeTestAsset& Asset, const FString& ProviderId,
        const TSharedRef<FJsonObject>& OutJson, FString& Error);
    bool ExportReadOnlyTest(const UProbeTestAsset& Asset, const FString& ProviderId,
        FString& OutFile, FString& Error);
    bool ExportSourceDefinition(const ProbePortable::FPortableProvider& Provider,
        const FString& Id, bool bPack, FString& OutFile, FString& Error);
    FString PreviewTest(const ProbePortable::FPortableProvider& Provider,
        const ProbePortable::FPortableTest& Test);
    FString PreviewPack(const ProbePortable::FPortableProvider& Provider,
        const ProbePortable::FPortablePack& Pack);
    struct FRunSummary
    {
        int32 Passed = 0, Failed = 0, Errors = 0, Cancelled = 0, Skipped = 0;
        FString ToString() const;
    };
    FString DisplayStatus(const Probe::FTestResult& Test);
    FRunSummary Summarize(const TArray<Probe::FTestResult>& Results);
    TSharedRef<FJsonObject> BuildRunReport(const FString& DefinitionId, bool bPack,
        bool bActions, const TArray<Probe::FTestResult>& Results);
    /** Retain only newest matching Portable-*.json files; leave unrelated project files untouched. */
    int32 PrunePortableReports(const FString& Directory, int32 MaximumFiles = 25);
    bool SaveRunReport(const FString& DefinitionId, bool bPack, bool bActions,
        const TArray<Probe::FTestResult>& Results, FString& OutFile, FString& Error);
}
