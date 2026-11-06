#pragma once

#include "ProbeTestRunner.h"
#include "ProbeAssertionCatalog.h"
#include "ProbeNativeUIPolicy.h"
#include "ProbeDev9Workflow.h"
#include "ProbeDev10PackPolicy.h"
#include "ProbeDev14StepValidation.h"

// Dev 14: read-only authoring contracts. No test is executed during preflight.
namespace ProbeDev14
{
    enum class ESeverity : uint8 { Information, Warning, Blocking };
    struct FIssue
    {
        ESeverity Severity = ESeverity::Information;
        int32 StepIndex = INDEX_NONE; // zero-based, INDEX_NONE for test/pack issues
        FString Message;
    };
    struct FReport
    {
        TArray<FIssue> Issues;
        int32 Blocking = 0;
        int32 Warnings = 0;
        void Add(ESeverity Severity, const FString& Message, int32 StepIndex = INDEX_NONE)
        {
            FIssue Issue;
            Issue.Severity = Severity;
            Issue.StepIndex = StepIndex;
            Issue.Message = Message;
            Issues.Add(MoveTemp(Issue));
            Blocking += Severity == ESeverity::Blocking;
            Warnings += Severity == ESeverity::Warning;
        }
        bool Ready() const { return Blocking == 0; }
        FString Text() const
        {
            FString Output = FString::Printf(TEXT("%d blocking issue(s), %d warning(s)\n"), Blocking, Warnings);
            if (Issues.IsEmpty()) { return Output + TEXT("Ready for execution."); }
            for (const FIssue& Issue : Issues)
            {
                Output += FString::Printf(TEXT("[%s] %s%s\n"),
                    Issue.Severity == ESeverity::Blocking ? TEXT("BLOCK") : Issue.Severity == ESeverity::Warning ? TEXT("WARN") : TEXT("INFO"),
                    Issue.StepIndex == INDEX_NONE ? TEXT("") : *FString::Printf(TEXT("Step %d: "), Issue.StepIndex + 1), *Issue.Message);
            }
            return Output;
        }
    };

    inline FReport PreflightTest(const UProbeTestAsset& Test)
    {
        FReport Report;
        if (!Test.bEnabled) { Report.Add(ESeverity::Warning, TEXT("Test is disabled: execution will be skipped.")); }
        if (Test.bNeedsConfiguration) { Report.Add(ESeverity::Blocking, TEXT("Test still needs author review/configuration.")); }
        if (Test.Steps.IsEmpty()) { Report.Add(ESeverity::Blocking, TEXT("Test has no steps.")); }
        if (Test.Steps.Num() > 4096) { Report.Add(ESeverity::Blocking, TEXT("Test exceeds the 4096-step authoring limit.")); }
        int32 Enabled = 0;
        for (int32 I = 0; I < Test.Steps.Num(); ++I)
        {
            const FProbeTestStep& Step = Test.Steps[I];
            if (!Step.bEnabled) { continue; }
            ++Enabled;
            const FString Missing = ConfigurationIssue(Step);
            if (!Missing.IsEmpty()) { Report.Add(ESeverity::Blocking, Missing, I); }
            if (Step.AssertionId.IsNone() && ProbeNativeUIPolicy::IsInput(Step.Type) && !Test.bAllowEditorUIInput)
            { Report.Add(ESeverity::Blocking, TEXT("Live Editor input needs explicit permission."), I); }
            if (Step.AssertionId.IsNone() && Step.Type >= EProbeStepType::ActorFixtureSpawn
                && Step.Type <= EProbeStepType::ActorFixtureScaleNotNearlyEquals && !Test.bAllowTransientEditorActors)
            { Report.Add(ESeverity::Blocking, TEXT("Editor actor fixture requires explicit permission."), I); }
        }
        if (!Test.Steps.IsEmpty() && Enabled == 0)
        { Report.Add(ESeverity::Warning, TEXT("All steps are disabled: execution will be skipped.")); }
        Probe::FRunOptions Full;
        ProbeDev9Workflow::FLifecycle Lifecycle;
        FString Error;
        if (!ProbeDev9Workflow::ValidateLifecycle(Test, Full, Lifecycle, Error))
        { Report.Add(ESeverity::Blocking, TEXT("Workflow: ") + Error); }
        TSet<FString> Names;
        for (int32 I = 0; I < Test.Variations.Num(); ++I)
        {
            const FProbeTestVariation& Variation = Test.Variations[I];
            if (!Variation.bEnabled) { continue; }
            const FString Name = Variation.Name.TrimStartAndEnd();
            if (Name.IsEmpty()) { Report.Add(ESeverity::Blocking, FString::Printf(TEXT("Variation %d is unnamed."), I + 1)); continue; }
            const FString Canonical = Name.ToLower();
            if (Names.Contains(Canonical)) { Report.Add(ESeverity::Blocking, TEXT("Duplicate variation name: ") + Name); }
            Names.Add(Canonical);
            TArray<FProbeTestStep> Expanded;
            FString ExpandedName;
            if (!ProbeDev9Workflow::Resolve(Test, I, Expanded, ExpandedName, Error))
            { Report.Add(ESeverity::Blocking, FString::Printf(TEXT("Variation '%s': %s"), *Name, *Error)); }
            else
            {
                for (int32 StepIndex = 0; StepIndex < Expanded.Num(); ++StepIndex)
                {
                    if (!Expanded[StepIndex].bEnabled) { continue; }
                    const FString Issue = ConfigurationIssue(Expanded[StepIndex]);
                    if (!Issue.IsEmpty()) { Report.Add(ESeverity::Blocking,
                        FString::Printf(TEXT("Variation '%s': %s"), *Name, *Issue), StepIndex); }
                }
            }
        }
        if (Test.bRunVariationsInBulk && ProbeDev9Workflow::EnabledVariationIndices(Test).IsEmpty())
        { Report.Add(ESeverity::Warning, TEXT("Run Variations in Bulk is enabled but no variations are enabled.")); }
        return Report;
    }

