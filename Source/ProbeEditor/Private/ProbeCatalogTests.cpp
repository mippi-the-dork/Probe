#include "ProbeAssertionCatalog.h"
#include "ProbeTestRunner.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeCatalogCoverageTest,
    "Probe.Catalog.AllAssertionsRegistered",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeCatalogCoverageTest::RunTest(const FString& Parameters)
{
    const UEnum* Enum = StaticEnum<EProbeStepType>();
    if (!TestNotNull(TEXT("EProbeStepType should have reflection metadata"), Enum)) { return false; }

    TSet<uint8> Seen;
    for (const Probe::FAssertionInfo& Info : Probe::AssertionCatalog())
    {
        const uint8 Index = static_cast<uint8>(Info.Type);
        TestFalse(FString::Printf(TEXT("Duplicate assertion index %d"), Index), Seen.Contains(Index));
        Seen.Add(Index);
        TestTrue(FString::Printf(TEXT("Assertion %d needs a name"), Index), FCString::Strlen(Info.Name) > 0);
        TestTrue(FString::Printf(TEXT("Assertion %d needs guidance"), Index), FCString::Strlen(Info.Description) > 0);
        TestTrue(FString::Printf(TEXT("Assertion %d must have a category"), Index), FCString::Strlen(Info.Category) > 0);
        bool bFoundCategory = false;
        for (const FString& Category : Probe::AssertionCategories())
        {
            bFoundCategory |= Category == Info.Category;
        }
        TestTrue(FString::Printf(TEXT("Assertion %d category must exist"), Index), bFoundCategory);
    }
    TestEqual(TEXT("Assertion catalog covers all serialized enum values"), Seen.Num(), 41);
    // These original IDs must never change: Probe Tests are serialized assets.
    TestEqual(TEXT("Original PluginEnabled enum value"), static_cast<int32>(EProbeStepType::PluginEnabled), 0);
    TestEqual(TEXT("Original SelectedActorCountAtLeast enum value"), static_cast<int32>(EProbeStepType::SelectedActorCountAtLeast), 10);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeReadOnlyRunnerTest,
    "Probe.Runner.EditorPluginAssertions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProbeReadOnlyRunnerTest::RunTest(const FString& Parameters)
{
    UProbeTestAsset* Asset = NewObject<UProbeTestAsset>(GetTransientPackage());
    Asset->bEnabled = true;
    FProbeTestStep PluginStep;
    PluginStep.Type = EProbeStepType::PluginInstalled;
    PluginStep.Target = TEXT("Probe");
    Asset->Steps.Add(PluginStep);
    FProbeTestStep ClassStep;
    ClassStep.Type = EProbeStepType::ClassHasFunction;
    ClassStep.Target = TEXT("/Script/Engine.Actor");
    ClassStep.PropertyName = TEXT("GetActorLocation");
    Asset->Steps.Add(ClassStep);
    const Probe::FTestResult Result = Probe::RunTest(Asset);
    TestEqual(TEXT("Installed plugin and native Actor reflection should pass"), static_cast<int32>(Result.Status), static_cast<int32>(Probe::EResultStatus::Passed));
    TestEqual(TEXT("Two results should be returned"), Result.Steps.Num(), 2);
    return true;
}

#endif
