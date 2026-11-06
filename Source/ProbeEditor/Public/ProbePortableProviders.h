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
        bool bEnabled = true;
        TArray<FProbeTestStep> Steps;
    };

    struct FPortablePack
    {
        FString Id;
        FString Name;
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

    struct FPortableProvider
    {
        FString PluginName;
        FString ProviderId;
        FString Version;
        FString Directory;
        TArray<FString> ModuleNames;
        TArray<FAssertionDefinition> Assertions;
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
    /** The following only run on an explicit user or caller request. No UAsset is created. */
    PROBEEDITOR_API bool RunTest(const FString& TestId,
        TFunction<void(TArray<Probe::FTestResult>)> OnComplete, FString& Reason);
    PROBEEDITOR_API bool RunPack(const FString& PackId,
        TFunction<void(TArray<Probe::FTestResult>)> OnComplete, FString& Reason);
    /** Copy a portable test into a new transient UProbeTestAsset for authoring/import. Never grants input permissions. */
    PROBEEDITOR_API bool MakeTransientTest(const FPortableTest& Definition, UProbeTestAsset*& Out, FString& Reason);
}
