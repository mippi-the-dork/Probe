// Probe 0.15.4: independent Automation Framework proof that real Probe-authored
// Test Asset steps use Automation Driver against the native Editor Details UI.
// A native spec checks the Probe result; a green Probe result alone is insufficient.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "ProbeDetailsSearchRestore.h"
#include "ProbeTestRunner.h"
#include "ProbeTestAsset.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "LevelEditor.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SWindow.h"
#include "Layout/ChildrenBase.h"
#include "Layout/WidgetPath.h"
#include "Widgets/Docking/SDockTab.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/Package.h"
#include "Async/Async.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

namespace ProbeAuthoredDetails
{
    struct FObservedCompletion
    {
        TAtomic<bool> Completed{false};
        Probe::FTestResult Result;
    };

    static void CollectSearch(const TSharedRef<SWidget>& Widget, TArray<TSharedRef<SSearchBox>>& Out, int32 Depth = 0)
    {
        if (Depth > 32 || Out.Num() > 4) { return; }
        if (Widget->GetTypeAsString() == TEXT("SSearchBox"))
        {
            Out.Add(StaticCastSharedRef<SSearchBox>(Widget)); return;
        }
        if (FChildren* Children = Widget->GetChildren())
        {
            for (int32 Index = 0; Index < Children->Num(); ++Index)
            { CollectSearch(Children->GetChildAt(Index), Out, Depth + 1); }
        }
    }
}

