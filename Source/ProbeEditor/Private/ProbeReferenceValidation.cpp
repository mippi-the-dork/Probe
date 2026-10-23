#include "ProbeReferenceValidation.h"
#include "ProbeTestAsset.h"
#include "ProbeTestPack.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/UObjectGlobals.h"

namespace Probe
{
    FReferenceHealth InspectTestReference(const FSoftObjectPath& Path)
    {
        FReferenceHealth Health;
        Health.Path = Path.ToString();
        if (Path.IsNull())
        {
            Health.Status = EReferenceHealth::Empty;
            Health.Explanation = TEXT("Empty reference. Choose an existing Probe Test.");
            return Health;
        }
        // Support newly-created but unsaved Test Assets without touching the disk loader.
        if (UProbeTestAsset* Loaded = FindObject<UProbeTestAsset>(nullptr, *Health.Path))
        {
            Health.Status = EReferenceHealth::Valid;
            Health.Explanation = TEXT("Test exists in memory.");
            return Health;
        }
        FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        const FAssetData Data = Module.Get().GetAssetByObjectPath(Path);
        if (!Data.IsValid())
        {
            Health.Status = EReferenceHealth::Missing;
            Health.Explanation = FString::Printf(TEXT("No asset is registered at '%s'. It may have been moved, renamed, or deleted."), *Health.Path);
            return Health;
        }
        if (Data.AssetClassPath == UObjectRedirector::StaticClass()->GetClassPathName())
        {
            Health.Status = EReferenceHealth::Redirector;
            Health.Explanation = FString::Printf(TEXT("'%s' is a redirector. Replace the reference or fix up redirectors in the Content Browser."), *Health.Path);
            return Health;
        }
        if (!FPackageName::DoesPackageExist(Path.GetLongPackageName()))
        {
            Health.Status = EReferenceHealth::Missing;
            Health.Explanation = FString::Printf(TEXT("The registry lists '%s' but its package is not on disk. Refresh the Content Browser or replace this reference."), *Health.Path);
            return Health;
        }
        if (Data.AssetClassPath != UProbeTestAsset::StaticClass()->GetClassPathName())
        {
            Health.Status = EReferenceHealth::WrongType;
            Health.Explanation = FString::Printf(TEXT("'%s' is not a Probe Test asset."), *Health.Path);
            return Health;
        }
        Health.Status = EReferenceHealth::Valid;
        Health.Explanation = TEXT("Probe Test asset is registered.");
        return Health;
    }

    FString FReferenceAudit::ToText() const
    {
        FString Text = FString::Printf(TEXT("Probe Reference Validation\nPacks checked: %d | References: %d | Problems: %d | Duplicate Test IDs: %d\n"),
            PacksChecked, ReferencesChecked, Problems, DuplicateIds);
        if (Findings.IsEmpty()) { Text += TEXT("No reference problems found.\n"); }
        else
        {
            for (const FString& Finding : Findings) { Text += TEXT("\n") + Finding + TEXT("\n"); }
        }
        return Text;
    }

    FReferenceAudit AuditPackReferences(const UProbeTestPack* Pack)
    {
        FReferenceAudit Audit;
        if (!Pack) { Audit.Findings.Add(TEXT("No Pack selected.")); ++Audit.Problems; return Audit; }
        ++Audit.PacksChecked;
        TSet<FString> SeenPaths;
        for (int32 Index = 0; Index < Pack->Tests.Num(); ++Index)
        {
            const FSoftObjectPath Path = Pack->Tests[Index].ToSoftObjectPath();
            const FReferenceHealth Check = InspectTestReference(Path);
            ++Audit.ReferencesChecked;
            if (!Check.IsUsable())
            {
                ++Audit.Problems;
                Audit.Findings.Add(FString::Printf(TEXT("[%s] Test %d: %s"), *Pack->GetName(), Index + 1, *Check.Explanation));
            }
            if (!Check.Path.IsEmpty() && SeenPaths.Contains(Check.Path))
            {
                ++Audit.Problems;
                Audit.Findings.Add(FString::Printf(TEXT("[%s] Test %d: Duplicate Pack entry '%s'."), *Pack->GetName(), Index + 1, *Check.Path));
            }
            SeenPaths.Add(Check.Path);
        }
        return Audit;
    }

