#include "ProbeTestAsset.h"
#include "ProbeTestRunner.h"
#include "ProbeAssertionCatalog.h"
#include "ProbeTestAssetFactory.h"
#include "ProbeEditorNavigation.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ToolMenus.h"
#include "ToolMenu.h"
#include "ContentBrowserMenuContexts.h"
#include "ContentBrowserModule.h"
#include "Misc/ConfigCacheIni.h"
#include "LevelEditor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "FileHelpers.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Images/SImage.h"
#include "Styling/AppStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/PackageName.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "ScopedTransaction.h"
#include "Misc/TransactionObjectEvent.h"
#include "UObject/UObjectGlobals.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"

#define LOCTEXT_NAMESPACE "ProbeEditor"

namespace Probe
{
    // Keep the original tab ID so an existing saved Editor layout can reopen it.
    static const FName TabId(TEXT("ProbeWorkspace"));

    enum class ETestTemplate : uint8
    {
        Blank,
        Smoke,
        Plugin,
        World,
        Asset,
        ActorClass,
        ActorTag,
        AssetClass,
        Property,
        ActorLabel,
        ActorSelection,
        Module,
        EditorTab,
        ToolMenu,
        ConsoleVariable,
        AssetFolder,
        AssetDirty,
        ClassContract,
        ObjectPropertyAbsent,
        ActorComponent,
        ActorFolder,
        SelectionExact
    };

    static FProbeTestStep MakeStep(EProbeStepType Type, const FString& OptionalAssetPath = FString())
    {
        FProbeTestStep Step;
        Step.Type = Type;
        switch (Type)
        {
        case EProbeStepType::PluginEnabled:
            Step.Target = TEXT("Probe");
            Step.Description = TEXT("Verify the plugin is enabled");
            break;
        case EProbeStepType::ClassExists:
            Step.Target = TEXT("/Script/Engine.Actor");
            Step.Description = TEXT("Verify the class can be found");
            break;
        case EProbeStepType::AssetExists:
            Step.Target = OptionalAssetPath;
            Step.Description = TEXT("Verify the asset exists");
            break;
        case EProbeStepType::EditorWorldExists:
            Step.Description = TEXT("Verify an Editor world is loaded");
            break;
        case EProbeStepType::ActorCountAtLeast:
            Step.MinimumActorCount = 1;
            Step.Description = TEXT("Verify the level has at least one actor");
            break;
        case EProbeStepType::ActorClassCountAtLeast:
            Step.Target = TEXT("/Script/Engine.Actor");
            Step.MinimumActorCount = 1;
            Step.Description = TEXT("Verify at least one actor of the specified class");
            break;
        case EProbeStepType::ActorTagExists:
            Step.Description = TEXT("Verify an actor has the specified tag (set Target)");
            break;
        case EProbeStepType::AssetClassIs:
            Step.Target = OptionalAssetPath;
            Step.ExpectedClass = TEXT("/Script/ProbeEditor.ProbeTestAsset");
            Step.Description = TEXT("Verify the asset has the expected class");
            break;
        case EProbeStepType::ActorLabelExists:
            Step.Description = TEXT("Verify an actor exists with this exact World Outliner label");
            break;
        case EProbeStepType::SelectedActorCountAtLeast:
            Step.MinimumActorCount = 1;
            Step.Description = TEXT("Verify at least one actor is currently selected in the Editor");
            break;
        case EProbeStepType::ObjectPropertyEquals:
            Step.Target = OptionalAssetPath;
            Step.PropertyName = TEXT("bEnabled");
            Step.ExpectedValue = TEXT("True");
            Step.Description = TEXT("Verify a reflected property matches the expected value");
            break;
        case EProbeStepType::PluginInstalled:
        case EProbeStepType::PluginDisabled:
            Step.Target = TEXT("Probe");
            break;
        case EProbeStepType::ModuleLoaded:
            Step.Target = TEXT("ProbeEditor");
            break;
        case EProbeStepType::ConsoleVariableExists:
        case EProbeStepType::ConsoleVariableEquals:
            Step.Target = TEXT("r.VSync");
            Step.ExpectedValue = TEXT("0");
            break;
        case EProbeStepType::EditorTabOpen:
            Step.Target = TEXT("ProbeWorkspace");
            break;
        case EProbeStepType::ToolMenuRegistered:
            Step.Target = TEXT("LevelEditor.MainMenu.Tools");
            break;
        case EProbeStepType::AssetCountAtLeast:
            Step.Target = TEXT("/Game/ProbeTests");
            Step.MinimumActorCount = 1;
            break;
        case EProbeStepType::AssetIsDirty:
        case EProbeStepType::AssetIsClean:
        case EProbeStepType::AssetIsLoaded:
        case EProbeStepType::ObjectLoaded:
            Step.Target = OptionalAssetPath;
            break;
        case EProbeStepType::AssetRegistryTagEquals:
            Step.Target = OptionalAssetPath;
            break;
        case EProbeStepType::ClassIsChildOf:
            Step.Target = TEXT("/Script/Engine.Actor");
            Step.ExpectedClass = TEXT("/Script/CoreUObject.Object");
            break;
        case EProbeStepType::ClassImplementsInterface:
            Step.Target = TEXT("/Script/Engine.Actor");
            break;
        case EProbeStepType::ClassHasProperty:
            Step.Target = TEXT("/Script/Engine.Actor");
            Step.PropertyName = TEXT("Tags");
            break;
        case EProbeStepType::ClassHasFunction:
            Step.Target = TEXT("/Script/Engine.Actor");
            Step.PropertyName = TEXT("GetActorLocation");
            break;
        case EProbeStepType::ObjectPropertyExists:
            Step.Target = OptionalAssetPath;
            Step.PropertyName = TEXT("bEnabled");
            break;
        case EProbeStepType::ObjectPropertyNotEquals:
            Step.Target = OptionalAssetPath;
            Step.PropertyName = TEXT("bEnabled");
            Step.ExpectedValue = TEXT("False");
            break;
        case EProbeStepType::ActorLabelNotExists:
        case EProbeStepType::ActorTagNotExists:
        case EProbeStepType::ActorHasComponent:
        case EProbeStepType::ActorFolderEquals:
        case EProbeStepType::ActorPropertyEquals:
            if (Type == EProbeStepType::ActorHasComponent)
                Step.ExpectedClass = TEXT("/Script/Engine.SceneComponent");
            if (Type == EProbeStepType::ActorPropertyEquals)
                Step.PropertyName = TEXT("bHidden");
            break;
        case EProbeStepType::ActorCountExactly:
        case EProbeStepType::ActorClassCountExactly:
        case EProbeStepType::SelectedActorCountExactly:
            Step.MinimumActorCount = 1;
            if (Type == EProbeStepType::ActorClassCountExactly)
                Step.Target = TEXT("/Script/Engine.Actor");
            break;
        case EProbeStepType::SelectedActorIsClass:
            Step.Target = TEXT("/Script/Engine.Actor");
            break;
        case EProbeStepType::SelectedActorLabelEquals:
        case EProbeStepType::SelectedActorHasTag:
            break;
        }
        return Step;
    }


}

