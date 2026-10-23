#include "ProbeTestAsset.h"
#include "ProbeTestRunner.h"
#include "ProbeReferenceValidation.h"
#include "ProbeAssertionCatalog.h"
#include "ProbeExtensions.h"
#include "ProbeTestAssetFactory.h"
#include "ProbeTestPack.h"
#include "ProbeTestPackFactory.h"
#include "ProbeEditorNavigation.h"
#include "AssetDefinition.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "PluginDescriptor.h"
#include "ModuleDescriptor.h"
#include "ToolMenus.h"
#include "ToolMenu.h"
#include "ContentBrowserMenuContexts.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Misc/ConfigCacheIni.h"
#include "LevelEditor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "FileHelpers.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Images/SImage.h"
#include "Styling/AppStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SWindow.h"
#include "DragAndDrop/AssetDragDropOp.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/PackageName.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "ScopedTransaction.h"
#include "Misc/TransactionObjectEvent.h"
#include "UObject/UObjectGlobals.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Brushes/SlateImageBrush.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformApplicationMisc.h"

#define LOCTEXT_NAMESPACE "ProbeEditor"

namespace Probe
{
    // Last-run status is session-only. It does not alter Test/Pack assets.
    static TMap<FString, EResultStatus> RecentTestStatuses;
    static void UpdateRecentTestStatuses(const TArray<FTestResult>& Results)
    {
        for (const FTestResult& Result : Results)
        { if (!Result.AssetPath.IsEmpty()) { RecentTestStatuses.Add(Result.AssetPath, Result.Status); } }
    }
    static FString RecentResultName(const FString& AssetPath)
    {
        if (const EResultStatus* Status = RecentTestStatuses.Find(AssetPath)) { return StatusName(*Status); }
        return TEXT("Not Run");
    }
    static TArray<FString> AllCategories()
    {
        TArray<FString> Categories = AssertionCategories();
        for (const FName Id : GetExtensionAssertionIds())
        {
            if (const FExtensionAssertion* Info = FindExtensionAssertion(Id)) { Categories.AddUnique(Info->Category); }
        }
        for (const FName Id : GetExtensionTemplateIds())
        {
            if (const FExtensionTemplate* Info = FindExtensionTemplate(Id)) { Categories.AddUnique(Info->Category); }
        }
        return Categories;
    }
    static uint8 StepFields(const FProbeTestStep& Step)
    {
        if (!Step.AssertionId.IsNone())
        {
            if (const FExtensionAssertion* Info = FindExtensionAssertion(Step.AssertionId)) { return Info->Fields; }
            return TargetField; // preserve the input for a temporarily missing provider
        }
        return GetAssertionInfo(Step.Type).Fields;
    }
    static FString StepDisplayName(const FProbeTestStep& Step)
    {
        if (!Step.AssertionId.IsNone())
        {
            if (const FExtensionAssertion* Info = FindExtensionAssertion(Step.AssertionId)) { return Info->Name; }
            return FString::Printf(TEXT("Missing provider: %s"), *Step.AssertionId.ToString());
        }
        return GetAssertionInfo(Step.Type).Name;
    }
    static FString StepHelp(const FProbeTestStep& Step)
    {
        if (!Step.AssertionId.IsNone())
        {
            if (const FExtensionAssertion* Info = FindExtensionAssertion(Step.AssertionId)) { return Info->Description; }
            return FString::Printf(TEXT("Install the provider for %s"), *Step.AssertionId.ToString());
        }
        return GetAssertionInfo(Step.Type).Description;
    }

    // Per-assertion preflight is intentionally about authoring, not environment state:
    // a test may legitimately fail because an actor is absent, but should not be
    // accidentally run with an omitted class/path/comparison field.
    static FString StepConfigurationIssue(const FProbeTestStep& Step)
    {
        if (!Step.AssertionId.IsNone() && !FindExtensionAssertion(Step.AssertionId))
        { return FString::Printf(TEXT("Required assertion provider '%s' is unavailable"), *Step.AssertionId.ToString()); }
        const uint8 Fields = StepFields(Step);
        if ((Fields & TargetField) && Step.Target.TrimStartAndEnd().IsEmpty()) { return TEXT("Choose a Target"); }
        if ((Fields & ClassField) && Step.ExpectedClass.TrimStartAndEnd().IsEmpty()) { return TEXT("Choose the expected class"); }
        if ((Fields & PropertyField) && Step.PropertyName.TrimStartAndEnd().IsEmpty()) { return TEXT("Enter a Property, Function, or Tag name"); }
        if ((Fields & ValueField) && Step.ExpectedValue.TrimStartAndEnd().IsEmpty()) { return TEXT("Enter an expected value or search text"); }
        if (Step.Type == EProbeStepType::ObjectPropertyNumericInRange && Step.AssertionId.IsNone())
        {
            FString Lower, Upper;
            double Min = 0.0, Max = 0.0;
            if (!Step.ExpectedValue.Split(TEXT(","), &Lower, &Upper)
                || !LexTryParseString(Min, *Lower.TrimStartAndEnd())
                || !LexTryParseString(Max, *Upper.TrimStartAndEnd()) || Min > Max)
            { return TEXT("Expected range must be min,max with min <= max"); }
        }
        return FString();
    }
    static FString TestConfigurationIssue(const UProbeTestAsset& Asset)
    {
        if (Asset.Steps.IsEmpty()) { return TEXT("Add at least one assertion step"); }
        for (int32 Index = 0; Index < Asset.Steps.Num(); ++Index)
        {
            const FString Issue = StepConfigurationIssue(Asset.Steps[Index]);
            if (!Issue.IsEmpty()) { return FString::Printf(TEXT("Step %d: %s"), Index + 1, *Issue); }
        }
        return FString();
    }

    // Keep the original tab ID so an existing saved Editor layout can reopen it.
    static const FName TabId(TEXT("ProbeWorkspace"));
    static const FName PackTabId(TEXT("ProbePackWorkspace"));

    enum class ETestTemplate : uint8
    {
        Blank,
        Smoke,
        Plugin,
        World,
        Asset,
        ActorClass,
        ActorTag,
        AssetClass,
        Property,
        ActorLabel,
        ActorSelection,
        Module,
        EditorTab,
        ToolMenu,
        ConsoleVariable,
        AssetFolder,
        AssetDirty,
        ClassContract,
        ObjectPropertyAbsent,
        ActorComponent,
        ActorFolder,
        SelectionExact,
        PluginDescriptor,
        PluginModule,
        ModuleDiscovery,
        TabRegistration,
        EditorNotPlaying,
        ConsoleVariableNegative,
        AssetAbsence,
        AssetTag,
        ClassFunctionContract,
        ClassPropertyAbsence,
        ObjectGreaterThan,
        ActorSelectionState,
        ActorMaxCount,
        SelectionMaxCount,
        SelectedComponent,
        AssetExactCount
    };

    static FProbeTestStep MakeStep(EProbeStepType Type, const FString& OptionalAssetPath = FString())
    {
        FProbeTestStep Step;
        Step.Type = Type;
        switch (Type)
        {
        case EProbeStepType::PluginEnabled:
            Step.Target = TEXT("Probe");
            Step.Description = TEXT("Verify the plugin is enabled");
            break;
        case EProbeStepType::ClassExists:
            Step.Target = TEXT("/Script/Engine.Actor");
            Step.Description = TEXT("Verify the class can be found");
            break;
        case EProbeStepType::AssetExists:
            Step.Target = OptionalAssetPath;
            Step.Description = TEXT("Verify the asset exists");
            break;
        case EProbeStepType::EditorWorldExists:
            Step.Description = TEXT("Verify an Editor world is loaded");
            break;
        case EProbeStepType::ActorCountAtLeast:
            Step.MinimumActorCount = 1;
            Step.Description = TEXT("Verify the level has at least one actor");
            break;
        case EProbeStepType::ActorClassCountAtLeast:
            Step.Target = TEXT("/Script/Engine.Actor");
            Step.MinimumActorCount = 1;
            Step.Description = TEXT("Verify at least one actor of the specified class");
            break;
        case EProbeStepType::ActorTagExists:
            Step.Description = TEXT("Verify an actor has the specified tag (set Target)");
            break;
        case EProbeStepType::AssetClassIs:
            Step.Target = OptionalAssetPath;
            Step.ExpectedClass = TEXT("/Script/ProbeEditor.ProbeTestAsset");
            Step.Description = TEXT("Verify the asset has the expected class");
            break;
        case EProbeStepType::ActorLabelExists:
            Step.Description = TEXT("Verify an actor exists with this exact World Outliner label");
            break;
        case EProbeStepType::SelectedActorCountAtLeast:
            Step.MinimumActorCount = 1;
            Step.Description = TEXT("Verify at least one actor is currently selected in the Editor");
            break;
        case EProbeStepType::ObjectPropertyEquals:
            Step.Target = OptionalAssetPath;
            Step.PropertyName = TEXT("bEnabled");
            Step.ExpectedValue = TEXT("True");
            Step.Description = TEXT("Verify a reflected property matches the expected value");
            break;
        case EProbeStepType::PluginInstalled:
        case EProbeStepType::PluginDisabled:
            Step.Target = TEXT("Probe");
            break;
        case EProbeStepType::ModuleLoaded:
            Step.Target = TEXT("ProbeEditor");
            break;
        case EProbeStepType::ConsoleVariableExists:
        case EProbeStepType::ConsoleVariableEquals:
            Step.Target = TEXT("r.VSync");
            Step.ExpectedValue = TEXT("0");
            break;
        case EProbeStepType::EditorTabOpen:
            Step.Target = TEXT("ProbeWorkspace");
            break;
        case EProbeStepType::ToolMenuRegistered:
            Step.Target = TEXT("LevelEditor.MainMenu.Tools");
            break;
        case EProbeStepType::AssetCountAtLeast:
            Step.Target = TEXT("/Game/ProbeTests");
            Step.MinimumActorCount = 1;
            break;
        case EProbeStepType::AssetIsDirty:
        case EProbeStepType::AssetIsClean:
        case EProbeStepType::AssetIsLoaded:
        case EProbeStepType::ObjectLoaded:
            Step.Target = OptionalAssetPath;
            break;
        case EProbeStepType::AssetRegistryTagEquals:
            Step.Target = OptionalAssetPath;
            break;
        case EProbeStepType::ClassIsChildOf:
            Step.Target = TEXT("/Script/Engine.Actor");
            Step.ExpectedClass = TEXT("/Script/CoreUObject.Object");
            break;
        case EProbeStepType::ClassImplementsInterface:
            Step.Target = TEXT("/Script/Engine.Actor");
            break;
        case EProbeStepType::ClassHasProperty:
            Step.Target = TEXT("/Script/Engine.Actor");
            Step.PropertyName = TEXT("Tags");
            break;
        case EProbeStepType::ClassHasFunction:
            Step.Target = TEXT("/Script/Engine.Actor");
            Step.PropertyName = TEXT("GetActorLocation");
            break;
        case EProbeStepType::ObjectPropertyExists:
            Step.Target = OptionalAssetPath;
            Step.PropertyName = TEXT("bEnabled");
            break;
        case EProbeStepType::ObjectPropertyNotEquals:
            Step.Target = OptionalAssetPath;
            Step.PropertyName = TEXT("bEnabled");
            Step.ExpectedValue = TEXT("False");
            break;
        case EProbeStepType::ActorLabelNotExists:
        case EProbeStepType::ActorTagNotExists:
        case EProbeStepType::ActorHasComponent:
        case EProbeStepType::ActorFolderEquals:
        case EProbeStepType::ActorPropertyEquals:
            if (Type == EProbeStepType::ActorHasComponent)
                Step.ExpectedClass = TEXT("/Script/Engine.SceneComponent");
            if (Type == EProbeStepType::ActorPropertyEquals)
                Step.PropertyName = TEXT("bHidden");
            break;
        case EProbeStepType::ActorCountExactly:
        case EProbeStepType::ActorClassCountExactly:
        case EProbeStepType::SelectedActorCountExactly:
            Step.MinimumActorCount = 1;
            if (Type == EProbeStepType::ActorClassCountExactly)
                Step.Target = TEXT("/Script/Engine.Actor");
            break;
        case EProbeStepType::SelectedActorIsClass:
            Step.Target = TEXT("/Script/Engine.Actor");
            break;
        case EProbeStepType::SelectedActorLabelEquals:
        case EProbeStepType::SelectedActorHasTag:
            break;
        case EProbeStepType::PluginFriendlyNameEquals:
            Step.Target = TEXT("Probe");
            Step.ExpectedValue = TEXT("Probe");
            break;
        case EProbeStepType::PluginVersionEquals:
            Step.Target = TEXT("Probe");
            Step.ExpectedValue = TEXT("{InstalledVersion}");
            break;
        case EProbeStepType::PluginDeclaresModule:
            Step.Target = TEXT("Probe");
            Step.ExpectedValue = TEXT("ProbeEditor");
            break;
        case EProbeStepType::PluginHasContent:
            Step.Target = TEXT("Probe");
            break;
        case EProbeStepType::ModuleNotLoaded:
        case EProbeStepType::ModuleExists:
            Step.Target = TEXT("ProbeEditor");
            break;
        case EProbeStepType::EditorTabRegistered:
        case EProbeStepType::EditorTabNotOpen:
            Step.Target = TEXT("ProbeWorkspace");
            break;
        case EProbeStepType::ToolMenuNotRegistered:
            Step.Target = TEXT("Probe.MissingMenuForNegativeTest");
            break;
        case EProbeStepType::ConsoleVariableNotExists:
            Step.Target = TEXT("Probe.MissingCVarForNegativeTest");
            break;
        case EProbeStepType::ConsoleVariableNotEquals:
            Step.Target = TEXT("r.VSync");
            Step.ExpectedValue = TEXT("__Probe_ImpossibleString__");
            break;
        case EProbeStepType::PIEIsRunning:
        case EProbeStepType::PIEIsNotRunning:
            break;
        case EProbeStepType::AssetNotExists:
            Step.Target = TEXT("/Game/ProbeTests/MissingProbeAsset.MissingProbeAsset");
            break;
        case EProbeStepType::AssetCountExactly:
            Step.Target = TEXT("/Game/ProbeTests");
            Step.MinimumActorCount = 1;
            break;
        case EProbeStepType::AssetRegistryTagExists:
            Step.Target = OptionalAssetPath;
            Step.PropertyName = TEXT("SomeTag");
            break;
        case EProbeStepType::ClassNotExists:
            Step.Target = TEXT("/Script/ProbeEditor.DoesNotExist");
            break;
        case EProbeStepType::ClassHasNoProperty:
            Step.Target = TEXT("/Script/Engine.Actor");
            Step.PropertyName = TEXT("ProbeNonexistentProperty");
            break;
        case EProbeStepType::ClassHasNoFunction:
            Step.Target = TEXT("/Script/Engine.Actor");
            Step.PropertyName = TEXT("ProbeNonexistentFunction");
            break;
        case EProbeStepType::ClassIsAbstract:
            Step.Target = TEXT("/Script/Engine.ActorComponent");
            break;
        case EProbeStepType::ClassFunctionBlueprintCallable:
            Step.Target = TEXT("/Script/Engine.Actor");
            Step.PropertyName = TEXT("GetActorLocation");
            break;
        case EProbeStepType::ObjectNotLoaded:
            Step.Target = TEXT("/Game/ProbeTests/MissingProbeAsset.MissingProbeAsset");
            break;
        case EProbeStepType::ObjectPropertyGreaterThan:
        case EProbeStepType::ObjectPropertyLessThan:
            Step.Target = TEXT("/Script/Engine.Default__Actor");
            Step.PropertyName = TEXT("InitialLifeSpan");
            Step.ExpectedValue = Type == EProbeStepType::ObjectPropertyGreaterThan ? TEXT("-1") : TEXT("1");
            break;
        case EProbeStepType::ActorCountAtMost:
        case EProbeStepType::ActorClassCountAtMost:
        case EProbeStepType::ActorLabelCountExactly:
        case EProbeStepType::SelectedActorCountAtMost:
            Step.MinimumActorCount = 1;
            if (Type == EProbeStepType::ActorClassCountAtMost) { Step.Target = TEXT("/Script/Engine.Actor"); }
            break;
        case EProbeStepType::ActorIsSelected:
        case EProbeStepType::ActorIsNotSelected:
            Step.Description = TEXT("Use Selected Actor to capture an Outliner label");
            break;
        case EProbeStepType::SelectedActorHasComponent:
            Step.ExpectedClass = TEXT("/Script/Engine.ActorComponent");
            break;
        case EProbeStepType::PluginIsEnginePlugin:
        case EProbeStepType::PluginIsProjectPlugin:
        case EProbeStepType::PluginModuleCountAtLeast:
        case EProbeStepType::PluginCategoryEquals:
            Step.Target = TEXT("Probe");
            Step.MinimumActorCount = 1;
            if (Type == EProbeStepType::PluginCategoryEquals) { Step.ExpectedValue = TEXT("Testing"); }
            break;
        case EProbeStepType::LoadedModuleCountAtLeast:
            Step.MinimumActorCount = 1;
            break;
        case EProbeStepType::ToolMenuSectionExists:
        case EProbeStepType::ToolMenuSectionNotExists:
            Step.Target = TEXT("LevelEditor.MainMenu.Tools");
            Step.ExpectedValue = Type == EProbeStepType::ToolMenuSectionExists
                ? TEXT("ProbeTools") : TEXT("MissingProbeSection");
            break;
        case EProbeStepType::SelectedActorsEmpty:
        case EProbeStepType::SelectedActorsNotEmpty:
            break;
        case EProbeStepType::AssetIsRedirector:
        case EProbeStepType::AssetIsNotRedirector:
        case EProbeStepType::AssetRegistryTagNotExists:
            Step.Target = OptionalAssetPath;
            Step.PropertyName = TEXT("SomeTag");
            break;
        case EProbeStepType::ClassIsInterface:
        case EProbeStepType::ClassIsNotAbstract:
        case EProbeStepType::ClassPropertyBlueprintVisible:
        case EProbeStepType::ClassPropertyEditable:
        case EProbeStepType::ClassFunctionIsPure:
        case EProbeStepType::ClassFunctionIsStatic:
        case EProbeStepType::ClassFunctionHasParameter:
            Step.Target = Type == EProbeStepType::ClassIsInterface
                ? FString() : TEXT("/Script/Engine.Actor");
            if (Type == EProbeStepType::ClassPropertyBlueprintVisible || Type == EProbeStepType::ClassPropertyEditable)
            { Step.PropertyName = TEXT("Tags"); }
            else { Step.PropertyName = TEXT("GetActorLocation"); }
            if (Type == EProbeStepType::ClassFunctionHasParameter) { Step.ExpectedValue = TEXT("ReturnValue"); }
            break;
        case EProbeStepType::ActorLabelContains:
            Step.Target = TEXT("Actor");
            break;
        case EProbeStepType::ActorCountWithTagAtLeast:
            Step.Target = TEXT("ProbeTag");
            Step.MinimumActorCount = 1;
            break;
        case EProbeStepType::ActorHasTagByLabel:
        case EProbeStepType::ActorDoesNotHaveTagByLabel:
            Step.ExpectedValue = TEXT("ProbeTag");
            break;
        case EProbeStepType::SelectedActorIsNotClass:
            Step.Target = TEXT("/Script/Engine.Light");
            break;
        case EProbeStepType::SelectedActorHasNoTag:
        case EProbeStepType::SelectedActorLabelContains:
            Step.Target = TEXT("ProbeTag");
            break;
        case EProbeStepType::ObjectPropertyStringContains:
        case EProbeStepType::ObjectPropertyStringStartsWith:
        case EProbeStepType::ObjectPropertyStringEndsWith:
        case EProbeStepType::ObjectPropertyIsTrue:
        case EProbeStepType::ObjectPropertyIsFalse:
            Step.Target = OptionalAssetPath;
            Step.PropertyName = Type == EProbeStepType::ObjectPropertyIsTrue || Type == EProbeStepType::ObjectPropertyIsFalse
                ? TEXT("bEnabled") : TEXT("TestDescription");
            if (Type == EProbeStepType::ObjectPropertyStringContains) { Step.ExpectedValue = TEXT("property"); }
            else if (Type == EProbeStepType::ObjectPropertyStringStartsWith) { Step.ExpectedValue = TEXT("Check"); }
            else if (Type == EProbeStepType::ObjectPropertyStringEndsWith) { Step.ExpectedValue = TEXT("text"); }
            else { Step.ExpectedValue = TEXT("Probe"); }
            break;

        case EProbeStepType::PluginModuleCountExactly:
        case EProbeStepType::PluginModuleCountAtMost:
            Step.Target = TEXT("Probe"); Step.MinimumActorCount = 1; break;
        case EProbeStepType::PluginDescriptionContains:
            Step.Target = TEXT("Probe"); Step.ExpectedValue = TEXT("test"); break;
        case EProbeStepType::PluginAuthorEquals:
            Step.Target = TEXT("Probe"); Step.ExpectedValue = TEXT("Author"); break;
        case EProbeStepType::LoadedModuleCountAtMost:
            Step.MinimumActorCount = 1000; break;
        case EProbeStepType::AssetCountAtMost:
            Step.Target = TEXT("/Game/ProbeTests"); Step.MinimumActorCount = 100; break;
        case EProbeStepType::AssetNameContains:
        case EProbeStepType::AssetNameStartsWith:
        case EProbeStepType::AssetNameEndsWith:
            Step.Target = OptionalAssetPath; Step.ExpectedValue = TEXT("Probe"); break;
        case EProbeStepType::AssetRegistryTagValueContains:
            Step.Target = OptionalAssetPath; Step.PropertyName = TEXT("SomeTag"); Step.ExpectedValue = TEXT("Probe"); break;
        case EProbeStepType::ClassPropertyIsBoolean:
            Step.Target = TEXT("/Script/Engine.Actor"); Step.PropertyName = TEXT("bHidden"); break;
        case EProbeStepType::ClassPropertyIsNumeric:
            Step.Target = TEXT("/Script/Engine.Actor"); Step.PropertyName = TEXT("InitialLifeSpan"); break;
        case EProbeStepType::ClassFunctionHasReturnValue:
            Step.Target = TEXT("/Script/Engine.Actor"); Step.PropertyName = TEXT("GetActorLocation"); break;
        case EProbeStepType::ActorLabelStartsWith:
        case EProbeStepType::ActorLabelEndsWith:
        case EProbeStepType::ActorLabelCountAtLeast:
            Step.Target = TEXT("Actor"); Step.MinimumActorCount = 1; break;
        case EProbeStepType::ActorCountWithTagExactly:
            Step.Target = TEXT("ProbeTag"); Step.MinimumActorCount = 1; break;
        case EProbeStepType::ActorFolderCountAtLeast:
            Step.Target = TEXT("ProbeFolder"); Step.MinimumActorCount = 1; break;
        case EProbeStepType::SelectedActorLabelStartsWith:
        case EProbeStepType::SelectedActorLabelEndsWith:
            Step.Target = TEXT("Actor"); break;
        case EProbeStepType::SelectedActorFolderEquals:
            Step.Target = TEXT("ProbeFolder"); break;
        case EProbeStepType::ObjectPropertyStringNotContains:
        case EProbeStepType::ObjectPropertyStringEqualsIgnoreCase:
            Step.Target = OptionalAssetPath; Step.PropertyName = TEXT("TestDescription"); Step.ExpectedValue = TEXT("Probe"); break;
        case EProbeStepType::ObjectPropertyNumericInRange:
            Step.Target = FString(); Step.PropertyName = TEXT("InitialLifeSpan"); Step.ExpectedValue = TEXT("0,1"); break;
        case EProbeStepType::ObjectPropertyNumericNearlyEquals:
            Step.Target = FString(); Step.PropertyName = TEXT("InitialLifeSpan"); Step.ExpectedValue = TEXT("0.0001"); break;
        case EProbeStepType::FixtureCreateObject:
            Step.Target = TEXT("/Script/ProbeEditor.ProbeTestAsset");
            Step.ExpectedValue = TEXT("Sample");
            Step.Description = TEXT("Create a temporary, isolated fixture for later steps");
            break;
        case EProbeStepType::FixtureSetProperty:
        case EProbeStepType::FixturePropertyEquals:
        case EProbeStepType::FixturePropertyNotEquals:
            Step.Target = TEXT("Sample"); Step.PropertyName = TEXT("bEnabled");
            Step.ExpectedValue = TEXT("True"); break;
        case EProbeStepType::FixtureObjectExists:
        case EProbeStepType::FixtureDestroyObject:
            Step.Target = TEXT("Sample"); break;
        case EProbeStepType::PluginVersionContains:
        case EProbeStepType::PluginFriendlyNameContains:
        case EProbeStepType::PluginHasNoContent:
            Step.Target = TEXT("Probe"); Step.ExpectedValue = TEXT("Probe"); break;
        case EProbeStepType::LoadedModuleCountExactly:
            Step.MinimumActorCount = 1; break;
        case EProbeStepType::AssetCountDirectAtLeast:
        case EProbeStepType::AssetCountDirectExactly:
            Step.Target = TEXT("/Game/ProbeTests"); Step.MinimumActorCount = 1; break;
        case EProbeStepType::AssetPackagePathEquals:
        case EProbeStepType::AssetClassNameEquals:
            Step.Target = OptionalAssetPath;
            Step.ExpectedValue = Type == EProbeStepType::AssetClassNameEquals ? TEXT("ProbeTestAsset") : TEXT("/Game/ProbeTests"); break;
        case EProbeStepType::ClassPropertyIsArray:
        case EProbeStepType::ClassPropertyIsObjectReference:
        case EProbeStepType::ClassPropertyIsStruct:
        case EProbeStepType::ClassPropertyIsEnum:
        case EProbeStepType::ClassPropertyIsName:
        case EProbeStepType::ClassPropertyIsString:
        case EProbeStepType::ClassPropertyIsText:
            Step.Target = TEXT("/Script/Engine.Actor");
            Step.PropertyName = Type == EProbeStepType::ClassPropertyIsArray ? TEXT("Tags") : FString(); break;
        case EProbeStepType::ClassFunctionIsConst:
        case EProbeStepType::ClassFunctionIsPublic:
        case EProbeStepType::ClassFunctionIsDeprecated:
        case EProbeStepType::ClassHasMetadata:
            Step.Target = TEXT("/Script/Engine.Actor");
            Step.PropertyName = Type == EProbeStepType::ClassHasMetadata ? FString() : TEXT("GetActorLocation"); break;
        case EProbeStepType::ActorLabelCountAtMost:
            Step.Target = TEXT("Actor"); Step.MinimumActorCount = 1; break;
        case EProbeStepType::ActorCountWithTagAtMost:
            Step.Target = TEXT("ProbeTag"); Step.MinimumActorCount = 1; break;
        case EProbeStepType::ActorFolderCountExactly:
        case EProbeStepType::ActorFolderCountAtMost:
            Step.Target = TEXT("ProbeFolder"); Step.MinimumActorCount = 1; break;
        case EProbeStepType::SelectedActorIsTemporarilyHidden:
        case EProbeStepType::SelectedActorIsNotTemporarilyHidden: break;

        }
        return Step;
    }

