#include "ProbeAssertionCatalog.h"
#include "ProbeTestRunner.h"
#include "ProbeSelfTest.h"
#include "ProbeStepEditing.h"
#include "ProbeExtensions.h"
#include "ProbeTestPack.h"
#include "ProbeReferenceValidation.h"
#include "Misc/AutomationTest.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeStepDuplicationRegressionTest,
    "Probe.Editor.StepDuplicationSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeStepDuplicationRegressionTest::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage());
    FProbeTestStep Source;
    Source.Type = EProbeStepType::ActorFixtureDistanceAtMost;
    Source.AssertionId = FName(TEXT("Example.CustomAssertion"));
    Source.Target = TEXT("A source target with a long string to force nontrivial FString copies");
    Source.ExpectedValue = TEXT("ParentFixture");
    Source.ExpectedClass = TEXT("/Script/Engine.StaticMeshActor");
    Source.PropertyName = TEXT("Tags");
    Source.NumericTolerance = 0.125;
    Source.MinimumActorCount = 250;
    Source.Description = FString::ChrN(1024, TCHAR('X'));
    Asset->Steps.Add(Source);
    TestFalse(TEXT("Negative duplicate index is rejected"), Probe::DuplicateStepSafely(*Asset, -1));
    TestFalse(TEXT("Out-of-range duplicate index is rejected"), Probe::DuplicateStepSafely(*Asset, 5));
    for (int32 Iteration = 0; Iteration < 64; ++Iteration)
    {
        TestTrue(TEXT("Duplicate step without aliasing TArray memory"), Probe::DuplicateStepSafely(*Asset, 0));
    }
    TestEqual(TEXT("One source and 64 independent copies"), Asset->Steps.Num(), 65);
    for (const FProbeTestStep& Step : Asset->Steps)
    {
        TestEqual(TEXT("Duplicate retains reflected assertion type"), static_cast<int32>(Step.Type), static_cast<int32>(Source.Type));
        TestEqual(TEXT("Duplicate retains extension provider"), Step.AssertionId, Source.AssertionId);
        TestEqual(TEXT("Duplicate retains target"), Step.Target, Source.Target);
        TestEqual(TEXT("Duplicate retains property"), Step.PropertyName, Source.PropertyName);
        TestEqual(TEXT("Duplicate retains expected value"), Step.ExpectedValue, Source.ExpectedValue);
        TestEqual(TEXT("Duplicate retains class"), Step.ExpectedClass, Source.ExpectedClass);
        TestEqual(TEXT("Duplicate retains count"), Step.MinimumActorCount, Source.MinimumActorCount);
        TestEqual(TEXT("Duplicate retains tolerance"), Step.NumericTolerance, Source.NumericTolerance);
        TestEqual(TEXT("Duplicate retains description"), Step.Description, Source.Description);
    }
    Asset->Steps[1].Description = TEXT("Edited duplicate");
    TestEqual(TEXT("Editing duplicate does not change original"), Asset->Steps[0].Description, Source.Description);
    return true;
}

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
    TestEqual(TEXT("Assertion catalog covers all serialized enum values"), Seen.Num(), 206);
    TestEqual(TEXT("0.14 last enum index"), static_cast<int32>(EProbeStepType::ActorFixtureScaleNotNearlyEquals), 205);
    // These original IDs must never change: Probe Tests are serialized assets.
    TestEqual(TEXT("Original PluginEnabled enum value"), static_cast<int32>(EProbeStepType::PluginEnabled), 0);
    TestEqual(TEXT("Original SelectedActorCountAtLeast enum value"), static_cast<int32>(EProbeStepType::SelectedActorCountAtLeast), 10);
    TestEqual(TEXT("0.6 last enum index"), static_cast<int32>(EProbeStepType::SelectedActorHasComponent), 71);
    TestEqual(TEXT("0.7 last enum index"), static_cast<int32>(EProbeStepType::ObjectPropertyIsFalse), 102);
    TestEqual(TEXT("0.8 last enum index"), static_cast<int32>(EProbeStepType::ObjectPropertyNumericNearlyEquals), 127);
    TestEqual(TEXT("0.9 last enum index"), static_cast<int32>(EProbeStepType::FixtureDestroyObject), 133);
    TestEqual(TEXT("0.11 last enum index"), static_cast<int32>(EProbeStepType::SelectionRestore), 163);
    TestEqual(TEXT("0.12 last enum index"), static_cast<int32>(EProbeStepType::ActorFixtureDestroy), 178);
    TestEqual(TEXT("0.13 last enum index"), static_cast<int32>(EProbeStepType::ActorFixtureDistanceAtMost), 194);
    TestEqual(TEXT("0.10 last enum index"), static_cast<int32>(EProbeStepType::SelectedActorIsNotTemporarilyHidden), 158);
    return true;
}

