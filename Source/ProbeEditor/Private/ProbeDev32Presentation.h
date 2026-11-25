#pragma once

#include "ProbeDev30ExecutionPresentation.h"
#include "ProbeTestPack.h"
#include "Math/Color.h"

// Dev32: centralized UI-only presentation policy. A terminal execution must
// never retain the animated blue in-progress treatment. State totals count
// Pack members, even when a portable Pack expands to several result records.
namespace ProbeDev32
{
    enum class EBarOutcome : uint8 { InProgress, Passed, Problems, Skipped, Empty };

    inline EBarOutcome ResolveOutcome(const TArray<ProbeDev29::EMemberState>& States, bool bExecuting)
    {
        if (bExecuting) { return EBarOutcome::InProgress; }
        const ProbeDev30::FProgress Progress = ProbeDev30::Summarize(States);
        if (Progress.Total == 0 || Progress.Pending > 0) { return EBarOutcome::Empty; }
        if (Progress.Problems > 0) { return EBarOutcome::Problems; }
        if (Progress.Skipped > 0) { return EBarOutcome::Skipped; }
        return EBarOutcome::Passed;
    }

    inline const TCHAR* CompletionLabel(const TArray<ProbeDev29::EMemberState>& States)
    {
        switch (ResolveOutcome(States, false))
        {
        case EBarOutcome::Passed: return TEXT("Finished: Passed");
        case EBarOutcome::Problems: return TEXT("Finished: Problems");
        case EBarOutcome::Skipped: return TEXT("Finished: Skipped");
        case EBarOutcome::Empty: return TEXT("Incomplete");
        default: return TEXT("Executing");
        }
    }

    inline FLinearColor CompletedTint(const TArray<ProbeDev29::EMemberState>& States)
    {
        switch (ResolveOutcome(States, false))
        {
        case EBarOutcome::Passed: return FLinearColor(0.10f, 0.60f, 0.26f, 1.f);
        case EBarOutcome::Problems: return FLinearColor(0.75f, 0.18f, 0.14f, 1.f);
        case EBarOutcome::Skipped: return FLinearColor(0.80f, 0.52f, 0.12f, 1.f);
        default: return FLinearColor(0.24f, 0.27f, 0.32f, 1.f);
        }
    }

    inline FString PackSourceSummary(const UProbeTestPack* Pack)
    {
        if (!Pack) { return TEXT("Tests (0) | Select a Pack"); }
        if (Pack->MixedEntries.IsEmpty())
        { return FString::Printf(TEXT("Tests (%d) | Authored"), Pack->Tests.Num()); }
        int32 Authored = 0, Portable = 0, Native = 0;
        for (const FProbePackMixedEntry& Entry : Pack->MixedEntries)
        {
            switch (Entry.Source)
            {
            case EProbePackMixedSource::AuthoredTest: ++Authored; break;
            case EProbePackMixedSource::NativeAutomation: ++Native; break;
            case EProbePackMixedSource::PortableTest:
            case EProbePackMixedSource::PortablePack: ++Portable; break;
            }
        }
        return FString::Printf(TEXT("Tests (%d) | Mixed sources: %d authored, %d portable, %d native"),
            Pack->MixedEntries.Num(), Authored, Portable, Native);
    }
}
