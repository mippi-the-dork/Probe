#include "ProbeTestRunner.h"
#include "ProbeAssertionCatalog.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/ARFilter.h"
#include "Components/ActorComponent.h"
#include "HAL/IConsoleManager.h"
#include "Framework/Docking/TabManager.h"
#include "ToolMenus.h"
#include "Editor.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Interfaces/IPluginManager.h"
#include "Containers/UnrealString.h"
#include "Modules/ModuleManager.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

namespace Probe
{
    const TCHAR* StatusName(EResultStatus Status)
    {
        switch (Status)
        {
        case EResultStatus::Passed: return TEXT("PASSED");
        case EResultStatus::Failed: return TEXT("FAILED");
        case EResultStatus::Skipped: return TEXT("SKIPPED");
        default: return TEXT("ERROR");
        }
    }

    static UWorld* GetEditorWorld()
    {
        return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    }

    static UClass* FindTestClass(const FString& Target)
    {
        UClass* Class = FindObject<UClass>(nullptr, *Target);
        if (!Class) { Class = LoadObject<UClass>(nullptr, *Target); }
        if (!Class)
        {
            for (TObjectIterator<UClass> It; It; ++It)
            {
                if (It->GetName() == Target) { Class = *It; break; }
            }
        }
        return Class;
    }

    static UObject* FindTestObject(const FString& Target)
    {
        UObject* Object = FindObject<UObject>(nullptr, *Target);
        return Object ? Object : LoadObject<UObject>(nullptr, *Target);
    }

