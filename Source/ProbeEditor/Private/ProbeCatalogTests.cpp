#include "ProbeAssertionCatalog.h"
#include "ProbeTestRunner.h"
#include "ProbeExtensions.h"
#include "ProbeTestPack.h"
#include "ProbeReferenceValidation.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeCatalogCoverageTest,
    "Probe.Catalog.AllAssertionsRegistered",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeCatalogCoverageTest::RunTest(const FString& Parameters)
{
    const UEnum* Enum = StaticEnum<EProbeStepType>();
    if (!TestNotNull(TEXT("EProbeStepType should have reflection metadata"), Enum)) { return false; }

    TSet<uint8> Seen;
    for (const Probe::FAssertionInfo& Info : Probe::AssertionCatalog())
    {
        const uint8 Index = static_cast<uint8>(Info.Type);
        TestFalse(FString::Printf(TEXT("Duplicate assertion index %d"), Index), Seen.Contains(Index));
        Seen.Add(Index);
        TestTrue(FString::Printf(TEXT("Assertion %d needs a name"), Index), FCString::Strlen(Info.Name) > 0);
        TestTrue(FString::Printf(TEXT("Assertion %d needs guidance"), Index), FCString::Strlen(Info.Description) > 0);
        TestTrue(FString::Printf(TEXT("Assertion %d must have a category"), Index), FCString::Strlen(Info.Category) > 0);
        bool bFoundCategory = false;
        for (const FString& Category : Probe::AssertionCategories())
        {
            bFoundCategory |= Category == Info.Category;
        }
        TestTrue(FString::Printf(TEXT("Assertion %d category must exist"), Index), bFoundCategory);
    }
    TestEqual(TEXT("Assertion catalog covers all serialized enum values"), Seen.Num(), 159);
    // These original IDs must never change: Probe Tests are serialized assets.
    TestEqual(TEXT("Original PluginEnabled enum value"), static_cast<int32>(EProbeStepType::PluginEnabled), 0);
    TestEqual(TEXT("Original SelectedActorCountAtLeast enum value"), static_cast<int32>(EProbeStepType::SelectedActorCountAtLeast), 10);
    TestEqual(TEXT("0.6 last enum index"), static_cast<int32>(EProbeStepType::SelectedActorHasComponent), 71);
    TestEqual(TEXT("0.7 last enum index"), static_cast<int32>(EProbeStepType::ObjectPropertyIsFalse), 102);
    TestEqual(TEXT("0.8 last enum index"), static_cast<int32>(EProbeStepType::ObjectPropertyNumericNearlyEquals), 127);
    TestEqual(TEXT("0.9 last enum index"), static_cast<int32>(EProbeStepType::FixtureDestroyObject), 133);
    TestEqual(TEXT("0.10 last enum index"), static_cast<int32>(EProbeStepType::SelectedActorIsNotTemporarilyHidden), 158);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeTemplateReadinessTest,
    "Probe.Runner.TemplateReadiness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeTemplateReadinessTest::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage());
    Asset->bEnabled = true;
    Asset->bNeedsConfiguration = true;
    FProbeTestStep Step;
    Step.Type = EProbeStepType::PluginEnabled;
    Step.Target = TEXT("Probe");
    Asset->Steps.Add(Step);
    const Probe::FTestResult Deferred = Probe::RunTest(Asset);
    TestEqual(TEXT("Unconfigured starter is Skipped"), static_cast<int32>(Deferred.Status),
        static_cast<int32>(Probe::EResultStatus::Skipped));
    TestTrue(TEXT("Skipped result explains how to configure"), Deferred.Detail.Contains(TEXT("Needs Configuration"))
        || Deferred.Detail.Contains(TEXT("needs configuration")));
    Asset->bNeedsConfiguration = false;
    const Probe::FTestResult Ready = Probe::RunTest(Asset);
    TestEqual(TEXT("Configured starter can execute"), static_cast<int32>(Ready.Status),
        static_cast<int32>(Probe::EResultStatus::Passed));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeExpandedAssertionTest,
    "Probe.Runner.ExpandedAssertionContracts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeExpandedAssertionTest::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage());
    FProbeTestStep ModuleCount;
    ModuleCount.Type = EProbeStepType::PluginModuleCountExactly;
    ModuleCount.Target = TEXT("Probe");
    ModuleCount.MinimumActorCount = 1;
    Asset->Steps.Add(ModuleCount);
    FProbeTestStep NumericType;
    NumericType.Type = EProbeStepType::ClassPropertyIsNumeric;
    NumericType.Target = TEXT("/Script/Engine.Actor");
    NumericType.PropertyName = TEXT("InitialLifeSpan");
    Asset->Steps.Add(NumericType);
    FProbeTestStep ReturnValue;
    ReturnValue.Type = EProbeStepType::ClassFunctionHasReturnValue;
    ReturnValue.Target = TEXT("/Script/Engine.Actor");
    ReturnValue.PropertyName = TEXT("GetActorLocation");
    Asset->Steps.Add(ReturnValue);
    const Probe::FTestResult Result = Probe::RunTest(Asset);
    TestEqual(TEXT("Plugin module and reflection assertions pass"), static_cast<int32>(Result.Status),
        static_cast<int32>(Probe::EResultStatus::Passed));
    TestEqual(TEXT("All 3 new assertions executed"), Result.Steps.Num(), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeContract010Test,
    "Probe.Runner.ContractAssertions010",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeContract010Test::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage());
    FProbeTestStep PluginStep;
    PluginStep.Type = EProbeStepType::PluginFriendlyNameContains;
    PluginStep.Target = TEXT("Probe");
    PluginStep.ExpectedValue = TEXT("Probe");
    Asset->Steps.Add(PluginStep);
    FProbeTestStep ArrayStep;
    ArrayStep.Type = EProbeStepType::ClassPropertyIsArray;
    ArrayStep.Target = TEXT("/Script/Engine.Actor");
    ArrayStep.PropertyName = TEXT("Tags");
    Asset->Steps.Add(ArrayStep);
    FProbeTestStep FunctionStep;
    FunctionStep.Type = EProbeStepType::ClassFunctionIsPublic;
    FunctionStep.Target = TEXT("/Script/Engine.Actor");
    FunctionStep.PropertyName = TEXT("GetActorLocation");
    Asset->Steps.Add(FunctionStep);
    const Probe::FTestResult Result = Probe::RunTest(Asset);
    TestEqual(TEXT("0.10 plugin and reflection contracts pass"), static_cast<int32>(Result.Status),
        static_cast<int32>(Probe::EResultStatus::Passed));
    TestEqual(TEXT("All 3 contract assertions produce results"), Result.Steps.Num(), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeReadOnlyRunnerTest,
    "Probe.Runner.EditorPluginAssertions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeReadOnlyRunnerTest::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage());
    Asset->bEnabled = true;
    FProbeTestStep PluginStep;
    PluginStep.Type = EProbeStepType::PluginInstalled;
    PluginStep.Target = TEXT("Probe");
    Asset->Steps.Add(PluginStep);
    FProbeTestStep ClassStep;
    ClassStep.Type = EProbeStepType::ClassHasFunction;
    ClassStep.Target = TEXT("/Script/Engine.Actor");
    ClassStep.PropertyName = TEXT("GetActorLocation");
    Asset->Steps.Add(ClassStep);
    const Probe::FTestResult Result = Probe::RunTest(Asset);
    TestEqual(TEXT("Installed plugin and native Actor reflection should pass"), static_cast<int32>(Result.Status), static_cast<int32>(Probe::EResultStatus::Passed));
    TestEqual(TEXT("Two results should be returned"), Result.Steps.Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeReflectionAliasTest,
    "Probe.Runner.ReflectionAliases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeReflectionAliasTest::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage());
    FProbeTestStep FunctionStep;
    FunctionStep.Type = EProbeStepType::ClassHasFunction;
    FunctionStep.Target = TEXT("/Script/Engine.Actor");
    FunctionStep.PropertyName = TEXT("GetActorLocation");
    Asset->Steps.Add(FunctionStep);
    FProbeTestStep CallableStep = FunctionStep;
    CallableStep.Type = EProbeStepType::ClassFunctionBlueprintCallable;
    Asset->Steps.Add(CallableStep);
    const Probe::FTestResult Result = Probe::RunTest(Asset);
    TestEqual(TEXT("UE Actor Blueprint alias and BlueprintCallable should resolve"),
        static_cast<int32>(Result.Status), static_cast<int32>(Probe::EResultStatus::Passed));
    TestEqual(TEXT("Both steps should execute"), Result.Steps.Num(), 2);
    if (Result.Steps.Num() == 2)
    {
        TestTrue(TEXT("Resolved UFUNCTION name should be visible in diagnostics"),
            Result.Steps[0].Detail.Contains(TEXT("K2_GetActorLocation")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDescriptorChecksTest,
    "Probe.Runner.PluginDescriptorAndNegativeAssertions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDescriptorChecksTest::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage());
    FProbeTestStep Module;
    Module.Type = EProbeStepType::PluginDeclaresModule;
    Module.Target = TEXT("Probe");
    Module.ExpectedValue = TEXT("ProbeEditor");
    Asset->Steps.Add(Module);
    FProbeTestStep Absent;
    Absent.Type = EProbeStepType::ClassNotExists;
    Absent.Target = TEXT("/Script/ProbeEditor.ProbeUnrealClassThatCannotExist");
    Asset->Steps.Add(Absent);
    const Probe::FTestResult Result = Probe::RunTest(Asset);
    TestEqual(TEXT("Probe declares ProbeEditor; imaginary class absent"),
        static_cast<int32>(Result.Status), static_cast<int32>(Probe::EResultStatus::Passed));
    TestEqual(TEXT("Both tests executed"), Result.Steps.Num(), 2);
    return true;
}

#endif

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeExtensionDispatchTest,
    "Probe.Extensions.RegistrationAndMissingProvider",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeExtensionDispatchTest::RunTest(const FString& Parameters)
{
    // Test both the registered extension path and explicit missing-provider handling.
    Probe::FExtensionAssertion Assertion;
    Assertion.Id = TEXT("ProbeSelfTest.Available");
    Assertion.Name = TEXT("Temporary Self Test");
    Assertion.Category = TEXT("Self Test");
    Assertion.Description = TEXT("Tests Probe extension dispatch");
    Assertion.Fields = 0;
    Assertion.Evaluate = [](const FProbeTestStep&, FString& Reason)
    {
        Reason = TEXT("Extension provider executed successfully");
        return Probe::EResultStatus::Passed;
    };
    if (!TestTrue(TEXT("Can register unique namespaced assertion"), Probe::RegisterAssertion(MoveTemp(Assertion))))
    { return false; }

    UProbeTestAsset* TestAsset = NewObject<UProbeTestAsset>(GetTransientPackage());
    FProbeTestStep Step;
    Step.AssertionId = TEXT("ProbeSelfTest.Available");
    TestAsset->Steps.Add(Step);
    const Probe::FTestResult Good = Probe::RunTest(TestAsset);
    TestEqual(TEXT("Custom assertion reports Passed"), static_cast<int32>(Good.Status),
        static_cast<int32>(Probe::EResultStatus::Passed));
    TestEqual(TEXT("Custom step count"), Good.Steps.Num(), 1);
    Probe::UnregisterAssertion(TEXT("ProbeSelfTest.Available"));

    const Probe::FTestResult Missing = Probe::RunTest(TestAsset);
    TestEqual(TEXT("Missing provider reports Error"), static_cast<int32>(Missing.Status),
        static_cast<int32>(Probe::EResultStatus::Error));
    TestTrue(TEXT("Error identifies missing provider"), Missing.Detail.Contains(TEXT("ProbeSelfTest.Available")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbePackEmptyTest,
    "Probe.Packs.EmptyPack",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbePackEmptyTest::RunTest(const FString& Parameters)
{
    UProbeTestPack* Pack = NewObject<UProbeTestPack>(GetTransientPackage());
    TestTrue(TEXT("Empty pack produces zero results, does not execute arbitrary tests"), Probe::RunPack(Pack).IsEmpty());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeTemporaryFixtureTest,
    "Probe.Runner.TemporaryFixtureLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeTemporaryFixtureTest::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage());
    FProbeTestStep Create;
    Create.Type = EProbeStepType::FixtureCreateObject;
    Create.Target = TEXT("/Script/ProbeEditor.ProbeTestAsset");
    Create.ExpectedValue = TEXT("TempA");
    Asset->Steps.Add(Create);
    FProbeTestStep Set;
    Set.Type = EProbeStepType::FixtureSetProperty;
    Set.Target = TEXT("TempA");
    Set.PropertyName = TEXT("bEnabled");
    Set.ExpectedValue = TEXT("False");
    Asset->Steps.Add(Set);
    FProbeTestStep Assert = Set;
    Assert.Type = EProbeStepType::FixturePropertyEquals;
    Asset->Steps.Add(Assert);
    FProbeTestStep Destroy;
    Destroy.Type = EProbeStepType::FixtureDestroyObject;
    Destroy.Target = TEXT("TempA");
    Asset->Steps.Add(Destroy);
    const Probe::FTestResult Passed = Probe::RunTest(Asset);
    TestEqual(TEXT("Temporary fixture create/mutate/assert/destroy pass"), static_cast<int32>(Passed.Status),
        static_cast<int32>(Probe::EResultStatus::Passed));
    TestEqual(TEXT("All four actions/conditions executed"), Passed.Steps.Num(), 4);

    // Fixture IDs must not persist into another run, including after failures.
    Asset->Steps.Empty();
    Asset->Steps.Add(Assert);
    const Probe::FTestResult Missing = Probe::RunTest(Asset);
    TestEqual(TEXT("Fixtures are isolated per run"), static_cast<int32>(Missing.Status),
        static_cast<int32>(Probe::EResultStatus::Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeMissingPackReferenceTest,
    "Probe.References.MissingPackEntry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeMissingPackReferenceTest::RunTest(const FString& Parameters)
{
    UProbeTestPack* Pack = NewObject<UProbeTestPack>(GetTransientPackage());
    Pack->Tests.Add(TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(TEXT("/Game/ProbeTests/MissingNonexistentTest_ProbeQA.MissingNonexistentTest_ProbeQA"))));
    const Probe::FReferenceAudit Report = Probe::AuditPackReferences(Pack);
    TestEqual(TEXT("One missing Pack reference flagged"), Report.Problems, 1);
    TestEqual(TEXT("One reference scanned"), Report.ReferencesChecked, 1);
    const TArray<Probe::FTestResult> Results = Probe::RunPack(Pack);
    TestEqual(TEXT("Missing reference produces one result"), Results.Num(), 1);
    if (Results.Num() == 1)
    {
        TestEqual(TEXT("Missing reference is an error, not an attempted load"), static_cast<int32>(Results[0].Status),
            static_cast<int32>(Probe::EResultStatus::Error));
    }
    return true;
}

#endif

