#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ProbePortableProviders.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    struct FProbeDev18Fixture
    {
        FString Directory;
        FProbeDev18Fixture()
        {
            Directory = FPaths::ProjectSavedDir() / TEXT("Probe/Dev18Fixtures")
                / FGuid::NewGuid().ToString(EGuidFormats::Digits);
            IFileManager::Get().MakeDirectory(*Directory, true);
        }
        ~FProbeDev18Fixture() { IFileManager::Get().DeleteDirectory(*Directory, false, true); }
        bool Write(const FString& Relative, const FString& Content)
        {
            const FString File = Directory / Relative;
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
            return FFileHelper::SaveStringToFile(Content, *File, FFileHelper::EEncodingOptions::ForceUTF8);
        }
        bool Valid()
        {
            return Write(TEXT("manifest.json"), TEXT("{\"format\":\"ProbeProvider/1\",\"provider\":\"Sample\",\"version\":\"1\",\"actions\":[\"Actions/Create.json\",\"Actions/Destroy.json\"],\"tests\":[\"Tests/Lifecycle.json\"],\"packs\":[\"Packs/All.json\"]}"))
                && Write(TEXT("Actions/Create.json"), TEXT("{\"format\":\"ProbeAction/1\",\"id\":\"Sample.Create\",\"name\":\"Create\",\"kind\":\"native-fixture\",\"permission\":\"transient-object\",\"type\":\"FixtureCreateObject\"}"))
                && Write(TEXT("Actions/Destroy.json"), TEXT("{\"format\":\"ProbeAction/1\",\"id\":\"Sample.Destroy\",\"name\":\"Destroy\",\"kind\":\"native-fixture\",\"permission\":\"transient-object\",\"type\":\"FixtureDestroyObject\"}"))
                && Write(TEXT("Tests/Lifecycle.json"), TEXT("{\"format\":\"ProbeTest/1\",\"id\":\"Sample.Lifecycle\",\"name\":\"Lifecycle\",\"steps\":[{\"action\":\"Sample.Create\",\"target\":\"/Script/ProbeEditor.ProbeTestAsset\",\"expected\":\"temp\"},{\"type\":\"FixtureObjectExists\",\"target\":\"temp\"},{\"action\":\"Sample.Destroy\",\"target\":\"temp\"}]}"))
                && Write(TEXT("Packs/All.json"), TEXT("{\"format\":\"ProbePack/1\",\"id\":\"Sample.All\",\"name\":\"All\",\"tests\":[\"Sample.Lifecycle\"]}"));
        }
        bool Load(ProbePortable::FPortableProvider& Provider, FString& Error)
        { return ProbePortable::LoadProvider(Directory, TEXT("Sample"), Provider, Error); }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev18ActionDiscovery, "Probe.Editor.Dev18ActionDiscovery", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev18ActionDiscovery::RunTest(const FString&)
{
    FProbeDev18Fixture Fixture; TestTrue(TEXT("Fixture saved"), Fixture.Valid());
    ProbePortable::FPortableProvider Provider; FString Error;
    if (!TestTrue(TEXT("Portable action manifest loads"), Fixture.Load(Provider, Error))) { AddError(Error); return false; }
    TestEqual(TEXT("Two provider actions"), Provider.Actions.Num(), 2);
    TestEqual(TEXT("One test"), Provider.Tests.Num(), 1);
    TestTrue(TEXT("Test requires authorization"), Provider.Tests[0].bRequiresActionAuthorization);
    TestFalse(TEXT("UObject-only fixture has no world actor authorization"), Provider.Tests[0].bRequiresTransientActors);
    TestTrue(TEXT("Action alias becomes test-owned create step"), Provider.Tests[0].Steps[0].Type == EProbeStepType::FixtureCreateObject);
    TestTrue(TEXT("Destroy alias becomes test-owned destroy step"), Provider.Tests[0].Steps[2].Type == EProbeStepType::FixtureDestroyObject);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev18PermissionDefaultDeny, "Probe.Editor.Dev18PermissionDefaultDeny", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev18PermissionDefaultDeny::RunTest(const FString&)
{
    FProbeDev18Fixture Fixture; Fixture.Valid();
    ProbePortable::FPortableProvider Provider; FString Error;
    if (!Fixture.Load(Provider, Error)) { AddError(Error); return false; }
    UProbeTestAsset* Asset = nullptr;
    TestFalse(TEXT("Authorless fixture execution is rejected"), ProbePortable::MakeTransientTest(Provider.Tests[0], Asset, Error));
    TestTrue(TEXT("No UAsset allocated on denial"), Asset == nullptr);
    TestTrue(TEXT("Reason requires explicit authorization"), Error.Contains(TEXT("authorization")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev18AuthorizedFixture, "Probe.Editor.Dev18AuthorizedFixture", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev18AuthorizedFixture::RunTest(const FString&)
{
    FProbeDev18Fixture Fixture; Fixture.Valid();
    ProbePortable::FPortableProvider Provider; FString Error;
    if (!Fixture.Load(Provider, Error)) { AddError(Error); return false; }
    UProbeTestAsset* Raw = nullptr;
    if (!TestTrue(TEXT("Explicit authorization constructs a transient test"),
        ProbePortable::MakeTransientTest(Provider.Tests[0], Raw, Error, true))) { AddError(Error); return false; }
    TStrongObjectPtr<UProbeTestAsset> Owned(Raw);
    TestTrue(TEXT("Never writes a saved asset"), Raw->HasAnyFlags(RF_Transient));
    TestFalse(TEXT("No live UI permission"), Raw->bAllowEditorUIInput);
    TestFalse(TEXT("No Editor world spawn permission"), Raw->bAllowTransientEditorActors);
    TestTrue(TEXT("Portable permission token is transient"), Raw->bPortableActionsAuthorized);
    TestFalse(TEXT("Default fixture class is concrete"), UProbeTestAsset::StaticClass()->HasAnyClassFlags(CLASS_Abstract));
    const Probe::FTestResult Result = Probe::RunTest(Raw);
    if (Result.Status != Probe::EResultStatus::Passed)
    {
        AddInfo(FString::Printf(TEXT("Fixture lifecycle runner detail: %s"), *Result.Detail));
        for (const Probe::FStepResult& StepResult : Result.Steps)
        {
            AddInfo(FString::Printf(TEXT("Step %d [%s]: %s"), StepResult.Index + 1,
                Probe::StatusName(StepResult.Status), *StepResult.Detail));
        }
    }
    TestTrue(TEXT("Real runner executes create/assert/destroy"), Result.Status == Probe::EResultStatus::Passed);
    TestEqual(TEXT("Three authored steps have results"), Result.Steps.Num(), 3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev18LiveInputBlocked, "Probe.Editor.Dev18LiveInputBlocked", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev18LiveInputBlocked::RunTest(const FString&)
{
    FProbeDev18Fixture Fixture; Fixture.Valid();
    Fixture.Write(TEXT("Actions/Create.json"), TEXT("{\"format\":\"ProbeAction/1\",\"id\":\"Sample.Create\",\"name\":\"Unsafe\",\"kind\":\"native-fixture\",\"permission\":\"transient-object\",\"type\":\"UIWidgetClick\"}"));
    ProbePortable::FPortableProvider Provider; FString Error;
    TestFalse(TEXT("UI input cannot be aliased into an action"), Fixture.Load(Provider, Error));
    TestTrue(TEXT("Denial reports allow-list"), Error.Contains(TEXT("allow-listed")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev18PermissionMismatch, "Probe.Editor.Dev18PermissionMismatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev18PermissionMismatch::RunTest(const FString&)
{
    FProbeDev18Fixture Fixture; Fixture.Valid();
    Fixture.Write(TEXT("Actions/Create.json"), TEXT("{\"format\":\"ProbeAction/1\",\"id\":\"Sample.Create\",\"name\":\"Create\",\"kind\":\"native-fixture\",\"permission\":\"transient-actor\",\"type\":\"FixtureCreateObject\"}"));
    ProbePortable::FPortableProvider Provider; FString Error;
    TestFalse(TEXT("Actor grant cannot masquerade as UObject grant"), Fixture.Load(Provider, Error));
    TestTrue(TEXT("Reports mismatch"), Error.Contains(TEXT("permission")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev18MissingAction, "Probe.Editor.Dev18MissingAction", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev18MissingAction::RunTest(const FString&)
{
    FProbeDev18Fixture Fixture; Fixture.Valid();
    Fixture.Write(TEXT("Tests/Lifecycle.json"), TEXT("{\"format\":\"ProbeTest/1\",\"id\":\"Sample.Lifecycle\",\"name\":\"Unknown\",\"steps\":[{\"action\":\"Sample.DoesNotExist\"}]}"));
    ProbePortable::FPortableProvider Provider; FString Error;
    TestFalse(TEXT("No late-bound or unknown plugin action allowed"), Fixture.Load(Provider, Error));
    TestTrue(TEXT("Missing reference explained"), Error.Contains(TEXT("undeclared")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev18SelectorSafety, "Probe.Editor.Dev18SelectorSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev18SelectorSafety::RunTest(const FString&)
{
    FProbeDev18Fixture Fixture; Fixture.Valid();
    Fixture.Write(TEXT("Tests/Lifecycle.json"), TEXT("{\"format\":\"ProbeTest/1\",\"id\":\"Sample.Lifecycle\",\"name\":\"Ambiguous\",\"steps\":[{\"type\":\"PluginInstalled\",\"target\":\"Probe\",\"action\":\"Sample.Create\"}]}"));
    ProbePortable::FPortableProvider Provider; FString Error;
    TestFalse(TEXT("Cannot execute one step as both assertion and action"), Fixture.Load(Provider, Error));
    TestTrue(TEXT("Explicit selector contract"), Error.Contains(TEXT("exactly one")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev18ActorScope, "Probe.Editor.Dev18ActorScope", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev18ActorScope::RunTest(const FString&)
{
    FProbeDev18Fixture Fixture; Fixture.Valid();
    Fixture.Write(TEXT("Actions/Create.json"), TEXT("{\"format\":\"ProbeAction/1\",\"id\":\"Sample.Create\",\"name\":\"Actor\",\"kind\":\"native-fixture\",\"permission\":\"transient-actor\",\"type\":\"ActorFixtureSpawn\"}"));
    ProbePortable::FPortableProvider Provider; FString Error;
    if (!Fixture.Load(Provider, Error)) { AddError(Error); return false; }
    TestTrue(TEXT("Actor action requires authorization"), Provider.Tests[0].bRequiresActionAuthorization);
    TestTrue(TEXT("Actor permission explicitly detected"), Provider.Tests[0].bRequiresTransientActors);
    UProbeTestAsset* Raw = nullptr;
    if (!ProbePortable::MakeTransientTest(Provider.Tests[0], Raw, Error, true)) { AddError(Error); return false; }
    TStrongObjectPtr<UProbeTestAsset> Owned(Raw);
    TestTrue(TEXT("Only authorized transient test can opt in to world fixtures"), Raw->bAllowTransientEditorActors);
    TestFalse(TEXT("Cannot grant live UI input"), Raw->bAllowEditorUIInput);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev18AsyncContract, "Probe.Editor.Dev18AsyncContract", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev18AsyncContract::RunTest(const FString&)
{
    FProbeDev18Fixture Fixture; Fixture.Valid();
    Fixture.Write(TEXT("manifest.json"), TEXT("{\"format\":\"ProbeProvider/1\",\"provider\":\"Sample\",\"version\":\"1\",\"actions\":[\"Actions/Async.json\"],\"tests\":[\"Tests/Lifecycle.json\"]}"));
    Fixture.Write(TEXT("Actions/Async.json"), TEXT("{\"format\":\"ProbeAction/1\",\"id\":\"Sample.Delay\",\"name\":\"Delay\",\"kind\":\"reflected-async\",\"permission\":\"provider-action\",\"class\":\"/Script/SampleEditor.SampleChecks\",\"startFunction\":\"Start\",\"pollFunction\":\"Poll\",\"cancelFunction\":\"Cancel\",\"cleanupFunction\":\"Cleanup\"}"));
    Fixture.Write(TEXT("Tests/Lifecycle.json"), TEXT("{\"format\":\"ProbeTest/1\",\"id\":\"Sample.Lifecycle\",\"name\":\"Async\",\"steps\":[{\"action\":\"Sample.Delay\",\"uiWaitTimeoutSeconds\":0.2}]}"));
    ProbePortable::FPortableProvider Provider; FString Error;
    if (!Fixture.Load(Provider, Error)) { AddError(Error); return false; }
    TestEqual(TEXT("Async action declared"), Provider.Actions[0].Kind, FString(TEXT("reflected-async")));
    TestEqual(TEXT("Cancel callback required"), Provider.Actions[0].CancelFunction, FString(TEXT("Cancel")));
    TestTrue(TEXT("Action always requires explicit authorization"), Provider.Tests[0].bRequiresActionAuthorization);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev18AsyncCancelRequired, "Probe.Editor.Dev18AsyncCancelRequired", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev18AsyncCancelRequired::RunTest(const FString&)
{
    FProbeDev18Fixture Fixture; Fixture.Valid();
    Fixture.Write(TEXT("manifest.json"), TEXT("{\"format\":\"ProbeProvider/1\",\"provider\":\"Sample\",\"version\":\"1\",\"actions\":[\"Actions/Async.json\"]}"));
    Fixture.Write(TEXT("Actions/Async.json"), TEXT("{\"format\":\"ProbeAction/1\",\"id\":\"Sample.Delay\",\"name\":\"Delay\",\"kind\":\"reflected-async\",\"permission\":\"provider-action\",\"class\":\"/Script/SampleEditor.Checks\",\"startFunction\":\"Start\",\"pollFunction\":\"Poll\",\"cleanupFunction\":\"Cleanup\"}"));
    ProbePortable::FPortableProvider Provider; FString Error;
    TestFalse(TEXT("Async actions require a cancel hook"), Fixture.Load(Provider, Error));
    TestTrue(TEXT("Missing cancel explains failure"), Error.Contains(TEXT("cancel")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev18CancellationEmpty, "Probe.Editor.Dev18CancellationEmpty", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev18CancellationEmpty::RunTest(const FString&)
{
    TestFalse(TEXT("Cancel with no active portable session is a no-op"), ProbePortable::RequestCancel());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev18NoAssetPermissionLeak, "Probe.Editor.Dev18NoAssetPermissionLeak", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev18NoAssetPermissionLeak::RunTest(const FString&)
{
    UProbeTestAsset* Asset = NewObject<UProbeTestAsset>();
    TestFalse(TEXT("Saved-class default does not authorize portable actions"), Asset->bPortableActionsAuthorized);
    TestFalse(TEXT("Saved-class default does not authorize cancellations"), Asset->bPortableCancellationRequested);
    TestFalse(TEXT("Does not authorize Editor input"), Asset->bAllowEditorUIInput);
    return true;
}
#endif