    // Pure read-only reflection check, shared by asset, object, and actor property assertions.
    static EResultStatus CompareObjectProperty(UObject* Object, const FProbeTestStep& Step, FString& Reason, bool bExpectDifferent)
    {
        if (!Object || Step.PropertyName.TrimStartAndEnd().IsEmpty())
        {
            Reason = TEXT("Missing object or Property Name.");
            return EResultStatus::Error;
        }
        const FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), FName(*Step.PropertyName));
        if (!Property)
        {
            Reason = FString::Printf(TEXT("Property '%s' not found on '%s'"), *Step.PropertyName, *Object->GetPathName());
            return EResultStatus::Failed;
        }
        FString Actual;
        bool bEqual = false;
        if (const FStrProperty* Str = CastField<FStrProperty>(Property))
        {
            Actual = *Str->ContainerPtrToValuePtr<FString>(Object);
            bEqual = Actual == Step.ExpectedValue;
        }
        else if (const FNameProperty* Name = CastField<FNameProperty>(Property))
        {
            Actual = Name->ContainerPtrToValuePtr<FName>(Object)->ToString();
            bEqual = Actual == Step.ExpectedValue;
        }
        else if (const FBoolProperty* Bool = CastField<FBoolProperty>(Property))
        {
            Actual = Bool->GetPropertyValue_InContainer(Object) ? TEXT("True") : TEXT("False");
            const bool bExpectedTrue = Step.ExpectedValue.Equals(TEXT("True"), ESearchCase::IgnoreCase)
                || Step.ExpectedValue == TEXT("1");
            const bool bExpectedFalse = Step.ExpectedValue.Equals(TEXT("False"), ESearchCase::IgnoreCase)
                || Step.ExpectedValue == TEXT("0");
            if (!bExpectedTrue && !bExpectedFalse)
            {
                Reason = TEXT("Expected Value must be True, False, 1 or 0 for a Boolean property.");
                return EResultStatus::Error;
            }
            bEqual = Bool->GetPropertyValue_InContainer(Object) == bExpectedTrue;
        }
        else if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
        {
            Actual = Numeric->GetNumericPropertyValueToString_InContainer(Object);
            double ActualNumber = 0.0, ExpectedNumber = 0.0;
            if (!LexTryParseString(ActualNumber, *Actual) || !LexTryParseString(ExpectedNumber, *Step.ExpectedValue))
            {
                Reason = FString::Printf(TEXT("Expected numeric value '%s' is invalid (actual '%s')"), *Step.ExpectedValue, *Actual);
                return EResultStatus::Error;
            }
            bEqual = FMath::Abs(ActualNumber - ExpectedNumber) <= FMath::Max(0.0, Step.NumericTolerance);
        }
        else
        {
            Reason = FString::Printf(TEXT("Unsupported property type '%s'; supported: bool, numeric, FString, FName"), *Property->GetClass()->GetName());
            return EResultStatus::Error;
        }
        Reason = FString::Printf(TEXT("Property '%s' = '%s'; expected %s '%s'"), *Step.PropertyName, *Actual,
            bExpectDifferent ? TEXT("different from") : TEXT("equal to"), *Step.ExpectedValue);
        const bool bPass = bExpectDifferent ? !bEqual : bEqual;
        return bPass ? EResultStatus::Passed : EResultStatus::Failed;
    }

    static AActor* FindActorWithLabel(const FString& Label)
    {
        UWorld* World = GetEditorWorld();
        if (!World) { return nullptr; }
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->GetActorLabel().Equals(Label, ESearchCase::CaseSensitive)) { return *It; }
        }
        return nullptr;
    }

    static EResultStatus ExecuteStep(const FProbeTestStep& Step, FString& Reason)
    {
        const FString Target = Step.Target.TrimStartAndEnd();
        // Every assertion declares its required fields in the shared catalog.
        const bool bNeedsTarget = (GetAssertionInfo(Step.Type).Fields & TargetField) != 0;
        if (bNeedsTarget && Target.IsEmpty())
        {
            Reason = TEXT("Missing Target. Configure the step before running.");
            return EResultStatus::Error;
        }
        switch (Step.Type)
        {
        case EProbeStepType::PluginEnabled:
        {
            const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Target);
            const bool bPass = Plugin.IsValid() && Plugin->IsEnabled();
            Reason = bPass ? FString::Printf(TEXT("Plugin '%s' is enabled"), *Target)
                : FString::Printf(TEXT("Expected enabled plugin '%s', but it is missing or disabled"), *Target);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ClassExists:
        {
            const bool bPass = FindTestClass(Target) != nullptr;
            Reason = bPass ? FString::Printf(TEXT("Class '%s' found"), *Target)
                : FString::Printf(TEXT("Expected class '%s', but it was not found"), *Target);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::AssetExists:
        {
            FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            const FAssetData Asset = Registry.Get().GetAssetByObjectPath(FSoftObjectPath(Target));
            const bool bPass = Asset.IsValid();
            Reason = bPass ? FString::Printf(TEXT("Asset '%s' found"), *Target)
                : FString::Printf(TEXT("Expected asset '%s', but it was not found"), *Target);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::EditorWorldExists:
        {
            const bool bPass = GetEditorWorld() != nullptr;
            Reason = bPass ? TEXT("Editor world available") : TEXT("Expected an Editor world, but none is available");
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ActorCountAtLeast:
        case EProbeStepType::ActorClassCountAtLeast:
        {
            UWorld* World = GetEditorWorld();
            if (!World)
            {
                Reason = TEXT("No Editor world available");
                return EResultStatus::Failed;
            }
            UClass* RequiredClass = nullptr;
            if (Step.Type == EProbeStepType::ActorClassCountAtLeast)
            {
                RequiredClass = FindTestClass(Target);
                if (!RequiredClass || !RequiredClass->IsChildOf(AActor::StaticClass()))
                {
                    Reason = FString::Printf(TEXT("Expected an Actor class, but '%s' could not be resolved to one"), *Target);
                    return EResultStatus::Failed;
                }
            }
            int32 Count = 0;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (!RequiredClass || It->IsA(RequiredClass)) { ++Count; }
            }
            const bool bPass = Count >= Step.MinimumActorCount;
            Reason = RequiredClass
                ? FString::Printf(TEXT("Found %d actors of class '%s'; expected at least %d"), Count, *Target, Step.MinimumActorCount)
                : FString::Printf(TEXT("Found %d actors; expected at least %d"), Count, Step.MinimumActorCount);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::SelectedActorCountAtLeast:
        {
            if (!GetEditorWorld())
            {
                Reason = TEXT("No Editor world available");
                return EResultStatus::Failed;
            }
            const int32 Count = GEditor ? GEditor->GetSelectedActorCount() : 0;
            const bool bPass = Count >= Step.MinimumActorCount;
            Reason = FString::Printf(TEXT("Found %d selected level actors; expected at least %d"),
                Count, Step.MinimumActorCount);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ActorLabelExists:
        {
            UWorld* World = GetEditorWorld();
            if (!World)
            {
                Reason = TEXT("No Editor world available");
                return EResultStatus::Failed;
            }
            int32 Matches = 0;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (It->GetActorLabel().Equals(Target, ESearchCase::CaseSensitive))
                {
                    ++Matches;
                }
            }
            const bool bPass = Matches > 0;
            Reason = FString::Printf(TEXT("Found %d actors with exact Outliner label '%s'; expected at least one"), Matches, *Target);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ActorTagExists:
        {
            UWorld* World = GetEditorWorld();
            if (!World)
            {
                Reason = TEXT("No Editor world available");
                return EResultStatus::Failed;
            }
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (It->ActorHasTag(FName(*Target)))
                {
                    Reason = FString::Printf(TEXT("Found actor '%s' with tag '%s'"), *It->GetName(), *Target);
                    return EResultStatus::Passed;
                }
            }
            Reason = FString::Printf(TEXT("No actors have tag '%s' in the current Editor world"), *Target);
            return EResultStatus::Failed;
        }
        case EProbeStepType::AssetClassIs:
        {
            if (Step.ExpectedClass.TrimStartAndEnd().IsEmpty())
            {
                Reason = TEXT("Missing Expected Class. Set a class path such as /Script/ProbeEditor.ProbeTestAsset.");
                return EResultStatus::Error;
            }
            UClass* Class = FindTestClass(Step.ExpectedClass.TrimStartAndEnd());
            if (!Class)
            {
                Reason = FString::Printf(TEXT("Expected Class '%s' was not found"), *Step.ExpectedClass);
                return EResultStatus::Failed;
            }
            UObject* Object = FindTestObject(Target);
            const bool bPass = Object && Object->IsA(Class);
            Reason = bPass ? FString::Printf(TEXT("Asset '%s' is a '%s'"), *Target, *Class->GetName())
                : FString::Printf(TEXT("Asset '%s' is missing or is not a '%s'"), *Target, *Class->GetName());
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ObjectPropertyEquals:
        {
            if (Step.PropertyName.TrimStartAndEnd().IsEmpty())
            {
                Reason = TEXT("Missing Property Name.");
                return EResultStatus::Error;
            }
            UObject* Object = FindTestObject(Target);
            if (!Object)
            {
                Reason = FString::Printf(TEXT("Object '%s' not found"), *Target);
                return EResultStatus::Failed;
            }
            return CompareObjectProperty(Object, Step, Reason, false);
        }
        case EProbeStepType::PluginInstalled:
        case EProbeStepType::PluginDisabled:
        {
            const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Target);
            const bool bPass = Step.Type == EProbeStepType::PluginInstalled
                ? Plugin.IsValid() : Plugin.IsValid() && !Plugin->IsEnabled();
            Reason = FString::Printf(TEXT("Plugin '%s': %s; expected %s"), *Target,
                !Plugin.IsValid() ? TEXT("not installed") : Plugin->IsEnabled() ? TEXT("enabled") : TEXT("disabled"),
                Step.Type == EProbeStepType::PluginInstalled ? TEXT("installed") : TEXT("installed and disabled"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ModuleLoaded:
        {
            const bool bPass = FModuleManager::Get().IsModuleLoaded(FName(*Target));
            Reason = FString::Printf(TEXT("Module '%s' is %s; expected loaded"), *Target, bPass ? TEXT("loaded") : TEXT("not loaded"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ConsoleVariableExists:
        case EProbeStepType::ConsoleVariableEquals:
        {
            IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Target);
            if (!CVar)
            {
                Reason = FString::Printf(TEXT("Console variable '%s' is not registered"), *Target);
                return EResultStatus::Failed;
            }
            if (Step.Type == EProbeStepType::ConsoleVariableExists)
            {
                Reason = FString::Printf(TEXT("Console variable '%s' is registered"), *Target);
                return EResultStatus::Passed;
            }
            const FString Actual = CVar->GetString();
            const bool bPass = Actual == Step.ExpectedValue;
            Reason = FString::Printf(TEXT("Console variable '%s' = '%s'; expected '%s'"), *Target, *Actual, *Step.ExpectedValue);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::EditorTabOpen:
        {
            const bool bPass = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(FName(*Target))).IsValid();
            Reason = FString::Printf(TEXT("Editor tab '%s' is %s"), *Target, bPass ? TEXT("open") : TEXT("not open"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ToolMenuRegistered:
        {
            UToolMenus* Menus = UToolMenus::TryGet();
            const bool bPass = Menus && Menus->FindMenu(FName(*Target)) != nullptr;
            Reason = FString::Printf(TEXT("Tool menu '%s' is %s"), *Target, bPass ? TEXT("registered") : TEXT("not registered"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::AssetCountAtLeast:
        {
            if (!Target.StartsWith(TEXT("/")))
            {
                Reason = TEXT("Asset folder must be a package path such as /Game/ProbeTests or /Probe.");
                return EResultStatus::Error;
            }
            FARFilter Filter;
            Filter.PackagePaths.Add(FName(*Target));
            Filter.bRecursivePaths = true;
            TArray<FAssetData> Assets;
            FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            Registry.Get().GetAssets(Filter, Assets, true);
            const bool bPass = Assets.Num() >= Step.MinimumActorCount;
            Reason = FString::Printf(TEXT("Found %d assets under '%s'; expected at least %d"), Assets.Num(), *Target, Step.MinimumActorCount);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::AssetIsDirty:
        case EProbeStepType::AssetIsClean:
        {
            UObject* Object = FindTestObject(Target);
            if (!Object)
            {
                Reason = FString::Printf(TEXT("Asset '%s' could not be loaded"), *Target);
                return EResultStatus::Failed;
            }
            const bool bDirty = Object->GetOutermost()->IsDirty();
            const bool bPass = Step.Type == EProbeStepType::AssetIsDirty ? bDirty : !bDirty;
            Reason = FString::Printf(TEXT("Asset '%s' package is %s; expected %s"), *Target,
                bDirty ? TEXT("dirty") : TEXT("clean"), Step.Type == EProbeStepType::AssetIsDirty ? TEXT("dirty") : TEXT("clean"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::AssetIsLoaded:
        case EProbeStepType::ObjectLoaded:
        {
            // These assertions intentionally do not load the target; this distinction is useful for Editor lifecycle tests.
            const bool bPass = FindObject<UObject>(nullptr, *Target) != nullptr;
            Reason = FString::Printf(TEXT("Object '%s' is %s"), *Target, bPass ? TEXT("loaded") : TEXT("not loaded"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::AssetRegistryTagEquals:
        {
            if (Step.PropertyName.TrimStartAndEnd().IsEmpty())
            {
                Reason = TEXT("Missing Tag Name. Enter an Asset Registry searchable tag name.");
                return EResultStatus::Error;
            }
            FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            const FAssetData Data = Registry.Get().GetAssetByObjectPath(FSoftObjectPath(Target));
            if (!Data.IsValid())
            {
                Reason = FString::Printf(TEXT("Asset '%s' not found in the Asset Registry"), *Target);
                return EResultStatus::Failed;
            }
            FString Actual;
            const bool bFound = Data.GetTagValue(FName(*Step.PropertyName), Actual);
            const bool bPass = bFound && Actual == Step.ExpectedValue;
            Reason = bFound ? FString::Printf(TEXT("Registry tag '%s' = '%s'; expected '%s'"), *Step.PropertyName, *Actual, *Step.ExpectedValue)
                : FString::Printf(TEXT("Registry tag '%s' not found on '%s'"), *Step.PropertyName, *Target);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ClassIsChildOf:
        case EProbeStepType::ClassImplementsInterface:
        {
            if (Step.ExpectedClass.TrimStartAndEnd().IsEmpty())
            {
                Reason = TEXT("Missing Expected Class. Set a parent class or interface class.");
                return EResultStatus::Error;
            }
            UClass* Class = FindTestClass(Target);
            UClass* Other = FindTestClass(Step.ExpectedClass.TrimStartAndEnd());
            if (!Class || !Other)
            {
                Reason = FString::Printf(TEXT("Unable to resolve class '%s' or comparison class '%s'"), *Target, *Step.ExpectedClass);
                return EResultStatus::Failed;
            }
            if (Step.Type == EProbeStepType::ClassImplementsInterface && !Other->HasAnyClassFlags(CLASS_Interface))
            {
                Reason = FString::Printf(TEXT("'%s' is not a UInterface class"), *Step.ExpectedClass);
                return EResultStatus::Error;
            }
            const bool bPass = Step.Type == EProbeStepType::ClassIsChildOf ? Class->IsChildOf(Other) : Class->ImplementsInterface(Other);
            Reason = FString::Printf(TEXT("Class '%s' %s '%s': %s"), *Target,
                Step.Type == EProbeStepType::ClassIsChildOf ? TEXT("derives from") : TEXT("implements"), *Step.ExpectedClass,
                bPass ? TEXT("yes") : TEXT("no"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ClassHasProperty:
        case EProbeStepType::ClassHasFunction:
        {
            if (Step.PropertyName.TrimStartAndEnd().IsEmpty())
            {
                Reason = TEXT("Missing Property/Function Name.");
                return EResultStatus::Error;
            }
            UClass* Class = FindTestClass(Target);
            if (!Class)
            {
                Reason = FString::Printf(TEXT("Class '%s' not found"), *Target);
                return EResultStatus::Failed;
            }
            const bool bPass = Step.Type == EProbeStepType::ClassHasProperty
                ? FindFProperty<FProperty>(Class, FName(*Step.PropertyName)) != nullptr
                : Class->FindFunctionByName(FName(*Step.PropertyName)) != nullptr;
            Reason = FString::Printf(TEXT("Class '%s' %s '%s': %s"), *Target,
                Step.Type == EProbeStepType::ClassHasProperty ? TEXT("property") : TEXT("function"),
                *Step.PropertyName, bPass ? TEXT("found") : TEXT("not found"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ObjectPropertyExists:
        {
            if (Step.PropertyName.TrimStartAndEnd().IsEmpty())
            {
                Reason = TEXT("Missing Property Name.");
                return EResultStatus::Error;
            }
            UObject* Object = FindTestObject(Target);
            if (!Object)
            {
                Reason = FString::Printf(TEXT("Object '%s' not found"), *Target);
                return EResultStatus::Failed;
            }
            const bool bPass = FindFProperty<FProperty>(Object->GetClass(), FName(*Step.PropertyName)) != nullptr;
            Reason = FString::Printf(TEXT("Property '%s' on '%s': %s"), *Step.PropertyName, *Target,
                bPass ? TEXT("found") : TEXT("not found"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ActorLabelNotExists:
        case EProbeStepType::ActorTagNotExists:
        {
            UWorld* World = GetEditorWorld();
            if (!World)
            {
                Reason = TEXT("No Editor world available");
                return EResultStatus::Failed;
            }
            int32 Matches = 0;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (Step.Type == EProbeStepType::ActorLabelNotExists
                    ? It->GetActorLabel().Equals(Target, ESearchCase::CaseSensitive)
                    : It->ActorHasTag(FName(*Target))) { ++Matches; }
            }
            Reason = FString::Printf(TEXT("Found %d actors with %s '%s'; expected none"), Matches,
                Step.Type == EProbeStepType::ActorLabelNotExists ? TEXT("label") : TEXT("tag"), *Target);
            return Matches == 0 ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ActorHasComponent:
        {
            if (Step.ExpectedClass.TrimStartAndEnd().IsEmpty())
            {
                Reason = TEXT("Missing Expected Class. Provide a component class path.");
                return EResultStatus::Error;
            }
            UClass* ComponentClass = FindTestClass(Step.ExpectedClass.TrimStartAndEnd());
            if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
            {
                Reason = FString::Printf(TEXT("'%s' is not a component class"), *Step.ExpectedClass);
                return EResultStatus::Error;
            }
            AActor* Actor = FindActorWithLabel(Target);
            if (!Actor)
            {
                Reason = FString::Printf(TEXT("Actor with label '%s' not found"), *Target);
                return EResultStatus::Failed;
            }
            const bool bPass = Actor->GetComponentByClass(ComponentClass) != nullptr;
            Reason = FString::Printf(TEXT("Actor '%s' has component class '%s': %s"), *Target, *Step.ExpectedClass,
                bPass ? TEXT("yes") : TEXT("no"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ActorFolderEquals:
        {
            AActor* Actor = FindActorWithLabel(Target);
            if (!Actor)
            {
                Reason = FString::Printf(TEXT("Actor with label '%s' not found"), *Target);
                return EResultStatus::Failed;
            }
            const FString Actual = Actor->GetFolderPath().ToString();
            const bool bPass = Actual == Step.ExpectedValue;
            Reason = FString::Printf(TEXT("Actor '%s' Outliner folder = '%s'; expected '%s'"), *Target, *Actual, *Step.ExpectedValue);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ActorCountExactly:
        case EProbeStepType::ActorClassCountExactly:
        {
            UWorld* World = GetEditorWorld();
            if (!World)
            {
                Reason = TEXT("No Editor world available");
                return EResultStatus::Failed;
            }
            UClass* RequiredClass = nullptr;
            if (Step.Type == EProbeStepType::ActorClassCountExactly)
            {
                RequiredClass = FindTestClass(Target);
                if (!RequiredClass || !RequiredClass->IsChildOf(AActor::StaticClass()))
                {
                    Reason = FString::Printf(TEXT("'%s' does not resolve to an Actor class"), *Target);
                    return EResultStatus::Failed;
                }
            }
            int32 Count = 0;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (!RequiredClass || It->IsA(RequiredClass)) { ++Count; }
            }
            Reason = RequiredClass
                ? FString::Printf(TEXT("Found %d actors of class '%s'; expected exactly %d"), Count, *Target, Step.MinimumActorCount)
                : FString::Printf(TEXT("Found %d actors; expected exactly %d"), Count, Step.MinimumActorCount);
            return Count == Step.MinimumActorCount ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ActorPropertyEquals:
        {
            if (Step.PropertyName.TrimStartAndEnd().IsEmpty())
            {
                Reason = TEXT("Missing Property Name.");
                return EResultStatus::Error;
            }
            AActor* Actor = FindActorWithLabel(Target);
            if (!Actor)
            {
                Reason = FString::Printf(TEXT("Actor with label '%s' not found"), *Target);
                return EResultStatus::Failed;
            }
            return CompareObjectProperty(Actor, Step, Reason, false);
        }
        case EProbeStepType::ObjectPropertyNotEquals:
        {
            if (Step.PropertyName.TrimStartAndEnd().IsEmpty())
            {
                Reason = TEXT("Missing Property Name.");
                return EResultStatus::Error;
            }
            UObject* Object = FindTestObject(Target);
            if (!Object)
            {
                Reason = FString::Printf(TEXT("Object '%s' not found"), *Target);
                return EResultStatus::Failed;
            }
            return CompareObjectProperty(Object, Step, Reason, true);
        }
        case EProbeStepType::SelectedActorCountExactly:
        {
            if (!GetEditorWorld())
            {
                Reason = TEXT("No Editor world available");
                return EResultStatus::Failed;
            }
            const int32 Count = GEditor ? GEditor->GetSelectedActorCount() : 0;
            Reason = FString::Printf(TEXT("Found %d selected level actors; expected exactly %d"), Count, Step.MinimumActorCount);
            return Count == Step.MinimumActorCount ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::SelectedActorLabelEquals:
        case EProbeStepType::SelectedActorIsClass:
        case EProbeStepType::SelectedActorHasTag:
        {
            UWorld* World = GetEditorWorld();
            if (!World)
            {
                Reason = TEXT("No Editor world available");
                return EResultStatus::Failed;
            }
            if (!GEditor || GEditor->GetSelectedActorCount() != 1)
            {
                Reason = FString::Printf(TEXT("Expected exactly one selected actor; found %d"), GEditor ? GEditor->GetSelectedActorCount() : 0);
                return EResultStatus::Failed;
            }
            AActor* Selected = nullptr;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (It->IsSelectedInEditor()) { Selected = *It; break; }
            }
            if (!Selected)
            {
                Reason = TEXT("Selected actor not found in active Editor world");
                return EResultStatus::Failed;
            }
            bool bPass = false;
            if (Step.Type == EProbeStepType::SelectedActorLabelEquals)
            {
                bPass = Selected->GetActorLabel().Equals(Target, ESearchCase::CaseSensitive);
            }
            else if (Step.Type == EProbeStepType::SelectedActorHasTag)
            {
                bPass = Selected->ActorHasTag(FName(*Target));
            }
            else
            {
                UClass* Class = FindTestClass(Target);
                if (!Class || !Class->IsChildOf(AActor::StaticClass()))
                {
                    Reason = FString::Printf(TEXT("'%s' is not a valid Actor class"), *Target);
                    return EResultStatus::Failed;
                }
                bPass = Selected->IsA(Class);
            }
            Reason = FString::Printf(TEXT("Selected actor '%s' %s '%s': %s"), *Selected->GetActorLabel(),
                Step.Type == EProbeStepType::SelectedActorLabelEquals ? TEXT("has label")
                    : Step.Type == EProbeStepType::SelectedActorHasTag ? TEXT("has tag") : TEXT("is of class"),
                *Target, bPass ? TEXT("yes") : TEXT("no"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        default:
            Reason = TEXT("Unsupported step type");
            return EResultStatus::Error;
        }
    }

    FTestResult RunTest(const UProbeTestAsset* Asset)
    {
        FTestResult Result;
        if (!Asset) { Result.Detail = TEXT("Test asset could not be loaded"); return Result; }
        Result.Name = Asset->GetName();
        Result.Id = Asset->TestId.ToString();
        Result.AssetPath = Asset->GetPathName();
        const double Start = FPlatformTime::Seconds();
        if (!Asset->bEnabled)
        {
            Result.Status = EResultStatus::Skipped;
            Result.Detail = TEXT("Test disabled");
        }
        else if (Asset->Steps.IsEmpty())
        {
            Result.Status = EResultStatus::Error;
            Result.Detail = TEXT("Test has no steps. Use + Add Step to configure the test.");
        }
        else
        {
            Result.Status = EResultStatus::Passed;
            for (int32 Index = 0; Index < Asset->Steps.Num(); ++Index)
            {
                FStepResult StepResult;
                StepResult.Index = Index + 1;
                StepResult.Description = Asset->Steps[Index].Description;
                const double StepStart = FPlatformTime::Seconds();
                StepResult.Status = ExecuteStep(Asset->Steps[Index], StepResult.Detail);
                StepResult.DurationMs = (FPlatformTime::Seconds() - StepStart) * 1000.0;
                Result.Steps.Add(MoveTemp(StepResult));
                if (Result.Steps.Last().Status != EResultStatus::Passed)
                {
                    Result.Status = Result.Steps.Last().Status;
                    Result.Detail = FString::Printf(TEXT("Step %d: %s"), Index + 1, *Result.Steps.Last().Detail);
                    break;
                }
            }
            if (Result.Status == EResultStatus::Passed)
            {
                Result.Detail = FString::Printf(TEXT("All %d steps passed"), Asset->Steps.Num());
            }
        }
        Result.DurationMs = (FPlatformTime::Seconds() - Start) * 1000.0;
        return Result;
    }
}