// Editor selection actions are reversible, including when a later assertion fails.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeEditorSelectionRestorationTest,
    "Probe.Runner.EditorSelectionRestoration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeEditorSelectionRestorationTest::RunTest(const FString& Parameters)
{
    if (!GEditor || !GEditor->GetEditorWorldContext().World())
    {
        AddInfo(TEXT("No Editor world; selection restoration test skipped."));
        return true;
    }
    const int32 OriginalCount = GEditor->GetSelectedActorCount();
    UProbeTestAsset* TestAsset = NewObject<UProbeTestAsset>(GetTransientPackage());
    FProbeTestStep Clear;
    Clear.Type = EProbeStepType::SelectionClear;
    TestAsset->Steps.Add(Clear);
    FProbeTestStep AssertEmpty;
    AssertEmpty.Type = EProbeStepType::SelectedActorCountExactly;
    AssertEmpty.MinimumActorCount = 0;
    TestAsset->Steps.Add(AssertEmpty);
    FProbeTestStep Restore;
    Restore.Type = EProbeStepType::SelectionRestore;
    TestAsset->Steps.Add(Restore);
    const Probe::FTestResult Passed = Probe::RunTest(TestAsset);
    TestEqual(TEXT("Clear/verify/restore workflow passes"),
        static_cast<int32>(Passed.Status), static_cast<int32>(Probe::EResultStatus::Passed));
    TestEqual(TEXT("Original selection count preserved"), GEditor->GetSelectedActorCount(), OriginalCount);

    TestAsset->Steps.Reset();
    TestAsset->Steps.Add(Clear);
    FProbeTestStep ExpectNonempty;
    ExpectNonempty.Type = EProbeStepType::SelectedActorsNotEmpty;
    TestAsset->Steps.Add(ExpectNonempty);
    const Probe::FTestResult Failed = Probe::RunTest(TestAsset);
    TestEqual(TEXT("Expected failing selection assertion is reported"),
        static_cast<int32>(Failed.Status), static_cast<int32>(Probe::EResultStatus::Failed));
    TestEqual(TEXT("Failure cleanup restores selection count"), GEditor->GetSelectedActorCount(), OriginalCount);
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

// Exercise transform, attachment and cleanup without depending on persistent map actors.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeSpatialHierarchyFixtureTest,
    "Probe.Runner.TransientActorTransformsAndHierarchy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeSpatialHierarchyFixtureTest::RunTest(const FString& Parameters)
{
    if (!GEditor || !GEditor->GetEditorWorldContext().World() || GEditor->PlayWorld)
    {
        AddInfo(TEXT("Transient actor tests require an Editor world outside PIE."));
        return true;
    }
    UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage());
    Asset->bAllowTransientEditorActors = true;
    auto AddStep = [Asset](EProbeStepType Type, const TCHAR* Target, const TCHAR* Value = TEXT(""), int32 Count = 0)
    {
        FProbeTestStep Step;
        Step.Type = Type;
        Step.Target = Target;
        Step.ExpectedValue = Value;
        Step.MinimumActorCount = Count;
        Asset->Steps.Add(MoveTemp(Step));
    };
    AddStep(EProbeStepType::ActorFixtureSpawn, TEXT("/Script/Engine.StaticMeshActor"), TEXT("ProbeSpatialParent"));
    AddStep(EProbeStepType::ActorFixtureSpawn, TEXT("/Script/Engine.StaticMeshActor"), TEXT("ProbeSpatialChild"));
    AddStep(EProbeStepType::ActorFixtureSetLocation, TEXT("ProbeSpatialParent"), TEXT("50,0,0"));
    AddStep(EProbeStepType::ActorFixtureLocationNearlyEquals, TEXT("ProbeSpatialParent"), TEXT("50,0,0"));
    AddStep(EProbeStepType::ActorFixtureSetRotation, TEXT("ProbeSpatialParent"), TEXT("0,45,0"));
    AddStep(EProbeStepType::ActorFixtureRotationNearlyEquals, TEXT("ProbeSpatialParent"), TEXT("0,45,0"));
    AddStep(EProbeStepType::ActorFixtureSetScale, TEXT("ProbeSpatialParent"), TEXT("2,1,1"));
    AddStep(EProbeStepType::ActorFixtureScaleNearlyEquals, TEXT("ProbeSpatialParent"), TEXT("2,1,1"));
    AddStep(EProbeStepType::ActorFixtureAttachTo, TEXT("ProbeSpatialChild"), TEXT("ProbeSpatialParent"));
    AddStep(EProbeStepType::ActorFixtureHasParent, TEXT("ProbeSpatialChild"), TEXT("ProbeSpatialParent"));
    AddStep(EProbeStepType::ActorFixtureChildCountExactly, TEXT("ProbeSpatialParent"), TEXT(""), 1);
    AddStep(EProbeStepType::ActorFixtureDetach, TEXT("ProbeSpatialChild"));
    AddStep(EProbeStepType::ActorFixtureHasNoParent, TEXT("ProbeSpatialChild"));
    const Probe::FTestResult Passed = Probe::RunTest(Asset);
    TestEqual(TEXT("Transform + attach + detach workflow"), static_cast<int32>(Passed.Status),
        static_cast<int32>(Probe::EResultStatus::Passed));
    if (Passed.Status != Probe::EResultStatus::Passed)
    { AddError(FString::Printf(TEXT("Spatial workflow diagnostics: %s"), *Passed.Detail)); }
    // A failing assertion after two spawns must still clean up BOTH fixtures.
    Asset->Steps.SetNum(2);
    AddStep(EProbeStepType::ActorFixtureHasParent, TEXT("ProbeSpatialChild"), TEXT("ProbeSpatialParent"));
    const Probe::FTestResult Failed = Probe::RunTest(Asset);
    TestEqual(TEXT("Incorrect hierarchy assertion is a failure"), static_cast<int32>(Failed.Status),
        static_cast<int32>(Probe::EResultStatus::Failed));
    int32 Leftovers = 0;
    for (TActorIterator<AActor> It(GEditor->GetEditorWorldContext().World()); It; ++It)
    {
        if (It->GetActorLabel().StartsWith(TEXT("Probe_ProbeSpatialParent"))
            || It->GetActorLabel().StartsWith(TEXT("Probe_ProbeSpatialChild")))
        { ++Leftovers; }
    }
    TestEqual(TEXT("Spatial fixtures cleaned on success and failure"), Leftovers, 0);
    return true;
}

