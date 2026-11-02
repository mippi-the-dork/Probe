#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ProbeDev10PackPolicy.h"
#include "ProbeDev10RunHistory.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    UProbeTestAsset* MakeTest(const TCHAR* Name)
    {
        return NewObject<UProbeTestAsset>(GetTransientPackage(), UProbeTestAsset::StaticClass(),
            MakeUniqueObjectName(GetTransientPackage(), UProbeTestAsset::StaticClass(), FName(Name)), RF_Transient);
    }
    UProbeTestPack* MakePack()
    {
        return NewObject<UProbeTestPack>(GetTransientPackage(), UProbeTestPack::StaticClass(), NAME_None, RF_Transient);
    }
    TSoftObjectPtr<UProbeTestAsset> Ref(UProbeTestAsset* Test)
    {
        return TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(Test->GetPathName()));
    }
    FProbePackPrerequisite Requires(UProbeTestAsset* Dependent, UProbeTestAsset* Prerequisite)
    {
        FProbePackPrerequisite Rule;
        Rule.Test = Ref(Dependent);
        Rule.Requires = Ref(Prerequisite);
        return Rule;
    }
    Probe::FTestResult Outcome(UProbeTestAsset* Asset, Probe::EResultStatus Status, int32 Variation = INDEX_NONE)
    {
        Probe::FTestResult Result;
        Result.AssetPath = Asset->GetPathName();
        Result.Status = Status;
        Result.VariationIndex = Variation;
        return Result;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev10PackValidation,
    "Probe.Editor.Dev10PackValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev10PackValidation::RunTest(const FString& Parameters)
{
    UProbeTestAsset* First = MakeTest(TEXT("ProbeDev10First"));
    UProbeTestAsset* Second = MakeTest(TEXT("ProbeDev10Second"));
    UProbeTestAsset* Third = MakeTest(TEXT("ProbeDev10Third"));
    UProbeTestPack* Pack = MakePack();
    Pack->Tests = {Ref(First), Ref(Second), Ref(Third)};
    FString Reason;
    TestTrue(TEXT("Legacy Pack remains valid"), ProbeDev10PackPolicy::Validate(*Pack, Reason));
    TestEqual(TEXT("Legacy policy continues by default"),
        static_cast<int32>(Pack->FailurePolicy), static_cast<int32>(EProbePackFailurePolicy::Continue));
    TestFalse(TEXT("Safe retries are opt-in"), Pack->bRetryReadOnlyFailuresOnce);
    Pack->Prerequisites.Add(Requires(Second, First));
    Pack->Prerequisites.Add(Requires(Third, Second));
    TestTrue(TEXT("Ordered dependency chain accepted"), ProbeDev10PackPolicy::Validate(*Pack, Reason));
    Pack->Prerequisites.Add(Requires(First, Third));
    TestFalse(TEXT("Backwards dependencies rejected"), ProbeDev10PackPolicy::Validate(*Pack, Reason));
    TestTrue(TEXT("Backwards diagnostic names ordering"), Reason.Contains(TEXT("before")));
    Pack->Prerequisites.Pop();
    Pack->Prerequisites.Add(Requires(Second, First));
    TestFalse(TEXT("Duplicate dependencies rejected"), ProbeDev10PackPolicy::Validate(*Pack, Reason));
    Pack->Prerequisites.Pop();
    UProbeTestAsset* External = MakeTest(TEXT("ProbeDev10External"));
    Pack->Prerequisites.Add(Requires(Third, External));
    TestFalse(TEXT("Outside-Pack reference rejected"), ProbeDev10PackPolicy::Validate(*Pack, Reason));
    Pack->Prerequisites.Pop();
    Pack->Tests.Add(Ref(Second));
    TestFalse(TEXT("Duplicate Pack member is ambiguous with prerequisites"),
        ProbeDev10PackPolicy::Validate(*Pack, Reason));
    Pack->Prerequisites.Reset();
    TestTrue(TEXT("Legacy Packs with repeated members remain usable without dependencies"),
        ProbeDev10PackPolicy::Validate(*Pack, Reason));
    Pack->Tests.Add(TSoftObjectPtr<UProbeTestAsset>());
    TestTrue(TEXT("Legacy Packs with an empty member still report missing references rather than refusing the entire run"),
        ProbeDev10PackPolicy::Validate(*Pack, Reason));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev10DependencyOutcomes,
    "Probe.Runner.Dev10DependencyOutcomes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev10DependencyOutcomes::RunTest(const FString& Parameters)
{
    UProbeTestAsset* First = MakeTest(TEXT("ProbeDev10DependencyFirst"));
    UProbeTestAsset* Second = MakeTest(TEXT("ProbeDev10DependencySecond"));
    UProbeTestPack* Pack = MakePack();
    Pack->Tests = {Ref(First), Ref(Second)};
    Pack->Prerequisites.Add(Requires(Second, First));
    FString Reason;
    TArray<Probe::FTestResult> Results;
    TestFalse(TEXT("Unrun prerequisite fails closed"),
        ProbeDev10PackPolicy::DependenciesPassed(*Pack, Second->GetPathName(), Results, Reason));
    TestTrue(TEXT("Missing result reported"), Reason.Contains(TEXT("no execution result")));
    Results.Add(Outcome(First, Probe::EResultStatus::Passed, 0));
    Results.Add(Outcome(First, Probe::EResultStatus::Passed, 1));
    TestTrue(TEXT("All variations must pass"),
        ProbeDev10PackPolicy::DependenciesPassed(*Pack, Second->GetPathName(), Results, Reason));
    Results[1].Status = Probe::EResultStatus::Failed;
    TestFalse(TEXT("One failing variation blocks dependency"),
        ProbeDev10PackPolicy::DependenciesPassed(*Pack, Second->GetPathName(), Results, Reason));
    TestTrue(TEXT("Blocker identifies failed state"), Reason.Contains(TEXT("Failed")));
    Results[1].Status = Probe::EResultStatus::Skipped;
    TestFalse(TEXT("Skipped prerequisite also blocks dependent"),
        ProbeDev10PackPolicy::DependenciesPassed(*Pack, Second->GetPathName(), Results, Reason));
    TestTrue(TEXT("Unrelated Pack test has no prerequisites"),
        ProbeDev10PackPolicy::DependenciesPassed(*Pack, First->GetPathName(), Results, Reason));
    TestFalse(TEXT("Default failure policy continues"),
        ProbeDev10PackPolicy::ShouldStop(*Pack, Probe::EResultStatus::Failed));
    Pack->FailurePolicy = EProbePackFailurePolicy::StopOnFailure;
    TestTrue(TEXT("Opt-in stop on failed"),
        ProbeDev10PackPolicy::ShouldStop(*Pack, Probe::EResultStatus::Failed));
    TestTrue(TEXT("Opt-in stop on infrastructure error"),
        ProbeDev10PackPolicy::ShouldStop(*Pack, Probe::EResultStatus::Error));
    TestFalse(TEXT("Skip does not stop Pack"),
        ProbeDev10PackPolicy::ShouldStop(*Pack, Probe::EResultStatus::Skipped));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev10SafeRetry,
    "Probe.Runner.Dev10SafeRetry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev10SafeRetry::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Test = MakeTest(TEXT("ProbeDev10Retry"));
    UProbeTestPack* Pack = MakePack();
    FProbeTestStep ReadOnly;
    ReadOnly.Type = EProbeStepType::PluginInstalled;
    ReadOnly.Target = TEXT("Probe");
    Test->Steps.Add(ReadOnly);
    TestTrue(TEXT("Read-only built-in assertion eligible"), ProbeDev10PackPolicy::CanRetryReadOnly(*Test));
    TestFalse(TEXT("Retries are disabled by default"),
        ProbeDev10PackPolicy::ShouldRetry(*Pack, *Test, Probe::EResultStatus::Failed, 1));
    Pack->bRetryReadOnlyFailuresOnce = true;
    TestTrue(TEXT("One retry allowed on an opt-in failing read-only assertion"),
        ProbeDev10PackPolicy::ShouldRetry(*Pack, *Test, Probe::EResultStatus::Failed, 1));
    TestFalse(TEXT("Never retry more than once"),
        ProbeDev10PackPolicy::ShouldRetry(*Pack, *Test, Probe::EResultStatus::Failed, 2));
    TestFalse(TEXT("Errors are not replayed"),
        ProbeDev10PackPolicy::ShouldRetry(*Pack, *Test, Probe::EResultStatus::Error, 1));
    Test->bAllowEditorUIInput = true;
    TestFalse(TEXT("UI authorization rules out automatic retries"), ProbeDev10PackPolicy::CanRetryReadOnly(*Test));
    Test->bAllowEditorUIInput = false;
    Test->Steps[0].Type = EProbeStepType::UIWidgetClick;
    TestFalse(TEXT("Live UI clicks never retry"), ProbeDev10PackPolicy::CanRetryReadOnly(*Test));
    Test->Steps[0].Type = EProbeStepType::UIWidgetClearText;
    TestFalse(TEXT("Text edits never retry"), ProbeDev10PackPolicy::CanRetryReadOnly(*Test));
    Test->Steps[0].Type = EProbeStepType::ActorFixtureSpawn;
    TestFalse(TEXT("Actor fixture mutations never retry"), ProbeDev10PackPolicy::CanRetryReadOnly(*Test));
    Test->Steps[0].Type = EProbeStepType::SelectionClear;
    TestFalse(TEXT("Selection mutations never retry"), ProbeDev10PackPolicy::CanRetryReadOnly(*Test));
    Test->Steps[0].Type = EProbeStepType::FixtureCreateObject;
    TestFalse(TEXT("Object fixture mutations never retry"), ProbeDev10PackPolicy::CanRetryReadOnly(*Test));
    Test->Steps[0].Type = EProbeStepType::PluginInstalled;
    Test->Steps[0].AssertionId = FName(TEXT("External.CustomAssertion"));
    TestFalse(TEXT("Unknown provider effects never retry"), ProbeDev10PackPolicy::CanRetryReadOnly(*Test));
    Test->Steps[0].AssertionId = NAME_None;
    Test->bAllowTransientEditorActors = true;
    TestFalse(TEXT("Actor authorization rules out retries"), ProbeDev10PackPolicy::CanRetryReadOnly(*Test));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev10HistoryAndClassification,
    "Probe.Editor.Dev10RunComparison",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev10HistoryAndClassification::RunTest(const FString& Parameters)
{
    UProbeTestAsset* A = MakeTest(TEXT("ProbeDev10HistoryA"));
    UProbeTestAsset* B = MakeTest(TEXT("ProbeDev10HistoryB"));
    UProbeTestAsset* C = MakeTest(TEXT("ProbeDev10HistoryC"));
    TArray<Probe::FTestResult> Previous = {Outcome(A, Probe::EResultStatus::Passed),
        Outcome(B, Probe::EResultStatus::Failed), Outcome(C, Probe::EResultStatus::Failed)};
    TArray<Probe::FTestResult> Current = {Outcome(A, Probe::EResultStatus::Failed),
        Outcome(B, Probe::EResultStatus::Passed), Outcome(C, Probe::EResultStatus::Error)};
    const ProbeDev10RunHistory::FComparison Diff = ProbeDev10RunHistory::Compare(Previous, Current);
    TestTrue(TEXT("Previous run is available"), Diff.bHasPreviousRun);
    TestEqual(TEXT("New regression is counted"), Diff.Regressions, 1);
    TestEqual(TEXT("Recovery is counted"), Diff.Recovered, 1);
    TestEqual(TEXT("Existing failure is counted"), Diff.StillFailing, 1);
    TestFalse(TEXT("No previous data produces no comparison"),
        ProbeDev10RunHistory::Compare({}, Current).bHasPreviousRun);
    A->SetupGroupName = TEXT("Setup");
    A->CleanupGroupName = TEXT("Cleanup");
    Probe::FStepResult Failed;
    Failed.Status = Probe::EResultStatus::Failed;
    Failed.GroupName = TEXT("Setup");
    Current[0].Steps.Add(Failed);
    TestEqual(TEXT("Setup failures classified"), ProbeDev10RunHistory::Category(Current[0], A), FString(TEXT("Setup")));
    Current[0].Steps[0].GroupName = TEXT("Cleanup");
    TestEqual(TEXT("Cleanup failures classified"), ProbeDev10RunHistory::Category(Current[0], A), FString(TEXT("Cleanup")));
    Current[0].Steps[0].GroupName = TEXT("Verify");
    TestEqual(TEXT("Assertion failures classified"), ProbeDev10RunHistory::Category(Current[0], A), FString(TEXT("Assertion")));
    TestEqual(TEXT("No-step errors are infrastructure"), ProbeDev10RunHistory::Category(Current[2], C), FString(TEXT("Infrastructure")));
    return true;
}
#endif // WITH_DEV_AUTOMATION_TESTS
