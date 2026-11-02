#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ProbeWidgetTargetAuthoring.h"
#include "Framework/MetaData/DriverIdMetaData.h"
#include "Widgets/SWindow.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"

// Pure resolution cases including stale, empty, special, and ambiguous selectors.
// The Automation Driver's IDs are FNames and therefore case-insensitive.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeTargetResolutionTest,
    "Probe.Editor.WidgetTargetResolution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeTargetResolutionTest::RunTest(const FString& Parameters)
{
    using namespace ProbeWidgetDiscovery;
    TArray<FCandidate> Items;
    FCandidate A;
    A.Id = TEXT("Probe.Dev5.CaseSensitive");
    A.WidgetType = TEXT("SButton");
    A.WindowTitle = TEXT("Fixture Window");
    A.bVisible = true;
    A.bEnabled = true;
    Items.Add(A);

    const FResolution Unique = Resolve(Items, TEXT("id:Probe.Dev5.CaseSensitive"));
    TestTrue(TEXT("Unique ID is resolved"), Unique.CanUse());
    TestEqual(TEXT("One match"), Unique.Matches, 1);
    TestEqual(TEXT("Widget type is retained"), Unique.Candidate.WidgetType, TEXT("SButton"));
    TestTrue(TEXT("Whitespace outside selector is harmless"),
        Resolve(Items, TEXT(" id:Probe.Dev5.CaseSensitive ")).CanUse());
    const FResolution Alias = Resolve(Items, TEXT("id:probe.dev5.casesensitive"));
    TestTrue(TEXT("Case variants resolve the same FName-backed Automation Driver ID"), Alias.CanUse());
    TestEqual(TEXT("Case variant resolves exactly one widget"), Alias.Matches, 1);
    TestEqual(TEXT("Resolved target preserves canonical display spelling"),
        Alias.Candidate.Id, TEXT("Probe.Dev5.CaseSensitive"));
    TestTrue(TEXT("Uppercase id: selector prefix is accepted like the runtime"),
        Resolve(Items, TEXT("ID:Probe.Dev5.CaseSensitive")).CanUse());
    TestEqual(TEXT("Unknown ID resolves Missing"),
        static_cast<uint8>(Resolve(Items, TEXT("id:Probe.Dev5.Gone")).State),
        static_cast<uint8>(EResolution::Missing));
    TestEqual(TEXT("Empty selector resolves Empty"),
        static_cast<uint8>(Resolve(Items, TEXT(" ")).State),
        static_cast<uint8>(EResolution::Empty));
    TestEqual(TEXT("Malformed selectors are rejected"),
        static_cast<uint8>(Resolve(Items, TEXT("xpath://Button")).State),
        static_cast<uint8>(EResolution::Unsupported));
    TestEqual(TEXT("Native Details selector is explicitly recognized"),
        static_cast<uint8>(Resolve(Items, TEXT("details:search")).State),
        static_cast<uint8>(EResolution::NativeDetails));
    TestEqual(TEXT("Native Details selector casing matches the runtime"),
        static_cast<uint8>(Resolve(Items, TEXT("DETAILS:SEARCH")).State),
        static_cast<uint8>(EResolution::NativeDetails));
    FCandidate CaseCollision = A;
    CaseCollision.Id = TEXT("PROBE.DEV5.CASESENSITIVE");
    Items.Add(CaseCollision);
    const FResolution Duplicate = Resolve(Items, TEXT("id:Probe.Dev5.CaseSensitive"));
    TestEqual(TEXT("IDs differing only in casing are ambiguous"),
        static_cast<uint8>(Duplicate.State), static_cast<uint8>(EResolution::Ambiguous));
    TestEqual(TEXT("Both case-variant IDs are counted"), Duplicate.Matches, 2);
    TestFalse(TEXT("Case-variant collisions are not selectable"), Duplicate.CanUse());
    const FResolution DuplicateAlias = Resolve(Items, TEXT("ID:probe.dev5.casesensitive"));
    TestEqual(TEXT("Case-variant query is also ambiguous"),
        static_cast<uint8>(DuplicateAlias.State), static_cast<uint8>(EResolution::Ambiguous));
    TestEqual(TEXT("Case-variant query reports all matches"), DuplicateAlias.Matches, 2);
    return true;
}

