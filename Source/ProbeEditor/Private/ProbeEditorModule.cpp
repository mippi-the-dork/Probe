#include "ProbeTestAsset.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ToolMenus.h"
#include "LevelEditor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/DataAssetFactory.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Misc/PackageName.h"
#include "HAL/PlatformTime.h"

#define LOCTEXT_NAMESPACE "ProbeEditor"

namespace Probe
{
    static const FName TabId(TEXT("ProbeWorkspace"));

    struct FTestResult
    {
        FString Name;
        FString Id;
        FString Status;
        FString Detail;
        double DurationMs = 0.0;
    };

    static bool ExecuteStep(const FProbeTestStep& Step, FString& Reason)
    {
        switch (Step.Type)
        {
        case EProbeStepType::PluginEnabled:
        {
            const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Step.Target);
            const bool bPass = Plugin.IsValid() && Plugin->IsEnabled();
            Reason = bPass ? TEXT("Plugin enabled") : FString::Printf(TEXT("Plugin missing or disabled: %s"), *Step.Target);
            return bPass;
        }
        case EProbeStepType::ClassExists:
        {
            if (Step.Target.IsEmpty()) { Reason = TEXT("Class target is empty"); return false; }
            UClass* Found = FindObject<UClass>(nullptr, *Step.Target);
            if (!Found) { Found = LoadObject<UClass>(nullptr, *Step.Target); }
            if (!Found)
            {
                for (TObjectIterator<UClass> It; It; ++It)
                {
                    if (It->GetName() == Step.Target) { Found = *It; break; }
                }
            }
            Reason = Found ? TEXT("Class found") : FString::Printf(TEXT("Class not found: %s"), *Step.Target);
            return Found != nullptr;
        }
        case EProbeStepType::AssetExists:
        {
            if (Step.Target.IsEmpty()) { Reason = TEXT("Asset path is empty"); return false; }
            const FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            const FAssetData Asset = Registry.Get().GetAssetByObjectPath(FSoftObjectPath(Step.Target));
            const bool bPass = Asset.IsValid();
            Reason = bPass ? TEXT("Asset found") : FString::Printf(TEXT("Asset not found: %s"), *Step.Target);
            return bPass;
        }
        case EProbeStepType::EditorWorldExists:
        {
            const bool bPass = GEditor && GEditor->GetEditorWorldContext().World() != nullptr;
            Reason = bPass ? TEXT("Editor world available") : TEXT("No Editor world available");
            return bPass;
        }
        case EProbeStepType::ActorCountAtLeast:
        {
            UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
            if (!World) { Reason = TEXT("No Editor world available"); return false; }
            int32 Count = 0;
            for (TActorIterator<AActor> It(World); It; ++It) { ++Count; }
            const bool bPass = Count >= Step.MinimumActorCount;
            Reason = FString::Printf(TEXT("Actor count: %d (minimum: %d)"), Count, Step.MinimumActorCount);
            return bPass;
        }
        }
        Reason = TEXT("Unsupported step type");
        return false;
    }
}

class SProbeWorkspace : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SProbeWorkspace) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
        FDetailsViewArgs DetailsArgs;
        DetailsArgs.bAllowSearch = true;
        DetailsArgs.bHideSelectionTip = true;
        DetailsArgs.bUpdatesFromSelection = false;
        Details = PropertyModule.CreateDetailView(DetailsArgs);

        RefreshAssets();
        ChildSlot
        [
            SNew(SBorder).Padding(10)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
                [ SNew(STextBlock).Text(LOCTEXT("Heading", "PROBE  |  Editor QA Workspace")) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,8)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)
                    [ SNew(SButton).Text(LOCTEXT("Create", "New Test Asset")).OnClicked(this, &SProbeWorkspace::CreateAsset) ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)
                    [ SNew(SButton).Text(LOCTEXT("Refresh", "Refresh Tests")).OnClicked(this, &SProbeWorkspace::RefreshClicked) ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)
                    [ SNew(SButton).Text(LOCTEXT("RunSelected", "Run Selected")).OnClicked(this, &SProbeWorkspace::RunSelected) ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [ SNew(SButton).Text(LOCTEXT("RunAll", "Run All")).OnClicked(this, &SProbeWorkspace::RunAll) ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
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
                + SVerticalBox::Slot().FillHeight(0.60f).Padding(0,0,0,6)
                [ Details.ToSharedRef() ]
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(SSeparator) ]
                + SVerticalBox::Slot().AutoHeight().Padding(0,6)
                [ SNew(STextBlock).Text(LOCTEXT("Results", "Run Results (current session)")) ]
                + SVerticalBox::Slot().FillHeight(0.40f)
                [
                    SNew(SScrollBox)
                    + SScrollBox::Slot()
                    [ SAssignNew(ResultText, STextBlock).AutoWrapText(true).Text(LOCTEXT("Empty", "No tests run yet.")) ]
                ]
            ]
        ];
    }

