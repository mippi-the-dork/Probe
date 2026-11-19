#include "ProbeDev27MixedPolicy.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    FProbePackMixedEntry Native(const TCHAR* Path)
    {
        FProbePackMixedEntry Entry;
        Entry.Source = EProbePackMixedSource::NativeAutomation;
        Entry.Locator = Path;
        return Entry;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev28MixedDependencyOrdering,
    "Probe.Editor.Dev28DependencyOrdering", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev28MixedDependencyOrdering::RunTest(const FString&)
{
    UProbeTestPack* Pack = NewObject<UProbeTestPack>(GetTransientPackage());
    Pack->MixedEntries.Add(Native(TEXT("Probe.A")));
    Pack->MixedEntries.Add(Native(TEXT("Probe.B")));
    Pack->MixedEntries[1].Requires.Add(ProbeDev27::Identity(Pack->MixedEntries[0]));
    FString Error;
    TestTrue(TEXT("Earlier prerequisite valid"), ProbeDev27::ValidatePack(*Pack, Error));
    Pack->MixedEntries.Swap(0, 1);
    TestFalse(TEXT("Reordering before prerequisite is rejected"), ProbeDev27::ValidatePack(*Pack, Error));
    TestTrue(TEXT("Reports prerequisite ordering"), Error.Contains(TEXT("prerequisite"), ESearchCase::IgnoreCase));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev28MixedDependencyIdentity,
    "Probe.Editor.Dev28DependencyIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev28MixedDependencyIdentity::RunTest(const FString&)
{
    UProbeTestPack* Pack = NewObject<UProbeTestPack>(GetTransientPackage());
    Pack->MixedEntries.Add(Native(TEXT("Probe.A")));
    Pack->MixedEntries.Add(Native(TEXT("Probe.B")));
    FString Error;
    Pack->MixedEntries[1].Requires.Add(ProbeDev27::Identity(Pack->MixedEntries[1]));
    TestFalse(TEXT("Self dependency blocked"), ProbeDev27::ValidatePack(*Pack, Error));
    Pack->MixedEntries[1].Requires[0] = TEXT("99:missing");
    TestFalse(TEXT("Missing typed identity blocked"), ProbeDev27::ValidatePack(*Pack, Error));
    Pack->MixedEntries[1].Requires[0] = ProbeDev27::Identity(Pack->MixedEntries[0]);
    const FString DuplicatePrerequisite = Pack->MixedEntries[1].Requires[0];
    Pack->MixedEntries[1].Requires.Add(DuplicatePrerequisite);
    TestFalse(TEXT("Duplicate prerequisite blocked"), ProbeDev27::ValidatePack(*Pack, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev28MixedDependencyOutcome,
    "Probe.Editor.Dev28DependencyOutcome", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev28MixedDependencyOutcome::RunTest(const FString&)
{
    FProbePackMixedEntry First = Native(TEXT("Probe.A"));
    FProbePackMixedEntry Second = Native(TEXT("Probe.B"));
    Second.Requires.Add(ProbeDev27::Identity(First));
    TMap<FString, bool> Outcomes;
    TestEqual(TEXT("Undispatched prerequisite prevents execution"), ProbeDev27::FailedDependency(Second, Outcomes), ProbeDev27::Identity(First));
    Outcomes.Add(ProbeDev27::Identity(First), false);
    TestEqual(TEXT("Failed prerequisite prevents execution"), ProbeDev27::FailedDependency(Second, Outcomes), ProbeDev27::Identity(First));
    Outcomes.Add(ProbeDev27::Identity(First), true);
    TestTrue(TEXT("Passed prerequisite allows execution"), ProbeDev27::FailedDependency(Second, Outcomes).IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev28MixedDependencyMultiple,
    "Probe.Editor.Dev28DependencyMultiple", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev28MixedDependencyMultiple::RunTest(const FString&)
{
    FProbePackMixedEntry A = Native(TEXT("Probe.A"));
    FProbePackMixedEntry B = Native(TEXT("Probe.B"));
    FProbePackMixedEntry C = Native(TEXT("Probe.C"));
    C.Requires = {ProbeDev27::Identity(A), ProbeDev27::Identity(B)};
    UProbeTestPack* Pack = NewObject<UProbeTestPack>(GetTransientPackage());
    Pack->MixedEntries = {A,B,C};
    FString Error;
    TestTrue(TEXT("Multiple earlier prerequisites valid"), ProbeDev27::ValidatePack(*Pack, Error));
    TMap<FString, bool> Outcomes;
    Outcomes.Add(ProbeDev27::Identity(A), true);
    Outcomes.Add(ProbeDev27::Identity(B), false);
    TestEqual(TEXT("Second failed prerequisite blocks dependent"), ProbeDev27::FailedDependency(C, Outcomes), ProbeDev27::Identity(B));
    Outcomes.Add(ProbeDev27::Identity(B), true);
    TestTrue(TEXT("All prerequisites passed"), ProbeDev27::FailedDependency(C, Outcomes).IsEmpty());
    return true;
}
#endif