// Actor fixtures are deliberately off by default, and their cleanup is tested
// both on a successful run and after an assertion reports Failed.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeTransientActorFixtureTest,
    "Probe.Runner.TransientEditorActorLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeTransientActorFixtureTest::RunTest(const FString& Parameters)
{
    if (!GEditor || !GEditor->GetEditorWorldContext().World()
        || GEditor->PlayWorld)
    {
        AddInfo(TEXT("Transient actor fixture self-test requires an Editor world outside PIE."));
        return true;
    }
    UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage());
    FProbeTestStep Spawn;
    Spawn.Type = EProbeStepType::ActorFixtureSpawn;
    Spawn.Target = TEXT("/Script/Engine.Actor");
    Spawn.ExpectedValue = TEXT("ProbeFixtureSelfTestUnique");
    Asset->Steps.Add(Spawn);
    const Probe::FTestResult Denied = Probe::RunTest(Asset);
    TestEqual(TEXT("Default test cannot spawn level actors"), static_cast<int32>(Denied.Status),
        static_cast<int32>(Probe::EResultStatus::Error));
    TestTrue(TEXT("Denied reason identifies permission"), Denied.Detail.Contains(TEXT("Allow Transient")));

    Asset->bAllowTransientEditorActors = true;
    FProbeTestStep Exists;
    Exists.Type = EProbeStepType::ActorFixtureExists;
    Exists.Target = TEXT("ProbeFixtureSelfTestUnique");
    Asset->Steps.Add(Exists);
    const Probe::FTestResult Pass = Probe::RunTest(Asset);
    TestEqual(TEXT("Temporary actor is usable during a test"), static_cast<int32>(Pass.Status),
        static_cast<int32>(Probe::EResultStatus::Passed));

    // The next run expects a missing fixture: the successful run must have cleaned it.
    Asset->Steps.Reset();
    Asset->Steps.Add(Exists);
    const Probe::FTestResult Cleared = Probe::RunTest(Asset);
    TestEqual(TEXT("Fixture IDs never survive a test run"), static_cast<int32>(Cleared.Status),
        static_cast<int32>(Probe::EResultStatus::Failed));

    Asset->Steps.Reset();
    Asset->Steps.Add(Spawn);
    FProbeTestStep IncorrectLabel;
    IncorrectLabel.Type = EProbeStepType::ActorFixtureLabelEquals;
    IncorrectLabel.Target = TEXT("ProbeFixtureSelfTestUnique");
    IncorrectLabel.ExpectedValue = TEXT("MustNotMatchThisActorLabel");
    Asset->Steps.Add(IncorrectLabel);
    const Probe::FTestResult Failure = Probe::RunTest(Asset);
    TestEqual(TEXT("Incorrect fixture expectation fails normally"), static_cast<int32>(Failure.Status),
        static_cast<int32>(Probe::EResultStatus::Failed));
    Asset->Steps.Reset();
    Asset->Steps.Add(Exists);
    const Probe::FTestResult ClearedAfterFailure = Probe::RunTest(Asset);
    TestEqual(TEXT("Fixture IDs do not survive a failed run"), static_cast<int32>(ClearedAfterFailure.Status),
        static_cast<int32>(Probe::EResultStatus::Failed));
    int32 LeakedFixtures = 0;
    for (TActorIterator<AActor> It(GEditor->GetEditorWorldContext().World()); It; ++It)
    {
        if (It->GetActorLabel().StartsWith(TEXT("Probe_ProbeFixtureSelfTestUnique")))
        { ++LeakedFixtures; }
    }
    TestEqual(TEXT("No Probe fixture actors remain in the Editor level"), LeakedFixtures, 0);
    return true;
}