    // Built-in 0.7 templates also use the public registration API. This exercises
    // exactly the same discovery and creation path third-party plugins use.
    static void RegisterProbeAdvancedTemplates()
    {
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.PluginOrigin");
            Item.Category = TEXT("Plugins & Modules");
            Item.Name = TEXT("Plugin Origin");
            Item.Description = TEXT("Check whether the Probe plugin is project-installed");
            Item.Steps.Add(MakeStep(EProbeStepType::PluginIsProjectPlugin, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.PluginModuleInventory");
            Item.Category = TEXT("Plugins & Modules");
            Item.Name = TEXT("Plugin Module Inventory");
            Item.Description = TEXT("Verify module count declared by a plugin");
            Item.Steps.Add(MakeStep(EProbeStepType::PluginInstalled, TEXT("{Self}")));
            Item.Steps.Add(MakeStep(EProbeStepType::PluginModuleCountAtLeast, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.PluginCategory");
            Item.Category = TEXT("Plugins & Modules");
            Item.Name = TEXT("Plugin Descriptor Category");
            Item.Description = TEXT("Verify the plugin descriptor category");
            Item.Steps.Add(MakeStep(EProbeStepType::PluginCategoryEquals, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.LoadedModuleInventory");
            Item.Category = TEXT("Plugins & Modules");
            Item.Name = TEXT("Loaded Modules");
            Item.Description = TEXT("Check that the Editor has loaded modules");
            Item.Steps.Add(MakeStep(EProbeStepType::LoadedModuleCountAtLeast, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.ToolMenuSection");
            Item.Category = TEXT("Editor UI");
            Item.Name = TEXT("Tool Menu Section");
            Item.Description = TEXT("Verify a ToolMenus internal section ID exists (Probe uses ProbeTools, displayed as Testing)");
            Item.Steps.Add(MakeStep(EProbeStepType::ToolMenuSectionExists, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.ToolMenuSectionMissing");
            Item.Category = TEXT("Editor UI");
            Item.Name = TEXT("Missing Tool Menu Section");
            Item.Description = TEXT("Verify a menu section is absent (set Expected Value)");
            Item.Steps.Add(MakeStep(EProbeStepType::ToolMenuSectionNotExists, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.SelectionEmpty");
            Item.Category = TEXT("Selection");
            Item.Name = TEXT("No Selected Actors");
            Item.Description = TEXT("Verify the Editor actor selection is empty");
            Item.Steps.Add(MakeStep(EProbeStepType::SelectedActorsEmpty, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.SelectionPresent");
            Item.Category = TEXT("Selection");
            Item.Name = TEXT("Actor Selection Present");
            Item.Description = TEXT("Verify that at least one actor is selected");
            Item.Steps.Add(MakeStep(EProbeStepType::SelectedActorsNotEmpty, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.AssetNotRedirector");
            Item.Category = TEXT("Assets");
            Item.Name = TEXT("No Redirector");
            Item.Description = TEXT("Check that a Probe Test asset is not a redirector");
            Item.Steps.Add(MakeStep(EProbeStepType::AssetIsNotRedirector, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.AssetRedirector");
            Item.Category = TEXT("Assets");
            Item.Name = TEXT("Redirector Contract");
            Item.Description = TEXT("Test whether a specified asset is a redirector (configure Target)");
            Item.Steps.Add(MakeStep(EProbeStepType::AssetIsRedirector, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.RegistryTagAbsent");
            Item.Category = TEXT("Assets");
            Item.Name = TEXT("Registry Tag Absent");
            Item.Description = TEXT("Check that an asset registry tag is absent");
            Item.Steps.Add(MakeStep(EProbeStepType::AssetRegistryTagNotExists, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.ClassInterface");
            Item.Category = TEXT("Classes & Reflection");
            Item.Name = TEXT("Interface Contract");
            Item.Description = TEXT("Verify that a specified class is a UInterface (configure Target)");
            Item.Steps.Add(MakeStep(EProbeStepType::ClassIsInterface, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.ConcreteClass");
            Item.Category = TEXT("Classes & Reflection");
            Item.Name = TEXT("Concrete Actor Class");
            Item.Description = TEXT("Verify an Actor class is not abstract");
            Item.Steps.Add(MakeStep(EProbeStepType::ClassIsNotAbstract, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.BlueprintPropertyVisibility");
            Item.Category = TEXT("Classes & Reflection");
            Item.Name = TEXT("Blueprint Property Visibility");
            Item.Description = TEXT("Verify a reflected property is exposed to Blueprints");
            Item.Steps.Add(MakeStep(EProbeStepType::ClassHasProperty, TEXT("{Self}")));
            Item.Steps.Add(MakeStep(EProbeStepType::ClassPropertyBlueprintVisible, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.EditableProperty");
            Item.Category = TEXT("Classes & Reflection");
            Item.Name = TEXT("Editable Property");
            Item.Description = TEXT("Verify a reflected class property is editable");
            Item.Steps.Add(MakeStep(EProbeStepType::ClassPropertyEditable, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.BlueprintPureFunction");
            Item.Category = TEXT("Classes & Reflection");
            Item.Name = TEXT("Blueprint Pure Function");
            Item.Description = TEXT("Check a BlueprintPure UFUNCTION");
            Item.Steps.Add(MakeStep(EProbeStepType::ClassFunctionIsPure, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.StaticFunctionContract");
            Item.Category = TEXT("Classes & Reflection");
            Item.Name = TEXT("Static Function");
            Item.Description = TEXT("Check a reflected static UFUNCTION");
            Item.Steps.Add(MakeStep(EProbeStepType::ClassFunctionIsStatic, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.FunctionParameterContract");
            Item.Category = TEXT("Classes & Reflection");
            Item.Name = TEXT("Function Parameter");
            Item.Description = TEXT("Verify a reflected function includes a named parameter");
            Item.Steps.Add(MakeStep(EProbeStepType::ClassHasFunction, TEXT("{Self}")));
            Item.Steps.Add(MakeStep(EProbeStepType::ClassFunctionHasParameter, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.ActorLabelSearch");
            Item.Category = TEXT("Actors & World");
            Item.Name = TEXT("Actor Label Contains");
            Item.Description = TEXT("Find actors with a substring in their label");
            Item.Steps.Add(MakeStep(EProbeStepType::ActorLabelContains, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.ActorTagMinimum");
            Item.Category = TEXT("Actors & World");
            Item.Name = TEXT("Actor Tag Minimum");
            Item.Description = TEXT("Require at least N actors with a configured tag");
            Item.Steps.Add(MakeStep(EProbeStepType::ActorCountWithTagAtLeast, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.ActorTagByLabel");
            Item.Category = TEXT("Actors & World");
            Item.Name = TEXT("Named Actor Has Tag");
            Item.Description = TEXT("Verify a named actor has an expected tag");
            Item.Steps.Add(MakeStep(EProbeStepType::ActorLabelExists, TEXT("{Self}")));
            Item.Steps.Add(MakeStep(EProbeStepType::ActorHasTagByLabel, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.ActorMissingTag");
            Item.Category = TEXT("Actors & World");
            Item.Name = TEXT("Named Actor Lacks Tag");
            Item.Description = TEXT("Verify a named actor lacks a given tag");
            Item.Steps.Add(MakeStep(EProbeStepType::ActorDoesNotHaveTagByLabel, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.SelectedActorNoTag");
            Item.Category = TEXT("Selection");
            Item.Name = TEXT("Selected Actor Without Tag");
            Item.Description = TEXT("Require exactly one selected actor without a tag");
            Item.Steps.Add(MakeStep(EProbeStepType::SelectedActorCountExactly, TEXT("{Self}")));
            Item.Steps.Add(MakeStep(EProbeStepType::SelectedActorHasNoTag, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.SelectedLabelContains");
            Item.Category = TEXT("Selection");
            Item.Name = TEXT("Selected Actor Name Match");
            Item.Description = TEXT("Verify selected actor label contains a substring");
            Item.Steps.Add(MakeStep(EProbeStepType::SelectedActorLabelContains, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.StringPropertyContains");
            Item.Category = TEXT("Properties & Values");
            Item.Name = TEXT("String Contains");
            Item.Description = TEXT("Check that a FString property contains the expected text");
            Item.Steps.Add(MakeStep(EProbeStepType::ObjectPropertyStringContains, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.StringPropertyPrefix");
            Item.Category = TEXT("Properties & Values");
            Item.Name = TEXT("String Prefix");
            Item.Description = TEXT("Check a FString property begins with expected text");
            Item.Steps.Add(MakeStep(EProbeStepType::ObjectPropertyStringStartsWith, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.StringPropertySuffix");
            Item.Category = TEXT("Properties & Values");
            Item.Name = TEXT("String Suffix");
            Item.Description = TEXT("Check a FString property ends with expected text");
            Item.Steps.Add(MakeStep(EProbeStepType::ObjectPropertyStringEndsWith, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.BooleanTrue");
            Item.Category = TEXT("Properties & Values");
            Item.Name = TEXT("Boolean Is True");
            Item.Description = TEXT("Verify a boolean property is true");
            Item.Steps.Add(MakeStep(EProbeStepType::ObjectPropertyIsTrue, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
        {
            FExtensionTemplate Item;
            Item.Id = TEXT("Probe.BooleanFalse");
            Item.Category = TEXT("Properties & Values");
            Item.Name = TEXT("Boolean Is False");
            Item.Description = TEXT("Verify a boolean property is false");
            Item.Steps.Add(MakeStep(EProbeStepType::ObjectPropertyIsFalse, TEXT("{Self}")));
            RegisterTemplate(MoveTemp(Item));
        }
    }
    // Runnable action templates operate only on transient UObject fixtures. No level or
    // Content Browser assets are changed, and fixture state never crosses test runs.
    static void RegisterProbeContractTemplates()
    {
        struct FDefinition { const TCHAR* Id; const TCHAR* Category; const TCHAR* Name; EProbeStepType Type; bool bSelfAsset; };
        const FDefinition Definitions[] = {
            {TEXT("Probe.ContractPluginVersionContains"), TEXT("Plugins & Modules"), TEXT("Plugin Version Contains"), EProbeStepType::PluginVersionContains, false},
            {TEXT("Probe.ContractPluginFriendlyNameContains"), TEXT("Plugins & Modules"), TEXT("Plugin Friendly Name Contains"), EProbeStepType::PluginFriendlyNameContains, false},
            {TEXT("Probe.ContractPluginHasNoContent"), TEXT("Plugins & Modules"), TEXT("Plugin Has No Content"), EProbeStepType::PluginHasNoContent, false},
            {TEXT("Probe.ContractLoadedModuleCountExactly"), TEXT("Plugins & Modules"), TEXT("Loaded Module Count Exactly"), EProbeStepType::LoadedModuleCountExactly, false},
            {TEXT("Probe.ContractAssetCountDirectAtLeast"), TEXT("Assets"), TEXT("Asset Count In Folder (Direct) At Least"), EProbeStepType::AssetCountDirectAtLeast, false},
            {TEXT("Probe.ContractAssetCountDirectExactly"), TEXT("Assets"), TEXT("Asset Count In Folder (Direct) Exactly"), EProbeStepType::AssetCountDirectExactly, false},
            {TEXT("Probe.ContractAssetPackagePathEquals"), TEXT("Assets"), TEXT("Asset Package Path Equals"), EProbeStepType::AssetPackagePathEquals, true},
            {TEXT("Probe.ContractAssetClassNameEquals"), TEXT("Assets"), TEXT("Asset Class Name Equals"), EProbeStepType::AssetClassNameEquals, true},
            {TEXT("Probe.ContractClassPropertyIsArray"), TEXT("Classes & Reflection"), TEXT("Class Property Is Array"), EProbeStepType::ClassPropertyIsArray, false},
            {TEXT("Probe.ContractClassPropertyIsObjectReference"), TEXT("Classes & Reflection"), TEXT("Class Property Is Object Reference"), EProbeStepType::ClassPropertyIsObjectReference, false},
            {TEXT("Probe.ContractClassPropertyIsStruct"), TEXT("Classes & Reflection"), TEXT("Class Property Is Struct"), EProbeStepType::ClassPropertyIsStruct, false},
            {TEXT("Probe.ContractClassPropertyIsEnum"), TEXT("Classes & Reflection"), TEXT("Class Property Is Enum"), EProbeStepType::ClassPropertyIsEnum, false},
            {TEXT("Probe.ContractClassPropertyIsName"), TEXT("Classes & Reflection"), TEXT("Class Property Is Name"), EProbeStepType::ClassPropertyIsName, false},
            {TEXT("Probe.ContractClassPropertyIsString"), TEXT("Classes & Reflection"), TEXT("Class Property Is String"), EProbeStepType::ClassPropertyIsString, false},
            {TEXT("Probe.ContractClassPropertyIsText"), TEXT("Classes & Reflection"), TEXT("Class Property Is Text"), EProbeStepType::ClassPropertyIsText, false},
            {TEXT("Probe.ContractClassFunctionIsConst"), TEXT("Classes & Reflection"), TEXT("Class Function Is Const"), EProbeStepType::ClassFunctionIsConst, false},
            {TEXT("Probe.ContractClassFunctionIsPublic"), TEXT("Classes & Reflection"), TEXT("Class Function Is Public"), EProbeStepType::ClassFunctionIsPublic, false},
            {TEXT("Probe.ContractClassFunctionIsDeprecated"), TEXT("Classes & Reflection"), TEXT("Class Function Is Deprecated"), EProbeStepType::ClassFunctionIsDeprecated, false},
            {TEXT("Probe.ContractClassHasMetadata"), TEXT("Classes & Reflection"), TEXT("Class Has Metadata"), EProbeStepType::ClassHasMetadata, false},
            {TEXT("Probe.ContractActorLabelCountAtMost"), TEXT("Actors & World"), TEXT("Actor Label Count At Most"), EProbeStepType::ActorLabelCountAtMost, false},
            {TEXT("Probe.ContractActorCountWithTagAtMost"), TEXT("Actors & World"), TEXT("Actor Count With Tag At Most"), EProbeStepType::ActorCountWithTagAtMost, false},
            {TEXT("Probe.ContractActorFolderCountExactly"), TEXT("Actors & World"), TEXT("Actor Folder Count Exactly"), EProbeStepType::ActorFolderCountExactly, false},
            {TEXT("Probe.ContractActorFolderCountAtMost"), TEXT("Actors & World"), TEXT("Actor Folder Count At Most"), EProbeStepType::ActorFolderCountAtMost, false},
            {TEXT("Probe.ContractSelectedActorIsTemporarilyHidden"), TEXT("Selection"), TEXT("Selected Actor Is Temporarily Hidden"), EProbeStepType::SelectedActorIsTemporarilyHidden, false},
            {TEXT("Probe.ContractSelectedActorIsNotTemporarilyHidden"), TEXT("Selection"), TEXT("Selected Actor Is Not Temporarily Hidden"), EProbeStepType::SelectedActorIsNotTemporarilyHidden, false},
        };
        for (const FDefinition& Item : Definitions)
        {
            FExtensionTemplate Template;
            Template.Id = FName(Item.Id); Template.Name = Item.Name; Template.Category = Item.Category;
            Template.Description = FString(TEXT("Configure and verify: ")) + Item.Name;
            Template.bRequiresConfiguration = true;
            Template.Steps.Add(MakeStep(Item.Type));
            if (Item.bSelfAsset) { Template.Steps[0].Target = TEXT("{Self}"); }
            RegisterTemplate(MoveTemp(Template));
        }
    }

    static void RegisterProbeActionTemplates()
    {
        const FString FixtureClass = TEXT("/Script/ProbeEditor.ProbeTestAsset");
        auto NewFixture = [&FixtureClass]()
        {
            FProbeTestStep Step;
            Step.Type = EProbeStepType::FixtureCreateObject;
            Step.Target = FixtureClass;
            Step.ExpectedValue = TEXT("Sample");
            Step.Description = TEXT("Create a temporary Probe Test object, named Sample");
            return Step;
        };
        auto NewAction = [](EProbeStepType Type, const TCHAR* Property, const TCHAR* Value)
        {
            FProbeTestStep Step;
            Step.Type = Type;
            Step.Target = TEXT("Sample");
            Step.PropertyName = Property;
            Step.ExpectedValue = Value;
            return Step;
        };
        {
            FExtensionTemplate Template;
            Template.Id = TEXT("Probe.TemporaryObjectLifecycle");
            Template.Category = TEXT("Test Actions");
            Template.Name = TEXT("Temporary Object Lifecycle");
            Template.Description = TEXT("Create, verify and destroy an isolated transient UObject fixture.");
            Template.bRequiresConfiguration = false;
            Template.Steps.Add(NewFixture());
            Template.Steps.Add(NewAction(EProbeStepType::FixtureObjectExists, TEXT(""), TEXT("")));
            Template.Steps.Add(NewAction(EProbeStepType::FixtureDestroyObject, TEXT(""), TEXT("")));
            RegisterTemplate(MoveTemp(Template));
        }
        {
            FExtensionTemplate Template;
            Template.Id = TEXT("Probe.TemporaryBooleanMutation");
            Template.Category = TEXT("Test Actions");
            Template.Name = TEXT("Temporary Boolean Mutation");
            Template.Description = TEXT("Set bEnabled on an isolated fixture, then assert it changed.");
            Template.bRequiresConfiguration = false;
            Template.Steps.Add(NewFixture());
            Template.Steps.Add(NewAction(EProbeStepType::FixtureSetProperty, TEXT("bEnabled"), TEXT("False")));
            Template.Steps.Add(NewAction(EProbeStepType::FixturePropertyEquals, TEXT("bEnabled"), TEXT("False")));
            Template.Steps.Add(NewAction(EProbeStepType::FixtureDestroyObject, TEXT(""), TEXT("")));
            RegisterTemplate(MoveTemp(Template));
        }
        {
            FExtensionTemplate Template;
            Template.Id = TEXT("Probe.TemporaryStringMutation");
            Template.Category = TEXT("Test Actions");
            Template.Name = TEXT("Temporary String Mutation");
            Template.Description = TEXT("Change and verify TestDescription on a temporary Probe Test object.");
            Template.bRequiresConfiguration = false;
            Template.Steps.Add(NewFixture());
            Template.Steps.Add(NewAction(EProbeStepType::FixtureSetProperty, TEXT("TestDescription"), TEXT("Automated fixture")));
            Template.Steps.Add(NewAction(EProbeStepType::FixturePropertyEquals, TEXT("TestDescription"), TEXT("Automated fixture")));
            Template.Steps.Add(NewAction(EProbeStepType::FixturePropertyNotEquals, TEXT("TestDescription"), TEXT("Unexpected description")));
            RegisterTemplate(MoveTemp(Template));
        }
    }

    // 0.8.0 templates are examples requiring explicit configuration before Run All.
    static void RegisterProbeWorkflowTemplates()
    {
        struct FWorkflowTemplate
        {
            const TCHAR* Id;
            const TCHAR* Category;
            const TCHAR* Name;
            const TCHAR* Description;
            EProbeStepType Type;
            bool bSelfAsset;
        };
        const FWorkflowTemplate Definitions[] = {
            {TEXT("Probe.PluginModuleExact"), TEXT("Plugins & Modules"), TEXT("Plugin Module Exact Count"), TEXT("Configure plugin name and expected exact number of declared modules"), EProbeStepType::PluginModuleCountExactly, false},
            {TEXT("Probe.PluginModuleMaximum"), TEXT("Plugins & Modules"), TEXT("Plugin Module Maximum"), TEXT("Check maximum plugin module count"), EProbeStepType::PluginModuleCountAtMost, false},
            {TEXT("Probe.PluginDescriptionContains"), TEXT("Plugins & Modules"), TEXT("Plugin Description Contains"), TEXT("Select plugin and expected description substring"), EProbeStepType::PluginDescriptionContains, false},
            {TEXT("Probe.PluginAuthor"), TEXT("Plugins & Modules"), TEXT("Plugin Author"), TEXT("Check CreatedBy in plugin descriptor"), EProbeStepType::PluginAuthorEquals, false},
            {TEXT("Probe.ModuleMaximum"), TEXT("Plugins & Modules"), TEXT("Loaded Modules Maximum"), TEXT("Check an upper bound on Editor module count"), EProbeStepType::LoadedModuleCountAtMost, false},
            {TEXT("Probe.AssetFolderMaximum"), TEXT("Assets"), TEXT("Maximum Asset Count"), TEXT("Select folder and set upper bound"), EProbeStepType::AssetCountAtMost, false},
            {TEXT("Probe.AssetNameContains"), TEXT("Assets"), TEXT("Asset Name Contains"), TEXT("Check selected asset name includes text"), EProbeStepType::AssetNameContains, true},
            {TEXT("Probe.AssetNamePrefix"), TEXT("Assets"), TEXT("Asset Name Prefix"), TEXT("Check selected asset naming convention"), EProbeStepType::AssetNameStartsWith, true},
            {TEXT("Probe.AssetNameSuffix"), TEXT("Assets"), TEXT("Asset Name Suffix"), TEXT("Check selected asset name suffix"), EProbeStepType::AssetNameEndsWith, true},
            {TEXT("Probe.RegistryTagSubstring"), TEXT("Assets"), TEXT("Asset Registry Tag Substring"), TEXT("Choose asset, registry tag and required value substring"), EProbeStepType::AssetRegistryTagValueContains, true},
            {TEXT("Probe.BooleanPropertyContract"), TEXT("Classes & Reflection"), TEXT("Boolean Property Contract"), TEXT("Select class and reflected Boolean property"), EProbeStepType::ClassPropertyIsBoolean, false},
            {TEXT("Probe.NumericPropertyContract"), TEXT("Classes & Reflection"), TEXT("Numeric Property Contract"), TEXT("Select class and numeric property"), EProbeStepType::ClassPropertyIsNumeric, false},
            {TEXT("Probe.FunctionReturnContract"), TEXT("Classes & Reflection"), TEXT("Function Return Contract"), TEXT("Select class and function; require reflected return value"), EProbeStepType::ClassFunctionHasReturnValue, false},
            {TEXT("Probe.OutlinerLabelPrefix"), TEXT("Actors & World"), TEXT("Outliner Label Prefix"), TEXT("Set name prefix to find in current level"), EProbeStepType::ActorLabelStartsWith, false},
            {TEXT("Probe.OutlinerLabelSuffix"), TEXT("Actors & World"), TEXT("Outliner Label Suffix"), TEXT("Set name suffix to find in current level"), EProbeStepType::ActorLabelEndsWith, false},
            {TEXT("Probe.OutlinerLabelMinimum"), TEXT("Actors & World"), TEXT("Outliner Label Minimum Count"), TEXT("Set exact label and minimum number of actors"), EProbeStepType::ActorLabelCountAtLeast, false},
            {TEXT("Probe.ActorTagExactCount"), TEXT("Actors & World"), TEXT("Exact Actor Tag Count"), TEXT("Set actor tag and expected exact count"), EProbeStepType::ActorCountWithTagExactly, false},
            {TEXT("Probe.OutlinerFolderMinimum"), TEXT("Actors & World"), TEXT("Outliner Folder Count"), TEXT("Set folder and required minimum actors"), EProbeStepType::ActorFolderCountAtLeast, false},
            {TEXT("Probe.SelectedLabelPrefix"), TEXT("Selection"), TEXT("Selected Actor Label Prefix"), TEXT("Select one actor and configure the prefix"), EProbeStepType::SelectedActorLabelStartsWith, false},
            {TEXT("Probe.SelectedLabelSuffix"), TEXT("Selection"), TEXT("Selected Actor Label Suffix"), TEXT("Select one actor and configure the suffix"), EProbeStepType::SelectedActorLabelEndsWith, false},
            {TEXT("Probe.SelectedActorFolder"), TEXT("Selection"), TEXT("Selected Actor Folder"), TEXT("Select one actor and check its Outliner folder"), EProbeStepType::SelectedActorFolderEquals, false},
            {TEXT("Probe.StringPropertyExclusion"), TEXT("Properties & Values"), TEXT("String Does Not Contain"), TEXT("Choose object/property and forbidden text"), EProbeStepType::ObjectPropertyStringNotContains, true},
            {TEXT("Probe.StringPropertyCaseInsensitive"), TEXT("Properties & Values"), TEXT("String Equals Ignore Case"), TEXT("Choose object/property and comparison text"), EProbeStepType::ObjectPropertyStringEqualsIgnoreCase, true},
            {TEXT("Probe.NumericRange"), TEXT("Properties & Values"), TEXT("Numeric Inclusive Range"), TEXT("Enter min,max for numeric property"), EProbeStepType::ObjectPropertyNumericInRange, true},
            {TEXT("Probe.NumericNear"), TEXT("Properties & Values"), TEXT("Numeric Near Value"), TEXT("Compare numeric property within tolerance"), EProbeStepType::ObjectPropertyNumericNearlyEquals, true},
        };
        for (const FWorkflowTemplate& Definition : Definitions)
        {
            FExtensionTemplate Item;
            Item.Id = FName(Definition.Id);
            Item.Category = Definition.Category;
            Item.Name = Definition.Name;
            Item.Description = Definition.Description;
            Item.bRequiresConfiguration = true;
            Item.Steps.Add(MakeStep(Definition.Type, Definition.bSelfAsset ? TEXT("{Self}") : FString()));
            RegisterTemplate(MoveTemp(Item));
        }
    }

}

DECLARE_DELEGATE_OneParam(FOnProbeIdentifierPicked, const FString&);

struct FProbeIdentifierChoice
{
    FString Id;
    FString Display;
    FString Owner;
    FString State;
};

// Search by what people know (plugin/friendly tab name) while storing stable internal IDs.
class SProbeIdentifierPicker : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SProbeIdentifierPicker) {}
        SLATE_ARGUMENT(TArray<FProbeIdentifierChoice>, Candidates)
        SLATE_ARGUMENT(FText, SearchHint)
        SLATE_EVENT(FOnProbeIdentifierPicked, OnPicked)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Candidates = Args._Candidates;
        Picked = Args._OnPicked;
        ChildSlot [ SNew(SBox).WidthOverride(445.0f)
            [ SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(5)
                [ SNew(SSearchBox)
                  .HintText(Args._SearchHint)
                  .OnTextChanged(this, &SProbeIdentifierPicker::OnFilterChanged) ]
                + SVerticalBox::Slot().FillHeight(1).Padding(5,0,5,5)
                [ SNew(SBox).HeightOverride(315.0f)
                  [ SNew(SScrollBox) + SScrollBox::Slot()
                    [ SAssignNew(ResultsBox, SVerticalBox) ] ] ]
                + SVerticalBox::Slot().AutoHeight().Padding(7,2,7,6)
                [ SNew(STextBlock).AutoWrapText(true)
                  .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                  .Text(LOCTEXT("IdentifierPickerHint", "Uses the internal ID automatically. Some local asset-editor tabs may require manual entry.")) ]
            ] ];
        Refresh();
    }
private:
    TArray<FProbeIdentifierChoice> Candidates;
    FOnProbeIdentifierPicked Picked;
    TSharedPtr<SVerticalBox> ResultsBox;
    FString Search;
    void OnFilterChanged(const FText& Text) { Search = Text.ToString(); Refresh(); }
    void Refresh()
    {
        if (!ResultsBox.IsValid()) { return; }
        ResultsBox->ClearChildren();
        int32 Added = 0;
        for (const FProbeIdentifierChoice& Candidate : Candidates)
        {
            const FString Query = Candidate.Id + TEXT(" ") + Candidate.Display + TEXT(" ") + Candidate.Owner + TEXT(" ") + Candidate.State;
            if (!Search.IsEmpty() && !Query.Contains(Search, ESearchCase::IgnoreCase)) { continue; }
            if (++Added > 120) { break; }
            ResultsBox->AddSlot().AutoHeight().Padding(1)
            [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                .ToolTipText(FText::FromString(Candidate.Id))
                .HAlign(HAlign_Fill)
                .OnClicked_Lambda([this, Id = Candidate.Id]()
                { Picked.ExecuteIfBound(Id); FSlateApplication::Get().DismissAllMenus(); return FReply::Handled(); })
                [ SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.0f)
                        [ SNew(STextBlock).Text(FText::FromString(Candidate.Display)) ]
                        + SHorizontalBox::Slot().AutoWidth()
                        [ SNew(STextBlock).Text(FText::FromString(Candidate.State))
                          .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock)
                      .Text(FText::FromString(Candidate.Owner + TEXT("  |  ID: ") + Candidate.Id))
                      .Font(FAppStyle::GetFontStyle("SmallFont"))
                      .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
                ] ];
        }
        if (Added == 0)
        {
            ResultsBox->AddSlot().AutoHeight().Padding(5)
            [ SNew(STextBlock).Text(LOCTEXT("IdentifierNoMatches", "No matches. You can still enter an identifier manually.")) ];
        }
    }
};

// A result row uses native button styling while supporting double-click-to-edit.
class SProbeResultButton : public SButton
{
public:
    SLATE_BEGIN_ARGS(SProbeResultButton) {}
        SLATE_DEFAULT_SLOT(FArguments, Content)
        SLATE_EVENT(FOnClicked, OnClicked)
        SLATE_EVENT(FOnClicked, OnDoubleClicked)
        SLATE_ATTRIBUTE(FText, ToolTipText)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        DoubleClicked = Args._OnDoubleClicked;
        SButton::Construct(SButton::FArguments()
            .ButtonStyle(FAppStyle::Get(), "SimpleButton")
            .OnClicked(Args._OnClicked)
            .ToolTipText(Args._ToolTipText)
            [ Args._Content.Widget ]);
    }
    virtual FReply OnMouseButtonDoubleClick(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (Event.GetEffectingButton() == EKeys::LeftMouseButton && DoubleClicked.IsBound())
        { return DoubleClicked.Execute(); }
        return SButton::OnMouseButtonDoubleClick(Geometry, Event);
    }
private:
    FOnClicked DoubleClicked;
};

class SProbeWorkspace : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SProbeWorkspace) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        GConfig->GetFloat(TEXT("ProbeWorkspace"), TEXT("DefinitionFraction"), DefinitionFraction, GEditorPerProjectIni);
        DefinitionFraction = FMath::Clamp(DefinitionFraction, 0.20f, 0.85f);
        RefreshAssets();
        FEditorDelegates::PostUndoRedo.AddSP(this, &SProbeWorkspace::HandleUndoRedo);
        ChildSlot
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                .Padding(FMargin(5, 3))
                [
                    // Single command row. When docked extremely narrowly, the row can
                    // scroll horizontally rather than clipping inaccessible controls.
                    SNew(SScrollBox)
                    .Orientation(Orient_Horizontal)
                    .ScrollBarVisibility(EVisibility::Visible)
                    + SScrollBox::Slot()
                    [
                        SNew(SBox)
                        .MinDesiredWidth(685.0f)
                        .WidthOverride_Lambda([this]() -> FOptionalSize
                        {
                            // Fill normal-sized dock tabs; keep the toolbar horizontally
                            // scrollable rather than clipping actions in narrow docks.
                            return FOptionalSize(FMath::Max(685.0f,
                                GetCachedGeometry().GetLocalSize().X - 16.0f));
                        })
                        [
                            SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 3, 0)
                            [
                                SNew(SButton)
                                .ToolTipText(LOCTEXT("CreateTip", "Create a blank Probe Test asset"))
                                .OnClicked(this, &SProbeWorkspace::CreateBlankAsset)
                                [
                                    SNew(SHorizontalBox)
                                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                                    [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Plus")) ]
                                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                                    [ SNew(STextBlock).Text(LOCTEXT("NewTest", "New Test")) ]
                                ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
                            [
                                SNew(SComboButton)
                                .ToolTipText(LOCTEXT("TemplateTip", "Create a Probe Test from a template"))
                                .OnGetMenuContent(this, &SProbeWorkspace::BuildTemplateMenu)
                                .ButtonContent()
                                [ SNew(STextBlock).Text(LOCTEXT("Templates", "Templates")) ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                SNew(SButton)
                                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                .ToolTipText(LOCTEXT("SaveTip", "Save selected test asset (Ctrl+S)"))
                                .IsEnabled(this, &SProbeWorkspace::CanSaveSelected)
                                .OnClicked(this, &SProbeWorkspace::SaveSelectedClicked)
                                [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Save")) ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                SNew(SButton)
                                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                .ToolTipText(LOCTEXT("RefreshTip", "Refresh available Probe tests"))
                                .OnClicked(this, &SProbeWorkspace::RefreshClicked)
                                [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Refresh")) ]
                            ]
                            + SHorizontalBox::Slot().FillWidth(1.0f)
                            [ SNullWidget::NullWidget ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(7, 0, 4, 0)
                            [
                                SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                .ToolTipText(LOCTEXT("ManagePacksTip", "Open the Probe Packs workspace to add, remove, save, and run groups of tests"))
                                .OnClicked_Lambda([]() { FGlobalTabmanager::Get()->TryInvokeTab(Probe::PackTabId); return FReply::Handled(); })
                                [ SNew(STextBlock).Text(LOCTEXT("ManagePacksButton", "Manage Packs")) ]
                            ]
                            // Execution controls read left-to-right Export, Rerun,
                            // Run All, Run Selected. The primary action stays at the edge.
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                SNew(SButton)
                                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                .ToolTipText(LOCTEXT("ExportResultsTip", "Export current results as JSON in Saved/Probe/Reports"))
                                .IsEnabled(this, &SProbeWorkspace::HasResults)
                                .OnClicked(this, &SProbeWorkspace::ExportReport)
                                [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Export")) ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                SNew(SButton)
                                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                .ToolTipText(LOCTEXT("RerunProblemsTip", "Rerun tests that failed or errored"))
                                .IsEnabled(this, &SProbeWorkspace::HasProblemResults)
                                .OnClicked(this, &SProbeWorkspace::RerunProblems)
                                [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Reimport")) ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 3, 0)
                            [
                                SNew(SComboButton)
                                .ToolTipText(LOCTEXT("RunPackTip", "Run an existing Probe Pack; use Manage Packs to create and edit packs"))
                                .OnGetMenuContent(this, &SProbeWorkspace::BuildPackMenu)
                                .ButtonContent()
                                [ SNew(STextBlock).Text(LOCTEXT("RunPack", "Run Pack")) ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 3, 0)
                            [
                                SNew(SButton)
                                .ToolTipText(LOCTEXT("RunAllTip", "Run every discovered Probe test"))
                                .OnClicked(this, &SProbeWorkspace::RunAll)
                                [ SNew(STextBlock).Text(LOCTEXT("RunAll", "Run All")) ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                SNew(SButton)
                                .IsEnabled(this, &SProbeWorkspace::HasSelectedAsset)
                                .ToolTipText(LOCTEXT("RunSelectedTip", "Run the selected test without saving its unsaved edits"))
                                .OnClicked(this, &SProbeWorkspace::RunSelected)
                                [
                                    SNew(SHorizontalBox)
                                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                                    [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Play")) ]
                                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                                    [ SNew(STextBlock).Text(LOCTEXT("RunSelected", "Run Selected")) ]
                                ]
                            ]
                        ]
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(8, 7, 8, 6)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                    [ SNew(STextBlock).Text(LOCTEXT("SelectedTest", "Selected Test")) ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [ SNew(STextBlock).Text(this, &SProbeWorkspace::DirtyStatusText) ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SAssignNew(TestSelector, SComboBox<TSharedPtr<FAssetData>>)
                    .OptionsSource(&Assets)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FAssetData> Item)
                    {
                        return SNew(STextBlock).Text(FText::FromName(Item.IsValid() ? Item->AssetName : NAME_None));
                    })
                    .OnSelectionChanged(this, &SProbeWorkspace::OnSelectionChanged)
                    [ SNew(STextBlock).Text(this, &SProbeWorkspace::SelectedName) ]
                ]
            ]
            + SVerticalBox::Slot().FillHeight(1.0f).Padding(7, 0, 7, 0)
            [
                SNew(SSplitter).Orientation(Orient_Vertical)
                + SSplitter::Slot().Value(TAttribute<float>::Create(
                    TAttribute<float>::FGetter::CreateLambda([this]() { return bResultsFocused ? 0.0f : DefinitionFraction; })))
                .OnSlotResized(SSplitter::FOnSlotResized::CreateSP(this, &SProbeWorkspace::OnDefinitionResized))
                [
                    SNew(SVerticalBox)
                    .Visibility_Lambda([this]() { return bResultsFocused ? EVisibility::Collapsed : EVisibility::Visible; })
                    + SVerticalBox::Slot().AutoHeight().Padding(2, 3, 2, 6)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                        [ SNew(STextBlock).Text(LOCTEXT("DefinitionTitle", "Test Definition")) ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [
                            SNew(SComboButton)
                            .IsEnabled(this, &SProbeWorkspace::HasSelectedAsset)
                            .OnGetMenuContent(this, &SProbeWorkspace::BuildAddStepMenu)
                            .ToolTipText(LOCTEXT("AddStepTip", "Add an assertion step to this test"))
                            .ButtonContent()
                            [
                                SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                                [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Plus")) ]
                                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                                [ SNew(STextBlock).Text(LOCTEXT("AddStep", "Add Step")) ]
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot().FillHeight(1.0f)
                    [
                        SNew(SBorder)
                        .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                        .Padding(4)
                        [
                            SNew(SScrollBox)
                            + SScrollBox::Slot()
                            [ SAssignNew(EditorRows, SVerticalBox) ]
                        ]
                    ]
                ]
                + SSplitter::Slot().Value(TAttribute<float>::Create(
                    TAttribute<float>::FGetter::CreateLambda([this]() { return bResultsFocused ? 1.0f : 1.0f - DefinitionFraction; })))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(2, 6, 2, 4)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
                        [ SNew(STextBlock).Text(LOCTEXT("Results", "Results")) ]
                        + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                        [ SNew(SScrollBox).Orientation(Orient_Horizontal).ScrollBarVisibility(EVisibility::Visible)
                          + SScrollBox::Slot() [ SAssignNew(ResultSummary, SHorizontalBox) ] ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
                        [
                            SNew(SButton)
                            .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                            .ToolTipText(LOCTEXT("FocusResultsTip", "Expand Results to use the entire workspace; click again to show Test Definition"))
                            .OnClicked(this, &SProbeWorkspace::ToggleResultsFocus)
                            [ SNew(STextBlock).Text_Lambda([this]() { return bResultsFocused
                                ? LOCTEXT("ShowDefinition", "Show Definition") : LOCTEXT("FocusResults", "Focus Results"); }) ]
                        ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [
                            SNew(SButton)
                            .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                            .ToolTipText(LOCTEXT("CopyResultsTip", "Copy the complete unfiltered Results log"))
                            .IsEnabled(this, &SProbeWorkspace::HasResults)
                            .OnClicked(this, &SProbeWorkspace::CopyResults)
                            [
                                SNew(SHorizontalBox)
                                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                                [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Duplicate")) ]
                                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                                [ SNew(STextBlock).Text(LOCTEXT("CopyResults", "Copy Results")) ]
                            ]
                        ]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(2, 0, 2, 4)
                    [ SAssignNew(ResultTabs, SHorizontalBox) ]
                    + SVerticalBox::Slot().FillHeight(1.0f)
                    [
                        SAssignNew(ResultViewSwitcher, SWidgetSwitcher)
                        + SWidgetSwitcher::Slot()
                        [
                            SNew(SSplitter).Orientation(Orient_Horizontal)
                            + SSplitter::Slot().Value(0.40f)
                            [
                                SNew(SBorder)
                                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                                .Padding(3)
                                [
                                    SNew(SScrollBox)
                                    + SScrollBox::Slot() [ SAssignNew(ResultList, SVerticalBox) ]
                                ]
                            ]
                            + SSplitter::Slot().Value(0.60f)
                            [
                                SNew(SBorder)
                                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                                .Padding(5)
                                [
                                    SNew(SScrollBox)
                                    + SScrollBox::Slot() [ SAssignNew(ResultDetails, SVerticalBox) ]
                                ]
                            ]
                        ]
                        + SWidgetSwitcher::Slot()
                        [
                            SAssignNew(ResultsLog, SMultiLineEditableTextBox)
                            .IsReadOnly(true)
                            .AllowContextMenu(true)
                            .AutoWrapText(false)
                            .Text(LOCTEXT("ResultsPlaceholder", "Run a test to see its results here."))
                        ]
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(8, 5, 8, 5)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(this, &SProbeWorkspace::FeedbackText).AutoWrapText(true)
                  .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(this, &SProbeWorkspace::RunInfoText)
                  .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
            ]
        ];
        RebuildTestEditor();
        BuildResultTabs();
        ShowResults();
    }

    virtual ~SProbeWorkspace() override
    {
        FEditorDelegates::PostUndoRedo.RemoveAll(this);
    }

    virtual bool SupportsKeyboardFocus() const override { return true; }

    virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
    {
        if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::S && CanSaveSelected())
        {
            SaveSelected();
            return FReply::Handled();
        }
        return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
    }

public:
    void SelectTestByPath(const FString& AssetPath)
    {
        RefreshAssets();
        for (const TSharedPtr<FAssetData>& Entry : Assets)
        {
            if (Entry.IsValid() && Entry->GetObjectPathString() == AssetPath)
            {
                if (TestSelector.IsValid()) { TestSelector->SetSelectedItem(Entry); }
                else { Selected = Entry; RebuildTestEditor(); }
                bResultsFocused = false;
                return;
            }
        }
        FeedbackMessage = LOCTEXT("ContextMissingTest", "Selected Probe Test was not found. Refresh the test list.");
    }

    void RunAssetPaths(const TArray<FString>& AssetPaths)
    {
        Results.Reset();
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        ExpandedDiagnosticTestPath.Empty();
        ExpandedDiagnosticIndices.Reset();
        for (const FString& Path : AssetPaths)
        {
            if (UProbeTestAsset* Test = LoadObject<UProbeTestAsset>(nullptr, *Path))
            {
                ExecuteAsset(Test);
            }
            else
            {
                Probe::FTestResult Missing;
                Missing.AssetPath = Path;
                Missing.Name = FPackageName::GetShortName(Path);
                Missing.Status = Probe::EResultStatus::Error;
                Missing.Detail = TEXT("Probe Test asset could not be loaded");
                Results.Add(MoveTemp(Missing));
            }
        }
        RecordRunCompleted();
        ShowResults();
        bResultsFocused = true;
    }

private:
    TArray<TSharedPtr<FAssetData>> Assets;
    TSharedPtr<FAssetData> Selected;
    TSharedPtr<SComboBox<TSharedPtr<FAssetData>>> TestSelector;
    TSharedPtr<SVerticalBox> EditorRows;
    TSharedPtr<SHorizontalBox> ResultSummary;
    TSharedPtr<SMultiLineEditableTextBox> ResultsLog;
    TSharedPtr<SVerticalBox> ResultList;
    TSharedPtr<SVerticalBox> ResultDetails;
    TSharedPtr<SWidgetSwitcher> ResultViewSwitcher;
    TSharedPtr<SHorizontalBox> ResultTabs;
    TArray<Probe::FTestResult> Results;
    enum class EResultFilter : uint8 { All, Passed, Failed, Skipped, Error };
    EResultFilter ActiveResultFilter = EResultFilter::All;
    int32 SelectedResultIndex = INDEX_NONE;
    bool bProblemsFirst = true;
    bool bShowLog = false;
    bool bResultsFocused = false;
    float DefinitionFraction = 0.64f;
    TSet<int32> ExpandedDiagnosticIndices;
    FString ExpandedDiagnosticTestPath;
    TSet<int32> ExpandedStepIndices;
    FString ExpandedAssetPath;
    bool bStepNotesShown = false;
    FDateTime LastRunTime;
    double LastRunDurationMs = 0.0;

    void OnDefinitionResized(float NewFraction)
    {
        if (!bResultsFocused)
        {
            DefinitionFraction = FMath::Clamp(NewFraction, 0.20f, 0.85f);
            GConfig->SetFloat(TEXT("ProbeWorkspace"), TEXT("DefinitionFraction"), DefinitionFraction, GEditorPerProjectIni);
        }
    }

    FReply ToggleResultsFocus()
    {
        bResultsFocused = !bResultsFocused;
        return FReply::Handled();
    }

    bool HasResults() const { return !Results.IsEmpty(); }
    bool HasProblemResults() const
    {
        for (const Probe::FTestResult& Result : Results)
        {
            if (Result.Status == Probe::EResultStatus::Failed || Result.Status == Probe::EResultStatus::Error)
            {
                return true;
            }
        }
        return false;
    }
    FText FeedbackMessage = LOCTEXT("InitialHint", "Edit the test below, then Save Asset and Run Selected.");

    UProbeTestAsset* SelectedAsset() const
    {
        return Selected.IsValid() ? Cast<UProbeTestAsset>(Selected->GetAsset()) : nullptr;
    }

    bool HasSelectedAsset() const { return SelectedAsset() != nullptr; }

    bool CanSaveSelected() const
    {
        const UProbeTestAsset* Asset = SelectedAsset();
        return Asset && Asset->GetOutermost() && Asset->GetOutermost()->IsDirty();
    }

    FText DirtyStatusText() const
    {
        return CanSaveSelected() ? LOCTEXT("Unsaved", "* Unsaved changes") : FText::GetEmpty();
    }

    FText FeedbackText() const { return FeedbackMessage; }

    void RefreshAssets()
    {
        const FString PreviousPath = Selected.IsValid() ? Selected->GetObjectPathString() : FString();
        Assets.Reset();
        Selected.Reset();
        FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
        FARFilter Filter;
        Filter.ClassPaths.Add(UProbeTestAsset::StaticClass()->GetClassPathName());
        Filter.bRecursiveClasses = true;
        TArray<FAssetData> Found;
        Registry.Get().GetAssets(Filter, Found);
        Found.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
        for (const FAssetData& Asset : Found)
        {
            TSharedPtr<FAssetData> Entry = MakeShared<FAssetData>(Asset);
            Assets.Add(Entry);
            if (Asset.GetObjectPathString() == PreviousPath) { Selected = Entry; }
        }
        if (!Selected.IsValid() && Assets.Num()) { Selected = Assets[0]; }
        if (TestSelector.IsValid())
        {
            TestSelector->RefreshOptions();
            TestSelector->SetSelectedItem(Selected);
        }
        RebuildTestEditor();
    }


    // Probe-specific authoring UI. The underlying UProbeTestAsset remains an ordinary
    // Data Asset; no on-disk schema changes are required to use this editor.
    enum class EStepTextField : uint8 { Description, Target, ExpectedClass, PropertyName, ExpectedValue };

    static const TCHAR* StepName(EProbeStepType Type)
    {
        return Probe::GetAssertionInfo(Type).Name;
    }

    static const TCHAR* TargetHint(EProbeStepType Type)
    {
        switch (Type)
        {
        case EProbeStepType::PluginEnabled: return TEXT("Plugin identifier, such as Probe");
        case EProbeStepType::ClassExists: return TEXT("Class name or path, e.g. /Script/Engine.Actor");
        case EProbeStepType::AssetExists: return TEXT("Asset object path, e.g. /Game/ProbeTests/Probe_Smoke.Probe_Smoke");
        case EProbeStepType::ActorClassCountAtLeast: return TEXT("Actor class path, e.g. /Script/Engine.Actor");
        case EProbeStepType::ActorTagExists: return TEXT("Actor tag to find in the current level");
        case EProbeStepType::ActorLabelExists: return TEXT("Exact World Outliner label, case-sensitive");
        case EProbeStepType::AssetClassIs: return TEXT("Asset object path to inspect");
        case EProbeStepType::ObjectPropertyEquals: return TEXT("Object or asset path containing the property");
        case EProbeStepType::ModuleLoaded:
        case EProbeStepType::ModuleNotLoaded:
        case EProbeStepType::ModuleExists: return TEXT("Unreal module ID (use Browse to search actual module names)");
        case EProbeStepType::EditorTabOpen:
        case EProbeStepType::EditorTabNotOpen:
        case EProbeStepType::EditorTabRegistered: return TEXT("Internal global tab ID (use Browse; local tabs may not appear)");
        default: return Probe::GetAssertionInfo(Type).Description;
        }
    }

    template <typename EditFn>
    void ChangeAsset(TWeakObjectPtr<UProbeTestAsset> TestRef, const FText& TransactionLabel, EditFn&& Edit)
    {
        UProbeTestAsset* Asset = TestRef.Get();
        if (!Asset) { return; }
        const FScopedTransaction Transaction(TransactionLabel);
        Asset->Modify();
        Edit(*Asset);
        Asset->MarkPackageDirty();
    }

    template <typename EditFn>
    void ChangeStep(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index, const FText& TransactionLabel, EditFn&& Edit, bool bRebuild = false)
    {
        UProbeTestAsset* Asset = TestRef.Get();
        if (!Asset || !Asset->Steps.IsValidIndex(Index)) { return; }
        {
            const FScopedTransaction Transaction(TransactionLabel);
            Asset->Modify();
            Edit(Asset->Steps[Index]);
            Asset->MarkPackageDirty();
        }
        if (bRebuild && SelectedAsset() == Asset) { RebuildTestEditor(); }
    }

    void HandleUndoRedo()
    {
        RebuildTestEditor();
    }

    static FString StepFieldValue(const FProbeTestStep& Step, EStepTextField Field)
    {
        switch (Field)
        {
        case EStepTextField::Description: return Step.Description;
        case EStepTextField::Target: return Step.Target;
        case EStepTextField::ExpectedClass: return Step.ExpectedClass;
        case EStepTextField::PropertyName: return Step.PropertyName;
        case EStepTextField::ExpectedValue: return Step.ExpectedValue;
        }
        return FString();
    }

    // Consistent two-column rows mirror the UE Details panel rather than giving
    // every editable value the entire window width.
    TSharedRef<SWidget> BuildIdentifierBrowser(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index, EStepTextField Field)
    {
        const UProbeTestAsset* Asset = TestRef.Get();
        if (!Asset || !Asset->Steps.IsValidIndex(Index)) { return SNullWidget::NullWidget; }
        const EProbeStepType Type = Asset->Steps[Index].Type;
        const bool bModule = Type == EProbeStepType::ModuleLoaded || Type == EProbeStepType::ModuleNotLoaded
            || Type == EProbeStepType::ModuleExists || (Type == EProbeStepType::PluginDeclaresModule && Field == EStepTextField::ExpectedValue);
        const bool bClass = Field == EStepTextField::ExpectedClass || (Field == EStepTextField::Target
            && (FString(Probe::GetAssertionInfo(Type).Category) == TEXT("Classes & Reflection")
                || Type == EProbeStepType::ActorClassCountAtLeast || Type == EProbeStepType::ActorClassCountExactly
                || Type == EProbeStepType::ActorClassCountAtMost || Type == EProbeStepType::SelectedActorIsClass
                || Type == EProbeStepType::SelectedActorIsNotClass));
        const bool bPlugin = Field == EStepTextField::Target
            && FString(Probe::GetAssertionInfo(Type).Category) == TEXT("Plugins & Modules") && !bModule;
        const bool bActor = Field == EStepTextField::Target
            && (Type == EProbeStepType::ActorLabelExists || Type == EProbeStepType::ActorLabelNotExists
                || Type == EProbeStepType::ActorHasComponent || Type == EProbeStepType::ActorFolderEquals
                || Type == EProbeStepType::ActorPropertyEquals || Type == EProbeStepType::ActorIsSelected
                || Type == EProbeStepType::ActorIsNotSelected || Type == EProbeStepType::ActorLabelCountExactly
                || Type == EProbeStepType::ActorLabelCountAtLeast || Type == EProbeStepType::ActorHasTagByLabel
                || Type == EProbeStepType::ActorDoesNotHaveTagByLabel);
        TMap<FString, FProbeIdentifierChoice> Entries;
        if (bClass)
        {
            for (TObjectIterator<UClass> It; It; ++It)
            {
                if (It->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists)) { continue; }
                const FString Id = It->GetPathName();
                FProbeIdentifierChoice Choice;
                Choice.Id = Id; Choice.Display = It->GetName();
                Choice.Owner = It->GetOuter() ? It->GetOuter()->GetPathName() : TEXT("Unreal Class");
                Choice.State = TEXT("Class");
                Entries.Add(Id, MoveTemp(Choice));
            }
        }
        else if (bPlugin)
        {
            for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetDiscoveredPlugins())
            {
                FProbeIdentifierChoice Choice;
                Choice.Id = Plugin->GetName();
                Choice.Display = Plugin->GetDescriptor().FriendlyName;
                if (Choice.Display.IsEmpty()) { Choice.Display = Choice.Id; }
                Choice.Owner = Plugin->GetDescriptor().Category;
                Choice.State = Plugin->IsEnabled() ? TEXT("Enabled") : TEXT("Disabled");
                Entries.Add(Choice.Id, MoveTemp(Choice));
            }
        }
        else if (bActor)
        {
            if (GEditor)
            {
                if (UWorld* World = GEditor->GetEditorWorldContext().World())
                {
                    for (TActorIterator<AActor> It(World); It; ++It)
                    {
                        FProbeIdentifierChoice Choice;
                        Choice.Id = It->GetActorLabel();
                        Choice.Display = Choice.Id;
                        Choice.Owner = It->GetFolderPath().ToString();
                        Choice.State = It->IsSelectedInEditor() ? TEXT("Selected") : TEXT("Actor");
                        Entries.Add(Choice.Id, MoveTemp(Choice));
                    }
                }
            }
        }
        else if (bModule)
        {
            // Discover ownership from descriptors even for unloaded modules.
            for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetDiscoveredPlugins())
            {
                for (const FModuleDescriptor& Module : Plugin->GetDescriptor().Modules)
                {
                    const FString Id = Module.Name.ToString();
                    FProbeIdentifierChoice Choice;
                    Choice.Id = Id;
                    Choice.Display = Id;
                    Choice.Owner = Plugin->GetName() + TEXT(" plugin");
                    Choice.State = FModuleManager::Get().IsModuleLoaded(FName(*Id)) ? TEXT("Loaded") : TEXT("Not loaded");
                    Entries.Add(Id, Choice);
                }
            }
            TArray<FModuleStatus> Statuses;
            FModuleManager::Get().QueryModules(Statuses);
            for (const FModuleStatus& Status : Statuses)
            {
                if (Entries.Contains(Status.Name)) { continue; }
                FProbeIdentifierChoice Choice;
                Choice.Id = Status.Name;
                Choice.Display = Status.Name;
                Choice.Owner = TEXT("Engine / Project module");
                Choice.State = Status.bIsLoaded ? TEXT("Loaded") : TEXT("Not loaded");
                Entries.Add(Status.Name, Choice);
            }
        }
        else
        {
            for (const TWeakPtr<FTabSpawnerEntry>& WeakSpawner : FGlobalTabmanager::Get()->CollectSpawners())
            {
                if (const TSharedPtr<FTabSpawnerEntry> Spawner = WeakSpawner.Pin())
                {
                    const FString Id = Spawner->GetTabType().ToString();
                    FProbeIdentifierChoice Choice;
                    Choice.Id = Id;
                    Choice.Display = Spawner->GetDisplayName().ToString();
                    if (Choice.Display.IsEmpty()) { Choice.Display = Id; }
                    Choice.Owner = TEXT("Global Editor tab");
                    Choice.State = FGlobalTabmanager::Get()->FindExistingLiveTab(FTabId(FName(*Id))).IsValid()
                        ? TEXT("Open") : TEXT("Closed");
                    Entries.Add(Id, Choice);
                }
            }
            if (!Entries.Contains(Probe::TabId.ToString()))
            {
                Entries.Add(Probe::TabId.ToString(), FProbeIdentifierChoice{ Probe::TabId.ToString(), TEXT("Probe"), TEXT("Probe plugin"), TEXT("Open") });
            }
        }
        TArray<FProbeIdentifierChoice> Choices;
        Entries.GenerateValueArray(Choices);
        Choices.Sort([](const FProbeIdentifierChoice& A, const FProbeIdentifierChoice& B)
        {
            if (A.Owner != B.Owner) { return A.Owner < B.Owner; }
            return A.Display < B.Display;
        });
        return SNew(SProbeIdentifierPicker)
            .Candidates(Choices)
            .SearchHint(bClass ? LOCTEXT("ClassSearchHint", "Search reflected class name or package...")
                : bPlugin ? LOCTEXT("PluginSearchHint", "Search plugin name, category or enabled state...")
                : bActor ? LOCTEXT("ActorSearchHint", "Search Outliner actor label or folder...")
                : bModule ? LOCTEXT("ModuleSearchHint", "Search plugin, module name, or status...")
                : LOCTEXT("TabSearchHint", "Search visible tab name or internal ID..."))
            .OnPicked(FOnProbeIdentifierPicked::CreateLambda([this, TestRef, Index, Field](const FString& Id)
            {
                ChangeStep(TestRef, Index, LOCTEXT("BrowseIdentifierTransaction", "Choose Probe Assertion Identifier"),
                    [Id, Field](FProbeTestStep& Step)
                    {
                        if (Field == EStepTextField::ExpectedValue) { Step.ExpectedValue = Id; }
                        else if (Field == EStepTextField::ExpectedClass) { Step.ExpectedClass = Id; }
                        else { Step.Target = Id; }
                    }, true);
            }));
    }

    void AddStringField(const TSharedRef<SVerticalBox>& Body, TWeakObjectPtr<UProbeTestAsset> TestRef,
        int32 Index, EStepTextField Field, const FText& Label, const FString& Hint = FString())
    {
        Body->AddSlot().AutoHeight().Padding(2, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SBox).WidthOverride(134.0f)
              [ SNew(STextBlock).Text(Label).ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [
                SNew(SEditableTextBox)
                .SelectAllTextWhenFocused(false)
                .HintText(FText::FromString(Hint))
                .Text_Lambda([TestRef, Index, Field]() -> FText
                {
                    const UProbeTestAsset* Asset = TestRef.Get();
                    return Asset && Asset->Steps.IsValidIndex(Index)
                        ? FText::FromString(StepFieldValue(Asset->Steps[Index], Field)) : FText::GetEmpty();
                })
                .OnTextCommitted_Lambda([this, TestRef, Index, Field](const FText& NewText, ETextCommit::Type)
                {
                    const UProbeTestAsset* Test = TestRef.Get();
                    if (!Test || !Test->Steps.IsValidIndex(Index)
                        || StepFieldValue(Test->Steps[Index], Field) == NewText.ToString()) { return; }
                    ChangeStep(TestRef, Index, LOCTEXT("EditStepField", "Edit Probe Test Step"),
                        [Field, Value = NewText.ToString()](FProbeTestStep& Step)
                        {
                            switch (Field)
                            {
                            case EStepTextField::Description: Step.Description = Value; break;
                            case EStepTextField::Target: Step.Target = Value; break;
                            case EStepTextField::ExpectedClass: Step.ExpectedClass = Value; break;
                            case EStepTextField::PropertyName: Step.PropertyName = Value; break;
                            case EStepTextField::ExpectedValue: Step.ExpectedValue = Value; break;
                            }
                        });
                })
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
            [
                SNew(SComboButton)
                .Visibility_Lambda([TestRef, Index, Field]()
                {
                    const UProbeTestAsset* Asset = TestRef.Get();
                    if (!Asset || !Asset->Steps.IsValidIndex(Index)) { return EVisibility::Collapsed; }
                    const EProbeStepType Type = Asset->Steps[Index].Type;
                    const bool bPluginModuleValue = Field == EStepTextField::ExpectedValue
                        && Type == EProbeStepType::PluginDeclaresModule;
                    if (Field == EStepTextField::ExpectedClass) { return EVisibility::Visible; }
                    if (Field != EStepTextField::Target && !bPluginModuleValue) { return EVisibility::Collapsed; }
                    if (bPluginModuleValue) { return EVisibility::Visible; }
                    const bool bModule = Type == EProbeStepType::ModuleLoaded || Type == EProbeStepType::ModuleNotLoaded
                        || Type == EProbeStepType::ModuleExists;
                    const bool bTab = Type == EProbeStepType::EditorTabOpen || Type == EProbeStepType::EditorTabNotOpen
                        || Type == EProbeStepType::EditorTabRegistered;
                    const bool bClass = Type == EProbeStepType::FixtureCreateObject
                        || FString(Probe::GetAssertionInfo(Type).Category) == TEXT("Classes & Reflection")
                        || Type == EProbeStepType::ActorClassCountAtLeast || Type == EProbeStepType::ActorClassCountExactly
                        || Type == EProbeStepType::ActorClassCountAtMost || Type == EProbeStepType::SelectedActorIsClass
                        || Type == EProbeStepType::SelectedActorIsNotClass;
                    const bool bPlugin = Type != EProbeStepType::PluginDeclaresModule
                        && FString(Probe::GetAssertionInfo(Type).Category) == TEXT("Plugins & Modules")
                        && !bModule;
                    const bool bDeclaredPlugin = Type == EProbeStepType::PluginDeclaresModule;
                    const bool bActor = Type == EProbeStepType::ActorLabelExists || Type == EProbeStepType::ActorLabelNotExists
                        || Type == EProbeStepType::ActorHasComponent || Type == EProbeStepType::ActorFolderEquals
                        || Type == EProbeStepType::ActorPropertyEquals || Type == EProbeStepType::ActorIsSelected
                        || Type == EProbeStepType::ActorIsNotSelected || Type == EProbeStepType::ActorLabelCountExactly
                        || Type == EProbeStepType::ActorLabelCountAtLeast || Type == EProbeStepType::ActorHasTagByLabel
                        || Type == EProbeStepType::ActorDoesNotHaveTagByLabel;
                    return bModule || bTab || bClass || bPlugin || bDeclaredPlugin || bActor
                        ? EVisibility::Visible : EVisibility::Collapsed;
                })
                .ToolTipText(LOCTEXT("BrowseIdentifierTip", "Search real registered identifiers, classes, plugins or Outliner actors. Internal IDs are inserted automatically."))
                .OnGetMenuContent_Lambda([this, TestRef, Index, Field]() { return BuildIdentifierBrowser(TestRef, Index, Field); })
                .ButtonContent()
                [ SNew(STextBlock).Text(LOCTEXT("BrowseIdentifier", "Browse...")) ]
            ]
        ];
    }

    FReply UseSelectedActorLabel(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        if (!GEditor || GEditor->GetSelectedActorCount() != 1)
        {
            FeedbackMessage = LOCTEXT("SelectExactlyOneActor", "Select exactly one level actor in the World Outliner, then try again.");
            return FReply::Handled();
        }
        UWorld* World = GEditor->GetEditorWorldContext().World();
        if (!World)
        {
            FeedbackMessage = LOCTEXT("NoWorldForCapture", "No Editor world is available for actor capture.");
            return FReply::Handled();
        }
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (It->IsSelectedInEditor())
            {
                const FString Label = It->GetActorLabel();
                ChangeStep(TestRef, Index, LOCTEXT("CaptureActorLabel", "Capture Selected Actor Label"),
                    [Label](FProbeTestStep& Step) { Step.Target = Label; }, true);
                FeedbackMessage = FText::Format(LOCTEXT("CapturedActor", "Using actor label: {0}"), FText::FromString(Label));
                return FReply::Handled();
            }
        }
        FeedbackMessage = LOCTEXT("SelectedActorNotFound", "Selected actor was not found in the active Editor world.");
        return FReply::Handled();
    }

    FReply UseSelectedContentBrowserAsset(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        TArray<FAssetData> SelectedAssets;
        FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"))
            .Get().GetSelectedAssets(SelectedAssets);
        if (SelectedAssets.Num() != 1)
        {
            FeedbackMessage = LOCTEXT("PickExactlyOneAsset", "Select exactly one asset in the Content Browser, then try again.");
            return FReply::Handled();
        }
        const FString Path = SelectedAssets[0].GetObjectPathString();
        ChangeStep(TestRef, Index, LOCTEXT("CaptureContentBrowserAsset", "Capture Selected Content Browser Asset"),
            [Path](FProbeTestStep& Step) { Step.Target = Path; }, true);
        FeedbackMessage = FText::FromString(TEXT("Using asset: ") + Path);
        return FReply::Handled();
    }

    void AddActorCountField(const TSharedRef<SVerticalBox>& Body, TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        Body->AddSlot().AutoHeight().Padding(2, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SBox).WidthOverride(134.0f)
              [ SNew(STextBlock).Text_Lambda([TestRef, Index]()
                {
                    const UProbeTestAsset* Test = TestRef.Get();
                    if (!Test || !Test->Steps.IsValidIndex(Index)) { return LOCTEXT("ActorCountLabel", "Count"); }
                    const EProbeStepType Type = Test->Steps[Index].Type;
                    if (Type == EProbeStepType::AssetCountAtLeast) { return LOCTEXT("MinAssetsLabel", "Minimum Assets"); }
                    if (Type == EProbeStepType::AssetCountExactly) { return LOCTEXT("ExactAssetsLabel", "Exact Assets"); }
                    if (Type == EProbeStepType::AssetCountAtMost) { return LOCTEXT("MaxAssetsLabel", "Maximum Assets"); }
                    if (Type == EProbeStepType::PluginModuleCountExactly) { return LOCTEXT("ExactModulesLabel", "Exact Modules"); }
                    if (Type == EProbeStepType::PluginModuleCountAtMost) { return LOCTEXT("MaxModulesLabel", "Maximum Modules"); }
                    if (Type == EProbeStepType::LoadedModuleCountAtMost) { return LOCTEXT("LoadedModulesMaximum", "Maximum Modules"); }
                    if (Type == EProbeStepType::ActorFolderCountAtLeast || Type == EProbeStepType::ActorLabelCountAtLeast) { return LOCTEXT("MinMatchingActors", "Minimum Matching"); }
                    if (Type == EProbeStepType::ActorCountWithTagExactly) { return LOCTEXT("ExactMatchingTagCount", "Exact Matching"); }
                    if (Type == EProbeStepType::ActorCountAtMost || Type == EProbeStepType::ActorClassCountAtMost) { return LOCTEXT("MaxActorsLabel", "Maximum Actors"); }
                    if (Type == EProbeStepType::SelectedActorCountAtMost) { return LOCTEXT("MaxSelectedLabel", "Maximum Selected"); }
                    if (Type == EProbeStepType::ActorLabelCountExactly) { return LOCTEXT("ExactMatchingActors", "Exact Label Count"); }
                    if (Type == EProbeStepType::ActorCountExactly || Type == EProbeStepType::ActorClassCountExactly
                        || Type == EProbeStepType::SelectedActorCountExactly) { return LOCTEXT("ExactCountLabel", "Exact Count"); }
                    return Type == EProbeStepType::SelectedActorCountAtLeast
                        ? LOCTEXT("MinSelectionLabel", "Minimum Selected")
                        : LOCTEXT("MinActorCountLabel", "Minimum Actors");
                })
                .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [
                SNew(SNumericEntryBox<int32>)
                .AllowSpin(true).MinValue(0)
                .Value_Lambda([TestRef, Index]() -> TOptional<int32>
                {
                    const UProbeTestAsset* Asset = TestRef.Get();
                    return Asset && Asset->Steps.IsValidIndex(Index)
                        ? TOptional<int32>(Asset->Steps[Index].MinimumActorCount) : TOptional<int32>();
                })
                .OnValueCommitted_Lambda([this, TestRef, Index](int32 NewValue, ETextCommit::Type)
                {
                    const UProbeTestAsset* Test = TestRef.Get();
                    if (!Test || !Test->Steps.IsValidIndex(Index)
                        || Test->Steps[Index].MinimumActorCount == FMath::Max(0, NewValue)) { return; }
                    ChangeStep(TestRef, Index, LOCTEXT("ActorCountEdit", "Edit Minimum Actor Count"),
                        [NewValue](FProbeTestStep& Step) { Step.MinimumActorCount = FMath::Max(0, NewValue); });
                })
            ]
        ];
    }

    void AddToleranceField(const TSharedRef<SVerticalBox>& Body, TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        Body->AddSlot().AutoHeight().Padding(2, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SBox).WidthOverride(134.0f)
              [ SNew(STextBlock).Text(LOCTEXT("ToleranceLabel", "Numeric Tolerance"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [
                SNew(SNumericEntryBox<double>)
                .MinValue(0.0)
                .Value_Lambda([TestRef, Index]() -> TOptional<double>
                {
                    const UProbeTestAsset* Asset = TestRef.Get();
                    return Asset && Asset->Steps.IsValidIndex(Index)
                        ? TOptional<double>(Asset->Steps[Index].NumericTolerance) : TOptional<double>();
                })
                .OnValueCommitted_Lambda([this, TestRef, Index](double NewValue, ETextCommit::Type)
                {
                    const UProbeTestAsset* Test = TestRef.Get();
                    if (!Test || !Test->Steps.IsValidIndex(Index)
                        || Test->Steps[Index].NumericTolerance == FMath::Max(0.0, NewValue)) { return; }
                    ChangeStep(TestRef, Index, LOCTEXT("ToleranceEdit", "Edit Numeric Tolerance"),
                        [NewValue](FProbeTestStep& Step) { Step.NumericTolerance = FMath::Max(0.0, NewValue); });
                })
            ]
        ];
    }

    FString StepSummary(const FProbeTestStep& Step) const
    {
        const uint8 Fields = Probe::StepFields(Step);
        if (Fields == 0) { return Probe::StepHelp(Step); }
        FString Summary;
        if (Fields & Probe::TargetField)
        {
            Summary = Step.Target.IsEmpty() ? TEXT("Target not configured") : Step.Target;
        }
        if (Fields & Probe::ClassField)
        {
            Summary += FString::Printf(TEXT("  |  class: %s"), *Step.ExpectedClass);
        }
        if (Fields & Probe::PropertyField)
        {
            Summary += FString::Printf(TEXT("  |  %s"), *Step.PropertyName);
        }
        if (Fields & Probe::ValueField)
        {
            Summary += FString::Printf(TEXT(" = %s"), *Step.ExpectedValue);
        }
        if (Fields & Probe::CountField)
        {
            const bool bExact = Step.Type == EProbeStepType::ActorCountExactly
                || Step.Type == EProbeStepType::ActorClassCountExactly
                || Step.Type == EProbeStepType::SelectedActorCountExactly
                || Step.Type == EProbeStepType::AssetCountExactly
                || Step.Type == EProbeStepType::AssetCountDirectExactly
                || Step.Type == EProbeStepType::LoadedModuleCountExactly
                || Step.Type == EProbeStepType::ActorFolderCountExactly
                || Step.Type == EProbeStepType::ActorLabelCountExactly;
            const bool bMaximum = Step.Type == EProbeStepType::ActorCountAtMost
                || Step.Type == EProbeStepType::ActorClassCountAtMost
                || Step.Type == EProbeStepType::SelectedActorCountAtMost
                || Step.Type == EProbeStepType::ActorFolderCountAtMost
                || Step.Type == EProbeStepType::ActorCountWithTagAtMost
                || Step.Type == EProbeStepType::ActorLabelCountAtMost;
            Summary += FString::Printf(TEXT("  |  %s %d"), bExact ? TEXT("exactly") : bMaximum ? TEXT("at most") : TEXT("at least"), Step.MinimumActorCount);
        }
        return Summary.IsEmpty() ? Probe::StepHelp(Step) : Summary;
    }

    FReply ToggleStepExpanded(int32 Index)
    {
        if (ExpandedStepIndices.Contains(Index)) { ExpandedStepIndices.Remove(Index); }
        else { ExpandedStepIndices.Add(Index); }
        RebuildTestEditor();
        return FReply::Handled();
    }

    FReply ToggleStepNotes()
    {
        bStepNotesShown = !bStepNotesShown;
        RebuildTestEditor();
        return FReply::Handled();
    }

    void SetStepType(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index, EProbeStepType Type)
    {
        const UProbeTestAsset* Asset = TestRef.Get();
        if (!Asset || !Asset->Steps.IsValidIndex(Index)
            || (Asset->Steps[Index].Type == Type && Asset->Steps[Index].AssertionId.IsNone())) { return; }
        ChangeStep(TestRef, Index, LOCTEXT("StepTypeEdit", "Change Probe Test Step Type"),
            [Type](FProbeTestStep& Step) { Step.Type = Type; Step.AssertionId = NAME_None; }, true);
    }

    void SetExtensionStepType(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index, FName Id)
    {
        if (!Probe::FindExtensionAssertion(Id)) { return; }
        ChangeStep(TestRef, Index, LOCTEXT("ExtensionStepTypeEdit", "Change Probe Extension Assertion"),
            [Id](FProbeTestStep& Step) { Step.AssertionId = Id; }, true);
    }

    // Shared categorized submenus keep the growing assertion catalog navigable.
    TSharedRef<SWidget> BuildStepTypeMenu(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        FMenuBuilder Menu(true, nullptr);
        for (const FString& Category : Probe::AllCategories())
        {
            Menu.AddSubMenu(FText::FromString(Category), FText::FromString(Category + TEXT(" assertions")),
                FNewMenuDelegate::CreateLambda([this, TestRef, Index, Category](FMenuBuilder& SubMenu)
                {
                    for (const Probe::FAssertionInfo& Info : Probe::AssertionCatalog())
                    {
                        if (Category != Info.Category) { continue; }
                        SubMenu.AddMenuEntry(FText::FromString(Info.Name), FText::FromString(FString(Info.Description) + TEXT(". Review and mark Ready before Run All.")), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::SetStepType, TestRef, Index, Info.Type)));
                    }
                    for (const FName Id : Probe::GetExtensionAssertionIds())
                    {
                        const Probe::FExtensionAssertion* Info = Probe::FindExtensionAssertion(Id);
                        if (!Info || Category != Info->Category) { continue; }
                        SubMenu.AddMenuEntry(FText::FromString(Info->Name), FText::FromString(Info->Description + TEXT(". Configure this assertion before automatic execution.")), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::SetExtensionStepType, TestRef, Index, Id)));
                    }
                }));
        }
        return Menu.MakeWidget();
    }

    FReply ReorderStep(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index, int32 Direction)
    {
        UProbeTestAsset* Asset = TestRef.Get();
        if (!Asset || !Asset->Steps.IsValidIndex(Index) || !Asset->Steps.IsValidIndex(Index + Direction))
        {
            return FReply::Handled();
        }
        ChangeAsset(TestRef, LOCTEXT("MoveStepTransaction", "Move Probe Test Step"),
            [Index, Direction](UProbeTestAsset& Test) { Test.Steps.Swap(Index, Index + Direction); });
        if (ExpandedStepIndices.Contains(Index))
        {
            ExpandedStepIndices.Remove(Index);
            ExpandedStepIndices.Add(Index + Direction);
        }
        RebuildTestEditor();
        return FReply::Handled();
    }

    FReply DuplicateStep(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        UProbeTestAsset* Asset = TestRef.Get();
        if (!Asset || !Asset->Steps.IsValidIndex(Index)) { return FReply::Handled(); }
        ChangeAsset(TestRef, LOCTEXT("DuplicateStepTransaction", "Duplicate Probe Test Step"),
            [Index](UProbeTestAsset& Test) { Test.Steps.Insert(Test.Steps[Index], Index + 1); });
        ExpandedStepIndices.Add(Index + 1);
        RebuildTestEditor();
        return FReply::Handled();
    }

    FReply DeleteStep(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        UProbeTestAsset* Asset = TestRef.Get();
        if (!Asset || !Asset->Steps.IsValidIndex(Index)) { return FReply::Handled(); }
        ChangeAsset(TestRef, LOCTEXT("DeleteStepTransaction", "Remove Probe Test Step"),
            [Index](UProbeTestAsset& Test) { Test.Steps.RemoveAt(Index); });
        RebuildTestEditor();
        return FReply::Handled();
    }

    TSharedRef<SWidget> MakeStepCard(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        UProbeTestAsset* Asset = TestRef.Get();
        check(Asset && Asset->Steps.IsValidIndex(Index));
        const FProbeTestStep& Step = Asset->Steps[Index];
        const EProbeStepType Type = Step.Type;
        const int32 StepCount = Asset->Steps.Num();
        const bool bExpanded = ExpandedStepIndices.Contains(Index);
        TSharedRef<SVerticalBox> Card = SNew(SVerticalBox);
        Card->AddSlot().AutoHeight()
        [
            SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(FMargin(5, 3))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
                [
                    SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .ToolTipText(bExpanded ? LOCTEXT("CollapseStep", "Collapse step") : LOCTEXT("ExpandStep", "Expand step"))
                    .OnClicked_Lambda([this, Index]() { return ToggleStepExpanded(Index); })
                    [ SNew(SImage)
                      .Image(FSlateStyleRegistry::FindSlateStyle(TEXT("ProbeAssetAppearance"))
                          ? FSlateStyleRegistry::FindSlateStyle(TEXT("ProbeAssetAppearance"))->GetBrush(
                              bExpanded ? TEXT("Probe.DisclosureExpanded") : TEXT("Probe.DisclosureCollapsed"))
                          : FAppStyle::GetBrush(TEXT("Icons.ChevronRight"))) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                [ SNew(STextBlock).Text(FText::AsNumber(Index + 1)).ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SComboButton)
                        .ToolTipText(LOCTEXT("ChangeTypeTip", "Change assertion type"))
                        .OnGetMenuContent_Lambda([this, TestRef, Index]() { return BuildStepTypeMenu(TestRef, Index); })
                        .ButtonContent()
                        [ SNew(STextBlock).Text(FText::FromString(Probe::StepDisplayName(Step))) ]
                    ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text(FText::FromString(StepSummary(Step)))
                      .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                      .Visibility(bExpanded ? EVisibility::Collapsed : EVisibility::Visible)
                      .AutoWrapText(true) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 1, 0)
                [
                    SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .ToolTipText(LOCTEXT("MoveStepUp", "Move step up"))
                    .IsEnabled(Index > 0)
                    .OnClicked_Lambda([this, TestRef, Index]() { return ReorderStep(TestRef, Index, -1); })
                    [ SNew(STextBlock).Text(FText::FromString(TEXT("↑"))) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .ToolTipText(LOCTEXT("MoveStepDown", "Move step down"))
                    .IsEnabled(Index + 1 < StepCount)
                    .OnClicked_Lambda([this, TestRef, Index]() { return ReorderStep(TestRef, Index, +1); })
                    [ SNew(STextBlock).Text(FText::FromString(TEXT("↓"))) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .ToolTipText(LOCTEXT("DuplicateStepTip", "Duplicate this step"))
                    .OnClicked_Lambda([this, TestRef, Index]() { return DuplicateStep(TestRef, Index); })
                    [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Duplicate")) ]
                ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [
                    SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .ToolTipText(LOCTEXT("DeleteStepTip", "Remove this step"))
                    .OnClicked_Lambda([this, TestRef, Index]() { return DeleteStep(TestRef, Index); })
                    [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Delete")) ]
                ]
            ]
        ];
        const FString ConfigIssue = Probe::StepConfigurationIssue(Step);
        if (!ConfigIssue.IsEmpty())
        {
            Card->AddSlot().AutoHeight().Padding(30, 1, 5, 3)
            [ SNew(STextBlock).Text(FText::FromString(TEXT("Configure: ") + ConfigIssue))
              .ColorAndOpacity(FLinearColor(0.96f, 0.72f, 0.30f)) ];
        }
        if (bExpanded)
        {
            TSharedRef<SVerticalBox> Fields = SNew(SVerticalBox);
            const uint8 StepFieldMask = Probe::StepFields(Step);
            if (StepFieldMask & Probe::TargetField)
            {
                const FText Label = Type == EProbeStepType::FixtureCreateObject ? LOCTEXT("FixtureClassPath", "Object Class")
                    : Type == EProbeStepType::FixtureSetProperty || Type == EProbeStepType::FixturePropertyEquals
                    || Type == EProbeStepType::FixturePropertyNotEquals || Type == EProbeStepType::FixtureObjectExists
                    || Type == EProbeStepType::FixtureDestroyObject ? LOCTEXT("FixtureIdentifier", "Fixture ID")
                    : Type == EProbeStepType::AssetCountAtLeast || Type == EProbeStepType::AssetCountExactly
                    || Type == EProbeStepType::AssetCountAtMost || Type == EProbeStepType::AssetCountDirectAtLeast
                    || Type == EProbeStepType::AssetCountDirectExactly ? LOCTEXT("FolderPathLabel", "Folder Path")
                    : Type == EProbeStepType::EditorTabOpen || Type == EProbeStepType::EditorTabNotOpen
                      || Type == EProbeStepType::EditorTabRegistered ? LOCTEXT("TabIdLabel", "Tab ID")
                    : Type == EProbeStepType::ToolMenuRegistered || Type == EProbeStepType::ToolMenuSectionExists
                      || Type == EProbeStepType::ToolMenuSectionNotExists ? LOCTEXT("ToolMenuLabel", "Menu ID")
                    : Type == EProbeStepType::ModuleLoaded || Type == EProbeStepType::ModuleNotLoaded
                      || Type == EProbeStepType::ModuleExists ? LOCTEXT("ModuleNameLabel", "Module Name")
                    : Type == EProbeStepType::ConsoleVariableExists || Type == EProbeStepType::ConsoleVariableEquals
                      ? LOCTEXT("CVarNameLabel", "CVar Name")
                    : Type == EProbeStepType::ActorFolderCountExactly || Type == EProbeStepType::ActorFolderCountAtMost
                      ? LOCTEXT("OutlinerFolderTarget", "Outliner Folder")
                    : Type == EProbeStepType::ActorCountWithTagAtMost ? LOCTEXT("ActorTagTarget", "Actor Tag")
                    : Type == EProbeStepType::ActorHasComponent || Type == EProbeStepType::ActorFolderEquals
                      || Type == EProbeStepType::ActorPropertyEquals || Type == EProbeStepType::ActorIsSelected
                      || Type == EProbeStepType::ActorIsNotSelected || Type == EProbeStepType::ActorLabelCountExactly
                      || Type == EProbeStepType::ActorLabelCountAtLeast
                      ? LOCTEXT("ActorLabelTarget", "Actor Label")
                    : Step.AssertionId.IsNone() && FString(Probe::GetAssertionInfo(Type).Category) == TEXT("Plugins & Modules") ? LOCTEXT("TargetPluginLabel", "Plugin Name")
                    : Step.AssertionId.IsNone() && FString(Probe::GetAssertionInfo(Type).Category) == TEXT("Assets") ? LOCTEXT("TargetAssetPathLabel", "Asset Path")
                    : Step.AssertionId.IsNone() && FString(Probe::GetAssertionInfo(Type).Category) == TEXT("Classes & Reflection") ? LOCTEXT("TargetClassPathLabel", "Class Path")
                    : Step.AssertionId.IsNone() && FString(Probe::GetAssertionInfo(Type).Category) == TEXT("Selection") ? LOCTEXT("TargetSelectionLabel", "Match Text / Tag")
                    : Step.AssertionId.IsNone() && FString(Probe::GetAssertionInfo(Type).Category) == TEXT("Properties & Values") ? LOCTEXT("TargetObjectPathLabel", "Object Path")
                    : Step.AssertionId.IsNone() && FString(Probe::GetAssertionInfo(Type).Category) == TEXT("Actors & World") ? LOCTEXT("TargetActorLabel", "Actor Label / Tag")
                    : LOCTEXT("StepTarget", "Target");
                AddStringField(Fields, TestRef, Index, EStepTextField::Target, Label,
                    Step.AssertionId.IsNone() ? FString(TargetHint(Type)) : Probe::StepHelp(Step));
            }
            if (Type == EProbeStepType::ActorLabelExists || Type == EProbeStepType::ActorLabelNotExists
                || Type == EProbeStepType::ActorHasComponent || Type == EProbeStepType::ActorFolderEquals
                || Type == EProbeStepType::ActorPropertyEquals || Type == EProbeStepType::SelectedActorLabelEquals
                || Type == EProbeStepType::ActorIsSelected || Type == EProbeStepType::ActorIsNotSelected
                || Type == EProbeStepType::ActorLabelCountExactly)
            {
                Fields->AddSlot().AutoHeight().Padding(FMargin(136.0f, 1.0f, 2.0f, 3.0f))
                [
                    SNew(SButton)
                    .HAlign(HAlign_Center)
                    .ToolTipText(LOCTEXT("UseSelectedActorTip", "Read the selected actor's Outliner label into Target. Does not change the actor or level."))
                    .OnClicked_Lambda([this, TestRef, Index]() { return UseSelectedActorLabel(TestRef, Index); })
                    [ SNew(STextBlock).Text(LOCTEXT("UseSelectedActor", "Use Selected Actor")) ]
                ];
            }
            if ((StepFieldMask & Probe::TargetField) && Step.AssertionId.IsNone()
                && (FString(Probe::GetAssertionInfo(Type).Category) == TEXT("Properties & Values")
                    || (FString(Probe::GetAssertionInfo(Type).Category) == TEXT("Assets")
                        && Type != EProbeStepType::AssetCountAtLeast && Type != EProbeStepType::AssetCountExactly
                        && Type != EProbeStepType::AssetCountAtMost && Type != EProbeStepType::AssetNotExists
                        && Type != EProbeStepType::AssetCountDirectAtLeast && Type != EProbeStepType::AssetCountDirectExactly)))
            {
                Fields->AddSlot().AutoHeight().Padding(FMargin(136.0f, 1.0f, 2.0f, 3.0f))
                [ SNew(SButton).HAlign(HAlign_Center)
                  .ToolTipText(LOCTEXT("CaptureAssetPathTip", "Use the object path of the one asset selected in the Content Browser"))
                  .OnClicked_Lambda([this, TestRef, Index]() { return UseSelectedContentBrowserAsset(TestRef, Index); })
                  [ SNew(STextBlock).Text(LOCTEXT("CaptureAssetPath", "Use Selected Asset")) ] ];
            }
            if (StepFieldMask & Probe::CountField) { AddActorCountField(Fields, TestRef, Index); }
            if (StepFieldMask & Probe::ClassField)
            {
                AddStringField(Fields, TestRef, Index, EStepTextField::ExpectedClass,
                    Type == EProbeStepType::ActorHasComponent || Type == EProbeStepType::SelectedActorHasComponent
                      ? LOCTEXT("ComponentClassLabel", "Component Class")
                    : Type == EProbeStepType::ClassIsChildOf ? LOCTEXT("ParentClassLabel", "Parent Class")
                    : Type == EProbeStepType::ClassImplementsInterface ? LOCTEXT("InterfaceClassLabel", "Interface Class")
                    : LOCTEXT("StepClass", "Expected Class"), TEXT("/Script/Engine.Actor"));
            }
            if (StepFieldMask & Probe::PropertyField)
            {
                AddStringField(Fields, TestRef, Index, EStepTextField::PropertyName,
                    Type == EProbeStepType::ClassHasFunction || Type == EProbeStepType::ClassHasNoFunction
                    || Type == EProbeStepType::ClassFunctionBlueprintCallable ? LOCTEXT("FunctionNameLabel", "Function Name")
                    : Type == EProbeStepType::AssetRegistryTagEquals || Type == EProbeStepType::AssetRegistryTagExists
                      || Type == EProbeStepType::AssetRegistryTagValueContains
                    ? LOCTEXT("TagNameLabel", "Tag Name")
                    : LOCTEXT("StepProperty", "Property Name"));
            }
            if (StepFieldMask & Probe::ValueField)
            {
                AddStringField(Fields, TestRef, Index, EStepTextField::ExpectedValue,
                    Type == EProbeStepType::FixtureCreateObject ? LOCTEXT("NewFixtureIdentifier", "New Fixture ID")
                    : Type == EProbeStepType::FixtureSetProperty ? LOCTEXT("FixtureNewPropertyValue", "New Value")
                    : Type == EProbeStepType::ActorFolderEquals || Type == EProbeStepType::AssetPackagePathEquals
                      ? LOCTEXT("FolderValueLabel", "Expected Folder")
                    : Type == EProbeStepType::AssetClassNameEquals ? LOCTEXT("AssetClassNameExpected", "Asset Class Name")
                    : Type == EProbeStepType::PluginDeclaresModule ? LOCTEXT("DeclaredModuleLabel", "Module Name")
                    : Type == EProbeStepType::ObjectPropertyNumericInRange ? LOCTEXT("RangeMinMaxLabel", "Inclusive Range")
                    : Type == EProbeStepType::PluginDescriptionContains || Type == EProbeStepType::AssetNameContains
                      || Type == EProbeStepType::AssetNameStartsWith || Type == EProbeStepType::AssetNameEndsWith
                      || Type == EProbeStepType::AssetRegistryTagValueContains ? LOCTEXT("SubstringLabel", "Match Text")
                    : LOCTEXT("StepExpected", "Expected Value"));
            }
            if (StepFieldMask & Probe::ToleranceField) { AddToleranceField(Fields, TestRef, Index); }
            if (bStepNotesShown)
            {
                AddStringField(Fields, TestRef, Index, EStepTextField::Description,
                    LOCTEXT("StepDescription", "Description"), TEXT("Optional note describing the assertion"));
            }
            Card->AddSlot().AutoHeight().Padding(5, 2, 5, 5)
            [ Fields ];
        }
        return Card;
    }

    void RebuildTestEditor()
    {
        if (!EditorRows.IsValid()) { return; }
        EditorRows->ClearChildren();
        UProbeTestAsset* Asset = SelectedAsset();
        if (!Asset)
        {
            ExpandedAssetPath.Reset();
            ExpandedStepIndices.Reset();
            EditorRows->AddSlot().AutoHeight().Padding(10)
            [ SNew(STextBlock).Text(LOCTEXT("NoTestSelected", "Create or select a Probe Test to edit its configuration.")) ];
            return;
        }
        const FString AssetPath = Asset->GetPathName();
        if (ExpandedAssetPath != AssetPath)
        {
            ExpandedAssetPath = AssetPath;
            ExpandedStepIndices.Reset();
            if (Asset->Steps.Num() > 0) { ExpandedStepIndices.Add(0); }
        }
        const TWeakObjectPtr<UProbeTestAsset> TestRef(Asset);
        TSharedRef<SVerticalBox> Settings = SNew(SVerticalBox);
        Settings->AddSlot().AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 9, 0)
            [ SNew(SImage).Image(FAppStyle::GetBrush("ClassIcon.DataAsset")) ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(STextBlock).Text(FText::FromString(Asset->GetName())).Font(FAppStyle::GetFontStyle("NormalFontBold")) ]
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
                  .Text(LOCTEXT("TestAssetType", "Probe Test Asset")) ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SCheckBox)
                .IsChecked_Lambda([TestRef]()
                {
                    const UProbeTestAsset* Test = TestRef.Get();
                    return Test && Test->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                })
                .OnCheckStateChanged_Lambda([this, TestRef](ECheckBoxState NewState)
                {
                    ChangeAsset(TestRef, LOCTEXT("ToggleTestTransaction", "Toggle Probe Test"),
                        [NewState](UProbeTestAsset& Test) { Test.bEnabled = NewState == ECheckBoxState::Checked; });
                })
                [ SNew(STextBlock).Text(LOCTEXT("TestEnabled", "Enabled")) ]
            ]
        ];
        Settings->AddSlot().AutoHeight().Padding(0, 5, 0, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SCheckBox)
              .ToolTipText(LOCTEXT("NeedsConfigurationTip", "Hold template tests out of Run All until their targets, expectations, and required Editor conditions have been reviewed."))
              .IsChecked_Lambda([TestRef]()
              {
                  const UProbeTestAsset* Test = TestRef.Get();
                  return Test && Test->bNeedsConfiguration ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
              })
              .OnCheckStateChanged_Lambda([this, TestRef](ECheckBoxState State)
              {
                  const UProbeTestAsset* Test = TestRef.Get();
                  if (!Test) { return; }
                  if (State == ECheckBoxState::Unchecked)
                  {
                      const FString Issue = Probe::TestConfigurationIssue(*Test);
                      if (!Issue.IsEmpty())
                      {
                          FeedbackMessage = FText::FromString(TEXT("Cannot mark Ready: ") + Issue);
                          return;
                      }
                  }
                  ChangeAsset(TestRef, LOCTEXT("ReviewTestTransaction", "Change Probe Test Readiness"),
                      [State](UProbeTestAsset& Test) { Test.bNeedsConfiguration = State == ECheckBoxState::Checked; });
              })
              [ SNew(STextBlock).Text(LOCTEXT("NeedsConfigurationLabel", "Needs Configuration")) ] ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(12, 0, 0, 0)
            [ SNew(STextBlock)
              .Visibility_Lambda([TestRef]()
              {
                  const UProbeTestAsset* Test = TestRef.Get();
                  return Test && Test->bNeedsConfiguration ? EVisibility::Visible : EVisibility::Collapsed;
              })
              .Text(LOCTEXT("NeedsConfigurationNotice", "Review inputs, choose a meaningful expectation, then uncheck to run."))
              .AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
        ];
        Settings->AddSlot().AutoHeight().Padding(0, 7, 0, 0)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SBox).WidthOverride(102.0f)
              [ SNew(STextBlock).Text(LOCTEXT("TestDescription", "Description"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [
                SNew(SEditableTextBox)
                .HintText(LOCTEXT("DescriptionHint", "What does this test cover?"))
                .Text_Lambda([TestRef]()
                {
                    const UProbeTestAsset* Test = TestRef.Get();
                    return Test ? FText::FromString(Test->TestDescription) : FText::GetEmpty();
                })
                .OnTextCommitted_Lambda([this, TestRef](const FText& NewText, ETextCommit::Type)
                {
                    const UProbeTestAsset* Test = TestRef.Get();
                    if (!Test || Test->TestDescription == NewText.ToString()) { return; }
                    ChangeAsset(TestRef, LOCTEXT("DescriptionTransaction", "Edit Probe Test Description"),
                        [Value = NewText.ToString()](UProbeTestAsset& Test) { Test.TestDescription = Value; });
                })
            ]
        ];
        EditorRows->AddSlot().AutoHeight().Padding(3, 3, 3, 7)
        [
            SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(FMargin(9, 7))
            [ Settings ]
        ];
        EditorRows->AddSlot().AutoHeight().Padding(8, 4, 8, 5)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Test Steps (%d)"), Asset->Steps.Num()))) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [
                SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                .ToolTipText(LOCTEXT("NotesToggleTip", "Show or hide optional step descriptions"))
                .OnClicked(this, &SProbeWorkspace::ToggleStepNotes)
                [ SNew(STextBlock).Text(bStepNotesShown
                    ? LOCTEXT("HideNotes", "Hide Notes") : LOCTEXT("ShowNotes", "Show Notes"))
                  .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
            ]
        ];
        if (Asset->Steps.IsEmpty())
        {
            EditorRows->AddSlot().AutoHeight().Padding(10)
            [ SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
                .Text(LOCTEXT("NoSteps", "This test has no steps. Use Add Step above to create its first assertion.")) ];
        }
        for (int32 Index = 0; Index < Asset->Steps.Num(); ++Index)
        {
            EditorRows->AddSlot().AutoHeight().Padding(3, 0, 3, 3)
            [ MakeStepCard(TestRef, Index) ];
        }
    }

    FText SelectedName() const
    {
        if (!Selected.IsValid()) { return LOCTEXT("None", "No Probe test assets found"); }
        FString Name = Selected->AssetName.ToString();
        if (CanSaveSelected()) { Name += TEXT(" *"); }
        return FText::FromString(Name);
    }

    void OnSelectionChanged(TSharedPtr<FAssetData> Item, ESelectInfo::Type)
    {
        Selected = Item;
        RebuildTestEditor();
        FeedbackMessage = LOCTEXT("SelectedHint", "Edit Steps here. Unsaved edits are retained when switching tests.");
    }

    FReply RefreshClicked()
    {
        RefreshAssets();
        FeedbackMessage = LOCTEXT("Refreshed", "Test list refreshed. Unsaved edits are retained in memory.");
        return FReply::Handled();
    }

    void SelectCreatedAsset(UObject* Created)
    {
        for (const TSharedPtr<FAssetData>& Item : Assets)
        {
            if (Item.IsValid() && Item->GetObjectPathString() == Created->GetPathName())
            {
                Selected = Item;
                if (TestSelector.IsValid()) { TestSelector->SetSelectedItem(Item); }
                RebuildTestEditor();
                return;
            }
        }
        // On rare delayed-registry cases the newly-created asset will appear on Refresh Tests.
    }

    FReply CreateBlankAsset()
    {
        CreateAsset(Probe::ETestTemplate::Blank);
        return FReply::Handled();
    }

    void CreateAsset(Probe::ETestTemplate Template)
    {
        UProbeTestAssetFactory* Factory = NewObject<UProbeTestAssetFactory>();
        FAssetToolsModule& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
        UObject* Created = Tools.Get().CreateAssetWithDialog(TEXT("Probe_Test"), TEXT("/Game/ProbeTests"), UProbeTestAsset::StaticClass(), Factory);
        if (!Created)
        {
            FeedbackMessage = LOCTEXT("CreateCancelled", "Test creation canceled.");
            return;
        }
        UProbeTestAsset* Asset = Cast<UProbeTestAsset>(Created);
        if (Asset)
        {
            Asset->Modify();
            Asset->bNeedsConfiguration = Template != Probe::ETestTemplate::Smoke;
        }
        if (Asset && Template != Probe::ETestTemplate::Blank)
        {
            switch (Template)
            {
            case Probe::ETestTemplate::Smoke:
                Asset->TestDescription = TEXT("Basic Probe smoke test: plugin and Editor world availability");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::PluginEnabled));
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::EditorWorldExists));
                break;
            case Probe::ETestTemplate::Plugin:
                Asset->TestDescription = TEXT("Plugin availability check. Edit the Target plugin name.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::PluginEnabled));
                break;
            case Probe::ETestTemplate::World:
                Asset->TestDescription = TEXT("Editor world and minimum actor count checks");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::EditorWorldExists));
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ActorCountAtLeast));
                break;
            case Probe::ETestTemplate::Asset:
                Asset->TestDescription = TEXT("Check that an asset exists at the specified object path");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::AssetExists, Asset->GetPathName()));
                break;
            case Probe::ETestTemplate::ActorClass:
                Asset->TestDescription = TEXT("Check actors of a particular class in the Editor world");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ActorClassCountAtLeast));
                break;
            case Probe::ETestTemplate::ActorTag:
                Asset->TestDescription = TEXT("Check whether an actor has a tag. Set Target and tag an actor first.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ActorTagExists));
                break;
            case Probe::ETestTemplate::AssetClass:
                Asset->TestDescription = TEXT("Check this Probe Test asset's class. Change Target/Expected Class as needed.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::AssetClassIs, Asset->GetPathName()));
                break;
            case Probe::ETestTemplate::ActorLabel:
                Asset->TestDescription = TEXT("Verify an actor with a specific World Outliner label exists.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ActorLabelExists));
                break;
            case Probe::ETestTemplate::ActorSelection:
                Asset->TestDescription = TEXT("Verify the current selection contains at least one level actor.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::SelectedActorCountAtLeast));
                break;
            case Probe::ETestTemplate::Property:
                Asset->TestDescription = TEXT("Check this test asset's bEnabled property is True.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ObjectPropertyEquals, Asset->GetPathName()));
                break;
            case Probe::ETestTemplate::Module:
                Asset->TestDescription = TEXT("Verify ProbeEditor module has loaded.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ModuleLoaded));
                break;
            case Probe::ETestTemplate::EditorTab:
                Asset->TestDescription = TEXT("Verify the Probe Editor tab is open.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::EditorTabOpen));
                break;
            case Probe::ETestTemplate::ToolMenu:
                Asset->TestDescription = TEXT("Verify that an Editor ToolMenus path has been registered.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ToolMenuRegistered));
                break;
            case Probe::ETestTemplate::ConsoleVariable:
                Asset->TestDescription = TEXT("Check an Editor console variable is present.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ConsoleVariableExists));
                break;
            case Probe::ETestTemplate::AssetFolder:
                Asset->TestDescription = TEXT("Count assets in a folder recursively.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::AssetCountAtLeast));
                break;
            case Probe::ETestTemplate::AssetDirty:
                Asset->TestDescription = TEXT("Check this test asset has no unsaved changes after saving.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::AssetIsClean, Asset->GetPathName()));
                break;
            case Probe::ETestTemplate::ClassContract:
                Asset->TestDescription = TEXT("Verify an Actor class exposes a native property and function.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ClassHasProperty));
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ClassHasFunction));
                break;
            case Probe::ETestTemplate::ObjectPropertyAbsent:
                Asset->TestDescription = TEXT("Check a property is different from an expected value.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ObjectPropertyNotEquals, Asset->GetPathName()));
                break;
            case Probe::ETestTemplate::ActorComponent:
                Asset->TestDescription = TEXT("Check that an actor has a required component class. Capture an actor label.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ActorHasComponent));
                break;
            case Probe::ETestTemplate::ActorFolder:
                Asset->TestDescription = TEXT("Check a World Outliner actor folder path. Capture an actor label.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ActorFolderEquals));
                break;
            case Probe::ETestTemplate::SelectionExact:
                Asset->TestDescription = TEXT("Require exactly one actor selected in the level Editor.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::SelectedActorCountExactly));
                break;
            case Probe::ETestTemplate::PluginDescriptor:
                Asset->TestDescription = TEXT("Check Probe's descriptor identity. Adjust the version to match your plugin.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::PluginFriendlyNameEquals));
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::PluginVersionEquals));
                break;
            case Probe::ETestTemplate::PluginModule:
                Asset->TestDescription = TEXT("Check the plugin descriptor declares the expected module and that it is loaded.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::PluginDeclaresModule));
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ModuleLoaded));
                break;
            case Probe::ETestTemplate::ModuleDiscovery:
                Asset->TestDescription = TEXT("Verify a module exists and is currently loaded. Browse to choose identifiers.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ModuleExists));
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ModuleLoaded));
                break;
            case Probe::ETestTemplate::TabRegistration:
                Asset->TestDescription = TEXT("Verify Probe's global tab is registered and open.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::EditorTabRegistered));
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::EditorTabOpen));
                break;
            case Probe::ETestTemplate::EditorNotPlaying:
                Asset->TestDescription = TEXT("Check Editor world exists and PIE is not currently running.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::EditorWorldExists));
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::PIEIsNotRunning));
                break;
            case Probe::ETestTemplate::ConsoleVariableNegative:
                Asset->TestDescription = TEXT("Check a known console variable is not at an unwanted value.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ConsoleVariableNotEquals));
                break;
            case Probe::ETestTemplate::AssetAbsence:
                Asset->TestDescription = TEXT("Verify an asset object path is absent from the registry.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::AssetNotExists));
                break;
            case Probe::ETestTemplate::AssetTag:
                Asset->TestDescription = TEXT("Check an asset publishes a specified Asset Registry tag. Set tag name.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::AssetRegistryTagExists, Asset->GetPathName()));
                break;
            case Probe::ETestTemplate::ClassFunctionContract:
                Asset->TestDescription = TEXT("Check an Actor function exists and is callable from Blueprints.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ClassHasFunction));
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ClassFunctionBlueprintCallable));
                break;
            case Probe::ETestTemplate::ClassPropertyAbsence:
                Asset->TestDescription = TEXT("Verify an Actor class does not have a particular reflected property.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ClassHasNoProperty));
                break;
            case Probe::ETestTemplate::ObjectGreaterThan:
                Asset->TestDescription = TEXT("Compare numeric property on a target object against a threshold. Configure object path/property.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ObjectPropertyGreaterThan));
                break;
            case Probe::ETestTemplate::ActorSelectionState:
                Asset->TestDescription = TEXT("Verify an Outliner actor is selected. Use Selected Actor to capture its label.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ActorIsSelected));
                break;
            case Probe::ETestTemplate::ActorMaxCount:
                Asset->TestDescription = TEXT("Check the level has no more than a specified number of actors.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::ActorCountAtMost));
                break;
            case Probe::ETestTemplate::SelectionMaxCount:
                Asset->TestDescription = TEXT("Check no more than a configured number of actors are selected.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::SelectedActorCountAtMost));
                break;
            case Probe::ETestTemplate::SelectedComponent:
                Asset->TestDescription = TEXT("Select an actor and verify it has a component of the configured class.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::SelectedActorHasComponent));
                break;
            case Probe::ETestTemplate::AssetExactCount:
                Asset->TestDescription = TEXT("Count assets in a folder exactly. Set the expected count after inspection.");
                Asset->Steps.Add(Probe::MakeStep(EProbeStepType::AssetCountExactly));
                break;
            default: break;
            }
            for (FProbeTestStep& Step : Asset->Steps)
            {
                if (Step.ExpectedValue == TEXT("{InstalledVersion}"))
                {
                    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Probe"));
                    Step.ExpectedValue = Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : FString();
                }
            }
            Asset->MarkPackageDirty();
        }
        RefreshAssets();
        SelectCreatedAsset(Created);
        RebuildTestEditor();
        FeedbackMessage = FText::Format(LOCTEXT("Created", "Created {0}. Configure steps and Save Asset."), FText::FromString(Created->GetName()));
    }

    void CreatePack()
    {
        UProbeTestPackFactory* Factory = NewObject<UProbeTestPackFactory>();
        FAssetToolsModule& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
        UObject* Created = Tools.Get().CreateAssetWithDialog(TEXT("Probe_Pack"), TEXT("/Game/ProbeTests"),
            UProbeTestPack::StaticClass(), Factory);
        if (Created)
        {
            Probe::OpenPackAssetsInWorkspace({Created->GetPathName()});
            FeedbackMessage = LOCTEXT("PackCreated", "Probe Pack created. Add tests in the Packs tab, save, then run it.");
        }
    }

    void CreateExtensionTemplate(FName Id)
    {
        const Probe::FExtensionTemplate* Template = Probe::FindExtensionTemplate(Id);
        if (!Template) { return; }
        UProbeTestAssetFactory* Factory = NewObject<UProbeTestAssetFactory>();
        FAssetToolsModule& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
        UObject* Created = Tools.Get().CreateAssetWithDialog(TEXT("Probe_Test"), TEXT("/Game/ProbeTests"),
            UProbeTestAsset::StaticClass(), Factory);
        UProbeTestAsset* Asset = Cast<UProbeTestAsset>(Created);
        if (!Asset) { return; }
        Asset->Modify();
        Asset->TestDescription = Template->Description;
        Asset->Steps = Template->Steps;
        Asset->bNeedsConfiguration = Template->bRequiresConfiguration;
        for (FProbeTestStep& Step : Asset->Steps)
        {
            if (Step.Target == TEXT("{Self}")) { Step.Target = Asset->GetPathName(); }
            if (Step.ExpectedValue == TEXT("{InstalledVersion}"))
            {
                const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Probe"));
                Step.ExpectedValue = Plugin.IsValid() ? Plugin->GetDescriptor().VersionName : FString();
            }
        }
        Asset->MarkPackageDirty();
        RefreshAssets();
        SelectCreatedAsset(Asset);
        RebuildTestEditor();
        FeedbackMessage = Asset->bNeedsConfiguration
            ? LOCTEXT("ExtensionTemplateNeedsSetup", "Template created. Review its fields, then clear Needs Configuration and save before running.")
            : LOCTEXT("ExtensionTemplateCreated", "Template created. Review targets and save the asset.");
    }

    TSharedRef<SWidget> BuildTemplateMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        Menu.AddMenuEntry(LOCTEXT("BlankTemplate", "Blank Test"), LOCTEXT("BlankTemplateTip", "Create an empty test"), FSlateIcon(),
            FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::CreateAsset, Probe::ETestTemplate::Blank)));
        Menu.AddMenuEntry(LOCTEXT("SmokeTemplate", "Probe Smoke Test"), LOCTEXT("SmokeTemplateTip", "Verify Probe and the active Editor world"), FSlateIcon(),
            FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::CreateAsset, Probe::ETestTemplate::Smoke)));
        Menu.AddMenuSeparator();
        Menu.AddMenuEntry(LOCTEXT("NewTestPack", "New Probe Pack"),
            LOCTEXT("NewTestPackTip", "Create a Probe Pack, then add existing tests in Probe Packs. Packs reference tests without copying them"), FSlateIcon(),
            FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::CreatePack)));
        struct FTemplateInfo { Probe::ETestTemplate Type; const TCHAR* Category; const TCHAR* Name; const TCHAR* Description; };
        const TArray<FTemplateInfo> Templates = {
            {Probe::ETestTemplate::Plugin, TEXT("Plugins & Modules"), TEXT("Plugin Availability"), TEXT("Check plugin enabled state")},
            {Probe::ETestTemplate::Module, TEXT("Plugins & Modules"), TEXT("Module Loaded"), TEXT("Confirm ProbeEditor is loaded")},
            {Probe::ETestTemplate::EditorTab, TEXT("Editor UI"), TEXT("Editor Tab Open"), TEXT("Check a registered tab ID is currently open")},
            {Probe::ETestTemplate::ToolMenu, TEXT("Editor UI"), TEXT("Registered Tool Menu"), TEXT("Check an Editor menu ID exists")},
            {Probe::ETestTemplate::ConsoleVariable, TEXT("Editor State"), TEXT("Console Variable"), TEXT("Check a console variable is available")},
            {Probe::ETestTemplate::Asset, TEXT("Assets"), TEXT("Asset Availability"), TEXT("Check asset path exists")},
            {Probe::ETestTemplate::AssetClass, TEXT("Assets"), TEXT("Asset Class Check"), TEXT("Check this Test Asset class")},
            {Probe::ETestTemplate::AssetFolder, TEXT("Assets"), TEXT("Assets In Folder"), TEXT("Count assets in a content folder")},
            {Probe::ETestTemplate::AssetDirty, TEXT("Assets"), TEXT("Asset Save State"), TEXT("Check an asset package is clean")},
            {Probe::ETestTemplate::ClassContract, TEXT("Classes & Reflection"), TEXT("Class Property and Function"), TEXT("Check reflected properties and functions")},
            {Probe::ETestTemplate::World, TEXT("Actors & World"), TEXT("Editor World and Actors"), TEXT("Check Editor world and actor count")},
            {Probe::ETestTemplate::ActorClass, TEXT("Actors & World"), TEXT("Actor Class Count"), TEXT("Count actors derived from a class")},
            {Probe::ETestTemplate::ActorTag, TEXT("Actors & World"), TEXT("Actor Tag"), TEXT("Find an actor by tag")},
            {Probe::ETestTemplate::ActorLabel, TEXT("Actors & World"), TEXT("Actor Label"), TEXT("Find an actor by label")},
            {Probe::ETestTemplate::ActorComponent, TEXT("Actors & World"), TEXT("Actor Component"), TEXT("Check an actor component class")},
            {Probe::ETestTemplate::ActorFolder, TEXT("Actors & World"), TEXT("Actor Outliner Folder"), TEXT("Check an actor folder path")},
            {Probe::ETestTemplate::ActorSelection, TEXT("Selection"), TEXT("Selected Actors"), TEXT("Check a minimum actor selection count")},
            {Probe::ETestTemplate::SelectionExact, TEXT("Selection"), TEXT("Exact Selection Count"), TEXT("Check an exact actor selection count")},
            {Probe::ETestTemplate::Property, TEXT("Properties & Values"), TEXT("Object Property Check"), TEXT("Compare a reflected property")},
            {Probe::ETestTemplate::ObjectPropertyAbsent, TEXT("Properties & Values"), TEXT("Property Does Not Equal"), TEXT("Verify a property differs from a value")},
            {Probe::ETestTemplate::PluginDescriptor, TEXT("Plugins & Modules"), TEXT("Plugin Descriptor Identity"), TEXT("Compare a plugin's friendly name and version")},
            {Probe::ETestTemplate::PluginModule, TEXT("Plugins & Modules"), TEXT("Plugin Declared Module"), TEXT("Verify a module is declared and loaded")},
            {Probe::ETestTemplate::ModuleDiscovery, TEXT("Plugins & Modules"), TEXT("Module Existence And Load"), TEXT("Choose a module from live/discovered identifiers")},
            {Probe::ETestTemplate::TabRegistration, TEXT("Editor UI"), TEXT("Tab Registration And Open State"), TEXT("Check global tab ID is registered and open")},
            {Probe::ETestTemplate::EditorNotPlaying, TEXT("Editor State"), TEXT("Editor Not In PIE"), TEXT("Check the Editor is not currently playing")},
            {Probe::ETestTemplate::ConsoleVariableNegative, TEXT("Editor State"), TEXT("Console Variable Forbidden Value"), TEXT("Verify console variable differs from an unwanted value")},
            {Probe::ETestTemplate::AssetAbsence, TEXT("Assets"), TEXT("Missing Asset"), TEXT("Verify an asset does not exist")},
            {Probe::ETestTemplate::AssetTag, TEXT("Assets"), TEXT("Asset Registry Tag Present"), TEXT("Check asset has a searchable tag")},
            {Probe::ETestTemplate::AssetExactCount, TEXT("Assets"), TEXT("Exact Asset Count"), TEXT("Verify exact number of assets in folder")},
            {Probe::ETestTemplate::ClassFunctionContract, TEXT("Classes & Reflection"), TEXT("Blueprint Function Contract"), TEXT("Verify a function exists and is Blueprint callable")},
            {Probe::ETestTemplate::ClassPropertyAbsence, TEXT("Classes & Reflection"), TEXT("Reflected Property Absent"), TEXT("Verify a reflected property does not exist")},
            {Probe::ETestTemplate::ObjectGreaterThan, TEXT("Properties & Values"), TEXT("Numeric Threshold"), TEXT("Compare numeric property with a threshold")},
            {Probe::ETestTemplate::ActorSelectionState, TEXT("Actors & World"), TEXT("Actor Selected State"), TEXT("Capture and check the selection state of an actor")},
            {Probe::ETestTemplate::ActorMaxCount, TEXT("Actors & World"), TEXT("Maximum Actor Count"), TEXT("Verify upper bound for level actors")},
            {Probe::ETestTemplate::SelectionMaxCount, TEXT("Selection"), TEXT("Maximum Selected Actors"), TEXT("Verify selection count upper bound")},
            {Probe::ETestTemplate::SelectedComponent, TEXT("Selection"), TEXT("Selected Actor Component"), TEXT("Verify one selected actor contains a component")},
        };
        Menu.AddMenuSeparator();
        for (const FString& Category : Probe::AllCategories())
        {
            // Capture the descriptors by value; the submenu can be opened after this function returns.
            Menu.AddSubMenu(FText::FromString(Category), FText::FromString(Category + TEXT(" templates")),
                FNewMenuDelegate::CreateLambda([this, Category, Templates](FMenuBuilder& SubMenu)
                {
                    for (const FTemplateInfo& Info : Templates)
                    {
                        if (Category != Info.Category) { continue; }
                        SubMenu.AddMenuEntry(FText::FromString(Info.Name), FText::FromString(FString(Info.Description) + TEXT(". Review and mark Ready before Run All.")), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::CreateAsset, Info.Type)));
                    }
                    for (const FName Id : Probe::GetExtensionTemplateIds())
                    {
                        const Probe::FExtensionTemplate* Info = Probe::FindExtensionTemplate(Id);
                        if (!Info || Category != Info->Category) { continue; }
                        SubMenu.AddMenuEntry(FText::FromString(Info->Name), FText::FromString(Info->Description +
                            (Info->bRequiresConfiguration ? TEXT(". Needs configuration before automatic execution.") : TEXT(". Ready-to-run starter."))), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::CreateExtensionTemplate, Id)));
                    }
                }));
        }
        return Menu.MakeWidget();
    }

    void AddStep(EProbeStepType Type)
    {
        UProbeTestAsset* Asset = SelectedAsset();
        if (!Asset) { return; }
        const FScopedTransaction Transaction(LOCTEXT("AddStepTransaction", "Add Probe Test Step"));
        Asset->Modify();
        Asset->Steps.Add(Probe::MakeStep(Type, Asset->GetPathName()));
        Asset->MarkPackageDirty();
        ExpandedStepIndices.Add(Asset->Steps.Num() - 1);
        RebuildTestEditor();
        FeedbackMessage = LOCTEXT("StepAdded", "Step added. Review its parameters below, then Save Asset.");
    }

    void AddExtensionStep(FName Id)
    {
        const Probe::FExtensionAssertion* Extension = Probe::FindExtensionAssertion(Id);
        UProbeTestAsset* Asset = SelectedAsset();
        if (!Asset || !Extension) { return; }
        const FScopedTransaction Transaction(LOCTEXT("AddExtensionStepTransaction", "Add Probe Extension Assertion"));
        Asset->Modify();
        FProbeTestStep Step;
        Step.AssertionId = Id;
        Step.Description = Extension->Description;
        Asset->Steps.Add(Step);
        Asset->MarkPackageDirty();
        ExpandedStepIndices.Add(Asset->Steps.Num() - 1);
        RebuildTestEditor();
    }

    TSharedRef<SWidget> BuildAddStepMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        for (const FString& Category : Probe::AllCategories())
        {
            Menu.AddSubMenu(FText::FromString(Category), FText::FromString(Category + TEXT(" assertions")),
                FNewMenuDelegate::CreateLambda([this, Category](FMenuBuilder& SubMenu)
                {
                    for (const Probe::FAssertionInfo& Info : Probe::AssertionCatalog())
                    {
                        if (Category != Info.Category) { continue; }
                        SubMenu.AddMenuEntry(FText::FromString(Info.Name), FText::FromString(FString(Info.Description) + TEXT(". Review and mark Ready before Run All.")), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::AddStep, Info.Type)));
                    }
                    for (const FName Id : Probe::GetExtensionAssertionIds())
                    {
                        const Probe::FExtensionAssertion* Info = Probe::FindExtensionAssertion(Id);
                        if (!Info || Category != Info->Category) { continue; }
                        SubMenu.AddMenuEntry(FText::FromString(Info->Name), FText::FromString(Info->Description + TEXT(". Configure this assertion before automatic execution.")), FSlateIcon(),
                            FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::AddExtensionStep, Id)));
                    }
                }));
        }
        return Menu.MakeWidget();
    }

    bool SaveSelected()
    {
        UProbeTestAsset* Asset = SelectedAsset();
        if (!Asset) { return false; }
        UPackage* Package = Asset->GetOutermost();
        if (!Package || !Package->IsDirty())
        {
            FeedbackMessage = LOCTEXT("NothingToSave", "No unsaved changes in the selected test.");
            return true;
        }
        TArray<UPackage*> PackagesToSave;
        PackagesToSave.Add(Package);
        const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
        if (bSaved && !Package->IsDirty())
        {
            FeedbackMessage = FText::Format(LOCTEXT("Saved", "Saved {0}."), FText::FromString(Asset->GetName()));
            return true;
        }
        FeedbackMessage = LOCTEXT("SaveFailed", "Save did not complete. Check Output Log or source control status.");
        return false;
    }

    FReply SaveSelectedClicked()
    {
        SaveSelected();
        return FReply::Handled();
    }

    void ExecuteAsset(UProbeTestAsset* Asset)
    {
        Results.Add(Probe::RunTest(Asset));
    }

    static const FSlateBrush* ResultIcon(Probe::EResultStatus Status)
    {
        const ISlateStyle* Style = FSlateStyleRegistry::FindSlateStyle(TEXT("ProbeAssetAppearance"));
        if (!Style) { return FAppStyle::GetBrush(TEXT("Icons.Warning")); }
        switch (Status)
        {
        case Probe::EResultStatus::Passed: return Style->GetBrush(TEXT("Probe.StatusPassed"));
        case Probe::EResultStatus::Failed: return Style->GetBrush(TEXT("Probe.StatusFailed"));
        case Probe::EResultStatus::Skipped: return Style->GetBrush(TEXT("Probe.StatusSkipped"));
        default: return Style->GetBrush(TEXT("Probe.StatusError"));
        }
    }

    static FLinearColor ResultColor(Probe::EResultStatus Status)
    {
        switch (Status)
        {
        case Probe::EResultStatus::Passed: return FLinearColor(0.30f, 0.70f, 0.36f);
        case Probe::EResultStatus::Failed: return FLinearColor(0.90f, 0.34f, 0.31f);
        case Probe::EResultStatus::Skipped: return FLinearColor(0.62f, 0.62f, 0.62f);
        default: return FLinearColor(0.95f, 0.36f, 0.27f);
        }
    }

    FString BuildResultsLogString() const
    {
        if (Results.IsEmpty())
        {
            return TEXT("Run a test to see its results here. Select any text to copy it.");
        }

        FString Log;
        for (const Probe::FTestResult& Result : Results)
        {
            if (!Log.IsEmpty()) { Log += TEXT("\n\n"); }
            Log += FString::Printf(TEXT("[%s] %s  (%.2f ms)\n%s"),
                Probe::StatusName(Result.Status), *Result.Name, Result.DurationMs, *Result.Detail);
            for (const Probe::FStepResult& Step : Result.Steps)
            {
                const FString StepLabel = Step.Description.IsEmpty()
                    ? FString::Printf(TEXT("Step %d"), Step.Index)
                    : FString::Printf(TEXT("Step %d: %s"), Step.Index, *Step.Description);
                Log += FString::Printf(TEXT("\n  [%s] %s  (%.2f ms)\n    %s"),
                    Probe::StatusName(Step.Status), *StepLabel, Step.DurationMs, *Step.Detail);
            }
        }
        return Log;
    }

    FText ResultsLogText() const { return FText::FromString(BuildResultsLogString()); }

    FReply CopyResults()
    {
        FPlatformApplicationMisc::ClipboardCopy(*BuildResultsLogString());
        FeedbackMessage = LOCTEXT("ResultsCopied", "Results log copied to clipboard.");
        return FReply::Handled();
    }

    FText RunInfoText() const
    {
        if (Results.IsEmpty()) { return FText::GetEmpty(); }
        return FText::FromString(FString::Printf(TEXT("%d test(s) | %.2f ms | %s"),
            Results.Num(), LastRunDurationMs, *LastRunTime.ToString(TEXT("%H:%M:%S"))));
    }

    static int32 ResultSortPriority(Probe::EResultStatus Status)
    {
        switch (Status)
        {
        case Probe::EResultStatus::Error: return 0;
        case Probe::EResultStatus::Failed: return 1;
        case Probe::EResultStatus::Skipped: return 2;
        case Probe::EResultStatus::Passed: return 3;
        }
        return 4;
    }

    bool MatchesFilter(Probe::EResultStatus Status) const
    {
        switch (ActiveResultFilter)
        {
        case EResultFilter::Passed: return Status == Probe::EResultStatus::Passed;
        case EResultFilter::Failed: return Status == Probe::EResultStatus::Failed;
        case EResultFilter::Skipped: return Status == Probe::EResultStatus::Skipped;
        case EResultFilter::Error: return Status == Probe::EResultStatus::Error;
        default: return true;
        }
    }

    FReply SelectResultFilter(EResultFilter Filter)
    {
        ActiveResultFilter = Filter;
        RebuildResultList();
        ShowResultsSummary();
        return FReply::Handled();
    }

    void AddSummaryFilter(int32 Count, const TCHAR* Label, EResultFilter Filter,
        const FLinearColor& Color)
    {
        if (!ResultSummary.IsValid()) { return; }
        ResultSummary->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
        [
            SNew(SButton)
            .ButtonStyle(FAppStyle::Get(), ActiveResultFilter == Filter ? "Button" : "SimpleButton")
            .ToolTipText(FText::FromString(FString::Printf(TEXT("Show %s results"), Label)))
            .OnClicked_Lambda([this, Filter]() { return SelectResultFilter(Filter); })
            [
                SNew(STextBlock)
                .Text(FText::FromString(FString::Printf(TEXT("%d %s"), Count, Label)))
                .ColorAndOpacity(Count > 0 ? FSlateColor(Color) : FSlateColor::UseSubduedForeground())
            ]
        ];
    }

    void ShowResultsSummary()
    {
        if (!ResultSummary.IsValid()) { return; }
        ResultSummary->ClearChildren();
        if (Results.IsEmpty())
        {
            ResultSummary->AddSlot().AutoWidth()
            [ SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
              .Text(LOCTEXT("NoRunsSummary", "No tests run")) ];
            return;
        }
        int32 Passed = 0, Failed = 0, Skipped = 0, Errors = 0;
        for (const Probe::FTestResult& Result : Results)
        {
            switch (Result.Status)
            {
            case Probe::EResultStatus::Passed: ++Passed; break;
            case Probe::EResultStatus::Failed: ++Failed; break;
            case Probe::EResultStatus::Skipped: ++Skipped; break;
            default: ++Errors; break;
            }
        }
        AddSummaryFilter(Results.Num(), TEXT("All"), EResultFilter::All,
            FLinearColor(0.8f, 0.8f, 0.8f));
        AddSummaryFilter(Passed, TEXT("Passed"), EResultFilter::Passed,
            ResultColor(Probe::EResultStatus::Passed));
        AddSummaryFilter(Failed, TEXT("Failed"), EResultFilter::Failed,
            ResultColor(Probe::EResultStatus::Failed));
        AddSummaryFilter(Skipped, TEXT("Skipped"), EResultFilter::Skipped,
            FLinearColor(0.62f, 0.62f, 0.62f));
        AddSummaryFilter(Errors, TEXT("Errors"), EResultFilter::Error,
            FLinearColor(0.90f, 0.30f, 0.28f));
    }

    FReply ChooseResultView(bool bLog)
    {
        bShowLog = bLog;
        if (ResultViewSwitcher.IsValid()) { ResultViewSwitcher->SetActiveWidgetIndex(bLog ? 1 : 0); }
        BuildResultTabs();
        return FReply::Handled();
    }

    FReply ToggleResultOrder()
    {
        bProblemsFirst = !bProblemsFirst;
        BuildResultTabs();
        RebuildResultList();
        return FReply::Handled();
    }

    void BuildResultTabs()
    {
        if (!ResultTabs.IsValid()) { return; }
        ResultTabs->ClearChildren();
        ResultTabs->AddSlot().AutoWidth().Padding(0, 0, 3, 0)
        [
            SNew(SButton)
            .ToolTipText(LOCTEXT("ExplorerTip", "Explore each test and its step diagnostics"))
            .OnClicked_Lambda([this]() { return ChooseResultView(false); })
            [ SNew(STextBlock).Text(LOCTEXT("ExplorerTitle", "Results Explorer"))
              .ColorAndOpacity(bShowLog ? FSlateColor::UseSubduedForeground() : FSlateColor::UseForeground()) ]
        ];
        ResultTabs->AddSlot().AutoWidth().Padding(0, 0, 6, 0)
        [
            SNew(SButton)
            .ToolTipText(LOCTEXT("LogTip", "Select text, right-click to copy, or use Copy Results"))
            .OnClicked_Lambda([this]() { return ChooseResultView(true); })
            [ SNew(STextBlock).Text(LOCTEXT("LogTitle", "Log"))
              .ColorAndOpacity(bShowLog ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground()) ]
        ];
        ResultTabs->AddSlot().FillWidth(1.0f) [ SNullWidget::NullWidget ];
        ResultTabs->AddSlot().AutoWidth().VAlign(VAlign_Center)
        [
            SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
            .ToolTipText(LOCTEXT("SortTip", "Toggle problem-first ordering or original execution order"))
            .OnClicked(this, &SProbeWorkspace::ToggleResultOrder)
            [ SNew(STextBlock).Text(bProblemsFirst
                ? LOCTEXT("ProblemsFirst", "Problems First") : LOCTEXT("RunOrder", "Run Order"))
              .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
        ];
    }

    FReply SelectResult(int32 ResultIndex)
    {
        if (Results.IsValidIndex(ResultIndex))
        {
            SelectedResultIndex = ResultIndex;
            RebuildResultList();
            RebuildResultDetails();
        }
        return FReply::Handled();
    }

    void RebuildResultList()
    {
        if (!ResultList.IsValid()) { return; }
        ResultList->ClearChildren();
        ResultList->AddSlot().AutoHeight().Padding(5, 1, 5, 5)
        [ SNew(SHorizontalBox)
          + SHorizontalBox::Slot().FillWidth(1.0f)
          [ SNew(STextBlock).Text(LOCTEXT("ResultColumnTest", "Test"))
            .Font(FAppStyle::GetFontStyle("SmallFont"))
            .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
          + SHorizontalBox::Slot().AutoWidth()
          [ SNew(STextBlock).Text(LOCTEXT("ResultColumnSteps", "Steps"))
            .Font(FAppStyle::GetFontStyle("SmallFont"))
            .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ];
        TArray<int32> Visible;
        for (int32 Index = 0; Index < Results.Num(); ++Index)
        {
            if (MatchesFilter(Results[Index].Status)) { Visible.Add(Index); }
        }
        if (bProblemsFirst)
        {
            Visible.StableSort([this](int32 A, int32 B)
            {
                return ResultSortPriority(Results[A].Status) < ResultSortPriority(Results[B].Status);
            });
        }
        if (Visible.IsEmpty())
        {
            SelectedResultIndex = INDEX_NONE;
            ResultList->AddSlot().AutoHeight().Padding(7)
            [ SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
              .Text(Results.IsEmpty() ? LOCTEXT("RunToPopulate", "Run a test to see results.")
                    : LOCTEXT("NoMatchingResults", "No results match this filter.")) ];
            RebuildResultDetails();
            return;
        }
        if (!Visible.Contains(SelectedResultIndex)) { SelectedResultIndex = Visible[0]; }
        for (const int32 Index : Visible)
        {
            const Probe::FTestResult& Result = Results[Index];
            const bool bSelected = SelectedResultIndex == Index;
            ResultList->AddSlot().AutoHeight().Padding(0, 0, 0, 2)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                .BorderBackgroundColor(bSelected ? FLinearColor(0.19f, 0.27f, 0.35f, 1.0f)
                                                  : FLinearColor(1.0f, 1.0f, 1.0f, 0.12f))
                .Padding(1)
                [
                    SNew(SProbeResultButton)
                    .ToolTipText(FText::FromString(Result.Detail))
                    .OnClicked(FOnClicked::CreateLambda([this, Index]() { return SelectResult(Index); }))
                    .OnDoubleClicked(FOnClicked::CreateLambda([this, Index]()
                    {
                        SelectResult(Index);
                        bResultsFocused = false;
                        return EditSelectedResultTest();
                    }))
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 5, 0)
                        [ SNew(SImage).Image(ResultIcon(Result.Status)) ]
                        + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                        [ SNew(STextBlock).Text(FText::FromString(Result.Name)).AutoWrapText(true) ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3, 0)
                        [ SNew(STextBlock).Text(FText::AsNumber(Result.Steps.Num()))
                          .ToolTipText(LOCTEXT("StepCountTip", "Recorded steps"))
                          .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
                    ]
                ]
            ];
        }
        RebuildResultDetails();
    }

    FString BuildSelectedResultString() const
    {
        if (!Results.IsValidIndex(SelectedResultIndex)) { return FString(); }
        const Probe::FTestResult& Result = Results[SelectedResultIndex];
        FString Text = FString::Printf(TEXT("[%s] %s (%.2f ms)\n%s"),
            Probe::StatusName(Result.Status), *Result.Name, Result.DurationMs, *Result.Detail);
        for (const Probe::FStepResult& Step : Result.Steps)
        {
            Text += FString::Printf(TEXT("\n[%s] Step %d: %s (%.2f ms)\n%s"),
                Probe::StatusName(Step.Status), Step.Index, *Step.Description,
                Step.DurationMs, *Step.Detail);
        }
        return Text;
    }

    FReply EditSelectedResultTest()
    {
        if (!Results.IsValidIndex(SelectedResultIndex)) { return FReply::Handled(); }
        bResultsFocused = false;
        const FString Path = Results[SelectedResultIndex].AssetPath;
        for (const TSharedPtr<FAssetData>& Entry : Assets)
        {
            if (Entry.IsValid() && Entry->GetObjectPathString() == Path)
            {
                if (TestSelector.IsValid()) { TestSelector->SetSelectedItem(Entry); }
                else { Selected = Entry; RebuildTestEditor(); }
                FeedbackMessage = LOCTEXT("EditResultTestHint", "Selected the test definition for editing.");
                return FReply::Handled();
            }
        }
        FeedbackMessage = LOCTEXT("MissingResultAssetHint", "The result's test asset is not in the current test list. Refresh Tests.");
        return FReply::Handled();
    }

    FReply EditSelectedResultStep(int32 StepNumber)
    {
        const FReply Result = EditSelectedResultTest();
        if (StepNumber > 0)
        {
            ExpandedStepIndices.Add(StepNumber - 1);
            bResultsFocused = false;
            RebuildTestEditor();
        }
        return Result;
    }

    FReply CopySelectedDetails()
    {
        FPlatformApplicationMisc::ClipboardCopy(*BuildSelectedResultString());
        FeedbackMessage = LOCTEXT("SelectedDetailsCopied", "Selected test diagnostics copied.");
        return FReply::Handled();
    }

    void RebuildResultDetails()
    {
        if (!ResultDetails.IsValid()) { return; }
        ResultDetails->ClearChildren();
        if (!Results.IsValidIndex(SelectedResultIndex))
        {
            ResultDetails->AddSlot().AutoHeight().Padding(6)
            [ SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
              .Text(LOCTEXT("SelectResultHint", "Select a result to inspect its steps.")) ];
            return;
        }
        const Probe::FTestResult& Result = Results[SelectedResultIndex];
        if (ExpandedDiagnosticTestPath != Result.AssetPath)
        {
            ExpandedDiagnosticTestPath = Result.AssetPath;
            ExpandedDiagnosticIndices.Reset();
            for (const Probe::FStepResult& Step : Result.Steps)
            {
                if (Step.Status == Probe::EResultStatus::Failed || Step.Status == Probe::EResultStatus::Error)
                {
                    ExpandedDiagnosticIndices.Add(Step.Index);
                }
            }
        }
        ResultDetails->AddSlot().AutoHeight().Padding(3, 3, 3, 6)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(Result.Name))
              .Font(FAppStyle::GetFontStyle("NormalFontBold")).AutoWrapText(true) ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
            [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
              .ToolTipText(LOCTEXT("EditResultTip", "Select this test in the Test Definition editor"))
              .OnClicked(this, &SProbeWorkspace::EditSelectedResultTest)
              [ SNew(STextBlock).Text(LOCTEXT("EditResultTest", "Edit Test")) ] ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
              .ToolTipText(LOCTEXT("CopyDetailTip", "Copy diagnostics for this test"))
              .OnClicked(this, &SProbeWorkspace::CopySelectedDetails)
              [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Duplicate")) ] ]
        ];
        ResultDetails->AddSlot().AutoHeight().Padding(3, 0, 3, 4)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
            [ SNew(STextBlock).Text(FText::FromString(Probe::StatusName(Result.Status)))
              .ColorAndOpacity(ResultColor(Result.Status)) ]
            + SHorizontalBox::Slot().AutoWidth()
            [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%.2f ms"), Result.DurationMs)))
              .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
        ];
        ResultDetails->AddSlot().AutoHeight().Padding(3, 0, 3, 8)
        [ SNew(STextBlock).Text(FText::FromString(Result.Detail)).AutoWrapText(true) ];
        for (const Probe::FStepResult& Step : Result.Steps)
        {
            const bool bExpanded = ExpandedDiagnosticIndices.Contains(Step.Index);
            const int32 StepIndex = Step.Index;
            ResultDetails->AddSlot().AutoHeight().Padding(2, 0, 2, 4)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                .Padding(FMargin(5, 3))
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.0f)
                        [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                          .ToolTipText(bExpanded ? LOCTEXT("HideDiagnostic", "Collapse diagnostic") : LOCTEXT("ShowDiagnostic", "Expand diagnostic"))
                          .OnClicked_Lambda([this, StepIndex]()
                          {
                              if (ExpandedDiagnosticIndices.Contains(StepIndex)) { ExpandedDiagnosticIndices.Remove(StepIndex); }
                              else { ExpandedDiagnosticIndices.Add(StepIndex); }
                              RebuildResultDetails();
                              return FReply::Handled();
                          })
                          [ SNew(SHorizontalBox)
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 7, 0)
                            [ SNew(SImage).Image(FSlateStyleRegistry::FindSlateStyle(TEXT("ProbeAssetAppearance"))
                                ? FSlateStyleRegistry::FindSlateStyle(TEXT("ProbeAssetAppearance"))->GetBrush(
                                    bExpanded ? TEXT("Probe.DisclosureExpanded") : TEXT("Probe.DisclosureCollapsed"))
                                : FAppStyle::GetBrush(TEXT("Icons.ChevronRight"))) ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                            [ SNew(STextBlock).Text(FText::FromString(Probe::StatusName(Step.Status)))
                              .ColorAndOpacity(ResultColor(Step.Status)) ]
                            + SHorizontalBox::Slot().FillWidth(1.0f)
                            [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Step %d: %s"), Step.Index, *Step.Description)))
                              .AutoWrapText(true) ] ] ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                          .ToolTipText(LOCTEXT("EditDiagnosticStepTip", "Select this step in Test Definition"))
                          .OnClicked_Lambda([this, StepIndex]() { return EditSelectedResultStep(StepIndex); })
                          [ SNew(STextBlock).Text(LOCTEXT("EditDiagnosticStep", "Edit Step")) ] ]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(18, 4, 2, 3)
                    [ SNew(STextBlock).Text(FText::FromString(Step.Detail))
                      .AutoWrapText(true)
                      .Visibility(bExpanded ? EVisibility::Visible : EVisibility::Collapsed)
                      .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
                ]
            ];
        }
    }

    void ShowResults()
    {
        Probe::UpdateRecentTestStatuses(Results);
        if (ResultsLog.IsValid()) { ResultsLog->SetText(ResultsLogText()); }
        ShowResultsSummary();
        RebuildResultList();
    }

public:
    void RunPackPath(const FString& PackPath) { ExecutePackPath(PackPath); }
    void RunExplicitTestPaths(const TArray<FString>& Paths)
    {
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        Results.Reset();
        for (const FString& Path : Paths)
        {
            const Probe::FReferenceHealth Health = Probe::InspectTestReference(FSoftObjectPath(Path));
            if (!Health.IsUsable())
            {
                Probe::FTestResult Missing;
                Missing.Name = Path; Missing.AssetPath = Path;
                Missing.Status = Probe::EResultStatus::Error; Missing.Detail = Health.Explanation;
                Results.Add(MoveTemp(Missing)); continue;
            }
            if (UProbeTestAsset* Test = LoadObject<UProbeTestAsset>(nullptr, *Path)) { ExecuteAsset(Test); }
            else
            {
                Probe::FTestResult Missing;
                Missing.Name = Path; Missing.AssetPath = Path;
                Missing.Status = Probe::EResultStatus::Error; Missing.Detail = TEXT("Probe Test reference could not be loaded.");
                Results.Add(MoveTemp(Missing));
            }
        }
        FeedbackMessage = FText::FromString(FString::Printf(TEXT("Ran %d selected Probe Test(s)."), Results.Num()));
        RecordRunCompleted(); ShowResults();
    }
private:
    void ExecutePackPath(FString PackPath)
    {
        UProbeTestPack* Pack = LoadObject<UProbeTestPack>(nullptr, *PackPath);
        if (!Pack)
        {
            FeedbackMessage = LOCTEXT("PackMissing", "The selected Probe Pack could not be loaded.");
            return;
        }
        Results = Probe::RunPack(Pack);
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        FeedbackMessage = FText::FromString(FString::Printf(TEXT("Ran pack '%s' (%d tests)"), *Pack->GetName(), Results.Num()));
        RecordRunCompleted();
        ShowResults();
    }

    TSharedRef<SWidget> BuildPackMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        FARFilter Filter;
        Filter.ClassPaths.Add(UProbeTestPack::StaticClass()->GetClassPathName());
        TArray<FAssetData> Packs;
        Registry.Get().GetAssets(Filter, Packs);
        Packs.Sort([](const FAssetData& A, const FAssetData& B)
        { return A.AssetName.ToString() < B.AssetName.ToString(); });
        if (Packs.IsEmpty())
        {
            Menu.AddMenuEntry(LOCTEXT("NoPacks", "No Probe Packs Found"),
                LOCTEXT("NoPacksTip", "Create a Probe Pack from the Templates menu, then assign tests to it"),
                FSlateIcon(), FUIAction());
        }
        for (const FAssetData& PackData : Packs)
        {
            const FString Path = PackData.GetObjectPathString();
            Menu.AddMenuEntry(FText::FromName(PackData.AssetName), FText::FromString(Path), FSlateIcon(),
                FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::ExecutePackPath, Path)));
        }
        return Menu.MakeWidget();
    }

    FReply RunSelected()
    {
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        Results.Reset();
        if (UProbeTestAsset* Asset = SelectedAsset())
        {
            ExecuteAsset(Asset);
            FeedbackMessage = LOCTEXT("SelectedExecuted", "Selected test executed. See Results below.");
        }
        else { FeedbackMessage = LOCTEXT("NoSelectedTest", "Select a Probe Test asset first."); }
        RecordRunCompleted();
        ShowResults();
        return FReply::Handled();
    }

    FReply RunAll()
    {
        RefreshAssets();
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        Results.Reset();
        for (const TSharedPtr<FAssetData>& Entry : Assets)
        {
            if (Entry.IsValid()) { ExecuteAsset(Cast<UProbeTestAsset>(Entry->GetAsset())); }
        }
        FeedbackMessage = LOCTEXT("AllExecuted", "Test run complete. See Results below.");
        RecordRunCompleted();
        ShowResults();
        return FReply::Handled();
    }

    FReply RerunProblems()
    {
        TArray<FString> Paths;
        for (const Probe::FTestResult& Result : Results)
        {
            if (Result.Status == Probe::EResultStatus::Failed || Result.Status == Probe::EResultStatus::Error)
            {
                Paths.AddUnique(Result.AssetPath);
            }
        }
        Results.Reset();
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        for (const FString& Path : Paths)
        {
            if (UProbeTestAsset* Asset = Cast<UProbeTestAsset>(FindObject<UObject>(nullptr, *Path)))
            {
                ExecuteAsset(Asset);
            }
            else if (UProbeTestAsset* LoadedAsset = LoadObject<UProbeTestAsset>(nullptr, *Path))
            {
                ExecuteAsset(LoadedAsset);
            }
            else
            {
                Probe::FTestResult Missing;
                Missing.Name = Path;
                Missing.AssetPath = Path;
                Missing.Status = Probe::EResultStatus::Error;
                Missing.Detail = TEXT("Test asset could not be loaded for rerun");
                Results.Add(MoveTemp(Missing));
            }
        }
        FeedbackMessage = FText::Format(LOCTEXT("ProblemsRerun", "Reran {0} previously failed/error test(s)."), FText::AsNumber(Paths.Num()));
        RecordRunCompleted();
        ShowResults();
        return FReply::Handled();
    }

    void RecordRunCompleted()
    {
        LastRunTime = FDateTime::Now();
        LastRunDurationMs = 0.0;
        for (const Probe::FTestResult& Result : Results) { LastRunDurationMs += Result.DurationMs; }
        FeedbackMessage = LOCTEXT("RunCompleted", "Run complete. Select a result for diagnostics.");
    }

    FReply ExportReport()
    {
        if (Results.IsEmpty()) { return FReply::Handled(); }
        TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
        Root->SetStringField(TEXT("format"), TEXT("ProbeReport/1"));
        Root->SetStringField(TEXT("timestamp"), FDateTime::Now().ToIso8601());
        Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
        TArray<TSharedPtr<FJsonValue>> Tests;
        for (const Probe::FTestResult& Test : Results)
        {
            TSharedRef<FJsonObject> TestJson = MakeShared<FJsonObject>();
            TestJson->SetStringField(TEXT("test_id"), Test.Id);
            TestJson->SetStringField(TEXT("name"), Test.Name);
            TestJson->SetStringField(TEXT("asset_path"), Test.AssetPath);
            TestJson->SetStringField(TEXT("status"), Probe::StatusName(Test.Status));
            TestJson->SetStringField(TEXT("detail"), Test.Detail);
            TestJson->SetNumberField(TEXT("duration_ms"), Test.DurationMs);
            TArray<TSharedPtr<FJsonValue>> Steps;
            for (const Probe::FStepResult& Step : Test.Steps)
            {
                TSharedRef<FJsonObject> StepJson = MakeShared<FJsonObject>();
                StepJson->SetNumberField(TEXT("index"), Step.Index);
                StepJson->SetStringField(TEXT("assertion_id"), Step.AssertionId);
                StepJson->SetStringField(TEXT("status"), Probe::StatusName(Step.Status));
                StepJson->SetStringField(TEXT("description"), Step.Description);
                StepJson->SetStringField(TEXT("detail"), Step.Detail);
                StepJson->SetNumberField(TEXT("duration_ms"), Step.DurationMs);
                Steps.Add(MakeShared<FJsonValueObject>(StepJson));
            }
            TestJson->SetArrayField(TEXT("steps"), Steps);
            Tests.Add(MakeShared<FJsonValueObject>(TestJson));
        }
        Root->SetArrayField(TEXT("tests"), Tests);
        FString Json;
        const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
        if (!FJsonSerializer::Serialize(Root, Writer))
        {
            FeedbackMessage = LOCTEXT("ReportSerializeFailed", "Report serialization failed.");
            return FReply::Handled();
        }
        const FString ReportDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Probe"), TEXT("Reports"));
        IFileManager::Get().MakeDirectory(*ReportDirectory, true);
        const FString FileName = FString::Printf(TEXT("Probe-%s-%s.json"), *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6));
        const FString FilePath = FPaths::Combine(ReportDirectory, FileName);
        if (FFileHelper::SaveStringToFile(Json, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            FeedbackMessage = FText::FromString(FString::Printf(TEXT("Report exported: %s"), *FilePath));
        }
        else
        {
            FeedbackMessage = FText::FromString(FString::Printf(TEXT("Failed to write report: %s"), *FilePath));
        }
        return FReply::Handled();
    }

};


// Pack authoring lives in a dedicated dockable Unreal Editor tab. It never
// duplicates Test Assets: a Pack stores only soft references to existing tests.
class SProbePackWorkspace : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SProbePackWorkspace) {}
    SLATE_END_ARGS()

    void Construct(const FArguments&)
    {
        RefreshPacks();
        ChildSlot
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(8, 6)
            [ SNew(SHorizontalBox)
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)
              [ SNew(SButton).ToolTipText(LOCTEXT("NewPackTooltip", "Create a new Probe Pack asset."))
                .OnClicked(this, &SProbePackWorkspace::NewPack)
                [ SNew(STextBlock).Text(LOCTEXT("CreatePackInManager", "+ New Pack")) ] ]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,12,0)
              [ SNew(SButton).ToolTipText(LOCTEXT("SavePackTooltip", "Save changes to this Pack (Ctrl+S)."))
                .OnClicked(this, &SProbePackWorkspace::SavePack)
                .IsEnabled_Lambda([this]() { const UProbeTestPack* Pack = GetPack(); return Pack && Pack->GetOutermost()->IsDirty(); })
                [ SNew(SHorizontalBox)
                  + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
                  [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Save")) ]
                  + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                  [ SNew(STextBlock).Text(LOCTEXT("SaveCurrentPack", "Save")) ] ] ]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)
              [ SNew(SButton).OnClicked(this, &SProbePackWorkspace::RunPack)
                .ToolTipText(LOCTEXT("RunPackTooltip", "Run the tests in this Pack in their listed order, and show results in Probe."))
                .IsEnabled_Lambda([this]() { const UProbeTestPack* Pack = GetPack(); return Pack && !Pack->Tests.IsEmpty(); })
                [ SNew(SHorizontalBox)
                  + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
                  [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Play")) ]
                  + SHorizontalBox::Slot().AutoWidth()
                  [ SNew(STextBlock).Text(LOCTEXT("RunSelectedPack", "Run Pack")) ] ] ]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,10,0)
              [ SNew(SButton).OnClicked(this, &SProbePackWorkspace::RunSelectedTests)
                .IsEnabled_Lambda([this]() { return GetPack() && !SelectedMemberPaths.IsEmpty(); })
                .ToolTipText(LOCTEXT("RunSelectedPackTip", "Run only the checked tests in this Pack, preserving Pack order."))
                [ SNew(STextBlock).Text(LOCTEXT("RunSelectedPackLabel", "Run Selected")) ] ]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
              [ SNew(SButton).OnClicked(this, &SProbePackWorkspace::ValidateCurrentReferences)
                .ToolTipText(LOCTEXT("ValidateCurrentPackTip", "Validate only the selected Pack's test references."))
                .IsEnabled_Lambda([this]() { return GetPack() != nullptr; })
                [ SNew(STextBlock).Text(LOCTEXT("ValidateThisPack", "Validate Pack")) ] ]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
              [ SNew(SButton).OnClicked(this, &SProbePackWorkspace::ValidateAllReferences)
                .ToolTipText(LOCTEXT("ValidateAllPackTip", "Check all Packs and duplicate stable Test IDs across the project."))
                [ SNew(STextBlock).Text(LOCTEXT("ValidateAllPacks", "Validate All")) ] ]
              + SHorizontalBox::Slot().FillWidth(1) [ SNullWidget::NullWidget ]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
              [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                .ToolTipText(LOCTEXT("RefreshPacksTip", "Refresh the available Probe Packs."))
                .OnClicked_Lambda([this]() { RefreshPacks(); return FReply::Handled(); })
                [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Refresh")) ] ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 8)
            [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(9)
              [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,12,0)
                [ SNew(SImage).Image_Lambda([]()
                    { const ISlateStyle* Style = FSlateStyleRegistry::FindSlateStyle(TEXT("ProbeAssetAppearance"));
                      return Style ? Style->GetBrush(TEXT("Probe.PackIcon40")) : FAppStyle::GetBrush(TEXT("Icons.FolderClosed")); }) ]
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [ SNew(SVerticalBox)
                  + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,4)
                  [ SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [ SNew(STextBlock).Text(LOCTEXT("SelectedPackLabel", "Probe Pack"))
                      .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
                    + SHorizontalBox::Slot().AutoWidth().Padding(8,0).VAlign(VAlign_Center)
                    [ SNew(STextBlock).Text_Lambda([this]()
                      { const UProbeTestPack* Pack = GetPack();
                        return Pack && Pack->GetOutermost()->IsDirty() ? FText::FromString(TEXT("* Unsaved")) : FText::GetEmpty(); })
                      .ColorAndOpacity(FLinearColor(0.93f,0.7f,0.3f)) ] ]
                  + SVerticalBox::Slot().AutoHeight()
                  [ SAssignNew(PackSelector, SComboBox<TSharedPtr<FAssetData>>)
                    .OptionsSource(&Packs)
                    .OnGenerateWidget_Lambda([](TSharedPtr<FAssetData> Item)
                    { return SNew(STextBlock).Text(FText::FromName(Item.IsValid() ? Item->AssetName : NAME_None)); })
                    .OnSelectionChanged_Lambda([this](TSharedPtr<FAssetData> Item, ESelectInfo::Type)
                    { SelectedPack = Item; SelectedMemberPaths.Reset(); RebuildMembers(); })
                    [ SNew(STextBlock).Text_Lambda([this]()
                    { return FText::FromString(SelectedPack.IsValid() ? SelectedPack->AssetName.ToString() : TEXT("Create or choose a Probe Pack")); }) ] ]
                  + SVerticalBox::Slot().AutoHeight().Padding(0,4,0,0)
                  [ SNew(STextBlock).Text_Lambda([this]()
                    { return FText::FromString(SelectedPack.IsValid() ? SelectedPack->PackagePath.ToString() : TEXT("No Pack selected")); })
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12,0,0,0)
                [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                  .ToolTipText(LOCTEXT("BrowsePackAssetTip", "Show the current Probe Pack in the Content Browser."))
                  .IsEnabled_Lambda([this]() { return SelectedPack.IsValid(); })
                  .OnClicked(this, &SProbePackWorkspace::BrowseToPack)
                  [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.BrowseContent")) ] ] ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(9,0,9,8)
            [ SNew(SHorizontalBox)
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
              [ SNew(SBox).WidthOverride(90.0f)
                [ SNew(STextBlock).Text(LOCTEXT("PackDescriptionLabel", "Description")) ] ]
              + SHorizontalBox::Slot().FillWidth(1.0f)
              [ SNew(SEditableTextBox)
                .IsEnabled_Lambda([this]() { return GetPack() != nullptr; })
                .HintText(LOCTEXT("PackDescriptionHint", "Describe what this Pack tests..."))
                .Text_Lambda([this]() { const UProbeTestPack* Pack = GetPack(); return Pack ? FText::FromString(Pack->Description) : FText::GetEmpty(); })
                .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type)
                {
                    UProbeTestPack* Pack = GetPack();
                    if (!Pack || Pack->Description == Text.ToString()) { return; }
                    const FScopedTransaction Transaction(LOCTEXT("EditPackDescriptionTransaction", "Edit Probe Pack Description"));
                    Pack->Modify(); Pack->Description = Text.ToString(); Pack->MarkPackageDirty(); InvalidateAudit();
                }) ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(9,4,9,5)
            [ SNew(SHorizontalBox)
              + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
              [ SNew(STextBlock).Text_Lambda([this]()
                { const UProbeTestPack* Pack = GetPack(); return FText::FromString(FString::Printf(TEXT("Tests (%d)"), Pack ? Pack->Tests.Num() : 0)); }) ]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)
              [ SNew(SButton).IsEnabled_Lambda([this]() { return GetPack() && !SelectedMemberPaths.IsEmpty(); })
                .ToolTipText(LOCTEXT("RemoveSelectedPackTestsTip", "Remove the selected references from this Pack. The Test Assets are not deleted."))
                .OnClicked(this, &SProbePackWorkspace::RemoveSelectedTests)
                [ SNew(STextBlock).Text(LOCTEXT("RemoveSelectedPackTests", "Remove Selected")) ] ]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,6,0)
              [ SNew(SButton).IsEnabled_Lambda([this]() { return GetPack() != nullptr; })
                .ToolTipText(LOCTEXT("CreateTestFromPackTip", "Create a new Probe Test asset and add it to this Pack."))
                .OnClicked(this, &SProbePackWorkspace::CreateTestInPack)
                [ SNew(STextBlock).Text(LOCTEXT("CreateTestFromPack", "New Test")) ] ]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
              [ SNew(SButton).IsEnabled_Lambda([this]() { return GetPack() != nullptr; })
                .ToolTipText(LOCTEXT("OpenBulkAddTip", "Find and add multiple existing Probe Tests at once."))
                .OnClicked(this, &SProbePackWorkspace::OpenAddTestsDialog)
                [ SNew(STextBlock).Text(LOCTEXT("BulkAddPackTests", "+ Add Tests...")) ] ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(9,0,9,5)
            [ SAssignNew(MemberSearch, SSearchBox)
              .HintText(LOCTEXT("PackListSearchHint", "Search tests already in this Pack..."))
              .OnTextChanged_Lambda([this](const FText& Text)
              { MemberQuery = Text.ToString(); RebuildMembers(); }) ]
            + SVerticalBox::Slot().FillHeight(1).Padding(8,3)
            [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(4)
              [ SNew(SScrollBox) + SScrollBox::Slot() [ SAssignNew(MemberRows, SVerticalBox) ] ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(9,4,9,0)
            [ SNew(SExpandableArea).InitiallyCollapsed(true)
              .HeaderContent()
              [ SNew(STextBlock).Text_Lambda([this]()
                { return FText::FromString(bHasAudit ? TEXT("Reference Diagnostics (validated)") : TEXT("Reference Diagnostics")); }) ]
              .BodyContent()
              [ SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0,2,0,4)
                [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                  .IsEnabled_Lambda([this]() { return bHasAudit; })
                  .OnClicked_Lambda([this]()
                  { if (AuditOutput.IsValid()) { FPlatformApplicationMisc::ClipboardCopy(*AuditOutput->GetText().ToString()); }
                    return FReply::Handled(); })
                  [ SNew(STextBlock).Text(LOCTEXT("CopyPackValidation", "Copy Report")) ] ]
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(SBox).HeightOverride(115.0f)
                  [ SAssignNew(AuditOutput, SMultiLineEditableTextBox)
                    .IsReadOnly(true)
                    .Text(LOCTEXT("ReferenceAuditHint", "Use Validate Pack for the selected Pack or Validate All for project-wide diagnostics.")) ] ] ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(9,5)
            [ SNew(STextBlock)
              .Text_Lambda([this]() { return Message.IsEmpty()
                ? LOCTEXT("PackHelp", "Add existing tests, arrange their run order, then save and run. Tests remain independent assets.")
                : FText::FromString(Message); })
              .AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
        ];
        if (SelectedPack.IsValid() && PackSelector.IsValid()) { PackSelector->SetSelectedItem(SelectedPack); }
        RebuildMembers();
        FEditorDelegates::PostUndoRedo.AddSP(this, &SProbePackWorkspace::OnUndoRedo);
    }
    virtual ~SProbePackWorkspace() override
    {
        FEditorDelegates::PostUndoRedo.RemoveAll(this);
    }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override
    {
        if (Event.IsControlDown() && Event.GetKey() == EKeys::S && GetPack()) { return SavePack(); }
        return SCompoundWidget::OnKeyDown(Geometry, Event);
    }
    virtual FReply OnDragOver(const FGeometry& Geometry, const FDragDropEvent& Event) override
    {
        const TSharedPtr<FAssetDragDropOp> Dragged = Event.GetOperationAs<FAssetDragDropOp>();
        if (GetPack() && Dragged.IsValid())
        {
            for (const FAssetData& Asset : Dragged->GetAssets())
            { if (Asset.AssetClassPath == UProbeTestAsset::StaticClass()->GetClassPathName()) { return FReply::Handled(); } }
        }
        return FReply::Unhandled();
    }
    virtual FReply OnDrop(const FGeometry& Geometry, const FDragDropEvent& Event) override
    {
        const TSharedPtr<FAssetDragDropOp> Dragged = Event.GetOperationAs<FAssetDragDropOp>();
        UProbeTestPack* Pack = GetPack();
        if (!Dragged.IsValid() || !Pack) { return FReply::Unhandled(); }
        TArray<FString> Paths;
        for (const FAssetData& Asset : Dragged->GetAssets())
        {
            if (Asset.AssetClassPath == UProbeTestAsset::StaticClass()->GetClassPathName())
            { Paths.AddUnique(Asset.GetObjectPathString()); }
        }
        if (Paths.IsEmpty()) { return FReply::Unhandled(); }
        AddTests(Paths);
        return FReply::Handled();
    }
    void OnUndoRedo() { InvalidateAudit(); RebuildMembers(); }

    void SelectPackPath(const FString& Path)
    {
        RefreshPacks();
        for (const TSharedPtr<FAssetData>& Data : Packs)
        {
            if (Data.IsValid() && Data->GetObjectPathString() == Path)
            {
                SelectedPack = Data;
                if (PackSelector.IsValid()) { PackSelector->SetSelectedItem(Data); }
                RebuildMembers(); return;
            }
        }
    }
private:
    TArray<TSharedPtr<FAssetData>> Packs;
    TSharedPtr<FAssetData> SelectedPack;
    TSharedPtr<SComboBox<TSharedPtr<FAssetData>>> PackSelector;
    TSharedPtr<SVerticalBox> MemberRows;
    TSharedPtr<SMultiLineEditableTextBox> AuditOutput;
    TSharedPtr<SSearchBox> MemberSearch;
    FString MemberQuery;
    TSet<FString> SelectedMemberPaths;
    TArray<FAssetData> PickerCandidates;
    TSet<FString> PickerSelection;
    TSharedPtr<SVerticalBox> PickerRows;
    FString PickerQuery;
    bool bPickerReadyOnly = false;
    bool bHasAudit = false;
    FString Message;

    UProbeTestPack* GetPack() const
    {
        return SelectedPack.IsValid() ? Cast<UProbeTestPack>(SelectedPack->GetAsset()) : nullptr;
    }

    void InvalidateAudit()
    {
        bHasAudit = false;
        if (AuditOutput.IsValid()) { AuditOutput->SetText(LOCTEXT("PackAuditStale", "Pack changed. Validate again for current results.")); }
    }
    FReply BrowseToPack()
    {
        if (SelectedPack.IsValid())
        {
            TArray<FAssetData> Assets; Assets.Add(*SelectedPack);
            FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().SyncBrowserToAssets(Assets);
        }
        return FReply::Handled();
    }
    FReply ValidateCurrentReferences()
    {
        const UProbeTestPack* Pack = GetPack();
        if (!Pack) { return FReply::Handled(); }
        const Probe::FReferenceAudit Report = Probe::AuditPackReferences(Pack);
        if (AuditOutput.IsValid()) { AuditOutput->SetText(FText::FromString(Report.ToText())); }
        bHasAudit = true;
        Message = FString::Printf(TEXT("Validated Pack: %d reference(s), %d issue(s)."), Report.ReferencesChecked, Report.Problems);
        RebuildMembers();
        return FReply::Handled();
    }
    void RefreshPacks()
    {
        const FString Previous = SelectedPack.IsValid() ? SelectedPack->GetObjectPathString() : FString();
        Packs.Reset();
        FARFilter Filter;
        Filter.ClassPaths.Add(UProbeTestPack::StaticClass()->GetClassPathName());
        TArray<FAssetData> Found;
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssets(Filter, Found);
        Found.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
        for (const FAssetData& Data : Found) { Packs.Add(MakeShared<FAssetData>(Data)); }
        SelectedPack.Reset();
        for (const TSharedPtr<FAssetData>& Data : Packs)
        {
            if (Data.IsValid() && Data->GetObjectPathString() == Previous) { SelectedPack = Data; break; }
        }
        if (!SelectedPack.IsValid() && !Packs.IsEmpty()) { SelectedPack = Packs[0]; }
        if (PackSelector.IsValid())
        {
            PackSelector->RefreshOptions();
            if (SelectedPack.IsValid()) { PackSelector->SetSelectedItem(SelectedPack); }
        }
        RebuildMembers();
    }

    FReply ValidateAllReferences()
    {
        const Probe::FReferenceAudit Report = Probe::AuditAllReferences();
        if (AuditOutput.IsValid()) { AuditOutput->SetText(FText::FromString(Report.ToText())); }
        bHasAudit = true;
        Message = FString::Printf(TEXT("Checked %d Packs and %d references. %d problems found."),
            Report.PacksChecked, Report.ReferencesChecked, Report.Problems);
        RebuildMembers();
        return FReply::Handled();
    }

    FReply NewPack()
    {
        UProbeTestPackFactory* Factory = NewObject<UProbeTestPackFactory>();
        UObject* Created = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get()
            .CreateAssetWithDialog(TEXT("Probe_Pack"), TEXT("/Game/ProbeTests"), UProbeTestPack::StaticClass(), Factory);
        if (Created)
        {
            SelectPackPath(Created->GetPathName());
            Message = TEXT("Pack created. Choose Add Test, then Save Pack and Run Pack.");
        }
        return FReply::Handled();
    }

    FReply SavePack()
    {
        UProbeTestPack* Pack = GetPack();
        if (!Pack) { return FReply::Handled(); }
        TArray<UPackage*> Packages;
        Packages.Add(Pack->GetOutermost());
        const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(Packages, true);
        Message = bSaved && !Pack->GetOutermost()->IsDirty() ? TEXT("Pack saved.") : TEXT("Pack could not be saved. Check Output Log.");
        return FReply::Handled();
    }

    FReply RunPack()
    {
        const UProbeTestPack* Pack = GetPack();
        if (Pack) { Probe::RunPackInWorkspace(Pack->GetPathName()); }
        return FReply::Handled();
    }

    FReply CreateTestInPack()
    {
        UProbeTestPack* Pack = GetPack();
        if (!Pack) { return FReply::Handled(); }
        UProbeTestAssetFactory* Factory = NewObject<UProbeTestAssetFactory>();
        const FString Folder = SelectedPack.IsValid() ? SelectedPack->PackagePath.ToString() : TEXT("/Game/ProbeTests");
        UObject* NewObjectAsset = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get()
            .CreateAssetWithDialog(TEXT("Probe_Test"), Folder, UProbeTestAsset::StaticClass(), Factory);
        if (UProbeTestAsset* Test = Cast<UProbeTestAsset>(NewObjectAsset))
        {
            Test->Modify(); Test->bNeedsConfiguration = true; Test->MarkPackageDirty();
            TArray<FString> Added; Added.Add(Test->GetPathName()); AddTests(Added);
            OpenPackTest(Test->GetPathName());
        }
        return FReply::Handled();
    }
    FReply RunSelectedTests()
    {
        const UProbeTestPack* Pack = GetPack();
        if (!Pack) { return FReply::Handled(); }
        TArray<FString> Paths;
        for (const TSoftObjectPtr<UProbeTestAsset>& Ref : Pack->Tests)
        {
            const FString Path = Ref.ToString();
            if (SelectedMemberPaths.Contains(Path)) { Paths.Add(Path); }
        }
        if (!Paths.IsEmpty()) { Probe::RunTestsInWorkspace(Paths); }
        return FReply::Handled();
    }
    void AddTests(const TArray<FString>& Paths)
    {
        UProbeTestPack* Pack = GetPack();
        if (!Pack || Paths.IsEmpty()) { return; }
        const FScopedTransaction Transaction(LOCTEXT("BulkAddPackTestsTransaction", "Add Tests To Probe Pack"));
        Pack->Modify();
        int32 Added = 0;
        for (const FString& Path : Paths)
        {
            if (Path.IsEmpty()) { continue; }
            if (!Probe::InspectTestReference(FSoftObjectPath(Path)).IsUsable()) { continue; }
            const bool bExists = Pack->Tests.ContainsByPredicate([&Path](const TSoftObjectPtr<UProbeTestAsset>& Ref)
            { return Ref.ToString() == Path; });
            if (!bExists)
            {
                Pack->Tests.Add(TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(Path)));
                ++Added;
            }
        }
        if (Added > 0) { Pack->MarkPackageDirty(); InvalidateAudit(); }
        Message = FString::Printf(TEXT("Added %d test(s). Save Pack to persist changes."), Added);
        RebuildMembers();
    }
    void RebuildPickerRows()
    {
        if (!PickerRows.IsValid()) { return; }
        PickerRows->ClearChildren();
        int32 Shown = 0;
        for (const FAssetData& Candidate : PickerCandidates)
        {
            const FString Path = Candidate.GetObjectPathString();
            if (!PickerQuery.IsEmpty() && !Candidate.AssetName.ToString().Contains(PickerQuery, ESearchCase::IgnoreCase)
                && !Candidate.PackagePath.ToString().Contains(PickerQuery, ESearchCase::IgnoreCase)) { continue; }
            // A readiness filter only loads existing, registry-validated Test Assets.
            const UProbeTestAsset* Test = Cast<UProbeTestAsset>(Candidate.GetAsset());
            const bool bReady = Test && !Test->bNeedsConfiguration && Probe::TestConfigurationIssue(*Test).IsEmpty();
            if (bPickerReadyOnly && !bReady) { continue; }
            ++Shown;
            PickerRows->AddSlot().AutoHeight().Padding(3,2)
            [ SNew(SCheckBox)
              .IsChecked_Lambda([this, Path]() { return PickerSelection.Contains(Path) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
              .OnCheckStateChanged_Lambda([this, Path](ECheckBoxState State)
              { if (State == ECheckBoxState::Checked) { PickerSelection.Add(Path); } else { PickerSelection.Remove(Path); } })
              [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1)
                [ SNew(SVerticalBox)
                  + SVerticalBox::Slot().AutoHeight()
                  [ SNew(STextBlock).Text(FText::FromName(Candidate.AssetName)) ]
                  + SVerticalBox::Slot().AutoHeight()
                  [ SNew(STextBlock).Text(FText::FromName(Candidate.PackagePath))
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(10,0,0,0)
                [ SNew(STextBlock).Text(FText::FromString(bReady ? TEXT("Ready") : TEXT("Needs Setup")))
                  .ColorAndOpacity(bReady ? FSlateColor(FLinearColor(0.32f,0.72f,0.48f)) : FSlateColor::UseSubduedForeground()) ] ] ];
        }
        if (Shown == 0)
        {
            PickerRows->AddSlot().AutoHeight().Padding(12)
            [ SNew(STextBlock).Text(LOCTEXT("PackPickerNothingFound", "No matching tests available. Clear filters or create a Probe Test."))
              .AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground()) ];
        }
    }
    FReply SelectVisiblePickerTests()
    {
        for (const FAssetData& Candidate : PickerCandidates)
        {
            if (!PickerQuery.IsEmpty() && !Candidate.AssetName.ToString().Contains(PickerQuery, ESearchCase::IgnoreCase)
                && !Candidate.PackagePath.ToString().Contains(PickerQuery, ESearchCase::IgnoreCase)) { continue; }
            if (bPickerReadyOnly)
            {
                const UProbeTestAsset* Test = Cast<UProbeTestAsset>(Candidate.GetAsset());
                if (!Test || Test->bNeedsConfiguration || !Probe::TestConfigurationIssue(*Test).IsEmpty()) { continue; }
            }
            PickerSelection.Add(Candidate.GetObjectPathString());
        }
        RebuildPickerRows(); return FReply::Handled();
    }
    FReply ClearPickerSelection()
    {
        PickerSelection.Reset(); RebuildPickerRows(); return FReply::Handled();
    }
    FReply OpenAddTestsDialog()
    {
        const UProbeTestPack* Pack = GetPack();
        if (!Pack) { return FReply::Handled(); }
        PickerCandidates.Reset(); PickerSelection.Reset(); PickerQuery.Reset(); bPickerReadyOnly = false;
        FARFilter Filter;
        Filter.ClassPaths.Add(UProbeTestAsset::StaticClass()->GetClassPathName());
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssets(Filter, PickerCandidates);
        PickerCandidates.RemoveAll([Pack](const FAssetData& Item)
        {
            const FString Path = Item.GetObjectPathString();
            return Pack->Tests.ContainsByPredicate([&Path](const TSoftObjectPtr<UProbeTestAsset>& Ref) { return Ref.ToString() == Path; });
        });
        PickerCandidates.Sort([](const FAssetData& A, const FAssetData& B)
        { return A.AssetName.LexicalLess(B.AssetName); });
        TSharedRef<SWindow> Dialog = SNew(SWindow)
            .Title(LOCTEXT("AddTestsToPackTitle", "Add Tests to Probe Pack"))
            .ClientSize(FVector2D(700.0f, 540.0f))
            .SupportsMinimize(false).SupportsMaximize(true).SizingRule(ESizingRule::UserSized);
        Dialog->SetContent(
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(12,10)
            [ SNew(SSearchBox)
              .HintText(LOCTEXT("BulkPackSearch", "Search tests by name or folder..."))
              .OnTextChanged_Lambda([this](const FText& Text) { PickerQuery = Text.ToString(); RebuildPickerRows(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(12,0,12,8)
            [ SNew(SCheckBox).OnCheckStateChanged_Lambda([this](ECheckBoxState State)
              { bPickerReadyOnly = (State == ECheckBoxState::Checked); RebuildPickerRows(); })
              [ SNew(STextBlock).Text(LOCTEXT("BulkPackReadyFilter", "Show only tests marked Ready")) ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(12,0,12,7)
            [ SNew(SHorizontalBox)
              + SHorizontalBox::Slot().AutoWidth().Padding(0,0,7,0)
              [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                .OnClicked(this, &SProbePackWorkspace::SelectVisiblePickerTests)
                [ SNew(STextBlock).Text(LOCTEXT("PackSelectVisible", "Select Visible")) ] ]
              + SHorizontalBox::Slot().AutoWidth()
              [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                .OnClicked(this, &SProbePackWorkspace::ClearPickerSelection)
                [ SNew(STextBlock).Text(LOCTEXT("PackClearSelection", "Clear Selection")) ] ] ]
            + SVerticalBox::Slot().FillHeight(1.0f).Padding(12,0,12,9)
            [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(5)
              [ SNew(SScrollBox) + SScrollBox::Slot()
                [ SAssignNew(PickerRows, SVerticalBox) ] ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(12,0,12,12)
            [ SNew(SHorizontalBox)
              + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
              [ SNew(STextBlock).Text_Lambda([this]()
                { return FText::FromString(FString::Printf(TEXT("%d selected | %d available"), PickerSelection.Num(), PickerCandidates.Num())); })
                .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
              + SHorizontalBox::Slot().AutoWidth().Padding(0,0,8,0)
              [ SNew(SButton).OnClicked_Lambda([Dialog]() { Dialog->RequestDestroyWindow(); return FReply::Handled(); })
                [ SNew(STextBlock).Text(LOCTEXT("CancelPackAdd", "Cancel")) ] ]
              + SHorizontalBox::Slot().AutoWidth()
              [ SNew(SButton).IsEnabled_Lambda([this]() { return !PickerSelection.IsEmpty(); })
                .OnClicked_Lambda([this, Dialog]()
                { TArray<FString> Paths = PickerSelection.Array(); AddTests(Paths); Dialog->RequestDestroyWindow();
                  return FReply::Handled(); })
                [ SNew(STextBlock).Text(LOCTEXT("ConfirmPackAdd", "Add Selected")) ] ] ]);
        RebuildPickerRows();
        FSlateApplication::Get().AddModalWindow(Dialog, FSlateApplication::Get().GetActiveTopLevelWindow(), false);
        PickerRows.Reset(); PickerCandidates.Reset(); PickerSelection.Reset();
        return FReply::Handled();
    }

    TSharedRef<SWidget> BuildReplacementPicker(int32 Index)
    {
        FARFilter Filter;
        Filter.ClassPaths.Add(UProbeTestAsset::StaticClass()->GetClassPathName());
        TArray<FAssetData> Found;
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssets(Filter, Found);
        Found.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
        TArray<FProbeIdentifierChoice> Candidates;
        for (const FAssetData& Data : Found)
        {
            FProbeIdentifierChoice Choice;
            Choice.Id = Data.GetObjectPathString();
            Choice.Display = Data.AssetName.ToString();
            Choice.Owner = Data.PackagePath.ToString();
            Choice.State = TEXT("Replace Reference");
            Candidates.Add(MoveTemp(Choice));
        }
        return SNew(SProbeIdentifierPicker)
            .Candidates(Candidates)
            .SearchHint(LOCTEXT("PackReplacementSearch", "Find the moved or replacement Probe Test..."))
            .OnPicked(FOnProbeIdentifierPicked::CreateSP(this, &SProbePackWorkspace::ReplaceTest, Index));
    }

    void ReplaceTest(const FString& NewPath, int32 Index)
    {
        UProbeTestPack* Pack = GetPack();
        if (!Pack || !Pack->Tests.IsValidIndex(Index) || NewPath.IsEmpty()) { return; }
        if (!Probe::InspectTestReference(FSoftObjectPath(NewPath)).IsUsable())
        { Message = TEXT("Replacement is not a valid Probe Test."); return; }
        for (int32 Other = 0; Other < Pack->Tests.Num(); ++Other)
        {
            if (Other != Index && Pack->Tests[Other].ToString() == NewPath)
            { Message = TEXT("That test is already referenced by this Pack."); return; }
        }
        const FScopedTransaction Transaction(LOCTEXT("ReplacePackReferenceTransaction", "Replace Probe Pack Reference"));
        Pack->Modify();
        const FString PreviousPath = Pack->Tests[Index].ToString();
        Pack->Tests[Index] = TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(NewPath));
        SelectedMemberPaths.Remove(PreviousPath);
        Pack->MarkPackageDirty(); InvalidateAudit();
        Message = TEXT("Reference replaced. Save Pack to persist this change.");
        RebuildMembers();
    }

    FReply RemoveTest(int32 Index)
    {
        UProbeTestPack* Pack = GetPack();
        if (!Pack || !Pack->Tests.IsValidIndex(Index)) { return FReply::Handled(); }
        const FString Path = Pack->Tests[Index].ToString();
        const FScopedTransaction Transaction(LOCTEXT("RemoveTestFromPackTransaction", "Remove Test From Probe Pack"));
        Pack->Modify(); Pack->Tests.RemoveAt(Index); Pack->MarkPackageDirty();
        SelectedMemberPaths.Remove(Path); InvalidateAudit();
        Message = TEXT("Removed from Pack. The original Test Asset was not deleted.");
        RebuildMembers();
        return FReply::Handled();
    }
    FReply RemoveSelectedTests()
    {
        UProbeTestPack* Pack = GetPack();
        if (!Pack || SelectedMemberPaths.IsEmpty()) { return FReply::Handled(); }
        const FScopedTransaction Transaction(LOCTEXT("RemoveSelectedPackReferences", "Remove Selected Tests From Probe Pack"));
        Pack->Modify();
        const int32 Removed = Pack->Tests.RemoveAll([this](const TSoftObjectPtr<UProbeTestAsset>& Ref)
        { return SelectedMemberPaths.Contains(Ref.ToString()); });
        if (Removed > 0) { Pack->MarkPackageDirty(); InvalidateAudit(); }
        SelectedMemberPaths.Reset();
        Message = FString::Printf(TEXT("Removed %d reference(s). Original Test Assets were not deleted."), Removed);
        RebuildMembers(); return FReply::Handled();
    }
    FReply ReorderTest(int32 Index, int32 Direction)
    {
        UProbeTestPack* Pack = GetPack();
        if (!Pack || !Pack->Tests.IsValidIndex(Index) || !Pack->Tests.IsValidIndex(Index + Direction)) { return FReply::Handled(); }
        const FScopedTransaction Transaction(LOCTEXT("ReorderPackTest", "Reorder Probe Pack Tests"));
        Pack->Modify(); Pack->Tests.Swap(Index, Index + Direction); Pack->MarkPackageDirty(); InvalidateAudit();
        Message = TEXT("Execution order updated. Save Pack to persist changes.");
        RebuildMembers(); return FReply::Handled();
    }
    FReply OpenPackTest(const FString& Path)
    {
        if (!Path.IsEmpty() && Probe::InspectTestReference(FSoftObjectPath(Path)).IsUsable())
        { TArray<FString> Paths; Paths.Add(Path); Probe::OpenTestAssetsInWorkspace(Paths); }
        return FReply::Handled();
    }
    void RebuildMembers()
    {
        if (!MemberRows.IsValid()) { return; }
        MemberRows->ClearChildren();
        const UProbeTestPack* Pack = GetPack();
        if (!Pack)
        {
            MemberRows->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(18,38)
            [ SNew(STextBlock).Text(LOCTEXT("NoPackYet", "No Probe Pack selected. Choose New Pack to create one."))
              .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ];
            return;
        }
        if (Pack->Tests.IsEmpty())
        {
            MemberRows->AddSlot().AutoHeight().HAlign(HAlign_Center).Padding(18,35)
            [ SNew(SVerticalBox)
              + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,8)
              [ SNew(STextBlock).Text(LOCTEXT("EmptyPackHeader", "No tests in this Pack")) ]
              + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0,0,0,8)
              [ SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("EmptyPackHelp", "Add existing Probe Tests to build a reusable collection, or drag Test Assets here from the Content Browser."))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
              + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
              [ SNew(SButton).OnClicked(this, &SProbePackWorkspace::OpenAddTestsDialog)
                [ SNew(STextBlock).Text(LOCTEXT("EmptyPackAddTests", "+ Add Existing Tests")) ] ] ];
            return;
        }
        MemberRows->AddSlot().AutoHeight().Padding(5,3,5,6)
        [ SNew(SHorizontalBox)
          + SHorizontalBox::Slot().AutoWidth().Padding(0,0,12,0) [ SNew(STextBlock).Text(LOCTEXT("PackRowsOrder", "#")) ]
          + SHorizontalBox::Slot().FillWidth(1) [ SNew(STextBlock).Text(LOCTEXT("PackRowsTest", "Test / Asset Location")) ]
          + SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0) [ SNew(STextBlock).Text(LOCTEXT("PackRowsReady", "Readiness")) ]
          + SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0) [ SNew(STextBlock).Text(LOCTEXT("PackRowsState", "Reference")) ]
          + SHorizontalBox::Slot().AutoWidth().Padding(0,0,10,0) [ SNew(STextBlock).Text(LOCTEXT("PackRowsResult", "Last Result")) ]
          + SHorizontalBox::Slot().AutoWidth() [ SNew(STextBlock).Text(LOCTEXT("PackRowsActions", "Actions")) ] ];
        int32 Shown = 0;
        for (int32 Index = 0; Index < Pack->Tests.Num(); ++Index)
        {
            const FString Path = Pack->Tests[Index].ToString();
            const FString Name = Path.IsEmpty() ? TEXT("<Empty Reference>") : FPackageName::GetShortName(Path);
            if (!MemberQuery.IsEmpty() && !Name.Contains(MemberQuery, ESearchCase::IgnoreCase)
                && !Path.Contains(MemberQuery, ESearchCase::IgnoreCase)) { continue; }
            ++Shown;
            const Probe::FReferenceHealth Check = Probe::InspectTestReference(Pack->Tests[Index].ToSoftObjectPath());
            const bool bValid = Check.IsUsable();
            const UProbeTestAsset* Test = bValid ? Cast<UProbeTestAsset>(Pack->Tests[Index].LoadSynchronous()) : nullptr;
            const bool bReady = Test && !Test->bNeedsConfiguration && Probe::TestConfigurationIssue(*Test).IsEmpty();
            const FString Folder = FPackageName::GetLongPackagePath(Path);
            MemberRows->AddSlot().AutoHeight().Padding(3,1)
            [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(FMargin(5,4))
              [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,7,0)
                [ SNew(SCheckBox)
                  .IsChecked_Lambda([this,Path]() { return SelectedMemberPaths.Contains(Path) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                  .OnCheckStateChanged_Lambda([this,Path](ECheckBoxState State)
                  { if (State == ECheckBoxState::Checked) { SelectedMemberPaths.Add(Path); }
                    else { SelectedMemberPaths.Remove(Path); } }) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
                [ SNew(STextBlock).Text(FText::AsNumber(Index + 1)).ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
                + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                [ SNew(SVerticalBox)
                  + SVerticalBox::Slot().AutoHeight()
                  [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .ToolTipText(FText::FromString(Check.Explanation))
                    .OnClicked_Lambda([this,Path]() { return OpenPackTest(Path); })
                    .IsEnabled(bValid)
                    [ SNew(STextBlock).Text(FText::FromString(Name)) ] ]
                  + SVerticalBox::Slot().AutoHeight()
                  [ SNew(STextBlock).Text(FText::FromString(Folder))
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6,0,10,0)
                [ SNew(STextBlock).Text(FText::FromString(!bValid ? TEXT("Unavailable") :
                    bReady ? TEXT("Ready") : TEXT("Needs Setup")))
                  .ColorAndOpacity(bReady ? FSlateColor(FLinearColor(0.32f,0.72f,0.48f)) : FSlateColor::UseSubduedForeground()) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,10,0)
                [ SNew(STextBlock).Text(FText::FromString(bValid ? TEXT("Valid") :
                    (Check.Status == Probe::EReferenceHealth::Redirector ? TEXT("Redirector") : TEXT("Missing / Invalid"))))
                  .ToolTipText(FText::FromString(Check.Explanation))
                  .ColorAndOpacity(bValid ? FSlateColor(FLinearColor(0.32f,0.72f,0.48f)) : FSlateColor(FLinearColor(0.95f,0.45f,0.25f))) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,10,0)
                [ SNew(STextBlock).Text_Lambda([Path]() { return FText::FromString(Probe::RecentResultName(Path)); })
                  .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                  .ToolTipText(LOCTEXT("MovePackTestUp", "Move earlier in execution order"))
                  .IsEnabled(Index > 0).OnClicked_Lambda([this,Index]() { return ReorderTest(Index,-1); })
                  [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.ChevronUp")) ] ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                  .ToolTipText(LOCTEXT("MovePackTestDown", "Move later in execution order"))
                  .IsEnabled(Index + 1 < Pack->Tests.Num()).OnClicked_Lambda([this,Index]() { return ReorderTest(Index,1); })
                  [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.ChevronDown")) ] ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2,0)
                [ SNew(SComboButton)
                  .ToolTipText(LOCTEXT("ReplacePackReferenceTip", "Replace this reference with an existing Test Asset."))
                  .OnGetMenuContent_Lambda([this, Index]() { return BuildReplacementPicker(Index); })
                  .ButtonContent()[ SNew(STextBlock).Text(LOCTEXT("ReplacePackTestButton", "Replace")) ] ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                  .ToolTipText(LOCTEXT("RemovePackMember", "Remove from this Pack without deleting its Test Asset."))
                  .OnClicked_Lambda([this,Index]() { return RemoveTest(Index); })
                  [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Delete")) ] ] ] ];
        }
        if (!Shown)
        {
            MemberRows->AddSlot().AutoHeight().Padding(15)
            [ SNew(STextBlock).Text(LOCTEXT("NoMatchingPackTests", "No tests match the current search.")) ];
        }
    }

};

class FProbeEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        Probe::RegisterProbeAdvancedTemplates();
        Probe::RegisterProbeWorkflowTemplates();
        Probe::RegisterProbeActionTemplates();
        Probe::RegisterProbeContractTemplates();
        RegisterAssetAppearance();

        // Record only Probe assets affected by Undo/Redo. The post-transaction callback
        // restores package dirtiness after the engine has restored transaction package flags.
        FCoreUObjectDelegates::OnObjectTransacted.AddRaw(this, &FProbeEditorModule::OnObjectTransacted);
        FEditorDelegates::PostUndoRedo.AddRaw(this, &FProbeEditorModule::OnPostUndoRedo);
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(Probe::PackTabId,
            FOnSpawnTab::CreateRaw(this, &FProbeEditorModule::SpawnPackTab))
            .SetDisplayName(LOCTEXT("PacksTabName", "Probe Packs"))
            .SetIcon(FSlateIcon(TEXT("ProbeAssetAppearance"), TEXT("Probe.TabIcon")))
            .SetMenuType(ETabSpawnerMenuType::Hidden);
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(Probe::TabId,
            FOnSpawnTab::CreateRaw(this, &FProbeEditorModule::SpawnTab))
            .SetDisplayName(LOCTEXT("TabName", "Probe"))
            .SetIcon(FSlateIcon(TEXT("ProbeAssetAppearance"), TEXT("Probe.TabIcon")))
            .SetMenuType(ETabSpawnerMenuType::Hidden);
        UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FProbeEditorModule::RegisterMenus));
    }

    virtual void ShutdownModule() override
    {
        for (const FName Id : Probe::GetExtensionTemplateIds())
        {
            if (Id.ToString().StartsWith(TEXT("Probe."))) { Probe::UnregisterTemplate(Id); }
        }
        FEditorDelegates::PostUndoRedo.RemoveAll(this);
        FCoreUObjectDelegates::OnObjectTransacted.RemoveAll(this);
        PendingUndoAssets.Reset();
        PendingUndoPacks.Reset();
        if (UToolMenus::TryGet() != nullptr)
        {
            UToolMenus::UnRegisterStartupCallback(this);
            UToolMenus::UnregisterOwner(this);
        }
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(Probe::PackTabId);
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(Probe::TabId);
        if (AssetStyle.IsValid())
        {
            FSlateStyleRegistry::UnRegisterSlateStyle(*AssetStyle);
            AssetStyle.Reset();
        }
    }

public:
    /** Called by the Probe Test Asset Definition for standard Content Browser opening. */
    void OpenTestAssets(const TArray<FString>& Paths)
    {
        OpenTestsFromBrowser(Paths);
    }
    void OpenPackAssets(const TArray<FString>& Paths)
    {
        FGlobalTabmanager::Get()->TryInvokeTab(Probe::PackTabId);
        if (!Paths.IsEmpty())
        {
            if (TSharedPtr<SProbePackWorkspace> Panel = PackWorkspace.Pin()) { Panel->SelectPackPath(Paths[0]); }
        }
    }
    void RunPackByPath(const FString& Path)
    {
        if (TSharedPtr<SProbeWorkspace> Panel = OpenWorkspace()) { Panel->RunPackPath(Path); }
    }
    void RunTestPaths(const TArray<FString>& Paths)
    {
        if (TSharedPtr<SProbeWorkspace> Panel = OpenWorkspace()) { Panel->RunExplicitTestPaths(Paths); }
    }

private:
    TSharedPtr<FSlateStyleSet> AssetStyle;
    TWeakPtr<SProbeWorkspace> Workspace;
    TWeakPtr<SProbePackWorkspace> PackWorkspace;

    TSharedPtr<SProbeWorkspace> OpenWorkspace()
    {
        FGlobalTabmanager::Get()->TryInvokeTab(Probe::TabId);
        return Workspace.Pin();
    }

    void OpenTestsFromBrowser(const TArray<FString>& Paths)
    {
        if (Paths.IsEmpty()) { return; }
        if (TSharedPtr<SProbeWorkspace> Panel = OpenWorkspace())
        {
            Panel->SelectTestByPath(Paths[0]);
        }
    }

    void RunTestsFromBrowser(const TArray<FString>& Paths)
    {
        if (Paths.IsEmpty()) { return; }
        if (TSharedPtr<SProbeWorkspace> Panel = OpenWorkspace())
        {
            Panel->SelectTestByPath(Paths[0]);
            Panel->RunAssetPaths(Paths);
        }
    }

    void DuplicateTestsFromBrowser(const TArray<FString>& Paths)
    {
        IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        for (const FString& Path : Paths)
        {
            UProbeTestAsset* Original = LoadObject<UProbeTestAsset>(nullptr, *Path);
            if (!Original) { continue; }
            FString PackageName, AssetName;
            Tools.CreateUniqueAssetName(Original->GetOutermost()->GetName(), TEXT("_Copy"), PackageName, AssetName);
            if (UProbeTestAsset* Copy = Cast<UProbeTestAsset>(
                Tools.DuplicateAsset(AssetName, FPackageName::GetLongPackagePath(PackageName), Original)))
            {
                // Duplicating a serialized asset also duplicates its GUID. Reports need
                // an independent test identity, not two tests with the same stable ID.
                Copy->Modify();
                Copy->TestId = FGuid::NewGuid();
                // Self-targeting templates should still target the copied asset.
                for (FProbeTestStep& Step : Copy->Steps)
                {
                    if (Step.Target == Original->GetPathName())
                    {
                        Step.Target = Copy->GetPathName();
                    }
                }
                Copy->MarkPackageDirty();
            }
        }
        if (TSharedPtr<SProbeWorkspace> Panel = Workspace.Pin())
        {
            Panel->SelectTestByPath(Paths.IsEmpty() ? FString() : Paths[0]);
        }
    }

    void MarkTestsForReviewFromBrowser(const TArray<FString>& Paths)
    {
        const FScopedTransaction Transaction(LOCTEXT("MarkTestsForReview", "Mark Probe Tests Needs Configuration"));
        for (const FString& Path : Paths)
        {
            UProbeTestAsset* Test = LoadObject<UProbeTestAsset>(nullptr, *Path);
            if (!Test || Test->bNeedsConfiguration) { continue; }
            Test->Modify();
            Test->bNeedsConfiguration = true;
            Test->MarkPackageDirty();
        }
        if (TSharedPtr<SProbeWorkspace> Panel = Workspace.Pin())
        {
            if (!Paths.IsEmpty()) { Panel->SelectTestByPath(Paths[0]); }
        }
    }

    void AddBrowserTestActions(FToolMenuSection& Section)
    {
        const UContentBrowserAssetContextMenuContext* Context =
            UContentBrowserAssetContextMenuContext::FindContextWithAssets(Section);
        if (!Context) { return; }
        TArray<FString> Paths;
        for (const FAssetData& Data : Context->SelectedAssets)
        {
            if (Data.AssetClassPath == UProbeTestAsset::StaticClass()->GetClassPathName())
            {
                Paths.Add(Data.GetObjectPathString());
            }
        }
        if (Paths.IsEmpty() || Paths.Num() != Context->SelectedAssets.Num()) { return; }

        FToolUIAction OpenAction;
        OpenAction.ExecuteAction = FToolMenuExecuteAction::CreateLambda([this, Paths](const FToolMenuContext&)
        {
            OpenTestsFromBrowser(Paths);
        });
        Section.AddMenuEntry(TEXT("ProbeOpenInWorkspace"),
            LOCTEXT("ContextOpenProbe", "Open in Probe"),
            LOCTEXT("ContextOpenProbeTip", "Open the first selected Probe Test in the Probe editor"),
            FSlateIcon(TEXT("ProbeAssetAppearance"), TEXT("Probe.TabIcon")), OpenAction);

        FToolUIAction RunAction;
        RunAction.ExecuteAction = FToolMenuExecuteAction::CreateLambda([this, Paths](const FToolMenuContext&)
        {
            RunTestsFromBrowser(Paths);
        });
        Section.AddMenuEntry(TEXT("ProbeRunTests"),
            Paths.Num() == 1 ? LOCTEXT("ContextRunTest", "Run Test") : LOCTEXT("ContextRunTests", "Run Selected Tests"),
            LOCTEXT("ContextRunTip", "Execute the selected Probe Test assets and inspect results in Probe"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Play")), RunAction);

        FToolUIAction DuplicateAction;
        DuplicateAction.ExecuteAction = FToolMenuExecuteAction::CreateLambda([this, Paths](const FToolMenuContext&)
        {
            DuplicateTestsFromBrowser(Paths);
        });
        Section.AddMenuEntry(TEXT("ProbeDuplicateTest"),
            Paths.Num() == 1 ? LOCTEXT("ContextDuplicateTest", "Duplicate Test") : LOCTEXT("ContextDuplicateTests", "Duplicate Selected Tests"),
            LOCTEXT("ContextDuplicateTip", "Create independent copies of the selected tests, with unique asset names"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Duplicate")), DuplicateAction);

        FToolUIAction ReviewAction;
        ReviewAction.ExecuteAction = FToolMenuExecuteAction::CreateLambda([this, Paths](const FToolMenuContext&)
        {
            MarkTestsForReviewFromBrowser(Paths);
        });
        Section.AddMenuEntry(TEXT("ProbeMarkNeedsConfiguration"),
            LOCTEXT("ContextMarkForReview", "Mark Needs Configuration"),
            LOCTEXT("ContextMarkForReviewTip", "Mark selected tests as requiring review. They will be Skipped until explicitly made ready. Does not save automatically."),
            FSlateIcon(), ReviewAction);
    }


    void AddBrowserPackActions(FToolMenuSection& Section)
    {
        const UContentBrowserAssetContextMenuContext* Context =
            UContentBrowserAssetContextMenuContext::FindContextWithAssets(Section);
        if (!Context) { return; }
        TArray<FString> Paths;
        for (const FAssetData& Data : Context->SelectedAssets)
        {
            if (Data.AssetClassPath == UProbeTestPack::StaticClass()->GetClassPathName())
            { Paths.Add(Data.GetObjectPathString()); }
        }
        if (Paths.IsEmpty() || Paths.Num() != Context->SelectedAssets.Num()) { return; }
        FToolUIAction EditAction;
        EditAction.ExecuteAction = FToolMenuExecuteAction::CreateLambda([this, Paths](const FToolMenuContext&)
        { OpenPackAssets(Paths); });
        Section.AddMenuEntry(TEXT("ProbeManagePacksContext"),
            LOCTEXT("OpenPackInProbe", "Open in Probe Packs"),
            LOCTEXT("OpenPackInProbeTip", "Edit Pack membership, description, and run settings in Probe"),
            FSlateIcon(TEXT("ProbeAssetAppearance"), TEXT("Probe.TabIcon")), EditAction);
        FToolUIAction RunAction;
        RunAction.ExecuteAction = FToolMenuExecuteAction::CreateLambda([this, Paths](const FToolMenuContext&)
        {
            if (!Paths.IsEmpty()) { RunPackByPath(Paths[0]); }
        });
        Section.AddMenuEntry(TEXT("ProbeRunPacksContext"),
            LOCTEXT("RunPackInProbe", "Run Pack"),
            LOCTEXT("RunPacksInProbeTip", "Execute the first selected Pack and display diagnostics in Probe"),
            FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Play")), RunAction);
    }

    void RegisterAssetAppearance()
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Probe"));
        if (!Plugin.IsValid())
        {
            return;
        }

        // "ProbeTestAsset" is the UObject class name without its U prefix.
        // This changes only Editor presentation, not the serialized asset class.
        AssetStyle = MakeShared<FSlateStyleSet>(TEXT("ProbeAssetAppearance"));
        AssetStyle->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));
        AssetStyle->Set(TEXT("ClassThumbnail.ProbeTestPack"),
            new FSlateImageBrush(AssetStyle->RootToContentDir(TEXT("ProbePackAsset256"), TEXT(".png")), FVector2D(256.0f, 256.0f)));
        AssetStyle->Set(TEXT("ClassIcon.ProbeTestPack"),
            new FSlateImageBrush(AssetStyle->RootToContentDir(TEXT("ProbePackAsset16"), TEXT(".png")), FVector2D(16.0f, 16.0f)));
        AssetStyle->Set(TEXT("Probe.PackIcon16"),
            new FSlateImageBrush(AssetStyle->RootToContentDir(TEXT("ProbePackAsset16"), TEXT(".png")), FVector2D(16.0f, 16.0f)));
        AssetStyle->Set(TEXT("Probe.PackIcon40"),
            new FSlateImageBrush(AssetStyle->RootToContentDir(TEXT("ProbePackAsset40"), TEXT(".png")), FVector2D(40.0f, 40.0f)));
        AssetStyle->Set(TEXT("ClassThumbnail.ProbeTestAsset"),
            new FSlateImageBrush(
                AssetStyle->RootToContentDir(TEXT("ProbeTestThumbnail"), TEXT(".png")),
                FVector2D(256.0f, 256.0f)));
        AssetStyle->Set(TEXT("ClassIcon.ProbeTestAsset"),
            new FSlateImageBrush(
                AssetStyle->RootToContentDir(TEXT("ProbeTestIcon16"), TEXT(".png")),
                FVector2D(16.0f, 16.0f)));
        AssetStyle->Set(TEXT("Probe.TabIcon"),
            new FSlateImageBrush(
                AssetStyle->RootToContentDir(TEXT("ProbeTabIcon16"), TEXT(".png")),
                FVector2D(16.0f, 16.0f)));
        AssetStyle->Set(TEXT("Probe.DisclosureCollapsed"),
            new FSlateImageBrush(AssetStyle->RootToContentDir(TEXT("ProbeDisclosureCollapsed16"), TEXT(".png")), FVector2D(12.0f, 12.0f)));
        AssetStyle->Set(TEXT("Probe.DisclosureExpanded"),
            new FSlateImageBrush(AssetStyle->RootToContentDir(TEXT("ProbeDisclosureExpanded16"), TEXT(".png")), FVector2D(12.0f, 12.0f)));
        AssetStyle->Set(TEXT("Probe.StatusPassed"),
            new FSlateImageBrush(AssetStyle->RootToContentDir(TEXT("ProbeStatusPassed16"), TEXT(".png")), FVector2D(12.0f, 12.0f)));
        AssetStyle->Set(TEXT("Probe.StatusFailed"),
            new FSlateImageBrush(AssetStyle->RootToContentDir(TEXT("ProbeStatusFailed16"), TEXT(".png")), FVector2D(12.0f, 12.0f)));
        AssetStyle->Set(TEXT("Probe.StatusError"),
            new FSlateImageBrush(AssetStyle->RootToContentDir(TEXT("ProbeStatusError16"), TEXT(".png")), FVector2D(12.0f, 12.0f)));
        AssetStyle->Set(TEXT("Probe.StatusSkipped"),
            new FSlateImageBrush(AssetStyle->RootToContentDir(TEXT("ProbeStatusSkipped16"), TEXT(".png")), FVector2D(12.0f, 12.0f)));
        FSlateStyleRegistry::RegisterSlateStyle(*AssetStyle);
    }

    TArray<TWeakObjectPtr<UProbeTestAsset>> PendingUndoAssets;
    TArray<TWeakObjectPtr<UProbeTestPack>> PendingUndoPacks;

    void OnObjectTransacted(UObject* Object, const FTransactionObjectEvent& Event)
    {
        if (Event.GetEventType() != ETransactionObjectEventType::UndoRedo)
        {
            return;
        }
        if (UProbeTestAsset* Test = Cast<UProbeTestAsset>(Object))
        {
            PendingUndoAssets.AddUnique(TWeakObjectPtr<UProbeTestAsset>(Test));
        }
        else if (UProbeTestPack* Pack = Cast<UProbeTestPack>(Object))
        {
            PendingUndoPacks.AddUnique(TWeakObjectPtr<UProbeTestPack>(Pack));
        }
    }

    void OnPostUndoRedo()
    {
        for (const TWeakObjectPtr<UProbeTestAsset>& Test : PendingUndoAssets)
        {
            if (UProbeTestAsset* Asset = Test.Get())
            {
                // Transaction restore can reset the saved dirty flag. Mark affected
                // assets after the operation so Save Asset and Save All can detect edits.
                Asset->MarkPackageDirty();
            }
        }
        PendingUndoAssets.Reset();
        for (const TWeakObjectPtr<UProbeTestPack>& Ref : PendingUndoPacks)
        {
            if (UProbeTestPack* Pack = Ref.Get()) { Pack->MarkPackageDirty(); }
        }
        PendingUndoPacks.Reset();
    }

    TSharedRef<SDockTab> SpawnPackTab(const FSpawnTabArgs&)
    {
        TSharedRef<SProbePackWorkspace> Panel = SNew(SProbePackWorkspace);
        PackWorkspace = Panel;
        TSharedRef<SDockTab> Tab = SNew(SDockTab).TabRole(ETabRole::NomadTab)[Panel];
        Tab->SetTabIcon(AssetStyle.IsValid() ? AssetStyle->GetBrush(TEXT("Probe.PackIcon16"))
            : FAppStyle::GetBrush(TEXT("Icons.Search")));
        return Tab;
    }

    TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs&)
    {
        TSharedRef<SProbeWorkspace> Panel = SNew(SProbeWorkspace);
        Workspace = Panel;
        TSharedRef<SDockTab> Tab = SNew(SDockTab)
            .TabRole(ETabRole::NomadTab)
            [ Panel ];

        // UE 5.8 exposes the icon through SetTabIcon(), not the Slate constructor.
        // Keep the icon on the actual tab as well as its registered tab spawner.
        Tab->SetTabIcon(AssetStyle.IsValid()
            ? AssetStyle->GetBrush(TEXT("Probe.TabIcon"))
            : FAppStyle::GetBrush(TEXT("Icons.Search")));
        return Tab;
    }

    void RegisterMenus()
    {
        FToolMenuOwnerScoped Owner(this);
        UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Tools");
        // An explicitly labelled section keeps Probe out of adjacent PCG categories.
        FToolMenuSection& Section = Menu->FindOrAddSection(
            "ProbeTools", LOCTEXT("TestingMenuSection", "Testing"), FToolMenuInsert());
        Section.AddMenuEntry("OpenProbe", LOCTEXT("OpenProbe", "Probe"),
            LOCTEXT("OpenProbeTip", "Open Probe"), FSlateIcon(TEXT("ProbeAssetAppearance"), TEXT("Probe.TabIcon")),
            FUIAction(FExecuteAction::CreateLambda([]()
            {
                FGlobalTabmanager::Get()->TryInvokeTab(Probe::TabId);
            })));

        if (UToolMenu* PackAssetMenu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UProbeTestPack::StaticClass()))
        {
            FToolMenuSection& Actions = PackAssetMenu->FindOrAddSection(
                TEXT("ProbePackActions"), LOCTEXT("ProbePackAssetActions", "Probe"), FToolMenuInsert());
            Actions.AddDynamicEntry(TEXT("ProbePackContextActions"),
                FNewToolMenuSectionDelegate::CreateRaw(this, &FProbeEditorModule::AddBrowserPackActions));
        }
        // Type-specific context menu: do not load test assets while the right-click
        // menu is being built. Only executable actions load their selected assets.
        if (UToolMenu* AssetMenu = UE::ContentBrowser::ExtendToolMenu_AssetContextMenu(UProbeTestAsset::StaticClass()))
        {
            FToolMenuSection& Actions = AssetMenu->FindOrAddSection(
                TEXT("ProbeTestActions"), LOCTEXT("ProbeAssetActions", "Probe"), FToolMenuInsert());
            Actions.AddDynamicEntry(TEXT("ProbeTestContextActions"),
                FNewToolMenuSectionDelegate::CreateRaw(this, &FProbeEditorModule::AddBrowserTestActions));
        }
    }
};

namespace Probe
{
    void OpenTestAssetsInWorkspace(const TArray<FString>& AssetObjectPaths)
    {
        FProbeEditorModule& Module = FModuleManager::LoadModuleChecked<FProbeEditorModule>(TEXT("ProbeEditor"));
        Module.OpenTestAssets(AssetObjectPaths);
    }
    void OpenPackAssetsInWorkspace(const TArray<FString>& AssetObjectPaths)
    {
        FModuleManager::LoadModuleChecked<FProbeEditorModule>(TEXT("ProbeEditor")).OpenPackAssets(AssetObjectPaths);
    }
    void RunPackInWorkspace(const FString& AssetObjectPath)
    {
        FModuleManager::LoadModuleChecked<FProbeEditorModule>(TEXT("ProbeEditor")).RunPackByPath(AssetObjectPath);
    }
    void RunTestsInWorkspace(const TArray<FString>& AssetObjectPaths)
    {
        FModuleManager::LoadModuleChecked<FProbeEditorModule>(TEXT("ProbeEditor")).RunTestPaths(AssetObjectPaths);
    }
}

IMPLEMENT_MODULE(FProbeEditorModule, ProbeEditor)
#undef LOCTEXT_NAMESPACE