BEGIN_DEFINE_SPEC(FProbeDriverAuthoredDetailsSpec,
    "Probe.Driver.AuthoredDetailsSearch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
    TSharedPtr<IDetailsView> View;
    TSharedPtr<SSearchBox> Search;
    TSharedPtr<ProbeAuthoredDetails::FObservedCompletion> Completion;
    TArray<TWeakObjectPtr<UObject>> OldObjects;
    TWeakPtr<SWidget> OldFocus;
    FText OldSearchText;
    bool bOriginalActive = false;
    TStrongObjectPtr<UProbeTestAsset> TemporaryObject;
    TStrongObjectPtr<UProbeTestAsset> AuthoredTest;
    bool bChangedObjects = false;
    bool bReady = false;
    TAtomic<bool> bTimedOut{false};
END_DEFINE_SPEC(FProbeDriverAuthoredDetailsSpec)

void FProbeDriverAuthoredDetailsSpec::Define()
{
    Describe("Native Details input through an authorable Probe Test", [this]()
    {
        BeforeEach([this]()
        {
            bReady = false;
            bTimedOut.Store(false);
            Completion = MakeShared<ProbeAuthoredDetails::FObservedCompletion>();
            bChangedObjects = false;
            View.Reset();
            Search.Reset();
            OldObjects.Reset();
            TemporaryObject.Reset();
            AuthoredTest.Reset();
            OldFocus.Reset();
            OldSearchText = FText::GetEmpty();
            bOriginalActive = false;

            FPropertyEditorModule& Module = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
            View = Module.FindDetailView(LevelEditorTabIds::LevelEditorSelectionDetails);
            if (!View.IsValid())
            {
                AddError(TEXT("Details 1 is not open. Open Window > Details > Details 1.")); return;
            }
            OldObjects = View->GetSelectedObjects();
            OldFocus = FSlateApplication::Get().GetKeyboardFocusedWidget();
            // We read the existing search text from the view's actual filter
            // before creating any test object. The runner restores the box.
            if (OldObjects.IsEmpty())
            {
                TemporaryObject.Reset(NewObject<UProbeTestAsset>(
                    GetTransientPackage(), UProbeTestAsset::StaticClass(), NAME_None, RF_Transient));
                if (!TemporaryObject.IsValid())
                {
                    AddError(TEXT("Cannot construct temporary in-memory Details object.")); return;
                }
                TArray<UObject*> Objects;
                Objects.Add(TemporaryObject.Get());
                View->SetObjects(Objects, true, true);
                bChangedObjects = true;
            }
            TSharedPtr<SDockTab> Tab;
            if (TSharedPtr<FTabManager> Manager = View->GetHostTabManager())
            {
                Tab = Manager->FindExistingLiveTab(FTabId(LevelEditorTabIds::LevelEditorSelectionDetails));
            }
            if (!Tab.IsValid())
            {
                Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(
                    FTabId(LevelEditorTabIds::LevelEditorSelectionDetails));
            }
            if (Tab.IsValid() && !Tab->IsForeground())
            {
                Tab->ActivateInParent(ETabActivationCause::SetDirectly);
                if (TSharedPtr<SWindow> Window = Tab->GetParentWindow()) { Window->BringToFront(false); }
            }
            TArray<TSharedRef<SSearchBox>> Searches;
            if (TSharedPtr<SWidget> Filter = View->GetFilterAreaWidget())
            { ProbeAuthoredDetails::CollectSearch(Filter.ToSharedRef(), Searches); }
            if (Searches.Num() == 0)
            { ProbeAuthoredDetails::CollectSearch(StaticCastSharedRef<SWidget>(View.ToSharedRef()), Searches); }
            if (Searches.Num() != 1)
            {
                AddError(TEXT("The native Details 1 search widget is missing or ambiguous.")); return;
            }
            Search = Searches[0];
            OldSearchText = Search->GetText();
            bOriginalActive = View->HasActiveSearch();

            AuthoredTest.Reset(NewObject<UProbeTestAsset>(
                GetTransientPackage(), UProbeTestAsset::StaticClass(), NAME_None, RF_Transient));
            if (!AuthoredTest.IsValid())
            {
                AddError(TEXT("Cannot create transient Probe-authored test.")); return;
            }
            AuthoredTest->bAllowEditorUIInput = true;
            const auto AddStep = [this](EProbeStepType Type, const FString& Target, const FString& Expected)
            {
                FProbeTestStep Step;
                Step.Type = Type;
                Step.Target = Target;
                Step.ExpectedValue = Expected;
                AuthoredTest->Steps.Add(Step);
            };
            const FString Text(TEXT("Probe0154NativeAuthoredFilter"));
            AddStep(EProbeStepType::UIDetailsSearchVisible, TEXT(""), TEXT(""));
            AddStep(EProbeStepType::UIWidgetTypeText, TEXT("details:search"), Text);
            AddStep(EProbeStepType::UIDetailsSearchTextEquals, TEXT(""), Text);
            AddStep(EProbeStepType::UIDetailsSearchTextContains, TEXT(""), TEXT("NativeAuthored"));
            AddStep(EProbeStepType::UIDetailsSearchTextNotEquals, TEXT(""), TEXT("UnexpectedSearchText"));
            AddStep(EProbeStepType::UIDetailsSearchFilterActive, TEXT(""), TEXT(""));
            bReady = true;
        });

        It("runs real authorable UI steps and restores native Details search", EAsyncExecution::ThreadPool, [this]()
        {
            if (!bReady) { return; }
            TSharedRef<ProbeAuthoredDetails::FObservedCompletion> State = Completion.ToSharedRef();
            UProbeTestAsset* TestPtr = AuthoredTest.Get();
            AsyncTask(ENamedThreads::GameThread, [State, TestPtr]()
            {
                Probe::RunTestAsync(TestPtr, [State](Probe::FTestResult Result)
                {
                    State->Result = MoveTemp(Result);
                    State->Completed.Store(true);
                });
            });
            // Never park the Editor game thread. Wait on this worker so Slate
            // input, debounce timers, restoration and async completion can tick.
            const double Deadline = FPlatformTime::Seconds() + 25.0;
            while (!State->Completed.Load() && FPlatformTime::Seconds() < Deadline)
            {
                FPlatformProcess::Sleep(0.05f);
            }
            if (!State->Completed.Load()) { bTimedOut.Store(true); }
        });

        AfterEach([this]()
        {
            if (bReady)
            {
                TestFalse(TEXT("Authored Details run completed within deadline"), bTimedOut.Load());
                if (Completion.IsValid() && Completion->Completed.Load())
                {
                    const Probe::FTestResult& ObservedResult = Completion->Result;
                    TestEqual(TEXT("Probe authored test status"), static_cast<int32>(ObservedResult.Status),
                        static_cast<int32>(Probe::EResultStatus::Passed));
                    TestEqual(TEXT("Every intended UI step executed"), ObservedResult.Steps.Num(), 6);
                    for (const Probe::FStepResult& Step : ObservedResult.Steps)
                    {
                        TestTrue(TEXT("Individual authored UI step passed"), Step.Status == Probe::EResultStatus::Passed);
                    }
                    TestEqual(TEXT("Original Details search text was restored"),
                        Search->GetText().ToString(), OldSearchText.ToString());
                    TestEqual(TEXT("Original Details active filter state restored"),
                        View->HasActiveSearch(), bOriginalActive);
                }
            }
            // Even on timeout do not leave an injected object displayed.
            if (View.IsValid())
            {
                if (bChangedObjects) { View->SetObjects(OldObjects, true, true); }
                ProbeDetailsSearchRestore::Restore(View, Search, OldSearchText);
            }
            if (TSharedPtr<SWidget> Focus = OldFocus.Pin())
            {
                FWidgetPath FocusPath;
                if (FSlateApplication::Get().FindPathToWidget(Focus.ToSharedRef(), FocusPath, EVisibility::Visible))
                { FSlateApplication::Get().SetKeyboardFocus(Focus.ToSharedRef(), EFocusCause::SetDirectly); }
            }
            AuthoredTest.Reset();
            TemporaryObject.Reset();
        });
    });
}

#endif // WITH_DEV_AUTOMATION_TESTS
