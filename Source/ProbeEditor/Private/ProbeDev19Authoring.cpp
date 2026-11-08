#include "ProbeDev19Authoring.h"

#include "ProbeAssertionCatalog.h"
#include "ProbeTestAssetFactory.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"

namespace ProbeDev19
{
namespace
{
    bool SaveJson(const FString& File, const TSharedRef<FJsonObject>& Object, FString& Error)
    {
        FString Text;
        if (!FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text)))
        { Error = TEXT("Unable to serialize portable JSON."); return false; }
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        if (!FFileHelper::SaveStringToFile(Text, *File, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        { Error = TEXT("Unable to write portable JSON to: ") + File; return false; }
        return true;
    }

    TSharedRef<FJsonObject> Manifest(const FString& Provider, const FString& RelativeTest,
        const FString& RelativePack = FString())
    {
        TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("format"), TEXT("ProbeProvider/1"));
        Obj->SetStringField(TEXT("provider"), Provider);
        Obj->SetStringField(TEXT("version"), TEXT("0.1.0"));
        TArray<TSharedPtr<FJsonValue>> Tests;
        Tests.Add(MakeShared<FJsonValueString>(RelativeTest));
        Obj->SetArrayField(TEXT("tests"), Tests);
        if (!RelativePack.IsEmpty())
        {
            TArray<TSharedPtr<FJsonValue>> Packs;
            Packs.Add(MakeShared<FJsonValueString>(RelativePack));
            Obj->SetArrayField(TEXT("packs"), Packs);
        }
        return Obj;
    }

