#pragma once
#include "ProbeTestAsset.h"

// Keep array editing out of Slate callbacks so it can be regression-tested.
// IMPORTANT: never pass Test.Steps[Index] directly into TArray::Insert: the
// insert can reallocate or shift the array and invalidate its source reference.
namespace Probe
{
    inline bool DuplicateStepSafely(UProbeTestAsset& Test, int32 Index)
    {
        if (!Test.Steps.IsValidIndex(Index)) { return false; }
        const FProbeTestStep Copy = Test.Steps[Index];
        Test.Steps.Insert(Copy, Index + 1);
        return true;
    }
}
