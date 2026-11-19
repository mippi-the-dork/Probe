#include "ProbeDev27MixedPolicy.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev27MixedEntryIdentity,
    "Probe.Editor.Dev27MixedIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev27MixedEntryIdentity::RunTest(const FString&)
{
    FProbePackMixedEntry Authored;
    Authored.Source = EProbePackMixedSource::AuthoredTest;
    Authored.Locator = TEXT("/Game/Tests/T_A.T_A");
    FProbePackMixedEntry Native;
    Native.Source = EProbePackMixedSource::NativeAutomation;
    Native.Locator = TEXT("Probe.Editor.Dev23NativeCompletionParsing");
    FString Error;
    TestTrue(TEXT("Authored path valid"), ProbeDev27::ValidateEntry(Authored, Error));
    TestTrue(TEXT("Native full path valid"), ProbeDev27::ValidateEntry(Native, Error));
    TestTrue(TEXT("Typed identities are separate"), ProbeDev27::Identity(Authored) != ProbeDev27::Identity(Native));
    Native.Source = EProbePackMixedSource::PortableTest;
    TestTrue(TEXT("Portable ID valid"), ProbeDev27::ValidateEntry(Native, Error));
    TestTrue(TEXT("Portable versus authored identity"), ProbeDev27::Identity(Authored) != ProbeDev27::Identity(Native));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev27MixedValidation,
    "Probe.Editor.Dev27MixedValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev27MixedValidation::RunTest(const FString&)
{
    FString Error;
    FProbePackMixedEntry Entry;
    Entry.Source = EProbePackMixedSource::NativeAutomation;
    TestFalse(TEXT("Empty native path rejected"), ProbeDev27::ValidateEntry(Entry, Error));
    Entry.Locator = TEXT(" unreal.path ");
    TestFalse(TEXT("Whitespace rejected"), ProbeDev27::ValidateEntry(Entry, Error));
    Entry.Locator = TEXT("unreal-automation://Unreal.Path");
    TestFalse(TEXT("URI is not a native full path"), ProbeDev27::ValidateEntry(Entry, Error));
    Entry.Locator = TEXT("Probe.Editor.Example");
    TestTrue(TEXT("Valid native path"), ProbeDev27::ValidateEntry(Entry, Error));
    UProbeTestPack* Pack = NewObject<UProbeTestPack>(GetTransientPackage());
    Pack->MixedEntries.Add(Entry);
    TestTrue(TEXT("One member is valid"), ProbeDev27::ValidatePack(*Pack, Error));
    Pack->MixedEntries.Add(Entry);
    TestFalse(TEXT("Duplicates rejected"), ProbeDev27::ValidatePack(*Pack, Error));
    Pack->MixedEntries.Pop();
    Pack->Tests.Add(TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(TEXT("/Game/Tests/Test.Test"))));
    TestFalse(TEXT("Mixed and legacy membership cannot silently coexist"), ProbeDev27::ValidatePack(*Pack, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev27MixedFailurePolicy,
    "Probe.Editor.Dev27MixedStopPolicy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev27MixedFailurePolicy::RunTest(const FString&)
{
    using Probe::EResultStatus;
    const EProbePackFailurePolicy Stop = EProbePackFailurePolicy::StopOnFailure;
    const EProbePackFailurePolicy Continue = EProbePackFailurePolicy::Continue;
    TestTrue(TEXT("Failure stops pack"), ProbeDev27::ShouldStop(Stop, EResultStatus::Failed));
    TestTrue(TEXT("Error stops pack"), ProbeDev27::ShouldStop(Stop, EResultStatus::Error));
    TestFalse(TEXT("Success continues"), ProbeDev27::ShouldStop(Stop, EResultStatus::Passed));
    TestFalse(TEXT("Interrupted does not become failure"), ProbeDev27::ShouldStop(Stop, EResultStatus::Interrupted));
    TestFalse(TEXT("Continue policy remains permissive"), ProbeDev27::ShouldStop(Continue, EResultStatus::Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev27MixedPackRunControl,
    "Probe.Editor.Dev27MixedPackRunControl", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev27MixedPackRunControl::RunTest(const FString&)
{
    TestFalse(TEXT("No selected Pack disables Run Pack"), ProbeDev27::HasRunMembers(nullptr));
    UProbeTestPack* Pack = NewObject<UProbeTestPack>(GetTransientPackage());
    TestFalse(TEXT("Empty Pack disables Run Pack"), ProbeDev27::HasRunMembers(Pack));
    FProbePackMixedEntry Member;
    Member.Source = EProbePackMixedSource::NativeAutomation;
    Member.Locator = TEXT("Probe.Editor.Dev23NativeCompletionParsing");
    Pack->MixedEntries.Add(Member);
    TestTrue(TEXT("Mixed-only Pack enables Run Pack"), ProbeDev27::HasRunMembers(Pack));
    Pack->MixedEntries.Reset();
    Pack->Tests.Add(TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(TEXT("/Game/Tests/Test.Test"))));
    TestTrue(TEXT("Legacy authored Pack still enables Run Pack"), ProbeDev27::HasRunMembers(Pack));
    return true;
}
#endif
