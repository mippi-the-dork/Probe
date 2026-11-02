#pragma once
#include "ProbeTestRunner.h"

namespace ProbeQueueControl
{
    // Never cancel input on the Slate thread or interrupt an active test.
    // Only unstarted queued results change state when a stop is requested.
    inline int32 SkipPending(TArray<int32>& Pending, TArray<Probe::FTestResult>& Results,
        const FString& Reason = TEXT("Stopped before execution at user request."))
    {
        int32 Skipped = 0;
        for (const int32 Index : Pending)
        {
            if (!Results.IsValidIndex(Index)) { continue; }
            Results[Index].Status = Probe::EResultStatus::Skipped;
            Results[Index].Detail = Reason;
            ++Skipped;
        }
        Pending.Reset();
        return Skipped;
    }
}
