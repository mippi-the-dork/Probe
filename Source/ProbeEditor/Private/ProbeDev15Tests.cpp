#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ProbeDev15Readiness.h"
#include "ProbeDev15Recipes.h"
#include "ProbeDev15Browser.h"
#include "ProbeTestAsset.h"
#include "ProbeTestPack.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    UProbeTestAsset* Dev15Test()
    {
        UProbeTestAsset* Test = NewObject<UProbeTestAsset>(GetTransientPackage(), NAME_None, RF_Transient);
        Test->bNeedsConfiguration = false;
        FProbeTestStep Step;
        Step.Type = EProbeStepType::PluginInstalled;
        Step.Target = TEXT("Probe");
        Step.Description = TEXT("Check the Probe plugin");
        Step.GroupName = TEXT("Health");
        Test->Steps.Add(Step);
        return Test;
    }
    ProbeDev15::FEntry Dev15Entry(const TCHAR* Name, ProbeDev15::EKind Kind = ProbeDev15::EKind::Test)
    {
        ProbeDev15::FEntry Entry;
        Entry.Name = Name;
        Entry.Path = FString(TEXT("/Game/QA/")) + Name + TEXT(".") + Name;
        Entry.Folder = TEXT("/Game/QA");
        Entry.Kind = Kind;
        return Entry;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev15TestReadiness,
    "Probe.Editor.Dev15TestReadiness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev15TestReadiness::RunTest(const FString&)
{
    UProbeTestAsset* Test = Dev15Test();
    ProbeDev15::FEntry Entry = Dev15Entry(TEXT("Health"));
    TestTrue(TEXT("Metadata entry starts unscanned"), Entry.State == ProbeDev15::EState::NotScanned);
    ProbeDev15::InspectTest(Entry, Test);
    TestTrue(TEXT("Configured Test is ready"), Entry.State == ProbeDev15::EState::Ready);
    TestEqual(TEXT("Test step count is included"), Entry.StepsOrMembers, 1);
    TestEqual(TEXT("No blocking issues"), Entry.Blocking, 0);
    TestTrue(TEXT("Diagnostics mention test"), Entry.Details.Contains(TEXT("TEST")));
    Test->bNeedsConfiguration = true;
    ProbeDev15::InspectTest(Entry, Test);
    TestTrue(TEXT("Unreviewed Test is blocked"), Entry.State == ProbeDev15::EState::Blocked);
    TestTrue(TEXT("Blocks unconfigured test"), Entry.Blocking > 0);
    Test->bNeedsConfiguration = false;
    Test->bEnabled = false;
    ProbeDev15::InspectTest(Entry, Test);
    TestTrue(TEXT("Disabled Test is distinguished from failure"), Entry.State == ProbeDev15::EState::Disabled);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev15PackReadiness,
    "Probe.Editor.Dev15PackReadiness", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev15PackReadiness::RunTest(const FString&)
{
    UProbeTestPack* Pack = NewObject<UProbeTestPack>(GetTransientPackage(), NAME_None, RF_Transient);
    UProbeTestAsset* Test = Dev15Test();
    const FString Path = Test->GetPathName();
    Pack->Tests.Add(TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(Path)));
    ProbeDev15::FEntry Entry = Dev15Entry(TEXT("Pack"), ProbeDev15::EKind::Pack);
    auto Resolver = [Test, Path](const FString& Candidate) -> const UProbeTestAsset*
    { return Candidate == Path ? Test : nullptr; };
    ProbeDev15::InspectPack(Entry, Pack, Resolver);
    TestTrue(TEXT("Valid Pack is ready"), Entry.State == ProbeDev15::EState::Ready);
    TestEqual(TEXT("Pack has one member"), Entry.StepsOrMembers, 1);
    Test->bNeedsConfiguration = true;
    ProbeDev15::InspectPack(Entry, Pack, Resolver);
    TestTrue(TEXT("Unreviewed member blocks Pack"), Entry.State == ProbeDev15::EState::Blocked);
    ProbeDev15::InspectPack(Entry, Pack, [](const FString&) -> const UProbeTestAsset* { return nullptr; });
    TestTrue(TEXT("Missing reference remains a blocking error"), Entry.Blocking > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev15SearchFilters,
    "Probe.Editor.Dev15SearchFilters", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev15SearchFilters::RunTest(const FString&)
{
    ProbeDev15::FEntry Test = Dev15Entry(TEXT("AnimationHealth"));
    Test.State = ProbeDev15::EState::Ready;
    ProbeDev15::FEntry Pack = Dev15Entry(TEXT("AnimationPack"), ProbeDev15::EKind::Pack);
    Pack.State = ProbeDev15::EState::Blocked;
    TestTrue(TEXT("Case-insensitive name match"), ProbeDev15::Matches(Test, TEXT("animation"), ProbeDev15::EKindFilter::All, ProbeDev15::EStateFilter::All));
    TestTrue(TEXT("Folder match"), ProbeDev15::Matches(Test, TEXT("/game/qa"), ProbeDev15::EKindFilter::All, ProbeDev15::EStateFilter::All));
    TestFalse(TEXT("Pack-only filter excludes Test"), ProbeDev15::Matches(Test, TEXT(""), ProbeDev15::EKindFilter::Packs, ProbeDev15::EStateFilter::All));
    TestTrue(TEXT("Ready filter accepts Ready"), ProbeDev15::Matches(Test, TEXT(""), ProbeDev15::EKindFilter::Tests, ProbeDev15::EStateFilter::Ready));
    TestFalse(TEXT("Blocked filter excludes Ready"), ProbeDev15::Matches(Test, TEXT(""), ProbeDev15::EKindFilter::All, ProbeDev15::EStateFilter::Blocked));
    TestTrue(TEXT("Blocked Pack filter"), ProbeDev15::Matches(Pack, TEXT("pack"), ProbeDev15::EKindFilter::Packs, ProbeDev15::EStateFilter::Blocked));
    TestFalse(TEXT("Unscanned filter excludes scanned Pack"), ProbeDev15::Matches(Pack, TEXT(""), ProbeDev15::EKindFilter::All, ProbeDev15::EStateFilter::NotScanned));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev15DashboardSummary,
    "Probe.Editor.Dev15DashboardSummary", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev15DashboardSummary::RunTest(const FString&)
{
    TArray<ProbeDev15::FEntry> Entries;
    Entries.Add(Dev15Entry(TEXT("Ready")));
    Entries.Last().State = ProbeDev15::EState::Ready;
    Entries.Add(Dev15Entry(TEXT("Blocked")));
    Entries.Last().State = ProbeDev15::EState::Blocked;
    Entries.Add(Dev15Entry(TEXT("Pack"), ProbeDev15::EKind::Pack));
    Entries.Add(Dev15Entry(TEXT("Disabled")));
    Entries.Last().State = ProbeDev15::EState::Disabled;
    const ProbeDev15::FSummary Summary = ProbeDev15::Summarize(Entries);
    TestEqual(TEXT("All assets counted"), Summary.Total, 4);
    TestEqual(TEXT("Three tests"), Summary.Tests, 3);
    TestEqual(TEXT("One Pack"), Summary.Packs, 1);
    TestEqual(TEXT("One ready"), Summary.Ready, 1);
    TestEqual(TEXT("One blocked"), Summary.Blocked, 1);
    TestEqual(TEXT("One unscanned"), Summary.Unscanned, 1);
    TestEqual(TEXT("One disabled"), Summary.Disabled, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev15StepFinder,
    "Probe.Editor.Dev15StepFinder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev15StepFinder::RunTest(const FString&)
{
    FProbeTestStep Step = Dev15Test()->Steps[0];
    Step.ExpectedValue = TEXT("Response 200");
    TestTrue(TEXT("Assertion type name can be searched"), ProbeDev15::StepMatches(Step, TEXT("plugin is installed")));
    TestTrue(TEXT("Description can be searched"), ProbeDev15::StepMatches(Step, TEXT("CHECK THE")));
    TestTrue(TEXT("Group can be searched"), ProbeDev15::StepMatches(Step, TEXT("health")));
    TestTrue(TEXT("Target can be searched"), ProbeDev15::StepMatches(Step, TEXT("probe")));
    TestTrue(TEXT("Expected value can be searched"), ProbeDev15::StepMatches(Step, TEXT("200")));
    TestFalse(TEXT("Unknown text is hidden"), ProbeDev15::StepMatches(Step, TEXT("missingneedle")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev15CSVExport,
    "Probe.Editor.Dev15CSVExport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev15CSVExport::RunTest(const FString&)
{
    TArray<ProbeDev15::FEntry> Entries;
    ProbeDev15::FEntry A = Dev15Entry(TEXT("=CMD"));
    A.Details = TEXT("Quoted \"diagnostic\", another cell");
    A.State = ProbeDev15::EState::Blocked;
    Entries.Add(A);
    Entries.Add(Dev15Entry(TEXT("Pack"), ProbeDev15::EKind::Pack));
    const FString CSV = ProbeDev15::ExportCSV(Entries);
    TestTrue(TEXT("CSV has stable column schema"), CSV.StartsWith(TEXT("Kind,Name,Path,State,Count,Blocking,Warnings,Details")));
    TestTrue(TEXT("User-supplied formulas are escaped"), CSV.Contains(TEXT("'=CMD")));
    TestTrue(TEXT("Embedded quotes are doubled"), CSV.Contains(TEXT("\"\"diagnostic\"\"")));
    const FString Filtered = ProbeDev15::ExportCSV(Entries, TEXT("pack"), ProbeDev15::EKindFilter::Packs);
    TestFalse(TEXT("Filtered CSV excludes test"), Filtered.Contains(TEXT("=CMD")));
    TestTrue(TEXT("Filtered CSV includes Pack"), Filtered.Contains(TEXT("Pack")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev15ReadOnlyScan,
    "Probe.Editor.Dev15ReadOnlyScan", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev15ReadOnlyScan::RunTest(const FString&)
{
    UProbeTestAsset* Test = Dev15Test();
    const int32 OriginalCount = Test->Steps.Num();
    const bool OriginalReview = Test->bNeedsConfiguration;
    const bool OriginalInput = Test->bAllowEditorUIInput;
    ProbeDev15::FEntry Entry = Dev15Entry(TEXT("ReadOnly"));
    ProbeDev15::InspectTest(Entry, Test);
    ProbeDev15::InspectTest(Entry, Test);
    TestEqual(TEXT("Preflight does not change step count"), Test->Steps.Num(), OriginalCount);
    TestEqual(TEXT("Preflight does not change review state"), Test->bNeedsConfiguration, OriginalReview);
    TestEqual(TEXT("Preflight never grants input permission"), Test->bAllowEditorUIInput, OriginalInput);
    TestTrue(TEXT("Repeated preflight remains stable"), Entry.State == ProbeDev15::EState::Ready);
    ProbeDev15::FEntry Missing = Dev15Entry(TEXT("Missing"));
    ProbeDev15::InspectTest(Missing, nullptr);
    TestTrue(TEXT("Null object is reported, not dereferenced"), Missing.State == ProbeDev15::EState::Blocked);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev15BrowserConstruct,
    "Probe.Editor.Dev15BrowserConstruct", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev15BrowserConstruct::RunTest(const FString&)
{
    TSharedRef<SProbeDev15Browser> Browser = SNew(SProbeDev15Browser);
    TestTrue(TEXT("Native Slate QA Browser constructed"), Browser->GetChildren()->Num() > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev15RecipeNameSafety,
    "Probe.Editor.Dev15RecipeNameSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev15RecipeNameSafety::RunTest(const FString&)
{
    FString Canonical;
    TestTrue(TEXT("Human-readable name normalizes to safe file name"), ProbeDev15Recipes::CanonicalName(TEXT("Actor Health"), Canonical));
    TestEqual(TEXT("Spaces become underscores"), Canonical, FString(TEXT("Actor_Health")));
    TestFalse(TEXT("Path traversal rejected"), ProbeDev15Recipes::CanonicalName(TEXT("../Secrets"), Canonical));
    TestFalse(TEXT("Windows drive separators rejected"), ProbeDev15Recipes::CanonicalName(TEXT("C:Disk"), Canonical));
    TestFalse(TEXT("Blank name rejected"), ProbeDev15Recipes::CanonicalName(TEXT("  "), Canonical));
    TestFalse(TEXT("Oversize name rejected"), ProbeDev15Recipes::CanonicalName(FString::ChrN(65, TEXT('a')), Canonical));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev15RecipeAppendSafety,
    "Probe.Editor.Dev15RecipeAppendSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev15RecipeAppendSafety::RunTest(const FString&)
{
    UProbeTestAsset* Test = Dev15Test();
    TArray<FProbeTestStep> Recipe;
    Recipe.Add(Test->Steps[0]);
    const bool InputBefore = Test->bAllowEditorUIInput;
    const bool FixtureBefore = Test->bAllowTransientEditorActors;
    FString Error;
    TestTrue(TEXT("Valid recipe appended"), ProbeDev15Recipes::Append(*Test, Recipe, Error));
    TestEqual(TEXT("Step was appended"), Test->Steps.Num(), 2);
    TestTrue(TEXT("Inserted recipe requires review"), Test->bNeedsConfiguration);
    TestEqual(TEXT("Input permission unchanged"), Test->bAllowEditorUIInput, InputBefore);
    TestEqual(TEXT("Actor permission unchanged"), Test->bAllowTransientEditorActors, FixtureBefore);
    Test->CleanupGroupName = TEXT("Cleanup");
    TestFalse(TEXT("Appending after Cleanup rejected"), ProbeDev15Recipes::Append(*Test, Recipe, Error));
    TestEqual(TEXT("Rejected append is atomic"), Test->Steps.Num(), 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev15RecipePersistence,
    "Probe.Editor.Dev15RecipePersistence", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev15RecipePersistence::RunTest(const FString&)
{
    UProbeTestAsset* Test = Dev15Test();
    ProbeStepGroups::FRange Range;
    Range.First = 0; Range.Last = 0;
    const FString Name = TEXT("Dev15Automation_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FString Error;
    const bool bSaved = ProbeDev15Recipes::Save(Test->Steps, Range, Name, false, Error);
    TestTrue(TEXT("Project-local recipe can be saved"), bSaved);
    if (bSaved)
    {
        TArray<FProbeTestStep> Restored;
        TestTrue(TEXT("Saved recipe can be loaded"), ProbeDev15Recipes::Read(Name, Restored, Error));
        TestEqual(TEXT("One step restored"), Restored.Num(), 1);
        if (Restored.Num() == 1)
        {
            TestEqual(TEXT("Target roundtrips"), Restored[0].Target, FString(TEXT("Probe")));
            TestEqual(TEXT("Group roundtrips"), Restored[0].GroupName, FString(TEXT("Health")));
        }
        TestFalse(TEXT("Existing file cannot silently be overwritten"),
            ProbeDev15Recipes::Save(Test->Steps, Range, Name, false, Error));
        TestTrue(TEXT("Saved recipe appears in menu discovery"), ProbeDev15Recipes::List().Contains(Name));
        IFileManager::Get().Delete(*ProbeDev15Recipes::FilePath(Name), false, true);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
