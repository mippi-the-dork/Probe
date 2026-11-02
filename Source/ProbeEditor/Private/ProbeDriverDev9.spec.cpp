#include "Misc/AutomationTest.h"
#include "ProbeTestAsset.h"
#include "ProbeTestRunner.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace ProbeDev9Driver
{
    struct FCompletion
    {
        TAtomic<bool> bDone{false};
        Probe::FTestResult Result;
    };
}

BEGIN_DEFINE_SPEC(FProbeDriverDev9LifecycleSpec,
    "Probe.Driver.Dev9Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    TStrongObjectPtr<UProbeTestAsset> Asset;
    TSharedPtr<ProbeDev9Driver::FCompletion> Completion;
    bool bTimedOut = false;
END_DEFINE_SPEC(FProbeDriverDev9LifecycleSpec)

void FProbeDriverDev9LifecycleSpec::Define()
{
    Describe("Asynchronous workflow cleanup", [this]()
    {
        BeforeEach([this]()
        {
            bTimedOut = false;
            Completion = MakeShared<ProbeDev9Driver::FCompletion>();
            Asset.Reset(NewObject<UProbeTestAsset>(GetTransientPackage(), UProbeTestAsset::StaticClass(), NAME_None, RF_Transient));
            Asset->SetupGroupName = TEXT("Setup");
            Asset->CleanupGroupName = TEXT("Cleanup");
        });

        It("runs the cleanup group after a native UI wait fails", EAsyncExecution::ThreadPool, [this]()
        {
            FProbeTestStep Setup;
            Setup.Type = EProbeStepType::PluginInstalled;
            Setup.Target = TEXT("Probe");
            Setup.GroupName = TEXT("Setup");
            FProbeTestStep Wait;
            Wait.Type = EProbeStepType::UIWaitForWidgetExists;
            Wait.Target = TEXT("id:Probe.Driver.Dev9.DefinitelyNotPresent");
            Wait.GroupName = TEXT("Verify");
            Wait.UIWaitTimeoutSeconds = 0.3;
            FProbeTestStep Cleanup = Setup;
            Cleanup.GroupName = TEXT("Cleanup");
            Asset->Steps = {Setup, Wait, Cleanup};
            const TSharedRef<ProbeDev9Driver::FCompletion> State = Completion.ToSharedRef();
            UProbeTestAsset* Test = Asset.Get();
            AsyncTask(ENamedThreads::GameThread, [State, Test]()
            {
                Probe::RunTestAsync(Test, [State](Probe::FTestResult Result)
                {
                    State->Result = MoveTemp(Result);
                    State->bDone.Store(true);
                });
            });
            const double Deadline = FPlatformTime::Seconds() + 20.0;
            while (!State->bDone.Load() && FPlatformTime::Seconds() < Deadline)
            { FPlatformProcess::Sleep(0.05f); }
            bTimedOut = !State->bDone.Load();
        });

        AfterEach([this]()
        {
            TestFalse(TEXT("Dev9 workflow completed"), bTimedOut);
            if (!bTimedOut && Completion.IsValid() && Completion->bDone.Load())
            {
                const Probe::FTestResult& Result = Completion->Result;
                TestEqual(TEXT("Native UI wait failure retained"), static_cast<int32>(Result.Status),
                    static_cast<int32>(Probe::EResultStatus::Failed));
                TestEqual(TEXT("Setup, failed wait, cleanup all reported"), Result.Steps.Num(), 3);
                if (Result.Steps.Num() == 3)
                {
                    TestEqual(TEXT("Setup passed"), static_cast<int32>(Result.Steps[0].Status),
                        static_cast<int32>(Probe::EResultStatus::Passed));
                    TestEqual(TEXT("Wait failed"), static_cast<int32>(Result.Steps[1].Status),
                        static_cast<int32>(Probe::EResultStatus::Failed));
                    TestEqual(TEXT("Cleanup executed despite wait failure"), static_cast<int32>(Result.Steps[2].Status),
                        static_cast<int32>(Probe::EResultStatus::Passed));
                    TestTrue(TEXT("Primary failure is preserved"), Result.Detail.Contains(TEXT("Step 2")));
                }
            }
            Asset.Reset(); Completion.Reset();
        });
    });
}
