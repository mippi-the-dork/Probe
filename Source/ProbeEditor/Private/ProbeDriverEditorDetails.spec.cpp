// Probe 0.15.3: first interaction with a *native Level Editor control*.
//
// This is an interactive Automation Driver spec, not a Probe Test Asset step.
// No actors, assets, selections or persistent settings are modified. The only
// temporary UI change is the search text in Details 1, restored in AfterEach.
// Driver's blocking methods run exclusively on a worker thread.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "IAutomationDriverModule.h"
#include "IAutomationDriver.h"
#include "AutomationDriverTypeDefs.h"
#include "IDriverElement.h"
#include "LocateBy.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "LevelEditor.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SSearchBox.h"
#include "Layout/ChildrenBase.h"
#include "InputCoreTypes.h"
#include "Modules/ModuleManager.h"

namespace ProbeEditorDetailsDriver
{
    static const TCHAR* TemporaryFilter = TEXT("Probe0153NoMatchingPropertiesTestFilter");

    // Inspect only the existing native widget hierarchy. We do not tag or
    // otherwise modify engine widgets to make them locatable by the driver.
    static void CollectSearchBoxes(const TSharedRef<SWidget>& Widget,
        TArray<TSharedRef<SSearchBox>>& OutSearchBoxes, int32 Depth = 0)
    {
        if (Depth > 32 || OutSearchBoxes.Num() > 4)
        {
            return;
        }
        if (Widget->GetTypeAsString() == TEXT("SSearchBox"))
        {
            OutSearchBoxes.Add(StaticCastSharedRef<SSearchBox>(Widget));
            return;
        }
        if (FChildren* Children = Widget->GetChildren())
        {
            const int32 Count = Children->Num();
            for (int32 Index = 0; Index < Count; ++Index)
            {
                CollectSearchBoxes(Children->GetChildAt(Index), OutSearchBoxes, Depth + 1);
                if (OutSearchBoxes.Num() > 4)
                {
                    return;
                }
            }
        }
    }
}

