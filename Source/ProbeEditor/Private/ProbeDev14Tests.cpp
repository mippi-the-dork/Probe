#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ProbeDev14Authoring.h"
#include "ProbeTestRunner.h"
#include "ProbeQueueControl.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

namespace
{
    UProbeTestAsset* NewTest()
    {
        UProbeTestAsset* Test = NewObject<UProbeTestAsset>(GetTransientPackage(), NAME_None, RF_Transient);
        Test->bNeedsConfiguration = false;
        return Test;
    }
    FProbeTestStep PluginStep(const FString& Value)
    {
        FProbeTestStep Step;
        Step.Type = EProbeStepType::PluginInstalled;
        Step.Target = Value;
        return Step;
    }
    FProbeTestStep NamedStep(const FString& Label)
    {
        FProbeTestStep Step = PluginStep(TEXT("Probe"));
        Step.Description = Label;
        return Step;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev14TestPreflight,
    "Probe.Editor.Dev14TestPreflight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev14TestPreflight::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Test = NewTest();
    Test->Steps.Add(PluginStep(TEXT("Probe")));
    TestTrue(TEXT("Valid read-only test preflights successfully"), ProbeDev14::PreflightTest(*Test).Ready());
    Test->Steps[0].Target.Reset();
    TestFalse(TEXT("Missing target is a blocking authoring issue"), ProbeDev14::PreflightTest(*Test).Ready());
    Test->Steps[0].Target = TEXT("id:Dev14.Input");
    Test->Steps[0].Type = EProbeStepType::UIWidgetClick;
    const auto WithoutPermission = ProbeDev14::PreflightTest(*Test);
    TestFalse(TEXT("Native input without explicit authorization is blocked"), WithoutPermission.Ready());
    Test->bAllowEditorUIInput = true;
    TestTrue(TEXT("Explicit permission removes authorization issue"), ProbeDev14::PreflightTest(*Test).Ready());
    Test->bNeedsConfiguration = true;
    TestFalse(TEXT("Unreviewed asset is never ready"), ProbeDev14::PreflightTest(*Test).Ready());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev14VariationsPreflight,
    "Probe.Editor.Dev14VariationPreflight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev14VariationsPreflight::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Test = NewTest();
    Test->Steps.Add(PluginStep(TEXT("${Plugin}")));
    FProbeTestVariation V;
    V.Name = TEXT("Case A"); V.Bindings = TEXT("Plugin=Probe");
    Test->Variations.Add(V);
    TestTrue(TEXT("Bound variation resolves without editing saved steps"), ProbeDev14::PreflightTest(*Test).Ready());
    TestEqual(TEXT("Original placeholder remains in Test Asset"), Test->Steps[0].Target, FString(TEXT("${Plugin}")));
    Test->Variations[0].Bindings = TEXT("Wrong=Probe");
    TestFalse(TEXT("Unknown placeholder reports error before run"), ProbeDev14::PreflightTest(*Test).Ready());
    Test->Variations[0].Bindings = TEXT("Plugin=Probe");
    Test->Variations.Add(V);
    TestFalse(TEXT("Duplicate enabled variation names blocked"), ProbeDev14::PreflightTest(*Test).Ready());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev14BulkMovement,
    "Probe.Editor.Dev14BulkMovement", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev14BulkMovement::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Test = NewTest();
    for (int32 I = 0; I < 5; ++I) { Test->Steps.Add(NamedStep(FString::FromInt(I))); }
    TSet<int32> Selected; Selected.Add(1); Selected.Add(2);
    TestTrue(TEXT("Selected block moves up"), ProbeDev14::ApplyBulk(*Test, Selected, ProbeDev14::EBulkAction::MoveUp));
    TestEqual(TEXT("First element moved without reordering selected steps"), Test->Steps[0].Description, FString(TEXT("1")));
    TestEqual(TEXT("Second selected element stays adjacent"), Test->Steps[1].Description, FString(TEXT("2")));
    TestTrue(TEXT("Selection follows moved step"), Selected.Contains(0) && Selected.Contains(1));
    TestTrue(TEXT("Selected block moves back down"), ProbeDev14::ApplyBulk(*Test, Selected, ProbeDev14::EBulkAction::MoveDown));
    for (int32 I = 0; I < 5; ++I)
    { TestEqual(*FString::Printf(TEXT("Stable step order %d"), I), Test->Steps[I].Description, FString::FromInt(I)); }
    TestTrue(TEXT("Group assigned to selected steps"), ProbeDev14::ApplyBulk(*Test, Selected, ProbeDev14::EBulkAction::AssignGroup, TEXT("Verify")));
    TestEqual(TEXT("Group 1"), Test->Steps[1].GroupName, FString(TEXT("Verify")));
    TestEqual(TEXT("Group 2"), Test->Steps[2].GroupName, FString(TEXT("Verify")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev14BulkSafety,
    "Probe.Editor.Dev14BulkSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev14BulkSafety::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Test = NewTest();
    for (int32 I = 0; I < 3; ++I) { Test->Steps.Add(NamedStep(FString::FromInt(I))); }
    TSet<int32> Selected; Selected.Add(0); Selected.Add(2);
    TestTrue(TEXT("Safe duplicate"), ProbeDev14::ApplyBulk(*Test, Selected, ProbeDev14::EBulkAction::Duplicate));
    TestEqual(TEXT("Two steps duplicated"), Test->Steps.Num(), 5);
    TestTrue(TEXT("New copies selected"), Selected.Contains(1) && Selected.Contains(4));
    TestEqual(TEXT("Original remains"), Test->Steps[0].Description, FString(TEXT("0")));
    TestEqual(TEXT("Duplicate matches"), Test->Steps[1].Description, FString(TEXT("0")));
    TestTrue(TEXT("Disable selected only"), ProbeDev14::ApplyBulk(*Test, Selected, ProbeDev14::EBulkAction::Disable));
    TestFalse(TEXT("Duplicate disabled"), Test->Steps[1].bEnabled);
    TestTrue(TEXT("Original still enabled"), Test->Steps[0].bEnabled);
    TestTrue(TEXT("Remove duplicates"), ProbeDev14::ApplyBulk(*Test, Selected, ProbeDev14::EBulkAction::Delete));
    TestEqual(TEXT("Original step count restored"), Test->Steps.Num(), 3);
    TestTrue(TEXT("Selection cleared after removal"), Selected.IsEmpty());
    TSet<int32> Invalid; Invalid.Add(100);
    TestFalse(TEXT("Invalid selection is rejected before changes"), ProbeDev14::ApplyBulk(*Test, Invalid, ProbeDev14::EBulkAction::Delete));
    TestEqual(TEXT("Unmodified on invalid selection"), Test->Steps.Num(), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev14RecipeContracts,
    "Probe.Editor.Dev14RecipeContracts", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev14RecipeContracts::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Test = NewTest();
    TestFalse(TEXT("No implicit live input permission"), Test->bAllowEditorUIInput);
    TestFalse(TEXT("No implicit actor fixture permission"), Test->bAllowTransientEditorActors);
    TestTrue(TEXT("Transient fixture recipe created"), ProbeDev14::AppendRecipe(*Test, ProbeDev14::ERecipe::TransientFixture));
    TestEqual(TEXT("Full three-stage recipe"), Test->Steps.Num(), 3);
    TestEqual(TEXT("Lifecycle setup configured"), Test->SetupGroupName, FString(TEXT("Setup")));
    TestEqual(TEXT("Lifecycle cleanup configured"), Test->CleanupGroupName, FString(TEXT("Cleanup")));
    TestTrue(TEXT("Requires explicit review"), Test->bNeedsConfiguration);
    TestFalse(TEXT("No permissions silently granted"), Test->bAllowEditorUIInput || Test->bAllowTransientEditorActors);
    TestFalse(TEXT("Cannot move existing cleanup away from end"), ProbeDev14::AppendRecipe(*Test, ProbeDev14::ERecipe::PluginHealth));
    TestEqual(TEXT("Unchanged lifecycle step count"), Test->Steps.Num(), 3);
    TestEqual(TEXT("Original lifecycle metadata never silently rewritten"), Test->CleanupGroupName, FString(TEXT("Cleanup")));
    TestEqual(TEXT("Editor State recipe has two steps"), ProbeDev14::BuildRecipe(ProbeDev14::ERecipe::EditorState).Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev14PackReadiness,
    "Probe.Editor.Dev14PackReadiness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev14PackReadiness::RunTest(const FString& Parameters)
{
    UProbeTestPack* Pack = NewObject<UProbeTestPack>(GetTransientPackage(), NAME_None, RF_Transient);
    UProbeTestAsset* Test = NewTest();
    Test->Steps.Add(PluginStep(TEXT("Probe")));
    const FString TestPath = Test->GetPathName();
    Pack->Tests.Add(TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(TestPath)));
    auto Resolver = [Test, TestPath](const FString& Path) -> const UProbeTestAsset*
    { return Path == TestPath ? Test : nullptr; };
    TestTrue(TEXT("Valid Pack has no blocking issues"), ProbeDev14::PreflightPack(*Pack, Resolver).Ready());
    Test->bNeedsConfiguration = true;
    TestFalse(TEXT("Needs-configuration member blocks Pack readiness"), ProbeDev14::PreflightPack(*Pack, Resolver).Ready());
    Test->bNeedsConfiguration = false;
    TestFalse(TEXT("Unresolvable member reported without dereference"), ProbeDev14::PreflightPack(*Pack,
        [](const FString&) -> const UProbeTestAsset* { return nullptr; }).Ready());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev14ProgressCallback,
    "Probe.Runner.Dev14ProgressCallback", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev14ProgressCallback::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Test = NewTest();
    Test->Steps.Add(PluginStep(TEXT("Probe")));
    TArray<int32> Steps;
    bool bCompleted = false;
    Probe::RunTestAsync(Test, [&bCompleted](Probe::FTestResult Result)
    { bCompleted = true; }, Probe::FRunOptions(), [&Steps](int32 Step, int32 Total)
    { Steps.Add(Step); });
    TestTrue(TEXT("Read-only runner completes on game thread"), bCompleted);
    TestEqual(TEXT("Start and completion progress callbacks"), Steps.Num(), 2);
    if (Steps.Num() >= 2)
    {
        TestEqual(TEXT("Initial step number"), Steps[0], 0);
        TestEqual(TEXT("Completed step number"), Steps[1], 1);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev14StopBoundary,
    "Probe.Runner.Dev14StopBoundary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev14StopBoundary::RunTest(const FString& Parameters)
{
    TArray<Probe::FTestResult> Results;
    Results.SetNum(3);
    Results[0].Status = Probe::EResultStatus::Passed;
    Results[1].Status = Probe::EResultStatus::Failed; // Active test completed; immutable.
    Results[2].Status = Probe::EResultStatus::Error; // Unstarted placeholder only.
    TArray<int32> Pending;
    Pending.Add(2);
    const int32 Skipped = ProbeQueueControl::SkipPending(Pending, Results, TEXT("Stopped after active test."));
    TestEqual(TEXT("One unstarted test skipped"), Skipped, 1);
    TestTrue(TEXT("Pending queue cleared"), Pending.IsEmpty());
    TestTrue(TEXT("Completed failing test is never rewritten"), Results[1].Status == Probe::EResultStatus::Failed);
    TestTrue(TEXT("Earlier successful test is untouched"), Results[0].Status == Probe::EResultStatus::Passed);
    TestTrue(TEXT("Unstarted test marked skipped"), Results[2].Status == Probe::EResultStatus::Skipped);
    TestTrue(TEXT("Stop reason preserved"), Results[2].Detail.Contains(TEXT("Stopped after active test")));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