#endif


#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeSubmenuGroupingTest,
    "Probe.Editor.CategorizedMenus",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeSubmenuGroupingTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Module checks are grouped together"),
        Probe::MenuSubgroup(TEXT("Plugins & Modules"), TEXT("Module Is Loaded")), FString(TEXT("Modules")));
    TestEqual(TEXT("Fixture transform checks are grouped together"),
        Probe::MenuSubgroup(TEXT("Test Actions"), TEXT("Temporary Actor Location Not Nearly Equals")),
        FString(TEXT("Actor Transforms & Distance")));
    TestEqual(TEXT("Hierarchy checks are grouped together"),
        Probe::MenuSubgroup(TEXT("Test Actions"), TEXT("Temporary Actor Child Count At Least")),
        FString(TEXT("Actor Hierarchy")));
    TestEqual(TEXT("Functions are grouped together"),
        Probe::MenuSubgroup(TEXT("Classes & Reflection"), TEXT("Class Function Is Pure")),
        FString(TEXT("Functions")));
    TSet<int32> Seen;
    for (const Probe::FAssertionInfo& Info : Probe::AssertionCatalog())
    {
        TestFalse(TEXT("No duplicate built-in assertion types"), Seen.Contains(static_cast<int32>(Info.Type)));
        Seen.Add(static_cast<int32>(Info.Type));
        TestFalse(TEXT("Every built-in assertion has a submenu"),
            Probe::MenuSubgroup(FString(Info.Category), FString(Info.Name)).IsEmpty());
    }
    TestEqual(TEXT("All 206 assertions are assigned to menus"), Seen.Num(), 206);
    return true;
}
#endif

#if WITH_DEV_AUTOMATION_TESTS

