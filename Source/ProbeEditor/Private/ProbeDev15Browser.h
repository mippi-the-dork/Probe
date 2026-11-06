#pragma once

#include "ProbeDev15Readiness.h"
#include "ProbeReferenceValidation.h"
#include "ProbeEditorNavigation.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/AppStyle.h"
#include "Modules/ModuleManager.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"

// Integrated native Slate browser. Discovery uses only Asset Registry metadata;
// preflight loading is explicit and never executes tests or modifies assets.
class SProbeDev15Browser : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SProbeDev15Browser) {} SLATE_END_ARGS()

    void Construct(const FArguments&)
    {
        ChildSlot
        [
            SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8)
            [ SNew(SVerticalBox)
              + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
              [ SNew(STextBlock)
                .Text(FText::FromString(TEXT("QA Browser | Project Test & Pack Readiness"))) ]
              + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
              [ SNew(STextBlock)
                .Text(FText::FromString(TEXT("Discovery is metadata-only. Scan preflights authoring without running tests; results are not saved to assets.")))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
              + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
              [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0,0,7,0)
                [ SNew(SSearchBox)
                  .HintText(FText::FromString(TEXT("Find by asset name, folder, path or diagnosis...")))
                  .OnTextChanged_Lambda([this](const FText& Value) { Search = Value.ToString(); RebuildRows(); }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,6,0)
                [ SNew(SComboButton).OnGetMenuContent(this, &SProbeDev15Browser::BuildKindMenu)
                  .ButtonContent()[ SNew(STextBlock).Text(this, &SProbeDev15Browser::KindLabel) ] ]
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SComboButton).OnGetMenuContent(this, &SProbeDev15Browser::BuildStateMenu)
                  .ButtonContent()[ SNew(STextBlock).Text(this, &SProbeDev15Browser::StateLabel) ] ] ]
              + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
              [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)
                [ SNew(SButton).ToolTipText(FText::FromString(TEXT("Rescan available assets from the Asset Registry. Retains previous inspection results for unchanged object paths.")))
                  .OnClicked_Lambda([this]() { RefreshAssets(); return FReply::Handled(); })
                  [ SNew(STextBlock).Text(FText::FromString(TEXT("Refresh Assets"))) ] ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)
                [ SNew(SButton).OnClicked_Lambda([this]() { Scan(false, false); return FReply::Handled(); })
                  .ToolTipText(FText::FromString(TEXT("Preflight entries currently matching the filters; does not execute tests.")))
                  [ SNew(STextBlock).Text(FText::FromString(TEXT("Scan Visible"))) ] ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)
                [ SNew(SButton).OnClicked_Lambda([this]() { Scan(true, false); return FReply::Handled(); })
                  .IsEnabled_Lambda([this]() { return !SelectedPaths.IsEmpty(); })
                  [ SNew(STextBlock).Text(FText::FromString(TEXT("Scan Selected"))) ] ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0)
                [ SNew(SButton).OnClicked_Lambda([this]() { Scan(false, true); return FReply::Handled(); })
                  .ToolTipText(FText::FromString(TEXT("Explicitly preflight every Probe Test and Pack in this project.")))
                  [ SNew(STextBlock).Text(FText::FromString(TEXT("Scan All"))) ] ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)
                [ SNew(SButton).OnClicked_Lambda([this]()
                  { for (const ProbeDev15::FEntry& Entry : Entries)
                    { if (Matches(Entry)) { SelectedPaths.Add(Entry.Path); } }
                    RebuildRows(); return FReply::Handled(); })
                  [ SNew(STextBlock).Text(FText::FromString(TEXT("Select Visible"))) ] ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0)
                [ SNew(SButton).OnClicked_Lambda([this]()
                  { SelectedPaths.Reset(); RebuildRows(); return FReply::Handled(); })
                  [ SNew(STextBlock).Text(FText::FromString(TEXT("Clear Selection"))) ] ]
                + SHorizontalBox::Slot().FillWidth(1.0f)[ SNullWidget::NullWidget ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0,0,5,0)
                [ SNew(SButton).OnClicked(this, &SProbeDev15Browser::CopyReport)
                  [ SNew(STextBlock).Text(FText::FromString(TEXT("Copy Report"))) ] ]
                + SHorizontalBox::Slot().AutoWidth()
                [ SNew(SButton).OnClicked(this, &SProbeDev15Browser::ExportReport)
                  [ SNew(STextBlock).Text(FText::FromString(TEXT("Export CSV"))) ] ] ]
              + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
              [ SNew(STextBlock).Text(this, &SProbeDev15Browser::SummaryText) ]
              + SVerticalBox::Slot().FillHeight(1.0f)
              [ SNew(SSplitter)
                + SSplitter::Slot().Value(0.58f)
                [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                  .Padding(5)[ SNew(SScrollBox)
                    + SScrollBox::Slot()[ SAssignNew(Rows, SVerticalBox) ] ] ]
                + SSplitter::Slot().Value(0.42f)
                [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                  .Padding(7)[ SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
                    [ SNew(STextBlock).Text(FText::FromString(TEXT("Inspection Details"))) ]
                    + SVerticalBox::Slot().FillHeight(1.0f)
                    [ SNew(SScrollBox) + SScrollBox::Slot()
                      [ SNew(STextBlock).Text(this, &SProbeDev15Browser::DetailsText).AutoWrapText(true) ] ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,8,0,0)
                    [ SNew(SButton).OnClicked(this, &SProbeDev15Browser::OpenActive)
                      .IsEnabled_Lambda([this]() { return Find(ActivePath) != nullptr; })
                      [ SNew(STextBlock).Text(FText::FromString(TEXT("Open in Probe Workspace"))) ] ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0,5,0,0)
                    [ SNew(SButton).OnClicked(this, &SProbeDev15Browser::RunReadySelection)
                      .ToolTipText(FText::FromString(TEXT("Run only explicitly scanned, Ready Test Assets that you selected. Skips Packs, disabled, blocked, warning and unscanned entries.")))
                      .IsEnabled_Lambda([this]() { return HasReadySelection(); })
                      [ SNew(STextBlock).Text(FText::FromString(TEXT("Run Selected Ready Tests"))) ] ] ] ] ]
              + SVerticalBox::Slot().AutoHeight().Padding(0,6,0,0)
              [ SNew(STextBlock).Text(this, &SProbeDev15Browser::NoticeText)
                .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
        ];
        RefreshAssets();
    }

