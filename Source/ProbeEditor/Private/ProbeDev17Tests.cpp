#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ProbePortableProviders.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    struct FPortableFixture
    {
        FString Directory;
        FPortableFixture()
        {
            Directory = FPaths::ProjectSavedDir() / TEXT("Probe/Dev17Fixtures")
                / FGuid::NewGuid().ToString(EGuidFormats::Digits);
            IFileManager::Get().MakeDirectory(*Directory, true);
        }
        ~FPortableFixture() { IFileManager::Get().DeleteDirectory(*Directory, false, true); }
        bool Write(const FString& Relative, const FString& Data)
        {
            const FString File = Directory / Relative;
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
            return FFileHelper::SaveStringToFile(Data, *File,
                FFileHelper::EEncodingOptions::ForceUTF8);
        }
        bool Valid()
        {
            return Write(TEXT("manifest.json"),
                TEXT("{\"format\":\"ProbeProvider/1\",\"provider\":\"Example\",\"version\":\"1.0.0\",\"assertions\":[\"Assertions/Ready.json\"],\"tests\":[\"Tests/Health.json\"],\"templates\":[\"Templates/Base.json\"],\"packs\":[\"Packs/All.json\"]}"))
                && Write(TEXT("Assertions/Ready.json"),
                TEXT("{\"format\":\"ProbeAssertion/1\",\"id\":\"Example.Ready\",\"name\":\"Ready\",\"category\":\"Example\",\"kind\":\"builtin-alias\",\"type\":\"PluginInstalled\",\"target\":\"Probe\"}"))
                && Write(TEXT("Tests/Health.json"),
                TEXT("{\"format\":\"ProbeTest/1\",\"id\":\"Example.Health\",\"name\":\"Health Check\",\"steps\":[{\"type\":\"PluginInstalled\",\"target\":\"Probe\"},{\"assertion\":\"Example.Ready\"}]}"))
                && Write(TEXT("Templates/Base.json"),
                TEXT("{\"format\":\"ProbeTemplate/1\",\"id\":\"Example.Template\",\"name\":\"Template\",\"steps\":[{\"type\":\"PluginEnabled\",\"target\":\"Probe\"}]}"))
                && Write(TEXT("Packs/All.json"),
                TEXT("{\"format\":\"ProbePack/1\",\"id\":\"Example.All\",\"name\":\"All Checks\",\"failurePolicy\":\"StopOnFailure\",\"tests\":[\"Example.Health\"]}"));
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev17ManifestDiscovery,
    "Probe.Editor.Dev17ManifestDiscovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev17ManifestDiscovery::RunTest(const FString&)
{
    FPortableFixture Fixture;
    TestTrue(TEXT("Fixture saved"), Fixture.Valid());
    ProbePortable::FPortableProvider Provider;
    FString Error;
    TestTrue(TEXT("Valid plugin-independent resources load"), ProbePortable::LoadProvider(Fixture.Directory, TEXT("Example"), Provider, Error));
    TestEqual(TEXT("One alias assertion"), Provider.Assertions.Num(), 1);
    TestEqual(TEXT("One portable Test"), Provider.Tests.Num(), 1);
    TestEqual(TEXT("One authoring Template"), Provider.Templates.Num(), 1);
    TestEqual(TEXT("One portable Pack"), Provider.Packs.Num(), 1);
    TestEqual(TEXT("Pack preserves reference order"), Provider.Packs[0].TestIds[0], FString(TEXT("Example.Health")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev17PathSafety,
    "Probe.Editor.Dev17PathSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev17PathSafety::RunTest(const FString&)
{
    FString Error;
    TestFalse(TEXT("No parent traversal"), ProbePortable::ValidateRelativePath(TEXT("../secret.json"), Error));
    TestFalse(TEXT("No nested parent traversal"), ProbePortable::ValidateRelativePath(TEXT("Tests/../secret.json"), Error));
    TestFalse(TEXT("No absolute paths"), ProbePortable::ValidateRelativePath(TEXT("/Tests/check.json"), Error));
    TestFalse(TEXT("No Windows drive paths"), ProbePortable::ValidateRelativePath(TEXT("C:/foo.json"), Error));
    TestFalse(TEXT("No alternate backslash separators"), ProbePortable::ValidateRelativePath(TEXT("Tests\\check.json"), Error));
    TestFalse(TEXT("No non-JSON file"), ProbePortable::ValidateRelativePath(TEXT("Tests/check.uasset"), Error));
    TestTrue(TEXT("Valid nested JSON path"), ProbePortable::ValidateRelativePath(TEXT("Tests/Check_1.json"), Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev17FormatVersion,
    "Probe.Editor.Dev17FormatVersion", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev17FormatVersion::RunTest(const FString&)
{
    FPortableFixture Fixture;
    TestTrue(TEXT("Fixture saved"), Fixture.Valid());
    Fixture.Write(TEXT("manifest.json"), TEXT("{\"format\":\"ProbeProvider/99\",\"provider\":\"Example\",\"version\":\"1.0\"}"));
    ProbePortable::FPortableProvider Provider; FString Error;
    TestFalse(TEXT("Future schema rejected explicitly"), ProbePortable::LoadProvider(Fixture.Directory, TEXT("Example"), Provider, Error));
    TestTrue(TEXT("Failure explains expected schema"), Error.Contains(TEXT("ProbeProvider/1")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev17OwnerIsolation,
    "Probe.Editor.Dev17OwnerIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev17OwnerIsolation::RunTest(const FString&)
{
    FPortableFixture Fixture; Fixture.Valid();
    ProbePortable::FPortableProvider Provider; FString Error;
    TestFalse(TEXT("Provider cannot claim a different owning plugin"),
        ProbePortable::LoadProvider(Fixture.Directory, TEXT("AnotherPlugin"), Provider, Error));
    TestTrue(TEXT("Provider ownership reason"), Error.Contains(TEXT("match owning plugin")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev17UnsafeInput,
    "Probe.Editor.Dev17UnsafeInput", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev17UnsafeInput::RunTest(const FString&)
{
    FPortableFixture Fixture; Fixture.Valid();
    Fixture.Write(TEXT("Tests/Health.json"),
        TEXT("{\"format\":\"ProbeTest/1\",\"id\":\"Example.Health\",\"name\":\"Unsafe\",\"allowEditorUIInput\":true,\"steps\":[{\"type\":\"PluginInstalled\",\"target\":\"Probe\"}]}"));
    ProbePortable::FPortableProvider Provider; FString Error;
    TestFalse(TEXT("JSON cannot grant UI authority"), ProbePortable::LoadProvider(Fixture.Directory, TEXT("Example"), Provider, Error));
    Fixture.Write(TEXT("Tests/Health.json"),
        TEXT("{\"format\":\"ProbeTest/1\",\"id\":\"Example.Health\",\"name\":\"Unsafe\",\"steps\":[{\"type\":\"UIWidgetClick\",\"target\":\"id:example\"}]}"));
    TestFalse(TEXT("State-changing native input rejected"), ProbePortable::LoadProvider(Fixture.Directory, TEXT("Example"), Provider, Error));
    TestTrue(TEXT("Denial explains read-only contract"), Error.Contains(TEXT("read-only")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev17OrphanedReferences,
    "Probe.Editor.Dev17OrphanedReferences", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev17OrphanedReferences::RunTest(const FString&)
{
    FPortableFixture Fixture; Fixture.Valid();
    Fixture.Write(TEXT("Packs/All.json"),
        TEXT("{\"format\":\"ProbePack/1\",\"id\":\"Example.All\",\"name\":\"Missing\",\"tests\":[\"Example.Unknown\"]}"));
    ProbePortable::FPortableProvider Provider; FString Error;
    TestFalse(TEXT("Missing pack test rejected"), ProbePortable::LoadProvider(Fixture.Directory, TEXT("Example"), Provider, Error));
    Fixture.Valid();
    Fixture.Write(TEXT("Tests/Health.json"),
        TEXT("{\"format\":\"ProbeTest/1\",\"id\":\"Example.Health\",\"name\":\"Missing\",\"steps\":[{\"assertion\":\"Example.Unknown\"}]}"));
    TestFalse(TEXT("Undeclared assertion rejected"), ProbePortable::LoadProvider(Fixture.Directory, TEXT("Example"), Provider, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev17CorruptJson,
    "Probe.Editor.Dev17CorruptJson", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev17CorruptJson::RunTest(const FString&)
{
    FPortableFixture Fixture; Fixture.Valid();
    Fixture.Write(TEXT("Tests/Health.json"), TEXT("{invalid JSON"));
    ProbePortable::FPortableProvider Provider; FString Error;
    TestFalse(TEXT("Malformed JSON fails closed"), ProbePortable::LoadProvider(Fixture.Directory, TEXT("Example"), Provider, Error));
    TestTrue(TEXT("Reports invalid JSON"), Error.Contains(TEXT("Invalid JSON")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev17TransientRun,
    "Probe.Editor.Dev17TransientRun", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev17TransientRun::RunTest(const FString&)
{
    ProbePortable::FPortableTest Definition;
    Definition.Id = TEXT("Example.ReadOnly");
    Definition.Name = TEXT("Example Read Only");
    FProbeTestStep Step;
    Step.Type = EProbeStepType::PluginInstalled;
    Step.Target = TEXT("Probe");
    Definition.Steps.Add(Step);
    UProbeTestAsset* Raw = nullptr; FString Error;
    TestTrue(TEXT("Conversion to transient runner input"), ProbePortable::MakeTransientTest(Definition, Raw, Error));
    TestNotNull(TEXT("Transient fixture created"), Raw);
    if (!Raw) { return false; }
    TStrongObjectPtr<UProbeTestAsset> Owned(Raw);
    TestTrue(TEXT("No .uasset created"), Raw->HasAnyFlags(RF_Transient));
    TestFalse(TEXT("No persistent package"), Raw->GetOutermost()->IsDirty());
    TestFalse(TEXT("Live UI input never authorized"), Raw->bAllowEditorUIInput);
    TestFalse(TEXT("Transient world actors never authorized"), Raw->bAllowTransientEditorActors);
    const Probe::FTestResult Result = Probe::RunTest(Raw);
    TestEqual(TEXT("Real existing runner evaluates JSON-converted test"),
        static_cast<int32>(Result.Status), static_cast<int32>(Probe::EResultStatus::Passed));
    FProbeTestStep Unsafe;
    Unsafe.Type = EProbeStepType::UIWidgetClick;
    Definition.Steps.Add(Unsafe);
    UProbeTestAsset* Rejected = nullptr;
    TestFalse(TEXT("Public transient API also rejects native UI input"),
        ProbePortable::MakeTransientTest(Definition, Rejected, Error));
    TestTrue(TEXT("Rejected definition does not allocate an asset"), Rejected == nullptr);
    Definition.Steps.Pop();
    Definition.Steps[0].AssertionId = TEXT("Unmanaged.Unknown");
    TestFalse(TEXT("Public API rejects unmanaged C++ callback assertions"),
        ProbePortable::MakeTransientTest(Definition, Rejected, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev17DuplicateDefinitions,
    "Probe.Editor.Dev17DuplicateDefinitions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev17DuplicateDefinitions::RunTest(const FString&)
{
    FPortableFixture Fixture; Fixture.Valid();
    Fixture.Write(TEXT("manifest.json"),
        TEXT("{\"format\":\"ProbeProvider/1\",\"provider\":\"Example\",\"version\":\"1.0\",\"tests\":[\"Tests/Health.json\",\"Tests/Health.json\"]}"));
    ProbePortable::FPortableProvider Provider; FString Error;
    TestFalse(TEXT("Duplicate file references blocked"), ProbePortable::LoadProvider(Fixture.Directory, TEXT("Example"), Provider, Error));
    TestTrue(TEXT("Explains collision"), Error.Contains(TEXT("Duplicate")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev17ReflectedSafety,
    "Probe.Editor.Dev17ReflectedSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev17ReflectedSafety::RunTest(const FString&)
{
    FPortableFixture Fixture; Fixture.Valid();
    Fixture.Write(TEXT("Assertions/Ready.json"),
        TEXT("{\"format\":\"ProbeAssertion/1\",\"id\":\"Example.Ready\",\"name\":\"Safe Check\",\"category\":\"Example\",\"kind\":\"reflected-readonly\",\"class\":\"/Script/SomeOtherPlugin.SomeClass\",\"function\":\"Tick\"}"));
    ProbePortable::FPortableProvider Provider; FString Error;
    TestTrue(TEXT("Reflection declaration is inert during discovery"),
        ProbePortable::LoadProvider(Fixture.Directory, TEXT("Example"), Provider, Error));
    TestEqual(TEXT("Reflected class declaration preserved"), Provider.Assertions[0].ReflectedClass,
        FString(TEXT("/Script/SomeOtherPlugin.SomeClass")));
    // Module ownership and UFUNCTION meta/signature gates are performed before execution.
    // No arbitrary ProcessEvent is invoked by LoadProvider.
    return true;
}
#endif
