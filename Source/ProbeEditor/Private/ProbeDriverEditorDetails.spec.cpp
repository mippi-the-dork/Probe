// Probe 0.15.3 hotfix 2: stabilize the real native Details filter test.
//
// This is an interactive Automation Driver spec, not a Probe Test Asset step.
// No actors, assets, selections or persistent settings are modified. The only
// temporary UI changes are the active Details 1 tab, its search text,
// and (when nothing is selected) a transient Probe object shown in the view.
// All are restored in AfterEach, including failure paths.
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
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Layout/WidgetPath.h"
#include "Widgets/Input/SSearchBox.h"
#include "Layout/ChildrenBase.h"
#include "InputCoreTypes.h"
#include "Modules/ModuleManager.h"
#include "ProbeTestAsset.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"

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
    TWeakPtr<SDockTab> PreviousActiveTab;
    TWeakPtr<SDockTab> DetailsTab;
    bool bActivatedDetailsTab = false;
    bool bSearchInVisibleSlatePathOnSetup = false;
    FText OriginalSearchText;
    TArray<TWeakObjectPtr<AActor>> OriginalSelectedActors;
    TArray<TWeakObjectPtr<UObject>> OriginalSelectedObjects;
    TStrongObjectPtr<UProbeTestAsset> TemporaryDetailsObject;
    bool bInstalledTemporaryObject = false;
    int32 UnfilteredRowCount = -1;
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
            OriginalSelectedObjects.Reset();
            TemporaryDetailsObject.Reset();
            bInstalledTemporaryObject = false;
            UnfilteredRowCount = -1;
            PreviousKeyboardFocus.Reset();
            PreviousActiveTab.Reset();
            DetailsTab.Reset();
            bActivatedDetailsTab = false;
            bSearchInVisibleSlatePathOnSetup = false;

            // Do not spawn a new panel or change the user's Editor layout.
            // Details 1 must already be open and visible for interactive UI QA.
            FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
            DetailsView = PropertyEditor.FindDetailView(LevelEditorTabIds::LevelEditorSelectionDetails);
            if (!DetailsView.IsValid())
            {
                AddError(TEXT("Level Editor Details 1 is not available. Open Window > Details > Details 1, then rerun the UI test."));
                return;
            }

            // Make the test independent of an actor already being selected.
            // SetObjects only changes which object this *existing* Details
            // view displays. No level actor or on-disk asset is created.
            OriginalSelectedActors = DetailsView->GetSelectedActors();
            OriginalSelectedObjects = DetailsView->GetSelectedObjects();
            if (OriginalSelectedObjects.IsEmpty())
            {
                TemporaryDetailsObject.Reset(NewObject<UProbeTestAsset>(
                    GetTransientPackage(), UProbeTestAsset::StaticClass(), NAME_None, RF_Transient));
                if (!TemporaryDetailsObject.IsValid())
                {
                    AddError(TEXT("Could not create the temporary Details UI test object."));
                    return;
                }
                TArray<UObject*> TestObjects;
                TestObjects.Add(TemporaryDetailsObject.Get());
                DetailsView->SetObjects(TestObjects, true, true);
                bInstalledTemporaryObject = true;
                UE_LOG(LogTemp, Display,
                    TEXT("Probe Details UI test: no objects were selected; installed a transient in-memory Probe test object in Details 1. Its original objects will be restored."));
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

            // An SSearchBox can exist inside a docked tab without appearing
            // in Slate's *arranged* widget hierarchy. This is common when
            // running from the Output Log with Details 1 in a background tab.
            // Bring the *existing* tab forward. Never spawn a replacement tab
            // or modify the user's docking layout.
            const TSharedRef<FGlobalTabmanager> GlobalTabManager = FGlobalTabmanager::Get();
            // Level Editor details tabs are commonly registered under a
            // *local* tab manager, not the global tab manager. Looking in
            // the latter alone generated a misleading warning in hotfix 1.
            TSharedPtr<FTabManager> DetailsTabManager = DetailsView->GetHostTabManager();
            TSharedPtr<SDockTab> ExistingDetailsTab;
            if (DetailsTabManager.IsValid())
            {
                ExistingDetailsTab = DetailsTabManager->FindExistingLiveTab(
                    FTabId(LevelEditorTabIds::LevelEditorSelectionDetails));
            }
            if (!ExistingDetailsTab.IsValid())
            {
                ExistingDetailsTab = GlobalTabManager->FindExistingLiveTab(
                    FTabId(LevelEditorTabIds::LevelEditorSelectionDetails));
            }
            if (ExistingDetailsTab.IsValid())
            {
                DetailsTab = ExistingDetailsTab;
                if (!ExistingDetailsTab->IsForeground())
                {
                    PreviousActiveTab = DetailsTabManager.IsValid()
                        ? DetailsTabManager->GetActiveTab() : GlobalTabManager->GetActiveTab();
                    ExistingDetailsTab->ActivateInParent(ETabActivationCause::SetDirectly);
                    if (TSharedPtr<SWindow> DetailsWindow = ExistingDetailsTab->GetParentWindow())
                    {
                        DetailsWindow->BringToFront(false);
                    }
                    bActivatedDetailsTab = true;
                    UE_LOG(LogTemp, Display, TEXT("Probe Details UI test: activated existing Details 1 tab so Slate can arrange its search widget."));
                }
            }
            else
            {
                UE_LOG(LogTemp, Display, TEXT("Probe Details UI test: no matching Details 1 tab was returned by local/global tab managers. Using Slate's actual visible search-widget path to determine interactability."));
            }

            FWidgetPath SearchWidgetPath;
            bSearchInVisibleSlatePathOnSetup = FSlateApplication::Get().FindPathToWidget(
                SearchBox.ToSharedRef(), SearchWidgetPath, EVisibility::Visible);
            if (!bSearchInVisibleSlatePathOnSetup)
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("Probe Details UI test: native search exists but is not in an arranged visible Slate widget path yet. This can occur when Details 1 is hidden, has no selected object, or has not repainted."));
            }

            if (!SearchBox->GetVisibility().IsVisible() || !SearchBox->IsEnabled())
            {
                AddError(TEXT("Details search exists but is not visible/enabled. Make Details 1 visible and rerun."));
                return;
            }
            OriginalSearchText = SearchBox->GetText();
            PreviousKeyboardFocus = FSlateApplication::Get().GetKeyboardFocusedWidget();
            // Start from a known empty search, even if the user previously
            // had a filter. Otherwise HasActiveSearch could already be true
            // and could not independently confirm the new filter took effect.
            DetailsView->SetSearchText(FText::GetEmpty());
            UnfilteredRowCount = DetailsView->CountRows();
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
            // Allow at least one Editor tick after foregrounding a previously
            // hidden tab. The driver still uses its normal implicit wait.
            Driver->Wait(FTimespan::FromMilliseconds(150));
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
                if (bTypeSucceeded)
                {
                    // SSearchBox can debounce OnTextChanged while typing.
                    // The native field can contain the input before the
                    // Details view's filter has processed it. Wait on the
                    // worker thread so Slate keeps ticking on the game thread.
                    Driver->Wait(FTimespan::FromMilliseconds(1600));
                }
            }
        });

        AfterEach([this]()
        {
            if (bReady)
            {
                // Verify actual effect (not just driver acceptance) before
                // restoring the user's text. The temporary filter is unique.
                if (!bClickSucceeded)
                {
                    // The driver already reports the failed locator. Report
                    // *why* it failed instead of 5 redundant downstream
                    // assertions that are impossible without the click.
                    FWidgetPath VisiblePath;
                    const bool bReachableNow = SearchBox.IsValid() &&
                        FSlateApplication::Get().FindPathToWidget(
                            SearchBox.ToSharedRef(), VisiblePath, EVisibility::Visible);
                    AddError(FString::Printf(
                        TEXT("Probe Details UI precondition failed: native search exists but driver cannot click it. Visible Slate path at setup=%s, after click=%s, Details 1 tab found=%s. Keep Details 1 visible and the Level Editor foreground; with no user selection Probe temporarily displays a transient test object. The test will not claim success without an actual click."),
                        bSearchInVisibleSlatePathOnSetup ? TEXT("yes") : TEXT("no"),
                        bReachableNow ? TEXT("yes") : TEXT("no"),
                        DetailsTab.IsValid() ? TEXT("yes") : TEXT("no")));
                }
                else
                {
                    TestTrue(TEXT("Automation Driver selected the existing text"), bSelectAllSucceeded);
                    if (bSelectAllSucceeded)
                    {
                        TestTrue(TEXT("Automation Driver typed into the native widget"), bTypeSucceeded);
                    }
                    if (bTypeSucceeded && SearchBox.IsValid() && DetailsView.IsValid())
                    {
                        const FString Actual = SearchBox->GetText().ToString();
                        TestEqual(TEXT("Native Details search contains the input typed through Slate"),
                            Actual, FString(ProbeEditorDetailsDriver::TemporaryFilter));
                        const bool bFilterActive = DetailsView->HasActiveSearch();
                        const int32 FilteredRowCount = DetailsView->CountRows();
                        if (!bFilterActive)
                        {
                            UE_LOG(LogTemp, Warning,
                                TEXT("Probe Details UI test: driver typed exact input but Details view did not activate the filter after 1600 ms. Rows before=%d, after=%d, displayed objects=%d, temporary object=%s. The native OnTextChanged binding or debounce must be investigated."),
                                UnfilteredRowCount, FilteredRowCount,
                                DetailsView->GetSelectedObjects().Num(),
                                bInstalledTemporaryObject ? TEXT("yes") : TEXT("no"));
                        }
                        TestTrue(TEXT("Native Details view applied the temporary filter after text-change debounce"), bFilterActive);
                    }
                }
                if (SearchBox.IsValid() && DetailsView.IsValid())
                {
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
            }
            // Undo temporary object display even if setup failed before
            // the Driver was enabled. This must occur before releasing the
            // transient object, otherwise Details could hold a stale pointer.
            if (bInstalledTemporaryObject && DetailsView.IsValid())
            {
                DetailsView->SetObjects(OriginalSelectedObjects, true, true);
                TestEqual(TEXT("Original Details object count restored"),
                    DetailsView->GetSelectedObjects().Num(), OriginalSelectedObjects.Num());
            }
            TemporaryDetailsObject.Reset();
            bInstalledTemporaryObject = false;
            OriginalSelectedObjects.Reset();
            if (bEnabledDriverForTest)
            {
                IAutomationDriverModule::Get().Disable();
            }
            // Restore the tab that was previously active if we changed it.
            // Do this after disabling the driver and restoring the text.
            if (bActivatedDetailsTab)
            {
                if (TSharedPtr<SDockTab> PreviousTab = PreviousActiveTab.Pin())
                {
                    PreviousTab->ActivateInParent(ETabActivationCause::SetDirectly);
                }
            }
            PreviousActiveTab.Reset();
            DetailsTab.Reset();
            // Keyboard focus restoration was previously inside a branch
            // following bReady=false, so it was never executed.
            if (TSharedPtr<SWidget> PriorFocus = PreviousKeyboardFocus.Pin())
            {
                FWidgetPath FocusPath;
                if (FSlateApplication::Get().FindPathToWidget(
                        PriorFocus.ToSharedRef(), FocusPath, EVisibility::Visible))
                {
                    FSlateApplication::Get().SetKeyboardFocus(
                        PriorFocus.ToSharedRef(), EFocusCause::SetDirectly);
                }
            }
            DetailsView.Reset();
            SearchBox.Reset();
            PreviousKeyboardFocus.Reset();
            OriginalSelectedActors.Reset();
        });
    });
}

#endif // WITH_DEV_AUTOMATION_TESTS
