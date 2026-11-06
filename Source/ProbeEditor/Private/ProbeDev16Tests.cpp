#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "ProbeDev16UX.h"
#include "ProbeDev14Authoring.h"
#include "UObject/Package.h"
#include "Misc/Guid.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    UProbeTestAsset* MakeDev16Fixture()
    {
        UProbeTestAsset* Test = NewObject<UProbeTestAsset>(GetTransientPackage(), NAME_None, RF_Transient);
        Test->bNeedsConfiguration = false;
        for (const TCHAR* GroupName : { TEXT("Setup"), TEXT("Setup"), TEXT(""), TEXT("Check"), TEXT("Check"), TEXT("Setup") })
        {
            FProbeTestStep Step;
            Step.Type = EProbeStepType::PluginInstalled;
            Step.Target = TEXT("Probe");
            Step.GroupName = GroupName;
            Test->Steps.Add(Step);
        }
        return Test;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev16GroupBoundaries,
    "Probe.Editor.Dev16GroupBoundaries", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev16GroupBoundaries::RunTest(const FString&)
{
    const auto& Steps = MakeDev16Fixture()->Steps;
    TestTrue(TEXT("First group starts at step 0"), ProbeDev16::IsGroupStart(Steps, 0));
    TestFalse(TEXT("Second step belongs to first group"), ProbeDev16::IsGroupStart(Steps, 1));
    TestFalse(TEXT("Ungrouped step has no header"), ProbeDev16::IsGroupStart(Steps, 2));
    TestTrue(TEXT("Second named group starts"), ProbeDev16::IsGroupStart(Steps, 3));
    TestTrue(TEXT("Repeated name after gap starts a separate group"), ProbeDev16::IsGroupStart(Steps, 5));
    TestEqual(TEXT("Contiguous Check span"), ProbeDev16::GroupAt(Steps, 4).Num(), 2);
    TestFalse(TEXT("Group instances have distinct identities"),
        ProbeDev16::GroupKey(ProbeDev16::GroupAt(Steps, 0)) == ProbeDev16::GroupKey(ProbeDev16::GroupAt(Steps, 5)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev16SelectionBoundaries,
    "Probe.Editor.Dev16SelectionBoundaries", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev16SelectionBoundaries::RunTest(const FString&)
{
    ProbeDev16::FGroup Range;
    TSet<int32> Selection; Selection.Add(1); Selection.Add(2); Selection.Add(3);
    TestTrue(TEXT("Contiguous selection accepted"), ProbeDev16::IsContiguousSelection(Selection, 6, Range));
    TestEqual(TEXT("Starts at 1"), Range.First, 1);
    TestEqual(TEXT("Ends at 3"), Range.Last, 3);
    Selection.Remove(2);
    TestFalse(TEXT("Disjoint selection rejected"), ProbeDev16::IsContiguousSelection(Selection, 6, Range));
    Selection.Reset(); Selection.Add(7);
    TestFalse(TEXT("Stale out of bounds rejected"), ProbeDev16::IsContiguousSelection(Selection, 6, Range));
    Selection.Reset();
    TestFalse(TEXT("Empty selection rejected"), ProbeDev16::IsContiguousSelection(Selection, 6, Range));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev16GroupRename,
    "Probe.Editor.Dev16GroupRename", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev16GroupRename::RunTest(const FString&)
{
    UProbeTestAsset* Test = MakeDev16Fixture();
    Test->SetupGroupName = TEXT("Setup");
    TestTrue(TEXT("Rename full contiguous group"), ProbeDev16::RenameGroup(*Test, 1, TEXT("Preparation")));
    TestEqual(TEXT("Setup lifecycle reference follows group at beginning"), Test->SetupGroupName, FString(TEXT("Preparation")));
    TestEqual(TEXT("First member updated"), Test->Steps[0].GroupName, FString(TEXT("Preparation")));
    TestEqual(TEXT("Second member updated"), Test->Steps[1].GroupName, FString(TEXT("Preparation")));
    TestEqual(TEXT("Non-member unchanged"), Test->Steps[5].GroupName, FString(TEXT("Setup")));
    TestFalse(TEXT("Empty name rejected"), ProbeDev16::RenameGroup(*Test, 0, TEXT("   ")));
    TestFalse(TEXT("Ungrouped step rejected"), ProbeDev16::RenameGroup(*Test, 2, TEXT("Anything")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev16OverviewSafety,
    "Probe.Editor.Dev16OverviewSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev16OverviewSafety::RunTest(const FString&)
{
    UProbeTestAsset* Test = MakeDev16Fixture();
    Test->bNeedsConfiguration = true;
    Test->bAllowEditorUIInput = false;
    Test->bAllowTransientEditorActors = false;
    const FString Overview = ProbeDev16::TestSummary(*Test);
    TestTrue(TEXT("Overview displays author review state"), Overview.Contains(TEXT("Needs Review")));
    TestTrue(TEXT("Overview shows step count"), Overview.Contains(TEXT("6 steps")));
    TestTrue(TEXT("Overview does not imply authorization"), Overview.Contains(TEXT("Not authorized")));
    TestTrue(TEXT("Read-only overview preserves configuration"), Test->bNeedsConfiguration);
    TestFalse(TEXT("UI permission still disabled"), Test->bAllowEditorUIInput);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev16RecipeManageSafety,
    "Probe.Editor.Dev16RecipeManageSafety", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev16RecipeManageSafety::RunTest(const FString&)
{
    FString Error;
    TestFalse(TEXT("Delete blocks invalid path"), ProbeDev16::DeleteRecipe(TEXT("../Other"), Error));
    TestFalse(TEXT("Rename blocks invalid source"), ProbeDev16::RenameRecipe(TEXT("../Other"), TEXT("Safe"), Error));
    TestFalse(TEXT("Rename blocks invalid destination"), ProbeDev16::RenameRecipe(TEXT("Safe"), TEXT("../../Other"), Error));
    TestFalse(TEXT("Duplicate blocks invalid name"), ProbeDev16::DuplicateRecipe(TEXT("../Other"), TEXT("Safe"), Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev16RecipeRoundTrip,
    "Probe.Editor.Dev16RecipeRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev16RecipeRoundTrip::RunTest(const FString&)
{
    // Project-local disposable files. The test never changes Content Browser assets.
    UProbeTestAsset* Test = MakeDev16Fixture();
    FString Error;
    const FString Prefix = TEXT("_D16_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString A = Prefix + TEXT("_A");
    const FString B = Prefix + TEXT("_B");
    const FString C = Prefix + TEXT("_C");
    ProbeStepGroups::FRange Range; Range.First = 0; Range.Last = 1;
    TestTrue(TEXT("Save recipe"), ProbeDev15Recipes::Save(Test->Steps, Range, A, false, Error));
    TestTrue(TEXT("Duplicate recipe"), ProbeDev16::DuplicateRecipe(A, B, Error));
    TestTrue(TEXT("Rename copied recipe"), ProbeDev16::RenameRecipe(B, C, Error));
    TArray<FProbeTestStep> Read;
    TestTrue(TEXT("Read renamed recipe"), ProbeDev15Recipes::Read(C, Read, Error));
    TestEqual(TEXT("Two steps retained"), Read.Num(), 2);
    TestFalse(TEXT("Deleted path no longer exists"), IFileManager::Get().FileExists(*ProbeDev15Recipes::FilePath(B)));
    TestTrue(TEXT("Delete renamed recipe"), ProbeDev16::DeleteRecipe(C, Error));
    TestTrue(TEXT("Delete original recipe"), ProbeDev16::DeleteRecipe(A, Error));
    TestFalse(TEXT("No accidental permission propagation"), Test->bAllowEditorUIInput);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev16RecipeCollision,
    "Probe.Editor.Dev16RecipeCollision", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev16RecipeCollision::RunTest(const FString&)
{
    UProbeTestAsset* Test = MakeDev16Fixture();
    FString Error;
    const FString Prefix = TEXT("_D16_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString A = Prefix + TEXT("_A");
    const FString B = Prefix + TEXT("_B");
    ProbeStepGroups::FRange Range; Range.First = 0; Range.Last = 1;
    TestTrue(TEXT("Save source"), ProbeDev15Recipes::Save(Test->Steps, Range, A, false, Error));
    TestTrue(TEXT("Save destination"), ProbeDev15Recipes::Save(Test->Steps, Range, B, false, Error));
    TestFalse(TEXT("Rename never overwrites destination"), ProbeDev16::RenameRecipe(A, B, Error));
    TestFalse(TEXT("Duplicate never overwrites destination"), ProbeDev16::DuplicateRecipe(A, B, Error));
    TArray<FProbeTestStep> Steps;
    TestTrue(TEXT("Original still readable"), ProbeDev15Recipes::Read(A, Steps, Error));
    TestTrue(TEXT("Destination still readable"), ProbeDev15Recipes::Read(B, Steps, Error));
    ProbeDev16::DeleteRecipe(A, Error); ProbeDev16::DeleteRecipe(B, Error);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev16RecipePermissions,
    "Probe.Editor.Dev16RecipePermissions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev16RecipePermissions::RunTest(const FString&)
{
    UProbeTestAsset* Test = MakeDev16Fixture();
    Test->bAllowEditorUIInput = false;
    Test->bAllowTransientEditorActors = false;
    Test->bNeedsConfiguration = false;
    TArray<FProbeTestStep> Saved = {Test->Steps[0], Test->Steps[1]};
    FString Error;
    TestTrue(TEXT("Append reusable recipe"), ProbeDev15Recipes::Append(*Test, Saved, Error));
    TestEqual(TEXT("Appends exactly two authored steps"), Test->Steps.Num(), 8);
    TestTrue(TEXT("Appended test requires review"), Test->bNeedsConfiguration);
    TestFalse(TEXT("Append cannot grant UI input"), Test->bAllowEditorUIInput);
    TestFalse(TEXT("Append cannot grant Actor world permission"), Test->bAllowTransientEditorActors);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev16GroupEdgeCases,
    "Probe.Editor.Dev16GroupEdgeCases", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev16GroupEdgeCases::RunTest(const FString&)
{
    UProbeTestAsset* Test = MakeDev16Fixture();
    auto& Steps = Test->Steps;
    TestEqual(TEXT("Invalid group request has empty span"), ProbeDev16::GroupAt(Steps, -1).Num(), 0);
    TestEqual(TEXT("Ungrouped step has no group"), ProbeDev16::GroupAt(Steps, 2).Num(), 0);
    TestEqual(TEXT("Singleton final group is recognized"), ProbeDev16::GroupAt(Steps, 5).Num(), 1);
    TestTrue(TEXT("Final singleton is a group header"), ProbeDev16::IsGroupStart(Steps, 5));
    const FString Original = Steps[3].GroupName;
    TestFalse(TEXT("Excessive group name rejected"), ProbeDev16::RenameGroup(*Test, 3, FString::ChrN(97, TEXT('A'))));
    TestEqual(TEXT("Invalid rename is atomic"), Steps[3].GroupName, Original);
    return true;
}
#endif
