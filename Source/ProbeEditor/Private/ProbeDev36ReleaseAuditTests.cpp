#include "ProbeDev36ReleaseAudit.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
FProbePackMixedEntry Dev36Native(const TCHAR* Locator)
{
    FProbePackMixedEntry M;
    M.Source = EProbePackMixedSource::NativeAutomation;
    M.Locator = Locator;
    return M;
}
FProbePackMixedEntry Dev36Authored(const TCHAR* Locator)
{
    FProbePackMixedEntry M;
    M.Source = EProbePackMixedSource::AuthoredTest;
    M.Locator = Locator;
    return M;
}
UProbeTestPack* Dev36Pack()
{
    return NewObject<UProbeTestPack>(GetTransientPackage());
}
ProbeDev36::FReport Dev36Audit(const UProbeTestPack& Pack, ProbeDev36::EAvailability Availability)
{
    return ProbeDev36::Audit(Pack, [Availability](const FProbePackMixedEntry&) { return Availability; });
}
bool Dev36Has(const ProbeDev36::FReport& Report, const TCHAR* Code)
{
    for (const ProbeDev36::FFinding& F : Report.Findings) { if (F.Code == Code) { return true; } }
    return false;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev36EmptyPack, "Probe.Editor.Dev36EmptyPack", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev36EmptyPack::RunTest(const FString&)
{
    const ProbeDev36::FReport R = Dev36Audit(*Dev36Pack(), ProbeDev36::EAvailability::Available);
    TestFalse(TEXT("Empty is not releasable"), R.IsStructurallyReady());
    TestTrue(TEXT("Explicit blocker"), Dev36Has(R, TEXT("EMPTY_PACK")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev36ValidMixed, "Probe.Editor.Dev36ValidMixed", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev36ValidMixed::RunTest(const FString&)
{
    UProbeTestPack* P = Dev36Pack();
    P->MixedEntries = {Dev36Native(TEXT("Probe.A")), Dev36Native(TEXT("Probe.B"))};
    P->MixedEntries[1].Requires.Add(ProbeDev27::Identity(P->MixedEntries[0]));
    const ProbeDev36::FReport R = Dev36Audit(*P, ProbeDev36::EAvailability::Deferred);
    TestTrue(TEXT("Definition gate passes"), R.IsStructurallyReady());
    TestEqual(TEXT("Two deferred dispatch checks"), R.Deferred, 2);
    TestEqual(TEXT("One link"), R.PrerequisiteLinks, 1);
    TestTrue(TEXT("Pending is not incorrectly reported passed"), R.ToText().Contains(TEXT("NOT VERIFIED")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev36Duplicate, "Probe.Editor.Dev36Duplicate", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev36Duplicate::RunTest(const FString&)
{
    UProbeTestPack* P = Dev36Pack();
    P->MixedEntries = {Dev36Native(TEXT("Probe.A")), Dev36Native(TEXT("Probe.A"))};
    const ProbeDev36::FReport R = Dev36Audit(*P, ProbeDev36::EAvailability::Deferred);
    TestTrue(TEXT("Duplicate detected"), Dev36Has(R, TEXT("DUPLICATE_MEMBER")));
    TestFalse(TEXT("Blocks release"), R.IsStructurallyReady());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev36Dependency, "Probe.Editor.Dev36Dependency", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev36Dependency::RunTest(const FString&)
{
    UProbeTestPack* P = Dev36Pack();
    P->MixedEntries = {Dev36Native(TEXT("Probe.A")), Dev36Native(TEXT("Probe.B"))};
    P->MixedEntries[0].Requires.Add(ProbeDev27::Identity(P->MixedEntries[1]));
    P->MixedEntries[1].Requires.Add(TEXT("99:missing"));
    const ProbeDev36::FReport R = Dev36Audit(*P, ProbeDev36::EAvailability::Deferred);
    TestEqual(TEXT("Two bad prerequisite links"), R.PrerequisiteLinks, 2);
    TestTrue(TEXT("Bad links found"), Dev36Has(R, TEXT("INVALID_PREREQUISITE")));
    TestFalse(TEXT("Blocks release"), R.IsStructurallyReady());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev36AuthoredAvailability, "Probe.Editor.Dev36AuthoredAvailability", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev36AuthoredAvailability::RunTest(const FString&)
{
    UProbeTestPack* P = Dev36Pack();
    P->MixedEntries = {Dev36Authored(TEXT("/Game/Test.PT_Test"))};
    const ProbeDev36::FReport Missing = Dev36Audit(*P, ProbeDev36::EAvailability::Missing);
    TestTrue(TEXT("Missing authored target diagnosed"), Dev36Has(Missing, TEXT("MISSING_TARGET")));
    const ProbeDev36::FReport Needs = Dev36Audit(*P, ProbeDev36::EAvailability::NeedsConfiguration);
    TestTrue(TEXT("Needs configuration diagnosed"), Dev36Has(Needs, TEXT("NEEDS_CONFIGURATION")));
    const ProbeDev36::FReport Ready = Dev36Audit(*P, ProbeDev36::EAvailability::Available);
    TestEqual(TEXT("Locally validated authored"), Ready.Validated, 1);
    TestTrue(TEXT("Ready definition"), Ready.IsStructurallyReady());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev36Legacy, "Probe.Editor.Dev36Legacy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev36Legacy::RunTest(const FString&)
{
    UProbeTestPack* P = Dev36Pack();
    P->Tests.Add(TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(TEXT("/Game/Tests/PT_A.PT_A"))));
    P->Tests.Add(TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(TEXT("/Game/Tests/PT_B.PT_B"))));
    const ProbeDev36::FReport R = Dev36Audit(*P, ProbeDev36::EAvailability::Available);
    TestTrue(TEXT("Legacy mode retained"), R.bLegacy);
    TestTrue(TEXT("Two legacy refs"), R.Members == 2);
    TestEqual(TEXT("No blockers"), R.Blockers, 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev36MixedConflict, "Probe.Editor.Dev36MixedConflict", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev36MixedConflict::RunTest(const FString&)
{
    UProbeTestPack* P = Dev36Pack();
    P->MixedEntries.Add(Dev36Native(TEXT("Probe.A")));
    P->Tests.Add(TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(TEXT("/Game/Tests/PT_A.PT_A"))));
    const ProbeDev36::FReport R = Dev36Audit(*P, ProbeDev36::EAvailability::Deferred);
    TestTrue(TEXT("Mixed/legacy conflict diagnosed"), Dev36Has(R, TEXT("MIXED_LEGACY_CONFLICT")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev36ReadOnlyStable, "Probe.Editor.Dev36ReadOnlyStable", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev36ReadOnlyStable::RunTest(const FString&)
{
    UProbeTestPack* P = Dev36Pack();
    P->MixedEntries = {Dev36Native(TEXT("Probe.A")), Dev36Native(TEXT("Probe.B"))};
    const TArray<FProbePackMixedEntry> Snapshot = P->MixedEntries;
    const FString A = Dev36Audit(*P, ProbeDev36::EAvailability::Deferred).ToText();
    const FString B = Dev36Audit(*P, ProbeDev36::EAvailability::Deferred).ToText();
    TestEqual(TEXT("Deterministic text"), A, B);
    TestTrue(TEXT("Definition retained"), P->MixedEntries.Num() == Snapshot.Num() && P->MixedEntries[0].Locator == Snapshot[0].Locator);
    TestTrue(TEXT("Report includes code"), A.Contains(TEXT("DISPATCH_CHECK_PENDING")));
    return true;
}
#endif
