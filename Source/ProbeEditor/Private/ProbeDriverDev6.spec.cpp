#include "Misc/AutomationTest.h"
#include "ProbeTestAsset.h"
#include "ProbeTestRunner.h"
#include "ProbeNativeUIPolicy.h"
#include "ProbeWidgetTargetAuthoring.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MetaData/DriverIdMetaData.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace ProbeDev6
{
    struct FCompletion
    {
        TAtomic<bool> bDone{false};
        Probe::FTestResult Result;
    };

    static void Add(UProbeTestAsset& Asset, EProbeStepType Type, const TCHAR* Target,
        const TCHAR* Expected = TEXT(""), double Timeout = 2.0)
    {
        FProbeTestStep Step;
        Step.Type = Type;
        Step.Target = Target;
        Step.ExpectedValue = Expected;
        Step.UIWaitTimeoutSeconds = Timeout;
        Asset.Steps.Add(Step);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeNativeUIPolicyDev6Test,
    "Probe.Editor.NativeUIDev6Policy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeNativeUIPolicyDev6Test::RunTest(const FString&)
{
    using namespace ProbeNativeUIPolicy;
    TestTrue(TEXT("Append text requires permission"), IsInput(EProbeStepType::UIWidgetAppendText));
    TestTrue(TEXT("Clear text requires permission"), IsInput(EProbeStepType::UIWidgetClearText));
    TestFalse(TEXT("Wait is never input"), IsInput(EProbeStepType::UIWaitForWidgetInteractable));
    TestTrue(TEXT("Wait for gone is observation"), IsObservation(EProbeStepType::UIWaitForWidgetGone));
    TestTrue(TEXT("Text change is observation"), IsObservation(EProbeStepType::UIWaitForWidgetTextNotEquals));
    TestTrue(TEXT("Text change has timeout"), IsWait(EProbeStepType::UIWaitForWidgetTextNotEquals));
    TestFalse(TEXT("Snapshot does not have timeout"), IsWait(EProbeStepType::UIWidgetNotVisible));
    TestTrue(TEXT("Wait contains reads text"), ReadsText(EProbeStepType::UIWaitForWidgetTextContains));
    TestTrue(TEXT("Empty replacement text is supported"), AllowsEmptyValue(EProbeStepType::UIWidgetTypeText));
    TestTrue(TEXT("Text-change baseline can be empty"), AllowsEmptyValue(EProbeStepType::UIWaitForWidgetTextNotEquals));
    TestTrue(TEXT("Native input is targetable"), HasTarget(EProbeStepType::UIWidgetClearText));
    TestFalse(TEXT("Non-UI assertion not targetable"), HasTarget(EProbeStepType::PluginEnabled));
    UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage(), UProbeTestAsset::StaticClass(), NAME_None, RF_Transient);
    ProbeDev6::Add(*Asset, EProbeStepType::UIWaitForWidgetGone, TEXT("id:Probe.Dev6.Missing"));
    ProbeDev6::Add(*Asset, EProbeStepType::UIWidgetExists, TEXT("id:Probe.Dev6.Missing"));
    const auto Report = ProbeWidgetTargetAuthoring::AnalyzeTargets(*Asset, TArray<ProbeWidgetDiscovery::FCandidate>());
    TestEqual(TEXT("Valid missing target on negative condition"), Report.ExpectedAbsent, 1);
    TestEqual(TEXT("Unexpected missing target is still reported"), Report.Missing, 1);
    return true;
}

BEGIN_DEFINE_SPEC(FProbeDriverDev6Spec,
    "Probe.Driver.Dev6ConditionalUI",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    TSharedPtr<SWindow> Window;
    TSharedPtr<SEditableTextBox> TextBox;
    TStrongObjectPtr<UProbeTestAsset> Asset;
    TSharedPtr<ProbeDev6::FCompletion> Completion;
    bool bExpectedPass = true;
    int32 ExpectedSteps = 1;
    bool bTimedOut = false;
    bool bExpectTimeoutDetail = false;
    bool bExpectDelayed = false;
    bool bExpectEdit = false;
END_DEFINE_SPEC(FProbeDriverDev6Spec)

void FProbeDriverDev6Spec::Define()
{
    Describe("Native conditional UI authoring", [this]()
    {
        BeforeEach([this]()
        {
            bTimedOut = false;
            bExpectedPass = true;
            bExpectTimeoutDetail = false;
            bExpectDelayed = false;
            bExpectEdit = false;
            ExpectedSteps = 1;
            Completion = MakeShared<ProbeDev6::FCompletion>();
            SAssignNew(Window, SWindow)
                .Title(FText::FromString(TEXT("Probe Dev6 UI Fixture")))
                .ClientSize(FVector2D(440.f, 120.f))
                .SupportsMinimize(false)
                .SupportsMaximize(false)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(8)
                    [
                        SAssignNew(TextBox, SEditableTextBox)
                        .AddMetaData(MakeShared<FDriverIdMetaData>(FName(TEXT("Probe.Driver.Dev6.Editable"))))
                        .Text(FText::FromString(TEXT("ProbeDev6Original")))
                    ]
                ];
            FSlateApplication::Get().AddWindow(Window.ToSharedRef());
            Asset.Reset(NewObject<UProbeTestAsset>(GetTransientPackage(), UProbeTestAsset::StaticClass(), NAME_None, RF_Transient));
        });

        It("passes absence, presence, interaction and text wait conditions without input", EAsyncExecution::ThreadPool, [this]()
        {
            ExpectedSteps = 10;
            constexpr const TCHAR* Target = TEXT("id:Probe.Driver.Dev6.Editable");
            constexpr const TCHAR* Absent = TEXT("id:Probe.Driver.Dev6.NotPresent");
            ProbeDev6::Add(*Asset.Get(), EProbeStepType::UIWaitForWidgetExists, Target);
            ProbeDev6::Add(*Asset.Get(), EProbeStepType::UIWaitForWidgetVisible, Target);
            ProbeDev6::Add(*Asset.Get(), EProbeStepType::UIWaitForWidgetInteractable, Target);
            ProbeDev6::Add(*Asset.Get(), EProbeStepType::UIWaitForWidgetTextContains, Target, TEXT("Dev6"));
            ProbeDev6::Add(*Asset.Get(), EProbeStepType::UIWaitForWidgetTextNotEquals, Target, TEXT("Other"));
            ProbeDev6::Add(*Asset.Get(), EProbeStepType::UIWidgetNotVisible, Absent);
            ProbeDev6::Add(*Asset.Get(), EProbeStepType::UIWidgetNotInteractable, Absent);
            ProbeDev6::Add(*Asset.Get(), EProbeStepType::UIWaitForWidgetGone, Absent);
            ProbeDev6::Add(*Asset.Get(), EProbeStepType::UIWaitForWidgetNotVisible, Absent);
            ProbeDev6::Add(*Asset.Get(), EProbeStepType::UIWidgetTextContains, Target, TEXT("Original"));
            const TSharedRef<ProbeDev6::FCompletion> State = Completion.ToSharedRef();
            UProbeTestAsset* Test = Asset.Get();
            AsyncTask(ENamedThreads::GameThread, [State, Test]()
            {
                Probe::RunTestAsync(Test, [State](Probe::FTestResult Result)
                {
                    State->Result = MoveTemp(Result); State->bDone.Store(true);
                });
            });
            const double Deadline = FPlatformTime::Seconds() + 25.0;
            while (!State->bDone.Load() && FPlatformTime::Seconds() < Deadline)
            { FPlatformProcess::Sleep(0.05f); }
            bTimedOut = !State->bDone.Load();
        });

        It("reports a bounded wait timeout with the last observed state", EAsyncExecution::ThreadPool, [this]()
        {
            bExpectedPass = false;
            bExpectTimeoutDetail = true;
            ProbeDev6::Add(*Asset.Get(), EProbeStepType::UIWaitForWidgetTextContains,
                TEXT("id:Probe.Driver.Dev6.Editable"), TEXT("NeverAppearsInProbeDev6"), 0.3);
            const TSharedRef<ProbeDev6::FCompletion> State = Completion.ToSharedRef();
            UProbeTestAsset* Test = Asset.Get();
            AsyncTask(ENamedThreads::GameThread, [State, Test]()
            {
                Probe::RunTestAsync(Test, [State](Probe::FTestResult Result)
                { State->Result = MoveTemp(Result); State->bDone.Store(true); });
            });
            const double Deadline = FPlatformTime::Seconds() + 25.0;
            while (!State->bDone.Load() && FPlatformTime::Seconds() < Deadline)
            { FPlatformProcess::Sleep(0.05f); }
            bTimedOut = !State->bDone.Load();
        });

        It("observes a delayed live Slate text transition", EAsyncExecution::ThreadPool, [this]()
        {
            bExpectDelayed = true;
            ProbeDev6::Add(*Asset.Get(), EProbeStepType::UIWaitForWidgetTextContains,
                TEXT("id:Probe.Driver.Dev6.Editable"), TEXT("ReadyNow"), 3.0);
            const TSharedRef<ProbeDev6::FCompletion> State = Completion.ToSharedRef();
            UProbeTestAsset* Test = Asset.Get();
            TWeakPtr<SEditableTextBox> WeakText = TextBox;
            AsyncTask(ENamedThreads::GameThread, [State, Test]()
            {
                Probe::RunTestAsync(Test, [State](Probe::FTestResult Result)
                { State->Result = MoveTemp(Result); State->bDone.Store(true); });
            });
            FPlatformProcess::Sleep(0.45f);
            AsyncTask(ENamedThreads::GameThread, [WeakText]()
            {
                if (TSharedPtr<SEditableTextBox> Box = WeakText.Pin())
                { Box->SetText(FText::FromString(TEXT("ReadyNow"))); }
            });
            const double Deadline = FPlatformTime::Seconds() + 25.0;
            while (!State->bDone.Load() && FPlatformTime::Seconds() < Deadline)
            { FPlatformProcess::Sleep(0.05f); }
            bTimedOut = !State->bDone.Load();
        });

        It("clears and appends text through explicitly authorized native input", EAsyncExecution::ThreadPool, [this]()
        {
            bExpectEdit = true;
            ExpectedSteps = 3;
            Asset->bAllowEditorUIInput = true;
            const TCHAR* Target = TEXT("id:Probe.Driver.Dev6.Editable");
            ProbeDev6::Add(*Asset.Get(), EProbeStepType::UIWidgetClearText, Target);
            ProbeDev6::Add(*Asset.Get(), EProbeStepType::UIWidgetAppendText, Target, TEXT("ProbeDev6Typed"));
            ProbeDev6::Add(*Asset.Get(), EProbeStepType::UIWidgetTextEquals, Target, TEXT("ProbeDev6Typed"));
            const TSharedRef<ProbeDev6::FCompletion> State = Completion.ToSharedRef();
            UProbeTestAsset* Test = Asset.Get();
            AsyncTask(ENamedThreads::GameThread, [State, Test]()
            {
                Probe::RunTestAsync(Test, [State](Probe::FTestResult Result)
                { State->Result = MoveTemp(Result); State->bDone.Store(true); });
            });
            const double Deadline = FPlatformTime::Seconds() + 30.0;
            while (!State->bDone.Load() && FPlatformTime::Seconds() < Deadline)
            { FPlatformProcess::Sleep(0.05f); }
            bTimedOut = !State->bDone.Load();
        });

        AfterEach([this]()
        {
            TestFalse(TEXT("Dev6 test completed within its wall-clock gate"), bTimedOut);
            if (!bTimedOut && Completion.IsValid() && Completion->bDone.Load())
            {
                const Probe::FTestResult& Result = Completion->Result;
                if (bExpectEdit && Result.Status != Probe::EResultStatus::Passed)
                {
                    AddInfo(FString::Printf(TEXT("Dev6 text input failed: %s"), *Result.Detail));
                    for (const Probe::FStepResult& StepResult : Result.Steps)
                    {
                        AddInfo(FString::Printf(TEXT("  Step %d: status=%d, detail=%s"),
                            StepResult.Index, static_cast<int32>(StepResult.Status), *StepResult.Detail));
                    }
                }
                TestEqual(TEXT("Correct overall result"), static_cast<int32>(Result.Status),
                    static_cast<int32>(bExpectedPass ? Probe::EResultStatus::Passed : Probe::EResultStatus::Failed));
                TestEqual(TEXT("Expected step count"), Result.Steps.Num(), ExpectedSteps);
                if (bExpectTimeoutDetail)
                {
                    TestTrue(TEXT("Timeout diagnostic is explicit"), Result.Detail.Contains(TEXT("timed out")));
                    TestTrue(TEXT("Timeout includes observed text"), Result.Detail.Contains(TEXT("ProbeDev6Original")));
                }
                if (bExpectDelayed && !Result.Steps.IsEmpty())
                { TestTrue(TEXT("Wait used polling, not an immediate snapshot"), Result.Steps[0].DurationMs >= 250.0); }
                if (bExpectEdit && TextBox.IsValid())
                { TestEqual(TEXT("Actual editable Slate text"), TextBox->GetText().ToString(), FString(TEXT("ProbeDev6Typed"))); }
            }
            if (Window.IsValid()) { FSlateApplication::Get().RequestDestroyWindow(Window.ToSharedRef()); Window.Reset(); }
            TextBox.Reset(); Asset.Reset(); Completion.Reset();
        });
    });
}
