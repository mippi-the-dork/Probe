#include "ProbeDev38ProviderPresentation.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
    ProbePortable::FPortableProvider MakeProvider(const TCHAR* Name)
    {
        ProbePortable::FPortableProvider P;
        P.PluginName = Name;
        P.ProviderId = Name;
        P.Version = TEXT("1.0");
        return P;
    }
    ProbePortable::FPortableTest MakeTest(const TCHAR* Id, const TCHAR* Name)
    {
        ProbePortable::FPortableTest T;
        T.Id = Id;
        T.Name = Name;
        return T;
    }
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev38Empty, "Probe.Editor.Dev38EmptyBrowser", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev38Empty::RunTest(const FString&)
{
    const auto C = ProbeDev38::Count({}, {}, TEXT(""));
    TestEqual(TEXT("No invented provider"), C.Providers, 0);
    TestEqual(TEXT("No invented rows"), C.VisibleDefinitions, 0);
    TestTrue(TEXT("Empty summary explicit"), ProbeDev38::Summary(C, false).Contains(TEXT("0 providers")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev38Case, "Probe.Editor.Dev38CaseInsensitiveSearch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev38Case::RunTest(const FString&)
{
    TestTrue(TEXT("Search by test ID"), ProbeDev38::Matches(TEXT("smoke"), TEXT("Name"), TEXT("MyPlugin.SMOKE"), TEXT("MyPlugin")));
    TestTrue(TEXT("Search by provider"), ProbeDev38::Matches(TEXT("myplugin"), TEXT("Name"), TEXT("Id"), TEXT("MyPlugin")));
    TestFalse(TEXT("Unrelated definitions excluded"), ProbeDev38::Matches(TEXT("other"), TEXT("Name"), TEXT("Id"), TEXT("MyPlugin")));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev38ProviderFilter, "Probe.Editor.Dev38ProviderFilter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev38ProviderFilter::RunTest(const FString&)
{
    auto P = MakeProvider(TEXT("Sample"));
    P.Tests.Add(MakeTest(TEXT("Sample.A"), TEXT("First")));
    P.Tests.Add(MakeTest(TEXT("Sample.B"), TEXT("Second")));
    const auto C = ProbeDev38::Count({P}, {}, TEXT("sample"));
    TestEqual(TEXT("Whole provider expanded by owner search"), C.VisibleDefinitions, 2);
    TestEqual(TEXT("Only one owner"), C.VisibleProviders, 1);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev38DefinitionFilter, "Probe.Editor.Dev38DefinitionFilter", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev38DefinitionFilter::RunTest(const FString&)
{
    auto P = MakeProvider(TEXT("Sample"));
    P.Tests.Add(MakeTest(TEXT("Sample.A"), TEXT("Apple")));
    P.Tests.Add(MakeTest(TEXT("Sample.B"), TEXT("Banana")));
    auto Q = MakeProvider(TEXT("Unrelated"));
    Q.Tests.Add(MakeTest(TEXT("Unrelated.A"), TEXT("Orange")));
    const auto C = ProbeDev38::Count({P,Q}, {}, TEXT("apple"));
    TestEqual(TEXT("Only matching owner"), C.VisibleProviders, 1);
    TestEqual(TEXT("Only matching definition"), C.VisibleDefinitions, 1);
    TestEqual(TEXT("Total remains unfiltered"), C.Tests, 3);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDev38Diagnostics, "Probe.Editor.Dev38DiagnosticsSummary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDev38Diagnostics::RunTest(const FString&)
{
    const auto C = ProbeDev38::Count({}, {TEXT("Bad manifest"), TEXT("Duplicate ID")}, TEXT(""));
    TestEqual(TEXT("Discovery issues preserved"), C.Diagnostics, 2);
    TestTrue(TEXT("Issue count visible"), ProbeDev38::Summary(C, false).Contains(TEXT("2 discovery issues")));
    return true;
}
#endif