class SProbeWorkspace : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SProbeWorkspace) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        GConfig->GetFloat(TEXT("ProbeWorkspace"), TEXT("DefinitionFraction"), DefinitionFraction, GEditorPerProjectIni);
        DefinitionFraction = FMath::Clamp(DefinitionFraction, 0.20f, 0.85f);
        RefreshAssets();
        FEditorDelegates::PostUndoRedo.AddSP(this, &SProbeWorkspace::HandleUndoRedo);
        ChildSlot
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                .Padding(FMargin(5, 3))
                [
                    // Single command row. When docked extremely narrowly, the row can
                    // scroll horizontally rather than clipping inaccessible controls.
                    SNew(SScrollBox)
                    .Orientation(Orient_Horizontal)
                    .ScrollBarVisibility(EVisibility::Visible)
                    + SScrollBox::Slot()
                    [
                        SNew(SBox)
                        .MinDesiredWidth(685.0f)
                        .WidthOverride_Lambda([this]() -> FOptionalSize
                        {
                            // Fill normal-sized dock tabs; keep the toolbar horizontally
                            // scrollable rather than clipping actions in narrow docks.
                            return FOptionalSize(FMath::Max(685.0f,
                                GetCachedGeometry().GetLocalSize().X - 16.0f));
                        })
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 3, 0)
                            [
                                SNew(SButton)
                                .ToolTipText(LOCTEXT("CreateTip", "Create a blank Probe Test asset"))
                                .OnClicked(this, &SProbeWorkspace::CreateBlankAsset)
                                [
                                    SNew(SHorizontalBox)
                                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                                    [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Plus")) ]
                                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                                    [ SNew(STextBlock).Text(LOCTEXT("NewTest", "New Test")) ]
                                ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
                            [
                                SNew(SComboButton)
                                .ToolTipText(LOCTEXT("TemplateTip", "Create a Probe Test from a template"))
                                .OnGetMenuContent(this, &SProbeWorkspace::BuildTemplateMenu)
                                .ButtonContent()
                                [ SNew(STextBlock).Text(LOCTEXT("Templates", "Templates")) ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                SNew(SButton)
                                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                .ToolTipText(LOCTEXT("SaveTip", "Save selected test asset (Ctrl+S)"))
                                .IsEnabled(this, &SProbeWorkspace::CanSaveSelected)
                                .OnClicked(this, &SProbeWorkspace::SaveSelectedClicked)
                                [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Save")) ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                SNew(SButton)
                                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                .ToolTipText(LOCTEXT("RefreshTip", "Refresh available Probe tests"))
                                .OnClicked(this, &SProbeWorkspace::RefreshClicked)
                                [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Refresh")) ]
                            ]
                            + SHorizontalBox::Slot().FillWidth(1.0f)
                            [ SNullWidget::NullWidget ]
                            // Execution controls read left-to-right Export, Rerun,
                            // Run All, Run Selected. The primary action stays at the edge.
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                SNew(SButton)
                                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                .ToolTipText(LOCTEXT("ExportResultsTip", "Export current results as JSON in Saved/Probe/Reports"))
                                .IsEnabled(this, &SProbeWorkspace::HasResults)
                                .OnClicked(this, &SProbeWorkspace::ExportReport)
                                [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Export")) ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                SNew(SButton)
                                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                .ToolTipText(LOCTEXT("RerunProblemsTip", "Rerun tests that failed or errored"))
                                .IsEnabled(this, &SProbeWorkspace::HasProblemResults)
                                .OnClicked(this, &SProbeWorkspace::RerunProblems)
                                [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Reimport")) ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 3, 0)
                            [
                                SNew(SButton)
                                .ToolTipText(LOCTEXT("RunAllTip", "Run every discovered Probe test"))
                                .OnClicked(this, &SProbeWorkspace::RunAll)
                                [ SNew(STextBlock).Text(LOCTEXT("RunAll", "Run All")) ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                SNew(SButton)
                                .IsEnabled(this, &SProbeWorkspace::HasSelectedAsset)
                                .ToolTipText(LOCTEXT("RunSelectedTip", "Run the selected test without saving its unsaved edits"))
                                .OnClicked(this, &SProbeWorkspace::RunSelected)
                                [
                                    SNew(SHorizontalBox)
                                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                                    [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Play")) ]
                                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                                    [ SNew(STextBlock).Text(LOCTEXT("RunSelected", "Run Selected")) ]
                                ]
                            ]
                        ]
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(8, 7, 8, 6)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                    [ SNew(STextBlock).Text(LOCTEXT("SelectedTest", "Selected Test")) ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [ SNew(STextBlock).Text(this, &SProbeWorkspace::DirtyStatusText) ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SAssignNew(TestSelector, SComboBox<TSharedPtr<FAssetData>>)
                    .OptionsSource(&Assets)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FAssetData> Item)
                    {
                        return SNew(STextBlock).Text(FText::FromName(Item.IsValid() ? Item->AssetName : NAME_None));
                    })
                    .OnSelectionChanged(this, &SProbeWorkspace::OnSelectionChanged)
                    [ SNew(STextBlock).Text(this, &SProbeWorkspace::SelectedName) ]
                ]
            ]
            + SVerticalBox::Slot().FillHeight(1.0f).Padding(7, 0, 7, 0)
            [
                SNew(SSplitter).Orientation(Orient_Vertical)
                + SSplitter::Slot().Value(TAttribute<float>::Create(
                    TAttribute<float>::FGetter::CreateLambda([this]() { return bResultsFocused ? 0.0f : DefinitionFraction; })))
                .OnSlotResized(SSplitter::FOnSlotResized::CreateSP(this, &SProbeWorkspace::OnDefinitionResized))
                [
                    SNew(SVerticalBox)
                    .Visibility_Lambda([this]() { return bResultsFocused ? EVisibility::Collapsed : EVisibility::Visible; })
                    + SVerticalBox::Slot().AutoHeight().Padding(2, 3, 2, 6)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                        [ SNew(STextBlock).Text(LOCTEXT("DefinitionTitle", "Test Definition")) ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [
                            SNew(SComboButton)
                            .IsEnabled(this, &SProbeWorkspace::HasSelectedAsset)
                            .OnGetMenuContent(this, &SProbeWorkspace::BuildAddStepMenu)
                            .ToolTipText(LOCTEXT("AddStepTip", "Add an assertion step to this test"))
                            .ButtonContent()
                            [
                                SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                                [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Plus")) ]
                                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                                [ SNew(STextBlock).Text(LOCTEXT("AddStep", "Add Step")) ]
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot().FillHeight(1.0f)
                    [
                        SNew(SBorder)
                        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                        .Padding(4)
                        [
                            SNew(SScrollBox)
                            + SScrollBox::Slot()
                            [ SAssignNew(EditorRows, SVerticalBox) ]
                        ]
                    ]
                ]
                + SSplitter::Slot().Value(TAttribute<float>::Create(
                    TAttribute<float>::FGetter::CreateLambda([this]() { return bResultsFocused ? 1.0f : 1.0f - DefinitionFraction; })))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(2, 6, 2, 4)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
                        [ SNew(STextBlock).Text(LOCTEXT("Results", "Results")) ]
                        + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                        [ SNew(SScrollBox).Orientation(Orient_Horizontal).ScrollBarVisibility(EVisibility::Visible)
                          + SScrollBox::Slot() [ SAssignNew(ResultSummary, SHorizontalBox) ] ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
                        [
                            SNew(SButton)
                            .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                            .ToolTipText(LOCTEXT("FocusResultsTip", "Expand Results to use the entire workspace; click again to show Test Definition"))
                            .OnClicked(this, &SProbeWorkspace::ToggleResultsFocus)
                            [ SNew(STextBlock).Text_Lambda([this]() { return bResultsFocused
                                ? LOCTEXT("ShowDefinition", "Show Definition") : LOCTEXT("FocusResults", "Focus Results"); }) ]
                        ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [
                            SNew(SButton)
                            .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                            .ToolTipText(LOCTEXT("CopyResultsTip", "Copy the complete unfiltered Results log"))
                            .IsEnabled(this, &SProbeWorkspace::HasResults)
                            .OnClicked(this, &SProbeWorkspace::CopyResults)
                            [
                                SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                                [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Duplicate")) ]
                                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                                [ SNew(STextBlock).Text(LOCTEXT("CopyResults", "Copy Results")) ]
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(2, 0, 2, 4)
                    [ SAssignNew(ResultTabs, SHorizontalBox) ]
                    + SVerticalBox::Slot().FillHeight(1.0f)
                    [
                        SAssignNew(ResultViewSwitcher, SWidgetSwitcher)
                        + SWidgetSwitcher::Slot()
                        [
                            SNew(SSplitter).Orientation(Orient_Horizontal)
                            + SSplitter::Slot().Value(0.40f)
                            [
                                SNew(SBorder)
                                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                                .Padding(3)
                                [
                                    SNew(SScrollBox)
                                    + SScrollBox::Slot() [ SAssignNew(ResultList, SVerticalBox) ]
                                ]
                            ]
                            + SSplitter::Slot().Value(0.60f)
                            [
                                SNew(SBorder)
                                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                                .Padding(5)
                                [
                                    SNew(SScrollBox)
                                    + SScrollBox::Slot() [ SAssignNew(ResultDetails, SVerticalBox) ]
                                ]
                            ]
                        ]
                        + SWidgetSwitcher::Slot()
                        [
                            SAssignNew(ResultsLog, SMultiLineEditableTextBox)
                            .IsReadOnly(true)
                            .AllowContextMenu(true)
                            .AutoWrapText(false)
                            .Text(LOCTEXT("ResultsPlaceholder", "Run a test to see its results here."))
                        ]
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(8, 5, 8, 5)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(this, &SProbeWorkspace::FeedbackText).AutoWrapText(true)
                  .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(this, &SProbeWorkspace::RunInfoText)
                  .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
            ]
        ];
        RebuildTestEditor();
        BuildResultTabs();
        ShowResults();
    }

    virtual ~SProbeWorkspace() override
    {
        FEditorDelegates::PostUndoRedo.RemoveAll(this);
    }

    virtual bool SupportsKeyboardFocus() const override { return true; }

    virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
    {
        if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::S && CanSaveSelected())
        {
            SaveSelected();
            return FReply::Handled();
        }
        return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
    }

public:
    void SelectTestByPath(const FString& AssetPath)
    {
        RefreshAssets();
        for (const TSharedPtr<FAssetData>& Entry : Assets)
        {
            if (Entry.IsValid() && Entry->GetObjectPathString() == AssetPath)
            {
                if (TestSelector.IsValid()) { TestSelector->SetSelectedItem(Entry); }
                else { Selected = Entry; RebuildTestEditor(); }
                bResultsFocused = false;
                return;
            }
        }
        FeedbackMessage = LOCTEXT("ContextMissingTest", "Selected Probe Test was not found. Refresh the test list.");
    }

    void RunAssetPaths(const TArray<FString>& AssetPaths)
    {
        Results.Reset();
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        ExpandedDiagnosticTestPath.Empty();
        ExpandedDiagnosticIndices.Reset();
        for (const FString& Path : AssetPaths)
        {
            if (UProbeTestAsset* Test = LoadObject<UProbeTestAsset>(nullptr, *Path))
            {
                ExecuteAsset(Test);
            }
            else
            {
                Probe::FTestResult Missing;
                Missing.AssetPath = Path;
                Missing.Name = FPackageName::GetShortName(Path);
                Missing.Status = Probe::EResultStatus::Error;
                Missing.Detail = TEXT("Probe Test asset could not be loaded");
                Results.Add(MoveTemp(Missing));
            }
        }
        RecordRunCompleted();
        ShowResults();
        bResultsFocused = true;
    }

private:
    TArray<TSharedPtr<FAssetData>> Assets;
    TSharedPtr<FAssetData> Selected;
    TSharedPtr<SComboBox<TSharedPtr<FAssetData>>> TestSelector;
    TSharedPtr<SVerticalBox> EditorRows;
    TSharedPtr<SHorizontalBox> ResultSummary;
    TSharedPtr<SMultiLineEditableTextBox> ResultsLog;
    TSharedPtr<SVerticalBox> ResultList;
    TSharedPtr<SVerticalBox> ResultDetails;
    TSharedPtr<SWidgetSwitcher> ResultViewSwitcher;
    TSharedPtr<SHorizontalBox> ResultTabs;
    TArray<Probe::FTestResult> Results;
    enum class EResultFilter : uint8 { All, Passed, Failed, Skipped, Error };
    EResultFilter ActiveResultFilter = EResultFilter::All;
    int32 SelectedResultIndex = INDEX_NONE;
    bool bProblemsFirst = true;
    bool bShowLog = false;
    bool bResultsFocused = false;
    float DefinitionFraction = 0.64f;
    TSet<int32> ExpandedDiagnosticIndices;
    FString ExpandedDiagnosticTestPath;
    TSet<int32> ExpandedStepIndices;
    FString ExpandedAssetPath;
    bool bStepNotesShown = false;
    FDateTime LastRunTime;
    double LastRunDurationMs = 0.0;

    void OnDefinitionResized(float NewFraction)
    {
        if (!bResultsFocused)
        {
            DefinitionFraction = FMath::Clamp(NewFraction, 0.20f, 0.85f);
            GConfig->SetFloat(TEXT("ProbeWorkspace"), TEXT("DefinitionFraction"), DefinitionFraction, GEditorPerProjectIni);
        }
    }

    FReply ToggleResultsFocus()
    {
        bResultsFocused = !bResultsFocused;
        return FReply::Handled();
    }

    bool HasResults() const { return !Results.IsEmpty(); }
    bool HasProblemResults() const
    {
        for (const Probe::FTestResult& Result : Results)
        {
            if (Result.Status == Probe::EResultStatus::Failed || Result.Status == Probe::EResultStatus::Error)
            {
                return true;
            }
        }
        return false;
    }
    FText FeedbackMessage = LOCTEXT("InitialHint", "Edit the test below, then Save Asset and Run Selected.");

    UProbeTestAsset* SelectedAsset() const
    {
        return Selected.IsValid() ? Cast<UProbeTestAsset>(Selected->GetAsset()) : nullptr;
    }

    bool HasSelectedAsset() const { return SelectedAsset() != nullptr; }

    bool CanSaveSelected() const
    {
        const UProbeTestAsset* Asset = SelectedAsset();
        return Asset && Asset->GetOutermost() && Asset->GetOutermost()->IsDirty();
    }

    FText DirtyStatusText() const
    {
        return CanSaveSelected() ? LOCTEXT("Unsaved", "* Unsaved changes") : FText::GetEmpty();
    }

    FText FeedbackText() const { return FeedbackMessage; }

    void RefreshAssets()
    {
        const FString PreviousPath = Selected.IsValid() ? Selected->GetObjectPathString() : FString();
        Assets.Reset();
        Selected.Reset();
        FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
        FARFilter Filter;
        Filter.ClassPaths.Add(UProbeTestAsset::StaticClass()->GetClassPathName());
        Filter.bRecursiveClasses = true;
        TArray<FAssetData> Found;
        Registry.Get().GetAssets(Filter, Found);
        Found.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
        for (const FAssetData& Asset : Found)
        {
            TSharedPtr<FAssetData> Entry = MakeShared<FAssetData>(Asset);
            Assets.Add(Entry);
            if (Asset.GetObjectPathString() == PreviousPath) { Selected = Entry; }
        }
        if (!Selected.IsValid() && Assets.Num()) { Selected = Assets[0]; }
        if (TestSelector.IsValid())
        {
            TestSelector->RefreshOptions();
            TestSelector->SetSelectedItem(Selected);
        }
        RebuildTestEditor();
    }


    // Probe-specific authoring UI. The underlying UProbeTestAsset remains an ordinary
    // Data Asset; no on-disk schema changes are required to use this editor.
    enum class EStepTextField : uint8 { Description, Target, ExpectedClass, PropertyName, ExpectedValue };

    static const TCHAR* StepName(EProbeStepType Type)
    {
        return Probe::GetAssertionInfo(Type).Name;
    }

    static const TCHAR* TargetHint(EProbeStepType Type)
    {
        switch (Type)
        {
        case EProbeStepType::PluginEnabled: return TEXT("Plugin identifier, such as Probe");
        case EProbeStepType::ClassExists: return TEXT("Class name or path, e.g. /Script/Engine.Actor");
        case EProbeStepType::AssetExists: return TEXT("Asset object path, e.g. /Game/ProbeTests/Probe_Smoke.Probe_Smoke");
        case EProbeStepType::ActorClassCountAtLeast: return TEXT("Actor class path, e.g. /Script/Engine.Actor");
        case EProbeStepType::ActorTagExists: return TEXT("Actor tag to find in the current level");
        case EProbeStepType::ActorLabelExists: return TEXT("Exact World Outliner label, case-sensitive");
        case EProbeStepType::AssetClassIs: return TEXT("Asset object path to inspect");
        case EProbeStepType::ObjectPropertyEquals: return TEXT("Object or asset path containing the property");
        default: return Probe::GetAssertionInfo(Type).Description;
        }
    }

    template <typename EditFn>
    void ChangeAsset(TWeakObjectPtr<UProbeTestAsset> TestRef, const FText& TransactionLabel, EditFn&& Edit)
    {
        UProbeTestAsset* Asset = TestRef.Get();
        if (!Asset) { return; }
        const FScopedTransaction Transaction(TransactionLabel);
        Asset->Modify();
        Edit(*Asset);
        Asset->MarkPackageDirty();
    }

    template <typename EditFn>
    void ChangeStep(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index, const FText& TransactionLabel, EditFn&& Edit, bool bRebuild = false)
    {
        UProbeTestAsset* Asset = TestRef.Get();
        if (!Asset || !Asset->Steps.IsValidIndex(Index)) { return; }
        {
            const FScopedTransaction Transaction(TransactionLabel);
            Asset->Modify();
            Edit(Asset->Steps[Index]);
            Asset->MarkPackageDirty();
        }
        if (bRebuild && SelectedAsset() == Asset) { RebuildTestEditor(); }
    }

    void HandleUndoRedo()
    {
        RebuildTestEditor();
    }

    static FString StepFieldValue(const FProbeTestStep& Step, EStepTextField Field)
    {
        switch (Field)
        {
        case EStepTextField::Description: return Step.Description;
        case EStepTextField::Target: return Step.Target;
        case EStepTextField::ExpectedClass: return Step.ExpectedClass;
        case EStepTextField::PropertyName: return Step.PropertyName;
        case EStepTextField::ExpectedValue: return Step.ExpectedValue;
        }
        return FString();
    }

    // Consistent two-column rows mirror the UE Details panel rather than giving
    // every editable value the entire window width.
    void AddStringField(const TSharedRef<SVerticalBox>& Body, TWeakObjectPtr<UProbeTestAsset> TestRef,
        int32 Index, EStepTextField Field, const FText& Label, const FString& Hint = FString())
    {
        Body->AddSlot().AutoHeight().Padding(2, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SBox).WidthOverride(134.0f)
              [ SNew(STextBlock).Text(Label).ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [
                SNew(SEditableTextBox)
                .SelectAllTextWhenFocused(false)
                .HintText(FText::FromString(Hint))
                .Text_Lambda([TestRef, Index, Field]() -> FText
                {
                    const UProbeTestAsset* Asset = TestRef.Get();
                    return Asset && Asset->Steps.IsValidIndex(Index)
                        ? FText::FromString(StepFieldValue(Asset->Steps[Index], Field)) : FText::GetEmpty();
                })
                .OnTextCommitted_Lambda([this, TestRef, Index, Field](const FText& NewText, ETextCommit::Type)
                {
                    const UProbeTestAsset* Test = TestRef.Get();
                    if (!Test || !Test->Steps.IsValidIndex(Index)
                        || StepFieldValue(Test->Steps[Index], Field) == NewText.ToString()) { return; }
                    ChangeStep(TestRef, Index, LOCTEXT("EditStepField", "Edit Probe Test Step"),
                        [Field, Value = NewText.ToString()](FProbeTestStep& Step)
                        {
                            switch (Field)
                            {
                            case EStepTextField::Description: Step.Description = Value; break;
                            case EStepTextField::Target: Step.Target = Value; break;
                            case EStepTextField::ExpectedClass: Step.ExpectedClass = Value; break;
                            case EStepTextField::PropertyName: Step.PropertyName = Value; break;
                            case EStepTextField::ExpectedValue: Step.ExpectedValue = Value; break;
                            }
                        });
                })
            ]
        ];
    }

    FReply UseSelectedActorLabel(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        if (!GEditor || GEditor->GetSelectedActorCount() != 1)
        {
            FeedbackMessage = LOCTEXT("SelectExactlyOneActor", "Select exactly one level actor in the World Outliner, then try again.");
            return FReply::Handled();
        }
        UWorld* World = GEditor->GetEditorWorldContext().World();
        if (!World)
        {
            FeedbackMessage = LOCTEXT("NoWorldForCapture", "No Editor world is available for actor capture.");
            return FReply::Handled();
        }
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->IsSelectedInEditor())
            {
                const FString Label = It->GetActorLabel();
                ChangeStep(TestRef, Index, LOCTEXT("CaptureActorLabel", "Capture Selected Actor Label"),
                    [Label](FProbeTestStep& Step) { Step.Target = Label; }, true);
                FeedbackMessage = FText::Format(LOCTEXT("CapturedActor", "Using actor label: {0}"), FText::FromString(Label));
                return FReply::Handled();
            }
        }
        FeedbackMessage = LOCTEXT("SelectedActorNotFound", "Selected actor was not found in the active Editor world.");
        return FReply::Handled();
    }

    void AddActorCountField(const TSharedRef<SVerticalBox>& Body, TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        Body->AddSlot().AutoHeight().Padding(2, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SBox).WidthOverride(134.0f)
              [ SNew(STextBlock).Text_Lambda([TestRef, Index]()
                {
                    const UProbeTestAsset* Test = TestRef.Get();
                    if (!Test || !Test->Steps.IsValidIndex(Index)) { return LOCTEXT("ActorCountLabel", "Count"); }
                    const EProbeStepType Type = Test->Steps[Index].Type;
                    if (Type == EProbeStepType::AssetCountAtLeast) { return LOCTEXT("MinAssetsLabel", "Minimum Assets"); }
                    if (Type == EProbeStepType::ActorCountExactly || Type == EProbeStepType::ActorClassCountExactly
                        || Type == EProbeStepType::SelectedActorCountExactly) { return LOCTEXT("ExactCountLabel", "Exact Count"); }
                    return Type == EProbeStepType::SelectedActorCountAtLeast
                        ? LOCTEXT("MinSelectionLabel", "Minimum Selected")
                        : LOCTEXT("MinActorCountLabel", "Minimum Actors");
                })
                .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [
                SNew(SNumericEntryBox<int32>)
                .AllowSpin(true).MinValue(0)
                .Value_Lambda([TestRef, Index]() -> TOptional<int32>
                {
                    const UProbeTestAsset* Asset = TestRef.Get();
                    return Asset && Asset->Steps.IsValidIndex(Index)
                        ? TOptional<int32>(Asset->Steps[Index].MinimumActorCount) : TOptional<int32>();
                })
                .OnValueCommitted_Lambda([this, TestRef, Index](int32 NewValue, ETextCommit::Type)
                {
                    const UProbeTestAsset* Test = TestRef.Get();
                    if (!Test || !Test->Steps.IsValidIndex(Index)
                        || Test->Steps[Index].MinimumActorCount == FMath::Max(0, NewValue)) { return; }
                    ChangeStep(TestRef, Index, LOCTEXT("ActorCountEdit", "Edit Minimum Actor Count"),
                        [NewValue](FProbeTestStep& Step) { Step.MinimumActorCount = FMath::Max(0, NewValue); });
                })
            ]
        ];
    }

    void AddToleranceField(const TSharedRef<SVerticalBox>& Body, TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        Body->AddSlot().AutoHeight().Padding(2, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SBox).WidthOverride(134.0f)
              [ SNew(STextBlock).Text(LOCTEXT("ToleranceLabel", "Numeric Tolerance"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [
                SNew(SNumericEntryBox<double>)
                .MinValue(0.0)
                .Value_Lambda([TestRef, Index]() -> TOptional<double>
                {
                    const UProbeTestAsset* Asset = TestRef.Get();
                    return Asset && Asset->Steps.IsValidIndex(Index)
                        ? TOptional<double>(Asset->Steps[Index].NumericTolerance) : TOptional<double>();
                })
                .OnValueCommitted_Lambda([this, TestRef, Index](double NewValue, ETextCommit::Type)
                {
                    const UProbeTestAsset* Test = TestRef.Get();
                    if (!Test || !Test->Steps.IsValidIndex(Index)
                        || Test->Steps[Index].NumericTolerance == FMath::Max(0.0, NewValue)) { return; }
                    ChangeStep(TestRef, Index, LOCTEXT("ToleranceEdit", "Edit Numeric Tolerance"),
                        [NewValue](FProbeTestStep& Step) { Step.NumericTolerance = FMath::Max(0.0, NewValue); });
                })
            ]
        ];
    }

    FString StepSummary(const FProbeTestStep& Step) const
    {
        const Probe::FAssertionInfo& Info = Probe::GetAssertionInfo(Step.Type);
        if (Info.Fields == 0) { return TEXT("Editor world available"); }
        FString Summary;
        if (Info.Fields & Probe::TargetField)
        {
            Summary = Step.Target.IsEmpty() ? TEXT("Target not configured") : Step.Target;
        }
        if (Info.Fields & Probe::ClassField)
        {
            Summary += FString::Printf(TEXT("  |  class: %s"), *Step.ExpectedClass);
        }
        if (Info.Fields & Probe::PropertyField)
        {
            Summary += FString::Printf(TEXT("  |  %s"), *Step.PropertyName);
        }
        if (Info.Fields & Probe::ValueField)
        {
            Summary += FString::Printf(TEXT(" = %s"), *Step.ExpectedValue);
        }
        if (Info.Fields & Probe::CountField)
        {
            const bool bExact = Step.Type == EProbeStepType::ActorCountExactly
                || Step.Type == EProbeStepType::ActorClassCountExactly
                || Step.Type == EProbeStepType::SelectedActorCountExactly;
            Summary += FString::Printf(TEXT("  |  %s %d"), bExact ? TEXT("exactly") : TEXT("at least"), Step.MinimumActorCount);
        }
        return Summary.IsEmpty() ? FString(Info.Description) : Summary;
    }

    FReply ToggleStepExpanded(int32 Index)
    {
        if (ExpandedStepIndices.Contains(Index)) { ExpandedStepIndices.Remove(Index); }
        else { ExpandedStepIndices.Add(Index); }
        RebuildTestEditor();
        return FReply::Handled();
    }

    FReply ToggleStepNotes()
    {
        bStepNotesShown = !bStepNotesShown;
        RebuildTestEditor();
        return FReply::Handled();
    }

    void SetStepType(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index, EProbeStepType Type)
    {
        const UProbeTestAsset* Asset = TestRef.Get();
        if (!Asset || !Asset->Steps.IsValidIndex(Index) || Asset->Steps[Index].Type == Type) { return; }
        ChangeStep(TestRef, Index, LOCTEXT("StepTypeEdit", "Change Probe Test Step Type"),
            [Type](FProbeTestStep& Step) { Step.Type = Type; }, true);
    }

    // Shared categorized submenus prevent the assertion picker from becoming a flat 41-item list.
    TSharedRef<SWidget> BuildStepTypeMenu(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        FMenuBuilder Menu(true, nullptr);
        for (const FString& Category : Probe::AssertionCategories())
        {
            Menu.AddSubMenu(FText::FromString(Category), FText::FromString(Category + TEXT(" assertions")),
                FNewMenuDelegate::CreateLambda([this, TestRef, Index, Category](FMenuBuilder& SubMenu)
                {
                    for (const Probe::FAssertionInfo& Info : Probe::AssertionCatalog())
                    {
                        if (Category != Info.Category) { continue; }
                        SubMenu.AddMenuEntry(FText::FromString(Info.Name), FText::FromString(Info.Description), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::SetStepType, TestRef, Index, Info.Type)));
                    }
                }));
        }
        return Menu.MakeWidget();
    }

    FReply ReorderStep(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index, int32 Direction)
    {
        UProbeTestAsset* Asset = TestRef.Get();
        if (!Asset || !Asset->Steps.IsValidIndex(Index) || !Asset->Steps.IsValidIndex(Index + Direction))
        {
            return FReply::Handled();
        }
        ChangeAsset(TestRef, LOCTEXT("MoveStepTransaction", "Move Probe Test Step"),
            [Index, Direction](UProbeTestAsset& Test) { Test.Steps.Swap(Index, Index + Direction); });
        if (ExpandedStepIndices.Contains(Index))
        {
            ExpandedStepIndices.Remove(Index);
            ExpandedStepIndices.Add(Index + Direction);
        }
        RebuildTestEditor();
        return FReply::Handled();
    }

    FReply DuplicateStep(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        UProbeTestAsset* Asset = TestRef.Get();
        if (!Asset || !Asset->Steps.IsValidIndex(Index)) { return FReply::Handled(); }
        ChangeAsset(TestRef, LOCTEXT("DuplicateStepTransaction", "Duplicate Probe Test Step"),
            [Index](UProbeTestAsset& Test) { Test.Steps.Insert(Test.Steps[Index], Index + 1); });
        ExpandedStepIndices.Add(Index + 1);
        RebuildTestEditor();
        return FReply::Handled();
    }

    FReply DeleteStep(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        UProbeTestAsset* Asset = TestRef.Get();
        if (!Asset || !Asset->Steps.IsValidIndex(Index)) { return FReply::Handled(); }
        ChangeAsset(TestRef, LOCTEXT("DeleteStepTransaction", "Remove Probe Test Step"),
            [Index](UProbeTestAsset& Test) { Test.Steps.RemoveAt(Index); });
        RebuildTestEditor();
        return FReply::Handled();
    }

    TSharedRef<SWidget> MakeStepCard(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        UProbeTestAsset* Asset = TestRef.Get();
        check(Asset && Asset->Steps.IsValidIndex(Index));
        const FProbeTestStep& Step = Asset->Steps[Index];
        const EProbeStepType Type = Step.Type;
        const int32 StepCount = Asset->Steps.Num();
        const bool bExpanded = ExpandedStepIndices.Contains(Index);
        TSharedRef<SVerticalBox> Card = SNew(SVerticalBox);
        Card->AddSlot().AutoHeight()
        [
            SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(FMargin(5, 3))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                [
                    SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .ToolTipText(bExpanded ? LOCTEXT("CollapseStep", "Collapse step") : LOCTEXT("ExpandStep", "Expand step"))
                    .OnClicked_Lambda([this, Index]() { return ToggleStepExpanded(Index); })
                    [ SNew(SImage)
                      .Image(FSlateStyleRegistry::FindSlateStyle(TEXT("ProbeAssetAppearance"))
                          ? FSlateStyleRegistry::FindSlateStyle(TEXT("ProbeAssetAppearance"))->GetBrush(
                              bExpanded ? TEXT("Probe.DisclosureExpanded") : TEXT("Probe.DisclosureCollapsed"))
                          : FAppStyle::GetBrush(TEXT("Icons.ChevronRight"))) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                [ SNew(STextBlock).Text(FText::AsNumber(Index + 1)).ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SComboButton)
                        .ToolTipText(LOCTEXT("ChangeTypeTip", "Change assertion type"))
                        .OnGetMenuContent_Lambda([this, TestRef, Index]() { return BuildStepTypeMenu(TestRef, Index); })
                        .ButtonContent()
                        [ SNew(STextBlock).Text(FText::FromString(StepName(Type))) ]
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text(FText::FromString(StepSummary(Step)))
                      .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                      .Visibility(bExpanded ? EVisibility::Collapsed : EVisibility::Visible)
                      .AutoWrapText(true) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 1, 0)
                [
                    SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .ToolTipText(LOCTEXT("MoveStepUp", "Move step up"))
                    .IsEnabled(Index > 0)
                    .OnClicked_Lambda([this, TestRef, Index]() { return ReorderStep(TestRef, Index, -1); })
                    [ SNew(STextBlock).Text(FText::FromString(TEXT("↑"))) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .ToolTipText(LOCTEXT("MoveStepDown", "Move step down"))
                    .IsEnabled(Index + 1 < StepCount)
                    .OnClicked_Lambda([this, TestRef, Index]() { return ReorderStep(TestRef, Index, +1); })
                    [ SNew(STextBlock).Text(FText::FromString(TEXT("↓"))) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .ToolTipText(LOCTEXT("DuplicateStepTip", "Duplicate this step"))
                    .OnClicked_Lambda([this, TestRef, Index]() { return DuplicateStep(TestRef, Index); })
                    [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Duplicate")) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .ToolTipText(LOCTEXT("DeleteStepTip", "Remove this step"))
                    .OnClicked_Lambda([this, TestRef, Index]() { return DeleteStep(TestRef, Index); })
                    [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Delete")) ]
                ]
            ]
        ];
        if (bExpanded)
        {
            TSharedRef<SVerticalBox> Fields = SNew(SVerticalBox);
            const Probe::FAssertionInfo& Info = Probe::GetAssertionInfo(Type);
            if (Info.Fields & Probe::TargetField)
            {
                const FText Label = Type == EProbeStepType::AssetCountAtLeast ? LOCTEXT("FolderPathLabel", "Folder Path")
                    : Type == EProbeStepType::EditorTabOpen ? LOCTEXT("TabIdLabel", "Tab ID")
                    : Type == EProbeStepType::ToolMenuRegistered ? LOCTEXT("ToolMenuLabel", "Menu ID")
                    : Type == EProbeStepType::ModuleLoaded ? LOCTEXT("ModuleNameLabel", "Module Name")
                    : Type == EProbeStepType::ConsoleVariableExists || Type == EProbeStepType::ConsoleVariableEquals
                      ? LOCTEXT("CVarNameLabel", "CVar Name")
                    : Type == EProbeStepType::ActorHasComponent || Type == EProbeStepType::ActorFolderEquals
                      || Type == EProbeStepType::ActorPropertyEquals ? LOCTEXT("ActorLabelTarget", "Actor Label")
                    : LOCTEXT("StepTarget", "Target");
                AddStringField(Fields, TestRef, Index, EStepTextField::Target, Label, TargetHint(Type));
            }
            if (Type == EProbeStepType::ActorLabelExists || Type == EProbeStepType::ActorLabelNotExists
                || Type == EProbeStepType::ActorHasComponent || Type == EProbeStepType::ActorFolderEquals
                || Type == EProbeStepType::ActorPropertyEquals || Type == EProbeStepType::SelectedActorLabelEquals)
            {
                Fields->AddSlot().AutoHeight().Padding(FMargin(136.0f, 1.0f, 2.0f, 3.0f))
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .ToolTipText(LOCTEXT("UseSelectedActorTip", "Read the selected actor's Outliner label into Target. Does not change the actor or level."))
                    .OnClicked_Lambda([this, TestRef, Index]() { return UseSelectedActorLabel(TestRef, Index); })
                    [ SNew(STextBlock).Text(LOCTEXT("UseSelectedActor", "Use Selected Actor")) ]
                ];
            }
            if (Info.Fields & Probe::CountField) { AddActorCountField(Fields, TestRef, Index); }
            if (Info.Fields & Probe::ClassField)
            {
                AddStringField(Fields, TestRef, Index, EStepTextField::ExpectedClass,
                    Type == EProbeStepType::ActorHasComponent ? LOCTEXT("ComponentClassLabel", "Component Class")
                    : Type == EProbeStepType::ClassIsChildOf ? LOCTEXT("ParentClassLabel", "Parent Class")
                    : Type == EProbeStepType::ClassImplementsInterface ? LOCTEXT("InterfaceClassLabel", "Interface Class")
                    : LOCTEXT("StepClass", "Expected Class"), TEXT("/Script/Engine.Actor"));
            }
            if (Info.Fields & Probe::PropertyField)
            {
                AddStringField(Fields, TestRef, Index, EStepTextField::PropertyName,
                    Type == EProbeStepType::ClassHasFunction ? LOCTEXT("FunctionNameLabel", "Function Name")
                    : Type == EProbeStepType::AssetRegistryTagEquals ? LOCTEXT("TagNameLabel", "Tag Name")
                    : LOCTEXT("StepProperty", "Property Name"));
            }
            if (Info.Fields & Probe::ValueField)
            {
                AddStringField(Fields, TestRef, Index, EStepTextField::ExpectedValue,
                    Type == EProbeStepType::ActorFolderEquals ? LOCTEXT("FolderValueLabel", "Expected Folder")
                    : LOCTEXT("StepExpected", "Expected Value"));
            }
            if (Info.Fields & Probe::ToleranceField) { AddToleranceField(Fields, TestRef, Index); }
            if (bStepNotesShown)
            {
                AddStringField(Fields, TestRef, Index, EStepTextField::Description,
                    LOCTEXT("StepDescription", "Description"), TEXT("Optional note describing the assertion"));
            }
            Card->AddSlot().AutoHeight().Padding(5, 2, 5, 5)
            [ Fields ];
        }
        return Card;
    }

    void RebuildTestEditor()
    {
        if (!EditorRows.IsValid()) { return; }
        EditorRows->ClearChildren();
        UProbeTestAsset* Asset = SelectedAsset();
        if (!Asset)
        {
            ExpandedAssetPath.Reset();
            ExpandedStepIndices.Reset();
            EditorRows->AddSlot().AutoHeight().Padding(10)
            [ SNew(STextBlock).Text(LOCTEXT("NoTestSelected", "Create or select a Probe Test to edit its configuration.")) ];
            return;
        }
        const FString AssetPath = Asset->GetPathName();
        if (ExpandedAssetPath != AssetPath)
        {
            ExpandedAssetPath = AssetPath;
            ExpandedStepIndices.Reset();
            if (Asset->Steps.Num() > 0) { ExpandedStepIndices.Add(0); }
        }
        const TWeakObjectPtr<UProbeTestAsset> TestRef(Asset);
        TSharedRef<SVerticalBox> Settings = SNew(SVerticalBox);
        Settings->AddSlot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 9, 0)
            [ SNew(SImage).Image(FAppStyle::GetBrush("ClassIcon.DataAsset")) ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(STextBlock).Text(FText::FromString(Asset->GetName())).Font(FAppStyle::GetFontStyle("NormalFontBold")) ]
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
                  .Text(LOCTEXT("TestAssetType", "Probe Test Asset")) ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SCheckBox)
                .IsChecked_Lambda([TestRef]()
                {
                    const UProbeTestAsset* Test = TestRef.Get();
                    return Test && Test->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                })
                .OnCheckStateChanged_Lambda([this, TestRef](ECheckBoxState NewState)
                {
                    ChangeAsset(TestRef, LOCTEXT("ToggleTestTransaction", "Toggle Probe Test"),
                        [NewState](UProbeTestAsset& Test) { Test.bEnabled = NewState == ECheckBoxState::Checked; });
                })
                [ SNew(STextBlock).Text(LOCTEXT("TestEnabled", "Enabled")) ]
            ]
        ];
        Settings->AddSlot().AutoHeight().Padding(0, 7, 0, 0)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SBox).WidthOverride(102.0f)
              [ SNew(STextBlock).Text(LOCTEXT("TestDescription", "Description"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [
                SNew(SEditableTextBox)
                .HintText(LOCTEXT("DescriptionHint", "What does this test cover?"))
                .Text_Lambda([TestRef]()
                {
                    const UProbeTestAsset* Test = TestRef.Get();
                    return Test ? FText::FromString(Test->TestDescription) : FText::GetEmpty();
                })
                .OnTextCommitted_Lambda([this, TestRef](const FText& NewText, ETextCommit::Type)
                {
                    const UProbeTestAsset* Test = TestRef.Get();
                    if (!Test || Test->TestDescription == NewText.ToString()) { return; }
                    ChangeAsset(TestRef, LOCTEXT("DescriptionTransaction", "Edit Probe Test Description"),
                        [Value = NewText.ToString()](UProbeTestAsset& Test) { Test.TestDescription = Value; });
                })
            ]
        ];
        EditorRows->AddSlot().AutoHeight().Padding(3, 3, 3, 7)
        [
            SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(FMargin(9, 7))
            [ Settings ]
        ];
        EditorRows->AddSlot().AutoHeight().Padding(8, 4, 8, 5)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Test Steps (%d)"), Asset->Steps.Num()))) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                .ToolTipText(LOCTEXT("NotesToggleTip", "Show or hide optional step descriptions"))
                .OnClicked(this, &SProbeWorkspace::ToggleStepNotes)
                [ SNew(STextBlock).Text(bStepNotesShown
                    ? LOCTEXT("HideNotes", "Hide Notes") : LOCTEXT("ShowNotes", "Show Notes"))
                  .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
            ]
        ];
        if (Asset->Steps.IsEmpty())
        {
            EditorRows->AddSlot().AutoHeight().Padding(10)
            [ SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
                .Text(LOCTEXT("NoSteps", "This test has no steps. Use Add Step above to create its first assertion.")) ];
        }
        for (int32 Index = 0; Index < Asset->Steps.Num(); ++Index)
        {
            EditorRows->AddSlot().AutoHeight().Padding(3, 0, 3, 3)
            [ MakeStepCard(TestRef, Index) ];
        }
    }

    FText SelectedName() const
    {
        if (!Selected.IsValid()) { return LOCTEXT("None", "No Probe test assets found"); }
        FString Name = Selected->AssetName.ToString();
        if (CanSaveSelected()) { Name += TEXT(" *"); }
        return FText::FromString(Name);
    }

    void OnSelectionChanged(TSharedPtr<FAssetData> Item, ESelectInfo::Type)
    {
        Selected = Item;
        RebuildTestEditor();
        FeedbackMessage = LOCTEXT("SelectedHint", "Edit Steps here. Unsaved edits are retained when switching tests.");
    }

    FReply RefreshClicked()
    {
        RefreshAssets();
        FeedbackMessage = LOCTEXT("Refreshed", "Test list refreshed. Unsaved edits are retained in memory.");
        return FReply::Handled();
    }

    void SelectCreatedAsset(UObject* Created)
    {
        for (const TSharedPtr<FAssetData>& Item : Assets)
        {
            if (Item.IsValid() && Item->GetObjectPathString() == Created->GetPathName())
            {
                Selected = Item;
                if (TestSelector.IsValid()) { TestSelector->SetSelectedItem(Item); }
                RebuildTestEditor();
                return;
            }
        }
        // On rare delayed-registry cases the newly-created asset will appear on Refresh Tests.
    }

    FReply CreateBlankAsset()
    {
        CreateAsset(Probe::ETestTemplate::Blank);
        return FReply::Handled();
    }

    void CreateAsset(Probe::ETestTemplate Template)
    {
        UProbeTestAssetFactory* Factory = NewObject<UProbeTestAssetFactory>();
        FAssetToolsModule& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
        UObject* Created = Tools.Get().CreateAssetWithDialog(TEXT("Probe_Test"), TEXT("/Game/ProbeTests"), UProbeTestAsset::StaticClass(), Factory);
        if (!Created)
        {
            FeedbackMessage = LOCTEXT("CreateCancelled", "Test creation canceled.");
            return;
        }
        UProbeTestAsset* Asset = Cast<UProbeTestAsset>(Created);
        if (Asset && Template != Probe::ETestTemplate::Blank)
        {
            Asset->Modify();
            switch (Template)
            {
            case Probe::ETestTemplate::Smoke:
                Asset->TestDescription = TEXT("Basic Probe smoke test: plugin and Editor world availability");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::PluginEnabled));
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::EditorWorldExists));
                break;
            case Probe::ETestTemplate::Plugin:
                Asset->TestDescription = TEXT("Plugin availability check. Edit the Target plugin name.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::PluginEnabled));
                break;
            case Probe::ETestTemplate::World:
                Asset->TestDescription = TEXT("Editor world and minimum actor count checks");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::EditorWorldExists));
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ActorCountAtLeast));
                break;
            case Probe::ETestTemplate::Asset:
                Asset->TestDescription = TEXT("Check that an asset exists at the specified object path");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::AssetExists, Asset->GetPathName()));
                break;
            case Probe::ETestTemplate::ActorClass:
                Asset->TestDescription = TEXT("Check actors of a particular class in the Editor world");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ActorClassCountAtLeast));
                break;
            case Probe::ETestTemplate::ActorTag:
                Asset->TestDescription = TEXT("Check whether an actor has a tag. Set Target and tag an actor first.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ActorTagExists));
                break;
            case Probe::ETestTemplate::AssetClass:
                Asset->TestDescription = TEXT("Check this Probe Test asset's class. Change Target/Expected Class as needed.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::AssetClassIs, Asset->GetPathName()));
                break;
            case Probe::ETestTemplate::ActorLabel:
                Asset->TestDescription = TEXT("Verify an actor with a specific World Outliner label exists.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ActorLabelExists));
                break;
            case Probe::ETestTemplate::ActorSelection:
                Asset->TestDescription = TEXT("Verify the current selection contains at least one level actor.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::SelectedActorCountAtLeast));
                break;
            case Probe::ETestTemplate::Property:
                Asset->TestDescription = TEXT("Check this test asset's bEnabled property is True.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ObjectPropertyEquals, Asset->GetPathName()));
                break;
            case Probe::ETestTemplate::Module:
                Asset->TestDescription = TEXT("Verify ProbeEditor module has loaded.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ModuleLoaded));
                break;
            case Probe::ETestTemplate::EditorTab:
                Asset->TestDescription = TEXT("Verify the Probe Editor tab is open.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::EditorTabOpen));
                break;
            case Probe::ETestTemplate::ToolMenu:
                Asset->TestDescription = TEXT("Verify that an Editor ToolMenus path has been registered.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ToolMenuRegistered));
                break;
            case Probe::ETestTemplate::ConsoleVariable:
                Asset->TestDescription = TEXT("Check an Editor console variable is present.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ConsoleVariableExists));
                break;
            case Probe::ETestTemplate::AssetFolder:
                Asset->TestDescription = TEXT("Count assets in a folder recursively.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::AssetCountAtLeast));
                break;
            case Probe::ETestTemplate::AssetDirty:
                Asset->TestDescription = TEXT("Check this test asset has no unsaved changes after saving.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::AssetIsClean, Asset->GetPathName()));
                break;
            case Probe::ETestTemplate::ClassContract:
                Asset->TestDescription = TEXT("Verify an Actor class exposes a native property and function.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ClassHasProperty));
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ClassHasFunction));
                break;
            case Probe::ETestTemplate::ObjectPropertyAbsent:
                Asset->TestDescription = TEXT("Check a property is different from an expected value.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ObjectPropertyNotEquals, Asset->GetPathName()));
                break;
            case Probe::ETestTemplate::ActorComponent:
                Asset->TestDescription = TEXT("Check that an actor has a required component class. Capture an actor label.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ActorHasComponent));
                break;
            case Probe::ETestTemplate::ActorFolder:
                Asset->TestDescription = TEXT("Check a World Outliner actor folder path. Capture an actor label.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ActorFolderEquals));
                break;
            case Probe::ETestTemplate::SelectionExact:
                Asset->TestDescription = TEXT("Require exactly one actor selected in the level Editor.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::SelectedActorCountExactly));
                break;
            default: break;
            }
            Asset->MarkPackageDirty();
        }
        RefreshAssets();
        SelectCreatedAsset(Created);
        RebuildTestEditor();
        FeedbackMessage = FText::Format(LOCTEXT("Created", "Created {0}. Configure steps and Save Asset."), FText::FromString(Created->GetName()));
    }

    TSharedRef<SWidget> BuildTemplateMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        Menu.AddMenuEntry(LOCTEXT("BlankTemplate", "Blank Test"), LOCTEXT("BlankTemplateTip", "Create an empty test"), FSlateIcon(),
            FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::CreateAsset, Probe::ETestTemplate::Blank)));
        Menu.AddMenuEntry(LOCTEXT("SmokeTemplate", "Probe Smoke Test"), LOCTEXT("SmokeTemplateTip", "Verify Probe and the active Editor world"), FSlateIcon(),
            FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::CreateAsset, Probe::ETestTemplate::Smoke)));
        struct FTemplateInfo { Probe::ETestTemplate Type; const TCHAR* Category; const TCHAR* Name; const TCHAR* Description; };
        const TArray<FTemplateInfo> Templates = {
            {Probe::ETestTemplate::Plugin, TEXT("Plugins & Modules"), TEXT("Plugin Availability"), TEXT("Check plugin enabled state")},
            {Probe::ETestTemplate::Module, TEXT("Plugins & Modules"), TEXT("Module Loaded"), TEXT("Confirm ProbeEditor is loaded")},
            {Probe::ETestTemplate::EditorTab, TEXT("Editor UI"), TEXT("Editor Tab Open"), TEXT("Check a registered tab ID is currently open")},
            {Probe::ETestTemplate::ToolMenu, TEXT("Editor UI"), TEXT("Registered Tool Menu"), TEXT("Check an Editor menu ID exists")},
            {Probe::ETestTemplate::ConsoleVariable, TEXT("Editor State"), TEXT("Console Variable"), TEXT("Check a console variable is available")},
            {Probe::ETestTemplate::Asset, TEXT("Assets"), TEXT("Asset Availability"), TEXT("Check asset path exists")},
            {Probe::ETestTemplate::AssetClass, TEXT("Assets"), TEXT("Asset Class Check"), TEXT("Check this Test Asset class")},
            {Probe::ETestTemplate::AssetFolder, TEXT("Assets"), TEXT("Assets In Folder"), TEXT("Count assets in a content folder")},
            {Probe::ETestTemplate::AssetDirty, TEXT("Assets"), TEXT("Asset Save State"), TEXT("Check an asset package is clean")},
            {Probe::ETestTemplate::ClassContract, TEXT("Classes & Reflection"), TEXT("Class Property and Function"), TEXT("Check reflected properties and functions")},
            {Probe::ETestTemplate::World, TEXT("Actors & World"), TEXT("Editor World and Actors"), TEXT("Check Editor world and actor count")},
            {Probe::ETestTemplate::ActorClass, TEXT("Actors & World"), TEXT("Actor Class Count"), TEXT("Count actors derived from a class")},
            {Probe::ETestTemplate::ActorTag, TEXT("Actors & World"), TEXT("Actor Tag"), TEXT("Find an actor by tag")},
            {Probe::ETestTemplate::ActorLabel, TEXT("Actors & World"), TEXT("Actor Label"), TEXT("Find an actor by label")},
            {Probe::ETestTemplate::ActorComponent, TEXT("Actors & World"), TEXT("Actor Component"), TEXT("Check an actor component class")},
            {Probe::ETestTemplate::ActorFolder, TEXT("Actors & World"), TEXT("Actor Outliner Folder"), TEXT("Check an actor folder path")},
            {Probe::ETestTemplate::ActorSelection, TEXT("Selection"), TEXT("Selected Actors"), TEXT("Check a minimum actor selection count")},
            {Probe::ETestTemplate::SelectionExact, TEXT("Selection"), TEXT("Exact Selection Count"), TEXT("Check an exact actor selection count")},
            {Probe::ETestTemplate::Property, TEXT("Properties & Values"), TEXT("Object Property Check"), TEXT("Compare a reflected property")},
            {Probe::ETestTemplate::ObjectPropertyAbsent, TEXT("Properties & Values"), TEXT("Property Does Not Equal"), TEXT("Verify a property differs from a value")}
        };
        Menu.AddMenuSeparator();
        for (const FString& Category : Probe::AssertionCategories())
        {
            // Capture the descriptors by value; the submenu can be opened after this function returns.
            Menu.AddSubMenu(FText::FromString(Category), FText::FromString(Category + TEXT(" templates")),
                FNewMenuDelegate::CreateLambda([this, Category, Templates](FMenuBuilder& SubMenu)
                {
                    for (const FTemplateInfo& Info : Templates)
                    {
                        if (Category != Info.Category) { continue; }
                        SubMenu.AddMenuEntry(FText::FromString(Info.Name), FText::FromString(Info.Description), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::CreateAsset, Info.Type)));
                    }
                }));
        }
        return Menu.MakeWidget();
    }

    void AddStep(EProbeStepType Type)
    {
        UProbeTestAsset* Asset = SelectedAsset();
        if (!Asset) { return; }
        const FScopedTransaction Transaction(LOCTEXT("AddStepTransaction", "Add Probe Test Step"));
        Asset->Modify();
        Asset->Steps.Add(Probe::MakeStep(Type, Asset->GetPathName()));
        Asset->MarkPackageDirty();
        ExpandedStepIndices.Add(Asset->Steps.Num() - 1);
        RebuildTestEditor();
        FeedbackMessage = LOCTEXT("StepAdded", "Step added. Review its parameters below, then Save Asset.");
    }

    TSharedRef<SWidget> BuildAddStepMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        for (const FString& Category : Probe::AssertionCategories())
        {
            Menu.AddSubMenu(FText::FromString(Category), FText::FromString(Category + TEXT(" assertions")),
                FNewMenuDelegate::CreateLambda([this, Category](FMenuBuilder& SubMenu)
                {
                    for (const Probe::FAssertionInfo& Info : Probe::AssertionCatalog())
                    {
                        if (Category != Info.Category) { continue; }
                        SubMenu.AddMenuEntry(FText::FromString(Info.Name), FText::FromString(Info.Description), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::AddStep, Info.Type)));
                    }
                }));
        }
        return Menu.MakeWidget();
    }

    bool SaveSelected()
    {
        UProbeTestAsset* Asset = SelectedAsset();
        if (!Asset) { return false; }
        UPackage* Package = Asset->GetOutermost();
        if (!Package || !Package->IsDirty())
        {
            FeedbackMessage = LOCTEXT("NothingToSave", "No unsaved changes in the selected test.");
            return true;
        }
        TArray<UPackage*> PackagesToSave;
        PackagesToSave.Add(Package);
        const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
        if (bSaved && !Package->IsDirty())
        {
            FeedbackMessage = FText::Format(LOCTEXT("Saved", "Saved {0}."), FText::FromString(Asset->GetName()));
            return true;
        }
        FeedbackMessage = LOCTEXT("SaveFailed", "Save did not complete. Check Output Log or source control status.");
        return false;
    }

    FReply SaveSelectedClicked()
    {
        SaveSelected();
        return FReply::Handled();
    }

    void ExecuteAsset(UProbeTestAsset* Asset)
    {
        Results.Add(Probe::RunTest(Asset));
    }

    static const FSlateBrush* ResultIcon(Probe::EResultStatus Status)
    {
        const ISlateStyle* Style = FSlateStyleRegistry::FindSlateStyle(TEXT("ProbeAssetAppearance"));
        if (!Style) { return FAppStyle::GetBrush(TEXT("Icons.Warning")); }
        switch (Status)
        {
        case Probe::EResultStatus::Passed: return Style->GetBrush(TEXT("Probe.StatusPassed"));
        case Probe::EResultStatus::Failed: return Style->GetBrush(TEXT("Probe.StatusFailed"));
        case Probe::EResultStatus::Skipped: return Style->GetBrush(TEXT("Probe.StatusSkipped"));
        default: return Style->GetBrush(TEXT("Probe.StatusError"));
        }
    }

    static FLinearColor ResultColor(Probe::EResultStatus Status)
    {
        switch (Status)
        {
        case Probe::EResultStatus::Passed: return FLinearColor(0.30f, 0.70f, 0.36f);
        case Probe::EResultStatus::Failed: return FLinearColor(0.90f, 0.34f, 0.31f);
        case Probe::EResultStatus::Skipped: return FLinearColor(0.62f, 0.62f, 0.62f);
        default: return FLinearColor(0.95f, 0.36f, 0.27f);
        }
    }

    FString BuildResultsLogString() const
    {
        if (Results.IsEmpty())
        {
            return TEXT("Run a test to see its results here. Select any text to copy it.");
        }

        FString Log;
        for (const Probe::FTestResult& Result : Results)
        {
            if (!Log.IsEmpty()) { Log += TEXT("\n\n"); }
            Log += FString::Printf(TEXT("[%s] %s  (%.2f ms)\n%s"),
                Probe::StatusName(Result.Status), *Result.Name, Result.DurationMs, *Result.Detail);
            for (const Probe::FStepResult& Step : Result.Steps)
            {
                const FString StepLabel = Step.Description.IsEmpty()
                    ? FString::Printf(TEXT("Step %d"), Step.Index)
                    : FString::Printf(TEXT("Step %d: %s"), Step.Index, *Step.Description);
                Log += FString::Printf(TEXT("\n  [%s] %s  (%.2f ms)\n    %s"),
                    Probe::StatusName(Step.Status), *StepLabel, Step.DurationMs, *Step.Detail);
            }
        }
        return Log;
    }

    FText ResultsLogText() const { return FText::FromString(BuildResultsLogString()); }

    FReply CopyResults()
    {
        FPlatformApplicationMisc::ClipboardCopy(*BuildResultsLogString());
        FeedbackMessage = LOCTEXT("ResultsCopied", "Results log copied to clipboard.");
        return FReply::Handled();
    }

    FText RunInfoText() const
    {
        if (Results.IsEmpty()) { return FText::GetEmpty(); }
        return FText::FromString(FString::Printf(TEXT("%d test(s) | %.2f ms | %s"),
            Results.Num(), LastRunDurationMs, *LastRunTime.ToString(TEXT("%H:%M:%S"))));
    }

    static int32 ResultSortPriority(Probe::EResultStatus Status)
    {
        switch (Status)
        {
        case Probe::EResultStatus::Error: return 0;
        case Probe::EResultStatus::Failed: return 1;
        case Probe::EResultStatus::Skipped: return 2;
        case Probe::EResultStatus::Passed: return 3;
        }
        return 4;
    }

    bool MatchesFilter(Probe::EResultStatus Status) const
    {
        switch (ActiveResultFilter)
        {
        case EResultFilter::Passed: return Status == Probe::EResultStatus::Passed;
        case EResultFilter::Failed: return Status == Probe::EResultStatus::Failed;
        case EResultFilter::Skipped: return Status == Probe::EResultStatus::Skipped;
        case EResultFilter::Error: return Status == Probe::EResultStatus::Error;
        default: return true;
        }
    }

    FReply SelectResultFilter(EResultFilter Filter)
    {
        ActiveResultFilter = Filter;
        RebuildResultList();
        ShowResultsSummary();
        return FReply::Handled();
    }

    void AddSummaryFilter(int32 Count, const TCHAR* Label, EResultFilter Filter,
        const FLinearColor& Color)
    {
        if (!ResultSummary.IsValid()) { return; }
        ResultSummary->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
        [
            SNew(SButton)
            .ButtonStyle(FAppStyle::Get(), ActiveResultFilter == Filter ? "Button" : "SimpleButton")
            .ToolTipText(FText::FromString(FString::Printf(TEXT("Show %s results"), Label)))
            .OnClicked_Lambda([this, Filter]() { return SelectResultFilter(Filter); })
            [
                SNew(STextBlock)
                .Text(FText::FromString(FString::Printf(TEXT("%d %s"), Count, Label)))
                .ColorAndOpacity(Count > 0 ? FSlateColor(Color) : FSlateColor::UseSubduedForeground())
            ]
        ];
    }

    void ShowResultsSummary()
    {
        if (!ResultSummary.IsValid()) { return; }
        ResultSummary->ClearChildren();
        if (Results.IsEmpty())
        {
            ResultSummary->AddSlot().AutoWidth()
            [ SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
              .Text(LOCTEXT("NoRunsSummary", "No tests run")) ];
            return;
        }
        int32 Passed = 0, Failed = 0, Skipped = 0, Errors = 0;
        for (const Probe::FTestResult& Result : Results)
        {
            switch (Result.Status)
            {
            case Probe::EResultStatus::Passed: ++Passed; break;
            case Probe::EResultStatus::Failed: ++Failed; break;
            case Probe::EResultStatus::Skipped: ++Skipped; break;
            default: ++Errors; break;
            }
        }
        AddSummaryFilter(Results.Num(), TEXT("All"), EResultFilter::All,
            FLinearColor(0.8f, 0.8f, 0.8f));
        AddSummaryFilter(Passed, TEXT("Passed"), EResultFilter::Passed,
            ResultColor(Probe::EResultStatus::Passed));
        AddSummaryFilter(Failed, TEXT("Failed"), EResultFilter::Failed,
            ResultColor(Probe::EResultStatus::Failed));
        AddSummaryFilter(Skipped, TEXT("Skipped"), EResultFilter::Skipped,
            FLinearColor(0.62f, 0.62f, 0.62f));
        AddSummaryFilter(Errors, TEXT("Errors"), EResultFilter::Error,
            FLinearColor(0.90f, 0.30f, 0.28f));
    }

    FReply ChooseResultView(bool bLog)
    {
        bShowLog = bLog;
        if (ResultViewSwitcher.IsValid()) { ResultViewSwitcher->SetActiveWidgetIndex(bLog ? 1 : 0); }
        BuildResultTabs();
        return FReply::Handled();
    }

    FReply ToggleResultOrder()
    {
        bProblemsFirst = !bProblemsFirst;
        BuildResultTabs();
        RebuildResultList();
        return FReply::Handled();
    }

    void BuildResultTabs()
    {
        if (!ResultTabs.IsValid()) { return; }
        ResultTabs->ClearChildren();
        ResultTabs->AddSlot().AutoWidth().Padding(0, 0, 3, 0)
        [
            SNew(SButton)
            .ToolTipText(LOCTEXT("ExplorerTip", "Explore each test and its step diagnostics"))
            .OnClicked_Lambda([this]() { return ChooseResultView(false); })
            [ SNew(STextBlock).Text(LOCTEXT("ExplorerTitle", "Results Explorer"))
              .ColorAndOpacity(bShowLog ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground()) ]
        ];
        ResultTabs->AddSlot().AutoWidth().Padding(0, 0, 6, 0)
        [
            SNew(SButton)
            .ToolTipText(LOCTEXT("LogTip", "Select text, right-click to copy, or use Copy Results"))
            .OnClicked_Lambda([this]() { return ChooseResultView(true); })
            [ SNew(STextBlock).Text(LOCTEXT("LogTitle", "Log"))
              .ColorAndOpacity(bShowLog ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground()) ]
        ];
        ResultTabs->AddSlot().FillWidth(1.0f) [ SNullWidget::NullWidget ];
        ResultTabs->AddSlot().AutoWidth().VAlign(VAlign_Center)
        [
            SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
            .ToolTipText(LOCTEXT("SortTip", "Toggle problem-first ordering or original execution order"))
            .OnClicked(this, &SProbeWorkspace::ToggleResultOrder)
            [ SNew(STextBlock).Text(bProblemsFirst
                ? LOCTEXT("ProblemsFirst", "Problems First") : LOCTEXT("RunOrder", "Run Order"))
              .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
        ];
    }

    FReply SelectResult(int32 ResultIndex)
    {
        if (Results.IsValidIndex(ResultIndex))
        {
            SelectedResultIndex = ResultIndex;
            RebuildResultList();
            RebuildResultDetails();
        }
        return FReply::Handled();
    }

    void RebuildResultList()
    {
        if (!ResultList.IsValid()) { return; }
        ResultList->ClearChildren();
        ResultList->AddSlot().AutoHeight().Padding(5, 1, 5, 5)
        [ SNew(SHorizontalBox)
          + SHorizontalBox::Slot().FillWidth(1.0f)
          [ SNew(STextBlock).Text(LOCTEXT("ResultColumnTest", "Test"))
            .Font(FAppStyle::GetFontStyle("SmallFont"))
            .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
          + SHorizontalBox::Slot().AutoWidth()
          [ SNew(STextBlock).Text(LOCTEXT("ResultColumnSteps", "Steps"))
            .Font(FAppStyle::GetFontStyle("SmallFont"))
            .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ];
        TArray<int32> Visible;
        for (int32 Index = 0; Index < Results.Num(); ++Index)
        {
            if (MatchesFilter(Results[Index].Status)) { Visible.Add(Index); }
        }
        if (bProblemsFirst)
        {
            Visible.StableSort([this](int32 A, int32 B)
            {
                return ResultSortPriority(Results[A].Status) < ResultSortPriority(Results[B].Status);
            });
        }
        if (Visible.IsEmpty())
        {
            SelectedResultIndex = INDEX_NONE;
            ResultList->AddSlot().AutoHeight().Padding(7)
            [ SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
              .Text(Results.IsEmpty() ? LOCTEXT("RunToPopulate", "Run a test to see results.")
                    : LOCTEXT("NoMatchingResults", "No results match this filter.")) ];
            RebuildResultDetails();
            return;
        }
        if (!Visible.Contains(SelectedResultIndex)) { SelectedResultIndex = Visible[0]; }
        for (const int32 Index : Visible)
        {
            const Probe::FTestResult& Result = Results[Index];
            const bool bSelected = SelectedResultIndex == Index;
            ResultList->AddSlot().AutoHeight().Padding(0, 0, 0, 2)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                .BorderBackgroundColor(bSelected ? FLinearColor(0.19f, 0.27f, 0.35f, 1.0f)
                                                  : FLinearColor(1.0f, 1.0f, 1.0f, 0.12f))
                .Padding(1)
                [
                    SNew(SButton)
                    .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .ToolTipText(FText::FromString(Result.Detail))
                    .OnClicked_Lambda([this, Index]() { return SelectResult(Index); })
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 5, 0)
                        [ SNew(SImage).Image(ResultIcon(Result.Status)) ]
                        + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                        [ SNew(STextBlock).Text(FText::FromString(Result.Name)).AutoWrapText(true) ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3, 0)
                        [ SNew(STextBlock).Text(FText::AsNumber(Result.Steps.Num()))
                          .ToolTipText(LOCTEXT("StepCountTip", "Recorded steps"))
                          .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
                    ]
                ]
            ];
        }
        RebuildResultDetails();
    }

    FString BuildSelectedResultString() const
    {
        if (!Results.IsValidIndex(SelectedResultIndex)) { return FString(); }
        const Probe::FTestResult& Result = Results[SelectedResultIndex];
        FString Text = FString::Printf(TEXT("[%s] %s (%.2f ms)\n%s"),
            Probe::StatusName(Result.Status), *Result.Name, Result.DurationMs, *Result.Detail);
        for (const Probe::FStepResult& Step : Result.Steps)
        {
            Text += FString::Printf(TEXT("\n[%s] Step %d: %s (%.2f ms)\n%s"),
                Probe::StatusName(Step.Status), Step.Index, *Step.Description,
                Step.DurationMs, *Step.Detail);
        }
        return Text;
    }

    FReply EditSelectedResultTest()
    {
        if (!Results.IsValidIndex(SelectedResultIndex)) { return FReply::Handled(); }
        const FString Path = Results[SelectedResultIndex].AssetPath;
        for (const TSharedPtr<FAssetData>& Entry : Assets)
        {
            if (Entry.IsValid() && Entry->GetObjectPathString() == Path)
            {
                if (TestSelector.IsValid()) { TestSelector->SetSelectedItem(Entry); }
                else { Selected = Entry; RebuildTestEditor(); }
                FeedbackMessage = LOCTEXT("EditResultTestHint", "Selected the test definition for editing.");
                return FReply::Handled();
            }
        }
        FeedbackMessage = LOCTEXT("MissingResultAssetHint", "The result's test asset is not in the current test list. Refresh Tests.");
        return FReply::Handled();
    }

    FReply EditSelectedResultStep(int32 StepNumber)
    {
        const FReply Result = EditSelectedResultTest();
        if (StepNumber > 0)
        {
            ExpandedStepIndices.Add(StepNumber - 1);
            bResultsFocused = false;
            RebuildTestEditor();
        }
        return Result;
    }

    FReply CopySelectedDetails()
    {
        FPlatformApplicationMisc::ClipboardCopy(*BuildSelectedResultString());
        FeedbackMessage = LOCTEXT("SelectedDetailsCopied", "Selected test diagnostics copied.");
        return FReply::Handled();
    }

    void RebuildResultDetails()
    {
        if (!ResultDetails.IsValid()) { return; }
        ResultDetails->ClearChildren();
        if (!Results.IsValidIndex(SelectedResultIndex))
        {
            ResultDetails->AddSlot().AutoHeight().Padding(6)
            [ SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
              .Text(LOCTEXT("SelectResultHint", "Select a result to inspect its steps.")) ];
            return;
        }
        const Probe::FTestResult& Result = Results[SelectedResultIndex];
        if (ExpandedDiagnosticTestPath != Result.AssetPath)
        {
            ExpandedDiagnosticTestPath = Result.AssetPath;
            ExpandedDiagnosticIndices.Reset();
            for (const Probe::FStepResult& Step : Result.Steps)
            {
                if (Step.Status == Probe::EResultStatus::Failed || Step.Status == Probe::EResultStatus::Error)
                {
                    ExpandedDiagnosticIndices.Add(Step.Index);
                }
            }
        }
        ResultDetails->AddSlot().AutoHeight().Padding(3, 3, 3, 6)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(Result.Name))
              .Font(FAppStyle::GetFontStyle("NormalFontBold")).AutoWrapText(true) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
            [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
              .ToolTipText(LOCTEXT("EditResultTip", "Select this test in the Test Definition editor"))
              .OnClicked(this, &SProbeWorkspace::EditSelectedResultTest)
              [ SNew(STextBlock).Text(LOCTEXT("EditResultTest", "Edit Test")) ] ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
              .ToolTipText(LOCTEXT("CopyDetailTip", "Copy diagnostics for this test"))
              .OnClicked(this, &SProbeWorkspace::CopySelectedDetails)
              [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Duplicate")) ] ]
        ];
        ResultDetails->AddSlot().AutoHeight().Padding(3, 0, 3, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
            [ SNew(STextBlock).Text(FText::FromString(Probe::StatusName(Result.Status)))
              .ColorAndOpacity(ResultColor(Result.Status)) ]
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%.2f ms"), Result.DurationMs)))
              .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
        ];
        ResultDetails->AddSlot().AutoHeight().Padding(3, 0, 3, 8)
        [ SNew(STextBlock).Text(FText::FromString(Result.Detail)).AutoWrapText(true) ];
        for (const Probe::FStepResult& Step : Result.Steps)
        {
            const bool bExpanded = ExpandedDiagnosticIndices.Contains(Step.Index);
            const int32 StepIndex = Step.Index;
            ResultDetails->AddSlot().AutoHeight().Padding(2, 0, 2, 4)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                .Padding(FMargin(5, 3))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.0f)
                        [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                          .ToolTipText(bExpanded ? LOCTEXT("HideDiagnostic", "Collapse diagnostic") : LOCTEXT("ShowDiagnostic", "Expand diagnostic"))
                          .OnClicked_Lambda([this, StepIndex]()
                          {
                              if (ExpandedDiagnosticIndices.Contains(StepIndex)) { ExpandedDiagnosticIndices.Remove(StepIndex); }
                              else { ExpandedDiagnosticIndices.Add(StepIndex); }
                              RebuildResultDetails();
                              return FReply::Handled();
                          })
                          [ SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 7, 0)
                            [ SNew(SImage).Image(FSlateStyleRegistry::FindSlateStyle(TEXT("ProbeAssetAppearance"))
                                ? FSlateStyleRegistry::FindSlateStyle(TEXT("ProbeAssetAppearance"))->GetBrush(
                                    bExpanded ? TEXT("Probe.DisclosureExpanded") : TEXT("Probe.DisclosureCollapsed"))
                                : FAppStyle::GetBrush(TEXT("Icons.ChevronRight"))) ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                            [ SNew(STextBlock).Text(FText::FromString(Probe::StatusName(Step.Status)))
                              .ColorAndOpacity(ResultColor(Step.Status)) ]
                            + SHorizontalBox::Slot().FillWidth(1.0f)
                            [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Step %d: %s"), Step.Index, *Step.Description)))
                              .AutoWrapText(true) ] ] ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                          .ToolTipText(LOCTEXT("EditDiagnosticStepTip", "Select this step in Test Definition"))
                          .OnClicked_Lambda([this, StepIndex]() { return EditSelectedResultStep(StepIndex); })
                          [ SNew(STextBlock).Text(LOCTEXT("EditDiagnosticStep", "Edit Step")) ] ]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(18, 4, 2, 3)
                    [ SNew(STextBlock).Text(FText::FromString(Step.Detail))
                      .AutoWrapText(true)
                      .Visibility(bExpanded ? EVisibility::Visible : EVisibility::Collapsed)
                      .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
                ]
            ];
        }
    }

    void ShowResults()
    {
        if (ResultsLog.IsValid()) { ResultsLog->SetText(ResultsLogText()); }
        ShowResultsSummary();
        RebuildResultList();
    }

    FReply RunSelected()
    {
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        Results.Reset();
        if (UProbeTestAsset* Asset = SelectedAsset())
        {
            ExecuteAsset(Asset);
            FeedbackMessage = LOCTEXT("SelectedExecuted", "Selected test executed. See Results below.");
        }
        else { FeedbackMessage = LOCTEXT("NoSelectedTest", "Select a Probe Test asset first."); }
        RecordRunCompleted();
        ShowResults();
        return FReply::Handled();
    }

    FReply RunAll()
    {
        RefreshAssets();
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        Results.Reset();
        for (const TSharedPtr<FAssetData>& Entry : Assets)
        {
            if (Entry.IsValid()) { ExecuteAsset(Cast<UProbeTestAsset>(Entry->GetAsset())); }
        }
        FeedbackMessage = LOCTEXT("AllExecuted", "Test run complete. See Results below.");
        RecordRunCompleted();
        ShowResults();
        return FReply::Handled();
    }

    FReply RerunProblems()
    {
        TArray<FString> Paths;
        for (const Probe::FTestResult& Result : Results)
        {
            if (Result.Status == Probe::EResultStatus::Failed || Result.Status == Probe::EResultStatus::Error)
            {
                Paths.AddUnique(Result.AssetPath);
            }
        }
        Results.Reset();
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        for (const FString& Path : Paths)
        {
            if (UProbeTestAsset* Asset = Cast<UProbeTestAsset>(FindObject<UObject>(nullptr, *Path)))
            {
                ExecuteAsset(Asset);
            }
            else if (UProbeTestAsset* LoadedAsset = LoadObject<UProbeTestAsset>(nullptr, *Path))
            {
                ExecuteAsset(LoadedAsset);
            }
            else
            {
                Probe::FTestResult Missing;
                Missing.Name = Path;
                Missing.AssetPath = Path;
                Missing.Status = Probe::EResultStatus::Error;
                Missing.Detail = TEXT("Test asset could not be loaded for rerun");
                Results.Add(MoveTemp(Missing));
            }
        }
        FeedbackMessage = FText::Format(LOCTEXT("ProblemsRerun", "Reran {0} previously failed/error test(s)."), FText::AsNumber(Paths.Num()));
        RecordRunCompleted();
        ShowResults();
        return FReply::Handled();
    }

    void RecordRunCompleted()
    {
        LastRunTime = FDateTime::Now();
        LastRunDurationMs = 0.0;
        for (const Probe::FTestResult& Result : Results) { LastRunDurationMs += Result.DurationMs; }
        FeedbackMessage = LOCTEXT("RunCompleted", "Run complete. Select a result for diagnostics.");
    }

    FReply ExportReport()
    {
        if (Results.IsEmpty()) { return FReply::Handled(); }
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("format"), TEXT("ProbeReport/1"));
        Root->SetStringField(TEXT("timestamp"), FDateTime::Now().ToIso8601());
        Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
        TArray<TSharedPtr<FJsonValue>> Tests;
        for (const Probe::FTestResult& Test : Results)
        {
            TSharedRef<FJsonObject> TestJson = MakeShared<FJsonObject>();
            TestJson->SetStringField(TEXT("test_id"), Test.Id);
            TestJson->SetStringField(TEXT("name"), Test.Name);
            TestJson->SetStringField(TEXT("asset_path"), Test.AssetPath);
            TestJson->SetStringField(TEXT("status"), Probe::StatusName(Test.Status));
            TestJson->SetStringField(TEXT("detail"), Test.Detail);
            TestJson->SetNumberField(TEXT("duration_ms"), Test.DurationMs);
            TArray<TSharedPtr<FJsonValue>> Steps;
            for (const Probe::FStepResult& Step : Test.Steps)
            {
                TSharedRef<FJsonObject> StepJson = MakeShared<FJsonObject>();
                StepJson->SetNumberField(TEXT("index"), Step.Index);
                StepJson->SetStringField(TEXT("status"), Probe::StatusName(Step.Status));
                StepJson->SetStringField(TEXT("description"), Step.Description);
                StepJson->SetStringField(TEXT("detail"), Step.Detail);
                StepJson->SetNumberField(TEXT("duration_ms"), Step.DurationMs);
                Steps.Add(MakeShared<FJsonValueObject>(StepJson));
            }
            TestJson->SetArrayField(TEXT("steps"), Steps);
            Tests.Add(MakeShared<FJsonValueObject>(TestJson));
        }
        Root->SetArrayField(TEXT("tests"), Tests);
        FString Json;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
        if (!FJsonSerializer::Serialize(Root, Writer))
        {
            FeedbackMessage = LOCTEXT("ReportSerializeFailed", "Report serialization failed.");
            return FReply::Handled();
        }
        const FString ReportDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Probe"), TEXT("Reports"));
        IFileManager::Get().MakeDirectory(*ReportDirectory, true);
        const FString FileName = FString::Printf(TEXT("Probe-%s-%s.json"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6));
        const FString FilePath = FPaths::Combine(ReportDirectory, FileName);
        if (FFileHelper::SaveStringToFile(Json, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            FeedbackMessage = FText::FromString(FString::Printf(TEXT("Report exported: %s"), *FilePath));
        }
        else
        {
            FeedbackMessage = FText::FromString(FString::Printf(TEXT("Failed to write report: %s"), *FilePath));
        }
        return FReply::Handled();
    }

};

class FProbeEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        RegisterAssetAppearance();

        // Record only Probe assets affected by Undo/Redo. The post-transaction callback
        // restores package dirtiness after the engine has restored transaction package flags.
        FCoreUObjectDelegates::OnObjectTransacted.AddRaw(this, &FProbeEditorModule::OnObjectTransacted);
        FEditorDelegates::PostUndoRedo.AddRaw(this, &FProbeEditorModule::OnPostUndoRedo);
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(Probe::TabId,
            FOnSpawnTab::CreateRaw(this, &FProbeEditorModule::SpawnTab))
            .SetDisplayName(LOCTEXT("TabName", "Probe"))
            .SetIcon(FSlateIcon(TEXT("ProbeAssetAppearance"), TEXT("Probe.TabIcon")))
            .SetMenuType(ETabSpawnerMenuType::Hidden);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FProbeEditorModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        FEditorDelegates::PostUndoRedo.RemoveAll(this);
        FCoreUObjectDelegates::OnObjectTransacted.RemoveAll(this);
        PendingUndoAssets.Reset();
        if (UToolMenus::TryGet() != nullptr)
        {
            UToolMenus::UnRegisterStartupCallback(this);
            UToolMenus::UnregisterOwner(this);
        }
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(Probe::TabId);
        if (AssetStyle.IsValid())
        {
            FSlateStyleRegistry::UnRegisterSlateStyle(*AssetStyle);
            AssetStyle.Reset();
        }
    }

public:
    /** Called by the Probe Test Asset Definition for standard Content Browser opening. */
    void OpenTestAssets(const TArray<FString>& Paths)
    {
        OpenTestsFromBrowser(Paths);
    }

private:
    TSharedPtr<FSlateStyleSet> AssetStyle;
    TWeakPtr<SProbeWorkspace> Workspace;

    TSharedPtr<SProbeWorkspace> OpenWorkspace()
    {
        FGlobalTabmanager::Get()->TryInvokeTab(Probe::TabId);
        return Workspace.Pin();
    }

    void OpenTestsFromBrowser(const TArray<FString>& Paths)
    {
        if (Paths.IsEmpty()) { return; }
        if (TSharedPtr<SProbeWorkspace> Panel = OpenWorkspace())
        {
            Panel->SelectTestByPath(Paths[0]);
        }
    }

    void RunTestsFromBrowser(const TArray<FString>& Paths)
    {
        if (Paths.IsEmpty()) { return; }
        if (TSharedPtr<SProbeWorkspace> Panel = OpenWorkspace())
        {
            Panel->SelectTestByPath(Paths[0]);
            Panel->RunAssetPaths(Paths);
        }
    }

    void DuplicateTestsFromBrowser(const TArray<FString>& Paths)
    {
        IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        for (const FString& Path : Paths)
        {
            UProbeTestAsset* Original = LoadObject<UProbeTestAsset>(nullptr, *Path);
            if (!Original) { continue; }
            FString PackageName, AssetName;
            Tools.CreateUniqueAssetName(Original->GetOutermost()->GetName(), TEXT("_Copy"), PackageName, AssetName);
            if (UProbeTestAsset* Copy = Cast<UProbeTestAsset>(
                Tools.DuplicateAsset(AssetName, FPackageName::GetLongPackagePath(PackageName), Original)))
            {
                // Duplicating a serialized asset also duplicates its GUID. Reports need
                // an independent test identity, not two tests with the same stable ID.
                Copy->Modify();
                Copy->TestId = FGuid::NewGuid();
                // Self-targeting templates should still target the copied asset.
                for (FProbeTestStep& Step : Copy->Steps)
                {
                    if (Step.Target == Original->GetPathName())
                    {
                        Step.Target = Copy->GetPathName();
                    }
                }
                Copy->MarkPackageDirty();
            }
        }
        if (TSharedPtr<SProbeWorkspace> Panel = Workspace.Pin())
        {
            Panel->SelectTestByPath(Paths.IsEmpty() ? FString() : Paths[0]);
        }
    }

    void AddBrowserTestActions(FToolMenuSection& Section)
    {
        const UContentBrowserAssetContextMenuContext* Context =
            UContentBrowserAssetContextMenuContext::FindContextWithAssets(Section);
        if (!Context) { return; }
        TArray<FString> Paths;
        for (const FAssetData& Data : Context->SelectedAssets)
        {
            if (Data.AssetClassPath == UProbeTestAsset::StaticClass()->GetClassPathName())
            {
                Paths.Add(Data.GetObjectPathString());
            }
        }
        if (Paths.IsEmpty() || Paths.Num() != Context->SelectedAssets.Num()) { return; }

        FToolUIAction OpenAction;
        OpenAction.ExecuteAction = FToolMenuExecuteAction::CreateLambda([this, Paths](const FToolMenuContext&)
        {
            OpenTestsFromBrowser(Paths);
        });
        Section.AddMenuEntry(TEXT("ProbeOpenInWorkspace"),
            LOCTEXT("ContextOpenProbe", "Open in Probe"),
            LOCTEXT("ContextOpenProbeTip", "Open the first selected Probe Test in the Probe editor"),
            FSlateIcon(TEXT("ProbeAssetAppearance"), TEXT("Probe.TabIcon")), OpenAction);

        FToolUIAction RunAction;
        RunAction.ExecuteAction = FToolMenuExecuteAction::CreateLambda([this, Paths](const FToolMenuContext&)
        {
            RunTestsFromBrowser(Paths);
        });
        Section.AddMenuEntry(TEXT("ProbeRunTests"),
            Paths.Num() == 1 ? LOCTEXT("ContextRunTest", "Run Test") : LOCTEXT("ContextRunTests", "Run Selected Tests"),
            LOCTEXT("ContextRunTip", "Execute the selected Probe Test assets and inspect results in Probe"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Play")), RunAction);

        FToolUIAction DuplicateAction;
        DuplicateAction.ExecuteAction = FToolMenuExecuteAction::CreateLambda([this, Paths](const FToolMenuContext&)
        {
            DuplicateTestsFromBrowser(Paths);
        });
        Section.AddMenuEntry(TEXT("ProbeDuplicateTest"),
            Paths.Num() == 1 ? LOCTEXT("ContextDuplicateTest", "Duplicate Test") : LOCTEXT("ContextDuplicateTests", "Duplicate Selected Tests"),
            LOCTEXT("ContextDuplicateTip", "Create independent copies of the selected tests, with unique asset names"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Duplicate")), DuplicateAction);
    }


    void RegisterAssetAppearance()
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Probe"));
        if (!Plugin.IsValid())
        {
            return;
        }

        // "ProbeTestAsset" is the UObject class name without its U prefix.
        // This changes only Editor presentation, not the serialized asset class.
        AssetStyle = MakeShared<FSlateStyleSet>(TEXT("ProbeAssetAppearance"));
        AssetStyle->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));
        AssetStyle->Set(TEXT("ClassThumbnail.ProbeTestAsset"),
            new FSlateImageBrush(
                AssetStyle->RootToContentDir(TEXT("ProbeTestThumbnail"), TEXT(".png")),
                FVector2D(256.0f, 256.0f)));
        AssetStyle->Set(TEXT("ClassIcon.ProbeTestAsset"),
            new FSlateImageBrush(
                AssetStyle->RootToContentDir(TEXT("ProbeTestIcon16"), TEXT(".png")),
                FVector2D(16.0f, 16.0f)));
        AssetStyle->Set(TEXT("Probe.TabIcon"),
            new FSlateImageBrush(
                AssetStyle->RootToContentDir(TEXT("ProbeTabIcon16"), TEXT(".png")),
                FVector2D(16.0f, 16.0f)));
        AssetStyle->Set(TEXT("Probe.DisclosureCollapsed"),
            new FSlateImageBrush(AssetStyle->RootToContentDir(TEXT("ProbeDisclosureCollapsed16"), TEXT(".png")), FVector2D(12.0f, 12.0f)));
        AssetStyle->Set(TEXT("Probe.DisclosureExpanded"),
            new FSlateImageBrush(AssetStyle->RootToContentDir(TEXT("ProbeDisclosureExpanded16"), TEXT(".png")), FVector2D(12.0f, 12.0f)));
        AssetStyle->Set(TEXT("Probe.StatusPassed"),
            new FSlateImageBrush(AssetStyle->RootToContentDir(TEXT("ProbeStatusPassed16"), TEXT(".png")), FVector2D(12.0f, 12.0f)));
        AssetStyle->Set(TEXT("Probe.StatusFailed"),
            new FSlateImageBrush(AssetStyle->RootToContentDir(TEXT("ProbeStatusFailed16"), TEXT(".png")), FVector2D(12.0f, 12.0f)));
        AssetStyle->Set(TEXT("Probe.StatusError"),
            new FSlateImageBrush(AssetStyle->RootToContentDir(TEXT("ProbeStatusError16"), TEXT(".png")), FVector2D(12.0f, 12.0f)));
        AssetStyle->Set(TEXT("Probe.StatusSkipped"),
            new FSlateImageBrush(AssetStyle->RootToContentDir(TEXT("ProbeStatusSkipped16"), TEXT(".png")), FVector2D(12.0f, 12.0f)));
        FSlateStyleRegistry::RegisterSlateStyle(*AssetStyle);
    }

    TArray<TWeakObjectPtr<UProbeTestAsset>> PendingUndoAssets;

    void OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event)
    {
        if (Event.GetEventType() != ETransactionObjectEventType::UndoRedo)
        {
            return;
        }
        if (UProbeTestAsset* Test = Cast<UProbeTestAsset>(Object))
        {
            PendingUndoAssets.AddUnique(TWeakObjectPtr<UProbeTestAsset>(Test));
        }
    }

    void OnPostUndoRedo()
    {
        for (const TWeakObjectPtr<UProbeTestAsset>& Test : PendingUndoAssets)
        {
            if (UProbeTestAsset* Asset = Test.Get())
            {
                // Transaction restore can reset the saved dirty flag. Mark affected
                // assets after the operation so Save Asset and Save All can detect edits.
                Asset->MarkPackageDirty();
            }
        }
        PendingUndoAssets.Reset();
    }

    TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs&)
    {
        TSharedRef<SProbeWorkspace> Panel = SNew(SProbeWorkspace);
        Workspace = Panel;
        TSharedRef<SDockTab> Tab = SNew(SDockTab)
            .TabRole(ETabRole::NomadTab)
            [ Panel ];

        // UE 5.8 exposes the icon through SetTabIcon(), not the Slate constructor.
        // Keep the icon on the actual tab as well as its registered tab spawner.
        Tab->SetTabIcon(AssetStyle.IsValid()
            ? AssetStyle->GetBrush(TEXT("Probe.TabIcon"))
            : FAppStyle::GetBrush(TEXT("Icons.Search")));
        return Tab;
    }

    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
        // An explicitly labelled section keeps Probe out of adjacent PCG categories.
        FToolMenuSection& Section = Menu->FindOrAddSection(
            "ProbeTools", LOCTEXT("TestingMenuSection", "Testing"), FToolMenuInsert());
        Section.AddMenuEntry("OpenProbe", LOCTEXT("OpenProbe", "Probe"),
            LOCTEXT("OpenProbeTip", "Open Probe"), FSlateIcon(TEXT("ProbeAssetAppearance"), TEXT("Probe.TabIcon")),
            FUIAction(FExecuteAction::CreateLambda([]()
            {
                FGlobalTabmanager::Get()->TryInvokeTab(Probe::TabId);
            })));

        // Type-specific context menu: do not load test assets while the right-click
        // menu is being built. Only executable actions load their selected assets.
        if (UToolMenu* AssetMenu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UProbeTestAsset::StaticClass()))
        {
            FToolMenuSection& Actions = AssetMenu->FindOrAddSection(
                TEXT("ProbeTestActions"), LOCTEXT("ProbeAssetActions", "Probe"), FToolMenuInsert());
            Actions.AddDynamicEntry(TEXT("ProbeTestContextActions"),
                FNewToolMenuSectionDelegate::CreateRaw(this, &FProbeEditorModule::AddBrowserTestActions));
        }
    }
};

namespace Probe
{
    void OpenTestAssetsInWorkspace(const TArray<FString>& AssetObjectPaths)
    {
        FProbeEditorModule& Module = FModuleManager::LoadModuleChecked<FProbeEditorModule>(TEXT("ProbeEditor"));
        Module.OpenTestAssets(AssetObjectPaths);
    }
}

IMPLEMENT_MODULE(FProbeEditorModule, ProbeEditor)
#undef LOCTEXT_NAMESPACE
