#include "ProbeSelfTest.h"
#include "ProbeTestAsset.h"
#include "Interfaces/IPluginManager.h"
#include "PluginDescriptor.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogProbeSelfTest, Log, All);

namespace Probe
{
    namespace
    {
        // These expectations are defined outside the normal assertion catalog and runner.
        // A test that returns an incorrect status or runs too few steps fails the gate.
        constexpr int32 RequiredCases = 15;
        constexpr int32 RequiredPasses = 6;
        constexpr int32 RequiredFailures = 4;
        constexpr int32 RequiredErrors = 3;
        constexpr int32 RequiredSkips = 2;

        struct FCaseDefinition
        {
            FString Id;
            UProbeTestAsset* Test = nullptr;
            EResultStatus Expected = EResultStatus::Error;
            int32 ExpectedSteps = 0;
            FString RequiredDiagnostic;
        };

        static void AddCase(TArray<FCaseDefinition>& Cases, const TCHAR* Id, UProbeTestAsset* Test,
            EResultStatus Expected, int32 ExpectedSteps, const TCHAR* Diagnostic)
        {
            FCaseDefinition Definition;
            Definition.Id = Id;
            Definition.Test = Test;
            Definition.Expected = Expected;
            Definition.ExpectedSteps = ExpectedSteps;
            Definition.RequiredDiagnostic = Diagnostic;
            Cases.Add(MoveTemp(Definition));
        }

        static FProbeTestStep Step(EProbeStepType Type, const TCHAR* Target = TEXT(""),
            const TCHAR* Property = TEXT(""), const TCHAR* ExpectedValue = TEXT(""))
        {
            FProbeTestStep Result;
            Result.Type = Type;
            Result.Target = Target;
            Result.PropertyName = Property;
            Result.ExpectedValue = ExpectedValue;
            return Result;
        }

        static UProbeTestAsset* NewSelfTest()
        {
            UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage(), NAME_None, RF_Transient);
            Asset->bEnabled = true;
            Asset->bNeedsConfiguration = false;
            Asset->bAllowTransientEditorActors = false;
            return Asset;
        }

