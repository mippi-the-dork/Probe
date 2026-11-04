#pragma once

#include "ProbeDev13HistoryModel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"

// A full-size, read-only browser. All mutable workspace actions use weak Slate
// delegates, so closing the Probe workspace cannot leave dangling UI pointers.
namespace ProbeDev13HistoryUI
{
    DECLARE_DELEGATE_OneParam(FOnRunAction, FString);
    DECLARE_DELEGATE_OneParam(FOnHistoryTextAction, FString);

    class SBrowser : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SBrowser) {}
            SLATE_EVENT(FOnRunAction, OnViewRun)
            SLATE_EVENT(FOnRunAction, OnCompareRun)
            SLATE_EVENT(FOnRunAction, OnPinRun)
            SLATE_EVENT(FOnRunAction, OnDeleteRun)
            SLATE_EVENT(FOnHistoryTextAction, OnCopyReport)
            SLATE_EVENT(FOnHistoryTextAction, OnExportCsv)
        SLATE_END_ARGS()

        void Construct(const FArguments& Args)
        {
            OnViewRun = Args._OnViewRun;
            OnCompareRun = Args._OnCompareRun;
            OnPinRun = Args._OnPinRun;
            OnDeleteRun = Args._OnDeleteRun;
            OnCopyReport = Args._OnCopyReport;
            OnExportCsv = Args._OnExportCsv;

            ChildSlot
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(12, 10, 12, 5)
                [ SNew(SVerticalBox)
                  + SVerticalBox::Slot().AutoHeight()
                  [ SNew(STextBlock).Text(FText::FromString(TEXT("Probe History Browser"))) ]
                  + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
                  [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(Summary); }) ] ]
                + SVerticalBox::Slot().AutoHeight().Padding(12, 5)
                [ SNew(SHorizontalBox)
                  + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                  [ SNew(SSearchBox)
                    .HintText(FText::FromString(TEXT("Search time, Test Asset, Pack, or variation...")))
                    .OnTextChanged_Lambda([this](const FText& Text)
                    { Search = Text.ToString(); RebuildRunList(); RebuildDetails(); }) ]
                  + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                  [ SNew(SComboButton)
                    .OnGetMenuContent(this, &SBrowser::BuildFilterMenu)
                    .ButtonContent()
                    [ SNew(STextBlock).Text_Lambda([this]()
                      {
                        using F = ProbeDev13History::EStatusFilter;
                        return FText::FromString(Filter == F::Problems ? TEXT("Problems")
                            : Filter == F::PassedOnly ? TEXT("No Problems") : TEXT("All Runs"));
                      }) ] ]
                  + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                  [ SNew(SButton).OnClicked_Lambda([this]() { Refresh(); return FReply::Handled(); })
                    [ SNew(STextBlock).Text(FText::FromString(TEXT("Refresh"))) ] ] ]
                + SVerticalBox::Slot().FillHeight(1).Padding(12, 5, 12, 12)
                [ SNew(SSplitter).Orientation(Orient_Horizontal)
                  + SSplitter::Slot().Value(0.43f)
                  [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(6)
                    [ SNew(SVerticalBox)
                      + SVerticalBox::Slot().AutoHeight().Padding(3, 3)
                      [ SNew(STextBlock).Text(FText::FromString(TEXT("SAVED RUNS"))) ]
                      + SVerticalBox::Slot().FillHeight(1)
                      [ SNew(SScrollBox) + SScrollBox::Slot()
                        [ SAssignNew(RunRows, SVerticalBox) ] ] ] ]
                  + SSplitter::Slot().Value(0.57f)
                  [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(7)
                    [ SNew(SVerticalBox)
                      + SVerticalBox::Slot().AutoHeight().Padding(2, 2, 2, 7)
                      [ SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 6, 0)
                        [ SNew(SButton).OnClicked_Lambda([this]()
                          { bAnalysis = false; RebuildDetails(); return FReply::Handled(); })
                          [ SNew(STextBlock).Text(FText::FromString(TEXT("Run Details"))) ] ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [ SNew(SButton).OnClicked_Lambda([this]()
                          { bAnalysis = true; RebuildDetails(); return FReply::Handled(); })
                          [ SNew(STextBlock).Text(FText::FromString(TEXT("Regression Analysis"))) ] ] ]
                      + SVerticalBox::Slot().FillHeight(1)
                      [ SNew(SScrollBox) + SScrollBox::Slot()
                        [ SAssignNew(DetailRows, SVerticalBox) ] ] ] ] ]
            ];
            Refresh();
        }

        void Refresh()
        {
            Runs = ProbeDev13History::Load(ProbeDev11RunHistory::DefaultDirectory());
            int64 Bytes = 0;
            int32 Problems = 0;
            for (const auto& Run : Runs) { Bytes += Run.Bytes; Problems += Run.Problems; }
            Summary = FString::Printf(TEXT("%d saved runs  |  %.2f MiB on disk  |  %d problem results  |  read-only until an action is confirmed"),
                Runs.Num(), static_cast<double>(Bytes) / (1024.0 * 1024.0), Problems);
            bool bSelectedExists = false;
            for (const auto& Run : Runs)
            { if (Run.Path == SelectedPath) { bSelectedExists = true; break; } }
            if (!bSelectedExists) { SelectedPath = Runs.IsEmpty() ? FString() : Runs[0].Path; }
            RebuildRunList();
            RebuildDetails();
        }

    private:
        FOnRunAction OnViewRun, OnCompareRun, OnPinRun, OnDeleteRun;
        FOnHistoryTextAction OnCopyReport, OnExportCsv;
        FString Search, Summary, SelectedPath;
        ProbeDev13History::EStatusFilter Filter = ProbeDev13History::EStatusFilter::All;
        TArray<ProbeDev13History::FRun> Runs;
        TSharedPtr<SVerticalBox> RunRows, DetailRows;
        bool bAnalysis = false;
        bool bProblemsOnly = false;

        TSharedRef<SWidget> BuildFilterMenu()
        {
            FMenuBuilder Menu(true, nullptr);
            using F = ProbeDev13History::EStatusFilter;
            for (const auto Choice : { F::All, F::Problems, F::PassedOnly })
            {
                const FString Label = Choice == F::All ? TEXT("All Runs")
                    : Choice == F::Problems ? TEXT("Runs with Problems") : TEXT("Runs without Problems");
                Menu.AddMenuEntry(FText::FromString(Label), FText::GetEmpty(), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this, Choice]()
                    { Filter = Choice; RebuildRunList(); RebuildDetails(); }),
                        FCanExecuteAction(), FIsActionChecked::CreateLambda([this, Choice]() { return Filter == Choice; })),
                    NAME_None, EUserInterfaceActionType::RadioButton);
            }
            return Menu.MakeWidget();
        }

        void AddLine(const TSharedPtr<SVerticalBox>& Container, const FString& Text,
            const FMargin& Margin = FMargin(3, 3))
        {
            Container->AddSlot().AutoHeight().Padding(Margin)
            [ SNew(STextBlock).Text(FText::FromString(Text)).AutoWrapText(true) ];
        }

        void RebuildRunList()
        {
            if (!RunRows.IsValid()) { return; }
            bool bSelectionIsVisible = false;
            for (const auto& Run : Runs)
            {
                if (Run.Path == SelectedPath && ProbeDev13History::MatchesWithTimestamp(Run, Search, Filter))
                { bSelectionIsVisible = true; break; }
            }
            if (!bSelectionIsVisible)
            {
                SelectedPath.Reset();
                for (const auto& Run : Runs)
                {
                    if (ProbeDev13History::MatchesWithTimestamp(Run, Search, Filter))
                    { SelectedPath = Run.Path; break; }
                }
            }
            RunRows->ClearChildren();
            int32 Count = 0;
            for (const auto& Run : Runs)
            {
                if (!ProbeDev13History::MatchesWithTimestamp(Run, Search, Filter)) { continue; }
                ++Count;
                const FString Path = Run.Path;
                RunRows->AddSlot().AutoHeight().Padding(1, 2)
                [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                  .OnClicked_Lambda([this, Path]()
                  { SelectedPath = Path; bAnalysis = false; RebuildRunList(); RebuildDetails(); return FReply::Handled(); })
                  .ToolTipText(FText::FromString(Path))
                  [ SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text(FText::FromString(
                        FString(Run.Path == SelectedPath ? TEXT("[Selected] ") : TEXT("")) + Run.Label())) ]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
                    [ SNew(STextBlock).Text(FText::FromString(Run.Snapshot.PackPath.IsEmpty()
                        ? FString::Printf(TEXT("Single/All Tests  |  %d total"), Run.Snapshot.Results.Num())
                        : FString::Printf(TEXT("Pack: %s  |  %d total"), *Run.Snapshot.PackPath, Run.Snapshot.Results.Num()))) ] ] ];
            }
            if (Count == 0) { AddLine(RunRows, TEXT("No saved runs match these filters.")); }
        }

        const ProbeDev13History::FRun* SelectedRun() const
        {
            for (const auto& Run : Runs)
            { if (Run.Path == SelectedPath) { return &Run; } }
            return nullptr;
        }

        void AddButton(const FString& Label, TFunction<void()> Callback)
        {
            const TSharedRef<TFunction<void()>> Action = MakeShared<TFunction<void()>>(MoveTemp(Callback));
            DetailRows->AddSlot().AutoHeight().Padding(2, 2)
            [ SNew(SButton).OnClicked_Lambda([Action]()
              { (*Action)(); return FReply::Handled(); })
              [ SNew(STextBlock).Text(FText::FromString(Label)) ] ];
        }

        void RebuildDetails()
        {
            if (!DetailRows.IsValid()) { return; }
            DetailRows->ClearChildren();
            if (bAnalysis) { BuildAnalysis(); return; }
            const auto* Selected = SelectedRun();
            if (!Selected) { AddLine(DetailRows, TEXT("No run selected.")); return; }
            AddLine(DetailRows, TEXT("SELECTED RUN"), FMargin(3, 6));
            AddLine(DetailRows, Selected->Label());
            AddLine(DetailRows, FString::Printf(TEXT("%d tests  |  %d errors  |  %d skipped  |  %.1f KiB"),
                Selected->Snapshot.Results.Num(), Selected->Errors, Selected->Skipped,
                static_cast<double>(Selected->Bytes) / 1024.0));
            if (!Selected->Snapshot.PackPath.IsEmpty())
            { AddLine(DetailRows, TEXT("Pack: ") + Selected->Snapshot.PackPath); }
            const FString Path = Selected->Path;
            AddButton(TEXT("View Saved Results in Probe"), [this, Path]() { OnViewRun.ExecuteIfBound(Path); });
            AddButton(TEXT("Compare Future Runs Against This"), [this, Path]() { OnCompareRun.ExecuteIfBound(Path); });
            AddButton(TEXT("Pin as Persistent Baseline"), [this, Path]() { OnPinRun.ExecuteIfBound(Path); });
            AddButton(TEXT("Delete This Run..."), [this, Path]()
            { OnDeleteRun.ExecuteIfBound(Path); Refresh(); });

            // Compare the selected run with its immediately preceding saved run.
            int32 SelectedIndex = INDEX_NONE;
            for (int32 Index = 0; Index < Runs.Num(); ++Index)
            { if (Runs[Index].Path == Path) { SelectedIndex = Index; break; } }
            if (SelectedIndex != INDEX_NONE && Runs.IsValidIndex(SelectedIndex + 1))
            {
                const auto Deltas = ProbeDev13History::Compare(
                    Runs[SelectedIndex + 1].Snapshot, Selected->Snapshot);
                int32 Regressions = 0, Recoveries = 0, StillFailing = 0;
                for (const auto& Delta : Deltas)
                {
                    if (Delta.Kind == ProbeDev13History::EDeltaKind::NewFailure) { ++Regressions; }
                    if (Delta.Kind == ProbeDev13History::EDeltaKind::Recovered) { ++Recoveries; }
                    if (Delta.Kind == ProbeDev13History::EDeltaKind::StillFailing) { ++StillFailing; }
                }
                AddLine(DetailRows, FString::Printf(TEXT("CHANGE VS PREVIOUS RUN  |  %d regressions, %d recovered, %d still failing"),
                    Regressions, Recoveries, StillFailing), FMargin(3, 12, 3, 5));
                int32 Shown = 0;
                for (const auto& Delta : Deltas)
                {
                    if (Delta.Kind == ProbeDev13History::EDeltaKind::Unchanged ||
                        Delta.Kind == ProbeDev13History::EDeltaKind::Skipped) { continue; }
                    if (++Shown > 16) { break; }
                    AddLine(DetailRows, FString::Printf(TEXT("[%s] %s%s"),
                        *ProbeDev13History::DeltaLabel(Delta.Kind), *Delta.AssetPath,
                        Delta.VariationName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" (%s)"), *Delta.VariationName)));
                }
                if (Shown > 16) { AddLine(DetailRows, TEXT("Additional changes omitted from preview.")); }
            }
            AddLine(DetailRows, TEXT("TEST RESULTS"), FMargin(3, 12, 3, 5));
            DetailRows->AddSlot().AutoHeight().Padding(2, 2)
            [ SNew(SCheckBox).IsChecked(bProblemsOnly ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
              .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
              { bProblemsOnly = State == ECheckBoxState::Checked; RebuildDetails(); })
              [ SNew(STextBlock).Text(FText::FromString(TEXT("Problems only"))) ] ];
            int32 ShownTests = 0;
            for (const auto& Test : Selected->Snapshot.Results)
            {
                if (bProblemsOnly && !ProbeDev13History::IsProblem(Test.Status)) { continue; }
                if (++ShownTests > 200) { break; }
                AddLine(DetailRows, FString::Printf(TEXT("[%s] %s%s (%.1f ms, %d attempt(s))"),
                    Probe::StatusName(Test.Status), *Test.AssetPath,
                    Test.VariationName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [%s]"), *Test.VariationName),
                    Test.DurationMs, Test.Attempts), FMargin(3, 5, 3, 0));
                if (ProbeDev13History::IsProblem(Test.Status))
                {
                    if (!Test.Detail.IsEmpty()) { AddLine(DetailRows, TEXT("   ") + Test.Detail); }
                    for (const auto& Step : Test.Steps)
                    {
                        if (ProbeDev13History::IsProblem(Step.Status))
                        {
                            AddLine(DetailRows, FString::Printf(TEXT("   Step %d / %s / %s: %s"),
                                Step.Index, *Step.GroupName, *Step.AssertionId, *Step.Detail));
                        }
                    }
                }
            }
            if (ShownTests > 200) { AddLine(DetailRows, TEXT("Only the first 200 matching tests are shown. View Saved Results for the full report.")); }
        }

        TArray<ProbeDev13History::FSnapshot> FilteredChronological() const
        {
            TArray<ProbeDev13History::FSnapshot> Result;
            for (int32 Index = Runs.Num() - 1; Index >= 0; --Index)
            { if (ProbeDev13History::MatchesWithTimestamp(Runs[Index], Search, Filter))
              { Result.Add(Runs[Index].Snapshot); } }
            return Result;
        }

        FString FilteredReport() const
        {
            TArray<ProbeDev13History::FRun> Matching;
            for (const auto& Run : Runs)
            { if (ProbeDev13History::MatchesWithTimestamp(Run, Search, Filter)) { Matching.Add(Run); } }
            return ProbeDev13History::InvestigationReport(Matching);
        }

        FString FilteredCsv() const
        { return ProbeDev13History::ExportTrendCsv(ProbeDev12History::Analyze(FilteredChronological())); }

        void BuildAnalysis()
        {
            const TArray<ProbeDev13History::FSnapshot> OldestFirst = FilteredChronological();
            const auto Trends = ProbeDev12History::Analyze(OldestFirst);
            const auto Incidents = ProbeDev13History::RecurringFailures(OldestFirst);
            int32 FlakyCount = 0;
            for (const auto& Row : Trends) { if (Row.IsFlaky()) { ++FlakyCount; } }
            AddLine(DetailRows, TEXT("HISTORICAL REGRESSION ANALYSIS"), FMargin(3, 6));
            AddLine(DetailRows, FString::Printf(TEXT("%d matching runs  |  %d test/variation identities  |  %d flaky  |  %d failure signatures"),
                OldestFirst.Num(), Trends.Num(), FlakyCount, Incidents.Num()));
            AddLine(DetailRows, TEXT("Skipped tests do not count toward failure rates. A flaky test needs at least two pass/fail transitions."));
            AddButton(TEXT("Copy Investigation Report"), [this]() { OnCopyReport.ExecuteIfBound(FilteredReport()); });
            AddButton(TEXT("Export Trends as CSV"), [this]() { OnExportCsv.ExecuteIfBound(FilteredCsv()); });
            AddLine(DetailRows, TEXT("FLAKY AND FAILING TESTS"), FMargin(3, 12, 3, 5));
            int32 Count = 0;
            for (const auto& Row : Trends)
            {
                if (Row.Problems == 0) { continue; }
                if (++Count > 60) { break; }
                AddLine(DetailRows, FString::Printf(TEXT("%s %s%s  |  %.1f%% failures  |  %d pass / %d problems / %d skipped  |  %d transitions"),
                    Row.IsFlaky() ? TEXT("[FLAKY]") : TEXT("[FAILING]"), *Row.AssetPath,
                    Row.VariationName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [%s]"), *Row.VariationName),
                    Row.FailureRate(), Row.Passes, Row.Problems, Row.Skipped, Row.Transitions));
            }
            if (Count == 0) { AddLine(DetailRows, TEXT("No failures in matching retained runs.")); }
            AddLine(DetailRows, TEXT("RECURRING FAILURE SIGNATURES"), FMargin(3, 12, 3, 5));
            for (int32 Index = 0; Index < FMath::Min(Incidents.Num(), 40); ++Index)
            {
                const auto& Incident = Incidents[Index];
                AddLine(DetailRows, FString::Printf(TEXT("%d runs | %s%s | %s%s"),
                    Incident.RunsAffected, *Incident.AssetPath,
                    Incident.VariationName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [%s]"), *Incident.VariationName),
                    *Incident.Assertion,
                    Incident.Group.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" / %s"), *Incident.Group)));
                AddLine(DetailRows, TEXT("   Most recent: ") + Incident.MostRecentDetail);
            }
        }
    };
}