    enum class EBulkAction : uint8 { Enable, Disable, Duplicate, Delete, MoveUp, MoveDown, AssignGroup };
    inline bool ApplyBulk(UProbeTestAsset& Asset, TSet<int32>& Selection, EBulkAction Action,
        const FString& Group = FString())
    {
        const int32 Count = Asset.Steps.Num();
        if (Selection.IsEmpty()) { return false; }
        for (const int32 Index : Selection) { if (!Asset.Steps.IsValidIndex(Index)) { return false; } }
        if (Action == EBulkAction::Duplicate && Count + Selection.Num() > 4096) { return false; }
        if (Action == EBulkAction::AssignGroup && Group.TrimStartAndEnd().Len() > 96) { return false; }
        if (Action == EBulkAction::MoveUp || Action == EBulkAction::MoveDown)
        {
            // Move selected blocks one position at a time; preserve their relative order.
            if (Action == EBulkAction::MoveUp)
            {
                for (int32 I = 1; I < Count; ++I)
                { if (Selection.Contains(I) && !Selection.Contains(I - 1))
                    { Asset.Steps.Swap(I - 1, I); Selection.Remove(I); Selection.Add(I - 1); } }
            }
            else
            {
                for (int32 I = Count - 2; I >= 0; --I)
                { if (Selection.Contains(I) && !Selection.Contains(I + 1))
                    { Asset.Steps.Swap(I, I + 1); Selection.Remove(I); Selection.Add(I + 1); } }
            }
            return true;
        }
        if (Action == EBulkAction::Duplicate || Action == EBulkAction::Delete)
        {
            TArray<FProbeTestStep> Rebuilt;
            Rebuilt.Reserve(Count + (Action == EBulkAction::Duplicate ? Selection.Num() : 0));
            TSet<int32> NewSelection;
            for (int32 I = 0; I < Count; ++I)
            {
                if (Action == EBulkAction::Delete && Selection.Contains(I)) { continue; }
                const FProbeTestStep Copy = Asset.Steps[I];
                Rebuilt.Add(Copy);
                if (Action == EBulkAction::Duplicate && Selection.Contains(I))
                { NewSelection.Add(Rebuilt.Num()); Rebuilt.Add(Copy); }
            }
            Asset.Steps = MoveTemp(Rebuilt);
            Selection = MoveTemp(NewSelection);
            return true;
        }
        for (int32 I : Selection)
        {
            FProbeTestStep& Step = Asset.Steps[I];
            if (Action == EBulkAction::Enable) { Step.bEnabled = true; }
            else if (Action == EBulkAction::Disable) { Step.bEnabled = false; }
            else if (Action == EBulkAction::AssignGroup) { Step.GroupName = Group.TrimStartAndEnd(); }
        }
        return true;
    }