        static TArray<FCaseDefinition> DefineCases()
        {
            TArray<FCaseDefinition> Cases;
            {
                UProbeTestAsset* Test = NewSelfTest();
                Test->Steps.Add(Step(EProbeStepType::PluginEnabled, TEXT("Probe")));
                AddCase(Cases, TEXT("Probe.SelfTest.Smoke.PluginEnabled"), Test, EResultStatus::Passed, 1, TEXT("enabled"));
            }
            {
                UProbeTestAsset* Test = NewSelfTest();
                Test->Steps.Add(Step(EProbeStepType::ClassHasFunction,
                    TEXT("/Script/Engine.Actor"), TEXT("GetActorLocation")));
                AddCase(Cases, TEXT("Probe.SelfTest.Smoke.ReflectionAlias"), Test, EResultStatus::Passed, 1, TEXT("K2_GetActorLocation") );
            }
            {
                UProbeTestAsset* Test = NewSelfTest();
                Test->Steps.Add(Step(EProbeStepType::FixtureCreateObject,
                    TEXT("/Script/ProbeEditor.ProbeTestAsset"), TEXT(""), TEXT("SmokeFixture")));
                Test->Steps.Add(Step(EProbeStepType::FixtureSetProperty,
                    TEXT("SmokeFixture"), TEXT("TestDescription"), TEXT("ProbeSmokeFixtureValue")));
                Test->Steps.Add(Step(EProbeStepType::FixturePropertyEquals,
                    TEXT("SmokeFixture"), TEXT("TestDescription"), TEXT("ProbeSmokeFixtureValue")));
                Test->Steps.Add(Step(EProbeStepType::FixtureDestroyObject, TEXT("SmokeFixture")));
                AddCase(Cases, TEXT("Probe.SelfTest.Smoke.FixtureRoundTrip"), Test, EResultStatus::Passed, 4, TEXT("All 4 steps") );
            }
            {
                UProbeTestAsset* Test = NewSelfTest();
                Test->Steps.Add(Step(EProbeStepType::PluginEnabled, TEXT("ProbeSelfTest_Plugin_DoesNotExist")));
                AddCase(Cases, TEXT("Probe.SelfTest.Smoke.ExpectedFailure"), Test, EResultStatus::Failed, 1, TEXT("missing or disabled") );
            }
            {
                UProbeTestAsset* Test = NewSelfTest();
                Test->Steps.Add(Step(EProbeStepType::FixtureObjectExists, TEXT("FixtureNotCreated")));
                // A well-formed exists assertion with an absent fixture returns Failed, not Error.
                AddCase(Cases, TEXT("Probe.SelfTest.Smoke.ExpectedMissingFixture"), Test, EResultStatus::Failed, 1, TEXT("does not exist") );
            }
            {
                UProbeTestAsset* Test = NewSelfTest();
                Test->Steps.Add(Step(EProbeStepType::FixtureSetProperty, TEXT("FixtureNotCreated"), TEXT("TestDescription"), TEXT("x")));
                AddCase(Cases, TEXT("Probe.SelfTest.Smoke.ExpectedError"), Test, EResultStatus::Error, 1, TEXT("not created") );
            }
            {
                UProbeTestAsset* Test = NewSelfTest();
                AddCase(Cases, TEXT("Probe.SelfTest.Smoke.EmptyTestError"), Test, EResultStatus::Error, 0, TEXT("no steps") );
            }
            {
                UProbeTestAsset* Test = NewSelfTest();
                Test->bEnabled = false;
                Test->Steps.Add(Step(EProbeStepType::PluginEnabled, TEXT("Probe")));
                AddCase(Cases, TEXT("Probe.SelfTest.Smoke.DisabledSkip"), Test, EResultStatus::Skipped, 0, TEXT("disabled") );
            }
            {
                UProbeTestAsset* Test = NewSelfTest();
                Test->bNeedsConfiguration = true;
                Test->Steps.Add(Step(EProbeStepType::PluginEnabled, TEXT("Probe")));
                AddCase(Cases, TEXT("Probe.SelfTest.Smoke.ConfigurationSkip"), Test, EResultStatus::Skipped, 0, TEXT("needs configuration") );
            }
            // 0.15.1 regression expansion. All tests remain in-memory; none use project assets,
            // the persistent Editor world, or UI input.
            {
                UProbeTestAsset* Test = NewSelfTest();
                Test->Steps.Add(Step(EProbeStepType::PluginInstalled, TEXT("Probe")));
                AddCase(Cases, TEXT("Probe.SelfTest.Smoke.PluginInstalled"), Test, EResultStatus::Passed, 1, TEXT("installed"));
            }
            {
                UProbeTestAsset* Test = NewSelfTest();
                Test->Steps.Add(Step(EProbeStepType::ClassHasProperty,
                    TEXT("/Script/Engine.Actor"), TEXT("Tags")));
                AddCase(Cases, TEXT("Probe.SelfTest.Smoke.ReflectionProperty"), Test, EResultStatus::Passed, 1, TEXT("Tags"));
            }
            {
                UProbeTestAsset* Test = NewSelfTest();
                Test->Steps.Add(Step(EProbeStepType::FixtureCreateObject,
                    TEXT("/Script/ProbeEditor.ProbeTestAsset"), TEXT(""), TEXT("InequalityFixture")));
                Test->Steps.Add(Step(EProbeStepType::FixtureSetProperty,
                    TEXT("InequalityFixture"), TEXT("TestDescription"), TEXT("ActualValue")));
                Test->Steps.Add(Step(EProbeStepType::FixturePropertyNotEquals,
                    TEXT("InequalityFixture"), TEXT("TestDescription"), TEXT("DifferentValue")));
                Test->Steps.Add(Step(EProbeStepType::FixtureDestroyObject, TEXT("InequalityFixture")));
                AddCase(Cases, TEXT("Probe.SelfTest.Smoke.FixtureInequality"), Test,
                    EResultStatus::Passed, 4, TEXT("All 4 steps"));
            }
            {
                // The fixture is intentionally left alive when the third step fails.
                // FProbeRunContext must release it even on early exit.
                UProbeTestAsset* Test = NewSelfTest();
                Test->Steps.Add(Step(EProbeStepType::FixtureCreateObject,
                    TEXT("/Script/ProbeEditor.ProbeTestAsset"), TEXT(""), TEXT("FailedFixture")));
                Test->Steps.Add(Step(EProbeStepType::FixtureSetProperty,
                    TEXT("FailedFixture"), TEXT("TestDescription"), TEXT("ActualValue")));
                Test->Steps.Add(Step(EProbeStepType::FixturePropertyEquals,
                    TEXT("FailedFixture"), TEXT("TestDescription"), TEXT("WrongValue")));
                AddCase(Cases, TEXT("Probe.SelfTest.Smoke.FixtureValueMismatch"), Test,
                    EResultStatus::Failed, 3, TEXT("WrongValue"));
            }
            {
                // An assertion after the first failure must never execute.
                UProbeTestAsset* Test = NewSelfTest();
                Test->Steps.Add(Step(EProbeStepType::PluginEnabled,
                    TEXT("ProbeSelfTest_Plugin_DoesNotExist")));
                Test->Steps.Add(Step(EProbeStepType::PluginEnabled, TEXT("Probe")));
                AddCase(Cases, TEXT("Probe.SelfTest.Smoke.StopAfterFailure"), Test,
                    EResultStatus::Failed, 1, TEXT("missing or disabled"));
            }
            {
                // A missing extension must be an Error, not an accidental built-in Pass.
                UProbeTestAsset* Test = NewSelfTest();
                FProbeTestStep MissingProvider = Step(EProbeStepType::PluginEnabled, TEXT("Probe"));
                MissingProvider.AssertionId = FName(TEXT("ProbeSelfTest.MissingProvider_0151"));
                Test->Steps.Add(MoveTemp(MissingProvider));
                AddCase(Cases, TEXT("Probe.SelfTest.Smoke.MissingProvider"), Test,
                    EResultStatus::Error, 1, TEXT("unavailable"));
            }
            return Cases;
        }

