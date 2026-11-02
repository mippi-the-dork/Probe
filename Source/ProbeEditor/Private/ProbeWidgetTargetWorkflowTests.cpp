// Dev 5 acceptance automation: browser filtering, authoring, and preflight
// against disposable Slate fixtures. No level, Content Browser asset, or
// currently open Probe Test Asset is modified.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ProbeWidgetTargetAuthoring.h"
#include "ProbeWidgetDiscovery.h"
#include "Framework/MetaData/DriverIdMetaData.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeTargetBrowserFilterTest,
    "Probe.Editor.WidgetTargetBrowserFilters",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeTargetBrowserFilterTest::RunTest(const FString& Parameters)
{
    using namespace ProbeWidgetTargetAuthoring;
    ProbeWidgetDiscovery::FCandidate Unique;
    Unique.Id = TEXT("Probe.Acceptance.Unique");
    Unique.WindowTitle = TEXT("Probe Acceptance Window");
    Unique.WidgetType = TEXT("STextBlock");
    Unique.ReadableText = TEXT("TEST READINESS | Ready");
    ProbeWidgetDiscovery::FCandidate Duplicate = Unique;
    Duplicate.Id = TEXT("Probe.Acceptance.Duplicate");
    Duplicate.Matches = 2;

    TestTrue(TEXT("Unique-only includes unique widgets"), MatchesBrowserFilters(Unique, TEXT(""), true, false));
    TestFalse(TEXT("Unique-only excludes duplicate widgets"), MatchesBrowserFilters(Duplicate, TEXT(""), true, false));
    TestTrue(TEXT("Duplicate-only includes colliding widgets"), MatchesBrowserFilters(Duplicate, TEXT(""), false, true));
    TestFalse(TEXT("Duplicate-only excludes unique widgets"), MatchesBrowserFilters(Unique, TEXT(""), false, true));
    TestFalse(TEXT("Conflicting filters do not expose ambiguous widgets"), MatchesBrowserFilters(Duplicate, TEXT(""), true, true));
    TestTrue(TEXT("Search matches ID case-insensitively"), MatchesBrowserFilters(Unique, TEXT("acceptance.unique"), false, false));
    TestTrue(TEXT("Search matches window title"), MatchesBrowserFilters(Unique, TEXT("ACCEPTANCE WINDOW"), false, false));
    TestTrue(TEXT("Search matches widget type"), MatchesBrowserFilters(Unique, TEXT("textblock"), false, false));
    TestTrue(TEXT("Search matches readable text"), MatchesBrowserFilters(Unique, TEXT("readiness"), false, false));
    TestFalse(TEXT("Unrelated search is excluded"), MatchesBrowserFilters(Unique, TEXT("does-not-match"), false, false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeTargetAuthoringPreflightTest,
    "Probe.Editor.WidgetTargetAuthoringPreflight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeTargetAuthoringPreflightTest::RunTest(const FString& Parameters)
{
    using namespace ProbeWidgetTargetAuthoring;
    // Explicitly tagged fixtures ensure Collect, Resolve, quick authoring,
    // and preflight are exercised through the same paths as the workspace.
    const FString LabelId = TEXT("Probe.Acceptance.Text");
    const FString ButtonId = TEXT("Probe.Acceptance.Button");
    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(FText::FromString(TEXT("Probe Acceptance Fixture")))
        .ClientSize(FVector2D(420.f, 140.f))
        .SupportsMinimize(false)
        .SupportsMaximize(false)
        [ SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock)
                .AddMetaData(MakeShared<FDriverIdMetaData>(FName(*LabelId)))
                .Text(FText::FromString(TEXT("TEST READINESS | Ready"))) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SButton)
                .AddMetaData(MakeShared<FDriverIdMetaData>(FName(*ButtonId)))
                .Text(FText::FromString(TEXT("No Input"))) ] ];
    FSlateApplication::Get().AddWindow(Window);

    const TArray<ProbeWidgetDiscovery::FCandidate> Candidates = ProbeWidgetDiscovery::Collect();
    const auto Button = ProbeWidgetDiscovery::Resolve(Candidates, TEXT("id:") + ButtonId);
    const auto Label = ProbeWidgetDiscovery::Resolve(Candidates, TEXT("id:") + LabelId);
    TestTrue(TEXT("Fixture button is uniquely discoverable"), Button.CanUse());
    TestTrue(TEXT("Fixture label is uniquely discoverable"), Label.CanUse());

    if (Button.CanUse() && Label.CanUse())
    {
        UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage(),
            UProbeTestAsset::StaticClass(), NAME_None, RF_Transient);
        TestFalse(TEXT("Read-only fixture does not authorize UI input"), Asset->bAllowEditorUIInput);
        TestFalse(TEXT("Fixture does not authorize transient editor actors"), Asset->bAllowTransientEditorActors);
        FProbeTestStep Exists;
        FProbeTestStep Wait;
        FProbeTestStep Text;
        TestTrue(TEXT("Quick-step authoring: Exists"), ConfigureQuickStep(Exists, EProbeStepType::UIWidgetExists, Button.Candidate));
        TestTrue(TEXT("Quick-step authoring: Wait Visible"), ConfigureQuickStep(Wait, EProbeStepType::UIWaitForWidgetVisible, Button.Candidate));
        TestTrue(TEXT("Quick-step authoring: Text Equals snapshot"), ConfigureQuickStep(Text, EProbeStepType::UIWidgetTextEquals, Label.Candidate));
        TestEqual(TEXT("Wait duration retains five-second default"), Wait.UIWaitTimeoutSeconds, 5.0);
        TestEqual(TEXT("Captured text is exact"), Text.ExpectedValue, TEXT("TEST READINESS | Ready"));
        TestEqual(TEXT("Quick-step selector retains live ID"), Exists.Target, TEXT("id:") + ButtonId);
        Asset->Steps.Add(Exists);
        Asset->Steps.Add(Wait);
        Asset->Steps.Add(Text);

        FProbeTestStep Missing = Exists;
        Missing.Target = TEXT("id:Probe.Acceptance.Missing");
        Asset->Steps.Add(Missing);
        FProbeTestStep Deferred = Exists;
        Deferred.Target = TEXT("DETAILS:SEARCH");
        Asset->Steps.Add(Deferred);
        FProbeTestStep Unsupported = Exists;
        Unsupported.Target = TEXT("xpath://Widget");
        Asset->Steps.Add(Unsupported);
        FProbeTestStep NonUI;
        NonUI.Type = EProbeStepType::EditorWorldExists;
        Asset->Steps.Add(NonUI);

        const int32 BeforeSteps = Asset->Steps.Num();
        const bool bReadinessBefore = Asset->bNeedsConfiguration;
        const bool bEnabledBefore = Asset->bEnabled;
        const FPreflightReport Report = AnalyzeTargets(*Asset, Candidates);
        TestEqual(TEXT("Preflight checks only native UI selectors"), Report.Total(), 6);
        TestEqual(TEXT("Three live unique widgets"), Report.Unique, 3);
        TestEqual(TEXT("Missing targets are reported"), Report.Missing, 1);
        TestEqual(TEXT("Native Details is deferred, not marked missing"), Report.DeferredNativeDetails, 1);
        TestEqual(TEXT("Unsupported selectors are differentiated"), Report.Unsupported, 1);
        TestEqual(TEXT("No duplicates in fixture"), Report.Ambiguous, 0);
        TestEqual(TEXT("Preflight does not add/remove steps"), Asset->Steps.Num(), BeforeSteps);
        TestEqual(TEXT("Preflight does not change readiness"), Asset->bNeedsConfiguration, bReadinessBefore);
        TestEqual(TEXT("Preflight does not change enabled state"), Asset->bEnabled, bEnabledBefore);
        TestFalse(TEXT("Preflight does not authorize UI input"), Asset->bAllowEditorUIInput);

        // UPROPERTY-backed payload must survive the UObject duplication path.
        // Actual disk save/reopen and Ctrl+Z/Ctrl+Y still require a UI check.
        UProbeTestAsset* Copy = DuplicateObject<UProbeTestAsset>(Asset, GetTransientPackage());
        TestNotNull(TEXT("Unreal copies the transient authored Test Asset"), Copy);
        if (Copy)
        {
            TestEqual(TEXT("Copied Test Asset step count"), Copy->Steps.Num(), Asset->Steps.Num());
            TestEqual(TEXT("Copied text snapshot"), Copy->Steps[2].ExpectedValue, Text.ExpectedValue);
            TestEqual(TEXT("Copied wait timeout"), Copy->Steps[1].UIWaitTimeoutSeconds, 5.0);
            TestEqual(TEXT("Copied selector"), Copy->Steps[0].Target, Exists.Target);
            TestFalse(TEXT("Copied Test Asset remains read-only"), Copy->bAllowEditorUIInput);
        }
    }
    FSlateApplication::Get().RequestDestroyWindow(Window);
    return true;
}

#endif
