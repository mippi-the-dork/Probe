#include "ProbeTestRunner.h"
#include "ProbeDetailsSearchRestore.h"
#include "ProbeAssertionCatalog.h"
#include "ProbeExtensions.h"
#include "ProbeReferenceValidation.h"
#include "ToolMenu.h"
#include "UObject/ObjectRedirector.h"

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
#include "Engine/Level.h"
#include "EngineUtils.h"
#include "Selection.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"
#include "PluginDescriptor.h"
#include "ModuleDescriptor.h"
#include "Containers/UnrealString.h"
#include "Modules/ModuleManager.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"
#include "IAutomationDriverModule.h"
#include "IAutomationDriver.h"
#include "DriverConfiguration.h"
#include "AutomationDriverTypeDefs.h"
#include "IDriverElement.h"
#include "LocateBy.h"
#include "InputCoreTypes.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "PropertyEditorModule.h"
#include "IDetailsView.h"
#include "LevelEditor.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Layout/WidgetPath.h"
#include "Layout/ChildrenBase.h"
#include "Widgets/Input/SSearchBox.h"

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

    // UFUNCTION names are not always their Blueprint-facing names. For example,
    // AActor::K2_GetActorLocation is exported to Blueprints as GetActorLocation.
    // Do not mistake a plain unreflected C++ method for a reflected UFUNCTION.
    static UFunction* FindReflectedFunction(UClass* Class, const FString& Requested)
    {
        if (!Class) { return nullptr; }
        const FString Trimmed = Requested.TrimStartAndEnd();
        if (Trimmed.IsEmpty()) { return nullptr; }
        if (UFunction* Direct = Class->FindFunctionByName(FName(*Trimmed))) { return Direct; }
        FString Normalized = Trimmed;
        Normalized.ReplaceInline(TEXT(" "), TEXT(""));
        for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            UFunction* Function = *It;
            const FString NativeName = Function->GetName();
            if (NativeName.Equals(Trimmed, ESearchCase::IgnoreCase)
                || (NativeName.StartsWith(TEXT("K2_")) && NativeName.RightChop(3).Equals(Trimmed, ESearchCase::IgnoreCase)))
            {
                return Function;
            }
#if WITH_EDITOR
            const FString ScriptName = Function->GetMetaData(TEXT("ScriptName"));
            FString DisplayName = Function->GetMetaData(TEXT("DisplayName"));
            DisplayName.ReplaceInline(TEXT(" "), TEXT(""));
            if (ScriptName.Equals(Trimmed, ESearchCase::IgnoreCase)
                || DisplayName.Equals(Normalized, ESearchCase::IgnoreCase))
            {
                return Function;
            }
#endif
        }
        return nullptr;
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

    // Plain numeric triples are easier to author than an Unreal export string.
    // Require exactly three finite values, e.g. "100,0,50" (X,Y,Z or Pitch,Yaw,Roll).
    static bool ParseProbeTriple(const FString& Input, FVector& Out, FString& Reason)
    {
        TArray<FString> Parts;
        Input.ParseIntoArray(Parts, TEXT(","), false);
        if (Parts.Num() != 3)
        {
            Reason = TEXT("Expected three comma-separated numbers, e.g. 100,0,50.");
            return false;
        }
        double Numbers[3] = {};
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const FString Part = Parts[Index].TrimStartAndEnd();
            if (!LexTryParseString(Numbers[Index], *Part) || !FMath::IsFinite(Numbers[Index]))
            {
                Reason = FString::Printf(TEXT("Invalid numeric component '%s'. Use X,Y,Z or Pitch,Yaw,Roll."), *Part);
                return false;
            }
        }
        Out = FVector(Numbers[0], Numbers[1], Numbers[2]);
        return true;
    }

    // Fixtures live only for a synchronous test execution. They are rooted during the
    // run and unrooted on every exit path, including a failing assertion or action.
    struct FProbeRunContext
    {
        TMap<FString, UObject*> Objects;
        TMap<FString, TWeakObjectPtr<AActor>> ActorFixtures;
        TWeakObjectPtr<UWorld> FixtureWorld;
        bool bAllowTransientEditorActors = false;

        AActor* GetActor(const FString& Id) const
        {
            const TWeakObjectPtr<AActor>* Entry = ActorFixtures.Find(Id.TrimStartAndEnd());
            return Entry ? Entry->Get() : nullptr;
        }
        bool DestroyActorFixture(const FString& Id)
        {
            const FString Key = Id.TrimStartAndEnd();
            TWeakObjectPtr<AActor>* Entry = ActorFixtures.Find(Key);
            if (!Entry) { return false; }
            AActor* Actor = Entry->Get();
            ActorFixtures.Remove(Key);
            if (IsValid(Actor))
            {
                UWorld* World = Actor->GetWorld();
                if (IsValid(World))
                {
                    // False for bShouldModifyLevel: Probe must not deliberately mark
                    // the user's level modified as part of cleanup.
                    World->DestroyActor(Actor, false, false);
                }
            }
            return true;
        }
        bool CheckActorWorld(FString& Reason) const
        {
            if (!bAllowTransientEditorActors)
            {
                Reason = TEXT("Temporary level actors are disabled for this test. Enable 'Allow Transient Editor Actors' in the test header, review your level, and run again.");
                return false;
            }
            UWorld* World = GetEditorWorld();
            if (!World || World->WorldType != EWorldType::Editor || !GEditor || GEditor->PlayWorld)
            {
                Reason = TEXT("Transient Editor actor actions require a loaded Editor level with PIE stopped.");
                return false;
            }
            if (FixtureWorld.IsValid() && FixtureWorld.Get() != World)
            {
                Reason = TEXT("Editor world changed during fixture execution. Refusing to touch a different level.");
                return false;
            }
            return true;
        }

        // These snapshots belong to ONE test execution, not the whole Pack.
        TWeakObjectPtr<UWorld> OriginalSelectionWorld;
        TArray<TWeakObjectPtr<AActor>> OriginalSelectedActors;
        bool bTouchedSelection = false;

        bool BeginSelectionAction(FString& Reason)
        {
            if (!GEditor || !GetEditorWorld() || !GEditor->GetSelectedActors())
            {
                Reason = TEXT("A loaded Editor level and actor selection are required.");
                return false;
            }
            if (!bTouchedSelection)
            {
                OriginalSelectionWorld = GetEditorWorld();
                for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
                {
                    if (AActor* Actor = Cast<AActor>(*It))
                    { OriginalSelectedActors.Add(Actor); }
                }
                bTouchedSelection = true;
            }
            if (OriginalSelectionWorld.Get() != GetEditorWorld())
            {
                Reason = TEXT("Editor world changed during the test; refusing to modify another level's selection.");
                return false;
            }
            return true;
        }

        void RestoreActorSelection()
        {
            if (!bTouchedSelection || !GEditor || !OriginalSelectionWorld.IsValid()
                || GetEditorWorld() != OriginalSelectionWorld.Get()) { return; }
            GEditor->SelectNone(false, false, false);
            for (const TWeakObjectPtr<AActor>& Entry : OriginalSelectedActors)
            {
                AActor* Actor = Entry.Get();
                if (IsValid(Actor) && Actor->GetWorld() == OriginalSelectionWorld.Get())
                { GEditor->SelectActor(Actor, true, false, true); }
            }
            GEditor->NoteSelectionChange();
            // Allow another explicit action to capture a fresh snapshot only after
            // the original snapshot has been fully restored.
            bTouchedSelection = false;
            OriginalSelectedActors.Reset();
            OriginalSelectionWorld.Reset();
        }
        ~FProbeRunContext()
        {
            // Clean up ALL test-owned actors even when the first failing step
            // aborts execution. Never delete actors Probe did not create.
            TArray<FString> FixtureIds;
            ActorFixtures.GenerateKeyArray(FixtureIds);
            for (const FString& Id : FixtureIds) { DestroyActorFixture(Id); }
            RestoreActorSelection();
            for (const TPair<FString, UObject*>& Entry : Objects)
            {
                if (IsValid(Entry.Value)) { Entry.Value->RemoveFromRoot(); }
            }
        }
        UObject* Get(const FString& Id) const
        {
            UObject* const* Found = Objects.Find(Id.TrimStartAndEnd());
            return Found ? *Found : nullptr;
        }
        bool Add(const FString& Id, UObject* Object)
        {
            if (!Object || Id.TrimStartAndEnd().IsEmpty() || Objects.Contains(Id.TrimStartAndEnd())) { return false; }
            Object->AddToRoot();
            Objects.Add(Id.TrimStartAndEnd(), Object);
            return true;
        }
        bool Remove(const FString& Id)
        {
            if (UObject** Found = Objects.Find(Id.TrimStartAndEnd()))
            {
                if (IsValid(*Found)) { (*Found)->RemoveFromRoot(); }
                Objects.Remove(Id.TrimStartAndEnd());
                return true;
            }
            return false;
        }
    };

    static EResultStatus SetFixtureProperty(UObject* Object, const FProbeTestStep& Step, FString& Reason)
    {
        if (!Object || Step.PropertyName.IsEmpty())
        {
            Reason = TEXT("Fixture or property name missing."); return EResultStatus::Error;
        }
        FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), FName(*Step.PropertyName));
        if (!Property)
        {
            Reason = FString::Printf(TEXT("Property '%s' is not reflected on fixture '%s'."),
                *Step.PropertyName, *Object->GetClass()->GetName());
            return EResultStatus::Failed;
        }
        const FString Value = Step.ExpectedValue;
        if (FBoolProperty* Bool = CastField<FBoolProperty>(Property))
        {
            const bool bTrue = Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1");
            const bool bFalse = Value.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Value == TEXT("0");
            if (!bTrue && !bFalse) { Reason = TEXT("Boolean value must be True, False, 1 or 0."); return EResultStatus::Error; }
            Bool->SetPropertyValue_InContainer(Object, bTrue);
        }
        else if (FStrProperty* Str = CastField<FStrProperty>(Property))
        {
            *Str->ContainerPtrToValuePtr<FString>(Object) = Value;
        }
        else if (FNameProperty* Name = CastField<FNameProperty>(Property))
        {
            *Name->ContainerPtrToValuePtr<FName>(Object) = FName(*Value);
        }
        else if (FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
        {
            double Number = 0.0;
            if (!LexTryParseString(Number, *Value))
            { Reason = TEXT("Numeric property requires a valid numeric value."); return EResultStatus::Error; }
            void* Location = Numeric->ContainerPtrToValuePtr<void>(Object);
            Numeric->SetNumericPropertyValueFromString(Location, *Value);
        }
        else
        {
            Reason = FString::Printf(TEXT("Unsupported fixture property type '%s'. Use bool, number, FString or FName."), *Property->GetClass()->GetName());
            return EResultStatus::Error;
        }
        Reason = FString::Printf(TEXT("Changed temporary fixture property '%s' to '%s'."), *Step.PropertyName, *Value);
        return EResultStatus::Passed;
    }

    static EResultStatus ExecuteStep(const FProbeTestStep& Step, FProbeRunContext& Context, FString& Reason)
    {
        // Extension IDs are resolved first, preserving all historical built-in enum ordinals.
        // Missing providers are errors, never a silent pass/skip or an invalid enum access.
        if (!Step.AssertionId.IsNone())
        {
            const FExtensionAssertion* Extension = FindExtensionAssertion(Step.AssertionId);
            if (!Extension)
            {
                Reason = FString::Printf(TEXT("Required assertion provider '%s' is unavailable. Enable its testing module."),
                    *Step.AssertionId.ToString());
                return EResultStatus::Error;
            }
            if ((Extension->Fields & TargetField) && Step.Target.TrimStartAndEnd().IsEmpty())
            {
                Reason = FString::Printf(TEXT("Missing Target for '%s'"), *Step.AssertionId.ToString());
                return EResultStatus::Error;
            }
            return Extension->Evaluate(Step, Reason);
        }
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
        case EProbeStepType::ActorFixtureSpawn:
        {
            if (!Context.CheckActorWorld(Reason)) { return EResultStatus::Error; }
            const FString Id = Step.ExpectedValue.TrimStartAndEnd();
            if (Id.IsEmpty() || Context.ActorFixtures.Contains(Id))
            {
                Reason = TEXT("New Actor ID must be nonempty and unique within the current test.");
                return EResultStatus::Error;
            }
            UClass* Class = FindTestClass(Target);
            if (!Class || !Class->IsChildOf(AActor::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract))
            {
                Reason = FString::Printf(TEXT("'%s' is not a concrete AActor class."), *Target);
                return EResultStatus::Error;
            }
            UWorld* World = GetEditorWorld();
            FActorSpawnParameters Params;
            Params.Name = MakeUniqueObjectName(World->GetCurrentLevel(), Class, FName(TEXT("ProbeTransientActor")));
            Params.ObjectFlags = RF_Transient;
            Params.bTemporaryEditorActor = true;
            Params.bHideFromSceneOutliner = false;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            AActor* Actor = World->SpawnActor<AActor>(Class, FTransform::Identity, Params);
            if (!IsValid(Actor))
            {
                Reason = TEXT("Editor world refused to spawn the temporary actor.");
                return EResultStatus::Error;
            }
            Context.FixtureWorld = World;
            Context.ActorFixtures.Add(Id, Actor);
            Actor->SetActorLabel(FString::Printf(TEXT("Probe_%s"), *Id), false);
            Reason = FString::Printf(TEXT("Created transient actor '%s' (%s). Probe will destroy it when this test ends. The level may become dirty."), *Id, *Class->GetName());
            return EResultStatus::Passed;
        }
        case EProbeStepType::ActorFixtureExists:
        {
            const bool bExists = IsValid(Context.GetActor(Target));
            Reason = FString::Printf(TEXT("Temporary actor '%s' %s."), *Target, bExists ? TEXT("exists") : TEXT("does not exist"));
            return bExists ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ActorFixtureDestroy:
        {
            if (!Context.CheckActorWorld(Reason)) { return EResultStatus::Error; }
            const bool bRemoved = Context.DestroyActorFixture(Target);
            Reason = bRemoved ? FString::Printf(TEXT("Destroyed temporary actor '%s'."), *Target)
                : FString::Printf(TEXT("Temporary actor '%s' was not found."), *Target);
            return bRemoved ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ActorFixtureSetLabel:
        case EProbeStepType::ActorFixtureSetFolder:
        case EProbeStepType::ActorFixtureAddTag:
        case EProbeStepType::ActorFixtureRemoveTag:
        case EProbeStepType::ActorFixtureSelect:
        case EProbeStepType::ActorFixtureHide:
        case EProbeStepType::ActorFixtureShow:
        case EProbeStepType::ActorFixtureLabelEquals:
        case EProbeStepType::ActorFixtureHasTag:
        case EProbeStepType::ActorFixtureFolderEquals:
        case EProbeStepType::ActorFixtureIsSelected:
        case EProbeStepType::ActorFixtureIsHidden:
        {
            if (!Context.CheckActorWorld(Reason)) { return EResultStatus::Error; }
            AActor* Actor = Context.GetActor(Target);
            if (!IsValid(Actor) || Actor->GetWorld() != GetEditorWorld())
            {
                Reason = FString::Printf(TEXT("Temporary actor '%s' was not created in this test or no longer exists."), *Target);
                return EResultStatus::Error;
            }
            const FString Value = Step.ExpectedValue.TrimStartAndEnd();
            switch (Step.Type)
            {
            case EProbeStepType::ActorFixtureSetLabel:
                Actor->SetActorLabel(Value, false);
                Reason = FString::Printf(TEXT("Temporary actor '%s' label set to '%s'."), *Target, *Value);
                return EResultStatus::Passed;
            case EProbeStepType::ActorFixtureSetFolder:
                Actor->SetFolderPath(FName(*Value));
                Reason = FString::Printf(TEXT("Temporary actor '%s' folder set to '%s'."), *Target, *Value);
                return EResultStatus::Passed;
            case EProbeStepType::ActorFixtureAddTag:
                Actor->Tags.AddUnique(FName(*Value));
                Reason = FString::Printf(TEXT("Added tag '%s' to temporary actor '%s'."), *Value, *Target);
                return EResultStatus::Passed;
            case EProbeStepType::ActorFixtureRemoveTag:
                Actor->Tags.Remove(FName(*Value));
                Reason = FString::Printf(TEXT("Removed tag '%s' from temporary actor '%s'."), *Value, *Target);
                return EResultStatus::Passed;
            case EProbeStepType::ActorFixtureSelect:
                if (!Context.BeginSelectionAction(Reason)) { return EResultStatus::Error; }
                GEditor->SelectNone(false, false, false);
                GEditor->SelectActor(Actor, true, false, true);
                GEditor->NoteSelectionChange();
                Reason = FString::Printf(TEXT("Temporarily selected Probe actor '%s'. Prior selection will be restored."), *Target);
                return Actor->IsSelectedInEditor() ? EResultStatus::Passed : EResultStatus::Failed;
            case EProbeStepType::ActorFixtureHide:
            case EProbeStepType::ActorFixtureShow:
                Actor->SetIsTemporarilyHiddenInEditor(Step.Type == EProbeStepType::ActorFixtureHide);
                Reason = FString::Printf(TEXT("Temporary actor '%s' hidden: %s."), *Target,
                    Actor->IsTemporarilyHiddenInEditor() ? TEXT("yes") : TEXT("no"));
                return EResultStatus::Passed;
            case EProbeStepType::ActorFixtureLabelEquals:
            {
                const FString Actual = Actor->GetActorLabel();
                Reason = FString::Printf(TEXT("Temporary actor '%s' label is '%s'; expected '%s'."), *Target, *Actual, *Value);
                return Actual == Value ? EResultStatus::Passed : EResultStatus::Failed;
            }
            case EProbeStepType::ActorFixtureHasTag:
            {
                const bool bHas = Actor->ActorHasTag(FName(*Value));
                Reason = FString::Printf(TEXT("Temporary actor '%s' %s tag '%s'."), *Target, bHas ? TEXT("has") : TEXT("lacks"), *Value);
                return bHas ? EResultStatus::Passed : EResultStatus::Failed;
            }
            case EProbeStepType::ActorFixtureFolderEquals:
            {
                const FString Actual = Actor->GetFolderPath().ToString();
                Reason = FString::Printf(TEXT("Temporary actor '%s' folder '%s'; expected '%s'."), *Target, *Actual, *Value);
                return Actual == Value ? EResultStatus::Passed : EResultStatus::Failed;
            }
            case EProbeStepType::ActorFixtureIsSelected:
                Reason = FString::Printf(TEXT("Temporary actor '%s' selected: %s."), *Target, Actor->IsSelectedInEditor() ? TEXT("yes") : TEXT("no"));
                return Actor->IsSelectedInEditor() ? EResultStatus::Passed : EResultStatus::Failed;
            case EProbeStepType::ActorFixtureIsHidden:
                Reason = FString::Printf(TEXT("Temporary actor '%s' hidden: %s."), *Target, Actor->IsTemporarilyHiddenInEditor() ? TEXT("yes") : TEXT("no"));
                return Actor->IsTemporarilyHiddenInEditor() ? EResultStatus::Passed : EResultStatus::Failed;
            default: break;
            }
            Reason = TEXT("Unsupported actor fixture action.");
            return EResultStatus::Error;
        }
        // 0.13.0: transform and hierarchy actions target only run-owned actor IDs.
        case EProbeStepType::ActorFixtureSetLocation:
        case EProbeStepType::ActorFixtureLocationNearlyEquals:
        case EProbeStepType::ActorFixtureSetRotation:
        case EProbeStepType::ActorFixtureRotationNearlyEquals:
        case EProbeStepType::ActorFixtureSetScale:
        case EProbeStepType::ActorFixtureScaleNearlyEquals:
        case EProbeStepType::ActorFixtureAttachTo:
        case EProbeStepType::ActorFixtureDetach:
        case EProbeStepType::ActorFixtureHasParent:
        case EProbeStepType::ActorFixtureHasNoParent:
        case EProbeStepType::ActorFixtureChildCountExactly:
        case EProbeStepType::ActorFixtureTagAbsent:
        case EProbeStepType::ActorFixtureIsVisible:
        case EProbeStepType::ActorFixtureIsNotSelected:
        case EProbeStepType::ActorFixtureFolderNotEquals:
        case EProbeStepType::ActorFixtureDistanceAtMost:
        case EProbeStepType::ActorFixtureChildCountAtLeast:
        case EProbeStepType::ActorFixtureChildCountAtMost:
        case EProbeStepType::ActorFixtureDistanceAtLeast:
        case EProbeStepType::ActorFixtureLabelContains:
        case EProbeStepType::ActorFixtureLabelStartsWith:
        case EProbeStepType::ActorFixtureLabelEndsWith:
        case EProbeStepType::ActorFixtureTagCountExactly:
        case EProbeStepType::ActorFixtureHasRootComponent:
        case EProbeStepType::ActorFixtureLocationNotNearlyEquals:
        case EProbeStepType::ActorFixtureRotationNotNearlyEquals:
        case EProbeStepType::ActorFixtureScaleNotNearlyEquals:
        {
            if (!Context.CheckActorWorld(Reason)) { return EResultStatus::Error; }
            AActor* Actor = Context.GetActor(Target);
            if (!IsValid(Actor) || Actor->GetWorld() != GetEditorWorld())
            {
                Reason = FString::Printf(TEXT("Actor ID '%s' does not identify a living actor created by this test."), *Target);
                return EResultStatus::Error;
            }
            const FString Value = Step.ExpectedValue.TrimStartAndEnd();
            const bool bNeedsTriple = Step.Type == EProbeStepType::ActorFixtureSetLocation
                || Step.Type == EProbeStepType::ActorFixtureLocationNearlyEquals
                || Step.Type == EProbeStepType::ActorFixtureLocationNotNearlyEquals
                || Step.Type == EProbeStepType::ActorFixtureSetRotation
                || Step.Type == EProbeStepType::ActorFixtureRotationNearlyEquals
                || Step.Type == EProbeStepType::ActorFixtureRotationNotNearlyEquals
                || Step.Type == EProbeStepType::ActorFixtureSetScale
                || Step.Type == EProbeStepType::ActorFixtureScaleNearlyEquals
                || Step.Type == EProbeStepType::ActorFixtureScaleNotNearlyEquals;
            FVector Triple = FVector::ZeroVector;
            if (bNeedsTriple && !ParseProbeTriple(Value, Triple, Reason))
            { return EResultStatus::Error; }
            const double Tolerance = FMath::Max(0.0, Step.NumericTolerance);
            switch (Step.Type)
            {
            case EProbeStepType::ActorFixtureSetLocation:
                if (!Actor->GetRootComponent() || !Actor->SetActorLocation(Triple, false))
                {
                    Reason = TEXT("Unable to move temporary actor. Use a concrete actor class with a root component, such as /Script/Engine.StaticMeshActor.");
                    return EResultStatus::Error;
                }
                Reason = FString::Printf(TEXT("Actor '%s' location set to %s cm."), *Target, *Triple.ToString());
                return EResultStatus::Passed;
            case EProbeStepType::ActorFixtureLocationNearlyEquals:
            case EProbeStepType::ActorFixtureLocationNotNearlyEquals:
            {
                const FVector Actual = Actor->GetActorLocation();
                const bool bEquals = Actual.Equals(Triple, Tolerance);
                const bool bPass = Step.Type == EProbeStepType::ActorFixtureLocationNearlyEquals ? bEquals : !bEquals;
                Reason = FString::Printf(TEXT("Actor '%s' location %s; compared with %s (tolerance %.4f cm, %s)."),
                    *Target, *Actual.ToString(), *Triple.ToString(), Tolerance,
                    Step.Type == EProbeStepType::ActorFixtureLocationNearlyEquals ? TEXT("equals") : TEXT("not equals"));
                return bPass ? EResultStatus::Passed : EResultStatus::Failed;
            }
            case EProbeStepType::ActorFixtureSetRotation:
            {
                if (!Actor->GetRootComponent() || !Actor->SetActorRotation(FRotator(Triple.X, Triple.Y, Triple.Z)))
                {
                    Reason = TEXT("Unable to rotate temporary actor. Use an actor class with a root component.");
                    return EResultStatus::Error;
                }
                Reason = FString::Printf(TEXT("Actor '%s' rotation set to Pitch,Yaw,Roll %s degrees."), *Target, *Value);
                return EResultStatus::Passed;
            }
            case EProbeStepType::ActorFixtureRotationNearlyEquals:
            case EProbeStepType::ActorFixtureRotationNotNearlyEquals:
            {
                const FRotator Expected(Triple.X, Triple.Y, Triple.Z);
                const FRotator Actual = Actor->GetActorRotation();
                Reason = FString::Printf(TEXT("Actor '%s' rotation %s; compared with %s (tolerance %.4f degrees, %s)."),
                    *Target, *Actual.ToString(), *Expected.ToString(), Tolerance,
                    Step.Type == EProbeStepType::ActorFixtureRotationNearlyEquals ? TEXT("equals") : TEXT("not equals"));
                const bool bEquals = Actual.Equals(Expected, Tolerance);
                const bool bPass = Step.Type == EProbeStepType::ActorFixtureRotationNearlyEquals ? bEquals : !bEquals;
                return bPass ? EResultStatus::Passed : EResultStatus::Failed;
            }
            case EProbeStepType::ActorFixtureSetScale:
                if (!Actor->GetRootComponent())
                {
                    Reason = TEXT("Cannot set scale without a root component. Use /Script/Engine.StaticMeshActor.");
                    return EResultStatus::Error;
                }
                Actor->SetActorScale3D(Triple);
                Reason = FString::Printf(TEXT("Actor '%s' scale set to %s."), *Target, *Triple.ToString());
                return EResultStatus::Passed;
            case EProbeStepType::ActorFixtureScaleNearlyEquals:
            case EProbeStepType::ActorFixtureScaleNotNearlyEquals:
            {
                const FVector Actual = Actor->GetActorScale3D();
                Reason = FString::Printf(TEXT("Actor '%s' scale %s; compared with %s (tolerance %.4f, %s)."),
                    *Target, *Actual.ToString(), *Triple.ToString(), Tolerance,
                    Step.Type == EProbeStepType::ActorFixtureScaleNearlyEquals ? TEXT("equals") : TEXT("not equals"));
                const bool bEquals = Actual.Equals(Triple, Tolerance);
                const bool bPass = Step.Type == EProbeStepType::ActorFixtureScaleNearlyEquals ? bEquals : !bEquals;
                return bPass ? EResultStatus::Passed : EResultStatus::Failed;
            }
            case EProbeStepType::ActorFixtureAttachTo:
            {
                AActor* Parent = Context.GetActor(Value);
                if (!IsValid(Parent) || Parent == Actor || Parent->GetWorld() != Actor->GetWorld())
                {
                    Reason = TEXT("Parent Actor ID must identify another living actor owned by this test.");
                    return EResultStatus::Error;
                }
                if (!Actor->GetRootComponent() || !Parent->GetRootComponent())
                {
                    Reason = TEXT("Child and parent must have root components for attachment. Use /Script/Engine.StaticMeshActor for both.");
                    return EResultStatus::Error;
                }
                if (!Actor->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform))
                {
                    Reason = TEXT("Actor attachment failed (possibly due to a hierarchy cycle).");
                    return EResultStatus::Failed;
                }
                Reason = FString::Printf(TEXT("Attached test actor '%s' to test actor '%s'."), *Target, *Value);
                return EResultStatus::Passed;
            }
            case EProbeStepType::ActorFixtureDetach:
                if (!Actor->GetAttachParentActor())
                {
                    Reason = FString::Printf(TEXT("Actor '%s' is already detached."), *Target);
                    return EResultStatus::Failed;
                }
                Actor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
                Reason = FString::Printf(TEXT("Detached test actor '%s'."), *Target);
                return Actor->GetAttachParentActor() == nullptr ? EResultStatus::Passed : EResultStatus::Failed;
            case EProbeStepType::ActorFixtureHasParent:
            {
                AActor* Parent = Context.GetActor(Value);
                if (!IsValid(Parent))
                {
                    Reason = FString::Printf(TEXT("Parent Actor ID '%s' has not been created by this test."), *Value);
                    return EResultStatus::Error;
                }
                Reason = FString::Printf(TEXT("Actor '%s' parent: '%s'; expected '%s'."), *Target,
                    Actor->GetAttachParentActor() ? *Actor->GetAttachParentActor()->GetActorLabel() : TEXT("none"), *Value);
                return Actor->GetAttachParentActor() == Parent ? EResultStatus::Passed : EResultStatus::Failed;
            }
            case EProbeStepType::ActorFixtureHasNoParent:
                Reason = FString::Printf(TEXT("Actor '%s' %s an attachment parent."), *Target,
                    Actor->GetAttachParentActor() ? TEXT("has") : TEXT("does not have"));
                return Actor->GetAttachParentActor() == nullptr ? EResultStatus::Passed : EResultStatus::Failed;
            case EProbeStepType::ActorFixtureChildCountExactly:
            case EProbeStepType::ActorFixtureChildCountAtLeast:
            case EProbeStepType::ActorFixtureChildCountAtMost:
            {
                int32 Count = 0;
                for (const TPair<FString, TWeakObjectPtr<AActor>>& Entry : Context.ActorFixtures)
                {
                    AActor* Other = Entry.Value.Get();
                    if (IsValid(Other) && Other != Actor && Other->GetAttachParentActor() == Actor) { ++Count; }
                }
                Reason = FString::Printf(TEXT("Actor '%s' has %d direct test-owned children; expected %d."),
                    *Target, Count, Step.MinimumActorCount);
                const bool bPass = Step.Type == EProbeStepType::ActorFixtureChildCountExactly ? Count == Step.MinimumActorCount
                    : Step.Type == EProbeStepType::ActorFixtureChildCountAtLeast ? Count >= Step.MinimumActorCount
                    : Count <= Step.MinimumActorCount;
                return bPass ? EResultStatus::Passed : EResultStatus::Failed;
            }
            case EProbeStepType::ActorFixtureLabelContains:
            case EProbeStepType::ActorFixtureLabelStartsWith:
            case EProbeStepType::ActorFixtureLabelEndsWith:
            {
                const FString Label = Actor->GetActorLabel();
                const bool bPass = Step.Type == EProbeStepType::ActorFixtureLabelContains ? Label.Contains(Value)
                    : Step.Type == EProbeStepType::ActorFixtureLabelStartsWith ? Label.StartsWith(Value)
                    : Label.EndsWith(Value);
                Reason = FString::Printf(TEXT("Temporary actor '%s' label '%s'; pattern '%s': %s."),
                    *Target, *Label, *Value, bPass ? TEXT("matched") : TEXT("not matched"));
                return bPass ? EResultStatus::Passed : EResultStatus::Failed;
            }
            case EProbeStepType::ActorFixtureTagCountExactly:
            {
                const int32 Count = Actor->Tags.Num();
                Reason = FString::Printf(TEXT("Temporary actor '%s' has %d tags; expected exactly %d."),
                    *Target, Count, Step.MinimumActorCount);
                return Count == Step.MinimumActorCount ? EResultStatus::Passed : EResultStatus::Failed;
            }
            case EProbeStepType::ActorFixtureHasRootComponent:
                Reason = FString::Printf(TEXT("Temporary actor '%s' %s a root component."), *Target,
                    Actor->GetRootComponent() ? TEXT("has") : TEXT("does not have"));
                return Actor->GetRootComponent() ? EResultStatus::Passed : EResultStatus::Failed;
            case EProbeStepType::ActorFixtureTagAbsent:
            {
                const bool bAbsent = !Actor->ActorHasTag(FName(*Value));
                Reason = FString::Printf(TEXT("Actor '%s' tag '%s' is %s."), *Target, *Value, bAbsent ? TEXT("absent") : TEXT("present"));
                return bAbsent ? EResultStatus::Passed : EResultStatus::Failed;
            }
            case EProbeStepType::ActorFixtureIsVisible:
            {
                const bool bVisible = !Actor->IsTemporarilyHiddenInEditor();
                Reason = FString::Printf(TEXT("Actor '%s' Editor visibility: %s."), *Target, bVisible ? TEXT("visible") : TEXT("hidden"));
                return bVisible ? EResultStatus::Passed : EResultStatus::Failed;
            }
            case EProbeStepType::ActorFixtureIsNotSelected:
                Reason = FString::Printf(TEXT("Actor '%s' selected: %s."), *Target, Actor->IsSelectedInEditor() ? TEXT("yes") : TEXT("no"));
                return !Actor->IsSelectedInEditor() ? EResultStatus::Passed : EResultStatus::Failed;
            case EProbeStepType::ActorFixtureFolderNotEquals:
            {
                const FString Folder = Actor->GetFolderPath().ToString();
                Reason = FString::Printf(TEXT("Actor '%s' folder '%s' must differ from '%s'."), *Target, *Folder, *Value);
                return Folder != Value ? EResultStatus::Passed : EResultStatus::Failed;
            }
            case EProbeStepType::ActorFixtureDistanceAtMost:
            case EProbeStepType::ActorFixtureDistanceAtLeast:
            {
                AActor* Other = Context.GetActor(Value);
                if (!IsValid(Other))
                {
                    Reason = FString::Printf(TEXT("Other Actor ID '%s' was not created by this test."), *Value);
                    return EResultStatus::Error;
                }
                if (Step.MinimumActorCount < 0)
                {
                    Reason = TEXT("Maximum Distance must be nonnegative (centimeters).");
                    return EResultStatus::Error;
                }
                const double Distance = FVector::Dist(Actor->GetActorLocation(), Other->GetActorLocation());
                Reason = FString::Printf(TEXT("Distance between test actors '%s' and '%s': %.3f cm; expected %s %d cm."),
                    *Target, *Value, Distance,
                    Step.Type == EProbeStepType::ActorFixtureDistanceAtMost ? TEXT("<=") : TEXT(">="), Step.MinimumActorCount);
                const bool bPass = Step.Type == EProbeStepType::ActorFixtureDistanceAtMost
                    ? Distance <= Step.MinimumActorCount : Distance >= Step.MinimumActorCount;
                return bPass ? EResultStatus::Passed : EResultStatus::Failed;
            }
            default: break;
            }
            Reason = TEXT("Unknown temporary actor operation.");
            return EResultStatus::Error;
        }
        case EProbeStepType::SelectionClear:
        case EProbeStepType::SelectionSelectFirstActor:
        case EProbeStepType::SelectionSelectActorByLabel:
        case EProbeStepType::SelectionSelectActorsWithTag:
        {
            if (!Context.BeginSelectionAction(Reason)) { return EResultStatus::Error; }
            UWorld* World = GetEditorWorld();
            TArray<AActor*> ToSelect;
            if (Step.Type == EProbeStepType::SelectionSelectFirstActor)
            {
                // Sort by full path for deterministic selection across runs.
                for (TActorIterator<AActor> It(World); It; ++It)
                {
                    AActor* Actor = *It;
                    if (IsValid(Actor) && !Actor->IsA<AWorldSettings>())
                    { ToSelect.Add(Actor); }
                }
                ToSelect.Sort([](const AActor& A, const AActor& B)
                { return A.GetPathName() < B.GetPathName(); });
                if (ToSelect.Num() > 1) { ToSelect.SetNum(1); }
            }
            else if (Step.Type == EProbeStepType::SelectionSelectActorByLabel
                || Step.Type == EProbeStepType::SelectionSelectActorsWithTag)
            {
                for (TActorIterator<AActor> It(World); It; ++It)
                {
                    AActor* Actor = *It;
                    if (!IsValid(Actor)) { continue; }
                    const bool bMatch = Step.Type == EProbeStepType::SelectionSelectActorByLabel
                        ? Actor->GetActorLabel().Equals(Target, ESearchCase::CaseSensitive)
                        : Actor->ActorHasTag(FName(*Target));
                    if (bMatch) { ToSelect.Add(Actor); }
                }
                if (Step.Type == EProbeStepType::SelectionSelectActorByLabel && ToSelect.Num() > 1)
                {
                    Reason = FString::Printf(TEXT("Outliner label '%s' matches %d actors. Use a unique label before selecting."),
                        *Target, ToSelect.Num());
                    return EResultStatus::Error;
                }
            }
            if (Step.Type != EProbeStepType::SelectionClear && ToSelect.IsEmpty())
            {
                Reason = Step.Type == EProbeStepType::SelectionSelectFirstActor
                    ? TEXT("No selectable actors found in the current level.")
                    : FString::Printf(TEXT("No level actors match '%s'."), *Target);
                return EResultStatus::Failed;
            }
            GEditor->SelectNone(false, false, false);
            for (AActor* Actor : ToSelect) { GEditor->SelectActor(Actor, true, false, true); }
            GEditor->NoteSelectionChange();
            const int32 SelectedCount = GEditor->GetSelectedActorCount();
            if (SelectedCount != ToSelect.Num())
            {
                Reason = FString::Printf(TEXT("Requested selection of %d actors, but Editor selected %d. Original selection will be restored."),
                    ToSelect.Num(), SelectedCount);
                return EResultStatus::Failed;
            }
            Reason = FString::Printf(TEXT("Selected %d level actor(s) temporarily. Original selection is restored after this test."), SelectedCount);
            return EResultStatus::Passed;
        }
        case EProbeStepType::SelectionRestore:
        {
            if (!Context.bTouchedSelection)
            {
                Reason = TEXT("No previous selection action to restore in this test.");
                return EResultStatus::Error;
            }
            Context.RestoreActorSelection();
            Reason = TEXT("Original Editor actor selection restored.");
            return EResultStatus::Passed;
        }
        case EProbeStepType::FixtureCreateObject:
        {
            const FString Id = Step.ExpectedValue.TrimStartAndEnd();
            if (Id.IsEmpty()) { Reason = TEXT("Enter a New Fixture ID."); return EResultStatus::Error; }
            if (Context.Get(Id)) { Reason = FString::Printf(TEXT("Fixture ID '%s' already exists in this test."), *Id); return EResultStatus::Error; }
            UClass* Class = FindTestClass(Target);
            if (!Class || Class->HasAnyClassFlags(CLASS_Abstract)
                || Class->IsChildOf(AActor::StaticClass()) || Class->IsChildOf(UActorComponent::StaticClass())
                || Class->IsChildOf(UWorld::StaticClass()) || Class->IsChildOf(UPackage::StaticClass())
                || (Class->ClassWithin && !GetTransientPackage()->IsA(Class->ClassWithin.Get())))
            {
                Reason = TEXT("Choose a concrete non-Actor UObject class. Fixtures cannot spawn actors or alter loaded worlds.");
                return EResultStatus::Error;
            }
            UObject* Object = NewObject<UObject>(GetTransientPackage(), Class, NAME_None, RF_Transient);
            if (!Object || !Context.Add(Id, Object))
            {
                Reason = TEXT("Unable to create transient fixture."); return EResultStatus::Error;
            }
            Reason = FString::Printf(TEXT("Created transient '%s' fixture '%s'."), *Class->GetName(), *Id);
            return EResultStatus::Passed;
        }
        case EProbeStepType::FixtureSetProperty:
        {
            UObject* Object = Context.Get(Target);
            if (!Object) { Reason = FString::Printf(TEXT("Fixture '%s' was not created by an earlier step."), *Target); return EResultStatus::Error; }
            return SetFixtureProperty(Object, Step, Reason);
        }
        case EProbeStepType::FixturePropertyEquals:
        case EProbeStepType::FixturePropertyNotEquals:
        {
            UObject* Object = Context.Get(Target);
            if (!Object) { Reason = FString::Printf(TEXT("Fixture '%s' was not created by an earlier step."), *Target); return EResultStatus::Error; }
            return CompareObjectProperty(Object, Step, Reason, Step.Type == EProbeStepType::FixturePropertyNotEquals);
        }
        case EProbeStepType::FixtureObjectExists:
        {
            const bool bExists = Context.Get(Target) != nullptr;
            Reason = FString::Printf(TEXT("Temporary fixture '%s' %s."), *Target, bExists ? TEXT("exists") : TEXT("does not exist"));
            return bExists ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::FixtureDestroyObject:
        {
            const bool bRemoved = Context.Remove(Target);
            Reason = bRemoved ? FString::Printf(TEXT("Released temporary fixture '%s'."), *Target)
                : FString::Printf(TEXT("Fixture '%s' does not exist."), *Target);
            return bRemoved ? EResultStatus::Passed : EResultStatus::Failed;
        }
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
            if (!bPass)
            {
                if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Target))
                {
                    TArray<FString> Declared;
                    for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
                    {
                        Declared.Add(Module.Name.ToString());
                    }
                    if (!Declared.IsEmpty())
                    {
                        Reason += FString::Printf(TEXT(". '%s' is a plugin name; its module IDs are: %s"),
                            *Target, *FString::Join(Declared, TEXT(", ")));
                    }
                }
                Reason += TEXT(". Use Browse next to Module Name to select an exact ID.");
            }
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
            if (!bPass) { Reason += TEXT(". Use Browse to select an internal global tab ID; local asset-editor tabs may not be enumerated."); }
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
            const UFunction* MatchedFunction = Step.Type == EProbeStepType::ClassHasFunction
                ? FindReflectedFunction(Class, Step.PropertyName) : nullptr;
            const bool bPass = Step.Type == EProbeStepType::ClassHasProperty
                ? FindFProperty<FProperty>(Class, FName(*Step.PropertyName)) != nullptr
                : MatchedFunction != nullptr;
            Reason = FString::Printf(TEXT("Class '%s' %s '%s': %s%s"), *Target,
                Step.Type == EProbeStepType::ClassHasProperty ? TEXT("property") : TEXT("function"),
                *Step.PropertyName, bPass ? TEXT("found") : TEXT("not found"),
                MatchedFunction ? *FString::Printf(TEXT(" (reflected as '%s')"), *MatchedFunction->GetName()) : TEXT(""));
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
        case EProbeStepType::PluginFriendlyNameEquals:
        case EProbeStepType::PluginVersionEquals:
        case EProbeStepType::PluginDeclaresModule:
        case EProbeStepType::PluginHasContent:
        {
            TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Target);
            if (!Plugin.IsValid())
            {
                Reason = FString::Printf(TEXT("Plugin '%s' is not installed"), *Target);
                return EResultStatus::Failed;
            }
            if (Step.Type == EProbeStepType::PluginHasContent)
            {
                const bool bPass = Plugin->GetDescriptor().bCanContainContent;
                Reason = FString::Printf(TEXT("Plugin '%s' CanContainContent: %s"), *Target, bPass ? TEXT("true") : TEXT("false"));
                return bPass ? EResultStatus::Passed : EResultStatus::Failed;
            }
            if (Step.ExpectedValue.IsEmpty())
            {
                Reason = TEXT("Missing Expected Value. Set the expected name, version, or declared module.");
                return EResultStatus::Error;
            }
            if (Step.Type == EProbeStepType::PluginDeclaresModule)
            {
                bool bFound = false;
                for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
                {
                    bFound |= Module.Name.ToString().Equals(Step.ExpectedValue, ESearchCase::IgnoreCase);
                }
                Reason = FString::Printf(TEXT("Plugin '%s' %s module '%s'"), *Target,
                    bFound ? TEXT("declares") : TEXT("does not declare"), *Step.ExpectedValue);
                return bFound ? EResultStatus::Passed : EResultStatus::Failed;
            }
            const FString Actual = Step.Type == EProbeStepType::PluginVersionEquals
                ? Plugin->GetDescriptor().VersionName : Plugin->GetFriendlyName();
            const bool bPass = Actual == Step.ExpectedValue;
            Reason = FString::Printf(TEXT("Plugin '%s' value '%s'; expected '%s'"), *Target, *Actual, *Step.ExpectedValue);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ModuleNotLoaded:
        case EProbeStepType::ModuleExists:
        {
            const bool bLoaded = FModuleManager::Get().IsModuleLoaded(FName(*Target));
            const bool bPass = Step.Type == EProbeStepType::ModuleNotLoaded ? !bLoaded
                : FModuleManager::Get().ModuleExists(*Target);
            Reason = FString::Printf(TEXT("Module '%s' is %s; existence check %s"), *Target,
                bLoaded ? TEXT("loaded") : TEXT("not loaded"), bPass ? TEXT("passed") : TEXT("failed"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::EditorTabNotOpen:
        case EProbeStepType::EditorTabRegistered:
        {
            const bool bOpen = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(FName(*Target))).IsValid();
            const bool bPass = Step.Type == EProbeStepType::EditorTabRegistered
                ? FGlobalTabmanager::Get()->HasTabSpawner(FName(*Target)) : !bOpen;
            Reason = FString::Printf(TEXT("Global Editor tab '%s': %s, spawner %s"), *Target,
                bOpen ? TEXT("open") : TEXT("not open"),
                FGlobalTabmanager::Get()->HasTabSpawner(FName(*Target)) ? TEXT("registered") : TEXT("not registered"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ToolMenuNotRegistered:
        {
            UToolMenus* Menus = UToolMenus::TryGet();
            if (!Menus) { Reason = TEXT("ToolMenus subsystem unavailable"); return EResultStatus::Error; }
            const bool bAbsent = Menus->FindMenu(FName(*Target)) == nullptr;
            Reason = FString::Printf(TEXT("Tool menu '%s' %s"), *Target,
                bAbsent ? TEXT("not registered") : TEXT("is registered"));
            return bAbsent ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ConsoleVariableNotExists:
        case EProbeStepType::ConsoleVariableNotEquals:
        {
            IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(*Target);
            if (Step.Type == EProbeStepType::ConsoleVariableNotExists)
            {
                Reason = FString::Printf(TEXT("Console variable '%s' %s"), *Target,
                    Variable ? TEXT("exists") : TEXT("does not exist"));
                return Variable ? EResultStatus::Failed : EResultStatus::Passed;
            }
            if (!Variable) { Reason = FString::Printf(TEXT("Console variable '%s' not registered"), *Target); return EResultStatus::Failed; }
            const FString Actual = Variable->GetString();
            const bool bDifferent = Actual != Step.ExpectedValue;
            Reason = FString::Printf(TEXT("Console variable '%s' = '%s'; must differ from '%s'"), *Target, *Actual, *Step.ExpectedValue);
            return bDifferent ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::PIEIsRunning:
        case EProbeStepType::PIEIsNotRunning:
        {
            if (!GEditor) { Reason = TEXT("Editor unavailable"); return EResultStatus::Error; }
            const bool bRunning = GEditor->PlayWorld != nullptr;
            const bool bPass = Step.Type == EProbeStepType::PIEIsRunning ? bRunning : !bRunning;
            Reason = FString::Printf(TEXT("Play In Editor is %s"), bRunning ? TEXT("running") : TEXT("not running"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::AssetNotExists:
        {
            FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            const bool bFound = Registry.Get().GetAssetByObjectPath(FSoftObjectPath(Target)).IsValid();
            Reason = FString::Printf(TEXT("Asset '%s' %s"), *Target, bFound ? TEXT("exists") : TEXT("does not exist"));
            return bFound ? EResultStatus::Failed : EResultStatus::Passed;
        }
        case EProbeStepType::AssetCountExactly:
        {
            if (!Target.StartsWith(TEXT("/"))) { Reason = TEXT("Expected /Game/... or /Plugin/... package folder path"); return EResultStatus::Error; }
            FARFilter Filter;
            Filter.PackagePaths.Add(FName(*Target));
            Filter.bRecursivePaths = true;
            TArray<FAssetData> Assets;
            FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            Registry.Get().GetAssets(Filter, Assets, true);
            const bool bPass = Assets.Num() == Step.MinimumActorCount;
            Reason = FString::Printf(TEXT("Folder '%s' contains %d assets; expected exactly %d"), *Target, Assets.Num(), Step.MinimumActorCount);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::AssetRegistryTagExists:
        {
            if (Step.PropertyName.TrimStartAndEnd().IsEmpty()) { Reason = TEXT("Missing Tag Name"); return EResultStatus::Error; }
            FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            const FAssetData Data = Registry.Get().GetAssetByObjectPath(FSoftObjectPath(Target));
            if (!Data.IsValid()) { Reason = FString::Printf(TEXT("Asset '%s' not found in registry"), *Target); return EResultStatus::Failed; }
            FString Value;
            const bool bFound = Data.GetTagValue(FName(*Step.PropertyName), Value);
            Reason = FString::Printf(TEXT("Registry tag '%s' on '%s': %s"), *Step.PropertyName, *Target, bFound ? TEXT("found") : TEXT("missing"));
            return bFound ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ClassNotExists:
        case EProbeStepType::ClassIsAbstract:
        case EProbeStepType::ClassHasNoProperty:
        case EProbeStepType::ClassHasNoFunction:
        case EProbeStepType::ClassFunctionBlueprintCallable:
        {
            UClass* Class = FindTestClass(Target);
            if (Step.Type == EProbeStepType::ClassNotExists)
            {
                Reason = FString::Printf(TEXT("Class '%s': %s"), *Target, Class ? TEXT("found") : TEXT("not found"));
                return Class ? EResultStatus::Failed : EResultStatus::Passed;
            }
            if (!Class) { Reason = FString::Printf(TEXT("Class '%s' not found"), *Target); return EResultStatus::Failed; }
            if (Step.Type == EProbeStepType::ClassIsAbstract)
            {
                const bool bPass = Class->HasAnyClassFlags(CLASS_Abstract);
                Reason = FString::Printf(TEXT("Class '%s' abstract: %s"), *Target, bPass ? TEXT("yes") : TEXT("no"));
                return bPass ? EResultStatus::Passed : EResultStatus::Failed;
            }
            if (Step.PropertyName.TrimStartAndEnd().IsEmpty()) { Reason = TEXT("Missing Property/Function Name"); return EResultStatus::Error; }
            if (Step.Type == EProbeStepType::ClassHasNoProperty)
            {
                const bool bAbsent = FindFProperty<FProperty>(Class, FName(*Step.PropertyName)) == nullptr;
                Reason = FString::Printf(TEXT("Class '%s' property '%s': %s"), *Target, *Step.PropertyName, bAbsent ? TEXT("absent") : TEXT("found"));
                return bAbsent ? EResultStatus::Passed : EResultStatus::Failed;
            }
            UFunction* Function = FindReflectedFunction(Class, Step.PropertyName);
            if (Step.Type == EProbeStepType::ClassHasNoFunction)
            {
                Reason = FString::Printf(TEXT("Class '%s' reflected function '%s': %s"), *Target, *Step.PropertyName,
                    Function ? *Function->GetName() : TEXT("absent"));
                return Function ? EResultStatus::Failed : EResultStatus::Passed;
            }
            if (!Function) { Reason = FString::Printf(TEXT("Reflected function '%s' not found on class '%s'"), *Step.PropertyName, *Target); return EResultStatus::Failed; }
            const bool bPass = Function->HasAnyFunctionFlags(FUNC_BlueprintCallable);
            Reason = FString::Printf(TEXT("Function '%s' (resolved '%s') BlueprintCallable: %s"), *Step.PropertyName, *Function->GetName(), bPass ? TEXT("yes") : TEXT("no"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ObjectNotLoaded:
        {
            const bool bLoaded = FindObject<UObject>(nullptr, *Target) != nullptr;
            Reason = FString::Printf(TEXT("Object '%s' is %s"), *Target, bLoaded ? TEXT("loaded") : TEXT("not loaded"));
            return bLoaded ? EResultStatus::Failed : EResultStatus::Passed;
        }
        case EProbeStepType::ObjectPropertyGreaterThan:
        case EProbeStepType::ObjectPropertyLessThan:
        {
            UObject* Object = FindTestObject(Target);
            if (!Object) { Reason = FString::Printf(TEXT("Object '%s' not found"), *Target); return EResultStatus::Failed; }
            if (Step.PropertyName.TrimStartAndEnd().IsEmpty()) { Reason = TEXT("Missing Property Name"); return EResultStatus::Error; }
            const FNumericProperty* Numeric = CastField<FNumericProperty>(FindFProperty<FProperty>(Object->GetClass(), FName(*Step.PropertyName)));
            if (!Numeric) { Reason = FString::Printf(TEXT("Property '%s' is missing or not numeric"), *Step.PropertyName); return EResultStatus::Error; }
            double Expected = 0.0;
            if (!LexTryParseString(Expected, *Step.ExpectedValue)) { Reason = TEXT("Expected Value must be a number"); return EResultStatus::Error; }
            const FString ActualString = Numeric->GetNumericPropertyValueToString_InContainer(Object);
            double Actual = 0.0;
            if (!LexTryParseString(Actual, *ActualString)) { Reason = TEXT("Unable to parse numeric property"); return EResultStatus::Error; }
            const bool bPass = Step.Type == EProbeStepType::ObjectPropertyGreaterThan
                ? Actual > Expected + FMath::Max(0.0, Step.NumericTolerance)
                : Actual < Expected - FMath::Max(0.0, Step.NumericTolerance);
            Reason = FString::Printf(TEXT("Property '%s' = %s; expected %s %s (tolerance %.6f)"), *Step.PropertyName,
                *ActualString, Step.Type == EProbeStepType::ObjectPropertyGreaterThan ? TEXT("greater than") : TEXT("less than"),
                *Step.ExpectedValue, Step.NumericTolerance);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ActorCountAtMost:
        case EProbeStepType::ActorClassCountAtMost:
        case EProbeStepType::ActorLabelCountExactly:
        {
            UWorld* World = GetEditorWorld();
            if (!World) { Reason = TEXT("No Editor world"); return EResultStatus::Failed; }
            UClass* Class = Step.Type == EProbeStepType::ActorClassCountAtMost ? FindTestClass(Target) : nullptr;
            if (Step.Type == EProbeStepType::ActorClassCountAtMost && (!Class || !Class->IsChildOf(AActor::StaticClass())))
            {
                Reason = FString::Printf(TEXT("'%s' must resolve to an Actor class"), *Target);
                return EResultStatus::Error;
            }
            int32 Count = 0;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (Step.Type == EProbeStepType::ActorLabelCountExactly ? It->GetActorLabel().Equals(Target, ESearchCase::CaseSensitive)
                    : !Class || It->IsA(Class)) { ++Count; }
            }
            const bool bPass = Step.Type == EProbeStepType::ActorLabelCountExactly ? Count == Step.MinimumActorCount
                : Count <= Step.MinimumActorCount;
            Reason = FString::Printf(TEXT("Found %d matching level actors; expected %s %d"), Count,
                Step.Type == EProbeStepType::ActorLabelCountExactly ? TEXT("exactly") : TEXT("at most"), Step.MinimumActorCount);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ActorIsSelected:
        case EProbeStepType::ActorIsNotSelected:
        {
            AActor* Actor = FindActorWithLabel(Target);
            if (!Actor) { Reason = FString::Printf(TEXT("Actor with label '%s' not found"), *Target); return EResultStatus::Failed; }
            const bool bSelected = Actor->IsSelectedInEditor();
            const bool bPass = Step.Type == EProbeStepType::ActorIsSelected ? bSelected : !bSelected;
            Reason = FString::Printf(TEXT("Actor '%s' %s"), *Target, bSelected ? TEXT("selected") : TEXT("not selected"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::SelectedActorCountAtMost:
        {
            if (!GEditor) { Reason = TEXT("Editor unavailable"); return EResultStatus::Error; }
            const int32 Count = GEditor->GetSelectedActorCount();
            const bool bPass = Count <= Step.MinimumActorCount;
            Reason = FString::Printf(TEXT("Selected %d actors; expected at most %d"), Count, Step.MinimumActorCount);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::SelectedActorHasComponent:
        {
            if (!GEditor || GEditor->GetSelectedActorCount() != 1) { Reason = TEXT("Select exactly one actor"); return EResultStatus::Failed; }
            UClass* ComponentClass = FindTestClass(Step.ExpectedClass.TrimStartAndEnd());
            if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
            {
                Reason = TEXT("Expected Class must be an ActorComponent class"); return EResultStatus::Error;
            }
            UWorld* World = GetEditorWorld();
            if (!World) { Reason = TEXT("Editor world unavailable"); return EResultStatus::Failed; }
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (It->IsSelectedInEditor())
                {
                    const bool bPass = It->GetComponentByClass(ComponentClass) != nullptr;
                    Reason = FString::Printf(TEXT("Selected actor '%s' %s component '%s'"), *It->GetActorLabel(),
                        bPass ? TEXT("has") : TEXT("does not have"), *Step.ExpectedClass);
                    return bPass ? EResultStatus::Passed : EResultStatus::Failed;
                }
            }
            Reason = TEXT("Selected actor not found in Editor world"); return EResultStatus::Failed;
        }
        // 0.7.0: Additional read-only Editor and plugin assertions.
        case EProbeStepType::PluginIsEnginePlugin:
        case EProbeStepType::PluginIsProjectPlugin:
        case EProbeStepType::PluginModuleCountAtLeast:
        case EProbeStepType::PluginCategoryEquals:
        {
            const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Target);
            if (!Plugin) { Reason = FString::Printf(TEXT("Plugin '%s' not found"), *Target); return EResultStatus::Failed; }
            bool bPass = false;
            if (Step.Type == EProbeStepType::PluginModuleCountAtLeast)
            {
                const int32 Count = Plugin->GetDescriptor().Modules.Num();
                bPass = Count >= Step.MinimumActorCount;
                Reason = FString::Printf(TEXT("Plugin '%s' declares %d modules; expected at least %d"), *Target, Count, Step.MinimumActorCount);
            }
            else if (Step.Type == EProbeStepType::PluginCategoryEquals)
            {
                const FString Category = Plugin->GetDescriptor().Category;
                bPass = Category.Equals(Step.ExpectedValue, ESearchCase::IgnoreCase);
                Reason = FString::Printf(TEXT("Plugin '%s' category '%s'; expected '%s'"), *Target, *Category, *Step.ExpectedValue);
            }
            else
            {
                const bool bEngine = Plugin->GetLoadedFrom() == EPluginLoadedFrom::Engine;
                bPass = Step.Type == EProbeStepType::PluginIsEnginePlugin ? bEngine : !bEngine;
                Reason = FString::Printf(TEXT("Plugin '%s' is %s-installed"), *Target, bEngine ? TEXT("Engine") : TEXT("Project"));
            }
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::LoadedModuleCountAtLeast:
        {
            TArray<FModuleStatus> Modules;
            FModuleManager::Get().QueryModules(Modules);
            int32 Count = 0;
            for (const FModuleStatus& Module : Modules) { if (Module.bIsLoaded) { ++Count; } }
            Reason = FString::Printf(TEXT("Found %d loaded modules; expected at least %d"), Count, Step.MinimumActorCount);
            return Count >= Step.MinimumActorCount ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ToolMenuSectionExists:
        case EProbeStepType::ToolMenuSectionNotExists:
        {
            if (Step.ExpectedValue.TrimStartAndEnd().IsEmpty()) { Reason = TEXT("Enter section ID in Expected Value"); return EResultStatus::Error; }
            UToolMenus* Menus = UToolMenus::TryGet();
            if (!Menus) { Reason = TEXT("ToolMenus not available"); return EResultStatus::Error; }
            UToolMenu* Menu = Menus->FindMenu(FName(*Target));
            const bool bFound = Menu && Menu->FindSection(FName(*Step.ExpectedValue)) != nullptr;
            const bool bPass = Step.Type == EProbeStepType::ToolMenuSectionExists ? bFound : !bFound;
            Reason = FString::Printf(TEXT("Menu '%s' section '%s': %s"), *Target, *Step.ExpectedValue, bFound ? TEXT("found") : TEXT("not found"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::SelectedActorsEmpty:
        case EProbeStepType::SelectedActorsNotEmpty:
        {
            if (!GEditor) { Reason = TEXT("Editor unavailable"); return EResultStatus::Error; }
            const int32 Count = GEditor->GetSelectedActorCount();
            const bool bPass = Step.Type == EProbeStepType::SelectedActorsEmpty ? Count == 0 : Count > 0;
            Reason = FString::Printf(TEXT("Selected actor count: %d"), Count);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::AssetIsRedirector:
        case EProbeStepType::AssetIsNotRedirector:
        case EProbeStepType::AssetRegistryTagNotExists:
        {
            FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            const FAssetData Data = Registry.Get().GetAssetByObjectPath(FSoftObjectPath(Target));
            if (!Data.IsValid()) { Reason = FString::Printf(TEXT("Asset '%s' not found"), *Target); return EResultStatus::Failed; }
            if (Step.Type == EProbeStepType::AssetRegistryTagNotExists)
            {
                if (Step.PropertyName.TrimStartAndEnd().IsEmpty()) { Reason = TEXT("Missing Registry Tag Name"); return EResultStatus::Error; }
                FString Value;
                const bool bFound = Data.GetTagValue(FName(*Step.PropertyName), Value);
                Reason = FString::Printf(TEXT("Asset '%s' registry tag '%s' %s"), *Target, *Step.PropertyName, bFound ? TEXT("exists") : TEXT("is absent"));
                return bFound ? EResultStatus::Failed : EResultStatus::Passed;
            }
            const bool bRedirector = Data.AssetClassPath == UObjectRedirector::StaticClass()->GetClassPathName();
            const bool bPass = Step.Type == EProbeStepType::AssetIsRedirector ? bRedirector : !bRedirector;
            Reason = FString::Printf(TEXT("Asset '%s' redirector: %s"), *Target, bRedirector ? TEXT("yes") : TEXT("no"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ClassIsInterface:
        case EProbeStepType::ClassIsNotAbstract:
        case EProbeStepType::ClassPropertyBlueprintVisible:
        case EProbeStepType::ClassPropertyEditable:
        case EProbeStepType::ClassFunctionIsPure:
        case EProbeStepType::ClassFunctionIsStatic:
        case EProbeStepType::ClassFunctionHasParameter:
        {
            UClass* Class = FindTestClass(Target);
            if (!Class) { Reason = FString::Printf(TEXT("Class '%s' not found"), *Target); return EResultStatus::Failed; }
            bool bPass = false;
            if (Step.Type == EProbeStepType::ClassIsInterface || Step.Type == EProbeStepType::ClassIsNotAbstract)
            {
                bPass = Step.Type == EProbeStepType::ClassIsInterface ? Class->HasAnyClassFlags(CLASS_Interface)
                    : !Class->HasAnyClassFlags(CLASS_Abstract);
                Reason = FString::Printf(TEXT("Class '%s' %s check: %s"), *Target,
                    Step.Type == EProbeStepType::ClassIsInterface ? TEXT("interface") : TEXT("concrete"), bPass ? TEXT("yes") : TEXT("no"));
            }
            else if (Step.Type == EProbeStepType::ClassPropertyBlueprintVisible || Step.Type == EProbeStepType::ClassPropertyEditable)
            {
                if (Step.PropertyName.IsEmpty()) { Reason = TEXT("Missing Property Name"); return EResultStatus::Error; }
                const FProperty* Property = FindFProperty<FProperty>(Class, FName(*Step.PropertyName));
                bPass = Property && (Step.Type == EProbeStepType::ClassPropertyEditable
                    ? Property->HasAnyPropertyFlags(CPF_Edit)
                    : Property->HasAnyPropertyFlags(CPF_BlueprintVisible));
                Reason = FString::Printf(TEXT("Class '%s' property '%s' %s: %s"), *Target, *Step.PropertyName,
                    Step.Type == EProbeStepType::ClassPropertyEditable ? TEXT("editable") : TEXT("Blueprint visible"), bPass ? TEXT("yes") : TEXT("no"));
            }
            else
            {
                if (Step.PropertyName.IsEmpty()) { Reason = TEXT("Missing Function Name"); return EResultStatus::Error; }
                UFunction* Function = FindReflectedFunction(Class, Step.PropertyName);
                if (!Function) { Reason = FString::Printf(TEXT("Reflected function '%s' not found"), *Step.PropertyName); return EResultStatus::Failed; }
                if (Step.Type == EProbeStepType::ClassFunctionHasParameter)
                {
                    if (Step.ExpectedValue.IsEmpty()) { Reason = TEXT("Enter parameter name in Expected Value"); return EResultStatus::Error; }
                    for (TFieldIterator<FProperty> It(Function); It; ++It)
                    {
                        if (It->HasAnyPropertyFlags(CPF_Parm) && It->GetName().Equals(Step.ExpectedValue, ESearchCase::IgnoreCase))
                        { bPass = true; break; }
                    }
                }
                else bPass = Function->HasAnyFunctionFlags(Step.Type == EProbeStepType::ClassFunctionIsPure ? FUNC_BlueprintPure : FUNC_Static);
                Reason = FString::Printf(TEXT("Function '%s' %s: %s"), *Function->GetName(),
                    Step.Type == EProbeStepType::ClassFunctionHasParameter ? *FString::Printf(TEXT("parameter '%s'"), *Step.ExpectedValue)
                        : Step.Type == EProbeStepType::ClassFunctionIsPure ? TEXT("BlueprintPure") : TEXT("Static"),
                    bPass ? TEXT("yes") : TEXT("no"));
            }
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ActorLabelContains:
        case EProbeStepType::ActorCountWithTagAtLeast:
        case EProbeStepType::ActorHasTagByLabel:
        case EProbeStepType::ActorDoesNotHaveTagByLabel:
        {
            UWorld* World = GetEditorWorld();
            if (!World) { Reason = TEXT("No Editor world"); return EResultStatus::Failed; }
            if (Step.Type == EProbeStepType::ActorHasTagByLabel || Step.Type == EProbeStepType::ActorDoesNotHaveTagByLabel)
            {
                if (Step.ExpectedValue.IsEmpty()) { Reason = TEXT("Enter Actor Tag in Expected Value"); return EResultStatus::Error; }
                AActor* Actor = FindActorWithLabel(Target);
                if (!Actor) { Reason = FString::Printf(TEXT("Actor '%s' not found"), *Target); return EResultStatus::Failed; }
                const bool bHas = Actor->ActorHasTag(FName(*Step.ExpectedValue));
                const bool bPass = Step.Type == EProbeStepType::ActorHasTagByLabel ? bHas : !bHas;
                Reason = FString::Printf(TEXT("Actor '%s' tag '%s' %s"), *Target, *Step.ExpectedValue, bHas ? TEXT("present") : TEXT("absent"));
                return bPass ? EResultStatus::Passed : EResultStatus::Failed;
            }
            int32 Count = 0;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (Step.Type == EProbeStepType::ActorLabelContains ? It->GetActorLabel().Contains(Target)
                    : It->ActorHasTag(FName(*Target))) { ++Count; }
            }
            const bool bPass = Step.Type == EProbeStepType::ActorLabelContains ? Count > 0 : Count >= Step.MinimumActorCount;
            Reason = FString::Printf(TEXT("Found %d matching actors; expected %s"), Count,
                Step.Type == EProbeStepType::ActorLabelContains ? TEXT("at least one")
                    : *FString::Printf(TEXT("at least %d"), Step.MinimumActorCount));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::SelectedActorIsNotClass:
        case EProbeStepType::SelectedActorHasNoTag:
        case EProbeStepType::SelectedActorLabelContains:
        {
            if (!GEditor || GEditor->GetSelectedActorCount() != 1) { Reason = TEXT("Select exactly one actor"); return EResultStatus::Failed; }
            UWorld* World = GetEditorWorld();
            if (!World) { Reason = TEXT("Editor world unavailable"); return EResultStatus::Failed; }
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (!It->IsSelectedInEditor()) { continue; }
                bool bPass = false;
                if (Step.Type == EProbeStepType::SelectedActorLabelContains) { bPass = It->GetActorLabel().Contains(Target); }
                else if (Step.Type == EProbeStepType::SelectedActorHasNoTag) { bPass = !It->ActorHasTag(FName(*Target)); }
                else
                {
                    UClass* Class = FindTestClass(Target);
                    if (!Class) { Reason = FString::Printf(TEXT("Class '%s' not found"), *Target); return EResultStatus::Error; }
                    bPass = !It->IsA(Class);
                }
                Reason = FString::Printf(TEXT("Selected actor '%s': %s"), *It->GetActorLabel(), bPass ? TEXT("matched") : TEXT("did not match"));
                return bPass ? EResultStatus::Passed : EResultStatus::Failed;
            }
            Reason = TEXT("Selected actor not found"); return EResultStatus::Failed;
        }
        case EProbeStepType::ObjectPropertyStringContains:
        case EProbeStepType::ObjectPropertyStringStartsWith:
        case EProbeStepType::ObjectPropertyStringEndsWith:
        case EProbeStepType::ObjectPropertyIsTrue:
        case EProbeStepType::ObjectPropertyIsFalse:
        {
            UObject* Object = FindTestObject(Target);
            if (!Object) { Reason = FString::Printf(TEXT("Object '%s' not found"), *Target); return EResultStatus::Failed; }
            if (Step.PropertyName.IsEmpty()) { Reason = TEXT("Missing Property Name"); return EResultStatus::Error; }
            const FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), FName(*Step.PropertyName));
            if (!Property) { Reason = FString::Printf(TEXT("Property '%s' not found"), *Step.PropertyName); return EResultStatus::Failed; }
            bool bPass = false;
            if (Step.Type == EProbeStepType::ObjectPropertyIsTrue || Step.Type == EProbeStepType::ObjectPropertyIsFalse)
            {
                const FBoolProperty* Bool = CastField<FBoolProperty>(Property);
                if (!Bool) { Reason = TEXT("Property is not a Boolean"); return EResultStatus::Error; }
                const bool bActual = Bool->GetPropertyValue_InContainer(Object);
                bPass = bActual == (Step.Type == EProbeStepType::ObjectPropertyIsTrue);
                Reason = FString::Printf(TEXT("Property '%s' = %s"), *Step.PropertyName, bActual ? TEXT("True") : TEXT("False"));
            }
            else
            {
                const FStrProperty* Str = CastField<FStrProperty>(Property);
                if (!Str) { Reason = TEXT("Property is not an FString"); return EResultStatus::Error; }
                const FString Actual = *Str->ContainerPtrToValuePtr<FString>(Object);
                bPass = Step.Type == EProbeStepType::ObjectPropertyStringContains ? Actual.Contains(Step.ExpectedValue)
                    : Step.Type == EProbeStepType::ObjectPropertyStringStartsWith ? Actual.StartsWith(Step.ExpectedValue)
                    : Actual.EndsWith(Step.ExpectedValue);
                Reason = FString::Printf(TEXT("Property '%s' = '%s', pattern '%s'"), *Step.PropertyName, *Actual, *Step.ExpectedValue);
            }
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        // 0.8.0: additional read-only assertions designed for Editor plugin QA.
        case EProbeStepType::PluginModuleCountExactly:
        case EProbeStepType::PluginModuleCountAtMost:
        case EProbeStepType::PluginDescriptionContains:
        case EProbeStepType::PluginAuthorEquals:
        {
            const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Target);
            if (!Plugin) { Reason = FString::Printf(TEXT("Plugin '%s' not found"), *Target); return EResultStatus::Failed; }
            const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
            bool bPass = false;
            if (Step.Type == EProbeStepType::PluginDescriptionContains || Step.Type == EProbeStepType::PluginAuthorEquals)
            {
                if (Step.ExpectedValue.IsEmpty()) { Reason = TEXT("Enter expected description text or author"); return EResultStatus::Error; }
                const FString Actual = Step.Type == EProbeStepType::PluginDescriptionContains ? Descriptor.Description : Descriptor.CreatedBy;
                bPass = Step.Type == EProbeStepType::PluginDescriptionContains
                    ? Actual.Contains(Step.ExpectedValue, ESearchCase::IgnoreCase)
                    : Actual.Equals(Step.ExpectedValue, ESearchCase::IgnoreCase);
                Reason = FString::Printf(TEXT("Plugin '%s': '%s'; expected '%s'"), *Target, *Actual, *Step.ExpectedValue);
            }
            else
            {
                const int32 Count = Descriptor.Modules.Num();
                bPass = Step.Type == EProbeStepType::PluginModuleCountExactly
                    ? Count == Step.MinimumActorCount : Count <= Step.MinimumActorCount;
                Reason = FString::Printf(TEXT("Plugin '%s' declares %d module(s), expected %s %d"), *Target, Count,
                    Step.Type == EProbeStepType::PluginModuleCountExactly ? TEXT("exactly") : TEXT("at most"), Step.MinimumActorCount);
            }
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::LoadedModuleCountAtMost:
        {
            TArray<FModuleStatus> Modules;
            FModuleManager::Get().QueryModules(Modules);
            int32 Count = 0;
            for (const FModuleStatus& Module : Modules) { if (Module.bIsLoaded) { ++Count; } }
            Reason = FString::Printf(TEXT("Loaded %d modules; expected at most %d"), Count, Step.MinimumActorCount);
            return Count <= Step.MinimumActorCount ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::AssetCountAtMost:
        {
            FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            FARFilter Filter;
            Filter.PackagePaths.Add(FName(*Target));
            Filter.bRecursivePaths = true;
            TArray<FAssetData> Found;
            Registry.Get().GetAssets(Filter, Found, true);
            const int32 Count = Found.Num();
            Reason = FString::Printf(TEXT("Folder '%s' contains %d asset(s); maximum %d"), *Target, Count, Step.MinimumActorCount);
            return Count <= Step.MinimumActorCount ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::AssetNameContains:
        case EProbeStepType::AssetNameStartsWith:
        case EProbeStepType::AssetNameEndsWith:
        case EProbeStepType::AssetRegistryTagValueContains:
        {
            if (Step.ExpectedValue.IsEmpty()) { Reason = TEXT("Expected Value is required"); return EResultStatus::Error; }
            FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            const FAssetData Data = Registry.Get().GetAssetByObjectPath(FSoftObjectPath(Target));
            if (!Data.IsValid()) { Reason = FString::Printf(TEXT("Asset '%s' does not exist"), *Target); return EResultStatus::Failed; }
            FString Actual = Data.AssetName.ToString();
            if (Step.Type == EProbeStepType::AssetRegistryTagValueContains)
            {
                if (Step.PropertyName.IsEmpty()) { Reason = TEXT("Registry Tag Name is required"); return EResultStatus::Error; }
                if (!Data.GetTagValue(FName(*Step.PropertyName), Actual))
                { Reason = FString::Printf(TEXT("Asset tag '%s' missing"), *Step.PropertyName); return EResultStatus::Failed; }
            }
            const bool bPass = Step.Type == EProbeStepType::AssetNameStartsWith ? Actual.StartsWith(Step.ExpectedValue)
                : Step.Type == EProbeStepType::AssetNameEndsWith ? Actual.EndsWith(Step.ExpectedValue)
                : Actual.Contains(Step.ExpectedValue);
            Reason = FString::Printf(TEXT("Asset value '%s'; expected %s '%s'"), *Actual,
                Step.Type == EProbeStepType::AssetNameStartsWith ? TEXT("prefix")
                : Step.Type == EProbeStepType::AssetNameEndsWith ? TEXT("suffix") : TEXT("substring"), *Step.ExpectedValue);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ClassPropertyIsBoolean:
        case EProbeStepType::ClassPropertyIsNumeric:
        case EProbeStepType::ClassFunctionHasReturnValue:
        {
            UClass* Class = FindTestClass(Target);
            if (!Class) { Reason = FString::Printf(TEXT("Class '%s' not found"), *Target); return EResultStatus::Failed; }
            if (Step.PropertyName.IsEmpty()) { Reason = TEXT("Property or Function Name is required"); return EResultStatus::Error; }
            bool bPass = false;
            if (Step.Type == EProbeStepType::ClassFunctionHasReturnValue)
            {
                UFunction* Function = FindReflectedFunction(Class, Step.PropertyName);
                if (!Function) { Reason = TEXT("Reflected function not found"); return EResultStatus::Failed; }
                for (TFieldIterator<FProperty> It(Function); It; ++It)
                { if (It->HasAnyPropertyFlags(CPF_ReturnParm)) { bPass = true; break; } }
            }
            else
            {
                const FProperty* Property = FindFProperty<FProperty>(Class, FName(*Step.PropertyName));
                bPass = Property && (Step.Type == EProbeStepType::ClassPropertyIsBoolean
                    ? CastField<FBoolProperty>(Property) != nullptr : CastField<FNumericProperty>(Property) != nullptr);
            }
            Reason = FString::Printf(TEXT("Class '%s' member '%s': %s"), *Target, *Step.PropertyName, bPass ? TEXT("matched") : TEXT("not matched"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ActorLabelStartsWith:
        case EProbeStepType::ActorLabelEndsWith:
        case EProbeStepType::ActorLabelCountAtLeast:
        case EProbeStepType::ActorCountWithTagExactly:
        case EProbeStepType::ActorFolderCountAtLeast:
        {
            UWorld* World = GetEditorWorld();
            if (!World) { Reason = TEXT("Editor world unavailable"); return EResultStatus::Error; }
            int32 Count = 0;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                const FString Label = It->GetActorLabel();
                if (Step.Type == EProbeStepType::ActorLabelStartsWith ? Label.StartsWith(Target)
                    : Step.Type == EProbeStepType::ActorLabelEndsWith ? Label.EndsWith(Target)
                    : Step.Type == EProbeStepType::ActorLabelCountAtLeast ? Label == Target
                    : Step.Type == EProbeStepType::ActorCountWithTagExactly ? It->ActorHasTag(FName(*Target))
                    : It->GetFolderPath().ToString() == Target) { ++Count; }
            }
            const bool bPass = Step.Type == EProbeStepType::ActorLabelStartsWith || Step.Type == EProbeStepType::ActorLabelEndsWith
                ? Count > 0 : Step.Type == EProbeStepType::ActorCountWithTagExactly
                ? Count == Step.MinimumActorCount : Count >= Step.MinimumActorCount;
            Reason = FString::Printf(TEXT("Found %d actors for '%s'; expected %s %d"), Count, *Target,
                Step.Type == EProbeStepType::ActorCountWithTagExactly ? TEXT("exactly") : TEXT("at least"),
                (Step.Type == EProbeStepType::ActorLabelStartsWith || Step.Type == EProbeStepType::ActorLabelEndsWith) ? 1 : Step.MinimumActorCount);
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::SelectedActorLabelStartsWith:
        case EProbeStepType::SelectedActorLabelEndsWith:
        case EProbeStepType::SelectedActorFolderEquals:
        {
            if (!GEditor || GEditor->GetSelectedActorCount() != 1) { Reason = TEXT("Select exactly one level actor"); return EResultStatus::Failed; }
            UWorld* World = GetEditorWorld();
            if (!World) { Reason = TEXT("Editor world unavailable"); return EResultStatus::Error; }
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (!It->IsSelectedInEditor()) { continue; }
                const bool bPass = Step.Type == EProbeStepType::SelectedActorLabelStartsWith ? It->GetActorLabel().StartsWith(Target)
                    : Step.Type == EProbeStepType::SelectedActorLabelEndsWith ? It->GetActorLabel().EndsWith(Target)
                    : It->GetFolderPath().ToString() == Target;
                Reason = FString::Printf(TEXT("Selected actor '%s', folder '%s'; expected '%s'"), *It->GetActorLabel(), *It->GetFolderPath().ToString(), *Target);
                return bPass ? EResultStatus::Passed : EResultStatus::Failed;
            }
            Reason = TEXT("Selected actor was not found"); return EResultStatus::Failed;
        }
        case EProbeStepType::ObjectPropertyStringNotContains:
        case EProbeStepType::ObjectPropertyStringEqualsIgnoreCase:
        case EProbeStepType::ObjectPropertyNumericInRange:
        case EProbeStepType::ObjectPropertyNumericNearlyEquals:
        {
            UObject* Object = FindTestObject(Target);
            if (!Object) { Reason = FString::Printf(TEXT("Object '%s' not found"), *Target); return EResultStatus::Failed; }
            const FProperty* Property = FindFProperty<FProperty>(Object->GetClass(), FName(*Step.PropertyName));
            if (!Property) { Reason = FString::Printf(TEXT("Property '%s' not found"), *Step.PropertyName); return EResultStatus::Failed; }
            bool bPass = false;
            if (Step.Type == EProbeStepType::ObjectPropertyStringNotContains || Step.Type == EProbeStepType::ObjectPropertyStringEqualsIgnoreCase)
            {
                const FStrProperty* Str = CastField<FStrProperty>(Property);
                if (!Str) { Reason = TEXT("Property must be FString"); return EResultStatus::Error; }
                const FString Actual = *Str->ContainerPtrToValuePtr<FString>(Object);
                bPass = Step.Type == EProbeStepType::ObjectPropertyStringNotContains
                    ? !Actual.Contains(Step.ExpectedValue) : Actual.Equals(Step.ExpectedValue, ESearchCase::IgnoreCase);
                Reason = FString::Printf(TEXT("Property '%s' = '%s', expected '%s'"), *Step.PropertyName, *Actual, *Step.ExpectedValue);
            }
            else
            {
                const FNumericProperty* Numeric = CastField<FNumericProperty>(Property);
                if (!Numeric) { Reason = TEXT("Property must be numeric"); return EResultStatus::Error; }
                const FString NumberString = Numeric->GetNumericPropertyValueToString_InContainer(Object);
                double Actual = 0.0;
                if (!LexTryParseString(Actual, *NumberString)) { Reason = TEXT("Could not read numeric property"); return EResultStatus::Error; }
                if (Step.Type == EProbeStepType::ObjectPropertyNumericInRange)
                {
                    FString Lower, Upper;
                    double Low = 0.0, High = 0.0;
                    if (!Step.ExpectedValue.Split(TEXT(","), &Lower, &Upper) || !LexTryParseString(Low, *Lower.TrimStartAndEnd())
                        || !LexTryParseString(High, *Upper.TrimStartAndEnd()) || Low > High)
                    { Reason = TEXT("Expected Value must be a valid inclusive range: min,max (for example 0,1)"); return EResultStatus::Error; }
                    bPass = Actual >= Low && Actual <= High;
                    Reason = FString::Printf(TEXT("Property '%s' = %g, expected between %g and %g inclusive"), *Step.PropertyName, Actual, Low, High);
                }
                else
                {
                    double Expected = 0.0;
                    if (!LexTryParseString(Expected, *Step.ExpectedValue))
                    { Reason = TEXT("Expected Value must be a number"); return EResultStatus::Error; }
                    bPass = FMath::Abs(Actual - Expected) <= FMath::Max(0.0, Step.NumericTolerance);
                    Reason = FString::Printf(TEXT("Property '%s' = %g, expected %g (tolerance %g)"), *Step.PropertyName, Actual, Expected, Step.NumericTolerance);
                }
            }
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        // 0.10.0: additional read-only Editor plugin contracts.
        case EProbeStepType::PluginVersionContains:
        case EProbeStepType::PluginFriendlyNameContains:
        case EProbeStepType::PluginHasNoContent:
        {
            const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Target);
            if (!Plugin) { Reason = FString::Printf(TEXT("Plugin '%s' missing"), *Target); return EResultStatus::Failed; }
            const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
            if (Step.Type == EProbeStepType::PluginHasNoContent)
            {
                Reason = Descriptor.bCanContainContent ? TEXT("Plugin declares content") : TEXT("Plugin declares no content");
                return Descriptor.bCanContainContent ? EResultStatus::Failed : EResultStatus::Passed;
            }
            const FString Actual = Step.Type == EProbeStepType::PluginVersionContains ? Descriptor.VersionName : Descriptor.FriendlyName;
            Reason = FString::Printf(TEXT("Actual '%s'; must contain '%s'"), *Actual, *Step.ExpectedValue);
            return Actual.Contains(Step.ExpectedValue, ESearchCase::IgnoreCase) ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::LoadedModuleCountExactly:
        {
            TArray<FModuleStatus> Modules; FModuleManager::Get().QueryModules(Modules);
            int32 Count = 0;
            for (const FModuleStatus& Module : Modules) { if (Module.bIsLoaded) { ++Count; } }
            Reason = FString::Printf(TEXT("%d modules loaded; expected exactly %d"), Count, Step.MinimumActorCount);
            return Count == Step.MinimumActorCount ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::AssetCountDirectAtLeast:
        case EProbeStepType::AssetCountDirectExactly:
        {
            FARFilter Filter; Filter.PackagePaths.Add(FName(*Target)); Filter.bRecursivePaths = false;
            TArray<FAssetData> Found;
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter, Found, true);
            const int32 Count = Found.Num();
            Reason = FString::Printf(TEXT("%d direct assets in '%s'; expected %s %d"), Count, *Target,
                Step.Type == EProbeStepType::AssetCountDirectExactly ? TEXT("exactly") : TEXT("at least"), Step.MinimumActorCount);
            return (Step.Type == EProbeStepType::AssetCountDirectExactly ? Count == Step.MinimumActorCount : Count >= Step.MinimumActorCount)
                ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::AssetPackagePathEquals:
        case EProbeStepType::AssetClassNameEquals:
        {
            const FAssetData Data = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
                .Get().GetAssetByObjectPath(FSoftObjectPath(Target));
            if (!Data.IsValid()) { Reason = TEXT("Asset not found"); return EResultStatus::Failed; }
            const FString Actual = Step.Type == EProbeStepType::AssetPackagePathEquals ? Data.PackagePath.ToString()
                : Data.AssetClassPath.GetAssetName().ToString();
            Reason = FString::Printf(TEXT("Actual '%s'; expected '%s'"), *Actual, *Step.ExpectedValue);
            return Actual.Equals(Step.ExpectedValue, ESearchCase::IgnoreCase) ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ClassPropertyIsArray:
        case EProbeStepType::ClassPropertyIsObjectReference:
        case EProbeStepType::ClassPropertyIsStruct:
        case EProbeStepType::ClassPropertyIsEnum:
        case EProbeStepType::ClassPropertyIsName:
        case EProbeStepType::ClassPropertyIsString:
        case EProbeStepType::ClassPropertyIsText:
        {
            UClass* Class = FindTestClass(Target);
            if (!Class) { Reason = TEXT("Class not found"); return EResultStatus::Failed; }
            const FProperty* Property = FindFProperty<FProperty>(Class, FName(*Step.PropertyName));
            if (!Property) { Reason = TEXT("Property not found"); return EResultStatus::Failed; }
            bool bPass = false;
            switch (Step.Type)
            {
            case EProbeStepType::ClassPropertyIsArray: bPass = CastField<FArrayProperty>(Property) != nullptr; break;
            case EProbeStepType::ClassPropertyIsObjectReference: bPass = CastField<FObjectPropertyBase>(Property) != nullptr; break;
            case EProbeStepType::ClassPropertyIsStruct: bPass = CastField<FStructProperty>(Property) != nullptr; break;
            case EProbeStepType::ClassPropertyIsEnum:
                bPass = CastField<FEnumProperty>(Property) != nullptr;
                if (const FByteProperty* Byte = CastField<FByteProperty>(Property)) { bPass |= Byte->Enum != nullptr; }
                break;
            case EProbeStepType::ClassPropertyIsName: bPass = CastField<FNameProperty>(Property) != nullptr; break;
            case EProbeStepType::ClassPropertyIsString: bPass = CastField<FStrProperty>(Property) != nullptr; break;
            case EProbeStepType::ClassPropertyIsText: bPass = CastField<FTextProperty>(Property) != nullptr; break;
            default: break;
            }
            Reason = FString::Printf(TEXT("Property '%s' type '%s'; %s"), *Step.PropertyName, *Property->GetCPPType(),
                bPass ? TEXT("type matched") : TEXT("type did not match"));
            return bPass ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ClassFunctionIsConst:
        case EProbeStepType::ClassFunctionIsPublic:
        case EProbeStepType::ClassFunctionIsDeprecated:
        case EProbeStepType::ClassHasMetadata:
        {
            UClass* Class = FindTestClass(Target);
            if (!Class) { Reason = TEXT("Class not found"); return EResultStatus::Failed; }
            if (Step.Type == EProbeStepType::ClassHasMetadata)
            {
                const bool bMatch = Class->HasMetaData(FName(*Step.PropertyName));
                Reason = FString::Printf(TEXT("Class metadata key '%s': %s"), *Step.PropertyName, bMatch ? TEXT("present") : TEXT("absent"));
                return bMatch ? EResultStatus::Passed : EResultStatus::Failed;
            }
            UFunction* Function = FindReflectedFunction(Class, Step.PropertyName);
            if (!Function) { Reason = TEXT("Reflected function not found"); return EResultStatus::Failed; }
            // Unreal represents UFUNCTION deprecation with metadata, not FUNC_Deprecated.
            // Keep the const/public checks on their actual EFunctionFlags.
            const bool bDeprecatedCheck = Step.Type == EProbeStepType::ClassFunctionIsDeprecated;
            const bool bMatch = bDeprecatedCheck
                ? Function->HasMetaData(TEXT("DeprecatedFunction"))
                : Function->HasAnyFunctionFlags(
                    Step.Type == EProbeStepType::ClassFunctionIsConst ? FUNC_Const : FUNC_Public);
            const TCHAR* CheckedAttribute = bDeprecatedCheck ? TEXT("DeprecatedFunction metadata")
                : Step.Type == EProbeStepType::ClassFunctionIsConst ? TEXT("Const flag") : TEXT("Public flag");
            Reason = FString::Printf(TEXT("Function '%s' %s: %s"), *Function->GetName(), CheckedAttribute,
                bMatch ? TEXT("yes") : TEXT("no"));
            return bMatch ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::ActorLabelCountAtMost:
        case EProbeStepType::ActorCountWithTagAtMost:
        case EProbeStepType::ActorFolderCountExactly:
        case EProbeStepType::ActorFolderCountAtMost:
        {
            UWorld* World = GetEditorWorld();
            if (!World) { Reason = TEXT("Editor world unavailable"); return EResultStatus::Error; }
            int32 Count = 0;
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                const bool Match = Step.Type == EProbeStepType::ActorLabelCountAtMost ? It->GetActorLabel() == Target
                    : Step.Type == EProbeStepType::ActorCountWithTagAtMost ? It->ActorHasTag(FName(*Target))
                    : It->GetFolderPath().ToString() == Target;
                if (Match) { ++Count; }
            }
            const bool bExact = Step.Type == EProbeStepType::ActorFolderCountExactly;
            Reason = FString::Printf(TEXT("%d actors match '%s'; expected %s %d"), Count, *Target,
                bExact ? TEXT("exactly") : TEXT("at most"), Step.MinimumActorCount);
            return (bExact ? Count == Step.MinimumActorCount : Count <= Step.MinimumActorCount)
                ? EResultStatus::Passed : EResultStatus::Failed;
        }
        case EProbeStepType::SelectedActorIsTemporarilyHidden:
        case EProbeStepType::SelectedActorIsNotTemporarilyHidden:
        {
            if (!GEditor || GEditor->GetSelectedActorCount() != 1) { Reason = TEXT("Select exactly one actor"); return EResultStatus::Failed; }
            UWorld* World = GetEditorWorld();
            if (!World) { Reason = TEXT("No Editor world"); return EResultStatus::Error; }
            for (TActorIterator<AActor> It(World); It; ++It)
            {
                if (!It->IsSelectedInEditor()) { continue; }
                const bool bHidden = It->IsTemporarilyHiddenInEditor();
                Reason = FString::Printf(TEXT("Actor '%s' hidden in Editor: %s"), *It->GetActorLabel(), bHidden ? TEXT("yes") : TEXT("no"));
                return (bHidden == (Step.Type == EProbeStepType::SelectedActorIsTemporarilyHidden))
                    ? EResultStatus::Passed : EResultStatus::Failed;
            }
            Reason = TEXT("Selected actor not found"); return EResultStatus::Failed;
        }
        case EProbeStepType::UIWidgetClick:
        case EProbeStepType::UIWidgetTypeText:
        case EProbeStepType::UIDetailsSearchVisible:
        case EProbeStepType::UIDetailsSearchTextEquals:
        case EProbeStepType::UIDetailsSearchFilterActive:
        case EProbeStepType::UIWidgetExists:
        case EProbeStepType::UIWidgetNotExists:
        case EProbeStepType::UIWidgetVisible:
        case EProbeStepType::UIWidgetInteractable:
        case EProbeStepType::UIWidgetTextEquals:
        case EProbeStepType::UIWidgetTextContains:
        case EProbeStepType::UIWidgetTextNotEquals:
        case EProbeStepType::UIWaitForWidgetVisible:
        case EProbeStepType::UIWaitForWidgetTextEquals:
        case EProbeStepType::UIDetailsSearchTextContains:
        case EProbeStepType::UIDetailsSearchTextNotEquals:
        case EProbeStepType::UIDetailsSearchFilterInactive:
            Reason = TEXT("Native UI steps require RunTestAsync: synchronous input blocks the Editor game thread. Open and run this test in the Probe workspace.");
            return EResultStatus::Error;
        default:
            Reason = TEXT("Unsupported step type");
            return EResultStatus::Error;
        }
    }

    // Native UI automation runs *asynchronously*. Driving Slate input from
    // RunTest's synchronous game-thread loop would deadlock Automation Driver.
    static bool IsNativeUIStep(EProbeStepType Type)
    {
        return Type == EProbeStepType::UIWidgetClick || Type == EProbeStepType::UIWidgetTypeText
            || Type == EProbeStepType::UIDetailsSearchVisible
            || Type == EProbeStepType::UIDetailsSearchTextEquals
            || Type == EProbeStepType::UIDetailsSearchFilterActive
            || (Type >= EProbeStepType::UIWidgetExists && Type <= EProbeStepType::UIDetailsSearchFilterInactive);
    }

    static bool IsUIObservationStep(EProbeStepType Type)
    {
        return Type >= EProbeStepType::UIWidgetExists && Type <= EProbeStepType::UIWaitForWidgetTextEquals;
    }

    static void CollectDetailsSearchBoxes(const TSharedRef<SWidget>& Widget,
        TArray<TSharedRef<SSearchBox>>& Out, int32 Depth = 0)
    {
        if (Depth > 32 || Out.Num() > 4) { return; }
        if (Widget->GetTypeAsString() == TEXT("SSearchBox"))
        {
            Out.Add(StaticCastSharedRef<SSearchBox>(Widget));
            return;
        }
        if (FChildren* Children = Widget->GetChildren())
        {
            for (int32 Index = 0; Index < Children->Num(); ++Index)
            {
                CollectDetailsSearchBoxes(Children->GetChildAt(Index), Out, Depth + 1);
                if (Out.Num() > 4) { return; }
            }
        }
    }

    // A named native selector, not a screen coordinate. Unlike an automation
    // ID, the stock Details search is located inside *one specific* panel.
    static bool ResolveDetailsSearch(TSharedPtr<IDetailsView>& OutView,
        TSharedPtr<SSearchBox>& OutSearch, bool bActivateExistingTab, FString& Reason)
    {
        if (!FSlateApplication::IsInitialized())
        {
            Reason = TEXT("Slate is not initialized."); return false;
        }
        FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
        OutView = PropertyEditor.FindDetailView(LevelEditorTabIds::LevelEditorSelectionDetails);
        if (!OutView.IsValid())
        {
            Reason = TEXT("Level Editor Details 1 does not exist. Open Window > Details > Details 1.");
            return false;
        }
        TArray<TSharedRef<SSearchBox>> Searches;
        const TSharedPtr<SWidget> Filter = OutView->GetFilterAreaWidget();
        if (Filter.IsValid()) { CollectDetailsSearchBoxes(Filter.ToSharedRef(), Searches); }
        if (Searches.Num() == 0)
        {
            CollectDetailsSearchBoxes(StaticCastSharedRef<SWidget>(OutView.ToSharedRef()), Searches);
        }
        if (Searches.Num() != 1)
        {
            Reason = FString::Printf(TEXT("Expected one native Details 1 search widget; found %d."), Searches.Num());
            return false;
        }
        OutSearch = Searches[0];
        if (bActivateExistingTab)
        {
            TSharedPtr<SDockTab> Tab;
            if (TSharedPtr<FTabManager> Manager = OutView->GetHostTabManager())
            {
                Tab = Manager->FindExistingLiveTab(FTabId(LevelEditorTabIds::LevelEditorSelectionDetails));
            }
            if (!Tab.IsValid())
            {
                Tab = FGlobalTabmanager::Get()->FindExistingLiveTab(
                    FTabId(LevelEditorTabIds::LevelEditorSelectionDetails));
            }
            if (Tab.IsValid() && !Tab->IsForeground())
            {
                Tab->ActivateInParent(ETabActivationCause::SetDirectly);
                if (TSharedPtr<SWindow> Window = Tab->GetParentWindow()) { Window->BringToFront(false); }
            }
        }
        return true;
    }

    class FAsyncProbeTestRun : public TSharedFromThis<FAsyncProbeTestRun>
    {
    public:
        TStrongObjectPtr<UProbeTestAsset> Test;
        TArray<FProbeTestStep> Definitions;
        FProbeRunContext Context;
        FTestResult Result;
        TFunction<void(FTestResult)> OnComplete;
        int32 Index = 0;
        double StartedAt = 0.0;
        double StepStartedAt = 0.0;
        bool bDriverEnabledByProbe = false;
        bool bSavedSearch = false;
        bool bSavedFocus = false;
        FText OriginalSearchText;
        bool bOriginalSearchActive = false;
        TWeakPtr<SSearchBox> EditedSearch;
        TWeakPtr<IDetailsView> EditedView;
        TWeakPtr<SWidget> PreviousKeyboardFocus;

        void Begin(const UProbeTestAsset* Asset, TFunction<void(FTestResult)> Callback)
        {
            check(IsInGameThread());
            OnComplete = MoveTemp(Callback);
            StartedAt = FPlatformTime::Seconds();
            if (!Asset)
            {
                Result.Detail = TEXT("Test asset could not be loaded"); Finish(); return;
            }
            Test.Reset(const_cast<UProbeTestAsset*>(Asset));
            Definitions = Asset->Steps; // Don't observe edits made during an in-flight UI test.
            Result.Name = Asset->GetName();
            Result.Id = Asset->TestId.ToString();
            Result.AssetPath = Asset->GetPathName();
            Context.bAllowTransientEditorActors = Asset->bAllowTransientEditorActors;
            if (!Asset->bEnabled)
            {
                Result.Status = EResultStatus::Skipped;
                Result.Detail = TEXT("Test disabled"); Finish(); return;
            }
            if (Asset->bNeedsConfiguration)
            {
                Result.Status = EResultStatus::Skipped;
                Result.Detail = TEXT("Template needs configuration"); Finish(); return;
            }
            if (Definitions.IsEmpty())
            {
                Result.Detail = TEXT("Test has no steps"); Finish(); return;
            }
            Result.Status = EResultStatus::Passed;
            NextStep();
        }

        void NextStep()
        {
            check(IsInGameThread());
            if (Index >= Definitions.Num())
            {
                Result.Detail = FString::Printf(TEXT("All %d steps passed"), Definitions.Num());
                Finish(); return;
            }
            const FProbeTestStep& Step = Definitions[Index];
            StepStartedAt = FPlatformTime::Seconds();
            if (!Step.AssertionId.IsNone() || !IsNativeUIStep(Step.Type))
            {
                FString Reason;
                const EResultStatus Status = ExecuteStep(Step, Context, Reason);
                CompleteStep(Status, Reason); return;
            }
            const bool bInput = Step.Type == EProbeStepType::UIWidgetClick
                || Step.Type == EProbeStepType::UIWidgetTypeText;
            if (bInput && !Test->bAllowEditorUIInput)
            {
                CompleteStep(EResultStatus::Error,
                    TEXT("Live Editor input is not authorized. Enable Allow Editor UI Input on this Test."));
                return;
            }
            if (bInput)
            {
                BeginInput(Step); return;
            }
            if (IsUIObservationStep(Step.Type))
            {
                BeginObservation(Step); return;
            }
            TSharedPtr<IDetailsView> View;
            TSharedPtr<SSearchBox> Search;
            FString Reason;
            if (!ResolveDetailsSearch(View, Search, Step.Type == EProbeStepType::UIDetailsSearchVisible, Reason))
            {
                CompleteStep(EResultStatus::Error, Reason); return;
            }
            if (Step.Type == EProbeStepType::UIDetailsSearchVisible)
            {
                // Foregrounding an existing dock tab does not necessarily
                // arrange its widgets in the current Slate tick. Allow one
                // short, non-blocking settle before asserting actual visibility.
                TSharedRef<FAsyncProbeTestRun> Self = AsShared();
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
                    [Self](float)
                    {
                        TSharedPtr<IDetailsView> SettledView;
                        TSharedPtr<SSearchBox> SettledSearch;
                        FString Message;
                        if (!ResolveDetailsSearch(SettledView, SettledSearch, false, Message))
                        {
                            Self->CompleteStep(EResultStatus::Error, Message);
                            return false;
                        }
                        FWidgetPath Path;
                        const bool bVisible = SettledSearch->GetVisibility().IsVisible() && SettledSearch->IsEnabled()
                            && FSlateApplication::Get().FindPathToWidget(
                                SettledSearch.ToSharedRef(), Path, EVisibility::Visible);
                        Self->CompleteStep(bVisible ? EResultStatus::Passed : EResultStatus::Failed,
                            bVisible ? TEXT("Native Details 1 search is arranged, enabled and visible.")
                                     : TEXT("Native Details 1 search exists but is not in a visible Slate path."));
                        return false;
                    }), 0.15f);
                return;
            }
            const FString Actual = Search->GetText().ToString();
            if (Step.Type == EProbeStepType::UIDetailsSearchTextEquals
                || Step.Type == EProbeStepType::UIDetailsSearchTextContains
                || Step.Type == EProbeStepType::UIDetailsSearchTextNotEquals)
            {
                const bool bMatch = Step.Type == EProbeStepType::UIDetailsSearchTextEquals
                    ? Actual == Step.ExpectedValue
                    : Step.Type == EProbeStepType::UIDetailsSearchTextContains
                    ? Actual.Contains(Step.ExpectedValue, ESearchCase::CaseSensitive)
                    : Actual != Step.ExpectedValue;
                const TCHAR* Operator = Step.Type == EProbeStepType::UIDetailsSearchTextEquals
                    ? TEXT("equals") : Step.Type == EProbeStepType::UIDetailsSearchTextContains
                    ? TEXT("contains") : TEXT("does not equal");
                CompleteStep(bMatch ? EResultStatus::Passed : EResultStatus::Failed,
                    FString::Printf(TEXT("Details 1 search text='%s'; %s '%s'"), *Actual, Operator, *Step.ExpectedValue));
                return;
            }
            const bool bActive = View->HasActiveSearch();
            const bool bExpectActive = Step.Type == EProbeStepType::UIDetailsSearchFilterActive;
            CompleteStep(bActive == bExpectActive ? EResultStatus::Passed : EResultStatus::Failed,
                FString::Printf(TEXT("Details 1 HasActiveSearch=%s; expected %s."),
                    bActive ? TEXT("true") : TEXT("false"), bExpectActive ? TEXT("true") : TEXT("false")));
        }

        // Read-only inspection uses Automation Driver's standard ID discovery and
        // text/visibility APIs, but NEVER injects a click or key press. All Driver
        // API calls are off the Editor thread, just like interactive input.
        void BeginObservation(const FProbeTestStep& Step)
        {
            check(IsInGameThread());
            const FString Selector = Step.Target.TrimStartAndEnd();
            const bool bNative = Selector.Equals(TEXT("details:search"), ESearchCase::IgnoreCase);
            const bool bTagged = Selector.StartsWith(TEXT("id:"), ESearchCase::IgnoreCase)
                && Selector.Len() > 3 && !Selector.RightChop(3).TrimStartAndEnd().IsEmpty();
            if (!bNative && !bTagged)
            {
                CompleteStep(EResultStatus::Error,
                    TEXT("Invalid selector. Use details:search or id:<Slate automation ID>. Coordinates and global label queries are not supported."));
                return;
            }
            const bool bWait = Step.Type == EProbeStepType::UIWaitForWidgetVisible
                || Step.Type == EProbeStepType::UIWaitForWidgetTextEquals;
            if (bWait && (!FMath::IsFinite(Step.UIWaitTimeoutSeconds)
                || Step.UIWaitTimeoutSeconds < 0.1 || Step.UIWaitTimeoutSeconds > 30.0))
            {
                CompleteStep(EResultStatus::Error, TEXT("UI Wait Timeout must be between 0.1 and 30 seconds."));
                return;
            }
            TWeakPtr<SSearchBox> WeakSearch;
            if (bNative)
            {
                TSharedPtr<IDetailsView> View;
                TSharedPtr<SSearchBox> Search;
                FString Reason;
                if (!ResolveDetailsSearch(View, Search, false, Reason))
                {
                    if (Step.Type == EProbeStepType::UIWidgetNotExists)
                    {
                        CompleteStep(EResultStatus::Passed, TEXT("Native Details 1 search is absent."));
                    }
                    else if (Step.Type == EProbeStepType::UIWidgetExists)
                    {
                        CompleteStep(EResultStatus::Failed, Reason);
                    }
                    else { CompleteStep(EResultStatus::Error, Reason); }
                    return;
                }
                WeakSearch = Search;
            }
            if (!bDriverEnabledByProbe && !IAutomationDriverModule::Get().IsEnabled())
            {
                IAutomationDriverModule::Get().Enable();
                bDriverEnabledByProbe = true;
            }
            const FString Id = bTagged ? Selector.RightChop(3) : FString();
            const FString Expected = Step.ExpectedValue;
            const EProbeStepType Type = Step.Type;
            const double Timeout = Step.UIWaitTimeoutSeconds;
            TSharedRef<FAsyncProbeTestRun> Self = AsShared();
            Async(EAsyncExecution::ThreadPool, [Self, WeakSearch, bNative, Id, Selector, Expected, Type, bWait, Timeout]()
            {
                const FAutomationDriverRef Driver = IAutomationDriverModule::Get().CreateDriver();
                // An observation is a snapshot unless explicitly authored as a wait.
                // Avoid the implicit 3-second delay for negative assertions.
                Driver->GetConfiguration()->ImplicitWait = FTimespan::Zero();
                const FDriverElementRef Element = bNative
                    ? Driver->FindElement(By::WidgetLambda(
                        [WeakSearch](TArray<TSharedRef<SWidget>>& Out)
                        {
                            if (TSharedPtr<SSearchBox> Search = WeakSearch.Pin())
                            { Out.Add(StaticCastSharedRef<SWidget>(Search.ToSharedRef())); }
                        }, TEXT("Probe.Details1.Search.Inspect")))
                    : Driver->FindElement(By::Id(*Id));
                EResultStatus Status = EResultStatus::Failed;
                FString Message;
                bool bPresent = false;
                bool bVisible = false;
                bool bInteractable = false;
                FString Actual;
                const double Started = FPlatformTime::Seconds();
                const double Deadline = Started + (bWait ? Timeout : 0.0);
                do
                {
                    Actual.Empty();
                    bPresent = Element->Exists();
                    bVisible = bPresent && Element->IsVisible();
                    bInteractable = bVisible && Element->IsInteractable();
                    if (bPresent && (Type == EProbeStepType::UIWidgetTextEquals
                        || Type == EProbeStepType::UIWidgetTextContains
                        || Type == EProbeStepType::UIWidgetTextNotEquals
                        || Type == EProbeStepType::UIWaitForWidgetTextEquals))
                    { Actual = Element->GetText().ToString(); }
                    bool bMatches = false;
                    switch (Type)
                    {
                        case EProbeStepType::UIWidgetExists: bMatches = bPresent; break;
                        case EProbeStepType::UIWidgetNotExists: bMatches = !bPresent; break;
                        case EProbeStepType::UIWidgetVisible:
                        case EProbeStepType::UIWaitForWidgetVisible: bMatches = bVisible; break;
                        case EProbeStepType::UIWidgetInteractable: bMatches = bInteractable; break;
                        case EProbeStepType::UIWidgetTextEquals:
                        case EProbeStepType::UIWaitForWidgetTextEquals: bMatches = bPresent && Actual == Expected; break;
                        case EProbeStepType::UIWidgetTextContains: bMatches = bPresent && Actual.Contains(Expected, ESearchCase::CaseSensitive); break;
                        case EProbeStepType::UIWidgetTextNotEquals: bMatches = bPresent && Actual != Expected; break;
                        default: break;
                    }
                    if (bMatches)
                    {
                        Status = EResultStatus::Passed;
                        break;
                    }
                    if (!bWait || FPlatformTime::Seconds() >= Deadline) { break; }
                    FPlatformProcess::Sleep(0.1f);
                }
                while (true);
                const bool bText = Type == EProbeStepType::UIWidgetTextEquals
                    || Type == EProbeStepType::UIWidgetTextContains
                    || Type == EProbeStepType::UIWidgetTextNotEquals
                    || Type == EProbeStepType::UIWaitForWidgetTextEquals;
                Message = bText
                    ? FString::Printf(TEXT("Widget '%s': exists=%s, actual text='%s', expected='%s'%s"),
                        *Selector, bPresent ? TEXT("true") : TEXT("false"), *Actual, *Expected,
                        bWait && Status != EResultStatus::Passed ? TEXT(" (wait timed out)") : TEXT(""))
                    : FString::Printf(TEXT("Widget '%s': exists=%s, visible=%s, interactable=%s%s"),
                        *Selector, bPresent ? TEXT("true") : TEXT("false"),
                        bVisible ? TEXT("true") : TEXT("false"), bInteractable ? TEXT("true") : TEXT("false"),
                        bWait && Status != EResultStatus::Passed ? TEXT(" (wait timed out)") : TEXT(""));
                AsyncTask(ENamedThreads::GameThread, [Self, Status, Message]()
                {
                    Self->CompleteStep(Status, Message);
                });
            });
        }

        void BeginInput(const FProbeTestStep& Step)
        {
            check(IsInGameThread());
            const FString Selector = Step.Target.TrimStartAndEnd();
            const bool bNative = Selector.Equals(TEXT("details:search"), ESearchCase::IgnoreCase);
            const bool bTagged = Selector.StartsWith(TEXT("id:"), ESearchCase::IgnoreCase)
                && Selector.Len() > 3 && !Selector.RightChop(3).TrimStartAndEnd().IsEmpty();
            if (!bNative && !bTagged)
            {
                CompleteStep(EResultStatus::Error,
                    TEXT("Invalid selector. Use details:search or id:<Slate automation ID>. Coordinates and unscoped text matching are not supported."));
                return;
            }
            if (!bSavedFocus)
            {
                PreviousKeyboardFocus = FSlateApplication::Get().GetKeyboardFocusedWidget();
                bSavedFocus = true;
            }
            TWeakPtr<SSearchBox> WeakSearch;
            if (bNative)
            {
                TSharedPtr<IDetailsView> View;
                TSharedPtr<SSearchBox> Search;
                FString Reason;
                if (!ResolveDetailsSearch(View, Search, true, Reason))
                {
                    CompleteStep(EResultStatus::Error, Reason); return;
                }
                WeakSearch = Search;
                if (Step.Type == EProbeStepType::UIWidgetTypeText && !bSavedSearch)
                {
                    EditedSearch = Search;
                    EditedView = View;
                    OriginalSearchText = Search->GetText();
                    bOriginalSearchActive = View->HasActiveSearch();
                    bSavedSearch = true;
                }
            }
            if (!bDriverEnabledByProbe && !IAutomationDriverModule::Get().IsEnabled())
            {
                IAutomationDriverModule::Get().Enable();
                bDriverEnabledByProbe = true;
            }
            const FString DriverId = bTagged ? Selector.RightChop(3) : FString();
            const FString Input = Step.ExpectedValue;
            const bool bType = Step.Type == EProbeStepType::UIWidgetTypeText;
            TSharedRef<FAsyncProbeTestRun> Self = AsShared();
            // NEVER run Click/TypeChord/Type on the Editor thread. The driver
            // waits for that same thread to pump actual Slate messages.
            Async(EAsyncExecution::ThreadPool, [Self, WeakSearch, bNative, DriverId, Input, bType]()
            {
                bool bSucceeded = false;
                FString Failure = TEXT("Automation Driver did not complete input.");
                const FAutomationDriverRef Driver = IAutomationDriverModule::Get().CreateDriver();
                if (bNative)
                {
                    const FDriverElementRef Element = Driver->FindElement(By::WidgetLambda(
                        [WeakSearch](TArray<TSharedRef<SWidget>>& Out)
                        {
                            if (TSharedPtr<SSearchBox> Widget = WeakSearch.Pin())
                            { Out.Add(StaticCastSharedRef<SWidget>(Widget.ToSharedRef())); }
                        }, TEXT("Probe.Details1.Search")));
                    bSucceeded = Element->Click();
                    if (bSucceeded && bType)
                    {
                        bSucceeded = Element->TypeChord(EKeys::LeftControl, EKeys::A)
                            && Element->Type(Input);
                        if (bSucceeded) { Driver->Wait(FTimespan::FromMilliseconds(1600)); }
                    }
                }
                else
                {
                    const FDriverElementRef Element = Driver->FindElement(By::Id(*DriverId));
                    bSucceeded = Element->Click();
                    if (bSucceeded && bType)
                    {
                        bSucceeded = Element->TypeChord(EKeys::LeftControl, EKeys::A)
                            && Element->Type(Input);
                    }
                }
                const FString SelectorName = bNative ? TEXT("details:search") : FString::Printf(TEXT("id:%s"), *DriverId);
                Failure = bSucceeded
                    ? FString::Printf(TEXT("Automation Driver %s succeeded on '%s'."), bType ? TEXT("typing") : TEXT("click"), *SelectorName)
                    : FString::Printf(TEXT("Automation Driver %s failed on selector '%s'. Check visibility, focus, and enabled state."),
                        bType ? TEXT("typing") : TEXT("click"), *SelectorName);
                AsyncTask(ENamedThreads::GameThread, [Self, bSucceeded, Failure]()
                {
                    Self->CompleteStep(bSucceeded ? EResultStatus::Passed : EResultStatus::Failed, Failure);
                });
            });
        }

        void CompleteStep(EResultStatus Status, const FString& Detail)
        {
            check(IsInGameThread());
            FStepResult StepResult;
            StepResult.Index = Index + 1;
            const FProbeTestStep& Definition = Definitions[Index];
            StepResult.AssertionId = !Definition.AssertionId.IsNone() ? Definition.AssertionId.ToString()
                : FString::Printf(TEXT("Probe.BuiltIn.%s"),
                    *StaticEnum<EProbeStepType>()->GetNameStringByValue(static_cast<int64>(Definition.Type)));
            StepResult.Description = Definition.Description;
            if (StepResult.Description.IsEmpty())
            {
                if (!Definition.AssertionId.IsNone())
                {
                    if (const FExtensionAssertion* Extension = FindExtensionAssertion(Definition.AssertionId))
                    { StepResult.Description = Extension->Name; }
                    else { StepResult.Description = Definition.AssertionId.ToString(); }
                }
                else { StepResult.Description = GetAssertionInfo(Definition.Type).Name; }
            }
            StepResult.Status = Status;
            StepResult.Detail = Detail;
            StepResult.DurationMs = (FPlatformTime::Seconds() - StepStartedAt) * 1000.0;
            Result.Steps.Add(MoveTemp(StepResult));
            if (Status != EResultStatus::Passed)
            {
                Result.Status = Status;
                Result.Detail = FString::Printf(TEXT("Step %d: %s"), Index + 1, *Detail);
                Finish(); return;
            }
            ++Index;
            NextStep();
        }

        void Finish()
        {
            check(IsInGameThread());
            // Restore only the native scoped Details search: we cannot safely
            // reverse an arbitrary button action or edit to another widget.
            if (bSavedSearch)
            {
                ProbeDetailsSearchRestore::Restore(
                    EditedView.Pin(), EditedSearch.Pin(), OriginalSearchText);
                // Slate Details search processes a delayed callback. Do not
                // claim cleanup was verified until it has had time to settle.
                TSharedRef<FAsyncProbeTestRun> Self = AsShared();
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
                    [Self](float)
                    {
                        Self->FinishRestoration();
                        return false;
                    }), 1.6f);
                return;
            }
            FinishRestoration();
        }

        void FinishRestoration()
        {
            check(IsInGameThread());
            if (bSavedSearch)
            {
                TSharedPtr<SSearchBox> Search = EditedSearch.Pin();
                TSharedPtr<IDetailsView> View = EditedView.Pin();
                const FString ActualText = Search.IsValid() ? Search->GetText().ToString() : TEXT("<widget unavailable>");
                const bool bActualActive = View.IsValid() && View->HasActiveSearch();
                if (!Search.IsValid() || !View.IsValid()
                    || ActualText != OriginalSearchText.ToString()
                    || bActualActive != bOriginalSearchActive)
                {
                    Result.Status = EResultStatus::Error;
                    Result.Detail += FString::Printf(
                        TEXT(" Native Details search restoration failed after 1600 ms: original text='%s', current text='%s', original active=%s, current active=%s, view valid=%s, widget valid=%s."),
                        *OriginalSearchText.ToString(), *ActualText,
                        bOriginalSearchActive ? TEXT("yes") : TEXT("no"),
                        bActualActive ? TEXT("yes") : TEXT("no"),
                        View.IsValid() ? TEXT("yes") : TEXT("no"),
                        Search.IsValid() ? TEXT("yes") : TEXT("no"));
                }
            }
            if (bSavedFocus && FSlateApplication::IsInitialized())
            {
                if (TSharedPtr<SWidget> Focus = PreviousKeyboardFocus.Pin())
                {
                    FWidgetPath FocusPath;
                    if (FSlateApplication::Get().FindPathToWidget(Focus.ToSharedRef(), FocusPath, EVisibility::Visible))
                    { FSlateApplication::Get().SetKeyboardFocus(Focus.ToSharedRef(), EFocusCause::SetDirectly); }
                }
            }
            if (bDriverEnabledByProbe && IAutomationDriverModule::Get().IsEnabled())
            { IAutomationDriverModule::Get().Disable(); }
            Result.DurationMs = (FPlatformTime::Seconds() - StartedAt) * 1000.0;
            // Editor fixture context stays alive until the completion callback
            // returns; its RAII cleanup runs before the shared state is freed.
            if (OnComplete) { OnComplete(MoveTemp(Result)); }
        }
    };

    void RunTestAsync(const UProbeTestAsset* Asset, TFunction<void(FTestResult)> Callback)
    {
        check(IsInGameThread());
        if (!Asset || !Asset->Steps.ContainsByPredicate(
            [](const FProbeTestStep& Step) { return Step.AssertionId.IsNone() && IsNativeUIStep(Step.Type); }))
        {
            Callback(RunTest(Asset)); // Existing non-UI runner remains unmodified.
            return;
        }
        TSharedRef<FAsyncProbeTestRun> Run = MakeShared<FAsyncProbeTestRun>();
        Run->Begin(Asset, MoveTemp(Callback));
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
        else if (Asset->bNeedsConfiguration)
        {
            Result.Status = EResultStatus::Skipped;
            Result.Detail = TEXT("Template needs configuration. Review the steps in Probe, then uncheck Needs Configuration to enable automatic runs.");
        }
        else if (Asset->Steps.IsEmpty())
        {
            Result.Status = EResultStatus::Error;
            Result.Detail = TEXT("Test has no steps. Use + Add Step to configure the test.");
        }
        else
        {
            FProbeRunContext Context; // RAII cleanup even if any step fails or errors.
            Context.bAllowTransientEditorActors = Asset->bAllowTransientEditorActors;
            Result.Status = EResultStatus::Passed;
            for (int32 Index = 0; Index < Asset->Steps.Num(); ++Index)
            {
                FStepResult StepResult;
                StepResult.Index = Index + 1;
                const FProbeTestStep& Definition = Asset->Steps[Index];
                StepResult.AssertionId = !Definition.AssertionId.IsNone()
                    ? Definition.AssertionId.ToString()
                    : FString::Printf(TEXT("Probe.BuiltIn.%s"),
                        *StaticEnum<EProbeStepType>()->GetNameStringByValue(static_cast<int64>(Definition.Type)));
                StepResult.Description = Definition.Description;
                if (StepResult.Description.IsEmpty())
                {
                    if (!Definition.AssertionId.IsNone())
                    {
                        if (const FExtensionAssertion* Provider = FindExtensionAssertion(Definition.AssertionId))
                        { StepResult.Description = Provider->Name; }
                        else { StepResult.Description = Definition.AssertionId.ToString(); }
                    }
                    else { StepResult.Description = GetAssertionInfo(Definition.Type).Name; }
                }
                const double StepStart = FPlatformTime::Seconds();
                StepResult.Status = ExecuteStep(Asset->Steps[Index], Context, StepResult.Detail);
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
    TArray<FTestResult> RunPack(const UProbeTestPack* Pack)
    {
        TArray<FTestResult> Results;
        if (!Pack) { return Results; }
        for (const TSoftObjectPtr<UProbeTestAsset>& Reference : Pack->Tests)
        {
            const FReferenceHealth Check = InspectTestReference(Reference.ToSoftObjectPath());
            if (!Check.IsUsable())
            {
                FTestResult Missing;
                Missing.Name = Reference.ToString();
                Missing.AssetPath = Reference.ToString();
                Missing.Status = EResultStatus::Error;
                Missing.Detail = Check.Explanation;
                Results.Add(MoveTemp(Missing));
                continue; // Never invoke the package loader for missing or redirector references.
            }
            UProbeTestAsset* Test = Reference.LoadSynchronous();
            if (Test)
            {
                Results.Add(RunTest(Test));
            }
            else
            {
                FTestResult Missing;
                Missing.Name = Reference.ToString();
                Missing.AssetPath = Reference.ToString();
                Missing.Status = EResultStatus::Error;
                Missing.Detail = TEXT("Probe Pack reference could not be loaded. The test may have moved or been deleted.");
                Results.Add(MoveTemp(Missing));
            }
        }
        return Results;
    }

}
