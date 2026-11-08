#pragma once

#include "CoreMinimal.h"
#include "ProbeTestAsset.h"
#include "ProbeTestRunner.h"
#include "Templates/Function.h"

/**
 * ProbeProvider/1: optional, dependency-free plugin integrations stored under
 * <Plugin>/Resources/Probe. The provider's plugin never includes this header.
 * All functions must be called from the Editor game thread.
 */
namespace ProbePortable
{
    struct FPortableTest
    {
        FString Id;
        FString Name;
        FString Description;
        FString SourcePath; // Relative validated JSON file from this provider manifest.
        bool bEnabled = true;
        // True when a JSON action is explicitly declared. Discovery does not
        // grant this authority; each execution must opt in separately.
        bool bRequiresActionAuthorization = false;
        bool bRequiresTransientActors = false;
        // Parser-only provenance. An action must use the JSON 'action' selector;
        // declaring it as a read-only assertion is rejected.
        TSet<int32> ActionStepIndices;
        TArray<FProbeTestStep> Steps;
    };

    struct FPortablePack
    {
        FString Id;
        FString Name;
        FString SourcePath; // Relative validated JSON file from this provider manifest.
        bool bStopOnFailure = false;
        TArray<FString> TestIds;
    };

    struct FAssertionDefinition
    {
        FString Id;
        FString Name;
        FString Category;
        FString Description;
        FString Kind;
        FString BuiltInType;
        FString DefaultTarget;
        FString ReflectedClass;
        FString ReflectedFunction;
    };

    struct FActionDefinition
    {
        FString Id;
        FString Name;
        FString Description;
        FString Kind; // native-fixture, reflected-action, reflected-async
        FString Permission; // transient-object, transient-actor, provider-action
        FString NativeType;
        FString ReflectedClass;
        FString StartFunction;
        FString PollFunction;
        FString CancelFunction;
        FString CleanupFunction;
    };

    struct FPortableProvider
    {
        FString PluginName;
        FString ProviderId;
        FString Version;
        FString Directory;
        TArray<FString> ModuleNames;
        TArray<FAssertionDefinition> Assertions;
        TArray<FActionDefinition> Actions;
        TArray<FPortableTest> Tests;
        TArray<FPortableTest> Templates;
        TArray<FPortablePack> Packs;
    };

    /** Pure validation; accepts relative paths only, rejecting escapes and alternate separators. */
    PROBEEDITOR_API bool ValidateRelativePath(const FString& Path, FString& Reason);
    /** Parse a single local provider directory. Does not run any test or reflection function. */
    PROBEEDITOR_API bool LoadProvider(const FString& Directory, const FString& PluginName,
        FPortableProvider& OutProvider, FString& Reason);
    /** Scan enabled plugins, validate declarations, then replace managed registry entries atomically. */
    PROBEEDITOR_API void Refresh();
    /** Unregister all JSON-managed entries during Probe module shutdown. */
    PROBEEDITOR_API void Clear();
    PROBEEDITOR_API const TArray<FPortableProvider>& Providers();
    PROBEEDITOR_API const TArray<FString>& Diagnostics();
    PROBEEDITOR_API FString DiscoverySummary();
    PROBEEDITOR_API bool RequiresAuthorization(const FString& Id, bool bPack);
    // Requests cancellation of current portable execution at the earliest safe
    // boundary. Provider async actions also receive their Cancel callback.
    PROBEEDITOR_API bool RequestCancel();
    /** The following only run on an explicit user or caller request. No UAsset is created. */
    PROBEEDITOR_API bool RunTest(const FString& TestId,
        TFunction<void(TArray<Probe::FTestResult>)> OnComplete, FString& Reason,
        bool bAuthorizeActions = false);
    PROBEEDITOR_API bool RunPack(const FString& PackId,
        TFunction<void(TArray<Probe::FTestResult>)> OnComplete, FString& Reason,
        bool bAuthorizeActions = false);
    /** Copy a portable test into a new transient UProbeTestAsset for authoring/import. Never grants input permissions. */
    PROBEEDITOR_API bool MakeTransientTest(const FPortableTest& Definition, UProbeTestAsset*& Out, FString& Reason, bool bAuthorizeActions = false);
}