// Snapshot text is deliberately only read from directly tagged, known widget
// types. No descendant text crawling, no UI input and no user data mutation.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeTargetReadableSnapshotTest,
    "Probe.Editor.WidgetTargetReadableSnapshot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeTargetReadableSnapshotTest::RunTest(const FString& Parameters)
{
    const FString LabelId = TEXT("Probe.Dev5.ReadOnlyLabel");
    const FString EditId = TEXT("Probe.Dev5.ReadOnlyEdit");
    const FString ButtonId = TEXT("Probe.Dev5.ButtonWithChildLabel");
    TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(FText::FromString(TEXT("Probe Dev 5 Widget Snapshot")))
        .ClientSize(FVector2D(450.0f, 200.0f))
        .SupportsMinimize(false)
        .SupportsMaximize(false)
        [ SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(STextBlock)
                .AddMetaData(MakeShared<FDriverIdMetaData>(FName(*LabelId)))
                .Text(FText::FromString(TEXT("Read-only status"))) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SEditableTextBox)
                .AddMetaData(MakeShared<FDriverIdMetaData>(FName(*EditId)))
                .Text(FText::FromString(TEXT("Current value"))) ]
            + SVerticalBox::Slot().AutoHeight()
            [ SNew(SButton)
                .AddMetaData(MakeShared<FDriverIdMetaData>(FName(*ButtonId)))
                .Text(FText::FromString(TEXT("Do Not Click"))) ] ];
    FSlateApplication::Get().AddWindow(Window);
    const TArray<ProbeWidgetDiscovery::FCandidate> Items = ProbeWidgetDiscovery::Collect();
    const auto Label = ProbeWidgetDiscovery::Resolve(Items, TEXT("id:") + LabelId);
    TestTrue(TEXT("Tagged label resolves"), Label.CanUse());
    if (Label.CanUse())
    {
        TestTrue(TEXT("Tagged label has readable text"), Label.Candidate.bHasReadableText);
        TestEqual(TEXT("Exact tagged label text"), Label.Candidate.ReadableText, TEXT("Read-only status"));
    }
    const auto Edit = ProbeWidgetDiscovery::Resolve(Items, TEXT("id:") + EditId);
    TestTrue(TEXT("Tagged editable text resolves"), Edit.CanUse());
    if (Edit.CanUse())
    {
        TestTrue(TEXT("Editable text box has readable text"), Edit.Candidate.bHasReadableText);
        TestEqual(TEXT("Exact editable text"), Edit.Candidate.ReadableText, TEXT("Current value"));
    }
    const auto Button = ProbeWidgetDiscovery::Resolve(Items, TEXT("id:") + ButtonId);
    TestTrue(TEXT("Tagged button resolves"), Button.CanUse());
    if (Button.CanUse())
    {
        TestFalse(TEXT("A child button label is not misreported as directly tagged text"),
            Button.Candidate.bHasReadableText);
    }
    FSlateApplication::Get().RequestDestroyWindow(Window);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeTargetQuickStepPolicyTest,
    "Probe.Editor.WidgetTargetQuickStepPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeTargetQuickStepPolicyTest::RunTest(const FString& Parameters)
{
    using namespace ProbeWidgetTargetAuthoring;
    ProbeWidgetDiscovery::FCandidate Candidate;
    Candidate.Id = TEXT("Probe.Dev5.Policy");
    Candidate.ReadableText = TEXT("Status Ready");
    Candidate.bHasReadableText = true;
    FProbeTestStep Exists;
    TestTrue(TEXT("Create exists assertion from tagged widget"),
        ConfigureQuickStep(Exists, EProbeStepType::UIWidgetExists, Candidate));
    TestEqual(TEXT("Selector copied to step"), Exists.Target, TEXT("id:Probe.Dev5.Policy"));
    TestEqual(TEXT("Step type set"), static_cast<uint8>(Exists.Type), static_cast<uint8>(EProbeStepType::UIWidgetExists));
    FProbeTestStep Text;
    TestTrue(TEXT("Create text assertion"), ConfigureQuickStep(Text, EProbeStepType::UIWidgetTextEquals, Candidate));
    TestEqual(TEXT("Text snapshot becomes Expected Value"), Text.ExpectedValue, TEXT("Status Ready"));
    Candidate.bHasReadableText = false;
    TestFalse(TEXT("Unsupported text capture is blocked"),
        ConfigureQuickStep(Text, EProbeStepType::UIWaitForWidgetTextEquals, Candidate));
    TestFalse(TEXT("Input-producing quick steps cannot be created"),
        ConfigureQuickStep(Text, EProbeStepType::UIWidgetClick, Candidate));
    Candidate.Matches = 2;
    TestFalse(TEXT("Ambiguous widget cannot produce an assertion"),
        ConfigureQuickStep(Text, EProbeStepType::UIWidgetExists, Candidate));
    return true;
}

#endif