        static TSharedRef<FJsonObject> SerializeCase(const FSelfTestCaseResult& Case)
        {
            TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
            Object->SetStringField(TEXT("id"), Case.CaseId);
            Object->SetStringField(TEXT("expected"), StatusName(Case.ExpectedStatus));
            Object->SetStringField(TEXT("actual"), StatusName(Case.ObservedStatus));
            Object->SetNumberField(TEXT("expectedSteps"), Case.ExpectedStepCount);
            Object->SetNumberField(TEXT("executedSteps"), Case.ObservedStepCount);
            Object->SetBoolField(TEXT("oraclePassed"), Case.bOraclePassed);
            Object->SetStringField(TEXT("oracleDetail"), Case.OracleDetail);
            Object->SetStringField(TEXT("runnerDetail"), Case.ProbeResult.Detail);
            Object->SetNumberField(TEXT("durationMs"), Case.ProbeResult.DurationMs);
            TArray<TSharedPtr<FJsonValue>> StepJson;
            for (const FStepResult& StepResult : Case.ProbeResult.Steps)
            {
                TSharedRef<FJsonObject> StepObject = MakeShared<FJsonObject>();
                StepObject->SetNumberField(TEXT("index"), StepResult.Index);
                StepObject->SetStringField(TEXT("status"), StatusName(StepResult.Status));
                StepObject->SetStringField(TEXT("assertionId"), StepResult.AssertionId);
                StepObject->SetStringField(TEXT("detail"), StepResult.Detail);
                StepJson.Add(MakeShared<FJsonValueObject>(StepObject));
            }
            Object->SetArrayField(TEXT("steps"), StepJson);
            return Object;
        }

