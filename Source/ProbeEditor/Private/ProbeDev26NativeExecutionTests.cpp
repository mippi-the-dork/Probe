#include "ProbeDev26NativeExecution.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev26SelectionSafety,
    "Probe.Editor.Dev26ExactSelectionSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev26SelectionSafety::RunTest(const FString&)
{
    TestTrue(TEXT("Exactly one matching path"), ProbeDev26::FDecision::ExactSelection({TEXT("Probe.Editor.One")}, TEXT("Probe.Editor.One")));
    TestFalse(TEXT("Empty selection rejected"), ProbeDev26::FDecision::ExactSelection({}, TEXT("Probe.Editor.One")));
    TestFalse(TEXT("Extra test rejected"), ProbeDev26::FDecision::ExactSelection({TEXT("Probe.Editor.One"),TEXT("Probe.Editor.Two")}, TEXT("Probe.Editor.One")));
    TestFalse(TEXT("Prefix rejected"), ProbeDev26::FDecision::ExactSelection({TEXT("Probe.Editor.OneExtra")}, TEXT("Probe.Editor.One")));
    TestFalse(TEXT("Case mismatch rejected"), ProbeDev26::FDecision::ExactSelection({TEXT("Probe.Editor.one")}, TEXT("Probe.Editor.One")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev26StateSafety,
    "Probe.Editor.Dev26ControllerStateSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev26StateSafety::RunTest(const FString&)
{
    TestFalse(TEXT("Controller in use is never taken over"), ProbeDev26::FDecision::CanStart(EAutomationControllerModuleState::Running));
    TestTrue(TEXT("Ready allows dispatch"), ProbeDev26::FDecision::CanStart(EAutomationControllerModuleState::Ready));
    TestTrue(TEXT("Disabled allows local worker discovery"), ProbeDev26::FDecision::CanStart(EAutomationControllerModuleState::Disabled));
    TestFalse(TEXT("Discovery is not a result"), ProbeDev26::FDecision::IsTerminal(ProbeDev26::EPhase::DiscoveringTests));
    TestFalse(TEXT("Running is not completion"), ProbeDev26::FDecision::IsTerminal(ProbeDev26::EPhase::Running));
    TestTrue(TEXT("Controller completion is terminal"), ProbeDev26::FDecision::IsTerminal(ProbeDev26::EPhase::Complete));
    TestTrue(TEXT("User stop is terminal"), ProbeDev26::FDecision::IsTerminal(ProbeDev26::EPhase::Interrupted));
    TestTrue(TEXT("Controller failure is terminal"), ProbeDev26::FDecision::IsTerminal(ProbeDev26::EPhase::Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev26DispatchSafety,
    "Probe.Editor.Dev26DispatchInputSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev26DispatchSafety::RunTest(const FString&)
{
    ProbeDev26::FNativeExecution Dispatch;
    FString Error;
    TestFalse(TEXT("Empty native path cannot dispatch"), Dispatch.Start(FString(), Error));
    TestFalse(TEXT("An error is returned"), Error.IsEmpty());
    TestTrue(TEXT("No run claimed"), Dispatch.Phase() == ProbeDev26::EPhase::Idle && !Dispatch.OwnsRun());
    Dispatch.Stop();
    TestTrue(TEXT("Stop on idle is harmless"), Dispatch.Phase() == ProbeDev26::EPhase::Idle);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev26UnfilteredDiscovery,
    "Probe.Editor.Dev26UnfilteredDiscovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev26UnfilteredDiscovery::RunTest(const FString&)
{
    using ProbeDev26::FDecision;
    TestFalse(TEXT("Do not probe before requesting a fresh test list"), FDecision::ShouldProbeSelection(false, true, 60.0));
    TestFalse(TEXT("Do not probe before refresh or retry interval"), FDecision::ShouldProbeSelection(true, false, 0.1));
    TestTrue(TEXT("Controller test refresh triggers immediate exact-selection probe even when the filtered tree is empty"), FDecision::ShouldProbeSelection(true, true, 0.1));
    TestTrue(TEXT("Periodic exact-selection probe works without refresh callback"), FDecision::ShouldProbeSelection(true, false, 0.5));
    TestFalse(TEXT("No filtered-list requirement; incorrect enabled identity is rejected"), FDecision::ExactSelection({TEXT("Different.Test")}, TEXT("Probe.Editor.Dev23NativeCompletionParsing")));
    TestTrue(TEXT("Exact enabled identity remains the only dispatch authority"), FDecision::ExactSelection({TEXT("Probe.Editor.Dev23NativeCompletionParsing")}, TEXT("Probe.Editor.Dev23NativeCompletionParsing")));
    return true;
}
#endif