private:
    TArray<ProbeDev15::FEntry> Entries;
    TSharedPtr<SVerticalBox> Rows;
    FString Search;
    FString ActivePath;
    TSet<FString> SelectedPaths;
    FString Notice = TEXT("Refresh to discover newly created assets. Scan to inspect readiness.");
    ProbeDev15::EKindFilter KindFilter = ProbeDev15::EKindFilter::All;
    ProbeDev15::EStateFilter StateFilter = ProbeDev15::EStateFilter::All;

    const ProbeDev15::FEntry* Find(const FString& Path) const
    {
        for (const ProbeDev15::FEntry& Entry : Entries)
        { if (Entry.Path == Path) { return &Entry; } }
        return nullptr;
    }
    bool Matches(const ProbeDev15::FEntry& Entry) const
    { return ProbeDev15::Matches(Entry, Search, KindFilter, StateFilter); }

    void RefreshAssets()
    {
        TMap<FString, ProbeDev15::FEntry> Existing;
        for (const ProbeDev15::FEntry& Entry : Entries) { Existing.Add(Entry.Path, Entry); }
        Entries.Reset();
        FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        FARFilter Filter;
        Filter.ClassPaths.Add(UProbeTestAsset::StaticClass()->GetClassPathName());
        Filter.ClassPaths.Add(UProbeTestPack::StaticClass()->GetClassPathName());
        TArray<FAssetData> Found;
        Module.Get().GetAssets(Filter, Found);
        for (const FAssetData& Asset : Found)
        {
            const FString Path = Asset.GetObjectPathString();
            ProbeDev15::FEntry Entry;
            if (const ProbeDev15::FEntry* Old = Existing.Find(Path)) { Entry = *Old; }
            Entry.Path = Path;
            Entry.Name = Asset.AssetName.ToString();
            Entry.Folder = Asset.PackagePath.ToString();
            Entry.Kind = Asset.AssetClassPath == UProbeTestPack::StaticClass()->GetClassPathName()
                ? ProbeDev15::EKind::Pack : ProbeDev15::EKind::Test;
            Entries.Add(MoveTemp(Entry));
        }
        Entries.Sort([](const ProbeDev15::FEntry& A, const ProbeDev15::FEntry& B)
        { return A.Name == B.Name ? A.Path < B.Path : A.Name < B.Name; });
        TArray<FString> MissingSelections;
        for (const FString& Path : SelectedPaths)
        { if (!Find(Path)) { MissingSelections.Add(Path); } }
        for (const FString& Path : MissingSelections) { SelectedPaths.Remove(Path); }
        if (!ActivePath.IsEmpty() && !Find(ActivePath)) { ActivePath.Reset(); }
        Notice = FString::Printf(TEXT("Discovered %d assets. Scanning is optional and read-only."), Entries.Num());
        RebuildRows();
    }

    void Scan(bool bOnlySelected, bool bIgnoreFilters)
    {
        int32 Scanned = 0;
        for (ProbeDev15::FEntry& Entry : Entries)
        {
            if (bOnlySelected ? !SelectedPaths.Contains(Entry.Path) : (!bIgnoreFilters && !Matches(Entry)))
            { continue; }
            if (Entry.Kind == ProbeDev15::EKind::Test)
            { ProbeDev15::InspectTest(Entry, LoadObject<UProbeTestAsset>(nullptr, *Entry.Path)); }
            else
            {
                ProbeDev15::InspectPack(Entry, LoadObject<UProbeTestPack>(nullptr, *Entry.Path),
                    [](const FString& Path) -> const UProbeTestAsset*
                    { return Path.IsEmpty() ? nullptr : LoadObject<UProbeTestAsset>(nullptr, *Path); });
            }
            ++Scanned;
        }
        Notice = FString::Printf(TEXT("Preflighted %d asset(s); no tests executed and no assets modified."), Scanned);
        RebuildRows();
    }

    void RebuildRows()
    {
        if (!Rows.IsValid()) { return; }
        Rows->ClearChildren();
        int32 Visible = 0;
        for (const ProbeDev15::FEntry& Entry : Entries)
        {
            if (!Matches(Entry)) { continue; }
            ++Visible;
            if (Visible > 300) { continue; } // Bound Slate allocations for large projects.
            const FString Path = Entry.Path;
            const bool bPack = Entry.Kind == ProbeDev15::EKind::Pack;
            Rows->AddSlot().AutoHeight().Padding(2,1)
            [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(4)
              [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,7,0)
                [ SNew(SCheckBox)
                  .IsChecked_Lambda([this,Path]() { return SelectedPaths.Contains(Path) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                  .OnCheckStateChanged_Lambda([this,Path](ECheckBoxState State)
                  { if (State == ECheckBoxState::Checked) { SelectedPaths.Add(Path); }
                    else { SelectedPaths.Remove(Path); } }) ]
                + SHorizontalBox::Slot().FillWidth(1.0f)
                [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                  .OnClicked_Lambda([this,Path]() { ActivePath = Path; return FReply::Handled(); })
                  [ SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text(FText::FromString(Entry.Name)) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text(FText::FromString(Entry.Folder))
                      .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ] ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6,0,6,0)
                [ SNew(STextBlock).Text(FText::FromString(bPack ? TEXT("PACK") : TEXT("TEST")))
                  .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(FText::FromString(ProbeDev15::StateName(Entry.State)))
                  .ColorAndOpacity(Entry.State == ProbeDev15::EState::Ready
                      ? FSlateColor(FLinearColor(0.32f,0.72f,0.48f)) : FSlateColor::UseSubduedForeground()) ] ] ];
        }
        if (Visible > 300)
        { Rows->AddSlot().AutoHeight().Padding(9)
            [ SNew(STextBlock).Text(FText::FromString(FString::Printf(
                TEXT("Showing first 300 of %d matches. Refine the search to view more."), Visible))) ]; }
        if (!Visible)
        { Rows->AddSlot().AutoHeight().Padding(9)
            [ SNew(STextBlock).Text(FText::FromString(TEXT("No assets match the current filters."))) ]; }
    }

    TSharedRef<SWidget> BuildKindMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        struct FOption { ProbeDev15::EKindFilter Filter; const TCHAR* Name; };
        const FOption Options[] = {
            {ProbeDev15::EKindFilter::All, TEXT("Tests & Packs")},
            {ProbeDev15::EKindFilter::Tests, TEXT("Tests Only")},
            {ProbeDev15::EKindFilter::Packs, TEXT("Packs Only")}
        };
        for (const FOption& Option : Options)
        { Menu.AddMenuEntry(FText::FromString(Option.Name), FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, Filter=Option.Filter]() { KindFilter = Filter; RebuildRows(); }))); }
        return Menu.MakeWidget();
    }
    TSharedRef<SWidget> BuildStateMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        struct FOption { ProbeDev15::EStateFilter Filter; const TCHAR* Name; };
        const FOption Options[] = {
            {ProbeDev15::EStateFilter::All,TEXT("All Readiness")},
            {ProbeDev15::EStateFilter::NotScanned,TEXT("Not Scanned")},
            {ProbeDev15::EStateFilter::Ready,TEXT("Ready")},
            {ProbeDev15::EStateFilter::Warning,TEXT("Warnings")},
            {ProbeDev15::EStateFilter::Blocked,TEXT("Blocked")},
            {ProbeDev15::EStateFilter::Disabled,TEXT("Disabled")}
        };
        for (const FOption& Option : Options)
        { Menu.AddMenuEntry(FText::FromString(Option.Name), FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, Filter=Option.Filter]() { StateFilter = Filter; RebuildRows(); }))); }
        return Menu.MakeWidget();
    }
    FText KindLabel() const
    {
        return FText::FromString(KindFilter == ProbeDev15::EKindFilter::Tests ? TEXT("Tests Only")
            : KindFilter == ProbeDev15::EKindFilter::Packs ? TEXT("Packs Only") : TEXT("Tests & Packs"));
    }
    FText StateLabel() const
    {
        switch (StateFilter)
        {
        case ProbeDev15::EStateFilter::Ready: return FText::FromString(TEXT("Ready"));
        case ProbeDev15::EStateFilter::Warning: return FText::FromString(TEXT("Warnings"));
        case ProbeDev15::EStateFilter::Blocked: return FText::FromString(TEXT("Blocked"));
        case ProbeDev15::EStateFilter::Disabled: return FText::FromString(TEXT("Disabled"));
        case ProbeDev15::EStateFilter::NotScanned: return FText::FromString(TEXT("Not Scanned"));
        default: return FText::FromString(TEXT("All Readiness"));
        }
    }
    FText SummaryText() const
    {
        const ProbeDev15::FSummary S = ProbeDev15::Summarize(Entries);
        return FText::FromString(FString::Printf(
            TEXT("%d total (%d Tests, %d Packs) | %d ready | %d warnings | %d blocked | %d disabled | %d unscanned | %d selected"),
            S.Total, S.Tests, S.Packs, S.Ready, S.Warnings, S.Blocked, S.Disabled, S.Unscanned, SelectedPaths.Num()));
    }
    FText DetailsText() const
    {
        const ProbeDev15::FEntry* Entry = Find(ActivePath);
        return FText::FromString(Entry ? Entry->Name + TEXT("\n") + Entry->Path + TEXT("\n\n") + Entry->Details
            : TEXT("Choose an asset on the left, then Scan Visible or Scan Selected to inspect its authoring readiness."));
    }
    FText NoticeText() const { return FText::FromString(Notice); }

    FReply OpenActive()
    {
        const ProbeDev15::FEntry* Entry = Find(ActivePath);
        if (!Entry) { return FReply::Handled(); }
        if (Entry->Kind == ProbeDev15::EKind::Test) { Probe::OpenTestAssetsInWorkspace({Entry->Path}); }
        else { Probe::OpenPackAssetsInWorkspace({Entry->Path}); }
        return FReply::Handled();
    }
    bool HasReadySelection() const
    {
        for (const ProbeDev15::FEntry& Entry : Entries)
        { if (SelectedPaths.Contains(Entry.Path) && Entry.Kind == ProbeDev15::EKind::Test
            && Entry.State == ProbeDev15::EState::Ready) { return true; } }
        return false;
    }
    FReply RunReadySelection()
    {
        TArray<FString> Paths;
        for (const ProbeDev15::FEntry& Entry : Entries)
        { if (SelectedPaths.Contains(Entry.Path) && Entry.Kind == ProbeDev15::EKind::Test
            && Entry.State == ProbeDev15::EState::Ready) { Paths.Add(Entry.Path); } }
        if (!Paths.IsEmpty()) { Probe::RunTestsInWorkspace(Paths); }
        Notice = FString::Printf(TEXT("Requested execution of %d explicitly selected Ready Test(s)."), Paths.Num());
        return FReply::Handled();
    }
    FString ReportText() const
    {
        FString Text = TEXT("Probe Dev 15 | Project QA Readiness (authoring only)\n");
        const ProbeDev15::FSummary S = ProbeDev15::Summarize(Entries);
        Text += FString::Printf(TEXT("%d total, %d Tests, %d Packs, %d Ready, %d Warnings, %d Blocked, %d Disabled, %d Unscanned\n\n"),
            S.Total, S.Tests, S.Packs, S.Ready, S.Warnings, S.Blocked, S.Disabled, S.Unscanned);
        for (const ProbeDev15::FEntry& Entry : Entries)
        { if (Matches(Entry)) { Text += Entry.Name + TEXT(" [") + ProbeDev15::StateName(Entry.State)
            + TEXT("] ") + Entry.Path + TEXT("\n") + Entry.Details + TEXT("\n\n"); } }
        return Text;
    }
    FReply CopyReport()
    {
        FPlatformApplicationMisc::ClipboardCopy(*ReportText());
        Notice = TEXT("Filtered QA readiness report copied to clipboard.");
        return FReply::Handled();
    }
    FReply ExportReport()
    {
        const FString Directory = FPaths::ProjectSavedDir() / TEXT("Probe/Reports");
        IFileManager::Get().MakeDirectory(*Directory, true);
        const FString File = Directory / (TEXT("ProbeReadiness-") + FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))
            + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8) + TEXT(".csv"));
        const FString CSV = ProbeDev15::ExportCSV(Entries, Search, KindFilter, StateFilter);
        const bool bSaved = FFileHelper::SaveStringToFile(CSV, *File, FFileHelper::EEncodingOptions::ForceUTF8);
        Notice = bSaved ? TEXT("Exported filtered QA readiness CSV: ") + File : TEXT("QA readiness CSV export failed.");
        return FReply::Handled();
    }
};

namespace ProbeDev15
{
    inline void OpenBrowser()
    {
        static TWeakPtr<SWindow> ExistingWindow;
        if (TSharedPtr<SWindow> Existing = ExistingWindow.Pin())
        { Existing->BringToFront(true); return; }
        TSharedRef<SWindow> Window = SNew(SWindow)
            .Title(FText::FromString(TEXT("Probe | QA Browser & Readiness")))
            .ClientSize(FVector2D(1080.0f, 720.0f))
            .SupportsMaximize(true).SupportsMinimize(true)
            [ SNew(SProbeDev15Browser) ];
        ExistingWindow = Window;
        FSlateApplication::Get().AddWindow(Window);
    }
}
