#pragma once

#include "CoreMinimal.h"
#include "ProbeTestPack.h"
#include "ProbeDev27MixedPolicy.h"
#include "ProbeTestRunner.h"

// Pure mapping policy shared by Slate and native automation tests. A member is
// not necessarily a single result: portable Pack members may emit many results.
namespace ProbeDev31
{
inline int32 ResultOffset(const TArray<FProbePackMixedEntry>& Ordered,
    const TMap<FString, TArray<Probe::FTestResult>>& MemberResults, int32 MemberIndex)
{
    if (!Ordered.IsValidIndex(MemberIndex)) { return INDEX_NONE; }
    int32 Offset = 0;
    for (int32 Index = 0; Index <= MemberIndex; ++Index)
    {
        const TArray<Probe::FTestResult>* Group = MemberResults.Find(ProbeDev27::Identity(Ordered[Index]));
        if (!Group || Group->IsEmpty()) { return INDEX_NONE; }
        if (Index == MemberIndex) { return Offset; }
        Offset += Group->Num();
    }
    return INDEX_NONE;
}
inline bool IsInspectable(const TArray<FProbePackMixedEntry>& Ordered,
    const TMap<FString, TArray<Probe::FTestResult>>& MemberResults, int32 MemberIndex)
{
    return ResultOffset(Ordered, MemberResults, MemberIndex) != INDEX_NONE;
}
inline FString MemberSummary(const TArray<Probe::FTestResult>* Group)
{
    if (!Group || Group->IsEmpty()) { return TEXT("This member has no completed results yet."); }
    FString Text = FString::Printf(TEXT("%d result(s) recorded"), Group->Num());
    for (const Probe::FTestResult& Result : *Group)
    {
        Text += FString::Printf(TEXT("\n[%s] %s (%.2f ms)"),
            Probe::StatusName(Result.Status), *Result.Name, Result.DurationMs);
        if (!Result.Detail.IsEmpty()) { Text += TEXT("\n") + Result.Detail; }
    }
    return Text;
}
}
