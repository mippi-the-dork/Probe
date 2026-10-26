// Probe 0.15.2: first real Slate input proof of concept.
// This is deliberately NOT a normal Probe test step yet. The synchronous
// Automation Driver must never be invoked on the Editor/game thread.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "IAutomationDriverModule.h"
#include "IAutomationDriver.h"
#include "AutomationDriverTypeDefs.h"
#include "IDriverElement.h"
#include "LocateBy.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MetaData/DriverIdMetaData.h"
#include "Widgets/SWindow.h"
#include "Widgets/Input/SButton.h"

BEGIN_DEFINE_SPEC(FProbeDriverSlateButtonSpec,
    "Probe.Driver.SlateButton",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    TSharedPtr<SWindow> ProbeWindow;
    bool bDriverWasEnabled = false;
    bool bClickReturnedSuccess = false;
    bool bButtonOnClickedFired = false;
END_DEFINE_SPEC(FProbeDriverSlateButtonSpec)

void FProbeDriverSlateButtonSpec::Define()
{
    Describe("Native Slate input", [this]()
    {
        BeforeEach([this]()
        {
            bClickReturnedSuccess = false;
            bButtonOnClickedFired = false;
            bDriverWasEnabled = IAutomationDriverModule::Get().IsEnabled();
            if (!bDriverWasEnabled)
            {
                IAutomationDriverModule::Get().Enable();
            }

            // Disposable, self-contained widget. No Editor assets or level actors.
            SAssignNew(ProbeWindow, SWindow)
                .Title(FText::FromString(TEXT("Probe Automation Driver Test")))
                .ClientSize(FVector2D(340.0f, 120.0f))
                .SupportsMaximize(false)
                .SupportsMinimize(false)
                [
                    SNew(SButton)
                    .AddMetaData(MakeShared<FDriverIdMetaData>(FName(TEXT("Probe.Driver.TestButton.0152"))))
                    .HAlign(HAlign_Center)
                    .VAlign(VAlign_Center)
                    .Text(FText::FromString(TEXT("Probe Test Click Target")))
                    .OnClicked_Lambda([this]()
                    {
                        bButtonOnClickedFired = true;
                        return FReply::Handled();
                    })
                ];
            FSlateApplication::Get().AddWindow(ProbeWindow.ToSharedRef());
        });

        // Automation Driver's synchronous Click blocks its caller while Slate
        // processes the injected input on the game thread. ThreadPool is REQUIRED.
        It("clicks its tagged button and fires the Slate callback", EAsyncExecution::ThreadPool, [this]()
        {
            const FAutomationDriverRef Driver = IAutomationDriverModule::Get().CreateDriver();
            bClickReturnedSuccess = Driver->FindElement(By::Id(TEXT("Probe.Driver.TestButton.0152")))->Click();
        });

        AfterEach([this]()
        {
            // Both signals are required: the driver must report success and
            // the button callback must have run. A missing click fails the test.
            TestTrue(TEXT("Automation Driver reports successful click"), bClickReturnedSuccess);
            TestTrue(TEXT("Slate button OnClicked callback executed"), bButtonOnClickedFired);

            if (ProbeWindow.IsValid())
            {
                FSlateApplication::Get().RequestDestroyWindow(ProbeWindow.ToSharedRef());
                ProbeWindow.Reset();
            }
            // Never disable a driver that was already enabled before this test.
            if (!bDriverWasEnabled)
            {
                IAutomationDriverModule::Get().Disable();
            }
        });
    });
}

#endif // WITH_DEV_AUTOMATION_TESTS
