#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ProbeDev8Workflow.h"
#include "UObject/Package.h"

namespace
{
    FString BuiltInId(EProbeStepType Type)
    {
        return FString::Printf(TEXT("Probe.BuiltIn.%s"),
            *StaticEnum<EProbeStepType>()->GetNameStringByValue(static_cast<int64>(Type)));
    }
    Probe::FStepResult MakeStep(int32 Index, EProbeStepType Type, Probe::EResultStatus Status,
        const FString& Description, const FString& Group = FString())
    {
        Probe::FStepResult Step;
        Step.Index = Index;
        Step.AssertionId = BuiltInId(Type);
        Step.Status = Status;
        Step.Description = Description;
        Step.GroupName = Group;
        Step.Detail = TEXT("Observed: expected diagnostic");
        Step.DurationMs = 2.5;
        return Step;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev8GroupDuplication,
    "Probe.Editor.Dev8GroupDuplication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev8GroupDuplication::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Test = NewObject<UProbeTestAsset>(GetTransientPackage(), UProbeTestAsset::StaticClass(), NAME_None, RF_Transient);
    FProbeTestStep A;
    A.Type = EProbeStepType::PluginInstalled;
    A.Target = TEXT("Probe");
    A.GroupName = TEXT("Setup");
    A.bEnabled = false;
    A.UIWaitTimeoutSeconds = 9.0;
    FProbeTestStep B = A;
    B.Type = EProbeStepType::PluginEnabled;
    B.bEnabled = true;
    FProbeTestStep C = A;
    C.GroupName = TEXT("Cleanup");
    Test->Steps = {A, B, C};
    TestFalse(TEXT("No implicit Editor UI input permission"), Test->bAllowEditorUIInput);
    const int32 OriginalCount = Test->Steps.Num();
    ProbeStepGroups::FRange Out;
    TestTrue(TEXT("Named group duplicates"), ProbeDev8Workflow::DuplicateGroup(*Test, 1, Out));
    TestEqual(TEXT("Duplicated two steps"), Out.Num(), 2);
    TestEqual(TEXT("Copies placed after original block"), Out.First, 2);
    TestEqual(TEXT("Array size updated"), Test->Steps.Num(), OriginalCount + 2);
    TestEqual(TEXT("Original group not renamed"), Test->Steps[0].GroupName, FString(TEXT("Setup")));
    TestEqual(TEXT("Copy group renamed"), Test->Steps[2].GroupName, FString(TEXT("Setup Copy")));
    TestEqual(TEXT("Second copied step in same group"), Test->Steps[3].GroupName, FString(TEXT("Setup Copy")));
    TestEqual(TEXT("Following group not displaced incorrectly"), Test->Steps[4].GroupName, FString(TEXT("Cleanup")));
    TestEqual(TEXT("Copied disabled state retained"), Test->Steps[2].bEnabled, false);
    TestEqual(TEXT("Copied enabled state retained"), Test->Steps[3].bEnabled, true);
    TestEqual(TEXT("Copied timeout retained"), Test->Steps[2].UIWaitTimeoutSeconds, 9.0);
    TestEqual(TEXT("Copied target retained"), Test->Steps[3].Target, FString(TEXT("Probe")));
    TestFalse(TEXT("Copy cannot authorize input"), Test->bAllowEditorUIInput);
    TestFalse(TEXT("Copy cannot authorize actor fixtures"), Test->bAllowTransientEditorActors);
    TestTrue(TEXT("Duplicating again gets unique group name"), ProbeDev8Workflow::DuplicateGroup(*Test, 0, Out));
    TestEqual(TEXT("Second generated group name"), Test->Steps[2].GroupName, FString(TEXT("Setup Copy 2")));
    TestFalse(TEXT("Out of bounds is rejected"), ProbeDev8Workflow::DuplicateGroup(*Test, 100, Out));
    TestFalse(TEXT("Rejected duplication has invalid range"), Out.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev8FailureTriage,
    "Probe.Editor.Dev8FailureTriage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev8FailureTriage::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Test = NewObject<UProbeTestAsset>(GetTransientPackage(), UProbeTestAsset::StaticClass(), NAME_None, RF_Transient);
    FProbeTestStep Setup;
    Setup.Type = EProbeStepType::PluginInstalled;
    Setup.GroupName = TEXT("Setup");
    FProbeTestStep Check = Setup;
    Check.Type = EProbeStepType::PluginEnabled;
    Check.GroupName = TEXT("Check");
    FProbeTestStep Check2 = Check;
    FProbeTestStep Cleanup = Setup;
    Cleanup.GroupName = TEXT("Cleanup");
    Test->Steps = {Setup, Check, Check2, Cleanup};
    Probe::FTestResult Result;
    Result.AssetPath = Test->GetPathName();
    Result.Status = Probe::EResultStatus::Failed;
    Result.Steps = {
        MakeStep(1, Setup.Type, Probe::EResultStatus::Passed, TEXT("setup"), TEXT("Setup")),
        MakeStep(2, Check.Type, Probe::EResultStatus::Failed, TEXT("check"), TEXT("Check"))
    };
    const ProbeDev8Workflow::FFailureScope Scope = ProbeDev8Workflow::FirstFailedGroup(Result, Test);
    TestTrue(TEXT("Failed step has a retry scope"), Scope.bValid);
    TestEqual(TEXT("Scope begins at group start"), Scope.Options.FirstStepIndex, 1);
    TestEqual(TEXT("Scope includes whole group"), Scope.Options.LastStepIndex, 2);
    TestEqual(TEXT("Original failing step number"), Scope.FailedStepNumber, 2);
    TestTrue(TEXT("No implicit setup promise"), Scope.Explanation.Contains(TEXT("NOT execute")));
    Test->Steps[1].bEnabled = false;
    TestFalse(TEXT("Disabled failed step cannot be retried"), ProbeDev8Workflow::FirstFailedGroup(Result, Test).bValid);
    Test->Steps[1].bEnabled = true;
    Test->Steps[1].Type = EProbeStepType::EditorWorldExists;
    TestFalse(TEXT("Changed assertion type cannot be retried"), ProbeDev8Workflow::FirstFailedGroup(Result, Test).bValid);
    Test->Steps[1] = Check;
    Result.AssetPath = TEXT("/Game/Other.Other");
    TestFalse(TEXT("Result from another asset rejected"), ProbeDev8Workflow::FirstFailedGroup(Result, Test).bValid);
    Result.AssetPath = Test->GetPathName();
    Result.Steps.Reset();
    TestFalse(TEXT("Failures without a recorded step need full rerun"), ProbeDev8Workflow::FirstFailedGroup(Result, Test).bValid);
    Result.Status = Probe::EResultStatus::Passed;
    Result.Steps.Add(MakeStep(2, Check.Type, Probe::EResultStatus::Failed, TEXT("check")));
    TestFalse(TEXT("Successful results never offer retry"), ProbeDev8Workflow::FirstFailedGroup(Result, Test).bValid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev8FailureSummary,
    "Probe.Editor.Dev8FailureSummary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev8FailureSummary::RunTest(const FString& Parameters)
{
    TArray<Probe::FTestResult> Results;
    Probe::FTestResult Passed;
    Passed.Name = TEXT("PassedFixture");
    Passed.Status = Probe::EResultStatus::Passed;
    Results.Add(Passed);
    Probe::FTestResult Failed;
    Failed.Name = TEXT("FailedCheck");
    Failed.AssetPath = TEXT("/Game/Tests/Check.Check");
    Failed.Status = Probe::EResultStatus::Failed;
    Failed.Detail = TEXT("Failed at assertion");
    Failed.Steps.Add(MakeStep(1, EProbeStepType::PluginInstalled,
        Probe::EResultStatus::Failed, TEXT("Plugin missing"), TEXT("Startup")));
    Results.Add(Failed);
    Probe::FTestResult Error;
    Error.Name = TEXT("SetupError");
    Error.AssetPath = TEXT("/Game/Tests/Error.Error");
    Error.Status = Probe::EResultStatus::Error;
    Error.Detail = TEXT("Could not load Test Asset");
    Results.Add(Error);
    const FString Report = ProbeDev8Workflow::BuildFailureSummary(Results);
    TestTrue(TEXT("Failed test included"), Report.Contains(TEXT("FailedCheck")));
    TestTrue(TEXT("Failed step index included"), Report.Contains(TEXT("Step 1")));
    TestTrue(TEXT("Group name included"), Report.Contains(TEXT("Group: Startup")));
    TestTrue(TEXT("Path included"), Report.Contains(TEXT("/Game/Tests/Check.Check")));
    TestTrue(TEXT("No-steps error included"), Report.Contains(TEXT("Could not load Test Asset")));
    TestFalse(TEXT("Passing fixture excluded"), Report.Contains(TEXT("PassedFixture")));
    TestEqual(TEXT("Empty results has explicit message"), ProbeDev8Workflow::BuildFailureSummary(TArray<Probe::FTestResult>()),
        FString(TEXT("Probe failure summary: no failures or errors in this run.")));
    return true;
}
#endif