    enum class ERecipe : uint8 { PluginHealth, EditorState, ReadOnlyUI, TransientFixture };
    inline const TCHAR* RecipeName(ERecipe Recipe)
    {
        switch (Recipe)
        {
        case ERecipe::PluginHealth: return TEXT("Plugin Health Check");
        case ERecipe::EditorState: return TEXT("Editor State Check");
        case ERecipe::ReadOnlyUI: return TEXT("Read-Only UI Check");
        default: return TEXT("Transient Fixture Lifecycle");
        }
    }
    inline TArray<FProbeTestStep> BuildRecipe(ERecipe Recipe)
    {
        TArray<FProbeTestStep> Steps;
        auto Add = [&Steps](EProbeStepType Type, const TCHAR* Target, const TCHAR* Value, const TCHAR* Group)
        {
            FProbeTestStep Step;
            Step.Type = Type; Step.Target = Target; Step.ExpectedValue = Value;
            Step.GroupName = Group; Steps.Add(MoveTemp(Step));
        };
        switch (Recipe)
        {
        case ERecipe::PluginHealth:
            Add(EProbeStepType::PluginInstalled, TEXT("Probe"), TEXT(""), TEXT("Plugin Health"));
            Add(EProbeStepType::PluginEnabled, TEXT("Probe"), TEXT(""), TEXT("Plugin Health"));
            Add(EProbeStepType::ModuleLoaded, TEXT("ProbeEditor"), TEXT(""), TEXT("Plugin Health"));
            break;
        case ERecipe::EditorState:
            Add(EProbeStepType::EditorWorldExists, TEXT(""), TEXT(""), TEXT("Editor State"));
            Add(EProbeStepType::PIEIsNotRunning, TEXT(""), TEXT(""), TEXT("Editor State"));
            break;
        case ERecipe::ReadOnlyUI:
            Add(EProbeStepType::UIWidgetExists, TEXT("details:search"), TEXT(""), TEXT("UI Inspection"));
            Add(EProbeStepType::UIWidgetVisible, TEXT("details:search"), TEXT(""), TEXT("UI Inspection"));
            break;
        case ERecipe::TransientFixture:
            Add(EProbeStepType::FixtureCreateObject, TEXT("/Script/CoreUObject.Object"), TEXT("Dev14Fixture"), TEXT("Setup"));
            Add(EProbeStepType::FixtureObjectExists, TEXT("Dev14Fixture"), TEXT(""), TEXT("Verify"));
            Add(EProbeStepType::FixtureDestroyObject, TEXT("Dev14Fixture"), TEXT(""), TEXT("Cleanup"));
            break;
        }
        return Steps;
    }
    inline bool AppendRecipe(UProbeTestAsset& Test, ERecipe Recipe)
    {
        const TArray<FProbeTestStep> Added = BuildRecipe(Recipe);
        // An existing cleanup group must remain the final contiguous block.
        // Appending a recipe after it would invalidate the saved lifecycle.
        if (Added.IsEmpty() || Test.Steps.Num() + Added.Num() > 4096
            || (!Test.Steps.IsEmpty() && !Test.CleanupGroupName.TrimStartAndEnd().IsEmpty()))
        { return false; }
        const bool bEmpty = Test.Steps.IsEmpty();
        Test.Steps.Append(Added);
        Test.bNeedsConfiguration = true; // Always require explicit author review.
        if (bEmpty && Recipe == ERecipe::TransientFixture)
        { Test.SetupGroupName = TEXT("Setup"); Test.CleanupGroupName = TEXT("Cleanup"); }
        // Do not change any existing permissions or lifecycle metadata.
        return true;
    }

    inline FReport PreflightPack(const UProbeTestPack& Pack,
        TFunctionRef<const UProbeTestAsset*(const FString&)> ResolveAsset)
    {
        FReport Report;
        FString Error;
        if (Pack.Tests.IsEmpty()) { Report.Add(ESeverity::Blocking, TEXT("Pack has no members.")); }
        if (!ProbeDev10PackPolicy::Validate(Pack, Error)) { Report.Add(ESeverity::Blocking, Error); }
        for (int32 I = 0; I < Pack.Tests.Num(); ++I)
        {
            const FString Path = Pack.Tests[I].ToString();
            const UProbeTestAsset* Test = Path.IsEmpty() ? nullptr : ResolveAsset(Path);
            if (!Test) { Report.Add(ESeverity::Blocking,
                FString::Printf(TEXT("Pack member %d cannot be loaded: %s"), I + 1, *Path)); continue; }
            FReport Member = PreflightTest(*Test);
            if (Member.Blocking || Member.Warnings)
            { Report.Add(Member.Blocking ? ESeverity::Blocking : ESeverity::Warning,
                FString::Printf(TEXT("Member %d '%s': %d blocking, %d warnings."),
                    I + 1, *Test->GetName(), Member.Blocking, Member.Warnings)); }
            if (Pack.bRetryReadOnlyFailuresOnce && !ProbeDev10PackPolicy::CanRetryReadOnly(*Test))
            { Report.Add(ESeverity::Information, FString::Printf(TEXT("Member %d is ineligible for read-only automatic retry."), I + 1)); }
        }
        return Report;
    }
}
