#pragma once

#include "CoreMinimal.h"
#include "ProbeTestRunner.h"

namespace Probe
{
    /** Probe's first self-hosted, in-memory regression pack. The cases do not create project assets. */
    struct FSelfTestCaseResult
    {
        FString CaseId;
        EResultStatus ExpectedStatus = EResultStatus::Error;
        EResultStatus ObservedStatus = EResultStatus::Error;
        int32 ExpectedStepCount = 0;
        int32 ObservedStepCount = 0;
        bool bOraclePassed = false;
        FString OracleDetail;
        FTestResult ProbeResult;
    };

    struct FSelfTestReport
    {
        FString SuiteId = TEXT("Probe.SelfTest.Smoke.v2");
        TArray<FSelfTestCaseResult> Cases;
        int32 PassedOutcomes = 0;
        int32 FailedOutcomes = 0;
        int32 ErrorOutcomes = 0;
        int32 SkippedOutcomes = 0;
        int32 OracleFailures = 0;
        bool bGatePassed = false;
        FString GateDetail;
        FString SavedReportPath;
    };

    /** Composes normal Probe Test Assets, executes them with Probe, then checks outcomes independently. */
    PROBEEDITOR_API FSelfTestReport RunSelfTestSmoke(bool bWriteReport = true);
    /** Console entry point: Probe.SelfTest */
    PROBEEDITOR_API void RunSelfTestConsoleCommand();
}
