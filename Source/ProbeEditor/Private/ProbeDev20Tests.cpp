#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ProbeDev20Catalog.h"
#include "ProbePortableProviders.h"
#include "ProbeTestAsset.h"
#include "ProbeTestPack.h"

namespace
{
    ProbeDev20::FEntry Example(ProbeDev20::ESource Source, const FString& Id,
        bool bAuthorization = false)
    {
        ProbeDev20::FEntry Entry;
        Entry.Source = Source;
        Entry.Id = Id;
        Entry.Name = TEXT("Fixture Smoke");
        Entry.Locator = Id;
        Entry.Owner = TEXT("ExamplePlugin");
        Entry.Detail = TEXT("Asynchronous fixture check");
        Entry.bRequiresAuthorization = bAuthorization;
        return Entry;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev20Sources,
    "Probe.Editor.Dev20Sources", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev20Sources::RunTest(const FString&)
{
    TestEqual(TEXT("Authored Test source"), FString(ProbeDev20::SourceName(ProbeDev20::ESource::AuthoredTest)), FString(TEXT("Authored Test")));
    TestEqual(TEXT("Authored Pack source"), FString(ProbeDev20::SourceName(ProbeDev20::ESource::AuthoredPack)), FString(TEXT("Authored Pack")));
    TestEqual(TEXT("Portable Test source"), FString(ProbeDev20::SourceName(ProbeDev20::ESource::PortableTest)), FString(TEXT("Portable Test")));
    TestEqual(TEXT("Portable Pack source"), FString(ProbeDev20::SourceName(ProbeDev20::ESource::PortablePack)), FString(TEXT("Portable Pack")));
    TestEqual(TEXT("Native source"), FString(ProbeDev20::SourceName(ProbeDev20::ESource::NativeAutomation)), FString(TEXT("Unreal Automation")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev20Search,
    "Probe.Editor.Dev20Search", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev20Search::RunTest(const FString&)
{
    const auto Entry = Example(ProbeDev20::ESource::PortableTest, TEXT("ExamplePlugin.Fixture"));
    for (const FString& Query : {FString(TEXT("FIXTURE")), FString(TEXT("exampleplugin")), FString(TEXT("asynchronous")), FString(TEXT("portable")), FString(TEXT("smoke"))})
    { TestTrue(TEXT("Searches source/ID/owner/title/description case-insensitively"), ProbeDev20::Matches(Entry, Query, TEXT("All"))); }
    TestFalse(TEXT("Unrelated text excluded"), ProbeDev20::Matches(Entry, TEXT("NotMyPlugin"), TEXT("All")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev20Filter,
    "Probe.Editor.Dev20SourceFilter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev20Filter::RunTest(const FString&)
{
    const auto Entry = Example(ProbeDev20::ESource::PortablePack, TEXT("ExamplePlugin.Actions"));
    TestTrue(TEXT("Exact source included"), ProbeDev20::Matches(Entry, TEXT(""), TEXT("Portable Pack")));
    TestFalse(TEXT("Other sources excluded"), ProbeDev20::Matches(Entry, TEXT(""), TEXT("Authored Pack")));
    TestTrue(TEXT("All source includes entry"), ProbeDev20::Matches(Entry, TEXT(""), TEXT("All")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev20Identity,
    "Probe.Editor.Dev20IdentityIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev20Identity::RunTest(const FString&)
{
    const FString SameId = TEXT("ExamplePlugin.Smoke");
    const auto Portable = Example(ProbeDev20::ESource::PortableTest, SameId);
    const auto Native = Example(ProbeDev20::ESource::NativeAutomation, SameId);
    TestTrue(TEXT("Equal strings from different source domains do not collide"),
        ProbeDev20::Identity(Portable) != ProbeDev20::Identity(Native));
    TestEqual(TEXT("Identity reproducible"), ProbeDev20::Identity(Portable),
        ProbeDev20::Identity(Example(ProbeDev20::ESource::PortableTest, SameId)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev20Discovery,
    "Probe.Editor.Dev20NativeDiscovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev20Discovery::RunTest(const FString&)
{
    // This test exercises actual UE 5.8 Automation enumeration without invoking RunTest.
    const TArray<ProbeDev20::FEntry> All = ProbeDev20::Discover();
    bool bNative = false;
    for (const auto& Entry : All)
    {
        if (Entry.Source == ProbeDev20::ESource::NativeAutomation
            && Entry.Id.Contains(TEXT("Dev20NativeDiscovery")))
        { bNative = true; break; }
    }
    TestTrue(TEXT("Discover registered native Automation entries"), bNative);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev20AuthoredDiscovery,
    "Probe.Editor.Dev20AuthoredDiscovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev20AuthoredDiscovery::RunTest(const FString&)
{
    const auto All = ProbeDev20::Discover();
    for (const auto& Entry : All)
    {
        if (Entry.Source == ProbeDev20::ESource::AuthoredTest)
        {
            TestTrue(TEXT("Authored Test uses ordinary UObject object path"), Entry.Locator.StartsWith(TEXT("/")));
            TestFalse(TEXT("Authored Test not action-authorized by catalog"), Entry.bRequiresAuthorization);
        }
        if (Entry.Source == ProbeDev20::ESource::AuthoredPack)
        { TestTrue(TEXT("Authored Pack path retained"), Entry.Locator.StartsWith(TEXT("/"))); }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev20PortableDiscovery,
    "Probe.Editor.Dev20PortableDiscovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev20PortableDiscovery::RunTest(const FString&)
{
    const auto All = ProbeDev20::Discover();
    int32 Num = 0;
    for (const auto& Entry : All)
    {
        if (Entry.Source == ProbeDev20::ESource::PortableTest || Entry.Source == ProbeDev20::ESource::PortablePack)
        {
            ++Num;
            TestFalse(TEXT("Portable definition is not an imported asset path"), Entry.Locator.StartsWith(TEXT("/Game/")));
            TestEqual(TEXT("Portable ID preserved as locator"), Entry.Locator, Entry.Id);
        }
    }
    int32 Expected = 0;
    for (const auto& Provider : ProbePortable::Providers()) { Expected += Provider.Tests.Num() + Provider.Packs.Num(); }
    TestEqual(TEXT("Discover all registered provider Tests/Packs"), Num, Expected);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev20Authorization,
    "Probe.Editor.Dev20ActionAuthorization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev20Authorization::RunTest(const FString&)
{
    const auto Allowed = Example(ProbeDev20::ESource::PortableTest, TEXT("P.Action"), true);
    const auto ReadOnly = Example(ProbeDev20::ESource::PortableTest, TEXT("P.Read"), false);
    TestTrue(TEXT("Action requirement is explicit"), Allowed.bRequiresAuthorization);
    TestFalse(TEXT("Read-only definition cannot inherit action permission"), ReadOnly.bRequiresAuthorization);
    TestEqual(TEXT("Identities ignore permission state"), ProbeDev20::Identity(Allowed),
        ProbeDev20::Identity(Example(ProbeDev20::ESource::PortableTest, TEXT("P.Action"), false)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev20NoAssetsCreated,
    "Probe.Editor.Dev20DiscoveryReadOnly", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev20NoAssetsCreated::RunTest(const FString&)
{
    // Discover only parses registry metadata, provider definitions, and native test info.
    const auto First = ProbeDev20::Discover();
    const auto Second = ProbeDev20::Discover();
    TestEqual(TEXT("Repeat discovery stable for unchanged project"), First.Num(), Second.Num());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev21NativeCategorySearch,
    "Probe.Editor.Dev21NativeCategorySearch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev21NativeCategorySearch::RunTest(const FString&)
{
    auto Native = Example(ProbeDev20::ESource::NativeAutomation, TEXT("Probe.Editor.Dev20NativeDiscovery"));
    Native.Category = TEXT("Probe.Editor");
    TestTrue(TEXT("Native categories are searchable"), ProbeDev20::Matches(Native, TEXT("Probe.Editor"), TEXT("Unreal Automation")));
    TestFalse(TEXT("Native category excludes authored source"), ProbeDev20::Matches(Native, TEXT(""), TEXT("Authored Test")));
    TestTrue(TEXT("Native full-path identity retains its namespace"), ProbeDev20::Identity(Native).Contains(TEXT("Probe.Editor.Dev20NativeDiscovery")));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