// Native Automation Framework tests are an independent entry point from the Probe
// Self-Test console command. Intentional Failed/Error/Skipped outcomes MUST be
// verified as expected, not treated as an overall automation-test failure.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeSelfTestNativeGate,
    "Probe.SelfTest.NativeSmokeGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeSelfTestNativeGate::RunTest(const FString& Parameters)
{
    const int32 InitialSelectionCount = GEditor ? GEditor->GetSelectedActorCount() : -1;
    const Probe::FSelfTestReport Report = Probe::RunSelfTestSmoke(false);
    TestTrue(TEXT("Smoke gate accepts exact case coverage and negative controls"), Report.bGatePassed);
    TestEqual(TEXT("Fifteen required smoke and regression cases actually executed"), Report.Cases.Num(), 15);
    TestEqual(TEXT("Six positive results"), Report.PassedOutcomes, 6);
    TestEqual(TEXT("Four deliberately failing results"), Report.FailedOutcomes, 4);
    TestEqual(TEXT("Three deliberate errors"), Report.ErrorOutcomes, 3);
    TestEqual(TEXT("Two deliberate skips"), Report.SkippedOutcomes, 2);
    TestEqual(TEXT("Independent oracle found zero mismatches"), Report.OracleFailures, 0);
    TMap<FString, Probe::EResultStatus> RequiredOutcomes;
    RequiredOutcomes.Add(TEXT("Probe.SelfTest.Smoke.PluginEnabled"), Probe::EResultStatus::Passed);
    RequiredOutcomes.Add(TEXT("Probe.SelfTest.Smoke.ReflectionAlias"), Probe::EResultStatus::Passed);
    RequiredOutcomes.Add(TEXT("Probe.SelfTest.Smoke.FixtureRoundTrip"), Probe::EResultStatus::Passed);
    RequiredOutcomes.Add(TEXT("Probe.SelfTest.Smoke.ExpectedFailure"), Probe::EResultStatus::Failed);
    RequiredOutcomes.Add(TEXT("Probe.SelfTest.Smoke.ExpectedMissingFixture"), Probe::EResultStatus::Failed);
    RequiredOutcomes.Add(TEXT("Probe.SelfTest.Smoke.ExpectedError"), Probe::EResultStatus::Error);
    RequiredOutcomes.Add(TEXT("Probe.SelfTest.Smoke.EmptyTestError"), Probe::EResultStatus::Error);
    RequiredOutcomes.Add(TEXT("Probe.SelfTest.Smoke.DisabledSkip"), Probe::EResultStatus::Skipped);
    RequiredOutcomes.Add(TEXT("Probe.SelfTest.Smoke.ConfigurationSkip"), Probe::EResultStatus::Skipped);
    RequiredOutcomes.Add(TEXT("Probe.SelfTest.Smoke.PluginInstalled"), Probe::EResultStatus::Passed);
    RequiredOutcomes.Add(TEXT("Probe.SelfTest.Smoke.ReflectionProperty"), Probe::EResultStatus::Passed);
    RequiredOutcomes.Add(TEXT("Probe.SelfTest.Smoke.FixtureInequality"), Probe::EResultStatus::Passed);
    RequiredOutcomes.Add(TEXT("Probe.SelfTest.Smoke.FixtureValueMismatch"), Probe::EResultStatus::Failed);
    RequiredOutcomes.Add(TEXT("Probe.SelfTest.Smoke.StopAfterFailure"), Probe::EResultStatus::Failed);
    RequiredOutcomes.Add(TEXT("Probe.SelfTest.Smoke.MissingProvider"), Probe::EResultStatus::Error);
    TSet<FString> Seen;
    for (const Probe::FSelfTestCaseResult& Case : Report.Cases)
    {
        const Probe::EResultStatus* Expected = RequiredOutcomes.Find(Case.CaseId);
        TestNotNull(FString::Printf(TEXT("Known test ID: %s"), *Case.CaseId), Expected);
        TestFalse(FString::Printf(TEXT("Unique test ID: %s"), *Case.CaseId), Seen.Contains(Case.CaseId));
        Seen.Add(Case.CaseId);
        if (Expected)
        {
            TestEqual(FString::Printf(TEXT("Status for %s"), *Case.CaseId),
                static_cast<int32>(Case.ObservedStatus), static_cast<int32>(*Expected));
        }
        TestTrue(FString::Printf(TEXT("Count/diagnostic oracle: %s"), *Case.CaseId), Case.bOraclePassed);
        TestEqual(FString::Printf(TEXT("Executed steps: %s"), *Case.CaseId),
            Case.ObservedStepCount, Case.ExpectedStepCount);
    }
    TestEqual(TEXT("No changes to Editor actor selection"),
        GEditor ? GEditor->GetSelectedActorCount() : -1, InitialSelectionCount);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeSelfTestFrameworkBridge,
    "Probe.SelfTest.AutomationFrameworkBridge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeSelfTestFrameworkBridge::RunTest(const FString& Parameters)
{
    // Proof of concept for executing a Probe-authored scenario from native Automation.
    // This does NOT yet dynamically register arbitrary .uasset tests or Gauntlet packs.
    UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage());
    FProbeTestStep Enabled;
    Enabled.Type = EProbeStepType::PluginEnabled;
    Enabled.Target = TEXT("Probe");
    Asset->Steps.Add(Enabled);
    FProbeTestStep Reflected;
    Reflected.Type = EProbeStepType::ClassHasProperty;
    Reflected.Target = TEXT("/Script/Engine.Actor");
    Reflected.PropertyName = TEXT("Tags");
    Asset->Steps.Add(Reflected);
    const Probe::FTestResult Outcome = Probe::RunTest(Asset);
    TestEqual(TEXT("Probe scenario executed through Automation Framework"),
        static_cast<int32>(Outcome.Status), static_cast<int32>(Probe::EResultStatus::Passed));
    TestEqual(TEXT("Both authored steps executed"), Outcome.Steps.Num(), 2);
    for (const Probe::FStepResult& Step : Outcome.Steps)
    {
        TestEqual(TEXT("Every bridge step passed"),
            static_cast<int32>(Step.Status), static_cast<int32>(Probe::EResultStatus::Passed));
        TestFalse(TEXT("Runner records a specific assertion ID"), Step.AssertionId.IsEmpty());
    }
    return true;
}