private:
    TArray<TSharedPtr<FAssetData>> Assets;
    TSharedPtr<FAssetData> Selected;
    TSharedPtr<SComboBox<TSharedPtr<FAssetData>>> TestSelector;
    TSharedPtr<IDetailsView> Details;
    TSharedPtr<STextBlock> ResultText;
    TArray<Probe::FTestResult> Results;

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
        UpdateDetails();
    }

    void UpdateDetails()
    {
        if (Details.IsValid())
        {
            Details->SetObject(Selected.IsValid() ? Cast<UProbeTestAsset>(Selected->GetAsset()) : nullptr);
        }
    }

    FText SelectedName() const
    {
        return Selected.IsValid() ? FText::FromName(Selected->AssetName) : LOCTEXT("None", "No Probe test assets found");
    }

    void OnSelectionChanged(TSharedPtr<FAssetData> Item, ESelectInfo::Type)
    {
        Selected = Item;
        UpdateDetails();
    }

    FReply RefreshClicked() { RefreshAssets(); return FReply::Handled(); }

    FReply CreateAsset()
    {
        UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
        Factory->DataAssetClass = UProbeTestAsset::StaticClass();
        FAssetToolsModule& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
        UObject* Created = Tools.Get().CreateAssetWithDialog(TEXT("Probe_Test"), TEXT("/Game/ProbeTests"), UProbeTestAsset::StaticClass(), Factory);
        RefreshAssets();
        if (Created)
        {
            for (const TSharedPtr<FAssetData>& Item : Assets)
            {
                if (Item.IsValid() && Item->GetAsset() == Created)
                {
                    TestSelector->SetSelectedItem(Item);
                    break;
                }
            }
        }
        return FReply::Handled();
    }

    void ExecuteAsset(UProbeTestAsset* Asset)
    {
        if (!Asset) { return; }
        Probe::FTestResult Result;
        Result.Name = Asset->GetName();
        Result.Id = Asset->TestId.ToString();
        const double Start = FPlatformTime::Seconds();
        if (!Asset->bEnabled)
        {
            Result.Status = TEXT("SKIPPED");
            Result.Detail = TEXT("Test disabled");
        }
        else if (Asset->Steps.IsEmpty())
        {
            Result.Status = TEXT("ERROR");
            Result.Detail = TEXT("Test has no steps");
        }
        else
        {
            Result.Status = TEXT("PASSED");
            for (int32 Index = 0; Index < Asset->Steps.Num(); ++Index)
            {
                FString Reason;
                if (!Probe::ExecuteStep(Asset->Steps[Index], Reason))
                {
                    Result.Status = TEXT("FAILED");
                    Result.Detail = FString::Printf(TEXT("Step %d: %s"), Index + 1, *Reason);
                    break;
                }
            }
            if (Result.Status == TEXT("PASSED"))
            {
                Result.Detail = FString::Printf(TEXT("%d steps passed"), Asset->Steps.Num());
            }
        }
        Result.DurationMs = (FPlatformTime::Seconds() - Start) * 1000.0;
        Results.Add(MoveTemp(Result));
    }

    void ShowResults()
    {
        FString Text;
        int32 Passed = 0, Failed = 0, Skipped = 0, Errors = 0;
        for (const Probe::FTestResult& Result : Results)
        {
            Passed += Result.Status == TEXT("PASSED");
            Failed += Result.Status == TEXT("FAILED");
            Skipped += Result.Status == TEXT("SKIPPED");
            Errors += Result.Status == TEXT("ERROR");
        }
        Text += FString::Printf(TEXT("Passed: %d   Failed: %d   Skipped: %d   Errors: %d\n\n"), Passed, Failed, Skipped, Errors);
        for (const Probe::FTestResult& Result : Results)
        {
            Text += FString::Printf(TEXT("[%s] %s  (%.2f ms)\n%s\nID: %s\n\n"), *Result.Status, *Result.Name, Result.DurationMs, *Result.Detail, *Result.Id);
        }
        ResultText->SetText(FText::FromString(Text));
    }

    FReply RunSelected()
    {
        Results.Reset();
        if (Selected.IsValid()) { ExecuteAsset(Cast<UProbeTestAsset>(Selected->GetAsset())); }
        ShowResults();
        return FReply::Handled();
    }

    FReply RunAll()
    {
        RefreshAssets();
        Results.Reset();
        for (const TSharedPtr<FAssetData>& Entry : Assets)
        {
            if (Entry.IsValid()) { ExecuteAsset(Cast<UProbeTestAsset>(Entry->GetAsset())); }
        }
        ShowResults();
        return FReply::Handled();
    }
};

class FProbeEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(Probe::TabId,
            FOnSpawnTab::CreateRaw(this, &FProbeEditorModule::SpawnTab))
            .SetDisplayName(LOCTEXT("TabName", "Probe QA"))
            .SetMenuType(ETabSpawnerMenuType::Hidden);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FProbeEditorModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        if (UToolMenus::IsToolMenusAvailable())
        {
            UToolMenus::UnRegisterStartupCallback(this);
            UToolMenus::UnregisterOwner(this);
        }
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(Probe::TabId);
    }

private:
    TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs&)
    {
        return SNew(SDockTab).TabRole(ETabRole::NomadTab)
            [ SNew(SProbeWorkspace) ];
    }

    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
        FToolMenuSection& Section = Menu->FindOrAddSection("Probe");
        Section.AddMenuEntry("OpenProbe", LOCTEXT("OpenProbe", "Probe QA"),
            LOCTEXT("OpenProbeTip", "Open the Probe testing workspace"), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([]()
            {
                FGlobalTabmanager::Get()->TryInvokeTab(Probe::TabId);
            })));
    }
};

IMPLEMENT_MODULE(FProbeEditorModule, ProbeEditor)
#undef LOCTEXT_NAMESPACE
