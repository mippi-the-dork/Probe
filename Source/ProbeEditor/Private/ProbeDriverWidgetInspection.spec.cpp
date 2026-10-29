// Dev 3: independently exercise authorable, read-only ID-targeted Slate assertions.
// The test owns its temporary window and in-memory asset. No level or asset edits.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ProbeTestRunner.h"
#include "ProbeTestAsset.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MetaData/DriverIdMetaData.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

namespace ProbeWidgetInspection
{
    struct FCompletion
    {
        TAtomic<bool> bCompleted{false};
        Probe::FTestResult Result;
    };
}

BEGIN_DEFINE_SPEC(FProbeDriverWidgetInspectionSpec,
    "Probe.Driver.AuthoredWidgetInspection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    TSharedPtr<SWindow> Window;
    TStrongObjectPtr<UProbeTestAsset> Positive;
    TStrongObjectPtr<UProbeTestAsset> Negative;
    TSharedPtr<ProbeWidgetInspection::FCompletion> Completion;
    bool bExpectPass = true;
    bool bTimedOut = false;
END_DEFINE_SPEC(FProbeDriverWidgetInspectionSpec)

void FProbeDriverWidgetInspectionSpec::Define()
{
    Describe("Authorable native widget observation", [this]()
    {
        BeforeEach([this]()
        {
            bTimedOut = false;
            bExpectPass = true;
            Completion = MakeShared<ProbeWidgetInspection::FCompletion>();
            SAssignNew(Window, SWindow)
                .Title(FText::FromString(TEXT("Probe Read-Only Widget Inspection")))
                .ClientSize(FVector2D(440.0f, 160.0f))
                .SupportsMinimize(false)
                .SupportsMaximize(false)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(10)
                    [
                        SNew(SButton)
                        .AddMetaData(MakeShared<FDriverIdMetaData>(FName(TEXT("Probe.Driver.InspectionButton.0154"))))
                        .Text(FText::FromString(TEXT("Safe Inspection Button")))
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(10)
                    [
                        SNew(SEditableTextBox)
                        .AddMetaData(MakeShared<FDriverIdMetaData>(FName(TEXT("Probe.Driver.InspectionText.0154"))))
                        .Text(FText::FromString(TEXT("ProbeWidgetInspection42")))
                    ]
                ];
            FSlateApplication::Get().AddWindow(Window.ToSharedRef());

            Positive.Reset(NewObject<UProbeTestAsset>(GetTransientPackage(), UProbeTestAsset::StaticClass(), NAME_None, RF_Transient));
            Negative.Reset(NewObject<UProbeTestAsset>(GetTransientPackage(), UProbeTestAsset::StaticClass(), NAME_None, RF_Transient));
            // Read-only steps must work with bAllowEditorUIInput=false.
            const auto Add = [](UProbeTestAsset& Asset, EProbeStepType Type, const TCHAR* Target,
                const TCHAR* Expected = TEXT(""))
            {
                FProbeTestStep Step;
                Step.Type = Type;
                Step.Target = Target;
                Step.ExpectedValue = Expected;
                Step.UIWaitTimeoutSeconds = 2.0;
                Asset.Steps.Add(Step);
            };
            const TCHAR* Button = TEXT("id:Probe.Driver.InspectionButton.0154");
            const TCHAR* Text = TEXT("id:Probe.Driver.InspectionText.0154");
            // The explicit wait allows the freshly opened Slate window to
            // arrange before any subsequent snapshot assertions run.
            Add(*Positive.Get(), EProbeStepType::UIWaitForWidgetVisible, Button);
            Add(*Positive.Get(), EProbeStepType::UIWidgetExists, Button);
            Add(*Positive.Get(), EProbeStepType::UIWidgetInteractable, Button);
            Add(*Positive.Get(), EProbeStepType::UIWidgetTextEquals, Text, TEXT("ProbeWidgetInspection42"));
            Add(*Positive.Get(), EProbeStepType::UIWidgetTextContains, Text, TEXT("Inspection"));
            Add(*Positive.Get(), EProbeStepType::UIWidgetTextNotEquals, Text, TEXT("OtherValue"));
            Add(*Positive.Get(), EProbeStepType::UIWidgetVisible, Button);
            Add(*Positive.Get(), EProbeStepType::UIWaitForWidgetTextEquals, Text, TEXT("ProbeWidgetInspection42"));
            Add(*Positive.Get(), EProbeStepType::UIWidgetNotExists, TEXT("id:Probe.Driver.NeverPresent.0154"));

            Add(*Negative.Get(), EProbeStepType::UIWidgetTextEquals, Text, TEXT("DeliberatelyWrong"));
            Add(*Negative.Get(), EProbeStepType::UIWidgetExists, Button); // Must not execute after failure.
        });

        It("reads tagged widget state without sending input", EAsyncExecution::ThreadPool, [this]()
        {
            bExpectPass = true;
            const TSharedRef<ProbeWidgetInspection::FCompletion> State = Completion.ToSharedRef();
            UProbeTestAsset* Asset = Positive.Get();
            AsyncTask(ENamedThreads::GameThread, [State, Asset]()
            {
                Probe::RunTestAsync(Asset, [State](Probe::FTestResult Result)
                {
                    State->Result = MoveTemp(Result);
                    State->bCompleted.Store(true);
                });
            });
            const double Deadline = FPlatformTime::Seconds() + 20.0;
            while (!State->bCompleted.Load() && FPlatformTime::Seconds() < Deadline)
            { FPlatformProcess::Sleep(0.05f); }
            bTimedOut = !State->bCompleted.Load();
        });

        It("reports a mismatched widget text as failure", EAsyncExecution::ThreadPool, [this]()
        {
            bExpectPass = false;
            const TSharedRef<ProbeWidgetInspection::FCompletion> State = Completion.ToSharedRef();
            UProbeTestAsset* Asset = Negative.Get();
            AsyncTask(ENamedThreads::GameThread, [State, Asset]()
            {
                Probe::RunTestAsync(Asset, [State](Probe::FTestResult Result)
                {
                    State->Result = MoveTemp(Result);
                    State->bCompleted.Store(true);
                });
            });
            const double Deadline = FPlatformTime::Seconds() + 20.0;
            while (!State->bCompleted.Load() && FPlatformTime::Seconds() < Deadline)
            { FPlatformProcess::Sleep(0.05f); }
            bTimedOut = !State->bCompleted.Load();
        });

        AfterEach([this]()
        {
            TestFalse(TEXT("Authorable widget inspection completed within the deadline"), bTimedOut);
            if (!bTimedOut && Completion.IsValid() && Completion->bCompleted.Load())
            {
                const Probe::FTestResult& Result = Completion->Result;
                TestEqual(TEXT("Authorable widget result status"), static_cast<int32>(Result.Status),
                    static_cast<int32>(bExpectPass ? Probe::EResultStatus::Passed : Probe::EResultStatus::Failed));
                TestEqual(TEXT("Correct number of steps executed"), Result.Steps.Num(), bExpectPass ? 9 : 1);
                for (const Probe::FStepResult& Step : Result.Steps)
                { TestEqual(TEXT("Correct individual step status"), static_cast<int32>(Step.Status),
                    static_cast<int32>(bExpectPass ? Probe::EResultStatus::Passed : Probe::EResultStatus::Failed)); }
                if (!bExpectPass)
                { TestTrue(TEXT("Mismatch includes observed text"), Result.Detail.Contains(TEXT("ProbeWidgetInspection42"))); }
            }
            if (Window.IsValid())
            {
                FSlateApplication::Get().RequestDestroyWindow(Window.ToSharedRef());
                Window.Reset();
            }
            Positive.Reset();
            Negative.Reset();
        });
    });
}

#endif // WITH_DEV_AUTOMATION_TESTS
