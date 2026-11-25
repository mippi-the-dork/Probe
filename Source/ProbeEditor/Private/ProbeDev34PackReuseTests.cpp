#include "ProbeDev34PackReuse.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
 FProbePackMixedEntry TestMember(const TCHAR* Name)
 { FProbePackMixedEntry E; E.Source=EProbePackMixedSource::NativeAutomation; E.Locator=Name; return E; }
 UProbeTestPack* EmptyPack() { return NewObject<UProbeTestPack>(GetTransientPackage()); }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev34MixedImport, "Probe.Editor.Dev34MixedImport", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev34MixedImport::RunTest(const FString&)
{
 UProbeTestPack* D=EmptyPack(); UProbeTestPack* S=EmptyPack();
 S->MixedEntries={TestMember(TEXT("Probe.A")),TestMember(TEXT("Probe.B"))};
 S->MixedEntries[1].Requires.Add(ProbeDev27::Identity(S->MixedEntries[0]));
 TArray<FProbePackMixedEntry> Out; ProbeDev34::FImportReport R;
 TestTrue(TEXT("Import two members"), ProbeDev34::PlanImport(*D,*S,Out,R));
 TestEqual(TEXT("Added"),R.Added,2); TestEqual(TEXT("Links"),R.DependencyLinks,1);
 TestEqual(TEXT("Source unmodified"),S->MixedEntries.Num(),2);
 FString Error; TestTrue(TEXT("Order valid"),ProbeDev33::HasValidOrder(Out,Error)); return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev34DuplicateReuse, "Probe.Editor.Dev34DuplicateReuse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev34DuplicateReuse::RunTest(const FString&)
{
 UProbeTestPack* D=EmptyPack(); UProbeTestPack* S=EmptyPack();
 D->MixedEntries={TestMember(TEXT("Probe.A"))};
 S->MixedEntries={TestMember(TEXT("Probe.A")),TestMember(TEXT("Probe.B"))};
 S->MixedEntries[1].Requires.Add(ProbeDev27::Identity(S->MixedEntries[0]));
 TArray<FProbePackMixedEntry> Out; ProbeDev34::FImportReport R;
 TestTrue(TEXT("Existing predecessor reused"),ProbeDev34::PlanImport(*D,*S,Out,R));
 TestEqual(TEXT("One new"),R.Added,1);TestEqual(TEXT("One duplicate"),R.Existing,1);
 TestEqual(TEXT("No duplicate"),Out.Num(),2);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev34Conflict, "Probe.Editor.Dev34Conflict", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev34Conflict::RunTest(const FString&)
{
 UProbeTestPack* D=EmptyPack(); UProbeTestPack* S=EmptyPack();
 D->MixedEntries={TestMember(TEXT("Probe.A")),TestMember(TEXT("Probe.B"))};
 S->MixedEntries=D->MixedEntries;S->MixedEntries[1].Requires.Add(ProbeDev27::Identity(S->MixedEntries[0]));
 S->MixedEntries.Add(TestMember(TEXT("Probe.C")));
 TArray<FProbePackMixedEntry> Out;ProbeDev34::FImportReport R;
 TestFalse(TEXT("Do not weaken existing dependency"),ProbeDev34::PlanImport(*D,*S,Out,R));
 TestTrue(TEXT("Informative diagnostic"),R.Error.Contains(TEXT("different prerequisites")));
 TestEqual(TEXT("Source unchanged"),S->MixedEntries.Num(),3);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev34Legacy, "Probe.Editor.Dev34Legacy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev34Legacy::RunTest(const FString&)
{
 UProbeTestPack* D=EmptyPack();UProbeTestPack* S=EmptyPack();
 S->Tests.Add(TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(TEXT("/Game/Test/PT_A.PT_A"))));
 S->Tests.Add(TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(TEXT("/Game/Test/PT_B.PT_B"))));
 FProbePackPrerequisite Rule;Rule.Test=S->Tests[1];Rule.Requires=S->Tests[0];S->Prerequisites.Add(Rule);
 TArray<FProbePackMixedEntry> Out;ProbeDev34::FImportReport R;
 TestTrue(TEXT("Import legacy references and dependency"),ProbeDev34::PlanImport(*D,*S,Out,R));
 TestEqual(TEXT("Two converted"),Out.Num(),2);TestEqual(TEXT("Prerequisite converted"),Out[1].Requires.Num(),1);
 TestEqual(TEXT("Legacy original untouched"),S->Tests.Num(),2);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev34AtomicFailure, "Probe.Editor.Dev34AtomicFailure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev34AtomicFailure::RunTest(const FString&)
{
 UProbeTestPack* D=EmptyPack(); UProbeTestPack* S=EmptyPack();
 D->MixedEntries={TestMember(TEXT("Probe.Existing"))};
 S->MixedEntries={TestMember(TEXT("Probe.New")),TestMember(TEXT("Probe.New"))};
 TArray<FProbePackMixedEntry> Out;ProbeDev34::FImportReport R;
 TestFalse(TEXT("Invalid source rejected"),ProbeDev34::PlanImport(*D,*S,Out,R));
 TestEqual(TEXT("Destination never modified"),D->MixedEntries.Num(),1);
 TestFalse(TEXT("Self import rejected"),ProbeDev34::PlanImport(*D,*D,Out,R));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev34DependencyView, "Probe.Editor.Dev34DependencyView", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev34DependencyView::RunTest(const FString&)
{
 TArray<FProbePackMixedEntry> A={TestMember(TEXT("Probe.A")),TestMember(TEXT("Probe.B"))};
 A[1].Requires.Add(ProbeDev27::Identity(A[0]));
 TestTrue(TEXT("Dependent count"),ProbeDev34::DependencySummary(A,0).Contains(TEXT("Required by 1")));
 TestTrue(TEXT("Readable predecessor"),ProbeDev34::DependencySummary(A,1).Contains(TEXT("#1")));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev34SelectionClosure, "Probe.Editor.Dev34SelectionClosure", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev34SelectionClosure::RunTest(const FString&)
{
 TArray<FProbePackMixedEntry> E={TestMember(TEXT("Probe.A")),TestMember(TEXT("Probe.B")),TestMember(TEXT("Probe.C")),TestMember(TEXT("Probe.D"))};
 E[1].Requires.Add(ProbeDev27::Identity(E[0]));
 E[2].Requires.Add(ProbeDev27::Identity(E[1]));
 TSet<FString> Selected={ProbeDev27::Identity(E[0])};
 const TSet<FString> Dependents=ProbeDev34::ExpandSelection(E,Selected,false);
 TestEqual(TEXT("Transitive dependents"),Dependents.Num(),3);
 TestFalse(TEXT("Independent member not included"),Dependents.Contains(ProbeDev27::Identity(E[3])));
 Selected.Reset();Selected.Add(ProbeDev27::Identity(E[2]));
 const TSet<FString> Upstream=ProbeDev34::ExpandSelection(E,Selected,true);
 TestEqual(TEXT("Transitive prerequisites"),Upstream.Num(),3);
 TestTrue(TEXT("Root included"),Upstream.Contains(ProbeDev27::Identity(E[0])));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev34AlreadyImported, "Probe.Editor.Dev34AlreadyImported", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev34AlreadyImported::RunTest(const FString&)
{
 UProbeTestPack* D=EmptyPack();UProbeTestPack* S=EmptyPack();
 D->MixedEntries={TestMember(TEXT("Probe.A"))}; S->MixedEntries=D->MixedEntries;
 TArray<FProbePackMixedEntry> Out;ProbeDev34::FImportReport R;
 TestFalse(TEXT("No-op import rejected"),ProbeDev34::PlanImport(*D,*S,Out,R));
 TestEqual(TEXT("No member added"),R.Added,0);
 TestEqual(TEXT("One duplicate"),R.Existing,1);
 TestTrue(TEXT("Explains no-op"),R.Error.Contains(TEXT("already exists")));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProbeDev34OutOfOrder, "Probe.Editor.Dev34OutOfOrder", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProbeDev34OutOfOrder::RunTest(const FString&)
{
 UProbeTestPack* D=EmptyPack(); UProbeTestPack* S=EmptyPack();
 D->MixedEntries={TestMember(TEXT("Probe.B"))};
 S->MixedEntries={TestMember(TEXT("Probe.A")),TestMember(TEXT("Probe.B")),TestMember(TEXT("Probe.C"))};
 S->MixedEntries[1].Requires.Add(ProbeDev27::Identity(S->MixedEntries[0]));
 TArray<FProbePackMixedEntry> Out;ProbeDev34::FImportReport R;
 TestFalse(TEXT("Reject dependency inversion with existing duplicate"),ProbeDev34::PlanImport(*D,*S,Out,R));
 TestTrue(TEXT("Shows ordered prerequisite cause"),R.Error.Contains(TEXT("prerequisite")));
 TestEqual(TEXT("Source unchanged"),S->MixedEntries.Num(),3);
 TestEqual(TEXT("Destination unchanged"),D->MixedEntries.Num(),1);
 return true;
}
#endif
