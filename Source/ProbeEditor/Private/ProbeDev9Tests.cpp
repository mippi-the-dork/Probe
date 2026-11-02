#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ProbeDev9Workflow.h"
#include "UObject/Package.h"

namespace
{
    UProbeTestAsset* NewScenario()
    {
        return NewObject<UProbeTestAsset>(GetTransientPackage(), UProbeTestAsset::StaticClass(), NAME_None, RF_Transient);
    }
    FProbeTestStep Installed(const FString& Plugin, const FString& Group = FString())
    {
        FProbeTestStep Step;
        Step.Type = EProbeStepType::PluginInstalled;
        Step.Target = Plugin;
        Step.GroupName = Group;
        return Step;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev9VariationResolution,
    "Probe.Editor.Dev9VariationResolution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev9VariationResolution::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Asset = NewScenario();
    FProbeTestStep Step = Installed(TEXT("${Plugin}"));
    Step.Description = TEXT("Verify ${Plugin} is installed");
    Step.ExpectedValue = TEXT("${Expectation}");
    Asset->Steps.Add(Step);
    FProbeTestVariation Variant;
    Variant.Name = TEXT("Win64");
    Variant.Bindings = TEXT("Plugin=Probe;Expectation=Value=WithEquals");
    Asset->Variations.Add(Variant);
    TArray<FProbeTestStep> Expanded;
    FString Name, Error;
    TestTrue(TEXT("Enabled variant resolves"), ProbeDev9Workflow::Resolve(*Asset, 0, Expanded, Name, Error));
    TestEqual(TEXT("Selected variation name"), Name, FString(TEXT("Win64")));
    TestEqual(TEXT("Target substitution"), Expanded[0].Target, FString(TEXT("Probe")));
    TestEqual(TEXT("Descriptions are substituted"), Expanded[0].Description, FString(TEXT("Verify Probe is installed")));
    TestEqual(TEXT("Equals in value retained"), Expanded[0].ExpectedValue, FString(TEXT("Value=WithEquals")));
    TestEqual(TEXT("Original step remains intact"), Asset->Steps[0].Target, FString(TEXT("${Plugin}")));
    TestTrue(TEXT("Baseline run ignores substitution"), ProbeDev9Workflow::Resolve(*Asset, INDEX_NONE, Expanded, Name, Error));
    TestEqual(TEXT("Unparameterized baseline untouched"), Expanded[0].Target, FString(TEXT("${Plugin}")));
    Asset->Variations[0].Bindings = TEXT("Other=Probe");
    TestFalse(TEXT("Unknown placeholder rejected"), ProbeDev9Workflow::Resolve(*Asset, 0, Expanded, Name, Error));
    TestTrue(TEXT("Unknown placeholder names the step"), Error.Contains(TEXT("Step 1")));
    Asset->Variations[0].Bindings = TEXT("Plugin=Probe;plugin=Other");
    TestFalse(TEXT("Case-insensitive duplicate rejected"), ProbeDev9Workflow::Resolve(*Asset, 0, Expanded, Name, Error));
    Asset->Variations[0].Bindings = TEXT("Plugin=Probe;Expectation=Good");
    FProbeTestVariation Duplicate = Asset->Variations[0];
    Duplicate.Name = TEXT("win64");
    Asset->Variations.Add(Duplicate);
    TestFalse(TEXT("Case-insensitive variation names unique"), ProbeDev9Workflow::Resolve(*Asset, 0, Expanded, Name, Error));
    TestFalse(TEXT("Bad variation index rejected"), ProbeDev9Workflow::Resolve(*Asset, 3, Expanded, Name, Error));
    TestFalse(TEXT("Variation cannot authorize UI input"), Asset->bAllowEditorUIInput);
    TestFalse(TEXT("Variation cannot authorize transient actors"), Asset->bAllowTransientEditorActors);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev9LifecyclePolicy,
    "Probe.Editor.Dev9LifecyclePolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev9LifecyclePolicy::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Asset = NewScenario();
    Asset->SetupGroupName = TEXT("Setup");
    Asset->CleanupGroupName = TEXT("Cleanup");
    Asset->Steps = {Installed(TEXT("Probe"), TEXT("Setup")), Installed(TEXT("Probe"), TEXT("Verify")),
                    Installed(TEXT("Probe"), TEXT("Cleanup")), Installed(TEXT("Probe"), TEXT("Cleanup"))};
    Probe::FRunOptions Options;
    ProbeDev9Workflow::FLifecycle Phase;
    FString Error;
    TestTrue(TEXT("Valid edge groups accepted"), ProbeDev9Workflow::ValidateLifecycle(*Asset, Options, Phase, Error));
    TestEqual(TEXT("Cleanup begins at third step"), Phase.CleanupFirst, 2);
    TestEqual(TEXT("Cleanup includes fourth step"), Phase.CleanupLast, 3);
    Options.FirstStepIndex = 1;
    Options.LastStepIndex = 1;
    TestTrue(TEXT("Scoped group runs do not automatically enter cleanup"), ProbeDev9Workflow::ValidateLifecycle(*Asset, Options, Phase, Error));
    TestFalse(TEXT("Scoped run has no forced cleanup"), Phase.HasCleanup());
    Options = Probe::FRunOptions();
    Asset->Steps[3].GroupName = TEXT("Not Cleanup");
    TestFalse(TEXT("Cleanup must be at last edge"), ProbeDev9Workflow::ValidateLifecycle(*Asset, Options, Phase, Error));
    Asset->Steps[3].GroupName = TEXT("Cleanup");
    // A longer, *contiguous* cleanup suffix is valid. The original Dev 9
    // fixture changed step 2 to Cleanup without separating that step from
    // steps 3-4, so it incorrectly expected rejection of a valid group.
    Asset->Steps[1].GroupName = TEXT("Cleanup");
    TestTrue(TEXT("Three adjacent cleanup steps form one valid group"),
        ProbeDev9Workflow::ValidateLifecycle(*Asset, Options, Phase, Error));
    TestEqual(TEXT("Expanded cleanup begins at second step"), Phase.CleanupFirst, 1);
    TestEqual(TEXT("Expanded cleanup ends at fourth step"), Phase.CleanupLast, 3);
    // Actual disjoint groups: Cleanup, Verify, Cleanup. The trailing
    // cleanup block is separated from the earlier occurrence.
    Asset->Steps[2].GroupName = TEXT("Verify");
    TestFalse(TEXT("Disjoint cleanup groups rejected"),
        ProbeDev9Workflow::ValidateLifecycle(*Asset, Options, Phase, Error));
    TestTrue(TEXT("Disjoint cleanup diagnostic explains the error"),
        Error.Contains(TEXT("multiple separate blocks")));
    Asset->Steps[1].GroupName = TEXT("Verify");
    Asset->Steps[2].GroupName = TEXT("Cleanup");
    Asset->SetupGroupName = TEXT("Cleanup");
    TestFalse(TEXT("Setup cannot share cleanup block"), ProbeDev9Workflow::ValidateLifecycle(*Asset, Options, Phase, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev9ParameterizedExecution,
    "Probe.Runner.Dev9ParameterizedExecution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev9ParameterizedExecution::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Asset = NewScenario();
    Asset->Steps.Add(Installed(TEXT("${Plugin}")));
    FProbeTestVariation Yes;
    Yes.Name = TEXT("Installed");
    Yes.Bindings = TEXT("Plugin=Probe");
    FProbeTestVariation No;
    No.Name = TEXT("Missing");
    No.Bindings = TEXT("Plugin=ProbeDev9NonexistentPlugin");
    Asset->Variations = {Yes, No};
    Probe::FRunOptions Options;
    Options.VariationIndex = 0;
    const Probe::FTestResult Pass = Probe::RunTest(Asset, Options);
    TestEqual(TEXT("Installed variation passes"), static_cast<int32>(Pass.Status), static_cast<int32>(Probe::EResultStatus::Passed));
    TestEqual(TEXT("Name contains variation"), Pass.VariationName, FString(TEXT("Installed")));
    TestEqual(TEXT("Identity index recorded"), Pass.VariationIndex, 0);
    TestEqual(TEXT("One step executed"), Pass.Steps.Num(), 1);
    TestEqual(TEXT("Full run historical summary preserved"), Pass.Detail, FString(TEXT("All 1 steps passed")));
    Options.VariationIndex = 1;
    const Probe::FTestResult Fail = Probe::RunTest(Asset, Options);
    TestEqual(TEXT("Missing variation fails"), static_cast<int32>(Fail.Status), static_cast<int32>(Probe::EResultStatus::Failed));
    TestEqual(TEXT("Failure retains variation name"), Fail.VariationName, FString(TEXT("Missing")));
    TestEqual(TEXT("Missing status preserved"), static_cast<int32>(Fail.Steps[0].Status), static_cast<int32>(Probe::EResultStatus::Failed));
    TestEqual(TEXT("Original definition not modified"), Asset->Steps[0].Target, FString(TEXT("${Plugin}")));
    TestFalse(TEXT("No implicit authorization"), Asset->bAllowEditorUIInput);
    Options.VariationIndex = 99;
    TestEqual(TEXT("Invalid variation fails closed"), static_cast<int32>(Probe::RunTest(Asset, Options).Status), static_cast<int32>(Probe::EResultStatus::Error));
    Asset->Variations[1].bEnabled = false;
    TestEqual(TEXT("Only enabled variations selected"), ProbeDev9Workflow::EnabledVariationIndices(*Asset).Num(), 1);
    TestFalse(TEXT("Bulk variations opt-in"), Asset->bRunVariationsInBulk);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev9CleanupOnFailure,
    "Probe.Runner.Dev9CleanupOnFailure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev9CleanupOnFailure::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Asset = NewScenario();
    Asset->SetupGroupName = TEXT("Setup");
    Asset->CleanupGroupName = TEXT("Cleanup");
    Asset->Steps = {
        Installed(TEXT("Probe"), TEXT("Setup")),
        Installed(TEXT("ProbeDev9NonexistentPlugin"), TEXT("Verify")),
        Installed(TEXT("Probe"), TEXT("Verify")),
        Installed(TEXT("Probe"), TEXT("Cleanup")),
        Installed(TEXT("Probe"), TEXT("Cleanup"))
    };
    const Probe::FTestResult Failure = Probe::RunTest(Asset);
    TestEqual(TEXT("Primary failure remains failed"), static_cast<int32>(Failure.Status), static_cast<int32>(Probe::EResultStatus::Failed));
    TestEqual(TEXT("All 5 steps reported"), Failure.Steps.Num(), 5);
    if (Failure.Steps.Num() == 5)
    {
        TestEqual(TEXT("Verification failure is reported"), static_cast<int32>(Failure.Steps[1].Status), static_cast<int32>(Probe::EResultStatus::Failed));
        TestEqual(TEXT("Unreached verification is skipped"), static_cast<int32>(Failure.Steps[2].Status), static_cast<int32>(Probe::EResultStatus::Skipped));
        TestEqual(TEXT("Cleanup first step executes"), static_cast<int32>(Failure.Steps[3].Status), static_cast<int32>(Probe::EResultStatus::Passed));
        TestEqual(TEXT("Cleanup second step executes"), static_cast<int32>(Failure.Steps[4].Status), static_cast<int32>(Probe::EResultStatus::Passed));
    }
    TestTrue(TEXT("Primary failure is not replaced by cleanup result"), Failure.Detail.Contains(TEXT("Step 2")));
    Asset->Steps[3] = Installed(TEXT("ProbeDev9NonexistentPlugin"), TEXT("Cleanup"));
    const Probe::FTestResult CleanupFailure = Probe::RunTest(Asset);
    TestEqual(TEXT("Cleanup failure retains original failed status"), static_cast<int32>(CleanupFailure.Status), static_cast<int32>(Probe::EResultStatus::Failed));
    TestEqual(TEXT("Cleanup continues after its own failed assertion"), static_cast<int32>(CleanupFailure.Steps.Last().Status), static_cast<int32>(Probe::EResultStatus::Passed));
    TestTrue(TEXT("Cleanup failure diagnostic appended"), CleanupFailure.Detail.Contains(TEXT("Cleanup step 4")));
    Asset->Steps[3] = Installed(TEXT("Probe"), TEXT("Cleanup"));
    Probe::FRunOptions Scoped;
    Scoped.FirstStepIndex = 1;
    Scoped.LastStepIndex = 1;
    const Probe::FTestResult GroupOnly = Probe::RunTest(Asset, Scoped);
    TestEqual(TEXT("Scoped run executes only the chosen step"), GroupOnly.Steps.Num(), 1);
    TestEqual(TEXT("Scoped step retains original index"), GroupOnly.Steps[0].Index, 2);
    Asset->CleanupGroupName.Empty();
    const Probe::FTestResult Legacy = Probe::RunTest(Asset);
    TestEqual(TEXT("Legacy stop on first failure"), Legacy.Steps.Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev9VariationIsolation,
    "Probe.Runner.Dev9VariationIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev9VariationIsolation::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Asset = NewScenario();
    Asset->SetupGroupName = TEXT("Setup");
    Asset->CleanupGroupName = TEXT("Cleanup");
    FProbeTestStep Create;
    Create.Type = EProbeStepType::FixtureCreateObject;
    Create.Target = TEXT("/Script/ProbeEditor.ProbeTestAsset");
    Create.ExpectedValue = TEXT("${Id}");
    Create.GroupName = TEXT("Setup");
    FProbeTestStep Exists;
    Exists.Type = EProbeStepType::FixtureObjectExists;
    Exists.Target = TEXT("${Id}");
    Exists.GroupName = TEXT("Verify");
    FProbeTestStep Destroy = Exists;
    Destroy.Type = EProbeStepType::FixtureDestroyObject;
    Destroy.GroupName = TEXT("Cleanup");
    Asset->Steps = {Create, Exists, Destroy};
    FProbeTestVariation A;
    A.Name = TEXT("VariantA"); A.Bindings = TEXT("Id=FixtureA");
    FProbeTestVariation B;
    B.Name = TEXT("VariantB"); B.Bindings = TEXT("Id=FixtureB");
    Asset->Variations = {A, B};
    Probe::FRunOptions Options;
    for (int32 Index = 0; Index < 2; ++Index)
    {
        Options.VariationIndex = Index;
        const Probe::FTestResult Run = Probe::RunTest(Asset, Options);
        TestEqual(*FString::Printf(TEXT("Variation %d succeeds"), Index), static_cast<int32>(Run.Status),
            static_cast<int32>(Probe::EResultStatus::Passed));
        TestEqual(*FString::Printf(TEXT("Variation %d executes all 3 steps"), Index), Run.Steps.Num(), 3);
    }
    UProbeTestAsset* Reader = NewScenario();
    FProbeTestStep Lookup;
    Lookup.Type = EProbeStepType::FixtureObjectExists;
    Lookup.Target = TEXT("FixtureA");
    Reader->Steps.Add(Lookup);
    TestEqual(TEXT("Temporary fixture from previous variation not shared"),
        static_cast<int32>(Probe::RunTest(Reader).Status), static_cast<int32>(Probe::EResultStatus::Failed));
    TestEqual(TEXT("Authored fixture ID remains a placeholder"), Asset->Steps[0].ExpectedValue,
        FString(TEXT("${Id}")));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