BEGIN_DEFINE_SPEC(FProbeDriverEditorDetailsSpec,
    "Probe.Driver.EditorDetailsSearch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    TSharedPtr<IDetailsView> DetailsView;
    TSharedPtr<SSearchBox> SearchBox;
    TWeakPtr<SWidget> PreviousKeyboardFocus;
    FText OriginalSearchText;
    TArray<TWeakObjectPtr<AActor>> OriginalSelectedActors;
    bool bReady = false;
    bool bDriverWasEnabled = false;
    bool bEnabledDriverForTest = false;
    bool bClickSucceeded = false;
    bool bSelectAllSucceeded = false;
    bool bTypeSucceeded = false;
END_DEFINE_SPEC(FProbeDriverEditorDetailsSpec)

void FProbeDriverEditorDetailsSpec::Define()
{
    Describe("Native Level Editor Details panel", [this]()
    {
        BeforeEach([this]()
        {
            bReady = false;
            bEnabledDriverForTest = false;
            bClickSucceeded = false;
            bSelectAllSucceeded = false;
            bTypeSucceeded = false;
            SearchBox.Reset();
            DetailsView.Reset();
            OriginalSearchText = FText::GetEmpty();
            OriginalSelectedActors.Reset();
            PreviousKeyboardFocus.Reset();

            // Do not spawn a new panel or change the user's Editor layout.
            // Details 1 must already be open and visible for interactive UI QA.
            FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
            DetailsView = PropertyEditor.FindDetailView(LevelEditorTabIds::LevelEditorSelectionDetails);
            if (!DetailsView.IsValid())
            {
                AddError(TEXT("Level Editor Details 1 is not available. Open Window > Details > Details 1, then rerun the UI test."));
                return;
            }

            TArray<TSharedRef<SSearchBox>> SearchBoxes;
            const TSharedPtr<SWidget> FilterArea = DetailsView->GetFilterAreaWidget();
            if (FilterArea.IsValid())
            {
                ProbeEditorDetailsDriver::CollectSearchBoxes(FilterArea.ToSharedRef(), SearchBoxes);
            }
            // Some layouts insert a wrapper around the filter area; fall back
            // to searching this one Details view only, never the entire Editor.
            if (SearchBoxes.Num() == 0)
            {
                ProbeEditorDetailsDriver::CollectSearchBoxes(StaticCastSharedRef<SWidget>(DetailsView.ToSharedRef()), SearchBoxes);
            }
            if (SearchBoxes.Num() != 1)
            {
                AddError(FString::Printf(TEXT("Expected one native Details search box; found %d. Check the visible Details panel and UE 5.8 widget layout."), SearchBoxes.Num()));
                return;
            }

            SearchBox = SearchBoxes[0];
            if (!SearchBox->GetVisibility().IsVisible() || !SearchBox->IsEnabled())
            {
                AddError(TEXT("Details search exists but is not visible/enabled. Make Details 1 visible and rerun."));
                return;
            }
            OriginalSearchText = SearchBox->GetText();
            OriginalSelectedActors = DetailsView->GetSelectedActors();
            PreviousKeyboardFocus = FSlateApplication::Get().GetKeyboardFocusedWidget();
            bDriverWasEnabled = IAutomationDriverModule::Get().IsEnabled();
            if (!bDriverWasEnabled)
            {
                IAutomationDriverModule::Get().Enable();
                bEnabledDriverForTest = true;
            }
            bReady = true;
        });

        // Driver->Click / TypeChord / Type wait on the worker thread while the
        // game thread services the actual Slate input. Never invoke on GT.
        It("types a temporary filter and restores the existing search", EAsyncExecution::ThreadPool, [this]()
        {
            if (!bReady)
            {
                return;
            }
            const FAutomationDriverRef Driver = IAutomationDriverModule::Get().CreateDriver();
            const FDriverElementRef Search = Driver->FindElement(By::WidgetLambda(
                [this](TArray<TSharedRef<SWidget>>& OutWidgets)
                {
                    // AutomationDriver invokes this locator on the game thread.
                    // Do not copy Slate shared references on this worker thread.
                    if (SearchBox.IsValid())
                    {
                        OutWidgets.Add(StaticCastSharedRef<SWidget>(SearchBox.ToSharedRef()));
                    }
                }, TEXT("Probe.NativeLevelEditor.DetailsSearch")));
            bClickSucceeded = Search->Click();
            if (bClickSucceeded)
            {
                bSelectAllSucceeded = Search->TypeChord(EKeys::LeftControl, EKeys::A);
            }
            if (bSelectAllSucceeded)
            {
                bTypeSucceeded = Search->Type(ProbeEditorDetailsDriver::TemporaryFilter);
            }
        });

        AfterEach([this]()
        {
            if (bReady)
            {
                // Verify actual effect (not just driver acceptance) before
                // restoring the user's text. The temporary filter is unique.
                TestTrue(TEXT("Automation Driver clicked the native Details search"), bClickSucceeded);
                TestTrue(TEXT("Automation Driver selected the existing text"), bSelectAllSucceeded);
                TestTrue(TEXT("Automation Driver typed into the native widget"), bTypeSucceeded);
                if (SearchBox.IsValid() && DetailsView.IsValid())
                {
                    const FString Actual = SearchBox->GetText().ToString();
                    TestEqual(TEXT("Native Details search contains the input typed through Slate"),
                        Actual, FString(ProbeEditorDetailsDriver::TemporaryFilter));
                    TestTrue(TEXT("Native Details view applied the temporary filter"), DetailsView->HasActiveSearch());

                    // Cleanup is unconditional, even when a check failed.
                    SearchBox->SetText(OriginalSearchText);
                    DetailsView->SetSearchText(OriginalSearchText);
                    TestEqual(TEXT("Native Details search text restored"),
                        SearchBox->GetText().ToString(), OriginalSearchText.ToString());
                    TestEqual(TEXT("Native Details search active state restored"),
                        DetailsView->HasActiveSearch(), !OriginalSearchText.IsEmpty());
                    const TArray<TWeakObjectPtr<AActor>>& AfterActors = DetailsView->GetSelectedActors();
                    bool bSelectionPreserved = AfterActors.Num() == OriginalSelectedActors.Num();
                    if (bSelectionPreserved)
                    {
                        for (int32 Index = 0; Index < AfterActors.Num(); ++Index)
                        {
                            if (AfterActors[Index] != OriginalSelectedActors[Index])
                            {
                                bSelectionPreserved = false;
                                break;
                            }
                        }
                    }
                    TestTrue(TEXT("Details selection preserved"), bSelectionPreserved);
                }
                if (TSharedPtr<SWidget> PriorFocus = PreviousKeyboardFocus.Pin())
                {
                    FSlateApplication::Get().SetKeyboardFocus(PriorFocus.ToSharedRef(), EFocusCause::SetDirectly);
                }
            }
            if (bEnabledDriverForTest)
            {
                IAutomationDriverModule::Get().Disable();
            }
            DetailsView.Reset();
            SearchBox.Reset();
            PreviousKeyboardFocus.Reset();
            OriginalSelectedActors.Reset();
        });
    });
}

#endif // WITH_DEV_AUTOMATION_TESTS