    FReferenceAudit AuditAllReferences()
    {
        FReferenceAudit Report;
        FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        FARFilter PackFilter;
        PackFilter.ClassPaths.Add(UProbeTestPack::StaticClass()->GetClassPathName());
        TArray<FAssetData> Packs;
        Module.Get().GetAssets(PackFilter, Packs);
        for (const FAssetData& Data : Packs)
        {
            UProbeTestPack* Pack = FindObject<UProbeTestPack>(nullptr, *Data.GetObjectPathString());
            if (!Pack && FPackageName::DoesPackageExist(Data.PackageName.ToString()))
            {
                Pack = Cast<UProbeTestPack>(Data.GetAsset());
            }
            if (!Pack) { ++Report.Problems; Report.Findings.Add(FString::Printf(TEXT("Unable to read Pack '%s'."), *Data.GetObjectPathString())); continue; }
            const FReferenceAudit One = AuditPackReferences(Pack);
            Report.PacksChecked += One.PacksChecked;
            Report.ReferencesChecked += One.ReferencesChecked;
            Report.Problems += One.Problems;
            Report.Findings.Append(One.Findings);
        }

        // Stable IDs must be unique even after normal Content Browser duplication.
        FARFilter TestFilter;
        TestFilter.ClassPaths.Add(UProbeTestAsset::StaticClass()->GetClassPathName());
        TArray<FAssetData> TestAssets;
        Module.Get().GetAssets(TestFilter, TestAssets);
        TMap<FGuid, FString> OriginalPaths;
        for (const FAssetData& Data : TestAssets)
        {
            const FString Path = Data.GetObjectPathString();
            UProbeTestAsset* Test = FindObject<UProbeTestAsset>(nullptr, *Path);
            if (!Test && FPackageName::DoesPackageExist(Data.PackageName.ToString()))
            {
                Test = Cast<UProbeTestAsset>(Data.GetAsset());
            }
            if (!Test) { continue; }
            // These checks require an existing asset. Negative assertions such as
            // Asset Does Not Exist must NEVER be reported as broken references.
            if (!Test->bNeedsConfiguration)
            {
                for (int32 StepIndex = 0; StepIndex < Test->Steps.Num(); ++StepIndex)
                {
                    const FProbeTestStep& Step = Test->Steps[StepIndex];
                    if (!Step.AssertionId.IsNone() || Step.Target.IsEmpty()) { continue; }
                    const bool bRequiresExistingAsset =
                        Step.Type == EProbeStepType::AssetExists || Step.Type == EProbeStepType::AssetClassIs
                        || Step.Type == EProbeStepType::AssetIsDirty || Step.Type == EProbeStepType::AssetIsClean
                        || Step.Type == EProbeStepType::AssetIsLoaded || Step.Type == EProbeStepType::AssetIsNotRedirector
                        || Step.Type == EProbeStepType::AssetRegistryTagEquals
                        || Step.Type == EProbeStepType::AssetRegistryTagExists
                        || Step.Type == EProbeStepType::AssetRegistryTagNotExists
                        || Step.Type == EProbeStepType::AssetRegistryTagValueContains
                        || Step.Type == EProbeStepType::AssetNameContains
                        || Step.Type == EProbeStepType::AssetNameStartsWith
                        || Step.Type == EProbeStepType::AssetNameEndsWith;
                    if (!bRequiresExistingAsset) { continue; }
                    ++Report.ReferencesChecked;
                    const FSoftObjectPath TargetPath(Step.Target);
                    if (TargetPath.IsNull()) { continue; }
                    if (!FindObject<UObject>(nullptr, *Step.Target)
                        && !Module.Get().GetAssetByObjectPath(TargetPath).IsValid())
                    {
                        ++Report.Problems;
                        Report.Findings.Add(FString::Printf(TEXT("Test '%s', step %d: expected asset '%s' is missing or moved."),
                            *Path, StepIndex + 1, *Step.Target));
                    }
                }
            }
            if (!Test->TestId.IsValid())
            {
                ++Report.Problems;
                Report.Findings.Add(FString::Printf(TEXT("Test '%s' has no stable Test ID."), *Path));
                continue;
            }
            if (const FString* Original = OriginalPaths.Find(Test->TestId))
            {
                ++Report.Problems;
                ++Report.DuplicateIds;
                Report.Findings.Add(FString::Printf(TEXT("Duplicate Test ID %s: '%s' and '%s'."), *Test->TestId.ToString(), **Original, *Path));
            }
            else { OriginalPaths.Add(Test->TestId, Path); }
        }
        return Report;
    }
}