        static bool WriteJson(FSelfTestReport& Report)
        {
            const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Probe"), TEXT("SelfTests"));
            if (!IFileManager::Get().MakeDirectory(*Folder, true)) { return false; }
            const FString Filename = FString::Printf(TEXT("Probe-SelfTest-%s.json"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")));
            const FString FullPath = FPaths::Combine(Folder, Filename);
            TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
            Root->SetStringField(TEXT("suiteId"), Report.SuiteId);
            Root->SetStringField(TEXT("timestampUtc"), FDateTime::UtcNow().ToIso8601());
            Root->SetStringField(TEXT("unrealVersion"), FEngineVersion::Current().ToString());
            const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Probe"));
            Root->SetStringField(TEXT("probeVersion"), Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : TEXT("Unknown"));
            Root->SetStringField(TEXT("verificationLayer"), TEXT("Probe-authored in-memory smoke cases with an independent outcome oracle"));
            Root->SetBoolField(TEXT("gatePassed"), Report.bGatePassed);
            Root->SetStringField(TEXT("gateDetail"), Report.GateDetail);
            Root->SetNumberField(TEXT("requiredCases"), RequiredCases);
            Root->SetNumberField(TEXT("executedCases"), Report.Cases.Num());
            Root->SetNumberField(TEXT("expectedPassCount"), RequiredPasses);
            Root->SetNumberField(TEXT("expectedFailCount"), RequiredFailures);
            Root->SetNumberField(TEXT("expectedErrorCount"), RequiredErrors);
            Root->SetNumberField(TEXT("expectedSkipCount"), RequiredSkips);
            Root->SetNumberField(TEXT("actualPassCount"), Report.PassedOutcomes);
            Root->SetNumberField(TEXT("actualFailCount"), Report.FailedOutcomes);
            Root->SetNumberField(TEXT("actualErrorCount"), Report.ErrorOutcomes);
            Root->SetNumberField(TEXT("actualSkipCount"), Report.SkippedOutcomes);
            Root->SetNumberField(TEXT("oracleFailures"), Report.OracleFailures);
            TArray<TSharedPtr<FJsonValue>> CasesJson;
            for (const FSelfTestCaseResult& Case : Report.Cases)
            { CasesJson.Add(MakeShared<FJsonValueObject>(SerializeCase(Case))); }
            Root->SetArrayField(TEXT("cases"), CasesJson);
            FString Json;
            const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
            if (!FJsonSerializer::Serialize(Root, Writer) || !FFileHelper::SaveStringToFile(Json, *FullPath)) { return false; }
            Report.SavedReportPath = FullPath;
            return true;
        }
    }

    FSelfTestReport RunSelfTestSmoke(bool bWriteReport)
    {
        FSelfTestReport Report;
        if (!IsInGameThread())
        {
            Report.GateDetail = TEXT("Probe Self-Test must run on the Editor game thread.");
            return Report;
        }
        const TArray<FCaseDefinition> Definitions = DefineCases();
        TSet<FString> Seen;
        for (const FCaseDefinition& Definition : Definitions)
        {
            FSelfTestCaseResult Case;
            Case.CaseId = Definition.Id;
            Case.ExpectedStatus = Definition.Expected;
            Case.ExpectedStepCount = Definition.ExpectedSteps;
            Case.ProbeResult = RunTest(Definition.Test);
            Case.ObservedStatus = Case.ProbeResult.Status;
            Case.ObservedStepCount = Case.ProbeResult.Steps.Num();
            const bool bStatus = Case.ObservedStatus == Case.ExpectedStatus;
            const bool bSteps = Case.ObservedStepCount == Definition.ExpectedSteps;
            const bool bDescription = Case.ProbeResult.Detail.Contains(Definition.RequiredDiagnostic, ESearchCase::IgnoreCase)
                || (Case.ProbeResult.Steps.Num() > 0
                    && Case.ProbeResult.Steps.Last().Detail.Contains(Definition.RequiredDiagnostic, ESearchCase::IgnoreCase));
            bool bStepStatusesConsistent = true;
            for (int32 StepIndex = 0; StepIndex < Case.ProbeResult.Steps.Num(); ++StepIndex)
            {
                const FStepResult& StepResult = Case.ProbeResult.Steps[StepIndex];
                bStepStatusesConsistent &= StepResult.Index == StepIndex + 1;
                if (StepIndex + 1 < Case.ProbeResult.Steps.Num())
                {
                    bStepStatusesConsistent &= StepResult.Status == EResultStatus::Passed;
                }
                else
                {
                    bStepStatusesConsistent &= StepResult.Status == Case.ObservedStatus;
                }
            }
            const bool bUnique = !Seen.Contains(Case.CaseId);
            Seen.Add(Case.CaseId);
            Case.bOraclePassed = bStatus && bSteps && bDescription && bStepStatusesConsistent && bUnique;
            if (!Case.bOraclePassed)
            {
                Case.OracleDetail = FString::Printf(TEXT("status %s/%s; steps %d/%d; diagnostic %s; unique ID %s"),
                    StatusName(Case.ObservedStatus), StatusName(Case.ExpectedStatus),
                    Case.ObservedStepCount, Case.ExpectedStepCount,
                    bDescription ? TEXT("ok") : TEXT("missing"), bUnique ? TEXT("yes") : TEXT("no"));
                if (!bStepStatusesConsistent)
                { Case.OracleDetail += TEXT("; step indices/statuses inconsistent"); }
                ++Report.OracleFailures;
            }
            else { Case.OracleDetail = TEXT("Expected outcome, executed step count, and diagnostic verified."); }
            switch (Case.ObservedStatus)
            {
            case EResultStatus::Passed: ++Report.PassedOutcomes; break;
            case EResultStatus::Failed: ++Report.FailedOutcomes; break;
            case EResultStatus::Error: ++Report.ErrorOutcomes; break;
            case EResultStatus::Skipped: ++Report.SkippedOutcomes; break;
            }
            Report.Cases.Add(MoveTemp(Case));
        }
        Report.bGatePassed = Report.Cases.Num() == RequiredCases && Seen.Num() == RequiredCases
            && Report.PassedOutcomes == RequiredPasses && Report.FailedOutcomes == RequiredFailures
            && Report.ErrorOutcomes == RequiredErrors && Report.SkippedOutcomes == RequiredSkips
            && Report.OracleFailures == 0;
        Report.GateDetail = FString::Printf(TEXT("%s: %d/%d cases; PASS=%d FAIL=%d ERROR=%d SKIP=%d; oracle mismatches=%d"),
            Report.bGatePassed ? TEXT("SELF-TEST GATE PASSED") : TEXT("SELF-TEST GATE FAILED"),
            Report.Cases.Num(), RequiredCases, Report.PassedOutcomes, Report.FailedOutcomes,
            Report.ErrorOutcomes, Report.SkippedOutcomes, Report.OracleFailures);
        if (bWriteReport && !WriteJson(Report))
        {
            Report.bGatePassed = false;
            Report.GateDetail += TEXT("; report could not be saved");
        }
        return Report;
    }

    void RunSelfTestConsoleCommand()
    {
        const FSelfTestReport Report = RunSelfTestSmoke();
        for (const FSelfTestCaseResult& Case : Report.Cases)
        {
            if (Case.bOraclePassed)
            {
                UE_LOG(LogProbeSelfTest, Display, TEXT("[OK] %s: observed %s (expected %s), %d steps"),
                    *Case.CaseId, StatusName(Case.ObservedStatus), StatusName(Case.ExpectedStatus), Case.ObservedStepCount);
            }
            else
            {
                UE_LOG(LogProbeSelfTest, Error, TEXT("[MISMATCH] %s: %s"), *Case.CaseId, *Case.OracleDetail);
            }
        }
        if (Report.bGatePassed)
        {
            UE_LOG(LogProbeSelfTest, Display, TEXT("%s"), *Report.GateDetail);
        }
        else
        {
            UE_LOG(LogProbeSelfTest, Error, TEXT("%s"), *Report.GateDetail);
        }
        if (!Report.SavedReportPath.IsEmpty())
        { UE_LOG(LogProbeSelfTest, Display, TEXT("JSON report: %s"), *Report.SavedReportPath); }
    }
}