    FString SafeFileStem(FString Name)
    {
        for (TCHAR& C : Name)
        {
            if (!((C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z'))
                || (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('_') || C == TEXT('-')))
            { C = TEXT('_'); }
        }
        return Name.Left(100);
    }

    FString SavedOutputRoot(const FString& Category, const FString& Stem)
    {
        return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Probe"), Category,
            Stem + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
    }
}

bool ValidProviderName(const FString& Name)
{
    if (Name.IsEmpty() || Name.Len() > 64 || Name.StartsWith(TEXT("."))) { return false; }
    for (TCHAR C : Name)
    {
        if (!((C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z'))
            || (C >= TEXT('0') && C <= TEXT('9')) || C == TEXT('_') || C == TEXT('-')))
        { return false; }
    }
    return true;
}

bool CreateStarter(const FString& PluginName, FString& OutDirectory, FString& Error)
{
    check(IsInGameThread()); Error.Reset(); OutDirectory.Reset();
    if (!ValidProviderName(PluginName))
    { Error = TEXT("Use the exact plugin descriptor name: 1-64 ASCII letters, digits, _ or -."); return false; }
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Probe"), TEXT("ProviderStarters"), PluginName);
    if (IFileManager::Get().DirectoryExists(*Directory))
    { Error = TEXT("Starter already exists (never overwritten): ") + Directory; return false; }
    const FString Root = Directory / TEXT("Resources/Probe");
    TSharedRef<FJsonObject> Test = MakeShared<FJsonObject>();
    Test->SetStringField(TEXT("format"), TEXT("ProbeTest/1"));
    Test->SetStringField(TEXT("id"), PluginName + TEXT(".Smoke"));
    Test->SetStringField(TEXT("name"), TEXT("Plugin enabled smoke check"));
    Test->SetStringField(TEXT("description"), TEXT("Dependency-free Probe smoke test. Edit this JSON before shipping."));
    TSharedRef<FJsonObject> Step = MakeShared<FJsonObject>();
    Step->SetStringField(TEXT("type"), TEXT("PluginEnabled"));
    Step->SetStringField(TEXT("target"), PluginName);
    TSharedRef<FJsonObject> AliasStep = MakeShared<FJsonObject>();
    AliasStep->SetStringField(TEXT("assertion"), PluginName + TEXT(".Installed"));
    TArray<TSharedPtr<FJsonValue>> TestSteps;
    TestSteps.Add(MakeShared<FJsonValueObject>(Step));
    TestSteps.Add(MakeShared<FJsonValueObject>(AliasStep));
    Test->SetArrayField(TEXT("steps"), TestSteps);
    TSharedRef<FJsonObject> Alias = MakeShared<FJsonObject>();
    Alias->SetStringField(TEXT("format"), TEXT("ProbeAssertion/1"));
    Alias->SetStringField(TEXT("id"), PluginName + TEXT(".Installed"));
    Alias->SetStringField(TEXT("name"), TEXT("Plugin installed"));
    Alias->SetStringField(TEXT("category"), PluginName);
    Alias->SetStringField(TEXT("kind"), TEXT("builtin-alias"));
    Alias->SetStringField(TEXT("type"), TEXT("PluginInstalled"));
    Alias->SetStringField(TEXT("target"), PluginName);
    TSharedRef<FJsonObject> Template = MakeShared<FJsonObject>();
    Template->SetStringField(TEXT("format"), TEXT("ProbeTemplate/1"));
    Template->SetStringField(TEXT("id"), PluginName + TEXT(".EnabledTemplate"));
    Template->SetStringField(TEXT("name"), TEXT("Plugin enabled template"));
    TArray<TSharedPtr<FJsonValue>> TemplateSteps;
    TemplateSteps.Add(MakeShared<FJsonValueObject>(Step));
    Template->SetArrayField(TEXT("steps"), TemplateSteps);
    TSharedRef<FJsonObject> Pack = MakeShared<FJsonObject>();
    Pack->SetStringField(TEXT("format"), TEXT("ProbePack/1"));
    Pack->SetStringField(TEXT("id"), PluginName + TEXT(".All"));
    Pack->SetStringField(TEXT("name"), TEXT("Plugin smoke checks"));
    Pack->SetStringField(TEXT("failurePolicy"), TEXT("StopOnFailure"));
    TArray<TSharedPtr<FJsonValue>> PackTests;
    PackTests.Add(MakeShared<FJsonValueString>(PluginName + TEXT(".Smoke")));
    Pack->SetArrayField(TEXT("tests"), PackTests);
    TSharedRef<FJsonObject> StarterManifest = Manifest(PluginName, TEXT("Tests/Smoke.json"), TEXT("Packs/All.json"));
    TArray<TSharedPtr<FJsonValue>> Aliases;
    Aliases.Add(MakeShared<FJsonValueString>(TEXT("Assertions/Installed.json")));
    StarterManifest->SetArrayField(TEXT("assertions"), Aliases);
    TArray<TSharedPtr<FJsonValue>> Templates;
    Templates.Add(MakeShared<FJsonValueString>(TEXT("Templates/Enabled.json")));
    StarterManifest->SetArrayField(TEXT("templates"), Templates);
    const bool bWritten = SaveJson(Root / TEXT("manifest.json"), StarterManifest, Error)
        && SaveJson(Root / TEXT("Assertions/Installed.json"), Alias, Error)
        && SaveJson(Root / TEXT("Templates/Enabled.json"), Template, Error)
        && SaveJson(Root / TEXT("Tests/Smoke.json"), Test, Error)
        && SaveJson(Root / TEXT("Packs/All.json"), Pack, Error);
    ProbePortable::FPortableProvider Loaded;
    if (!bWritten || !ProbePortable::LoadProvider(Root, PluginName, Loaded, Error))
    { IFileManager::Get().DeleteDirectory(*Directory, false, true); return false; }
    FFileHelper::SaveStringToFile(
        TEXT("Copy Resources/Probe to your existing plugin folder, merge manifest entries if you already have one, and ship these JSON files with your plugin.\n")
        TEXT("The plugin requires no Probe dependency. Run Tools/ValidateProbeProviderJSON.py on the final plugin before distribution.\n"),
        *(Directory / TEXT("README.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    OutDirectory = Directory;
    return true;
}

bool ValidateInstalled(const FString& PluginName, FString& Summary, FString& Error)
{
    check(IsInGameThread()); Error.Reset(); Summary.Reset();
    if (!ValidProviderName(PluginName)) { Error = TEXT("Invalid plugin name."); return false; }
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
    if (!Plugin.IsValid() || !Plugin->IsEnabled())
    { Error = TEXT("Plugin is not installed and enabled: ") + PluginName; return false; }
    ProbePortable::FPortableProvider Provider;
    if (!ProbePortable::LoadProvider(Plugin->GetBaseDir() / TEXT("Resources/Probe"), PluginName, Provider, Error))
    { Error = PluginName + TEXT(": ") + Error; return false; }
    Summary = FString::Printf(TEXT("%s valid: %d assertions, %d actions, %d Tests, %d Templates, %d Packs."),
        *PluginName, Provider.Assertions.Num(), Provider.Actions.Num(), Provider.Tests.Num(),
        Provider.Templates.Num(), Provider.Packs.Num());
    return true;
}

bool ImportReadOnlyTest(const ProbePortable::FPortableTest& Test, UProbeTestAsset*& Asset, FString& Error)
{
    check(IsInGameThread()); Error.Reset(); Asset = nullptr;
    if (Test.bRequiresActionAuthorization || !Test.ActionStepIndices.IsEmpty())
    { Error = TEXT("Action-bearing portable Tests cannot be imported into ordinary Test Assets. Run them as portable Tests with explicit authorization."); return false; }
    // Fail closed for programmatic callers too, not just validated browser data.
    UProbeTestAsset* ReadOnlyProbe = nullptr;
    if (!ProbePortable::MakeTransientTest(Test, ReadOnlyProbe, Error, false)) { return false; }
    if (Test.Steps.IsEmpty() || Test.Steps.Num() > 256)
    { Error = TEXT("Invalid portable Test step count."); return false; }
    FString PackageName, AssetName;
    IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    const FString Base = FString(TEXT("/Game/Probe/Imported/PT_")) + SafeFileStem(Test.Id);
    Tools.CreateUniqueAssetName(Base, TEXT(""), PackageName, AssetName);
    UProbeTestAssetFactory* Factory = NewObject<UProbeTestAssetFactory>();
    Asset = Cast<UProbeTestAsset>(Tools.CreateAsset(AssetName, FPaths::GetPath(PackageName), UProbeTestAsset::StaticClass(), Factory));
    if (!Asset) { Error = TEXT("Unable to create editable Probe Test Asset under /Game/Probe/Imported."); return false; }
    Asset->Modify();
    Asset->TestId = FGuid::NewGuid();
    Asset->TestDescription = Test.Description;
    Asset->Steps = Test.Steps;
    Asset->bEnabled = false;               // Must be intentionally reviewed before Run All.
    Asset->bNeedsConfiguration = true;
    Asset->bAllowEditorUIInput = false;
    Asset->bAllowTransientEditorActors = false;
    Asset->bPortableActionsAuthorized = false;
    Asset->MarkPackageDirty();
    return true;
}

bool BuildReadOnlyTestJson(const UProbeTestAsset& Asset, const FString& ProviderId,
    const TSharedRef<FJsonObject>& OutJson, FString& Error)
{
    Error.Reset();
    if (!ValidProviderName(ProviderId)) { Error = TEXT("Invalid provider name."); return false; }
    if (Asset.Steps.IsEmpty() || Asset.Steps.Num() > 256 || Asset.bAllowEditorUIInput
        || Asset.bAllowTransientEditorActors || Asset.bPortableActionsAuthorized)
    { Error = TEXT("Portable JSON export only supports nonempty read-only Test Assets with no elevated permissions."); return false; }
    TArray<TSharedPtr<FJsonValue>> Steps;
    for (const FProbeTestStep& Step : Asset.Steps)
    {
        if (!Step.AssertionId.IsNone())
        { Error = TEXT("Export of provider-specific assertion references requires bundling their declarations; use Export JSON in the Provider Browser instead."); return false; }
        const UEnum* Enum = StaticEnum<EProbeStepType>();
        const FString Name = Enum ? Enum->GetNameStringByValue(static_cast<int64>(Step.Type)) : FString();
        if (Name.IsEmpty()) { Error = TEXT("Unknown built-in step type."); return false; }
        TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("type"), Name);
        Item->SetStringField(TEXT("target"), Step.Target);
        Item->SetStringField(TEXT("expectedClass"), Step.ExpectedClass);
        Item->SetStringField(TEXT("property"), Step.PropertyName);
        Item->SetStringField(TEXT("expected"), Step.ExpectedValue);
        Item->SetStringField(TEXT("description"), Step.Description);
        Item->SetStringField(TEXT("group"), Step.GroupName);
        Item->SetBoolField(TEXT("enabled"), Step.bEnabled);
        Item->SetNumberField(TEXT("minimumActorCount"), Step.MinimumActorCount);
        Item->SetNumberField(TEXT("numericTolerance"), Step.NumericTolerance);
        Item->SetNumberField(TEXT("uiWaitTimeoutSeconds"), Step.UIWaitTimeoutSeconds);
        Steps.Add(MakeShared<FJsonValueObject>(Item));
    }
    OutJson->SetStringField(TEXT("format"), TEXT("ProbeTest/1"));
    OutJson->SetStringField(TEXT("id"), ProviderId + TEXT(".") + SafeFileStem(Asset.GetName()));
    OutJson->SetStringField(TEXT("name"), Asset.GetName());
    OutJson->SetStringField(TEXT("description"), Asset.TestDescription);
    OutJson->SetBoolField(TEXT("enabled"), Asset.bEnabled);
    OutJson->SetArrayField(TEXT("steps"), Steps);
    return true;
}

bool ExportReadOnlyTest(const UProbeTestAsset& Asset, const FString& ProviderId,
    FString& OutFile, FString& Error)
{
    check(IsInGameThread()); OutFile.Reset(); Error.Reset();
    const TSharedRef<FJsonObject> Test = MakeShared<FJsonObject>();
    if (!BuildReadOnlyTestJson(Asset, ProviderId, Test, Error)) { return false; }
    const FString Directory = SavedOutputRoot(TEXT("PortableExports"), ProviderId + TEXT("-") + SafeFileStem(Asset.GetName()));
    const FString Root = Directory / TEXT("Resources/Probe");
    const FString TestFile = Root / TEXT("Tests/Exported.json");
    bool bValid = SaveJson(Root / TEXT("manifest.json"), Manifest(ProviderId, TEXT("Tests/Exported.json")), Error)
        && SaveJson(TestFile, Test, Error);
    ProbePortable::FPortableProvider Provider;
    if (bValid) { bValid = ProbePortable::LoadProvider(Root, ProviderId, Provider, Error); }
    if (!bValid)
    { IFileManager::Get().DeleteDirectory(*Directory, false, true); return false; }
    OutFile = TestFile;
    return true;
}

bool ExportSourceDefinition(const ProbePortable::FPortableProvider& Provider,
    const FString& Id, bool bPack, FString& OutFile, FString& Error)
{
    check(IsInGameThread()); Error.Reset(); OutFile.Reset();
    FString Source;
    if (bPack)
    {
        for (const ProbePortable::FPortablePack& Pack : Provider.Packs)
        { if (Pack.Id == Id) { Source = Pack.SourcePath; break; } }
    }
    else
    {
        for (const ProbePortable::FPortableTest& Test : Provider.Tests)
        { if (Test.Id == Id) { Source = Test.SourcePath; break; } }
    }
    if (!ProbePortable::ValidateRelativePath(Source, Error))
    { Error = TEXT("Definition is unavailable or source path is unsafe: ") + Error; return false; }
    const FString SrcFile = Provider.Directory / Source;
    FString Content;
    if (IFileManager::Get().FileSize(*SrcFile) < 0 || IFileManager::Get().FileSize(*SrcFile) > 131072
        || !FFileHelper::LoadFileToString(Content, *SrcFile))
    { Error = TEXT("Definition file missing or oversized."); return false; }
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Probe"), TEXT("DefinitionExports"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    OutFile = Directory / (SafeFileStem(Id) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8) + TEXT(".json"));
    if (!FFileHelper::SaveStringToFile(Content, *OutFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    { Error = TEXT("Unable to export provider definition."); OutFile.Reset(); return false; }
    return true;
}

FString PreviewTest(const ProbePortable::FPortableProvider& Provider, const ProbePortable::FPortableTest& Test)
{
    FString Out = FString::Printf(TEXT("%s\n%s\nPlugin: %s | %d steps | %s\nSource: %s\n\n"),
        *Test.Name, *Test.Id, *Provider.PluginName, Test.Steps.Num(),
        Test.bRequiresActionAuthorization ? TEXT("AUTHORIZED ACTIONS") : TEXT("Read-only"), *Test.SourcePath);
    if (!Test.Description.IsEmpty()) { Out += Test.Description + TEXT("\n\n"); }
    for (int32 I = 0; I < Test.Steps.Num(); ++I)
    {
        const FProbeTestStep& Step = Test.Steps[I];
        FString Selector;
        if (!Step.AssertionId.IsNone()) { Selector = Step.AssertionId.ToString(); }
        else
        {
            const UEnum* Enum = StaticEnum<EProbeStepType>();
            Selector = Enum ? Enum->GetNameStringByValue(static_cast<int64>(Step.Type)) : TEXT("Unknown");
        }
        Out += FString::Printf(TEXT("%d. %s%s\n    Target: %s | Expected: %s\n"), I+1,
            Test.ActionStepIndices.Contains(I) ? TEXT("ACTION: ") : TEXT(""), *Selector,
            *Step.Target, *Step.ExpectedValue);
    }
    return Out;
}

FString PreviewPack(const ProbePortable::FPortableProvider& Provider, const ProbePortable::FPortablePack& Pack)
{
    FString Out = FString::Printf(TEXT("%s\n%s\nPlugin: %s | %s\nSource: %s\n\n"),
        *Pack.Name, *Pack.Id, *Provider.PluginName,
        Pack.bStopOnFailure ? TEXT("Stop on failure") : TEXT("Continue on failure"), *Pack.SourcePath);
    for (int32 I = 0; I < Pack.TestIds.Num(); ++I)
    {
        const FString& Id = Pack.TestIds[I];
        const ProbePortable::FPortableTest* Test = Provider.Tests.FindByPredicate(
            [&Id](const ProbePortable::FPortableTest& Candidate) { return Candidate.Id == Id; });
        Out += FString::Printf(TEXT("%d. %s%s\n"), I+1, *Id,
            Test && Test->bRequiresActionAuthorization ? TEXT(" (requires authorization)") : TEXT(""));
    }
    return Out;
}

FString DisplayStatus(const Probe::FTestResult& Test)
{
    if (Test.Status == Probe::EResultStatus::Error &&
        (Test.Detail.Contains(TEXT("cancelled"), ESearchCase::IgnoreCase)
         || Test.Detail.Contains(TEXT("canceled"), ESearchCase::IgnoreCase)))
    { return TEXT("CANCELLED"); }
    return Probe::StatusName(Test.Status);
}

FRunSummary Summarize(const TArray<Probe::FTestResult>& Results)
{
    FRunSummary Sum;
    for (const Probe::FTestResult& Test : Results)
    {
        const FString Status = DisplayStatus(Test);
        if (Status == TEXT("PASSED")) { ++Sum.Passed; }
        else if (Status == TEXT("FAILED")) { ++Sum.Failed; }
        else if (Status == TEXT("CANCELLED")) { ++Sum.Cancelled; }
        else if (Status == TEXT("SKIPPED")) { ++Sum.Skipped; }
        else { ++Sum.Errors; }
    }
    return Sum;
}

FString FRunSummary::ToString() const
{
    return FString::Printf(TEXT("%d passed, %d failed, %d errors, %d cancelled, %d skipped"),
        Passed, Failed, Errors, Cancelled, Skipped);
}

TSharedRef<FJsonObject> BuildRunReport(const FString& DefinitionId, bool bPack,
    bool bActions, const TArray<Probe::FTestResult>& Results)
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("format"), TEXT("ProbePortableReport/1"));
    Root->SetStringField(TEXT("definition_id"), DefinitionId);
    Root->SetStringField(TEXT("kind"), bPack ? TEXT("pack") : TEXT("test"));
    Root->SetBoolField(TEXT("authorized_actions"), bActions);
    Root->SetStringField(TEXT("timestamp_utc"), FDateTime::UtcNow().ToIso8601());
    const FRunSummary Summary = Summarize(Results);
    TSharedRef<FJsonObject> Totals = MakeShared<FJsonObject>();
    Totals->SetNumberField(TEXT("passed"), Summary.Passed);
    Totals->SetNumberField(TEXT("failed"), Summary.Failed);
    Totals->SetNumberField(TEXT("errors"), Summary.Errors);
    Totals->SetNumberField(TEXT("cancelled"), Summary.Cancelled);
    Totals->SetNumberField(TEXT("skipped"), Summary.Skipped);
    Root->SetObjectField(TEXT("summary"), Totals);
    TArray<TSharedPtr<FJsonValue>> Tests;
    for (const Probe::FTestResult& Test : Results)
    {
        TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("id"), Test.Id);
        Result->SetStringField(TEXT("name"), Test.Name);
        Result->SetStringField(TEXT("status"), DisplayStatus(Test));
        Result->SetStringField(TEXT("raw_status"), Probe::StatusName(Test.Status));
        Result->SetStringField(TEXT("detail"), Test.Detail);
        Result->SetNumberField(TEXT("duration_ms"), Test.DurationMs);
        TArray<TSharedPtr<FJsonValue>> Steps;
        for (const Probe::FStepResult& Step : Test.Steps)
        {
            TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
            Json->SetNumberField(TEXT("index"), Step.Index);
            Json->SetStringField(TEXT("assertion_id"), Step.AssertionId);
            Json->SetStringField(TEXT("status"), Probe::StatusName(Step.Status));
            Json->SetStringField(TEXT("description"), Step.Description);
            Json->SetStringField(TEXT("detail"), Step.Detail);
            Json->SetNumberField(TEXT("duration_ms"), Step.DurationMs);
            Steps.Add(MakeShared<FJsonValueObject>(Json));
        }
        Result->SetArrayField(TEXT("steps"), Steps);
        Tests.Add(MakeShared<FJsonValueObject>(Result));
    }
    Root->SetArrayField(TEXT("tests"), Tests);
    return Root;
}

int32 PrunePortableReports(const FString& Directory, int32 MaximumFiles)
{
    check(IsInGameThread());
    if (MaximumFiles < 1 || !IFileManager::Get().DirectoryExists(*Directory)) { return 0; }
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(Directory / TEXT("Portable-*.json")), true, false);
    Files.Sort(); // Filenames use sortable UTC date/time then a uniqueness token.
    const int32 Excess = FMath::Max(0, Files.Num() - MaximumFiles);
    int32 Removed = 0;
    for (int32 Index = 0; Index < Excess; ++Index)
    {
        const FString& Name = Files[Index];
        if (!Name.StartsWith(TEXT("Portable-")) || !Name.EndsWith(TEXT(".json"))
            || Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\"))) { continue; }
        Removed += IFileManager::Get().Delete(*(Directory / Name)) ? 1 : 0;
    }
    return Removed;
}

bool SaveRunReport(const FString& DefinitionId, bool bPack, bool bActions,
    const TArray<Probe::FTestResult>& Results, FString& OutFile, FString& Error)
{
    check(IsInGameThread()); OutFile.Reset(); Error.Reset();
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Probe"), TEXT("PortableReports"));
    const FString Name = FString::Printf(TEXT("Portable-%s-%s.json"),
        *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
    const FString Target = Directory / Name;
    if (!SaveJson(Target, BuildRunReport(DefinitionId, bPack, bActions, Results), Error)) { return false; }
    // Portable reports have their own bounded retention. Never delete a file
    // outside this owned filename pattern or the project's Saved/Probe directory.
    PrunePortableReports(Directory, 25);
    OutFile = Target;
    return true;
}
}
