#include "ProbeDev37ProviderAudit.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
ProbePortable::FPortableProvider Provider(const TCHAR* Name)
{
    ProbePortable::FPortableProvider P;
    P.PluginName = Name;
    P.ProviderId = Name;
    P.Version = TEXT("1.0");
    return P;
}
ProbePortable::FPortableTest Test(const TCHAR* Id, bool bAction = false)
{
    ProbePortable::FPortableTest T;
    T.Id = Id;
    T.Name = Id;
    T.bRequiresActionAuthorization = bAction;
    return T;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev37Empty, "Probe.Editor.Dev37EmptyRegistry", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev37Empty::RunTest(const FString&)
{
    const ProbeDev37::FReport R = ProbeDev37::Audit({}, {});
    TestEqual(TEXT("No fictitious providers"), R.Providers, 0);
    TestEqual(TEXT("No blockers for deliberately empty registry"), R.Blockers, 0);
    TestEqual(TEXT("No execution inferred"), R.Deferred, 0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev37Valid, "Probe.Editor.Dev37ValidAssetFreeProvider", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev37Valid::RunTest(const FString&)
{
    auto P = Provider(TEXT("Example"));
    P.Tests.Add(Test(TEXT("Example.Smoke")));
    ProbePortable::FPortablePack Pack; Pack.Id = TEXT("Example.All"); P.Packs.Add(Pack);
    const ProbeDev37::FReport R = ProbeDev37::Audit({P}, {});
    TestEqual(TEXT("One plugin"), R.Providers, 1);
    TestEqual(TEXT("One portable test"), R.Tests, 1);
    TestEqual(TEXT("One portable pack"), R.Packs, 1);
    TestEqual(TEXT("No structure problems"), R.Blockers, 0);
    TestEqual(TEXT("Execution unverified"), R.Deferred, 1);
    TestTrue(TEXT("Never claim executed"), R.ToText().Contains(TEXT("EXECUTION")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev37Collision, "Probe.Editor.Dev37CrossProviderIdCollision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev37Collision::RunTest(const FString&)
{
    auto A = Provider(TEXT("Example")); auto B = Provider(TEXT("Other"));
    A.Tests.Add(Test(TEXT("Example.Test")));
    B.Tests.Add(Test(TEXT("Example.Test")));
    const auto R = ProbeDev37::Audit({A,B}, {});
    TestTrue(TEXT("Cross-plugin ID claim blocked"), R.Blockers >= 2);
    TestTrue(TEXT("Explicit duplicate"), R.ToText().Contains(TEXT("DUPLICATE_DEFINITION")));
    TestTrue(TEXT("Wrong owner"), R.ToText().Contains(TEXT("ID_OWNERSHIP")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev37Ownership, "Probe.Editor.Dev37ProviderOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev37Ownership::RunTest(const FString&)
{
    auto P = Provider(TEXT("Example")); P.ProviderId = TEXT("Different");
    const auto R = ProbeDev37::Audit({P}, {});
    TestEqual(TEXT("Mismatched descriptor blocked"), R.Blockers, 1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev37Duplicates, "Probe.Editor.Dev37DuplicateProviders", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev37Duplicates::RunTest(const FString&)
{
    const auto P = Provider(TEXT("Example"));
    const auto R = ProbeDev37::Audit({P,P}, {});
    TestEqual(TEXT("Second owner blocked"), R.Blockers, 1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev37Issues, "Probe.Editor.Dev37DiscoveryFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev37Issues::RunTest(const FString&)
{
    const auto R = ProbeDev37::Audit({}, {TEXT("Example: invalid manifest")});
    TestEqual(TEXT("Reject incomplete discovery"), R.Blockers, 1);
    TestTrue(TEXT("Explain cause"), R.ToText().Contains(TEXT("invalid manifest")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev37Authorization, "Probe.Editor.Dev37AuthorizationDeferred", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev37Authorization::RunTest(const FString&)
{
    auto P = Provider(TEXT("Example")); P.Tests.Add(Test(TEXT("Example.Action"), true));
    const auto R = ProbeDev37::Audit({P}, {});
    TestEqual(TEXT("Action test remains deferred"), R.Deferred, 1);
    TestTrue(TEXT("Permission explicit"), R.ToText().Contains(TEXT("explicit action permission")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev37Stable, "Probe.Editor.Dev37ReadOnlyStableReport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev37Stable::RunTest(const FString&)
{
    auto A = Provider(TEXT("Zed")); auto B = Provider(TEXT("Alpha"));
    A.Tests.Add(Test(TEXT("Zed.Smoke"))); B.Tests.Add(Test(TEXT("Alpha.Smoke")));
    const auto First = ProbeDev37::Audit({A,B}, {});
    const auto Second = ProbeDev37::Audit({B,A}, {});
    TestEqual(TEXT("Deterministic independent of enumeration order"), First.ToText(), Second.ToText());
    TestEqual(TEXT("No false blockers"), First.Blockers, 0);
    return true;
}
#endif
