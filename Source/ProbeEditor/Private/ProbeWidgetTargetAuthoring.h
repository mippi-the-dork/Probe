#pragma once

#include "ProbeTestAsset.h"
#include "ProbeNativeUIPolicy.h"
#include "ProbeWidgetDiscovery.h"

// Dev 5: pure, testable policy shared by quick-step creation and regression tests.
// The UI must still rescan and resolve the selector immediately before applying.
namespace ProbeWidgetTargetAuthoring
{
    inline bool IsTextType(EProbeStepType Type)
    {
        return Type == EProbeStepType::UIWidgetTextEquals
            || Type == EProbeStepType::UIWaitForWidgetTextEquals
            || Type == EProbeStepType::UIWaitForWidgetTextNotEquals;
    }

    inline bool CanMakeQuickStep(EProbeStepType Type, const ProbeWidgetDiscovery::FCandidate& Candidate)
    {
        if (!Candidate.IsUnique() || Candidate.Id.IsEmpty()) { return false; }
        switch (Type)
        {
        case EProbeStepType::UIWidgetExists:
        case EProbeStepType::UIWidgetVisible:
        case EProbeStepType::UIWidgetInteractable:
        case EProbeStepType::UIWaitForWidgetVisible:
        case EProbeStepType::UIWaitForWidgetInteractable:
        case EProbeStepType::UIWaitForWidgetGone:
            return true;
        case EProbeStepType::UIWidgetTextEquals:
        case EProbeStepType::UIWaitForWidgetTextEquals:
        case EProbeStepType::UIWaitForWidgetTextNotEquals:
            return Candidate.bHasReadableText;
        default:
            return false;
        }
    }

    inline bool ConfigureQuickStep(FProbeTestStep& Step, EProbeStepType Type,
        const ProbeWidgetDiscovery::FCandidate& Candidate)
    {
        if (!CanMakeQuickStep(Type, Candidate)) { return false; }
        Step.Type = Type;
        Step.AssertionId = NAME_None;
        Step.Target = Candidate.Selector();
        if (IsTextType(Type)) { Step.ExpectedValue = Candidate.ReadableText; }
        return true;
    }

    // Shared filter policy for the live target browser. Both flags together
    // intentionally have no matches rather than silently loosening a filter.
    inline bool MatchesBrowserFilters(const ProbeWidgetDiscovery::FCandidate& Candidate,
        const FString& Query, bool bUniqueOnly, bool bDuplicatesOnly)
    {
        if (bUniqueOnly && !Candidate.IsUnique()) { return false; }
        if (bDuplicatesOnly && Candidate.IsUnique()) { return false; }
        if (Query.IsEmpty()) { return true; }
        const FString Searchable = Candidate.Id + TEXT(" ") + Candidate.WindowTitle
            + TEXT(" ") + Candidate.WidgetType + TEXT(" ") + Candidate.ReadableText;
        return Searchable.Contains(Query, ESearchCase::IgnoreCase);
    }

    inline bool HasNativeWidgetTarget(const FProbeTestStep& Step)
    {
        return Step.AssertionId.IsNone() && ProbeNativeUIPolicy::HasTarget(Step.Type);
    }

    struct FPreflightItem
    {
        int32 StepIndex = INDEX_NONE;
        FString Selector;
        ProbeWidgetDiscovery::FResolution Resolution;
        bool bMissingIsExpected = false;
    };

    struct FPreflightReport
    {
        TArray<FPreflightItem> Items;
        int32 Unique = 0;
        int32 Missing = 0;
        int32 ExpectedAbsent = 0;
        int32 Ambiguous = 0;
        int32 DeferredNativeDetails = 0;
        int32 Unsupported = 0;

        int32 Total() const { return Items.Num(); }
    };

    // A preflight is informational and side-effect-free: it must not modify the
    // Test Asset, run assertions, change readiness, or send native UI input.
    inline FPreflightReport AnalyzeTargets(const UProbeTestAsset& Asset,
        const TArray<ProbeWidgetDiscovery::FCandidate>& Candidates)
    {
        FPreflightReport Report;
        for (int32 Index = 0; Index < Asset.Steps.Num(); ++Index)
        {
            const FProbeTestStep& Step = Asset.Steps[Index];
            if (!HasNativeWidgetTarget(Step)) { continue; }
            FPreflightItem& Item = Report.Items.AddDefaulted_GetRef();
            Item.StepIndex = Index;
            Item.Selector = Step.Target;
            Item.Resolution = ProbeWidgetDiscovery::Resolve(Candidates, Step.Target);
            switch (Item.Resolution.State)
            {
            case ProbeWidgetDiscovery::EResolution::Unique: ++Report.Unique; break;
            case ProbeWidgetDiscovery::EResolution::Ambiguous: ++Report.Ambiguous; break;
            case ProbeWidgetDiscovery::EResolution::NativeDetails: ++Report.DeferredNativeDetails; break;
            case ProbeWidgetDiscovery::EResolution::Unsupported: ++Report.Unsupported; break;
            default:
                Item.bMissingIsExpected = Item.Resolution.State == ProbeWidgetDiscovery::EResolution::Missing
                    && (Step.Type == EProbeStepType::UIWidgetNotExists
                        || Step.Type == EProbeStepType::UIWidgetNotVisible
                        || Step.Type == EProbeStepType::UIWidgetNotInteractable
                        || Step.Type == EProbeStepType::UIWaitForWidgetGone
                        || Step.Type == EProbeStepType::UIWaitForWidgetNotVisible);
                if (Item.bMissingIsExpected) { ++Report.ExpectedAbsent; }
                else { ++Report.Missing; }
                break;
            }
        }
        return Report;
    }

}
