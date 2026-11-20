#pragma once

#include "ProbeDev29MixedRecovery.h"

// Presentation-only, deterministic view of the mixed Pack runner. This model
// intentionally counts Pack members, not individual result rows: portable
// Packs can emit more than one underlying test result.
namespace ProbeDev30
{
    struct FProgress
    {
        int32 Total = 0;
        int32 Completed = 0;
        int32 Passed = 0;
        int32 Problems = 0;
        int32 Skipped = 0;
        int32 Pending = 0;
        int32 ActiveIndex = INDEX_NONE;
        float Fraction() const { return Total > 0 ? static_cast<float>(Completed) / Total : 0.f; }
    };

    inline FProgress Summarize(const TArray<ProbeDev29::EMemberState>& States)
    {
        FProgress P;
        P.Total = States.Num();
        for (int32 Index = 0; Index < States.Num(); ++Index)
        {
            switch (States[Index])
            {
            case ProbeDev29::EMemberState::Passed:
            case ProbeDev29::EMemberState::Reused: ++P.Passed; ++P.Completed; break;
            case ProbeDev29::EMemberState::Failed:
            case ProbeDev29::EMemberState::Error:
            case ProbeDev29::EMemberState::Interrupted: ++P.Problems; ++P.Completed; break;
            case ProbeDev29::EMemberState::Skipped: ++P.Skipped; ++P.Completed; break;
            case ProbeDev29::EMemberState::Starting:
            case ProbeDev29::EMemberState::Running:
                if (P.ActiveIndex == INDEX_NONE) { P.ActiveIndex = Index; }
                ++P.Pending; break;
            case ProbeDev29::EMemberState::Queued: ++P.Pending; break;
            }
        }
        return P;
    }
}
