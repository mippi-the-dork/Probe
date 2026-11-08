#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ProbeDev19Authoring.h"
#include "ProbeTestAsset.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
    FString UniqueName()
    {
        return TEXT("Dev19") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(12);
    }
    UProbeTestAsset* MakeReadOnly()
    {
        UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage());
        Asset->TestDescription = TEXT("Portable authoring round-trip test");
        FProbeTestStep First;
        First.Type = EProbeStepType::PluginInstalled;
        First.Target = TEXT("Probe");
        First.Description = TEXT("Check installation");
        First.GroupName = TEXT("Smoke");
        FProbeTestStep Second;
        Second.Type = EProbeStepType::PluginEnabled;
        Second.Target = TEXT("Probe");
        Second.bEnabled = false;
        Asset->Steps = {First, Second};
        return Asset;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev19StarterGeneration,
    "Probe.Editor.Dev19StarterGeneration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev19StarterGeneration::RunTest(const FString&)
{
    const FString Name = UniqueName();
    FString Directory, Error;
    if (!TestTrue(TEXT("Creates a dependency-free Resources/Probe starter"),
        ProbeDev19::CreateStarter(Name, Directory, Error))) { AddError(Error); return false; }
    ProbePortable::FPortableProvider Provider;
    TestTrue(TEXT("Starter's manifest, Test and Pack pass the exact runtime provider validator"),
        ProbePortable::LoadProvider(Directory / TEXT("Resources/Probe"), Name, Provider, Error));
    TestEqual(TEXT("One generated assertion alias"), Provider.Assertions.Num(), 1);
    TestEqual(TEXT("One generated authoring template"), Provider.Templates.Num(), 1);
    TestEqual(TEXT("One generated Test"), Provider.Tests.Num(), 1);
    TestEqual(TEXT("One generated Pack"), Provider.Packs.Num(), 1);
    if (Provider.Tests.Num())
    {
        TestEqual(TEXT("Stable namespaced ID"), Provider.Tests[0].Id, Name + TEXT(".Smoke"));
        TestTrue(TEXT("Portable step type preserved"), Provider.Tests[0].Steps[0].Type == EProbeStepType::PluginEnabled);
        TestEqual(TEXT("Alias declaration resolves"), Provider.Tests[0].Steps[1].AssertionId.ToString(), Name + TEXT(".Installed"));
    }
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev19StarterNameSafety,
    "Probe.Editor.Dev19StarterNameSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev19StarterNameSafety::RunTest(const FString&)
{
    FString Directory, Error;
    for (const FString& Invalid : {TEXT("../Wrong"), TEXT("Bad/Path"), TEXT("Bad\\Path"), TEXT("Bad Name"), TEXT(".Hidden"), TEXT("")})
    {
        TestFalse(TEXT("Unsafe starter name rejected"), ProbeDev19::ValidProviderName(Invalid));
        TestFalse(TEXT("Unsafe starter never created"), ProbeDev19::CreateStarter(Invalid, Directory, Error));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev19StarterNoOverwrite,
    "Probe.Editor.Dev19StarterNoOverwrite", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev19StarterNoOverwrite::RunTest(const FString&)
{
    const FString Name = UniqueName();
    FString Directory, Error, Second;
    if (!TestTrue(TEXT("First starter created"), ProbeDev19::CreateStarter(Name, Directory, Error)))
    { AddError(Error); return false; }
    TestFalse(TEXT("Never overwrite a user's existing provider starter"),
        ProbeDev19::CreateStarter(Name, Second, Error));
    TestTrue(TEXT("Original manifest remains intact"),
        IFileManager::Get().FileExists(*(Directory / TEXT("Resources/Probe/manifest.json"))));
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev19AssetExportRoundTrip,
    "Probe.Editor.Dev19AssetExportRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev19AssetExportRoundTrip::RunTest(const FString&)
{
    const FString Name = UniqueName();
    UProbeTestAsset* Asset = MakeReadOnly();
    const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
    FString Error;
    if (!TestTrue(TEXT("Read-only Test Asset converts to portable JSON"),
        ProbeDev19::BuildReadOnlyTestJson(*Asset, Name, Json, Error)))
    { AddError(Error); return false; }
    const TArray<TSharedPtr<FJsonValue>>* Steps = nullptr;
    TestTrue(TEXT("Step array preserved"), Json->TryGetArrayField(TEXT("steps"), Steps) && Steps && Steps->Num() == 2);
    if (Steps && Steps->Num() == 2)
    {
        TestEqual(TEXT("First step type"), (*Steps)[0]->AsObject()->GetStringField(TEXT("type")), FString(TEXT("PluginInstalled")));
        TestEqual(TEXT("Group metadata preserved"), (*Steps)[0]->AsObject()->GetStringField(TEXT("group")), FString(TEXT("Smoke")));
        TestFalse(TEXT("Disabled step remains disabled"), (*Steps)[1]->AsObject()->GetBoolField(TEXT("enabled")));
    }
    FString File;
    if (!TestTrue(TEXT("Export is re-validated as a real portable provider"),
        ProbeDev19::ExportReadOnlyTest(*Asset, Name, File, Error)))
    { AddError(Error); return false; }
    const FString Root = FPaths::GetPath(FPaths::GetPath(File));
    ProbePortable::FPortableProvider Provider;
    TestTrue(TEXT("Export can be discovered by ProbeProvider/1 loader"),
        ProbePortable::LoadProvider(Root, Name, Provider, Error));
    if (Provider.Tests.Num() == 1)
    {
        TestEqual(TEXT("Two step round-trip"), Provider.Tests[0].Steps.Num(), 2);
        TestEqual(TEXT("Description round-trip"), Provider.Tests[0].Steps[0].Description, FString(TEXT("Check installation")));
    }
    // Each generated export has a unique containing folder. Remove only that folder.
    IFileManager::Get().DeleteDirectory(*FPaths::GetPath(FPaths::GetPath(Root)), false, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev19ExportDefaultDeny,
    "Probe.Editor.Dev19ExportDefaultDeny", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev19ExportDefaultDeny::RunTest(const FString&)
{
    UProbeTestAsset* Asset = MakeReadOnly();
    FString File, Error;
    Asset->bAllowEditorUIInput = true;
    TestFalse(TEXT("Live UI authorization not exportable as ordinary portable JSON"),
        ProbeDev19::ExportReadOnlyTest(*Asset, UniqueName(), File, Error));
    Asset->bAllowEditorUIInput = false;
    Asset->Steps[0].Type = EProbeStepType::FixtureCreateObject;
    TestFalse(TEXT("Raw fixture creation cannot bypass portable 'action' selector"),
        ProbeDev19::ExportReadOnlyTest(*Asset, UniqueName(), File, Error));
    Asset->Steps[0].Type = EProbeStepType::PluginInstalled;
    Asset->Steps[0].AssertionId = FName(TEXT("OtherProvider.Custom"));
    TestFalse(TEXT("Cannot export undeclared custom assertion without its own JSON declaration"),
        ProbeDev19::ExportReadOnlyTest(*Asset, UniqueName(), File, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev19ImportPermissionIsolation,
    "Probe.Editor.Dev19ImportPermissionIsolation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev19ImportPermissionIsolation::RunTest(const FString&)
{
    ProbePortable::FPortableTest Test;
    Test.Id = TEXT("Example.Action");
    Test.ActionStepIndices.Add(0);
    Test.bRequiresActionAuthorization = true;
    Test.Steps.Add(FProbeTestStep());
    FString Error;
    UProbeTestAsset* Asset = nullptr;
    TestFalse(TEXT("Action-bearing portable Test cannot become privileged saved asset"),
        ProbeDev19::ImportReadOnlyTest(Test, Asset, Error));
    TestTrue(TEXT("No asset created on denial"), Asset == nullptr);
    TestTrue(TEXT("Reason explains actions"), Error.Contains(TEXT("Action")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev19RunStatusSeparation,
    "Probe.Editor.Dev19RunStatusSeparation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev19RunStatusSeparation::RunTest(const FString&)
{
    TArray<Probe::FTestResult> Results;
    for (Probe::EResultStatus S : {Probe::EResultStatus::Passed, Probe::EResultStatus::Failed,
        Probe::EResultStatus::Error, Probe::EResultStatus::Skipped})
    { Probe::FTestResult Result; Result.Status = S; Results.Add(Result); }
    Probe::FTestResult Cancelled;
    Cancelled.Id = TEXT("Example.Slow");
    Cancelled.Status = Probe::EResultStatus::Error;
    Cancelled.Detail = TEXT("Portable action cancelled after 4.62s; cancellation callback completed.");
    Results.Add(Cancelled);
    TestEqual(TEXT("Cancellation presentation"), ProbeDev19::DisplayStatus(Cancelled), FString(TEXT("CANCELLED")));
    const ProbeDev19::FRunSummary Summary = ProbeDev19::Summarize(Results);
    TestEqual(TEXT("Passed count"), Summary.Passed, 1);
    TestEqual(TEXT("Failed count"), Summary.Failed, 1);
    TestEqual(TEXT("Error count"), Summary.Errors, 1);
    TestEqual(TEXT("Cancelled count"), Summary.Cancelled, 1);
    TestEqual(TEXT("Skipped count"), Summary.Skipped, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev19ReportFormat,
    "Probe.Editor.Dev19ReportFormat", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev19ReportFormat::RunTest(const FString&)
{
    Probe::FTestResult R;
    R.Id = TEXT("Example.Slow");
    R.Status = Probe::EResultStatus::Error;
    R.Detail = TEXT("Portable action cancelled by user");
    Probe::FStepResult Step;
    Step.Index = 1;
    Step.Description = TEXT("Test cancellation");
    Step.Detail = TEXT("Cancel callback completed");
    R.Steps.Add(Step);
    const TSharedRef<FJsonObject> Object = ProbeDev19::BuildRunReport(TEXT("Example.Pack"), true, true, {R});
    TestEqual(TEXT("Versioned portable report format"), Object->GetStringField(TEXT("format")), FString(TEXT("ProbePortableReport/1")));
    TestEqual(TEXT("Provider definition ID"), Object->GetStringField(TEXT("definition_id")), FString(TEXT("Example.Pack")));
    TestEqual(TEXT("Kind preserved"), Object->GetStringField(TEXT("kind")), FString(TEXT("pack")));
    TestTrue(TEXT("Authorization recorded"), Object->GetBoolField(TEXT("authorized_actions")));
    TestEqual(TEXT("Cancelled result count"), Object->GetObjectField(TEXT("summary"))->GetNumberField(TEXT("cancelled")), 1.0);
    const TArray<TSharedPtr<FJsonValue>>& Tests = Object->GetArrayField(TEXT("tests"));
    TestEqual(TEXT("One test"), Tests.Num(), 1);
    TestEqual(TEXT("Cancellation presented separately from raw engine Error"),
        Tests[0]->AsObject()->GetStringField(TEXT("status")), FString(TEXT("CANCELLED")));
    TestEqual(TEXT("Underlying engine status remains intact"),
        Tests[0]->AsObject()->GetStringField(TEXT("raw_status")), FString(TEXT("ERROR")));
    TestEqual(TEXT("Step evidence preserved"), Tests[0]->AsObject()->GetArrayField(TEXT("steps")).Num(), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev19PreviewNoExecution,
    "Probe.Editor.Dev19PreviewNoExecution", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev19PreviewNoExecution::RunTest(const FString&)
{
    ProbePortable::FPortableProvider Provider;
    Provider.PluginName = TEXT("Example");
    ProbePortable::FPortableTest Test;
    Test.Id = TEXT("Example.Smoke");
    Test.Name = TEXT("Smoke");
    Test.Steps.Add(FProbeTestStep());
    Test.Steps[0].Target = TEXT("Probe");
    const FString Preview = ProbeDev19::PreviewTest(Provider, Test);
    TestTrue(TEXT("Preview prints definition ID"), Preview.Contains(TEXT("Example.Smoke")));
    TestTrue(TEXT("Preview prints step target"), Preview.Contains(TEXT("Probe")));
    ProbePortable::FPortablePack Pack;
    Pack.Id = TEXT("Example.All");
    Pack.TestIds.Add(Test.Id);
    Provider.Tests.Add(Test);
    const FString PackPreview = ProbeDev19::PreviewPack(Provider, Pack);
    TestTrue(TEXT("Pack preview preserves test order and references"), PackPreview.Contains(TEXT("1. Example.Smoke")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev19SourceDefinitionExport,
    "Probe.Editor.Dev19SourceDefinitionExport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev19SourceDefinitionExport::RunTest(const FString&)
{
    const FString Name = UniqueName();
    FString Directory, Error;
    if (!TestTrue(TEXT("Starter created"), ProbeDev19::CreateStarter(Name, Directory, Error)))
    { AddError(Error); return false; }
    ProbePortable::FPortableProvider Provider;
    if (!TestTrue(TEXT("Starter loads"),
        ProbePortable::LoadProvider(Directory / TEXT("Resources/Probe"), Name, Provider, Error)))
    { IFileManager::Get().DeleteDirectory(*Directory, false, true); AddError(Error); return false; }
    TestEqual(TEXT("Loader retains safe source path"), Provider.Tests[0].SourcePath, FString(TEXT("Tests/Smoke.json")));
    FString Exported;
    if (TestTrue(TEXT("Source JSON copy exported"),
        ProbeDev19::ExportSourceDefinition(Provider, Name + TEXT(".Smoke"), false, Exported, Error)))
    {
        FString OriginalContents, CopyContents;
        TestTrue(TEXT("Original source read"), FFileHelper::LoadFileToString(OriginalContents, *(Provider.Directory / Provider.Tests[0].SourcePath)));
        TestTrue(TEXT("Export copy read"), FFileHelper::LoadFileToString(CopyContents, *Exported));
        TestEqual(TEXT("Byte-equivalent JSON source preserved"), CopyContents, OriginalContents);
        IFileManager::Get().Delete(*Exported);
    }
    else { AddError(Error); }
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev19ReportPersistence,
    "Probe.Editor.Dev19ReportPersistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev19ReportPersistence::RunTest(const FString&)
{
    Probe::FTestResult Result;
    Result.Id = TEXT("Example.Passed");
    Result.Name = TEXT("Valid source-independent test");
    Result.Status = Probe::EResultStatus::Passed;
    Result.Detail = TEXT("All steps passed");
    FString File, Error;
    if (!TestTrue(TEXT("Portable JSON report written to Saved/Probe"),
        ProbeDev19::SaveRunReport(TEXT("Example.All"), true, false, {Result}, File, Error)))
    { AddError(Error); return false; }
    TestTrue(TEXT("Report resides in restricted project Saved directory"),
        File.StartsWith(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Probe"), TEXT("PortableReports"))));
    FString Json;
    TestTrue(TEXT("Saved report is readable"), FFileHelper::LoadFileToString(Json, *File));
    TestTrue(TEXT("Versioned report persisted"), Json.Contains(TEXT("ProbePortableReport/1")));
    IFileManager::Get().Delete(*File);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev19ReportRetention,
    "Probe.Editor.Dev19ReportRetention", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev19ReportRetention::RunTest(const FString&)
{
    const FString Directory = FPaths::ProjectSavedDir() / TEXT("Probe/Dev19RetentionFixtures")
        / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    IFileManager::Get().MakeDirectory(*Directory, true);
    for (int32 Index = 0; Index < 7; ++Index)
    {
        const FString Name = FString::Printf(TEXT("Portable-20260101-00000%d-abcd.json"), Index);
        FFileHelper::SaveStringToFile(TEXT("{}"), *(Directory / Name));
    }
    FFileHelper::SaveStringToFile(TEXT("keep me"), *(Directory / TEXT("AuthorNotes.json")));
    TestEqual(TEXT("Deletes four oldest portable reports"), ProbeDev19::PrunePortableReports(Directory, 3), 4);
    TestTrue(TEXT("Unrelated file is preserved"), IFileManager::Get().FileExists(*(Directory / TEXT("AuthorNotes.json"))));
    TestFalse(TEXT("Oldest report removed"), IFileManager::Get().FileExists(*(Directory / TEXT("Portable-20260101-000000-abcd.json"))));
    TestTrue(TEXT("Latest report retained"), IFileManager::Get().FileExists(*(Directory / TEXT("Portable-20260101-000006-abcd.json"))));
    IFileManager::Get().DeleteDirectory(*Directory, false, true);
    return true;
}
#endif
