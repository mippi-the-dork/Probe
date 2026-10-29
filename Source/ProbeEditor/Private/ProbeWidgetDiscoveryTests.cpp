#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ProbeWidgetDiscovery.h"
#include "Framework/MetaData/DriverIdMetaData.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"

// All tests use disposable Slate windows. No UObjects, level edits, UI input,
// sleep, or external plugin dependencies. They execute on the game thread.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeWidgetDiscoveryUniqueTest,
    "Probe.Editor.WidgetDiscoveryUniqueIds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeWidgetDiscoveryUniqueTest::RunTest(const FString& Parameters)
{
    const FString Id = TEXT("Probe.Discovery.RegressionUnique.0154");
    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(FText::FromString(TEXT("Probe Widget Discovery Fixture")))
        .ClientSize(FVector2D(350.0f, 140.0f))
        .SupportsMinimize(false)
        .SupportsMaximize(false)
        [ SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SButton)
                .AddMetaData(MakeShared<FDriverIdMetaData>(FName(*Id)))
                .Text(FText::FromString(TEXT("Tagged"))) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock).Text(FText::FromString(TEXT("Untagged"))) ] ];
    FSlateApplication::Get().AddWindow(Window);
    const TArray<ProbeWidgetDiscovery::FCandidate> Candidates = ProbeWidgetDiscovery::Collect();
    int32 Found = 0;
    for (const auto& Candidate : Candidates)
    {
        if (Candidate.Id == Id)
        {
            ++Found;
            TestEqual(TEXT("Selector matches Probe runner's id: syntax"), Candidate.Selector(), TEXT("id:") + Id);
            TestTrue(TEXT("Explicit unique ID is selectable"), Candidate.IsUnique());
            TestEqual(TEXT("Widget type retained"), Candidate.WidgetType, TEXT("SButton"));
            TestEqual(TEXT("Owning window retained"), Candidate.WindowTitle, TEXT("Probe Widget Discovery Fixture"));
        }
    }
    TestEqual(TEXT("Exactly one fixture widget discovered"), Found, 1);
    FSlateApplication::Get().RequestDestroyWindow(Window);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeWidgetDiscoveryDuplicateTest,
    "Probe.Editor.WidgetDiscoveryRejectsDuplicates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeWidgetDiscoveryDuplicateTest::RunTest(const FString& Parameters)
{
    const FString Id = TEXT("Probe.Discovery.RegressionCollision.0154");
    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(FText::FromString(TEXT("Probe Duplicate ID Fixture")))
        .ClientSize(FVector2D(350.0f, 140.0f))
        .SupportsMinimize(false)
        .SupportsMaximize(false)
        [ SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SButton).AddMetaData(MakeShared<FDriverIdMetaData>(FName(*Id)))
                .Text(FText::FromString(TEXT("First"))) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SButton).AddMetaData(MakeShared<FDriverIdMetaData>(FName(*Id)))
                .Text(FText::FromString(TEXT("Second"))) ] ];
    FSlateApplication::Get().AddWindow(Window);
    const TArray<ProbeWidgetDiscovery::FCandidate> Candidates = ProbeWidgetDiscovery::Collect();
    int32 Found = 0;
    for (const auto& Candidate : Candidates)
    {
        if (Candidate.Id == Id)
        {
            ++Found;
            TestFalse(TEXT("Colliding target must not be selectable"), Candidate.IsUnique());
            TestEqual(TEXT("Both colliding widgets reported"), Candidate.Matches, 2);
        }
    }
    TestEqual(TEXT("Both duplicate IDs were discovered"), Found, 2);
    FSlateApplication::Get().RequestDestroyWindow(Window);
    return true;
}

#endif