// 0.15.1: Test runner's failure isolation with two independently constructed tests.
// Unlike the in-memory smoke oracle, this test asserts the runner's effects directly.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeSelfTestFailureIsolation,
    "Probe.SelfTest.FailureIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeSelfTestFailureIsolation::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Failing = NewObject<UProbeTestAsset>(GetTransientPackage());
    FProbeTestStep Create;
    Create.Type = EProbeStepType::FixtureCreateObject;
    Create.Target = TEXT("/Script/ProbeEditor.ProbeTestAsset");
    Create.ExpectedValue = TEXT("IsolationFixture");
    Failing->Steps.Add(Create);
    FProbeTestStep ForceFailure;
    ForceFailure.Type = EProbeStepType::PluginEnabled;
    ForceFailure.Target = TEXT("ProbeSelfTest_Plugin_DoesNotExist");
    Failing->Steps.Add(ForceFailure);
    FProbeTestStep ShouldNotExecute;
    ShouldNotExecute.Type = EProbeStepType::FixtureDestroyObject;
    ShouldNotExecute.Target = TEXT("IsolationFixture");
    Failing->Steps.Add(ShouldNotExecute);

    const Probe::FTestResult Failed = Probe::RunTest(Failing);
    TestEqual(TEXT("The deliberately failing run failed"), static_cast<int32>(Failed.Status),
        static_cast<int32>(Probe::EResultStatus::Failed));
    TestEqual(TEXT("No step executes after the failure"), Failed.Steps.Num(), 2);
    TestEqual(TEXT("Authored steps were not modified"), Failing->Steps.Num(), 3);

    UProbeTestAsset* Inspect = NewObject<UProbeTestAsset>(GetTransientPackage());
    FProbeTestStep Exists;
    Exists.Type = EProbeStepType::FixtureObjectExists;
    Exists.Target = TEXT("IsolationFixture");
    Inspect->Steps.Add(Exists);
    const Probe::FTestResult Isolated = Probe::RunTest(Inspect);
    TestEqual(TEXT("A second run cannot see the previous run's fixture"),
        static_cast<int32>(Isolated.Status), static_cast<int32>(Probe::EResultStatus::Failed));
    TestEqual(TEXT("The second run executes exactly one step"), Isolated.Steps.Num(), 1);

    // The original test should behave identically on repeat execution.
    const Probe::FTestResult Repeated = Probe::RunTest(Failing);
    TestEqual(TEXT("Repeat run still detects expected failure"),
        static_cast<int32>(Repeated.Status), static_cast<int32>(Probe::EResultStatus::Failed));
    TestEqual(TEXT("Repeat run has same executed-step count"), Repeated.Steps.Num(), Failed.Steps.Num());
    return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
