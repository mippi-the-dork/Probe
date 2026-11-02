#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ProbeStepGroups.h"
#include "ProbeQueueControl.h"
#include "ProbeTestRunner.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev7GroupClipboardTest,
    "Probe.Editor.Dev7GroupClipboard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev7GroupClipboardTest::RunTest(const FString& Parameters)
{
    TArray<FProbeTestStep> Steps;
    FProbeTestStep Setup;
    Setup.Type = EProbeStepType::PluginInstalled;
    Setup.GroupName = TEXT("Startup");
    Setup.Target = TEXT("Probe");
    Setup.Description = TEXT("Check installed plugin");
    Setup.bEnabled = false;
    Setup.UIWaitTimeoutSeconds = 9.5;
    Steps.Add(Setup);
    FProbeTestStep Second = Setup;
    Second.Type = EProbeStepType::PluginEnabled;
    Second.bEnabled = true;
    Second.Description = TEXT("Enabled");
    Steps.Add(Second);
    FProbeTestStep Third = Setup;
    Third.GroupName = TEXT("Cleanup");
    Third.AssertionId = FName(TEXT("Probe.Optional.Extension"));
    Third.ExpectedValue = TEXT("Custom expected text");
    Steps.Add(Third);
    const ProbeStepGroups::FRange First = ProbeStepGroups::FindContiguous(Steps, 1);
    TestEqual(TEXT("Contiguous group first"), First.First, 0);
    TestEqual(TEXT("Contiguous group last"), First.Last, 1);
    TestEqual(TEXT("Different group is isolated"), ProbeStepGroups::FindContiguous(Steps, 2).Num(), 1);
    TestFalse(TEXT("Invalid group index"), ProbeStepGroups::FindContiguous(Steps, 5).IsValid());
    FString Json, Error;
    TestTrue(TEXT("Group encodes"), ProbeStepGroups::Encode(Steps, First, Json, Error));
    TestTrue(TEXT("Version marker retained"), Json.Contains(TEXT("ProbeStepGroup/1")));
    TArray<FProbeTestStep> Restored;
    TestTrue(TEXT("Group decodes"), ProbeStepGroups::Decode(Json, Restored, Error));
    TestEqual(TEXT("Two steps restored"), Restored.Num(), 2);
    if (Restored.Num() == 2)
    {
        TestEqual(TEXT("Assertion type restored"), static_cast<uint8>(Restored[1].Type), static_cast<uint8>(EProbeStepType::PluginEnabled));
        TestEqual(TEXT("Group restored"), Restored[0].GroupName, FString(TEXT("Startup")));
        TestEqual(TEXT("Target restored"), Restored[0].Target, FString(TEXT("Probe")));
        TestEqual(TEXT("Disabled state restored"), Restored[0].bEnabled, false);
        TestEqual(TEXT("Wait timeout restored"), Restored[0].UIWaitTimeoutSeconds, 9.5);
        TestEqual(TEXT("Description restored"), Restored[0].Description, FString(TEXT("Check installed plugin")));
    }
    const ProbeStepGroups::FRange Single = ProbeStepGroups::FindContiguous(Steps, 2);
    TestTrue(TEXT("Single group encodes"), ProbeStepGroups::Encode(Steps, Single, Json, Error));
    TestTrue(TEXT("Extension step decodes"), ProbeStepGroups::Decode(Json, Restored, Error));
    if (Restored.Num() == 1)
    {
        TestEqual(TEXT("Extension ID preserved"), Restored[0].AssertionId, Third.AssertionId);
        TestEqual(TEXT("Expected value preserved"), Restored[0].ExpectedValue, Third.ExpectedValue);
    }
    TestFalse(TEXT("Non-Probe text rejected"), ProbeStepGroups::Decode(TEXT("not json"), Restored, Error));
    TestTrue(TEXT("Bad decode has no partial output"), Restored.IsEmpty());
    TestFalse(TEXT("Wrong format rejected"), ProbeStepGroups::Decode(TEXT("{\"format\":\"External\",\"steps\":[]}"), Restored, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev7ScopedRunTest,
    "Probe.Runner.Dev7ScopedExecution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev7ScopedRunTest::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage(), UProbeTestAsset::StaticClass(), NAME_None, RF_Transient);
    FProbeTestStep Missing;
    Missing.Type = EProbeStepType::PluginInstalled;
    Missing.Target = TEXT("ProbeMissingPluginNeverExists_DEV7");
    Missing.GroupName = TEXT("IntentionallyFail");
    FProbeTestStep Valid;
    Valid.Type = EProbeStepType::PluginInstalled;
    Valid.Target = TEXT("Probe");
    Valid.GroupName = TEXT("Check");
    FProbeTestStep Disabled = Missing;
    Disabled.GroupName = TEXT("Check");
    Disabled.bEnabled = false;
    Asset->Steps = {Missing, Valid, Disabled};
    // Complete, fully successful runs must keep their original human-readable
    // diagnostic. The Smoke oracle and external reporting rely on this contract.
    UProbeTestAsset* FullAsset = NewObject<UProbeTestAsset>(GetTransientPackage(),
        UProbeTestAsset::StaticClass(), NAME_None, RF_Transient);
    FullAsset->Steps = {Valid, Valid};
    const Probe::FTestResult FullResult = Probe::RunTest(FullAsset);
    TestEqual(TEXT("Full run remains Passed"), static_cast<uint8>(FullResult.Status),
        static_cast<uint8>(Probe::EResultStatus::Passed));
    TestEqual(TEXT("Full run retains legacy success detail"), FullResult.Detail,
        FString(TEXT("All 2 steps passed")));
    bool bAsyncCallbackCalled = false;
    Probe::RunTestAsync(FullAsset, [&bAsyncCallbackCalled, this](Probe::FTestResult AsyncResult)
    {
        bAsyncCallbackCalled = true;
        TestEqual(TEXT("Non-UI async dispatch preserves full-run detail"),
            AsyncResult.Detail, FString(TEXT("All 2 steps passed")));
    });
    TestTrue(TEXT("Non-UI async callback ran"), bAsyncCallbackCalled);

    FProbeTestStep DisabledValid = Valid;
    DisabledValid.bEnabled = false;
    FullAsset->Steps = {Valid, DisabledValid};
    const Probe::FTestResult FullWithDisabled = Probe::RunTest(FullAsset);
    TestEqual(TEXT("Full run with disabled steps still passes"),
        static_cast<uint8>(FullWithDisabled.Status),
        static_cast<uint8>(Probe::EResultStatus::Passed));
    TestEqual(TEXT("Full run with disabled steps reports their count"),
        FullWithDisabled.Detail, FString(TEXT("All 2 steps complete: 1 passed, 1 disabled")));
    FullAsset->Steps = {DisabledValid, DisabledValid};
    const Probe::FTestResult EntirelyDisabled = Probe::RunTest(FullAsset);
    TestEqual(TEXT("All-disabled whole run returns Skipped"),
        static_cast<uint8>(EntirelyDisabled.Status),
        static_cast<uint8>(Probe::EResultStatus::Skipped));
    TestEqual(TEXT("All-disabled summary does not claim assertions passed"),
        EntirelyDisabled.Detail, FString(TEXT("All 2 steps complete: 0 passed, 2 disabled")));

    Probe::FRunOptions Options;
    Options.FirstStepIndex = 1;
    Options.LastStepIndex = 2;
    const Probe::FTestResult Scoped = Probe::RunTest(Asset, Options);
    TestEqual(TEXT("Scope result passes"), static_cast<uint8>(Scoped.Status), static_cast<uint8>(Probe::EResultStatus::Passed));
    TestEqual(TEXT("Scope includes two steps"), Scoped.Steps.Num(), 2);
    TestEqual(TEXT("Scoped run retains range and disabled summary"), Scoped.Detail,
        FString(TEXT("Scoped steps 2-3: 1 passed, 1 disabled")));
    if (Scoped.Steps.Num() == 2)
    {
        TestEqual(TEXT("Original step number retained"), Scoped.Steps[0].Index, 2);
        TestEqual(TEXT("Group label retained"), Scoped.Steps[0].GroupName, FString(TEXT("Check")));
        TestEqual(TEXT("Disabled step does not execute"), static_cast<uint8>(Scoped.Steps[1].Status), static_cast<uint8>(Probe::EResultStatus::Skipped));
    }
    Options.FirstStepIndex = 2;
    Options.LastStepIndex = 2;
    const Probe::FTestResult AllSkipped = Probe::RunTest(Asset, Options);
    TestEqual(TEXT("All disabled steps produce Skipped"), static_cast<uint8>(AllSkipped.Status), static_cast<uint8>(Probe::EResultStatus::Skipped));
    Options.FirstStepIndex = 0;
    Options.LastStepIndex = 0;
    const Probe::FTestResult ExpectedFailure = Probe::RunTest(Asset, Options);
    TestEqual(TEXT("Failing group fails without running later groups"), static_cast<uint8>(ExpectedFailure.Status), static_cast<uint8>(Probe::EResultStatus::Failed));
    TestEqual(TEXT("Fail fast"), ExpectedFailure.Steps.Num(), 1);
    Options.FirstStepIndex = -1;
    const Probe::FTestResult Invalid = Probe::RunTest(Asset, Options);
    TestEqual(TEXT("Invalid range returns Error"), static_cast<uint8>(Invalid.Status), static_cast<uint8>(Probe::EResultStatus::Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev7StopQueueTest,
    "Probe.Editor.Dev7StopQueuePolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeDev7StopQueueTest::RunTest(const FString& Parameters)
{
    TArray<Probe::FTestResult> Results;
    Probe::FTestResult Completed;
    Completed.Status = Probe::EResultStatus::Passed;
    Completed.Detail = TEXT("Completed, keep unchanged");
    Results.Add(Completed);
    Probe::FTestResult Pending;
    Pending.Status = Probe::EResultStatus::Skipped;
    Pending.Detail = TEXT("Queued; not yet executed.");
    Results.Add(Pending);
    Results.Add(Pending);
    TArray<int32> Queued = {1, 2, 99};
    TestEqual(TEXT("Only pending valid result indices stopped"), ProbeQueueControl::SkipPending(Queued, Results), 2);
    TestTrue(TEXT("Queue cleared"), Queued.IsEmpty());
    TestEqual(TEXT("Completed test remains passed"), static_cast<uint8>(Results[0].Status), static_cast<uint8>(Probe::EResultStatus::Passed));
    TestEqual(TEXT("Completed diagnostics unchanged"), Results[0].Detail, FString(TEXT("Completed, keep unchanged")));
    TestEqual(TEXT("Unstarted result skipped"), static_cast<uint8>(Results[1].Status), static_cast<uint8>(Probe::EResultStatus::Skipped));
    TestTrue(TEXT("Stop reason recorded"), Results[2].Detail.Contains(TEXT("Stopped before execution")));
    return true;
}
#endif
