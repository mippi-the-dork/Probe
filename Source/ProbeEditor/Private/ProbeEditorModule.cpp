#include "ProbeTestAsset.h"
#include "ProbeTestRunner.h"
#include "ProbeReferenceValidation.h"
#include "ProbeAssertionCatalog.h"
#include "ProbeNativeUIPolicy.h"
#include "ProbeStepEditing.h"
#include "ProbeStepGroups.h"
#include "ProbeQueueControl.h"
#include "ProbeDev8Workflow.h"
#include "ProbeDev9Workflow.h"
#include "ProbeDev10PackPolicy.h"
#include "ProbeDev10RunHistory.h"
#include "ProbeDev11RunHistory.h"
#include "ProbeDev12History.h"
#include "ProbeDev13HistoryModel.h"
#include "ProbeDev13HistoryBrowser.h"
#include "ProbeDev14Authoring.h"
#include "ProbeExtensions.h"
#include "ProbePortableProviders.h"
#include "ProbeDev19Authoring.h"
#include "ProbeDev20Catalog.h"
#include "ProbeDev23NativeBridge.h"
#include "ProbeDev25StructuredResults.h"
#include "ProbeDev26NativeExecution.h"
#include "ProbeSelfTest.h"
#include "ProbeTestAssetFactory.h"
#include "ProbeTestPack.h"
#include "ProbeTestPackFactory.h"
#include "ProbeEditorNavigation.h"
#include "ProbeWidgetDiscovery.h"
#include "ProbeWidgetTargetAuthoring.h"
#include "AssetDefinition.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "PluginDescriptor.h"
#include "ModuleDescriptor.h"
#include "ToolMenus.h"
#include <initializer_list>
#include "ToolMenu.h"
#include "ContentBrowserMenuContexts.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"
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
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Views/SListView.h"
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
#include "HAL/PlatformProcess.h"
#include "Containers/Ticker.h"
#include "InputCoreTypes.h"
#include "ScopedTransaction.h"
#include "Misc/TransactionObjectEvent.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/StrongObjectPtr.h"
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
#include "Misc/AutomationTest.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Crc.h"
#include "HAL/PlatformApplicationMisc.h"

#include "ProbeDev15Recipes.h"
#include "ProbeDev15Browser.h"
#include "ProbeDev16UX.h"

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
        return ProbeDev14::ConfigurationIssue(Step);
    }
    static FString TestConfigurationIssue(const UProbeTestAsset& Asset)
    {
        if (Asset.Steps.IsEmpty()) { return TEXT("Add at least one assertion step"); }
        for (int32 Index = 0; Index < Asset.Steps.Num(); ++Index)
        {
            const FProbeTestStep& Step = Asset.Steps[Index];
            if (!Step.bEnabled) { continue; }
            if (Step.AssertionId.IsNone()
                && Step.Type >= EProbeStepType::ActorFixtureSpawn
                && Step.Type <= EProbeStepType::ActorFixtureScaleNotNearlyEquals
                && !Asset.bAllowTransientEditorActors)
            {
                return FString::Printf(TEXT("Step %d: Enable 'Allow Transient Editor Actors' to authorize temporary level actors."), Index + 1);
            }
            if (Step.AssertionId.IsNone() &&
                ProbeNativeUIPolicy::IsInput(Step.Type) && !Asset.bAllowEditorUIInput)
            {
                return FString::Printf(TEXT("Step %d: Enable 'Allow Editor UI Input' to authorize live Editor interactions."), Index + 1);
            }
            const FString Issue = StepConfigurationIssue(Asset.Steps[Index]);
            if (!Issue.IsEmpty()) { return FString::Printf(TEXT("Step %d: %s"), Index + 1, *Issue); }
        }
        return FString();
    }

    // Keep the original tab ID so an existing saved Editor layout can reopen it.
    static const FName TabId(TEXT("ProbeWorkspace"));
    static const FName PackTabId(TEXT("ProbePackWorkspace"));
    static const FName PortableTabId(TEXT("ProbePortableProviders"));

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
        case EProbeStepType::SelectionClear:
        case EProbeStepType::SelectionSelectFirstActor:
        case EProbeStepType::SelectionRestore: break;
        case EProbeStepType::SelectionSelectActorByLabel:
            Step.Description = TEXT("Select exactly one actor by its exact Outliner label. Selection is restored after this test."); break;
        case EProbeStepType::SelectionSelectActorsWithTag:
            Step.Description = TEXT("Select all actors carrying this actor tag. Selection is restored after this test."); break;
        // Actor Fixture ID is always a test-scoped alias, not an existing world actor.
        case EProbeStepType::ActorFixtureSpawn:
            Step.Target = TEXT("/Script/Engine.Actor");
            Step.ExpectedValue = TEXT("ActorA");
            Step.Description = TEXT("Create a transient actor owned by this test. Requires explicit permission in the test header.");
            break;
        case EProbeStepType::ActorFixtureSetLabel:
        case EProbeStepType::ActorFixtureLabelEquals:
            Step.Target = TEXT("ActorA"); Step.ExpectedValue = TEXT("Probe_Automation_Actor"); break;
        case EProbeStepType::ActorFixtureSetFolder:
        case EProbeStepType::ActorFixtureFolderEquals:
            Step.Target = TEXT("ActorA"); Step.ExpectedValue = TEXT("ProbeAutomation"); break;
        case EProbeStepType::ActorFixtureAddTag:
        case EProbeStepType::ActorFixtureRemoveTag:
        case EProbeStepType::ActorFixtureHasTag:
            Step.Target = TEXT("ActorA"); Step.ExpectedValue = TEXT("ProbeTestTag"); break;
        case EProbeStepType::ActorFixtureSelect:
        case EProbeStepType::ActorFixtureHide:
        case EProbeStepType::ActorFixtureShow:
        case EProbeStepType::ActorFixtureExists:
        case EProbeStepType::ActorFixtureIsSelected:
        case EProbeStepType::ActorFixtureIsHidden:
        case EProbeStepType::ActorFixtureDestroy:
            Step.Target = TEXT("ActorA"); break;
        // 0.13.0: all actor fixture IDs are local to one test.
        case EProbeStepType::ActorFixtureSetLocation:
        case EProbeStepType::ActorFixtureLocationNearlyEquals:
            Step.Target = TEXT("ActorA"); Step.ExpectedValue = TEXT("100,0,50"); break;
        case EProbeStepType::ActorFixtureSetRotation:
        case EProbeStepType::ActorFixtureRotationNearlyEquals:
            Step.Target = TEXT("ActorA"); Step.ExpectedValue = TEXT("0,45,0"); break;
        case EProbeStepType::ActorFixtureSetScale:
        case EProbeStepType::ActorFixtureScaleNearlyEquals:
            Step.Target = TEXT("ActorA"); Step.ExpectedValue = TEXT("1,1,1"); break;
        case EProbeStepType::ActorFixtureAttachTo:
        case EProbeStepType::ActorFixtureHasParent:
        case EProbeStepType::ActorFixtureDistanceAtMost:
            Step.Target = TEXT("ActorB"); Step.ExpectedValue = TEXT("ActorA");
            Step.MinimumActorCount = 200; break;
        case EProbeStepType::ActorFixtureDetach:
        case EProbeStepType::ActorFixtureHasNoParent:
        case EProbeStepType::ActorFixtureChildCountExactly:
        case EProbeStepType::ActorFixtureIsVisible:
        case EProbeStepType::ActorFixtureIsNotSelected:
            Step.Target = TEXT("ActorA"); Step.MinimumActorCount = 1; break;
        case EProbeStepType::ActorFixtureTagAbsent:
            Step.Target = TEXT("ActorA"); Step.ExpectedValue = TEXT("ProbeTestTag"); break;
        case EProbeStepType::ActorFixtureFolderNotEquals:
            Step.Target = TEXT("ActorA"); Step.ExpectedValue = TEXT("OtherProbeFolder"); break;
        case EProbeStepType::ActorFixtureChildCountAtLeast:
        case EProbeStepType::ActorFixtureChildCountAtMost:
        case EProbeStepType::ActorFixtureTagCountExactly:
        case EProbeStepType::ActorFixtureHasRootComponent:
            Step.Target = TEXT("ActorA"); Step.MinimumActorCount = 1; break;
        case EProbeStepType::ActorFixtureDistanceAtLeast:
            Step.Target = TEXT("ActorB"); Step.ExpectedValue = TEXT("ActorA"); Step.MinimumActorCount = 100; break;
        case EProbeStepType::ActorFixtureLabelContains:
        case EProbeStepType::ActorFixtureLabelStartsWith:
        case EProbeStepType::ActorFixtureLabelEndsWith:
            Step.Target = TEXT("ActorA"); Step.ExpectedValue = TEXT("Probe"); break;
        case EProbeStepType::ActorFixtureLocationNotNearlyEquals:
            Step.Target = TEXT("ActorA"); Step.ExpectedValue = TEXT("100,0,50"); break;
        case EProbeStepType::ActorFixtureRotationNotNearlyEquals:
            Step.Target = TEXT("ActorA"); Step.ExpectedValue = TEXT("0,45,0"); break;
        case EProbeStepType::ActorFixtureScaleNotNearlyEquals:
            Step.Target = TEXT("ActorA"); Step.ExpectedValue = TEXT("2,1,1"); break;
        case EProbeStepType::UIWidgetClick:
        case EProbeStepType::UIWidgetClearText:
            Step.Target = TEXT("details:search"); break;
        case EProbeStepType::UIWidgetTypeText:
        case EProbeStepType::UIWidgetAppendText:
            Step.Target = TEXT("details:search"); Step.ExpectedValue = TEXT("ProbeTemporarySearch"); break;
        case EProbeStepType::UIDetailsSearchVisible:
        case EProbeStepType::UIDetailsSearchFilterActive:
            break;
        case EProbeStepType::UIDetailsSearchTextEquals:
        case EProbeStepType::UIDetailsSearchTextContains:
        case EProbeStepType::UIDetailsSearchTextNotEquals:
            Step.ExpectedValue = TEXT("ProbeTemporarySearch"); break;
        case EProbeStepType::UIWidgetExists:
        case EProbeStepType::UIWidgetNotExists:
        case EProbeStepType::UIWidgetVisible:
        case EProbeStepType::UIWidgetInteractable:
        case EProbeStepType::UIWidgetNotVisible:
        case EProbeStepType::UIWidgetNotInteractable:
        case EProbeStepType::UIWaitForWidgetVisible:
        case EProbeStepType::UIWaitForWidgetExists:
        case EProbeStepType::UIWaitForWidgetGone:
        case EProbeStepType::UIWaitForWidgetInteractable:
        case EProbeStepType::UIWaitForWidgetNotVisible:
            Step.Target = TEXT("details:search"); break;
        case EProbeStepType::UIWidgetTextEquals:
        case EProbeStepType::UIWidgetTextContains:
        case EProbeStepType::UIWidgetTextNotEquals:
        case EProbeStepType::UIWaitForWidgetTextEquals:
        case EProbeStepType::UIWaitForWidgetTextContains:
        case EProbeStepType::UIWaitForWidgetTextNotEquals:
            Step.Target = TEXT("details:search"); Step.ExpectedValue = TEXT("ProbeTemporarySearch"); break;
        case EProbeStepType::UIDetailsSearchFilterInactive:
            break;

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

    // Selection actions are deliberately reversible. Even if a later assertion fails,
    // the runner restores the Editor's previous actor selection before returning.
    static void RegisterProbeSelectionTemplates()
    {
        auto Make = [](EProbeStepType Type, const TCHAR* Target = TEXT(""))
        {
            FProbeTestStep Step;
            Step.Type = Type;
            Step.Target = Target;
            return Step;
        };
        {
            FExtensionTemplate Template;
            Template.Id = TEXT("Probe.SelectionClearAndRestore");
            Template.Category = TEXT("Test Actions");
            Template.Name = TEXT("Selection Clear And Restore");
            Template.Description = TEXT("Ready to run. Clears selection, confirms zero actors, then restores the original selection.");
            Template.bRequiresConfiguration = false;
            Template.Steps.Add(Make(EProbeStepType::SelectionClear));
            FProbeTestStep Count = Make(EProbeStepType::SelectedActorCountExactly);
            Count.MinimumActorCount = 0;
            Template.Steps.Add(Count);
            Template.Steps.Add(Make(EProbeStepType::SelectionRestore));
            RegisterTemplate(MoveTemp(Template));
        }
        {
            FExtensionTemplate Template;
            Template.Id = TEXT("Probe.SelectionFirstAvailable");
            Template.Category = TEXT("Test Actions");
            Template.Name = TEXT("Selection First Available Actor");
            Template.Description = TEXT("Select one actor, check its selection count, then restore. Requires a nonempty level.");
            Template.bRequiresConfiguration = true;
            Template.Steps.Add(Make(EProbeStepType::SelectionSelectFirstActor));
            FProbeTestStep Count = Make(EProbeStepType::SelectedActorCountExactly);
            Count.MinimumActorCount = 1;
            Template.Steps.Add(Count);
            Template.Steps.Add(Make(EProbeStepType::SelectionRestore));
            RegisterTemplate(MoveTemp(Template));
        }
        {
            FExtensionTemplate Template;
            Template.Id = TEXT("Probe.SelectionActorByLabel");
            Template.Category = TEXT("Test Actions");
            Template.Name = TEXT("Selection By Actor Label");
            Template.Description = TEXT("Choose an actor label, select it and assert exactly one selected actor, then restore.");
            Template.bRequiresConfiguration = true;
            Template.Steps.Add(Make(EProbeStepType::SelectionSelectActorByLabel));
            FProbeTestStep Count = Make(EProbeStepType::SelectedActorCountExactly);
            Count.MinimumActorCount = 1;
            Template.Steps.Add(Count);
            Template.Steps.Add(Make(EProbeStepType::SelectionRestore));
            RegisterTemplate(MoveTemp(Template));
        }
        {
            FExtensionTemplate Template;
            Template.Id = TEXT("Probe.SelectionActorsByTag");
            Template.Category = TEXT("Test Actions");
            Template.Name = TEXT("Selection By Actor Tag");
            Template.Description = TEXT("Choose a tag, select all matching actors, assert at least one selected, then restore.");
            Template.bRequiresConfiguration = true;
            Template.Steps.Add(Make(EProbeStepType::SelectionSelectActorsWithTag));
            FProbeTestStep Count = Make(EProbeStepType::SelectedActorCountAtLeast);
            Count.MinimumActorCount = 1;
            Template.Steps.Add(Count);
            Template.Steps.Add(Make(EProbeStepType::SelectionRestore));
            RegisterTemplate(MoveTemp(Template));
        }
    }

    // 0.12.0: actor workflows operate only on transient actors that Probe owns.
    // All start Needs Configuration; users must independently opt into world actions.
    static void RegisterProbeActorFixtureTemplates()
    {
        auto Step = [](EProbeStepType Type, const TCHAR* Value = TEXT(""))
        {
            FProbeTestStep S;
            S.Type = Type;
            S.Target = Type == EProbeStepType::ActorFixtureSpawn ? TEXT("/Script/Engine.Actor") : TEXT("ActorA");
            S.ExpectedValue = Type == EProbeStepType::ActorFixtureSpawn ? TEXT("ActorA") : Value;
            return S;
        };
        auto Add = [&Step](const TCHAR* Id, const TCHAR* Name, const TCHAR* Description,
            std::initializer_list<FProbeTestStep> Steps)
        {
            FExtensionTemplate T;
            T.Id = FName(Id);
            T.Category = TEXT("Test Actions");
            T.Name = Name;
            T.Description = Description;
            T.bRequiresConfiguration = true;
            for (const FProbeTestStep& OneStep : Steps) { T.Steps.Add(OneStep); }
            RegisterTemplate(MoveTemp(T));
        };
        Add(TEXT("Probe.ActorFixtureLifecycle"), TEXT("Temporary Actor Lifecycle"),
            TEXT("Create, find, and destroy one transient actor in the Editor world."),
            {Step(EProbeStepType::ActorFixtureSpawn), Step(EProbeStepType::ActorFixtureExists), Step(EProbeStepType::ActorFixtureDestroy)});
        Add(TEXT("Probe.ActorFixtureLabel"), TEXT("Temporary Actor Label Round Trip"),
            TEXT("Set and assert an Outliner label on a test-owned actor."),
            {Step(EProbeStepType::ActorFixtureSpawn), Step(EProbeStepType::ActorFixtureSetLabel, TEXT("Probe_Automation_Actor")),
             Step(EProbeStepType::ActorFixtureLabelEquals, TEXT("Probe_Automation_Actor")), Step(EProbeStepType::ActorFixtureDestroy)});
        Add(TEXT("Probe.ActorFixtureTag"), TEXT("Temporary Actor Tag Round Trip"),
            TEXT("Set and verify an Actor Tag on a test-owned actor."),
            {Step(EProbeStepType::ActorFixtureSpawn), Step(EProbeStepType::ActorFixtureAddTag, TEXT("ProbeTestTag")),
             Step(EProbeStepType::ActorFixtureHasTag, TEXT("ProbeTestTag")), Step(EProbeStepType::ActorFixtureDestroy)});
        Add(TEXT("Probe.ActorFixtureFolder"), TEXT("Temporary Actor Outliner Folder"),
            TEXT("Move a test actor to a folder, assert its path, and clean up."),
            {Step(EProbeStepType::ActorFixtureSpawn), Step(EProbeStepType::ActorFixtureSetFolder, TEXT("ProbeAutomation")),
             Step(EProbeStepType::ActorFixtureFolderEquals, TEXT("ProbeAutomation")), Step(EProbeStepType::ActorFixtureDestroy)});
        Add(TEXT("Probe.ActorFixtureVisibility"), TEXT("Temporary Actor Visibility Round Trip"),
            TEXT("Temporarily hide, verify, then show a test-owned actor."),
            {Step(EProbeStepType::ActorFixtureSpawn), Step(EProbeStepType::ActorFixtureHide),
             Step(EProbeStepType::ActorFixtureIsHidden), Step(EProbeStepType::ActorFixtureShow), Step(EProbeStepType::ActorFixtureDestroy)});
        Add(TEXT("Probe.ActorFixtureSelection"), TEXT("Temporary Actor Selection Round Trip"),
            TEXT("Select a test actor and assert selection without retaining it afterward."),
            {Step(EProbeStepType::ActorFixtureSpawn), Step(EProbeStepType::ActorFixtureSelect),
             Step(EProbeStepType::ActorFixtureIsSelected), Step(EProbeStepType::SelectionRestore), Step(EProbeStepType::ActorFixtureDestroy)});
        Add(TEXT("Probe.ActorFixtureFailureCleanup"), TEXT("Temporary Actor Failure Cleanup"),
            TEXT("Negative regression example: intentionally fails after spawning; Probe must still destroy the actor."),
            {Step(EProbeStepType::ActorFixtureSpawn), Step(EProbeStepType::ActorFixtureLabelEquals, TEXT("IntentionallyWrongLabel"))});
    }

    // 0.13.0: actionable Editor-plugin QA workflows. All require review and opt-in.
    // StaticMeshActor supplies a root component for transforms and attachment;
    // a rootless AActor class would reject these movement operations.
    static void RegisterProbeSpatialTemplates()
    {
        auto Spawn = [](const TCHAR* Id)
        {
            FProbeTestStep S;
            S.Type = EProbeStepType::ActorFixtureSpawn;
            S.Target = TEXT("/Script/Engine.StaticMeshActor");
            S.ExpectedValue = Id;
            return S;
        };
        auto Step = [](EProbeStepType Type, const TCHAR* ActorId, const TCHAR* Value = TEXT(""), int32 Count = 0)
        {
            FProbeTestStep S;
            S.Type = Type;
            S.Target = ActorId;
            S.ExpectedValue = Value;
            S.MinimumActorCount = Count;
            return S;
        };
        auto Add = [](const TCHAR* Id, const TCHAR* Name, const TCHAR* Description,
            std::initializer_list<FProbeTestStep> Steps)
        {
            FExtensionTemplate T;
            T.Id = FName(Id);
            T.Category = TEXT("Test Actions");
            T.Name = Name;
            T.Description = Description;
            T.bRequiresConfiguration = true;
            for (const FProbeTestStep& S : Steps) { T.Steps.Add(S); }
            RegisterTemplate(MoveTemp(T));
        };
        Add(TEXT("Probe.SpatialLocation"), TEXT("Temporary Actor Location Round Trip"),
            TEXT("Spawn, move, verify world position, and destroy a test-owned actor."),
            {Spawn(TEXT("ActorA")), Step(EProbeStepType::ActorFixtureSetLocation, TEXT("ActorA"), TEXT("100,0,50")),
             Step(EProbeStepType::ActorFixtureLocationNearlyEquals, TEXT("ActorA"), TEXT("100,0,50")),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.SpatialRotation"), TEXT("Temporary Actor Rotation Round Trip"),
            TEXT("Spawn, rotate, verify Euler rotation, and destroy."),
            {Spawn(TEXT("ActorA")), Step(EProbeStepType::ActorFixtureSetRotation, TEXT("ActorA"), TEXT("0,45,0")),
             Step(EProbeStepType::ActorFixtureRotationNearlyEquals, TEXT("ActorA"), TEXT("0,45,0")),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.SpatialScale"), TEXT("Temporary Actor Scale Round Trip"),
            TEXT("Spawn, scale, compare, and clean up a test-owned actor."),
            {Spawn(TEXT("ActorA")), Step(EProbeStepType::ActorFixtureSetScale, TEXT("ActorA"), TEXT("2,1,1")),
             Step(EProbeStepType::ActorFixtureScaleNearlyEquals, TEXT("ActorA"), TEXT("2,1,1")),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.SpatialTransform"), TEXT("Temporary Actor Transform Contract"),
            TEXT("Verify position, rotation, and scale in one isolated run."),
            {Spawn(TEXT("ActorA")), Step(EProbeStepType::ActorFixtureSetLocation, TEXT("ActorA"), TEXT("100,0,50")),
             Step(EProbeStepType::ActorFixtureLocationNearlyEquals, TEXT("ActorA"), TEXT("100,0,50")),
             Step(EProbeStepType::ActorFixtureSetRotation, TEXT("ActorA"), TEXT("0,45,0")),
             Step(EProbeStepType::ActorFixtureRotationNearlyEquals, TEXT("ActorA"), TEXT("0,45,0")),
             Step(EProbeStepType::ActorFixtureSetScale, TEXT("ActorA"), TEXT("2,1,1")),
             Step(EProbeStepType::ActorFixtureScaleNearlyEquals, TEXT("ActorA"), TEXT("2,1,1")),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.HierarchyAttach"), TEXT("Temporary Actor Parent And Child"),
            TEXT("Create a parent and child, attach, verify relationship and count, then clean up."),
            {Spawn(TEXT("ActorA")), Spawn(TEXT("ActorB")),
             Step(EProbeStepType::ActorFixtureAttachTo, TEXT("ActorB"), TEXT("ActorA")),
             Step(EProbeStepType::ActorFixtureHasParent, TEXT("ActorB"), TEXT("ActorA")),
             Step(EProbeStepType::ActorFixtureChildCountExactly, TEXT("ActorA"), TEXT(""), 1),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorB")), Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.HierarchyDetach"), TEXT("Temporary Actor Detach Round Trip"),
            TEXT("Attach, detach, and verify a child no longer has a parent."),
            {Spawn(TEXT("ActorA")), Spawn(TEXT("ActorB")),
             Step(EProbeStepType::ActorFixtureAttachTo, TEXT("ActorB"), TEXT("ActorA")),
             Step(EProbeStepType::ActorFixtureDetach, TEXT("ActorB")),
             Step(EProbeStepType::ActorFixtureHasNoParent, TEXT("ActorB")),
             Step(EProbeStepType::ActorFixtureChildCountExactly, TEXT("ActorA"), TEXT(""), 0),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorB")), Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.HierarchyReparent"), TEXT("Temporary Actor Reparenting"),
            TEXT("Switch a test actor between two different test-owned parents and check counts."),
            {Spawn(TEXT("ActorA")), Spawn(TEXT("ActorB")), Spawn(TEXT("ActorC")),
             Step(EProbeStepType::ActorFixtureAttachTo, TEXT("ActorC"), TEXT("ActorA")),
             Step(EProbeStepType::ActorFixtureHasParent, TEXT("ActorC"), TEXT("ActorA")),
             Step(EProbeStepType::ActorFixtureAttachTo, TEXT("ActorC"), TEXT("ActorB")),
             Step(EProbeStepType::ActorFixtureHasParent, TEXT("ActorC"), TEXT("ActorB")),
             Step(EProbeStepType::ActorFixtureChildCountExactly, TEXT("ActorA"), TEXT(""), 0),
             Step(EProbeStepType::ActorFixtureChildCountExactly, TEXT("ActorB"), TEXT(""), 1),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorC")),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorB")), Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.HierarchyDistance"), TEXT("Temporary Actor Distance Threshold"),
            TEXT("Move two test actors and verify their world-space separation."),
            {Spawn(TEXT("ActorA")), Spawn(TEXT("ActorB")),
             Step(EProbeStepType::ActorFixtureSetLocation, TEXT("ActorA"), TEXT("0,0,0")),
             Step(EProbeStepType::ActorFixtureSetLocation, TEXT("ActorB"), TEXT("100,0,0")),
             Step(EProbeStepType::ActorFixtureDistanceAtMost, TEXT("ActorB"), TEXT("ActorA"), 125),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorB")), Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.StateTagRemoval"), TEXT("Temporary Actor Tag Removal"),
            TEXT("Add and remove an actor tag, then assert the tag is absent."),
            {Spawn(TEXT("ActorA")), Step(EProbeStepType::ActorFixtureAddTag, TEXT("ActorA"), TEXT("ProbeTestTag")),
             Step(EProbeStepType::ActorFixtureRemoveTag, TEXT("ActorA"), TEXT("ProbeTestTag")),
             Step(EProbeStepType::ActorFixtureTagAbsent, TEXT("ActorA"), TEXT("ProbeTestTag")),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.StateVisibility"), TEXT("Temporary Actor Hide And Show"),
            TEXT("Hide, assert hidden, show, assert visible, clean up."),
            {Spawn(TEXT("ActorA")), Step(EProbeStepType::ActorFixtureHide, TEXT("ActorA")),
             Step(EProbeStepType::ActorFixtureIsHidden, TEXT("ActorA")),
             Step(EProbeStepType::ActorFixtureShow, TEXT("ActorA")),
             Step(EProbeStepType::ActorFixtureIsVisible, TEXT("ActorA")),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.StateFolderNegative"), TEXT("Temporary Actor Folder Exclusion"),
            TEXT("Move to a test folder and verify it is not in a forbidden alternative folder."),
            {Spawn(TEXT("ActorA")), Step(EProbeStepType::ActorFixtureSetFolder, TEXT("ActorA"), TEXT("ProbeAutomation")),
             Step(EProbeStepType::ActorFixtureFolderNotEquals, TEXT("ActorA"), TEXT("OtherProbeFolder")),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.StateSelectionNegative"), TEXT("Temporary Actor Selection Cleared"),
            TEXT("Select a test actor, clear selection, verify it is not selected, and auto-restore previous selection."),
            {Spawn(TEXT("ActorA")), Step(EProbeStepType::ActorFixtureSelect, TEXT("ActorA")),
             Step(EProbeStepType::SelectionClear, TEXT("")),
             Step(EProbeStepType::ActorFixtureIsNotSelected, TEXT("ActorA")),
             Step(EProbeStepType::SelectionRestore, TEXT("")), Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
    }

    // 0.14.0: parameterized, test-owned actor state contracts. All require
    // human review and explicit authorization before they can mutate the Editor world.
    static void RegisterProbeBehaviorContractTemplates()
    {
        auto Spawn = [](const TCHAR* Id)
        {
            FProbeTestStep S;
            S.Type = EProbeStepType::ActorFixtureSpawn;
            S.Target = TEXT("/Script/Engine.StaticMeshActor");
            S.ExpectedValue = Id;
            return S;
        };
        auto Step = [](EProbeStepType Type, const TCHAR* ActorId, const TCHAR* Value = TEXT(""), int32 Count = 0)
        {
            FProbeTestStep S;
            S.Type = Type;
            S.Target = ActorId;
            S.ExpectedValue = Value;
            S.MinimumActorCount = Count;
            return S;
        };
        auto Add = [](const TCHAR* Id, const TCHAR* Name, const TCHAR* Description,
            std::initializer_list<FProbeTestStep> Steps)
        {
            FExtensionTemplate T;
            T.Id = FName(Id);
            T.Category = TEXT("Test Actions");
            T.Name = Name;
            T.Description = Description;
            T.bRequiresConfiguration = true;
            for (const FProbeTestStep& OneStep : Steps) { T.Steps.Add(OneStep); }
            RegisterTemplate(MoveTemp(T));
        };
        Add(TEXT("Probe.ContractChildMinimum"), TEXT("Temporary Actor Minimum Children"),
            TEXT("Attach a child and verify that the parent has at least one test-owned child."),
            {Spawn(TEXT("ActorA")), Spawn(TEXT("ActorB")), Step(EProbeStepType::ActorFixtureAttachTo, TEXT("ActorB"), TEXT("ActorA")),
             Step(EProbeStepType::ActorFixtureChildCountAtLeast, TEXT("ActorA"), TEXT(""), 1),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorB")), Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.ContractChildMaximum"), TEXT("Temporary Actor Maximum Children"),
            TEXT("Verify no more than one child remains after attachment."),
            {Spawn(TEXT("ActorA")), Spawn(TEXT("ActorB")), Step(EProbeStepType::ActorFixtureAttachTo, TEXT("ActorB"), TEXT("ActorA")),
             Step(EProbeStepType::ActorFixtureChildCountAtMost, TEXT("ActorA"), TEXT(""), 1),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorB")), Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.ContractDistanceMinimum"), TEXT("Temporary Actor Minimum Separation"),
            TEXT("Move two test actors 150 cm apart and verify separation is at least 100 cm."),
            {Spawn(TEXT("ActorA")), Spawn(TEXT("ActorB")),
             Step(EProbeStepType::ActorFixtureSetLocation, TEXT("ActorA"), TEXT("0,0,0")),
             Step(EProbeStepType::ActorFixtureSetLocation, TEXT("ActorB"), TEXT("150,0,0")),
             Step(EProbeStepType::ActorFixtureDistanceAtLeast, TEXT("ActorB"), TEXT("ActorA"), 100),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorB")), Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.ContractLabelContains"), TEXT("Temporary Actor Label Substring"),
            TEXT("Rename a test actor and verify its Outliner label includes a substring."),
            {Spawn(TEXT("ActorA")), Step(EProbeStepType::ActorFixtureSetLabel, TEXT("ActorA"), TEXT("Probe_Contract_014")),
             Step(EProbeStepType::ActorFixtureLabelContains, TEXT("ActorA"), TEXT("Contract")),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.ContractLabelPrefix"), TEXT("Temporary Actor Label Prefix"),
            TEXT("Check that a controlled actor label begins with a specified prefix."),
            {Spawn(TEXT("ActorA")), Step(EProbeStepType::ActorFixtureSetLabel, TEXT("ActorA"), TEXT("Probe_Contract_014")),
             Step(EProbeStepType::ActorFixtureLabelStartsWith, TEXT("ActorA"), TEXT("Probe_")),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.ContractLabelSuffix"), TEXT("Temporary Actor Label Suffix"),
            TEXT("Check that a controlled actor label ends with a specified suffix."),
            {Spawn(TEXT("ActorA")), Step(EProbeStepType::ActorFixtureSetLabel, TEXT("ActorA"), TEXT("Probe_Contract_014")),
             Step(EProbeStepType::ActorFixtureLabelEndsWith, TEXT("ActorA"), TEXT("_014")),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.ContractTagCount"), TEXT("Temporary Actor Exact Tag Count"),
            TEXT("Apply two tags to a controlled actor and verify the exact tag count."),
            {Spawn(TEXT("ActorA")), Step(EProbeStepType::ActorFixtureAddTag, TEXT("ActorA"), TEXT("ProbeA")),
             Step(EProbeStepType::ActorFixtureAddTag, TEXT("ActorA"), TEXT("ProbeB")),
             Step(EProbeStepType::ActorFixtureTagCountExactly, TEXT("ActorA"), TEXT(""), 2),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.ContractRootComponent"), TEXT("Temporary Actor Root Component"),
            TEXT("Verify the actor fixture has a root SceneComponent."),
            {Spawn(TEXT("ActorA")), Step(EProbeStepType::ActorFixtureHasRootComponent, TEXT("ActorA")),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.ContractLocationNegative"), TEXT("Temporary Actor Location Exclusion"),
            TEXT("Position a test actor and assert it is not at a forbidden location."),
            {Spawn(TEXT("ActorA")), Step(EProbeStepType::ActorFixtureSetLocation, TEXT("ActorA"), TEXT("0,0,0")),
             Step(EProbeStepType::ActorFixtureLocationNotNearlyEquals, TEXT("ActorA"), TEXT("100,0,0")),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.ContractRotationNegative"), TEXT("Temporary Actor Rotation Exclusion"),
            TEXT("Assert that the actor is not oriented toward a forbidden rotation."),
            {Spawn(TEXT("ActorA")), Step(EProbeStepType::ActorFixtureSetRotation, TEXT("ActorA"), TEXT("0,0,0")),
             Step(EProbeStepType::ActorFixtureRotationNotNearlyEquals, TEXT("ActorA"), TEXT("0,90,0")),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
        Add(TEXT("Probe.ContractScaleNegative"), TEXT("Temporary Actor Scale Exclusion"),
            TEXT("Assert a controlled actor does not have the forbidden scale."),
            {Spawn(TEXT("ActorA")), Step(EProbeStepType::ActorFixtureSetScale, TEXT("ActorA"), TEXT("1,1,1")),
             Step(EProbeStepType::ActorFixtureScaleNotNearlyEquals, TEXT("ActorA"), TEXT("2,2,2")),
             Step(EProbeStepType::ActorFixtureDestroy, TEXT("ActorA"))});
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


    // One source of truth for the built-in Templates menu and developer asset generator.
    struct FTemplateInfo
    {
        ETestTemplate Type;
        const TCHAR* Category;
        const TCHAR* Name;
        const TCHAR* Description;
    };
    static const TArray<FTemplateInfo>& BuiltinTemplates()
    {
        static const TArray<FTemplateInfo> Items = {
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
        return Items;
    }

    // Reused by interactive creation and bulk generation; the asset path resolves
    // the self-targeting steps after the factory has created the actual UObject.
    static void PopulateBuiltinTemplate(UProbeTestAsset* Asset, ETestTemplate Template)
    {
        if (!Asset) { return; }
        Asset->Modify();
        Asset->bNeedsConfiguration = Template != ETestTemplate::Smoke;
        if (Template == ETestTemplate::Blank) { return; }
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

    static void PopulateExtensionTemplate(UProbeTestAsset* Asset, const FExtensionTemplate& Template)
    {
        if (!Asset) { return; }
        Asset->Modify();
        Asset->TestDescription = Template.Description;
        Asset->Steps = Template.Steps;
        Asset->bNeedsConfiguration = Template.bRequiresConfiguration;
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
    }


    // Temporary developer utility. Asset/folder names must be valid Unreal long package names.
    static FString SafeTemplatePathSegment(const FString& Text)
    {
        FString Result;
        bool bLastSeparator = false;
        for (const TCHAR Character : Text)
        {
            const bool bAllowed = (Character >= TEXT('A') && Character <= TEXT('Z'))
                || (Character >= TEXT('a') && Character <= TEXT('z'))
                || (Character >= TEXT('0') && Character <= TEXT('9'));
            if (bAllowed)
            {
                Result.AppendChar(Character);
                bLastSeparator = false;
            }
            else if (!Result.IsEmpty() && !bLastSeparator)
            {
                Result.AppendChar(TEXT('_'));
                bLastSeparator = true;
            }
        }
        while (Result.EndsWith(TEXT("_"))) { Result.LeftChopInline(1); }
        return Result.IsEmpty() ? TEXT("General") : Result;
    }

    static void GenerateTemplateTests()
    {
        if (!IsInGameThread())
        {
            UE_LOG(LogTemp, Error, TEXT("Probe.GenerateTemplateTests must run on the Editor game thread."));
            return;
        }
        struct FGenerationItem
        {
            FString StableId;
            FString Name;
            FString Category;
            bool bExtension = false;
            ETestTemplate Builtin = ETestTemplate::Blank;
            FName ExtensionId;
        };
        TArray<FGenerationItem> Items;
        Items.Add({TEXT("Probe.Blank"), TEXT("Blank Test"), TEXT("Getting Started"), false, ETestTemplate::Blank, NAME_None});
        Items.Add({TEXT("Probe.Smoke"), TEXT("Probe Smoke Test"), TEXT("Getting Started"), false, ETestTemplate::Smoke, NAME_None});
        for (const FTemplateInfo& Info : BuiltinTemplates())
        {
            // The enum value remains stable, even when two displayed template names match.
            const FString Key = FString::Printf(TEXT("Probe.Builtin.%d"), static_cast<int32>(Info.Type));
            Items.Add({Key, Info.Name, Info.Category, false, Info.Type, NAME_None});
        }
        for (const FName Id : GetExtensionTemplateIds())
        {
            if (const FExtensionTemplate* Info = FindExtensionTemplate(Id))
            {
                Items.Add({Id.ToString(), Info->Name, Info->Category, true, ETestTemplate::Blank, Id});
            }
        }
        Items.Sort([](const FGenerationItem& A, const FGenerationItem& B)
        {
            return A.StableId < B.StableId;
        });

        IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
        IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
        TArray<UPackage*> PackagesToSave;
        int32 Created = 0;
        int32 Skipped = 0;
        int32 Failed = 0;
        for (const FGenerationItem& Item : Items)
        {
            const FString Group = Item.Category == TEXT("Getting Started")
                ? TEXT("Basic Tests") : MenuSubgroup(Item.Category, Item.Name);
            const FString Folder = TEXT("/Game/Probe/") + SafeTemplatePathSegment(Item.Category)
                + TEXT("/") + SafeTemplatePathSegment(Group);
            const uint32 IdHash = FCrc::StrCrc32(*Item.StableId);
            const FString AssetName = FString::Printf(TEXT("PT_%s_%08X"),
                *SafeTemplatePathSegment(Item.Name).Left(50), IdHash);
            const FString PackageName = Folder + TEXT("/") + AssetName;
            const FString ObjectPath = PackageName + TEXT(".") + AssetName;
            const FAssetData Existing = Registry.GetAssetByObjectPath(FSoftObjectPath(ObjectPath));
            if (Existing.IsValid() || FindPackage(nullptr, *PackageName) != nullptr
                || FPackageName::DoesPackageExist(PackageName))
            {
                ++Skipped;
                continue;
            }
            UProbeTestAssetFactory* Factory = NewObject<UProbeTestAssetFactory>();
            UProbeTestAsset* Asset = Cast<UProbeTestAsset>(AssetTools.CreateAsset(
                AssetName, Folder, UProbeTestAsset::StaticClass(), Factory));
            if (!Asset)
            {
                ++Failed;
                UE_LOG(LogTemp, Error, TEXT("Probe: Failed to generate template '%s' at %s"), *Item.StableId, *PackageName);
                continue;
            }
            if (Item.bExtension)
            {
                if (const FExtensionTemplate* Extension = FindExtensionTemplate(Item.ExtensionId))
                {
                    PopulateExtensionTemplate(Asset, *Extension);
                }
                else
                {
                    ++Failed;
                    UE_LOG(LogTemp, Error, TEXT("Probe: Template provider disappeared during generation: %s"), *Item.StableId);
                    continue;
                }
            }
            else
            {
                PopulateBuiltinTemplate(Asset, Item.Builtin);
            }
            Asset->MarkPackageDirty();
            PackagesToSave.AddUnique(Asset->GetOutermost());
            ++Created;
        }
        const bool bSaved = PackagesToSave.IsEmpty()
            || UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
        UE_LOG(LogTemp, Display,
            TEXT("Probe.GenerateTemplateTests: %d created, %d already present (skipped), %d failed. Saved: %s. Root: /Game/Probe"),
            Created, Skipped, Failed, bSaved ? TEXT("yes") : TEXT("NO - check unsaved assets"));
        if (!bSaved)
        {
            UE_LOG(LogTemp, Warning, TEXT("Probe: Some generated tests may still be unsaved. Use Save All and inspect the Output Log."));
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

// Discovery surface shared by the top-level browser and the per-step Target picker.
// Only explicit Automation Driver IDs are offered; duplicate IDs cannot be applied.
DECLARE_DELEGATE_OneParam(FOnProbeWidgetTargetChosen, const FString&);
DECLARE_DELEGATE_ThreeParams(FOnProbeWidgetTargetCreateStep, EProbeStepType, const FString&, const FString&);

class SProbeWidgetTargetPicker : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SProbeWidgetTargetPicker) {}
        SLATE_EVENT(FOnProbeWidgetTargetChosen, OnChosen)
        SLATE_EVENT(FOnProbeWidgetTargetCreateStep, OnCreateStep)
        SLATE_ARGUMENT(bool, CanCreateSteps)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        OnChosen = Args._OnChosen;
        OnCreateStep = Args._OnCreateStep;
        bCanCreateSteps = Args._CanCreateSteps;
        ChildSlot
        [ SNew(SBox).WidthOverride(690.0f)
          [ SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(8, 7, 8, 4)
            [ SNew(STextBlock).Text(LOCTEXT("WidgetTargetPickerTitle", "Live Widget Targets"))
              .Font(FAppStyle::GetFontStyle("HeadingExtraSmall")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 5)
            [ SNew(STextBlock).AutoWrapText(true)
              .Text(LOCTEXT("WidgetTargetPickerInfo", "Only explicitly tagged Automation Driver IDs can be selected. Open the plugin's UI first. Untagged widgets are intentionally omitted."))
              .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
            + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 5)
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f)
                [ SNew(SSearchBox)
                  .HintText(LOCTEXT("WidgetTargetSearch", "Filter ID, window, or widget type..."))
                  .OnTextChanged(this, &SProbeWidgetTargetPicker::FilterChanged) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(5, 0, 0, 0)
                [ SNew(SButton)
                  .ToolTipText(LOCTEXT("WidgetTargetRefreshTip", "Rescan currently visible Slate windows without changing Editor state"))
                  .OnClicked(this, &SProbeWidgetTargetPicker::Rescan)
                  [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Refresh")) ] ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 5)
            [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 14, 0)
                [ SNew(SCheckBox).OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                  { bOnlyUnique = State == ECheckBoxState::Checked; Rebuild(); })
                  [ SNew(STextBlock).Text(LOCTEXT("WidgetTargetOnlyUnique", "Unique only")) ] ]
                + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
                [ SNew(SCheckBox).OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                  { bOnlyAmbiguous = State == ECheckBoxState::Checked; Rebuild(); })
                  [ SNew(STextBlock).Text(LOCTEXT("WidgetTargetOnlyAmbiguous", "Duplicates only")) ] ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 5)
            [ SAssignNew(SummaryText, STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
            + SVerticalBox::Slot().FillHeight(1).Padding(6, 0, 6, 4)
            [ SNew(SBox).HeightOverride(345.0f)
              [ SNew(SScrollBox) + SScrollBox::Slot()
                [ SAssignNew(Rows, SVerticalBox) ] ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(8, 1, 8, 8)
            [ SNew(STextBlock).AutoWrapText(true)
              .ColorAndOpacity(FSlateColor::UseSubduedForeground())
              .Text(LOCTEXT("WidgetTargetPickerFooter", "Snapshots are read-only and refresh on demand. Preview text is provided only for directly tagged text widgets. Duplicate IDs cannot be used. Native Details uses details:search.")) ]
          ] ];
        Scan();
    }

private:
    TArray<ProbeWidgetDiscovery::FCandidate> Candidates;
    TSharedPtr<SVerticalBox> Rows;
    TSharedPtr<STextBlock> SummaryText;
    FOnProbeWidgetTargetChosen OnChosen;
    FOnProbeWidgetTargetCreateStep OnCreateStep;
    FString Filter;
    bool bCanCreateSteps = false;
    bool bOnlyUnique = false;
    bool bOnlyAmbiguous = false;

    void FilterChanged(const FText& NewText) { Filter = NewText.ToString(); Rebuild(); }
    FReply Rescan() { Scan(); return FReply::Handled(); }
    void Scan() { Candidates = ProbeWidgetDiscovery::Collect(); Rebuild(); }
    void Rebuild()
    {
        if (!Rows.IsValid()) { return; }
        Rows->ClearChildren();
        int32 Duplicates = 0;
        for (const auto& Candidate : Candidates) { if (!Candidate.IsUnique()) { ++Duplicates; } }
        if (SummaryText.IsValid())
        {
            SummaryText->SetText(FText::FromString(FString::Printf(
                TEXT("%d tagged widgets  |  %d ambiguous matches  |  read-only scan"), Candidates.Num(), Duplicates)));
        }
        int32 Shown = 0;
        for (const auto& Candidate : Candidates)
        {
            if (!ProbeWidgetTargetAuthoring::MatchesBrowserFilters(
                Candidate, Filter, bOnlyUnique, bOnlyAmbiguous)) { continue; }
            if (Shown++ >= 250) { break; }
            const FString Selector = Candidate.Selector();
            const bool bUnique = Candidate.IsUnique();
            const FString Status = bUnique
                ? (Candidate.bVisible ? (Candidate.bEnabled ? TEXT("Visible / Enabled") : TEXT("Visible / Disabled")) : TEXT("Hidden"))
                : FString::Printf(TEXT("DUPLICATE x%d"), Candidate.Matches);
            const FString Preview = Candidate.bHasReadableText
                ? TEXT("Text: ") + Candidate.ReadableText.Left(180).Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\r"), TEXT(" "))
                : TEXT("Text: not exposed by this widget type");
            Rows->AddSlot().AutoHeight().Padding(1, 2)
            [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(5)
              [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [ SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text(FText::FromString(Candidate.Id)) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock)
                      .Text(FText::FromString(Candidate.WindowTitle + TEXT("  |  ") + Candidate.WidgetType))
                      .Font(FAppStyle::GetFontStyle("SmallFont"))
                      .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text(FText::FromString(Status))
                      .Font(FAppStyle::GetFontStyle("SmallFont"))
                      .ColorAndOpacity(bUnique ? FSlateColor::UseSubduedForeground() : FSlateColor(FLinearColor(1.0f, 0.45f, 0.38f))) ]
                    + SVerticalBox::Slot().AutoHeight()
                    [ SNew(STextBlock).Text(FText::FromString(Preview))
                      .Font(FAppStyle::GetFontStyle("SmallFont"))
                      .ToolTipText(FText::FromString(Candidate.bHasReadableText ? Candidate.ReadableText : Preview))
                      .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5, 0)
                [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                  .IsEnabled(bUnique)
                  .ToolTipText(LOCTEXT("WidgetTargetCopyTip", "Copy the id: selector to the clipboard"))
                  .OnClicked_Lambda([Selector]()
                  { FPlatformApplicationMisc::ClipboardCopy(*Selector); return FReply::Handled(); })
                  [ SNew(STextBlock).Text(LOCTEXT("WidgetTargetCopy", "Copy")) ] ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                [ SNew(SButton).IsEnabled(bUnique && OnChosen.IsBound())
                  .ToolTipText(LOCTEXT("WidgetTargetUseTip", "Insert this selector in the selected Probe step"))
                  .OnClicked_Lambda([this, Selector]()
                  {
                      // Applying the target rebuilds its step card and may destroy
                      // this popup. Copy the callback and close the menu first.
                      const FOnProbeWidgetTargetChosen Chosen = OnChosen;
                      FSlateApplication::Get().DismissAllMenus();
                      Chosen.ExecuteIfBound(Selector);
                      return FReply::Handled();
                  })
                  [ SNew(STextBlock).Text(LOCTEXT("WidgetTargetUse", "Use")) ] ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
                [ SNew(SComboButton)
                  .IsEnabled(bUnique && bCanCreateSteps && OnCreateStep.IsBound())
                  .ToolTipText(LOCTEXT("WidgetCreateStepTip", "Create an assertion in the currently selected Probe Test. This does not click or modify the target widget."))
                  .OnGetMenuContent_Lambda([this, Selector, PreviewValue = Candidate.ReadableText, bTextReadable = Candidate.bHasReadableText]() -> TSharedRef<SWidget>
                  {
                      FMenuBuilder Menu(true, nullptr);
                      const FOnProbeWidgetTargetCreateStep Callback = OnCreateStep;
                      auto AddAction = [&Menu, Callback, Selector, PreviewValue](const FText& Label, EProbeStepType Type)
                      {
                          Menu.AddMenuEntry(Label, FText::GetEmpty(), FSlateIcon(),
                              FUIAction(FExecuteAction::CreateLambda([Callback, Type, Selector, PreviewValue]()
                              {
                                  FSlateApplication::Get().DismissAllMenus();
                                  Callback.ExecuteIfBound(Type, Selector, PreviewValue);
                              })));
                      };
                      AddAction(LOCTEXT("QuickWidgetExists", "Widget Exists"), EProbeStepType::UIWidgetExists);
                      AddAction(LOCTEXT("QuickWidgetVisible", "Widget Is Visible"), EProbeStepType::UIWidgetVisible);
                      AddAction(LOCTEXT("QuickWidgetInteractable", "Widget Is Interactable"), EProbeStepType::UIWidgetInteractable);
                      AddAction(LOCTEXT("QuickWaitVisible", "Wait For Widget Visible"), EProbeStepType::UIWaitForWidgetVisible);
                      AddAction(LOCTEXT("QuickWaitInteractable", "Wait For Widget Interactable"), EProbeStepType::UIWaitForWidgetInteractable);
                      AddAction(LOCTEXT("QuickWaitGone", "Wait For Widget Gone"), EProbeStepType::UIWaitForWidgetGone);
                      if (bTextReadable)
                      {
                          Menu.AddMenuSeparator();
                          AddAction(LOCTEXT("QuickWidgetTextEquals", "Widget Text Equals Current Text"), EProbeStepType::UIWidgetTextEquals);
                          AddAction(LOCTEXT("QuickWaitWidgetText", "Wait For Current Text"), EProbeStepType::UIWaitForWidgetTextEquals);
                          AddAction(LOCTEXT("QuickWaitWidgetTextChange", "Wait For Text To Change"), EProbeStepType::UIWaitForWidgetTextNotEquals);
                      }
                      return Menu.MakeWidget();
                  })
                  .ButtonContent()
                  [ SNew(STextBlock).Text(LOCTEXT("WidgetTargetAddStep", "+ Step")) ] ]
              ] ];
        }
        if (Shown == 0)
        {
            Rows->AddSlot().AutoHeight().Padding(8)
            [ SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("WidgetTargetNone", "No matching tagged widgets. Open a plugin panel with Automation Driver IDs and click Refresh.")) ];
        }
        else if (Shown > 250)
        {
            Rows->AddSlot().AutoHeight().Padding(8)
            [ SNew(STextBlock).Text(LOCTEXT("WidgetTargetCap", "Showing first 250 matches. Narrow the filter to see more.")) ];
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

// Dev 20: source-aware inventory. The catalog never instantiates portable assets or
// directly executes native Automation tests. Its host controls execution and results.
DECLARE_DELEGATE_OneParam(FOnProbeCatalogEntry, const ProbeDev20::FEntry&);

class SProbeUnifiedCatalog : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SProbeUnifiedCatalog) {}
        SLATE_EVENT(FOnProbeCatalogEntry, OnOpen)
        SLATE_EVENT(FOnProbeCatalogEntry, OnRun)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        OpenEntry = Args._OnOpen;
        RunEntry = Args._OnRun;
        ChildSlot
        [ SNew(SVerticalBox)
          + SVerticalBox::Slot().AutoHeight().Padding(3, 2, 3, 4)
            [ SNew(SHorizontalBox)
              + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [ SNew(SSearchBox).HintText(LOCTEXT("Dev20Search", "Search all Tests, Packs, provider IDs and native Automation..."))
                  .OnTextChanged_Lambda([this](const FText& Value) { Query = Value.ToString(); Filter(); }) ]
              + SHorizontalBox::Slot().AutoWidth().Padding(5,0,0,0)
                [ SNew(SComboButton).ToolTipText(LOCTEXT("Dev20SourceFilterTip", "Choose which Test source to browse"))
                  .OnGetMenuContent(this, &SProbeUnifiedCatalog::BuildSources)
                  .ButtonContent()[ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(NativeCategoryFilter.IsEmpty() ? SourceFilter : NativeCategoryFilter); }) ] ]
              + SHorizontalBox::Slot().AutoWidth().Padding(5,0,0,0)
                [ SNew(SButton).Text(LOCTEXT("Dev20Refresh", "Refresh Library"))
                  .ToolTipText(LOCTEXT("Dev20RefreshTip", "Refresh read-only asset, portable provider and native Automation discovery"))
                  .OnClicked_Lambda([this]() { ProbePortable::Refresh(); Refresh(); return FReply::Handled(); }) ]
              + SHorizontalBox::Slot().AutoWidth().Padding(5,0,0,0)
                [ SNew(SButton).Text(LOCTEXT("Dev20RunSelected", "Run Selection"))
                  .IsEnabled_Lambda([this]() { return Selected.IsValid(); })
                  .OnClicked_Lambda([this]() { RunSelection(); return FReply::Handled(); }) ] ]
          + SVerticalBox::Slot().AutoHeight().Padding(4,0,4,3)
            [ SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
              .Text_Lambda([this]() { return FText::FromString(FString::Printf(
                  TEXT("%d definitions | %d matching | showing %d of %d | %s%s%s"),
                  Entries.Num(), MatchCount, Visible.Num(), MatchCount, *SourceFilter,
                  NativeCategoryFilter.IsEmpty() ? TEXT("") : TEXT(" / "), *NativeCategoryFilter)); }) ]
          + SVerticalBox::Slot().FillHeight(1.f)
            [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(2)
              [ SAssignNew(List, SListView<TSharedPtr<ProbeDev20::FEntry>>)
                .ListItemsSource(&Visible)
                .SelectionMode(ESelectionMode::Single)
                .OnGenerateRow(this, &SProbeUnifiedCatalog::MakeRow)
                .OnSelectionChanged(this, &SProbeUnifiedCatalog::SelectionChanged) ] ]
          + SVerticalBox::Slot().AutoHeight().Padding(4,3,4,1)
            [ SNew(SBox).HeightOverride(82.f)
              [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(5)
                [ SNew(SScrollBox) + SScrollBox::Slot()
                  [ SNew(STextBlock).AutoWrapText(true)
                    .Text_Lambda([this]() { return FText::FromString(PreviewSelected()); }) ] ] ] ] ];
        Refresh();
    }

    TSharedPtr<ProbeDev20::FEntry> Selection() const { return Selected; }
    void ClearSelection()
    {
        Selected.Reset();
        if (List.IsValid()) { List->ClearSelection(); }
    }
    void Refresh()
    {
        const FString Previous = Selected.IsValid() ? ProbeDev20::Identity(*Selected) : FString();
        Entries.Reset();
        for (ProbeDev20::FEntry& Entry : ProbeDev20::Discover())
        { Entries.Add(MakeShared<ProbeDev20::FEntry>(MoveTemp(Entry))); }
        Filter();
        if (!Previous.IsEmpty())
        {
            const TSharedPtr<ProbeDev20::FEntry>* Matching = Visible.FindByPredicate(
                [&Previous](const TSharedPtr<ProbeDev20::FEntry>& Entry)
                { return Entry.IsValid() && ProbeDev20::Identity(*Entry) == Previous; });
            if (Matching && List.IsValid()) { List->SetSelection(*Matching, ESelectInfo::Direct); }
        }
    }

private:
    static constexpr int32 MaxRows = 200;
    FOnProbeCatalogEntry OpenEntry;
    FOnProbeCatalogEntry RunEntry;
    TArray<TSharedPtr<ProbeDev20::FEntry>> Entries;
    TArray<TSharedPtr<ProbeDev20::FEntry>> Visible;
    TSharedPtr<SListView<TSharedPtr<ProbeDev20::FEntry>>> List;
    TSharedPtr<ProbeDev20::FEntry> Selected;
    FString Query;
    FString SourceFilter = TEXT("All");
    FString NativeCategoryFilter;
    int32 MatchCount = 0;

    FString PreviewSelected() const
    {
        if (!Selected.IsValid())
        { return TEXT("Select a definition for its source, steps and authorization requirements. Run Selection executes it through its owner."); }
        if (Selected->Source == ProbeDev20::ESource::NativeAutomation)
        { return TEXT("Unreal Automation: ") + Selected->Id + TEXT("\n") + Selected->Detail
            + TEXT("\nDev 23 observes the Automation controller's exact-path completion record and adds the actual observed outcome to Probe Results and History. Submission alone never counts as success."); }
        if (Selected->Source == ProbeDev20::ESource::AuthoredTest || Selected->Source == ProbeDev20::ESource::AuthoredPack)
        { return FString(ProbeDev20::SourceName(Selected->Source)) + TEXT(": ") + Selected->Locator
            + TEXT("\nAuthor directly in Probe; results are recorded in Probe History."); }
        for (const ProbePortable::FPortableProvider& Provider : ProbePortable::Providers())
        {
            if (Selected->Source == ProbeDev20::ESource::PortableTest)
            {
                if (const ProbePortable::FPortableTest* Definition = Provider.Tests.FindByPredicate(
                    [this](const ProbePortable::FPortableTest& Item) { return Item.Id == Selected->Id; }))
                { return ProbeDev19::PreviewTest(Provider, *Definition); }
            }
            else if (Selected->Source == ProbeDev20::ESource::PortablePack)
            {
                if (const ProbePortable::FPortablePack* Definition = Provider.Packs.FindByPredicate(
                    [this](const ProbePortable::FPortablePack& Item) { return Item.Id == Selected->Id; }))
                { return ProbeDev19::PreviewPack(Provider, *Definition); }
            }
        }
        return TEXT("Definition is no longer discoverable. Refresh Library.");
    }

    void Filter()
    {
        Visible.Reset();
        MatchCount = 0;
        for (const TSharedPtr<ProbeDev20::FEntry>& Entry : Entries)
        {
            if (Entry.IsValid() && ProbeDev20::Matches(*Entry, Query, SourceFilter)
                && (NativeCategoryFilter.IsEmpty()
                    || (Entry->Source == ProbeDev20::ESource::NativeAutomation
                        && (Entry->Category == NativeCategoryFilter
                            || Entry->Category.StartsWith(NativeCategoryFilter + TEXT("."))))))
            {
                ++MatchCount;
                if (Visible.Num() < MaxRows) { Visible.Add(Entry); }
            }
        }
        if (List.IsValid()) { List->RequestListRefresh(); }
        if (!Visible.Contains(Selected)) { Selected.Reset(); }
    }

    TSharedRef<SWidget> BuildSources()
    {
        FMenuBuilder Menu(true, nullptr);
        for (const FString& FilterValue : {
            FString(TEXT("All")), FString(TEXT("Authored Test")), FString(TEXT("Authored Pack")),
            FString(TEXT("Portable Test")), FString(TEXT("Portable Pack"))})
        {
            Menu.AddMenuEntry(FText::FromString(FilterValue), FText::GetEmpty(), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([this, FilterValue]()
                { SourceFilter = FilterValue; NativeCategoryFilter.Empty(); Filter(); })));
        }
        Menu.AddSubMenu(LOCTEXT("Dev21NativeSubmenu", "Unreal Automation"),
            LOCTEXT("Dev21NativeSubmenuTip", "Filter native tests by their registered Automation path"),
            FNewMenuDelegate::CreateLambda([this](FMenuBuilder& SubMenu)
            {
                SubMenu.AddMenuEntry(LOCTEXT("Dev21NativeAll", "All Unreal Automation"), FText::GetEmpty(), FSlateIcon(),
                    FUIAction(FExecuteAction::CreateLambda([this]()
                    { SourceFilter = TEXT("Unreal Automation"); NativeCategoryFilter.Empty(); Filter(); })));
                TMap<FString, int32> Categories;
                for (const TSharedPtr<ProbeDev20::FEntry>& Entry : Entries)
                {
                    if (Entry.IsValid() && Entry->Source == ProbeDev20::ESource::NativeAutomation)
                    { ++Categories.FindOrAdd(Entry->Category.IsEmpty() ? TEXT("Other") : Entry->Category); }
                }
                TArray<FString> Keys;
                Categories.GetKeys(Keys);
                Keys.Sort();
                for (const FString& Category : Keys)
                {
                    SubMenu.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%s (%d)"), *Category, Categories.FindRef(Category))),
                        FText::FromString(TEXT("Show native Automation tests in this registered category")), FSlateIcon(),
                        FUIAction(FExecuteAction::CreateLambda([this, Category]()
                        { SourceFilter = TEXT("Unreal Automation"); NativeCategoryFilter = Category; Filter(); })));
                }
            }));
        return Menu.MakeWidget();
    }

    TSharedRef<ITableRow> MakeRow(TSharedPtr<ProbeDev20::FEntry> Entry,
                                  const TSharedRef<STableViewBase>& Owner)
    {
        return SNew(STableRow<TSharedPtr<ProbeDev20::FEntry>>, Owner)
        [ SNew(SHorizontalBox)
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5,1,8,1)
            [ SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
              .Text(FText::FromString(ProbeDev20::SourceName(Entry->Source))) ]
          + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(Entry->Name)).ToolTipText(FText::FromString(Entry->Id)) ]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,1)
            [ SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
              .Text(FText::FromString(Entry->Owner)) ]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3,0)
            [ SNew(STextBlock).ColorAndOpacity(FLinearColor(0.95f, 0.72f, 0.3f))
              .Text(Entry->bRequiresAuthorization ? LOCTEXT("Dev20Actions", "Actions") : FText::GetEmpty()) ] ];
    }
    void SelectionChanged(TSharedPtr<ProbeDev20::FEntry> Entry, ESelectInfo::Type)
    {
        Selected = Entry;
        if (Selected.IsValid()) { OpenEntry.ExecuteIfBound(*Selected); }
    }
    void RunSelection()
    {
        if (Selected.IsValid()) { RunEntry.ExecuteIfBound(*Selected); }
    }
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
        GConfig->GetBool(TEXT("ProbeWorkspace"), TEXT("Dev16SettingsExpanded"), bDev16SettingsExpanded, GEditorPerProjectIni);
        GConfig->GetBool(TEXT("ProbeWorkspace"), TEXT("Dev16ResultsCollapsed"), bDev16ResultsCollapsed, GEditorPerProjectIni);
        GConfig->GetBool(TEXT("ProbeWorkspace"), TEXT("Dev20LibraryExpanded"), bDev20LibraryExpanded, GEditorPerProjectIni);
        // Configurable project-local retention. Existing history remains untouched
        // when saving is disabled (0); Clear History is always explicit.
        GConfig->GetInt(TEXT("ProbeWorkspace"), TEXT("MaxSavedHistoryRuns"), MaxSavedHistoryRuns, GEditorPerProjectIni);
        MaxSavedHistoryRuns = FMath::Clamp(MaxSavedHistoryRuns, 0, ProbeDev11RunHistory::MaximumSavedRuns);
        if (MaxSavedHistoryRuns > 0)
        { ProbeDev11RunHistory::Prune(ProbeDev11RunHistory::DefaultDirectory(), MaxSavedHistoryRuns); }
        ProbeDev11RunHistory::FSnapshot PreviousSnapshot;
        if (ProbeDev11RunHistory::ReadLatest(ProbeDev11RunHistory::DefaultDirectory(), PreviousSnapshot))
        { LastCompletedResults = MoveTemp(PreviousSnapshot.Results); }
        ProbeDev11RunHistory::FSnapshot Pinned;
        if (ProbeDev12History::LoadPinned(ProbeDev11RunHistory::DefaultDirectory(), Pinned))
        { SelectedHistoryBaselinePath = ProbeDev12History::PinnedPath(ProbeDev11RunHistory::DefaultDirectory()); }
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
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
                            [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                              .ToolTipText(LOCTEXT("Dev19ExportSelectedTip", "Export selected read-only Test Asset to a validated Resources/Probe JSON starter under Saved/Probe/PortableExports. Action and live-input assets are refused."))
                              .OnClicked(this, &SProbeWorkspace::ExportSelectedPortableJson)
                              [ SNew(STextBlock).Text(LOCTEXT("Dev19ExportSelected", "Portable JSON")) ] ]
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
                                .AddMetaData(MakeShared<FDriverIdMetaData>(FName(TEXT("Probe.Workspace.RefreshTests"))))
                                .ToolTipText(LOCTEXT("RefreshTip", "Refresh available Probe tests"))
                                .OnClicked(this, &SProbeWorkspace::RefreshClicked)
                                [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Refresh")) ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6,0)
                            [ SNew(SSeparator).Orientation(Orient_Vertical) ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(3, 0, 2, 0)
                            [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                              .ToolTipText(LOCTEXT("ManagePacksTip", "Open the Probe Packs workspace to edit, validate and run Packs"))
                              .OnClicked_Lambda([]()
                              { FGlobalTabmanager::Get()->TryInvokeTab(Probe::PackTabId); return FReply::Handled(); })
                              [ SNew(STextBlock).Text(LOCTEXT("ManagePacksButton", "Packs")) ] ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2,0,2,0)
                            [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                              .ToolTipText(LOCTEXT("Dev15QABrowserTip", "Search Tests and Packs, inspect readiness and export preflight reports."))
                              .OnClicked_Lambda([]() { ProbeDev15::OpenBrowser(); return FReply::Handled(); })
                              [ SNew(STextBlock).Text(LOCTEXT("Dev15QABrowser", "QA Browser")) ] ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0, 3, 0)
                            [ SNew(SComboButton)
                              .ToolTipText(LOCTEXT("RunHistoryTip", "Browse saved runs, regressions and baselines."))
                              .OnGetMenuContent(this, &SProbeWorkspace::BuildHistoryMenu)
                              .ButtonContent()[ SNew(STextBlock).Text(LOCTEXT("RunHistory", "History")) ] ]
                            + SHorizontalBox::Slot().FillWidth(1.0f)
                            [ SNullWidget::NullWidget ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5,0,3,0)
                            [ SNew(SComboButton)
                              .ToolTipText(LOCTEXT("RunPackTip", "Run an existing Probe Pack. Manage Packs in the Packs workspace."))
                              .OnGetMenuContent(this, &SProbeWorkspace::BuildPackMenu)
                              .ButtonContent()[ SNew(STextBlock).Text(LOCTEXT("RunPack", "Run Pack")) ] ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 3, 0)
                            [ SNew(STextBlock)
                              .Visibility_Lambda([this]() { return bNativeRun ? EVisibility::Visible : EVisibility::Collapsed; })
                              .Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("%s: %s (%.1fs)"), *NativeExecution.PhaseLabel(), *FPaths::GetCleanFilename(NativePath), FMath::Max(0.0, FPlatformTime::Seconds() - NativeStartSeconds))); })
                              .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 3, 0)
                            [
                                SNew(SButton)
                                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                .ToolTipText_Lambda([this]() { return bNativeRun
                                    ? LOCTEXT("StopNativeMonitoringTip", "Stop the controller-owned test (if running), or stop discovery. Probe records Interrupted without claiming a test passed or was fully cancelled.")
                                    : LOCTEXT("StopAfterCurrentTip", "Finish the current test then safely skip queued tests."); })
                                .Visibility_Lambda([this]() { return bRunInProgress ? EVisibility::Visible : EVisibility::Collapsed; })
                                .IsEnabled_Lambda([this]() { return bRunInProgress && (bPortableRun || !bStopAfterCurrentTest); })
                                .OnClicked(this, &SProbeWorkspace::StopOrCancel)
                                [ SNew(STextBlock).Text_Lambda([this]() { return bPortableRun
                                    ? LOCTEXT("Dev20CancelPortable", "Cancel Portable Run")
                                    : bNativeRun ? LOCTEXT("Dev26StopNative", "Stop Native Run")
                                    : LOCTEXT("StopAfterCurrent", "Stop After Test"); }) ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 3, 0)
                            [
                                SNew(SButton)
                                .ToolTipText(LOCTEXT("RunAllTip", "Run all authored Probe Test Assets; use Test Library for portable and Unreal Automation Tests"))
                                .OnClicked(this, &SProbeWorkspace::RunAll)
                                [ SNew(STextBlock).Text(LOCTEXT("RunAll", "Run All Authored")) ]
                            ]
                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                            [
                                SNew(SButton)
                                .IsEnabled_Lambda([this]() { return HasSelectedAsset() || (UnifiedCatalog.IsValid() && UnifiedCatalog->Selection().IsValid()); })
                                .AddMetaData(MakeShared<FDriverIdMetaData>(FName(TEXT("Probe.Workspace.RunSelected"))))
                                .ToolTipText(LOCTEXT("RunSelectedTip", "Run selected Test Library entry, or the currently open authored Test if none is chosen"))
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
            + SVerticalBox::Slot().FillHeight(1.0f).Padding(7, 0, 7, 0)
            [
                SNew(SSplitter).Orientation(Orient_Vertical)
                + SSplitter::Slot().Value(TAttribute<float>::Create(
                    TAttribute<float>::FGetter::CreateLambda([this]() { return bResultsFocused ? 0.0f : (bDev16ResultsCollapsed ? 1.0f : DefinitionFraction); })))
                .OnSlotResized(SSplitter::FOnSlotResized::CreateSP(this, &SProbeWorkspace::OnDefinitionResized))
                [
                    SNew(SBox)
                // A zero-height splitter slot does not suppress children's desired sizes.
                // Collapse the entire upper workspace while Results has focus.
                .Visibility_Lambda([this]() { return bResultsFocused ? EVisibility::Collapsed : EVisibility::Visible; })
                [ SNew(SSplitter).Orientation(Orient_Horizontal)
                    + SSplitter::Slot().Value(0.36f)
                    [
                        SNew(SExpandableArea).InitiallyCollapsed(!bDev20LibraryExpanded)
              .OnAreaExpansionChanged_Lambda([this](bool bExpanded)
              { bDev20LibraryExpanded = bExpanded;
                GConfig->SetBool(TEXT("ProbeWorkspace"), TEXT("Dev20LibraryExpanded"), bExpanded, GEditorPerProjectIni); })
              .HeaderContent()
                [ SNew(STextBlock).Text(LOCTEXT("Dev20LibraryTitle", "Test Library  |  Authored  •  Portable  •  Unreal Automation"))
                  .Font(FAppStyle::GetFontStyle("NormalFontBold")) ]
              .BodyContent()
                [ SNew(SBox).MinDesiredHeight(340.f)
                  [ SAssignNew(UnifiedCatalog, SProbeUnifiedCatalog)
                    .OnOpen(FOnProbeCatalogEntry::CreateSP(this, &SProbeWorkspace::OpenCatalogEntry))
                    .OnRun(FOnProbeCatalogEntry::CreateSP(this, &SProbeWorkspace::RunCatalogEntry)) ] ]
                    ]
                    + SSplitter::Slot().Value(0.64f)
                    [
                    SNew(SVerticalBox)
                    .Visibility_Lambda([this]() { return bResultsFocused ? EVisibility::Collapsed : EVisibility::Visible; })
                    + SVerticalBox::Slot().AutoHeight().Padding(2, 3, 2, 6)
                    [
                                        SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                    [ SNew(STextBlock).Text(LOCTEXT("SelectedTest", "Authored Test Editor")) ]
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
                    + SVerticalBox::Slot().AutoHeight().Padding(2, 3, 2, 6)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                        [ SNew(STextBlock).Text(LOCTEXT("DefinitionTitle", "Test Definition")) ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
                        [
                            SNew(SComboButton)
                            .ToolTipText(LOCTEXT("OpenWidgetTargetBrowser", "Find explicitly tagged widgets in currently visible Unreal Editor windows. Copy stable id: selectors for your UI steps."))
                            .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
                            {
                                return SNew(SProbeWidgetTargetPicker)
                                    .CanCreateSteps(HasSelectedAsset())
                                    .OnCreateStep(FOnProbeWidgetTargetCreateStep::CreateSP(this, &SProbeWorkspace::AddWidgetTargetStep));
                            })
                            .ButtonContent()
                            [ SNew(STextBlock).Text(LOCTEXT("WidgetTargets", "Widget Targets")) ]
                        ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
                        [
                            SNew(SComboButton)
                            .IsEnabled(this, &SProbeWorkspace::HasSelectedAsset)
                            .ToolTipText(LOCTEXT("CheckTargetsTip", "Read-only live preflight for every native UI selector in this Test Asset"))
                            .OnGetMenuContent(this, &SProbeWorkspace::BuildWidgetTargetPreflight)
                            .ButtonContent()
                            [ SNew(STextBlock).Text(LOCTEXT("CheckTargets", "Check Targets")) ]
                        ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                        [
                            SNew(SComboButton)
                            .IsEnabled(this, &SProbeWorkspace::HasSelectedAsset)
                            .OnGetMenuContent(this, &SProbeWorkspace::BuildAddStepMenu)
                            .AddMetaData(MakeShared<FDriverIdMetaData>(FName(TEXT("Probe.Workspace.AddStep"))))
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
                            SAssignNew(DefinitionScroll, SScrollBox)
                            + SScrollBox::Slot()
                            [ SAssignNew(EditorRows, SVerticalBox) ]
                        ]
                    ]
                    ]
                ] ]
                + SSplitter::Slot().Value(TAttribute<float>::Create(
                    TAttribute<float>::FGetter::CreateLambda([this]() { return bResultsFocused ? 1.0f : (bDev16ResultsCollapsed ? 0.0f : 1.0f - DefinitionFraction); })))
                [
                    SNew(SVerticalBox)
                    .Visibility_Lambda([this]() { return (bDev16ResultsCollapsed && !bResultsFocused)
                        ? EVisibility::Collapsed : EVisibility::Visible; })
                    + SVerticalBox::Slot().AutoHeight().Padding(2, 6, 2, 4)
                    [
                        SNew(SHorizontalBox)
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
                        [ SNew(STextBlock).Text(LOCTEXT("Results", "Results")) ]
                        + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                        [ SNew(SScrollBox).Orientation(Orient_Horizontal).ScrollBarVisibility(EVisibility::Visible)
                          + SScrollBox::Slot() [ SAssignNew(ResultSummary, SHorizontalBox) ] ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,5,0)
                        [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                          .ToolTipText(LOCTEXT("Dev16ExportFromResultsTip", "Export current results to Saved/Probe/Reports as JSON."))
                          .IsEnabled(this, &SProbeWorkspace::HasResults)
                          .OnClicked(this, &SProbeWorkspace::ExportReport)
                          [ SNew(STextBlock).Text(LOCTEXT("Dev16ExportFromResults", "Export")) ] ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,5,0)
                        [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                          .ToolTipText(LOCTEXT("Dev16RerunProblemsTip", "Rerun only failed or errored tests."))
                          .IsEnabled(this, &SProbeWorkspace::HasProblemResults)
                          .OnClicked(this, &SProbeWorkspace::RerunProblems)
                          [ SNew(STextBlock).Text(LOCTEXT("Dev16RerunProblems", "Rerun Problems")) ] ]
                        + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,5,0)
                        [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                          .ToolTipText(LOCTEXT("Dev16CollapseResultsTip", "Collapse results and return screen space to Test authoring."))
                          .OnClicked(this, &SProbeWorkspace::ToggleDev16ResultsCollapsed)
                          [ SNew(STextBlock).Text(LOCTEXT("Dev16CollapseResults", "Collapse")) ] ]
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
            + SVerticalBox::Slot().AutoHeight().Padding(7,3,7,3)
            [ SNew(SBox).Visibility_Lambda([this]()
              { return bDev16ResultsCollapsed && !bResultsFocused ? EVisibility::Visible : EVisibility::Collapsed; })
              [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                .ToolTipText(LOCTEXT("Dev16ExpandResultsTip", "Expand the resizable Results Explorer and execution diagnostics panel."))
                .OnClicked(this, &SProbeWorkspace::ToggleDev16ResultsCollapsed)
                [ SNew(SHorizontalBox)
                  + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                  [ SNew(STextBlock).Text_Lambda([this]()
                    { return FText::FromString(FString::Printf(TEXT("Results  >  %d test(s)  |  %s"),
                        Results.Num(), bRunInProgress ? TEXT("Running") : Results.IsEmpty() ? TEXT("No results yet") : TEXT("Click to inspect"))); }) ]
                  + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                  [ SNew(STextBlock).Text(LOCTEXT("Dev16ExpandResults", "Expand")) ] ] ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(8, 1, 8, 1)
            [ SNew(SBox).Visibility_Lambda([this]() { return bRunInProgress ? EVisibility::Visible : EVisibility::Collapsed; })
              [ SNew(SHorizontalBox)
              + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center).Padding(0,0,10,0)
              [ SNew(STextBlock).Text(this, &SProbeWorkspace::Dev14LiveProgressText)
                .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
              + SHorizontalBox::Slot().FillWidth(0.7f).VAlign(VAlign_Center)
              [ SNew(SProgressBar).Percent(this, &SProbeWorkspace::Dev14LiveProgressFraction) ] ] ]
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
        NativeBridge.Stop();
        NativeExecution.Reset();
        FEditorDelegates::PostUndoRedo.RemoveAll(this);
    }

    virtual bool SupportsKeyboardFocus() const override { return true; }

    virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override
    {
        if (InKeyEvent.IsControlDown() && InKeyEvent.GetKey() == EKeys::F && Dev15StepSearchBox.IsValid())
        {
            FSlateApplication::Get().SetKeyboardFocus(Dev15StepSearchBox, EFocusCause::SetDirectly);
            return FReply::Handled();
        }
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
        if (bRunInProgress)
        {
            FeedbackMessage = LOCTEXT("ProbeRunAlreadyActive", "A Probe run is already in progress. Wait for it to finish before starting another.");
            return;
        }
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

public:
    void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override
    {
        SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
        if (!bNativeRun) { return; }
        NativeExecution.Tick();
        ProbeDev23::FObservedRun Observation;
        const bool bLogCompleted = NativeBridge.Read(Observation);
        if (NativeExecution.Phase() == ProbeDev26::EPhase::Error)
        {
            FinishNativeRun(Observation, NativeExecution.Error());
        }
        else if (NativeExecution.Phase() == ProbeDev26::EPhase::Complete)
        {
            ProbeDev25::FStructuredResult Structured;
            if (ProbeDev25::TryReadCompletedReport(NativePath, Structured))
            {
                // Completion delegate for this exact API-owned run plus exact-path structured
                // report is sufficient; log messages are supplemental, not required.
                Observation.bCompleted = true;
                Observation.Outcome = Structured.State;
                Observation.DurationMs = Structured.DurationMs;
                FinishNativeRun(Observation, FString());
            }
            else if (bLogCompleted)
            {
                FinishNativeRun(Observation, FString());
            }
            else if (FPlatformTime::Seconds() >= NativeDeadline)
            {
                FinishNativeRun(Observation, TEXT("Controller completion arrived, but no exact-path terminal report was available. Probe cannot infer an outcome."));
            }
        }
        else if (FPlatformTime::Seconds() >= NativeDeadline)
        {
            NativeExecution.Stop();
            FinishNativeRun(Observation, TEXT("Native Automation exceeded the 180-second execution deadline. Probe did not infer success."));
        }
    }

private:
    void FinishNativeRun(const ProbeDev23::FObservedRun& Observation, const FString& Interruption)
    {
        NativeBridge.Stop();
        bNativeRun = false;
        bRunInProgress = false;
        Probe::FTestResult Result;
        Result.Name = NativePath;
        Result.Id = NativePath;
        Result.AssetPath = TEXT("unreal-automation://") + NativePath;
        Result.DurationMs = Observation.bCompleted ? Observation.DurationMs
            : FMath::Max(0.0, (FPlatformTime::Seconds() - NativeStartSeconds) * 1000.0);
        Result.Status = Interruption.IsEmpty() ? ProbeDev23::StatusFromOutcome(Observation.Outcome)
            : (bUserStoppedNativeObservation ? Probe::EResultStatus::Interrupted : Probe::EResultStatus::Error);
        bUserStoppedNativeObservation = false;
        Result.Detail = Interruption.IsEmpty() ? TEXT("AutomationController reported ") + Observation.Outcome : Interruption;
        // Read the controller's structured result only AFTER a matching completion event.
        // This prevents a cached success from an earlier run being attributed to this one.
        ProbeDev25::FStructuredResult Structured;
        if (Interruption.IsEmpty() && Observation.bCompleted
            && ProbeDev25::TryReadCompletedReport(NativePath, Structured)
            && ProbeDev25::ConvertState(Structured.State) == ProbeDev23::StatusFromOutcome(Observation.Outcome))
        {
            Result.Status = ProbeDev25::ConvertState(Structured.State);
            Result.DurationMs = Structured.DurationMs;
            Result.Detail = FString::Printf(TEXT("AutomationController structured report: %s | %d warnings | %d errors"),
                *Structured.State, Structured.Warnings, Structured.Errors);
            if (!Structured.Device.IsEmpty()) { Result.Detail += TEXT("\nDevice cluster: ") + Structured.Device; }
            if (!Structured.GameInstance.IsEmpty()) { Result.Detail += TEXT("\nGame instance: ") + Structured.GameInstance; }
            for (const FString& Event : Structured.Events) { Result.Detail += TEXT("\n") + Event; }
        }
        else if (Interruption.IsEmpty() && Observation.bCompleted)
        {
            Result.Detail += TEXT("\nStructured controller report unavailable; authoritative completion-log fallback used.");
        }
        if (Observation.bStarted) { Result.Detail += TEXT("\nAutomationController confirmed test start."); }
        if (Observation.Events.Num() > 0) { Result.Detail += FString::Printf(TEXT("\nController diagnostic events (%d, capped at 64):"), Observation.Events.Num()); }
        for (const FString& Event : Observation.Events)
        { Result.Detail += TEXT("\n") + Event; }
        Results.Reset();
        Results.Add(MoveTemp(Result));
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        LastComparison = ProbeDev10RunHistory::Compare(LastCompletedResults, Results);
        LastCompletedResults = Results;
        LastRunTime = FDateTime::Now();
        LastRunDurationMs = Results[0].DurationMs;
        bViewingSavedHistory = false;
        if (MaxSavedHistoryRuns > 0)
        {
            ProbeDev11RunHistory::FSnapshot Snapshot;
            Snapshot.TimestampUtc = FDateTime::UtcNow();
            Snapshot.EngineVersion = FEngineVersion::Current().ToString();
            Snapshot.Results = Results;
            if (!ProbeDev11RunHistory::Save(ProbeDev11RunHistory::DefaultDirectory(), Snapshot,
                nullptr, MaxSavedHistoryRuns))
            { UE_LOG(LogTemp, Warning, TEXT("Probe: could not save native Automation result.")); }
        }
        FeedbackMessage = FText::FromString(Results[0].Detail);
        ShowResults();
        NativeExecution.Reset();
        NativePath.Empty();
    }

    TArray<TSharedPtr<FAssetData>> Assets;
    TSharedPtr<FAssetData> Selected;
    TSharedPtr<SComboBox<TSharedPtr<FAssetData>>> TestSelector;
    TSharedPtr<SProbeUnifiedCatalog> UnifiedCatalog;
    TSharedPtr<SVerticalBox> EditorRows;
    TSharedPtr<SScrollBox> DefinitionScroll;
    TMap<int32, TWeakPtr<SWidget>> StepRowWidgets;
    TSharedPtr<SHorizontalBox> ResultSummary;
    TSharedPtr<SMultiLineEditableTextBox> ResultsLog;
    TSharedPtr<SVerticalBox> ResultList;
    TSharedPtr<SVerticalBox> ResultDetails;
    TSharedPtr<SWidgetSwitcher> ResultViewSwitcher;
    TSharedPtr<SHorizontalBox> ResultTabs;
    TArray<Probe::FTestResult> Results;
    TArray<int32> QueuedTestResultIndices;
    TMap<int32, Probe::FRunOptions> ScopedRunOptions;
    bool bRunInProgress = false;
    bool bPortableRun = false;
    bool bNativeRun = false;
    bool bUserStoppedNativeObservation = false;
    double NativeStartSeconds = 0.0;
    ProbeDev23::FNativeLogBridge NativeBridge;
    ProbeDev26::FNativeExecution NativeExecution;
    double NativeDeadline = 0.0;
    FString NativePath;
    bool bDev20LibraryExpanded = true;
    bool bSelectingFromCatalog = false;
    bool bStopAfterCurrentTest = false;
    bool bStopDueToPackFailure = false;
    TStrongObjectPtr<UProbeTestPack> ActivePackForRun; // Retain policy asset until async queue drains.
    TMap<int32, int32> PackCompletedAttempts;
    enum class EResultFilter : uint8 { All, Passed, Failed, Skipped, Error, Interrupted };
    EResultFilter ActiveResultFilter = EResultFilter::All;
    int32 SelectedResultIndex = INDEX_NONE;
    bool bProblemsFirst = true;
    bool bProblemStepsOnly = false;
    bool bShowLog = false;
    bool bResultsFocused = false;
    float DefinitionFraction = 0.64f;
    TSet<int32> ExpandedDiagnosticIndices;
    FString ExpandedDiagnosticTestPath;
    TSet<int32> ExpandedStepIndices;
    TSet<int32> SelectedBulkStepIndices;
    FString BulkGroupName;
    FString Dev15StepQuery;
    TSharedPtr<SSearchBox> Dev15StepSearchBox; // SSearchBox has no .Text Slate argument in UE 5.8; restore through SetText().
    FString Dev15RecipeName;
    FString Dev16RecipeSearch;
    TWeakPtr<SWindow> Dev16RecipeBrowserWindow;
    TSharedPtr<SVerticalBox> Dev16RecipeBrowserRows;
    TSet<FString> Dev16CollapsedGroups;
    bool bDev16SettingsExpanded = false;
    FString Dev16OverviewCache;
    bool bDev16ResultsCollapsed = true;
    FString ExpandedAssetPath;
    int32 ActiveExecutionResult = INDEX_NONE;
    int32 ActiveExecutionStep = 0;
    int32 ActiveExecutionStepCount = 0;
    double ActiveExecutionStartSeconds = 0.0;
    bool bStepNotesShown = false;
    FDateTime LastRunTime;
    double LastRunDurationMs = 0.0;
    TArray<Probe::FTestResult> LastCompletedResults;
    ProbeDev10RunHistory::FComparison LastComparison;
    FString SelectedHistoryBaselinePath; // file path; a pinned baseline survives editor restarts
    int32 MaxSavedHistoryRuns = ProbeDev11RunHistory::DefaultSavedRuns;
    FString HistorySearch;
    ProbeDev12History::EHistoryStatusFilter HistoryStatusFilter = ProbeDev12History::EHistoryStatusFilter::All;
    bool bViewingSavedHistory = false;
    TWeakPtr<SWindow> HistoryBrowserWindow;

    void OnDefinitionResized(float NewFraction)
    {
        if (!bResultsFocused && !bDev16ResultsCollapsed)
        {
            DefinitionFraction = FMath::Clamp(NewFraction, 0.20f, 0.85f);
            GConfig->SetFloat(TEXT("ProbeWorkspace"), TEXT("DefinitionFraction"), DefinitionFraction, GEditorPerProjectIni);
        }
    }

    FReply ToggleResultsFocus()
    {
        bDev16ResultsCollapsed = false;
        bResultsFocused = !bResultsFocused;
        return FReply::Handled();
    }

    FReply ToggleDev16ResultsCollapsed()
    {
        bResultsFocused = false;
        bDev16ResultsCollapsed = !bDev16ResultsCollapsed;
        GConfig->SetBool(TEXT("ProbeWorkspace"), TEXT("Dev16ResultsCollapsed"), bDev16ResultsCollapsed, GEditorPerProjectIni);
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
        if (UnifiedCatalog.IsValid() && !bRunInProgress) { UnifiedCatalog->Refresh(); }
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
        Dev16OverviewCache = ProbeDev16::TestSummary(*Asset);
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
            Dev16OverviewCache = ProbeDev16::TestSummary(*Asset);
            Asset->MarkPackageDirty();
        }
        if (bRebuild && SelectedAsset() == Asset) { RebuildTestEditor(); }
    }

    void HandleUndoRedo()
    {
        SelectedBulkStepIndices.Reset();
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
                || Type == EProbeStepType::ActorDoesNotHaveTagByLabel
                || Type == EProbeStepType::SelectionSelectActorByLabel);
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

    TSharedRef<SWidget> BuildWidgetTargetPreflight()
    {
        TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
        Body->AddSlot().AutoHeight().Padding(8, 6, 8, 3)
        [ SNew(STextBlock).Text(LOCTEXT("PreflightTitle", "Native UI Target Preflight"))
          .Font(FAppStyle::GetFontStyle("HeadingExtraSmall")) ];
        const UProbeTestAsset* Asset = SelectedAsset();
        if (!Asset) { return SNullWidget::NullWidget; }
        const ProbeWidgetTargetAuthoring::FPreflightReport Report =
            ProbeWidgetTargetAuthoring::AnalyzeTargets(*Asset, ProbeWidgetDiscovery::Collect());
        TSharedRef<SVerticalBox> Items = SNew(SVerticalBox);
        for (const ProbeWidgetTargetAuthoring::FPreflightItem& Item : Report.Items)
        {
            Items->AddSlot().AutoHeight().Padding(8, 3)
            [ SNew(SVerticalBox)
              + SVerticalBox::Slot().AutoHeight()
              [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%d. %s"), Item.StepIndex + 1, *Item.Selector))) ]
              + SVerticalBox::Slot().AutoHeight()
              [ SNew(STextBlock).AutoWrapText(true).Text(FText::FromString(Item.Resolution.Description()
                    + (Item.bMissingIsExpected ? TEXT(" (absence satisfies this condition)") : TEXT(""))))
                .Font(FAppStyle::GetFontStyle("SmallFont"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ];
        }
        Body->AddSlot().AutoHeight().Padding(8, 3, 8, 5)
        [ SNew(STextBlock).Text(FText::FromString(FString::Printf(
            TEXT("%d targets | %d unique | %d missing | %d expected absent | %d ambiguous | %d native Details (deferred) | %d unsupported"),
            Report.Total(), Report.Unique, Report.Missing, Report.ExpectedAbsent, Report.Ambiguous,
            Report.DeferredNativeDetails, Report.Unsupported))) ];
        if (Report.Total() == 0)
        {
            Body->AddSlot().AutoHeight().Padding(8)
            [ SNew(STextBlock).Text(LOCTEXT("PreflightEmpty", "This Test has no native UI widget-selector steps.")) ];
        }
        Body->AddSlot().FillHeight(1).Padding(3)
        [ SNew(SScrollBox) + SScrollBox::Slot()[ Items ] ];
        Body->AddSlot().AutoHeight().Padding(8, 5)
        [ SNew(STextBlock).AutoWrapText(true)
          .Text(LOCTEXT("PreflightHelp", "Read-only snapshot, not a test result. Missing targets may appear after you open their panels; details:search resolves at execution time."))
          .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ];
        return SNew(SBox).WidthOverride(520.0f).HeightOverride(360.0f)[ Body ];
    }

    // Diagnostic snapshots never modify a Test Asset or invoke Automation Driver
    // input. Missing windows are hints, not permanent configuration failures.
    TSharedRef<SWidget> BuildWidgetTargetDiagnostics(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        const UProbeTestAsset* Test = TestRef.Get();
        if (!Test || !Test->Steps.IsValidIndex(Index)) { return SNullWidget::NullWidget; }
        const FProbeTestStep& Step = Test->Steps[Index];
        const ProbeWidgetDiscovery::FResolution Result =
            ProbeWidgetDiscovery::Resolve(ProbeWidgetDiscovery::Collect(), Step.Target);
        TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
        Body->AddSlot().AutoHeight().Padding(8, 6, 8, 3)
        [ SNew(STextBlock).Text(LOCTEXT("TargetDiagnosticTitle", "Live Target Inspector"))
          .Font(FAppStyle::GetFontStyle("HeadingExtraSmall")) ];
        Body->AddSlot().AutoHeight().Padding(8, 2)
        [ SNew(STextBlock).Text(FText::FromString(Step.Target)) ];
        Body->AddSlot().AutoHeight().Padding(8, 2)
        [ SNew(STextBlock).AutoWrapText(true)
          .Text(FText::FromString(Result.Description()))
          .ColorAndOpacity(Result.State == ProbeWidgetDiscovery::EResolution::Ambiguous
              || Result.State == ProbeWidgetDiscovery::EResolution::Unsupported
              ? FSlateColor(FLinearColor(1.0f, 0.50f, 0.35f))
              : FSlateColor::UseSubduedForeground()) ];
        if (Result.CanUse())
        {
            const ProbeWidgetDiscovery::FCandidate& Candidate = Result.Candidate;
            Body->AddSlot().AutoHeight().Padding(8, 2)
            [ SNew(STextBlock).AutoWrapText(true)
              .Text(FText::FromString(Candidate.bHasReadableText
                  ? TEXT("Readable text: ") + Candidate.ReadableText : TEXT("Direct text: unavailable for this widget type"))) ];
            if (Candidate.bHasReadableText)
            {
                Body->AddSlot().AutoHeight().Padding(8, 4)
                [ SNew(SHorizontalBox)
                  + SHorizontalBox::Slot().AutoWidth()
                  [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                    .OnClicked_Lambda([Text = Candidate.ReadableText]()
                    { FPlatformApplicationMisc::ClipboardCopy(*Text); return FReply::Handled(); })
                    [ SNew(STextBlock).Text(LOCTEXT("TargetCopyObservedText", "Copy Observed Text")) ] ]
                  + SHorizontalBox::Slot().AutoWidth().Padding(8, 0, 0, 0)
                  [ SNew(SButton)
                    .IsEnabled(Step.Type == EProbeStepType::UIWidgetTextEquals
                        || Step.Type == EProbeStepType::UIWidgetTextContains
                        || Step.Type == EProbeStepType::UIWidgetTextNotEquals
                        || Step.Type == EProbeStepType::UIWaitForWidgetTextEquals
                        || Step.Type == EProbeStepType::UIWaitForWidgetTextContains
                        || Step.Type == EProbeStepType::UIWaitForWidgetTextNotEquals)
                    .ToolTipText(LOCTEXT("CaptureObservedTextHelp", "Replace Expected Value with the observed text. This is an undoable asset change."))
                    .OnClicked_Lambda([this, TestRef, Index, Selector = Step.Target]()
                    {
                        FSlateApplication::Get().DismissAllMenus();
                        // Re-check that this selector is still unique before committing.
                        const auto Latest = ProbeWidgetDiscovery::Resolve(ProbeWidgetDiscovery::Collect(), Selector);
                        if (!Latest.CanUse() || !Latest.Candidate.bHasReadableText)
                        {
                            FeedbackMessage = LOCTEXT("CaptureTargetStale", "Widget changed or disappeared; inspect it again.");
                        }
                        else
                        {
                            ChangeStep(TestRef, Index, LOCTEXT("CaptureObservedWidgetText", "Capture Probe Widget Text"),
                                [Text = Latest.Candidate.ReadableText](FProbeTestStep& Mutable) { Mutable.ExpectedValue = Text; }, true);
                            FeedbackMessage = LOCTEXT("CapturedWidgetText", "Expected text captured from current widget.");
                        }
                        return FReply::Handled();
                    })
                    [ SNew(STextBlock).Text(LOCTEXT("CaptureObservedText", "Set as Expected")) ] ] ];
            }
        }
        Body->AddSlot().AutoHeight().Padding(8, 4, 8, 7)
        [ SNew(STextBlock).AutoWrapText(true)
          .Text(LOCTEXT("TargetInspectorCaveat", "Snapshot only. Reopen Inspect to refresh. A target may be unavailable until its owning panel is opened. This does not run a test."))
          .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ];
        return SNew(SBox).WidthOverride(460.0f)[ Body ];
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
                    if (Field != EStepTextField::Target) { return EVisibility::Collapsed; }
                    const UProbeTestAsset* Test = TestRef.Get();
                    if (!Test || !Test->Steps.IsValidIndex(Index) || !Test->Steps[Index].AssertionId.IsNone())
                    { return EVisibility::Collapsed; }
                    const EProbeStepType Type = Test->Steps[Index].Type;
                    return ProbeNativeUIPolicy::HasTarget(Type)
                        ? EVisibility::Visible : EVisibility::Collapsed;
                })
                .ToolTipText(LOCTEXT("FindWidgetTargetTip", "Discover live Automation Driver IDs and insert a unique widget target"))
                .OnGetMenuContent_Lambda([this, TestRef, Index]() -> TSharedRef<SWidget>
                {
                    return SNew(SProbeWidgetTargetPicker)
                        .OnChosen(FOnProbeWidgetTargetChosen::CreateLambda([this, TestRef, Index](const FString& Selector)
                        {
                            ChangeStep(TestRef, Index, LOCTEXT("ChooseWidgetTargetTxn", "Choose Probe Widget Target"),
                                [Selector](FProbeTestStep& Step) { Step.Target = Selector; }, true);
                            FeedbackMessage = FText::FromString(TEXT("Widget target selected: ") + Selector);
                        }));
                })
                .ButtonContent()
                [ SNew(STextBlock).Text(LOCTEXT("FindWidgetTargetButton", "Find...")) ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0, 0, 0)
            [
                SNew(SComboButton)
                .Visibility_Lambda([TestRef, Index, Field]()
                {
                    if (Field != EStepTextField::Target) { return EVisibility::Collapsed; }
                    const UProbeTestAsset* Test = TestRef.Get();
                    if (!Test || !Test->Steps.IsValidIndex(Index) || !Test->Steps[Index].AssertionId.IsNone())
                    { return EVisibility::Collapsed; }
                    const EProbeStepType Type = Test->Steps[Index].Type;
                    return ProbeNativeUIPolicy::HasTarget(Type)
                        ? EVisibility::Visible : EVisibility::Collapsed;
                })
                .ToolTipText(LOCTEXT("InspectTargetTip", "Inspect the current live widget without running the test or modifying the Editor"))
                .OnGetMenuContent_Lambda([this, TestRef, Index]() -> TSharedRef<SWidget>
                { return BuildWidgetTargetDiagnostics(TestRef, Index); })
                .ButtonContent()
                [ SNew(STextBlock).Text(LOCTEXT("InspectTargetButton", "Inspect")) ]
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
                    const bool bClass = Type == EProbeStepType::FixtureCreateObject || Type == EProbeStepType::ActorFixtureSpawn
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
                    if (Type == EProbeStepType::ActorFixtureChildCountExactly) { return LOCTEXT("FixtureChildrenExact", "Exact Child Count"); }
                    if (Type == EProbeStepType::ActorFixtureChildCountAtLeast) { return LOCTEXT("FixtureChildrenMinimum", "Minimum Children"); }
                    if (Type == EProbeStepType::ActorFixtureChildCountAtMost) { return LOCTEXT("FixtureChildrenMaximum", "Maximum Children"); }
                    if (Type == EProbeStepType::ActorFixtureTagCountExactly) { return LOCTEXT("FixtureTagsExact", "Exact Tag Count"); }
                    if (Type == EProbeStepType::ActorFixtureDistanceAtLeast) { return LOCTEXT("FixtureDistanceMinimum", "Minimum Distance (cm)"); }
                    if (Type == EProbeStepType::ActorFixtureDistanceAtMost) { return LOCTEXT("FixtureDistanceMaximum", "Maximum Distance (cm)"); }
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

    void AddUIWaitTimeoutField(const TSharedRef<SVerticalBox>& Body, TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        Body->AddSlot().AutoHeight().Padding(2, 2)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
            [ SNew(SBox).WidthOverride(134.0f)
              [ SNew(STextBlock).Text(LOCTEXT("UIWaitTimeout", "Wait Timeout (s)"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
            [
                SNew(SNumericEntryBox<double>)
                .MinValue(0.1).MaxValue(30.0).MinSliderValue(0.1).MaxSliderValue(30.0)
                .AllowSpin(true)
                .Value_Lambda([TestRef, Index]() -> TOptional<double>
                {
                    const UProbeTestAsset* Asset = TestRef.Get();
                    return Asset && Asset->Steps.IsValidIndex(Index)
                        ? TOptional<double>(Asset->Steps[Index].UIWaitTimeoutSeconds) : TOptional<double>();
                })
                .OnValueCommitted_Lambda([this, TestRef, Index](double Value, ETextCommit::Type)
                {
                    if (!FMath::IsFinite(Value)) { return; }
                    const double Clamped = FMath::Clamp(Value, 0.1, 30.0);
                    const UProbeTestAsset* Test = TestRef.Get();
                    if (!Test || !Test->Steps.IsValidIndex(Index)
                        || Test->Steps[Index].UIWaitTimeoutSeconds == Clamped) { return; }
                    ChangeStep(TestRef, Index, LOCTEXT("UIWaitTimeoutEdit", "Edit Probe UI Wait Timeout"),
                        [Clamped](FProbeTestStep& Step) { Step.UIWaitTimeoutSeconds = Clamped; });
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
        if (Step.AssertionId.IsNone() && (ProbeNativeUIPolicy::IsWait(Step.Type)))
        { Summary += FString::Printf(TEXT("  |  wait: %.1f s"), Step.UIWaitTimeoutSeconds); }
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
                || Step.Type == EProbeStepType::ActorLabelCountExactly
                || Step.Type == EProbeStepType::ActorFixtureChildCountExactly
                || Step.Type == EProbeStepType::ActorFixtureTagCountExactly;
            const bool bMaximum = Step.Type == EProbeStepType::ActorCountAtMost
                || Step.Type == EProbeStepType::ActorClassCountAtMost
                || Step.Type == EProbeStepType::SelectedActorCountAtMost
                || Step.Type == EProbeStepType::ActorFolderCountAtMost
                || Step.Type == EProbeStepType::ActorCountWithTagAtMost
                || Step.Type == EProbeStepType::ActorLabelCountAtMost
                || Step.Type == EProbeStepType::ActorFixtureDistanceAtMost
                || Step.Type == EProbeStepType::ActorFixtureChildCountAtMost;
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

    // Drilldown is generated from the same catalog metadata for Add Step and
    // Change Assertion, including third-party extension registrations.
    TSharedRef<SWidget> BuildStepTypeMenu(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        FMenuBuilder Menu(true, nullptr);
        for (const FString& Category : Probe::AllCategories())
        {
            Menu.AddSubMenu(FText::FromString(Category), FText::FromString(Category + TEXT(" step types")),
                FNewMenuDelegate::CreateLambda([this, TestRef, Index, Category](FMenuBuilder& CategoryMenu)
                {
                    TArray<FString> Groups;
                    for (const Probe::FAssertionInfo& Info : Probe::AssertionCatalog())
                    {
                        if (Category == Info.Category) { Groups.AddUnique(Probe::MenuSubgroup(Category, Info.Name)); }
                    }
                    for (const FName Id : Probe::GetExtensionAssertionIds())
                    {
                        if (const Probe::FExtensionAssertion* Info = Probe::FindExtensionAssertion(Id))
                        { if (Category == Info->Category) { Groups.AddUnique(Probe::MenuSubgroup(Category, Info->Name)); } }
                    }
                    Groups.Sort();
                    for (const FString& Group : Groups)
                    {
                        CategoryMenu.AddSubMenu(FText::FromString(Group), FText::FromString(Category + TEXT(" / ") + Group),
                            FNewMenuDelegate::CreateLambda([this, TestRef, Index, Category, Group](FMenuBuilder& GroupMenu)
                            {
                                for (const Probe::FAssertionInfo& Info : Probe::AssertionCatalog())
                                {
                                    if (Category != Info.Category || Group != Probe::MenuSubgroup(Category, Info.Name)) { continue; }
                                    GroupMenu.AddMenuEntry(FText::FromString(Info.Name), FText::FromString(Info.Description), FSlateIcon(),
                                        FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::SetStepType, TestRef, Index, Info.Type)));
                                }
                                for (const FName Id : Probe::GetExtensionAssertionIds())
                                {
                                    const Probe::FExtensionAssertion* Info = Probe::FindExtensionAssertion(Id);
                                    if (!Info || Category != Info->Category || Group != Probe::MenuSubgroup(Category, Info->Name)) { continue; }
                                    GroupMenu.AddMenuEntry(FText::FromString(Info->Name), FText::FromString(Info->Description), FSlateIcon(),
                                        FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::SetExtensionStepType, TestRef, Index, Id)));
                                }
                            }));
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
        const bool bFirstExpanded = ExpandedStepIndices.Contains(Index);
        const bool bSecondExpanded = ExpandedStepIndices.Contains(Index + Direction);
        ExpandedStepIndices.Remove(Index);
        ExpandedStepIndices.Remove(Index + Direction);
        if (bFirstExpanded) { ExpandedStepIndices.Add(Index + Direction); }
        if (bSecondExpanded) { ExpandedStepIndices.Add(Index); }
        RebuildTestEditor();
        return FReply::Handled();
    }

    FReply DuplicateStep(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        UProbeTestAsset* Asset = TestRef.Get();
        if (!Asset || !Asset->Steps.IsValidIndex(Index)) { return FReply::Handled(); }
        ChangeAsset(TestRef, LOCTEXT("DuplicateStepTransaction", "Duplicate Probe Test Step"),
            [Index](UProbeTestAsset& Test) { Probe::DuplicateStepSafely(Test, Index); });
        // Keep expanded/collapsed state associated with existing rows below the insertion.
        TSet<int32> Shifted;
        for (const int32 ExpandedIndex : ExpandedStepIndices)
        {
            Shifted.Add(ExpandedIndex > Index ? ExpandedIndex + 1 : ExpandedIndex);
        }
        ExpandedStepIndices = MoveTemp(Shifted);
        ExpandedStepIndices.Add(Index + 1);
        RebuildTestEditor();
        return FReply::Handled();
    }

    FReply DuplicateStepGroup(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        UProbeTestAsset* Test = TestRef.Get();
        if (!Test || !Test->Steps.IsValidIndex(Index)) { return FReply::Handled(); }
        const ProbeStepGroups::FRange Original = ProbeStepGroups::FindContiguous(Test->Steps, Index);
        if (!Original.IsValid() || Original.Num() > 256 || Test->Steps.Num() + Original.Num() > 4096)
        {
            FeedbackMessage = LOCTEXT("DuplicateGroupLimit", "Group cannot be duplicated (256 steps per group; 4096 total).");
            return FReply::Handled();
        }
        ProbeStepGroups::FRange Copied;
        ChangeAsset(TestRef, LOCTEXT("DuplicateGroupTransaction", "Duplicate Probe Step Group"),
            [Index, &Copied](UProbeTestAsset& Asset)
            { ProbeDev8Workflow::DuplicateGroup(Asset, Index, Copied); });
        if (!Copied.IsValid())
        {
            FeedbackMessage = LOCTEXT("DuplicateGroupFailed", "Could not duplicate the group.");
            return FReply::Handled();
        }
        TSet<int32> Shifted;
        for (const int32 Expanded : ExpandedStepIndices)
        { Shifted.Add(Expanded >= Copied.First ? Expanded + Copied.Num() : Expanded); }
        ExpandedStepIndices = MoveTemp(Shifted);
        for (int32 I = Copied.First; I <= Copied.Last; ++I) { ExpandedStepIndices.Add(I); }
        RebuildTestEditor();
        FeedbackMessage = FText::FromString(FString::Printf(TEXT("Duplicated %d step(s) into a separate group."), Copied.Num()));
        return FReply::Handled();
    }

    FReply DeleteStep(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        UProbeTestAsset* Asset = TestRef.Get();
        if (!Asset || !Asset->Steps.IsValidIndex(Index)) { return FReply::Handled(); }
        ChangeAsset(TestRef, LOCTEXT("DeleteStepTransaction", "Remove Probe Test Step"),
            [Index](UProbeTestAsset& Test) { Test.Steps.RemoveAt(Index); });
        TSet<int32> Shifted;
        for (const int32 ExpandedIndex : ExpandedStepIndices)
        {
            if (ExpandedIndex != Index)
            { Shifted.Add(ExpandedIndex > Index ? ExpandedIndex - 1 : ExpandedIndex); }
        }
        ExpandedStepIndices = MoveTemp(Shifted);
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
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
                [ SNew(SCheckBox)
                  .ToolTipText(LOCTEXT("Dev14BulkSelectStepTip", "Select for multi-step editing; independent from Enabled."))
                  .IsChecked_Lambda([this, Index]() { return SelectedBulkStepIndices.Contains(Index) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                  .OnCheckStateChanged_Lambda([this, Index](ECheckBoxState State)
                  { if (State == ECheckBoxState::Checked) { SelectedBulkStepIndices.Add(Index); }
                    else { SelectedBulkStepIndices.Remove(Index); } }) ]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 7, 0)
                [ SNew(SCheckBox)
                  .IsChecked(Step.bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                  .ToolTipText(LOCTEXT("StepEnabledTip", "Enable or disable this step without deleting it. Disabled steps report Skipped and never perform actions."))
                  .OnCheckStateChanged_Lambda([this, TestRef, Index](ECheckBoxState State)
                  {
                      ChangeStep(TestRef, Index, LOCTEXT("ToggleStepTransaction", "Toggle Probe Test Step"),
                          [bEnabled = State == ECheckBoxState::Checked](FProbeTestStep& Item) { Item.bEnabled = bEnabled; });
                      RebuildTestEditor();
                  }) ]
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
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0)
                [ SNew(SComboButton)
                  .ToolTipText(LOCTEXT("Dev16StepActionsTip", "Step actions: reorder, duplicate, copy, run or remove"))
                  .OnGetMenuContent_Lambda([this, TestRef, Index]() { return BuildDev16StepActionsMenu(TestRef, Index); })
                  .ButtonContent()[ SNew(STextBlock).Text(LOCTEXT("Dev16StepMenu", "...")) ] ]
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
            Fields->AddSlot().AutoHeight().Padding(4, 4)
            [ SNew(SHorizontalBox)
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 10, 0)
              [ SNew(STextBlock).Text(LOCTEXT("GroupNameLabel", "Group")) ]
              + SHorizontalBox::Slot().FillWidth(1.0f)
              [ SNew(SEditableTextBox)
                .HintText(LOCTEXT("GroupNameHint", "Optional name; adjacent matches form a group"))
                .Text(FText::FromString(Step.GroupName))
                .OnTextCommitted_Lambda([this, TestRef, Index](const FText& Value, ETextCommit::Type)
                {
                    ChangeStep(TestRef, Index, LOCTEXT("GroupNameTransaction", "Name Probe Step Group"),
                        [Name = Value.ToString().TrimStartAndEnd()](FProbeTestStep& Item) { Item.GroupName = Name.Left(96); });
                    RebuildTestEditor();
                }) ] ];
            const uint8 StepFieldMask = Probe::StepFields(Step);
            if (StepFieldMask & Probe::TargetField)
            {
                const FText Label = (Step.AssertionId.IsNone() && ProbeNativeUIPolicy::HasTarget(Type))
                    ? LOCTEXT("UIWidgetSelectorLabel", "Widget Selector")
                    : Type == EProbeStepType::FixtureCreateObject ? LOCTEXT("FixtureClassPath", "Object Class")
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
                    : Type == EProbeStepType::ActorFixtureSpawn ? LOCTEXT("FixtureActorClassPath", "Actor Class")
                    : (Step.AssertionId.IsNone() && ((Type >= EProbeStepType::ActorFixtureSetLabel
                        && Type <= EProbeStepType::ActorFixtureDestroy) ||
                        (Type >= EProbeStepType::ActorFixtureSetLocation && Type <= EProbeStepType::ActorFixtureScaleNotNearlyEquals)))
                      ? LOCTEXT("FixtureActorIdentifier", "Actor ID")
                    : Type == EProbeStepType::SelectionSelectActorByLabel ? LOCTEXT("ActionActorLabel", "Actor Label")
                    : Type == EProbeStepType::SelectionSelectActorsWithTag ? LOCTEXT("ActionActorTag", "Actor Tag")
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
                || Type == EProbeStepType::ActorLabelCountExactly
                || Type == EProbeStepType::SelectionSelectActorByLabel)
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
                    Type == EProbeStepType::ActorFixtureSpawn ? LOCTEXT("NewActorFixtureIdentifier", "New Actor ID")
                    : Type == EProbeStepType::ActorFixtureSetLabel ? LOCTEXT("NewActorFixtureLabel", "New Actor Label")
                    : Type == EProbeStepType::ActorFixtureSetFolder ? LOCTEXT("NewActorFixtureFolder", "New Outliner Folder")
                    : Type == EProbeStepType::ActorFixtureAddTag || Type == EProbeStepType::ActorFixtureRemoveTag
                        ? LOCTEXT("NewActorFixtureTag", "Actor Tag")
                    : Type == EProbeStepType::ActorFixtureLabelEquals ? LOCTEXT("ExpectedActorFixtureLabel", "Expected Actor Label")
                    : Type == EProbeStepType::ActorFixtureFolderEquals ? LOCTEXT("ExpectedActorFixtureFolder", "Expected Outliner Folder")
                    : Type == EProbeStepType::ActorFixtureHasTag || Type == EProbeStepType::ActorFixtureTagAbsent
                      ? LOCTEXT("ExpectedActorFixtureTag", "Actor Tag")
                    : Type == EProbeStepType::ActorFixtureSetLocation || Type == EProbeStepType::ActorFixtureLocationNearlyEquals
                      || Type == EProbeStepType::ActorFixtureLocationNotNearlyEquals
                      ? LOCTEXT("ActorFixtureLocation", "Location (X,Y,Z cm)")
                    : Type == EProbeStepType::ActorFixtureSetRotation || Type == EProbeStepType::ActorFixtureRotationNearlyEquals
                      || Type == EProbeStepType::ActorFixtureRotationNotNearlyEquals
                      ? LOCTEXT("ActorFixtureRotation", "Rotation (Pitch,Yaw,Roll)")
                    : Type == EProbeStepType::ActorFixtureSetScale || Type == EProbeStepType::ActorFixtureScaleNearlyEquals
                      || Type == EProbeStepType::ActorFixtureScaleNotNearlyEquals
                      ? LOCTEXT("ActorFixtureScale", "Scale (X,Y,Z)")
                    : Type == EProbeStepType::ActorFixtureAttachTo || Type == EProbeStepType::ActorFixtureHasParent
                      ? LOCTEXT("ActorFixtureParent", "Parent Actor ID")
                    : (Type == EProbeStepType::ActorFixtureDistanceAtMost || Type == EProbeStepType::ActorFixtureDistanceAtLeast)
                      ? LOCTEXT("ActorFixtureOther", "Other Actor ID")
                    : (Type == EProbeStepType::ActorFixtureLabelContains || Type == EProbeStepType::ActorFixtureLabelStartsWith || Type == EProbeStepType::ActorFixtureLabelEndsWith)
                      ? LOCTEXT("ActorFixtureLabelPattern", "Outliner Label Text")
                    : Type == EProbeStepType::ActorFixtureFolderNotEquals
                      ? LOCTEXT("ActorFixtureOtherFolder", "Forbidden Outliner Folder")
                    : (Type == EProbeStepType::UIWidgetTypeText || Type == EProbeStepType::UIWidgetAppendText) ? LOCTEXT("UIInputTextLabel", "Text To Enter")
                    : Type == EProbeStepType::UIWidgetTextContains || Type == EProbeStepType::UIWaitForWidgetTextContains
                      || Type == EProbeStepType::UIDetailsSearchTextContains
                      ? LOCTEXT("UIContainsExpectedLabel", "Contained Text")
                    : (Type == EProbeStepType::UIWaitForWidgetTextEquals || Type == EProbeStepType::UIWaitForWidgetTextNotEquals) ? LOCTEXT("UIWaitExpectedLabel", "Awaited Text")
                    : Type == EProbeStepType::FixtureCreateObject ? LOCTEXT("NewFixtureIdentifier", "New Fixture ID")
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
            if (Step.AssertionId.IsNone() && ProbeNativeUIPolicy::IsWait(Type))
            { AddUIWaitTimeoutField(Fields, TestRef, Index); }
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

    FReply ShowDev14TestPreflight()
    {
        if (const UProbeTestAsset* Test = SelectedAsset())
        {
            const ProbeDev14::FReport Report = ProbeDev14::PreflightTest(*Test);
            FMessageDialog::Open(EAppMsgType::Ok, EAppReturnType::Ok, FText::FromString(Report.Text()));
            FeedbackMessage = FText::FromString(FString::Printf(TEXT("Preflight: %d blocking, %d warning(s)."), Report.Blocking, Report.Warnings));
        }
        return FReply::Handled();
    }

    FReply AppendDev14Recipe(ProbeDev14::ERecipe Recipe)
    {
        UProbeTestAsset* Test = SelectedAsset();
        if (!Test || bRunInProgress) { return FReply::Handled(); }
        if (Test->Steps.Num() + ProbeDev14::BuildRecipe(Recipe).Num() > 4096
            || (!Test->Steps.IsEmpty() && !Test->CleanupGroupName.TrimStartAndEnd().IsEmpty()))
        { FeedbackMessage = LOCTEXT("Dev14RecipeFull", "Cannot append: recipe exceeds 4096 steps or would move an existing Cleanup group away from the end."); return FReply::Handled(); }
        ChangeAsset(TWeakObjectPtr<UProbeTestAsset>(Test), LOCTEXT("Dev14RecipeTransaction", "Append Probe Test Recipe"),
            [Recipe](UProbeTestAsset& Asset) { ProbeDev14::AppendRecipe(Asset, Recipe); });
        RebuildTestEditor();
        FeedbackMessage = FText::FromString(FString(TEXT("Added recipe: ")) + ProbeDev14::RecipeName(Recipe)
            + TEXT(". Review targets and mark the Test configured when ready."));
        return FReply::Handled();
    }

    // FUIAction's FExecuteAction is void-returning, unlike Slate button handlers.
    // Keep the FReply helper for consistency with the Test Steps command handlers,
    // and adapt it here for the menu delegate without changing recipe semantics.
    void AppendDev14RecipeMenuAction(ProbeDev14::ERecipe Recipe)
    {
        AppendDev14Recipe(Recipe);
    }

    TSharedRef<SWidget> BuildDev14RecipeMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        const ProbeDev14::ERecipe Recipes[] = {
            ProbeDev14::ERecipe::PluginHealth, ProbeDev14::ERecipe::EditorState,
            ProbeDev14::ERecipe::ReadOnlyUI, ProbeDev14::ERecipe::TransientFixture };
        for (const ProbeDev14::ERecipe Recipe : Recipes)
        {
            Menu.AddMenuEntry(FText::FromString(ProbeDev14::RecipeName(Recipe)),
                LOCTEXT("Dev14RecipeReview", "Append a multi-step pattern for author review. Existing permissions do not change."),
                FSlateIcon(), FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::AppendDev14RecipeMenuAction, Recipe)));
        }
        return Menu.MakeWidget();
    }

    FReply ApplyDev14Bulk(ProbeDev14::EBulkAction Action)
    {
        UProbeTestAsset* Test = SelectedAsset();
        if (!Test || bRunInProgress || SelectedBulkStepIndices.IsEmpty()) { return FReply::Handled(); }
        if (Action == ProbeDev14::EBulkAction::Delete && FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::No,
            LOCTEXT("Dev14BulkDeleteConfirm", "Remove selected Test steps? You can undo this operation.")) != EAppReturnType::Yes)
        { return FReply::Handled(); }
        TSet<int32> NewSelection = SelectedBulkStepIndices;
        for (int32 I : NewSelection)
        {
            if (!Test->Steps.IsValidIndex(I))
            { FeedbackMessage = LOCTEXT("Dev14BulkInvalid", "Selection changed. Reselect the steps before editing."); return FReply::Handled(); }
        }
        if ((Action == ProbeDev14::EBulkAction::Duplicate && Test->Steps.Num() + NewSelection.Num() > 4096)
            || (Action == ProbeDev14::EBulkAction::AssignGroup && BulkGroupName.TrimStartAndEnd().Len() > 96))
        { FeedbackMessage = LOCTEXT("Dev14BulkRejected", "Multi-step edit rejected. Check group name or step limit."); return FReply::Handled(); }
        ChangeAsset(TWeakObjectPtr<UProbeTestAsset>(Test), LOCTEXT("Dev14BulkTransaction", "Bulk Edit Probe Steps"),
            [this, Action, &NewSelection](UProbeTestAsset& Asset)
            { ProbeDev14::ApplyBulk(Asset, NewSelection, Action, BulkGroupName); });
        SelectedBulkStepIndices = MoveTemp(NewSelection);
        ExpandedStepIndices.Reset();
        RebuildTestEditor();
        FeedbackMessage = LOCTEXT("Dev14BulkDone", "Selected steps updated in one undoable transaction.");
        return FReply::Handled();
    }

    TSharedRef<SWidget> BuildDev14BulkActionsMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        struct FAction { ProbeDev14::EBulkAction Value; const TCHAR* Name; const TCHAR* Hint; };
        const FAction Actions[] = {
            {ProbeDev14::EBulkAction::Enable, TEXT("Enable Selected"), TEXT("Allow selected steps to run")},
            {ProbeDev14::EBulkAction::Disable, TEXT("Disable Selected"), TEXT("Skip selected steps without removing them")},
            {ProbeDev14::EBulkAction::MoveUp, TEXT("Move Selected Up"), TEXT("Move selected blocks up without changing their internal order")},
            {ProbeDev14::EBulkAction::MoveDown, TEXT("Move Selected Down"), TEXT("Move selected blocks down without changing their internal order")},
            {ProbeDev14::EBulkAction::Duplicate, TEXT("Duplicate Selected"), TEXT("Duplicate each selected step after its original")},
            {ProbeDev14::EBulkAction::Delete, TEXT("Remove Selected..."), TEXT("Ask for confirmation, then remove selected steps")}
        };
        for (const FAction& Action : Actions)
        {
            Menu.AddMenuEntry(FText::FromString(Action.Name), FText::FromString(Action.Hint), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([this, ActionValue = Action.Value]()
                { ApplyDev14Bulk(ActionValue); })));
        }
        return Menu.MakeWidget();
    }

    TSharedRef<SWidget> MakeDev14BulkToolbar()
    {
        TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox);
        Bar->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
        [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(FString::Printf(TEXT("Bulk: %d selected"), SelectedBulkStepIndices.Num())); }) ];
        Bar->AddSlot().AutoWidth().Padding(0, 0, 4, 0)
        [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
          .OnClicked_Lambda([this]() { if (const UProbeTestAsset* A = SelectedAsset())
          { for (int32 I = 0; I < A->Steps.Num(); ++I) { SelectedBulkStepIndices.Add(I); } RebuildTestEditor(); }
            return FReply::Handled(); })
          [ SNew(STextBlock).Text(LOCTEXT("Dev14SelectAll", "All")) ] ];
        Bar->AddSlot().AutoWidth().Padding(0, 0, 7, 0)
        [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
          .OnClicked_Lambda([this]() { SelectedBulkStepIndices.Reset(); RebuildTestEditor(); return FReply::Handled(); })
          [ SNew(STextBlock).Text(LOCTEXT("Dev14SelectNone", "None")) ] ];
        Bar->AddSlot().AutoWidth().Padding(0, 0, 5, 0)
        [ SNew(SComboButton)
          .ToolTipText(LOCTEXT("Dev14BulkActionsTip", "Apply a selected-step operation as one undoable transaction."))
          .IsEnabled_Lambda([this]() { return !SelectedBulkStepIndices.IsEmpty() && !bRunInProgress; })
          .OnGetMenuContent(this, &SProbeWorkspace::BuildDev14BulkActionsMenu)
          .ButtonContent()[ SNew(STextBlock).Text(LOCTEXT("Dev14BulkActions", "Actions")) ] ];
        Bar->AddSlot().FillWidth(1).Padding(6, 0, 4, 0)
        [ SNew(SEditableTextBox)
          .HintText(LOCTEXT("Dev14BulkGroupHint", "Group name"))
          .Text(FText::FromString(BulkGroupName))
          .OnTextCommitted_Lambda([this](const FText& Value, ETextCommit::Type)
          { BulkGroupName = Value.ToString().TrimStartAndEnd(); }) ];
        Bar->AddSlot().AutoWidth()
        [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
          .IsEnabled_Lambda([this]() { return !SelectedBulkStepIndices.IsEmpty() && !bRunInProgress; })
          .OnClicked_Lambda([this]() { return ApplyDev14Bulk(ProbeDev14::EBulkAction::AssignGroup); })
          [ SNew(STextBlock).Text(LOCTEXT("Dev14AssignGroup", "Assign Group")) ] ];
        return SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(5)[Bar];
    }

    FReply SaveDev15Recipe()
    {
        const UProbeTestAsset* Test = SelectedAsset();
        if (!Test || bRunInProgress || SelectedBulkStepIndices.IsEmpty())
        { FeedbackMessage = LOCTEXT("Dev15RecipeSelect", "Select a contiguous group of steps to save as a recipe."); return FReply::Handled(); }
        TArray<int32> Indices = SelectedBulkStepIndices.Array();
        Indices.Sort();
        for (int32 I = 1; I < Indices.Num(); ++I)
        { if (Indices[I] != Indices[I - 1] + 1)
          { FeedbackMessage = LOCTEXT("Dev15RecipeContiguous", "A reusable recipe must use a contiguous selection."); return FReply::Handled(); } }
        ProbeStepGroups::FRange Range;
        Range.First = Indices[0];
        Range.Last = Indices.Last();
        FString Error;
        if (!ProbeDev15Recipes::Save(Test->Steps, Range, Dev15RecipeName, false, Error))
        {
            if (Error.StartsWith(TEXT("Recipe exists:")) &&
                FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::No,
                    LOCTEXT("Dev15ReplaceRecipe", "A recipe with this name already exists. Replace it?")) == EAppReturnType::Yes)
            { ProbeDev15Recipes::Save(Test->Steps, Range, Dev15RecipeName, true, Error); }
        }
        FeedbackMessage = Error.IsEmpty()
            ? FText::FromString(TEXT("Saved reusable recipe. It is stored per project under Saved/Probe/Recipes."))
            : FText::FromString(Error);
        return FReply::Handled();
    }

    void InsertDev15SavedRecipe(const FString& Name)
    {
        UProbeTestAsset* Test = SelectedAsset();
        if (!Test || bRunInProgress) { return; }
        TArray<FProbeTestStep> Steps;
        FString Error;
        if (!ProbeDev15Recipes::Read(Name, Steps, Error))
        { FeedbackMessage = FText::FromString(Error); return; }
        // Preflight the append without touching the actual asset.
        if (Test->Steps.Num() + Steps.Num() > 4096 ||
            (!Test->Steps.IsEmpty() && !Test->CleanupGroupName.TrimStartAndEnd().IsEmpty()))
        { FeedbackMessage = LOCTEXT("Dev15RecipeCannotAppend", "Cannot append recipe after Cleanup or beyond 4096 steps."); return; }
        const int32 Before = Test->Steps.Num();
        ChangeAsset(TWeakObjectPtr<UProbeTestAsset>(Test),
            LOCTEXT("Dev15RecipeTransaction", "Insert Saved Probe Recipe"),
            [Steps](UProbeTestAsset& Asset)
            { FString Ignore; ProbeDev15Recipes::Append(Asset, Steps, Ignore); });
        for (int32 I = Before; I < Before + Steps.Num(); ++I) { ExpandedStepIndices.Add(I); }
        RebuildTestEditor();
        FeedbackMessage = FText::FromString(FString::Printf(
            TEXT("Inserted '%s' (%d steps). Review targets and permissions before executing."), *Name, Steps.Num()));
    }

    TSharedRef<SWidget> BuildDev15SavedRecipesMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        const TArray<FString> Names = ProbeDev15Recipes::List();
        if (Names.IsEmpty())
        { Menu.AddMenuEntry(LOCTEXT("Dev15NoSavedRecipes", "No saved recipes"),
            LOCTEXT("Dev15NoSavedRecipesHelp", "Select contiguous steps, enter a name, and use Save Group."),
            FSlateIcon(), FUIAction()); }
        for (const FString& Name : Names)
        { Menu.AddMenuEntry(FText::FromString(Name),
            LOCTEXT("Dev15InsertRecipeTip", "Append the saved steps and mark this Test as requiring author review. Permissions are not copied."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Name]() { InsertDev15SavedRecipe(Name); }))); }
        return Menu.MakeWidget();
    }

    void Dev15JumpToStep(int32 Index)
    {
        const UProbeTestAsset* Test = SelectedAsset();
        if (!Test || !Test->Steps.IsValidIndex(Index)) { return; }
        Dev15StepQuery.Reset();
        const ProbeDev16::FGroup Group = ProbeDev16::GroupAt(Test->Steps, Index);
        if (Group.Num()) { Dev16CollapsedGroups.Remove(ProbeDev16::GroupKey(Group)); }
        ExpandedStepIndices.Add(Index);
        RebuildTestEditor();
        if (DefinitionScroll.IsValid())
        {
            if (const TWeakPtr<SWidget>* Row = StepRowWidgets.Find(Index))
            { if (TSharedPtr<SWidget> Widget = Row->Pin())
              { DefinitionScroll->ScrollDescendantIntoView(Widget.ToSharedRef()); } }
        }
    }

    TSharedRef<SWidget> BuildDev15OutlineMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        const UProbeTestAsset* Test = SelectedAsset();
        if (!Test) { return Menu.MakeWidget(); }
        int32 GroupCount = 0;
        for (int32 Index = 0; Index < Test->Steps.Num(); ++Index)
        {
            if (!ProbeDev16::IsGroupStart(Test->Steps, Index)) { continue; }
            ++GroupCount;
            const ProbeDev16::FGroup Group = ProbeDev16::GroupAt(Test->Steps, Index);
            Menu.AddMenuEntry(FText::FromString(FString::Printf(TEXT("%d: %s (%d steps)"),
                    Index + 1, *Group.Name, Group.Num())),
                LOCTEXT("Dev16OutlineGroupTip", "Expand and jump to this distinct contiguous group."), FSlateIcon(),
                FUIAction(FExecuteAction::CreateLambda([this, Index]() { Dev15JumpToStep(Index); })));
        }
        if (GroupCount == 0)
        { Menu.AddMenuEntry(LOCTEXT("Dev15NoGroups", "No named groups"),
            LOCTEXT("Dev15NoGroupsTip", "Assign groups with the bulk step editor."), FSlateIcon(), FUIAction()); }
        return Menu.MakeWidget();
    }

    // Dev 16: Group is organization inside one Test; Recipe is a reusable saved pattern.
    // Both built-in and project-saved patterns appear in ONE Recipes surface.
    void PromptDev16Name(const FText& Title, const FString& Initial, int32 MaxLength,
        TFunction<void(const FString&)> OnAccepted)
    {
        TSharedRef<SWindow> Window = SNew(SWindow)
            .Title(Title).ClientSize(FVector2D(460.0f, 152.0f))
            .SupportsMinimize(false).SupportsMaximize(false);
        TSharedRef<FString> Value = MakeShared<FString>(Initial);
        TSharedRef<TFunction<void(const FString&)>> SavedCallback =
            MakeShared<TFunction<void(const FString&)>>(MoveTemp(OnAccepted));
        const TWeakPtr<SWindow> WeakWindow(Window);
        Window->SetContent(
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(12,10,12,6)
            [ SNew(STextBlock).Text(Title) ]
            + SVerticalBox::Slot().AutoHeight().Padding(12,0,12,6)
            [ SNew(SEditableTextBox).Text(FText::FromString(Initial))
              .HintText(LOCTEXT("Dev16NameHint", "Enter a descriptive name"))
              .OnTextChanged_Lambda([Value](const FText& Text) { *Value = Text.ToString(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(12,0,12,8)
            [ SNew(STextBlock)
              .Text(LOCTEXT("Dev16NameRules", "Recipe names: letters, numbers, spaces, underscores or hyphens. Existing recipes are never overwritten without confirmation."))
              .AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(10,0,10,10)
            [ SNew(SHorizontalBox)
              + SHorizontalBox::Slot().AutoWidth().Padding(4,0)
              [ SNew(SButton).Text(LOCTEXT("Dev16CancelName", "Cancel"))
                .OnClicked_Lambda([WeakWindow]()
                { if (TSharedPtr<SWindow> Dialog = WeakWindow.Pin()) { Dialog->RequestDestroyWindow(); }
                  return FReply::Handled(); }) ]
              + SHorizontalBox::Slot().AutoWidth().Padding(4,0)
              [ SNew(SButton).Text(LOCTEXT("Dev16AcceptName", "Save"))
                .IsEnabled_Lambda([Value, MaxLength]()
                { const FString Clean = Value->TrimStartAndEnd(); return !Clean.IsEmpty() && Clean.Len() <= MaxLength; })
                .OnClicked_Lambda([WeakWindow, Value, SavedCallback]()
                { const FString Clean = Value->TrimStartAndEnd();
                  if (TSharedPtr<SWindow> Dialog = WeakWindow.Pin()) { Dialog->RequestDestroyWindow(); }
                  (*SavedCallback)(Clean); return FReply::Handled(); }) ] ]);
        FSlateApplication::Get().AddModalWindow(Window,
            FSlateApplication::Get().GetActiveTopLevelWindow(), false);
    }

    void SaveDev16RecipeRange(TArray<FProbeTestStep> Snapshot, ProbeStepGroups::FRange Range,
        const FString& Name)
    {
        FString Error;
        if (!ProbeDev15Recipes::Save(Snapshot, Range, Name, false, Error))
        {
            if (Error.StartsWith(TEXT("Recipe exists:")) &&
                FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::No,
                    LOCTEXT("Dev16ConfirmReplaceRecipe", "Replace the existing saved recipe with this name?")) == EAppReturnType::Yes)
            { ProbeDev15Recipes::Save(Snapshot, Range, Name, true, Error); }
        }
        FeedbackMessage = Error.IsEmpty()
            ? FText::FromString(FString::Printf(TEXT("Saved recipe '%s'. Find it in Recipes > Saved Recipes."), *Name))
            : FText::FromString(Error);
        RefreshDev16RecipeBrowser();
    }

    FReply SaveDev16SelectedRecipe()
    {
        const UProbeTestAsset* Test = SelectedAsset();
        ProbeDev16::FGroup Span;
        if (!Test || bRunInProgress ||
            !ProbeDev16::IsContiguousSelection(SelectedBulkStepIndices, Test->Steps.Num(), Span))
        {
            FeedbackMessage = LOCTEXT("Dev16RecipeSelectionHint", "Select one or more adjacent Test steps before saving a recipe.");
            return FReply::Handled();
        }
        ProbeStepGroups::FRange Range;
        Range.First = Span.First; Range.Last = Span.Last;
        const TArray<FProbeTestStep> Snapshot = Test->Steps;
        PromptDev16Name(LOCTEXT("Dev16SaveRecipeTitle", "Save Selected Steps as Recipe"),
            Dev15RecipeName, 64,
            [this, Snapshot, Range](const FString& Name)
            { Dev15RecipeName = Name; SaveDev16RecipeRange(Snapshot, Range, Name); });
        return FReply::Handled();
    }

    void SaveDev16GroupRecipe(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        const UProbeTestAsset* Test = TestRef.Get();
        if (!Test || bRunInProgress) { return; }
        const ProbeDev16::FGroup Group = ProbeDev16::GroupAt(Test->Steps, Index);
        if (!Group.Num()) { return; }
        ProbeStepGroups::FRange Range;
        Range.First = Group.First; Range.Last = Group.Last;
        const TArray<FProbeTestStep> Snapshot = Test->Steps;
        PromptDev16Name(LOCTEXT("Dev16SaveGroupRecipe", "Save Group as Reusable Recipe"),
            Group.Name, 64,
            [this, Snapshot, Range](const FString& Name)
            { SaveDev16RecipeRange(Snapshot, Range, Name); });
    }

    void RenameDev16Group(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        const UProbeTestAsset* Test = TestRef.Get();
        if (!Test || bRunInProgress) { return; }
        const ProbeDev16::FGroup Group = ProbeDev16::GroupAt(Test->Steps, Index);
        if (!Group.Num()) { return; }
        PromptDev16Name(LOCTEXT("Dev16RenameGroup", "Rename Test Step Group"), Group.Name, 96,
            [this, TestRef, Index](const FString& Name)
            {
                ChangeAsset(TestRef, LOCTEXT("Dev16RenameGroupTransaction", "Rename Probe Step Group"),
                    [Index, Name](UProbeTestAsset& Asset) { ProbeDev16::RenameGroup(Asset, Index, Name); });
                Dev16CollapsedGroups.Reset();
                RebuildTestEditor();
            });
    }

    void RenameDev16Recipe(const FString& Name)
    {
        PromptDev16Name(LOCTEXT("Dev16RenameRecipe", "Rename Saved Recipe"), Name, 64,
            [this, Name](const FString& NewName)
            {
                FString Error;
                const bool bSuccess = ProbeDev16::RenameRecipe(Name, NewName, Error);
                FeedbackMessage = FText::FromString(bSuccess ? TEXT("Saved recipe renamed.") : Error);
                RefreshDev16RecipeBrowser();
            });
    }

    void DuplicateDev16Recipe(const FString& Name)
    {
        PromptDev16Name(LOCTEXT("Dev16DuplicateRecipe", "Duplicate Saved Recipe"), Name + TEXT("_Copy"), 64,
            [this, Name](const FString& NewName)
            {
                FString Error;
                const bool bSuccess = ProbeDev16::DuplicateRecipe(Name, NewName, Error);
                FeedbackMessage = FText::FromString(bSuccess ? TEXT("Saved recipe duplicated.") : Error);
                RefreshDev16RecipeBrowser();
            });
    }

    void DeleteDev16Recipe(const FString& Name)
    {
        if (FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::No,
            FText::FromString(FString::Printf(TEXT("Delete saved recipe '%s'? This cannot be undone."), *Name)))
            != EAppReturnType::Yes) { return; }
        FString Error;
        const bool bSuccess = ProbeDev16::DeleteRecipe(Name, Error);
        FeedbackMessage = FText::FromString(bSuccess ? TEXT("Saved recipe deleted.") : Error);
        RefreshDev16RecipeBrowser();
    }

    TSharedRef<SWidget> BuildDev16RecipesMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        Menu.BeginSection(TEXT("BuiltInRecipes"), LOCTEXT("Dev16BuiltInRecipes", "Built-in Recipes"));
        for (const ProbeDev14::ERecipe Recipe : {
            ProbeDev14::ERecipe::PluginHealth, ProbeDev14::ERecipe::EditorState,
            ProbeDev14::ERecipe::ReadOnlyUI, ProbeDev14::ERecipe::TransientFixture })
        {
            Menu.AddMenuEntry(FText::FromString(ProbeDev14::RecipeName(Recipe)),
                LOCTEXT("Dev16BuiltInRecipeTip", "Insert built-in Test steps. Review before running; no permissions are granted."),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Recipe]()
                { AppendDev14Recipe(Recipe); })));
        }
        Menu.EndSection();
        Menu.BeginSection(TEXT("SavedRecipes"), LOCTEXT("Dev16SavedRecipes", "Saved Recipes"));
        const TArray<FString> Names = ProbeDev15Recipes::List();
        if (Names.IsEmpty())
        { Menu.AddMenuEntry(LOCTEXT("Dev16NoRecipesYet", "No saved recipes yet"),
            LOCTEXT("Dev16NoRecipesHint", "Select adjacent steps and use Save Selection as Recipe."),
            FSlateIcon(), FUIAction()); }
        for (const FString& Name : Names)
        {
            Menu.AddMenuEntry(FText::FromString(Name),
                LOCTEXT("Dev16InsertSavedTip", "Insert project-local reusable recipe steps; receiving Test requires review."),
                FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Name]()
                { InsertDev15SavedRecipe(Name); })));
        }
        Menu.EndSection();
        Menu.BeginSection(TEXT("RecipeActions"), LOCTEXT("Dev16RecipeCommands", "Recipe Actions"));
        Menu.AddMenuEntry(LOCTEXT("Dev16SaveSelection", "Save Selection as Recipe..."),
            LOCTEXT("Dev16SaveSelectionTip", "Save contiguous selected Test steps to the project recipe library."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { SaveDev16SelectedRecipe(); }),
                FCanExecuteAction::CreateLambda([this]() { return !bRunInProgress && !SelectedBulkStepIndices.IsEmpty(); })));
        Menu.AddMenuEntry(LOCTEXT("Dev16BrowseRecipes", "Browse & Manage Recipes..."),
            LOCTEXT("Dev16BrowseRecipesTip", "Search, inspect, insert, rename, duplicate or delete project recipes."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { OpenDev16RecipeBrowser(); })));
        Menu.EndSection();
        return Menu.MakeWidget();
    }

    void PreviewDev16Recipe(const FString& Name, const TArray<FProbeTestStep>& Steps)
    {
        FString Preview = FString::Printf(TEXT("%s  |  %d step(s)\n\n"), *Name, Steps.Num());
        const int32 Shown = FMath::Min(Steps.Num(), 36);
        for (int32 I = 0; I < Shown; ++I)
        {
            const FProbeTestStep& Step = Steps[I];
            Preview += FString::Printf(TEXT("%d. %s%s\n"), I + 1,
                *Probe::StepDisplayName(Step),
                Step.GroupName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("  [%s]"), *Step.GroupName));
        }
        if (Steps.Num() > Shown) { Preview += TEXT("...remaining steps omitted from preview"); }
        FMessageDialog::Open(EAppMsgType::Ok, EAppReturnType::Ok, FText::FromString(Preview));
    }

    void RefreshDev16RecipeBrowser()
    {
        if (!Dev16RecipeBrowserRows.IsValid() || !Dev16RecipeBrowserWindow.IsValid()) { return; }
        Dev16RecipeBrowserRows->ClearChildren();
        auto Matches = [this](const FString& Name, const FString& Kind)
        { return Dev16RecipeSearch.IsEmpty() || Name.Contains(Dev16RecipeSearch, ESearchCase::IgnoreCase)
            || Kind.Contains(Dev16RecipeSearch, ESearchCase::IgnoreCase); };
        int32 Found = 0;
        for (const ProbeDev14::ERecipe Recipe : {
            ProbeDev14::ERecipe::PluginHealth, ProbeDev14::ERecipe::EditorState,
            ProbeDev14::ERecipe::ReadOnlyUI, ProbeDev14::ERecipe::TransientFixture })
        {
            const FString Name = ProbeDev14::RecipeName(Recipe);
            if (!Matches(Name, TEXT("Built-in"))) { continue; }
            ++Found;
            Dev16RecipeBrowserRows->AddSlot().AutoHeight().Padding(3,2)
            [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8)
              [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("%s  |  Built-in  |  %d steps"),
                    *Name, ProbeDev14::BuildRecipe(Recipe).Num()))) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(3,0)
                [ SNew(SButton).Text(LOCTEXT("Dev16PreviewBuiltIn", "Preview"))
                  .OnClicked_Lambda([this, Recipe, Name]()
                  { PreviewDev16Recipe(Name, ProbeDev14::BuildRecipe(Recipe)); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(5,0)
                [ SNew(SButton).Text(LOCTEXT("Dev16InsertBuiltIn", "Insert"))
                  .IsEnabled_Lambda([this]() { return HasSelectedAsset() && !bRunInProgress; })
                  .OnClicked_Lambda([this, Recipe]() { return AppendDev14Recipe(Recipe); }) ] ] ];
        }
        for (const FString& Name : ProbeDev15Recipes::List())
        {
            if (!Matches(Name, TEXT("Saved"))) { continue; }
            ++Found;
            TArray<FProbeTestStep> Steps;
            FString Error;
            const bool bRead = ProbeDev15Recipes::Read(Name, Steps, Error);
            const FString Detail = bRead
                ? FString::Printf(TEXT("%d steps | %s"), Steps.Num(), Steps.IsEmpty()
                    ? TEXT("Empty recipe") : *Probe::StepDisplayName(Steps[0]))
                : Error;
            Dev16RecipeBrowserRows->AddSlot().AutoHeight().Padding(3,2)
            [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8)
              [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                [ SNew(SVerticalBox)
                  + SVerticalBox::Slot().AutoHeight()
                  [ SNew(STextBlock).Text(FText::FromString(Name)).Font(FAppStyle::GetFontStyle("NormalFontBold")) ]
                  + SVerticalBox::Slot().AutoHeight()
                  [ SNew(STextBlock).Text(FText::FromString(Detail))
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
                + SHorizontalBox::Slot().AutoWidth().Padding(3,0)
                [ SNew(SButton).Text(LOCTEXT("Dev16PreviewSaved", "Preview"))
                  .IsEnabled(bRead)
                  .OnClicked_Lambda([this, Name, Steps]()
                  { PreviewDev16Recipe(Name, Steps); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(3,0)
                [ SNew(SButton).Text(LOCTEXT("Dev16InsertSaved", "Insert"))
                  .IsEnabled_Lambda([this, bRead]() { return bRead && HasSelectedAsset() && !bRunInProgress; })
                  .OnClicked_Lambda([this, Name]() { InsertDev15SavedRecipe(Name); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(3,0)
                [ SNew(SComboButton)
                  .OnGetMenuContent_Lambda([this, Name]() -> TSharedRef<SWidget>
                  {
                      FMenuBuilder Actions(true, nullptr);
                      Actions.AddMenuEntry(LOCTEXT("Dev16RenameSaved", "Rename..."), FText::GetEmpty(), FSlateIcon(),
                          FUIAction(FExecuteAction::CreateLambda([this, Name]() { RenameDev16Recipe(Name); })));
                      Actions.AddMenuEntry(LOCTEXT("Dev16DuplicateSaved", "Duplicate..."), FText::GetEmpty(), FSlateIcon(),
                          FUIAction(FExecuteAction::CreateLambda([this, Name]() { DuplicateDev16Recipe(Name); })));
                      Actions.AddMenuEntry(LOCTEXT("Dev16DeleteSaved", "Delete..."), FText::GetEmpty(), FSlateIcon(),
                          FUIAction(FExecuteAction::CreateLambda([this, Name]() { DeleteDev16Recipe(Name); })));
                      return Actions.MakeWidget();
                  })
                  .ButtonContent()[ SNew(STextBlock).Text(LOCTEXT("Dev16RecipeMore", "Actions")) ] ] ] ];
        }
        if (Found == 0)
        { Dev16RecipeBrowserRows->AddSlot().AutoHeight().Padding(12)
          [ SNew(STextBlock).Text(LOCTEXT("Dev16NoRecipeMatches", "No matching recipes. Clear the search or save a selection from a Test.")) ]; }
    }

    void OpenDev16RecipeBrowser()
    {
        if (TSharedPtr<SWindow> Existing = Dev16RecipeBrowserWindow.Pin())
        { Existing->BringToFront(true); return; }
        TSharedRef<SWindow> Window = SNew(SWindow)
            .Title(LOCTEXT("Dev16RecipeBrowserTitle", "Probe | Recipe Browser"))
            .ClientSize(FVector2D(800.0f, 540.0f));
        Window->SetContent(
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(10,9,10,6)
            [ SNew(STextBlock).Text(LOCTEXT("Dev16RecipeBrowserIntro",
                "Built-in patterns and saved project recipes. Inserting a recipe never grants permissions and requires Test review."))
                .AutoWrapText(true).ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
            + SVerticalBox::Slot().AutoHeight().Padding(10,0,10,8)
            [ SNew(SSearchBox)
              .HintText(LOCTEXT("Dev16RecipeFind", "Find recipes by name or kind..."))
              .OnTextChanged_Lambda([this](const FText& Query)
              { Dev16RecipeSearch = Query.ToString(); RefreshDev16RecipeBrowser(); }) ]
            + SVerticalBox::Slot().FillHeight(1.0f).Padding(8,0,8,8)
            [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
              [ SNew(SScrollBox)
                + SScrollBox::Slot()[ SAssignNew(Dev16RecipeBrowserRows, SVerticalBox) ] ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(10,0,10,10)
            [ SNew(SHorizontalBox)
              + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
              [ SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(STextBlock).Text(LOCTEXT("Dev16RecipeBrowserLocation", "Saved recipes: Project/Saved/Probe/Recipes/"))
                  .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ]
                + SVerticalBox::Slot().AutoHeight()
                [ SNew(STextBlock).Text(this, &SProbeWorkspace::FeedbackText)
                  .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
              + SHorizontalBox::Slot().AutoWidth()
              [ SNew(SButton).Text(LOCTEXT("Dev16BrowserSaveSelection", "Save Selected Steps as Recipe..."))
                .IsEnabled_Lambda([this]() { return !SelectedBulkStepIndices.IsEmpty() && !bRunInProgress; })
                .OnClicked_Lambda([this]() { return SaveDev16SelectedRecipe(); }) ] ]);
        Dev16RecipeBrowserWindow = Window;
        FSlateApplication::Get().AddWindow(Window);
        RefreshDev16RecipeBrowser();
    }

    TSharedRef<SWidget> BuildDev16GroupActionsMenu(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        FMenuBuilder Menu(true, nullptr);
        Menu.AddMenuEntry(LOCTEXT("Dev16GroupRenameAction", "Rename Group..."),
            LOCTEXT("Dev16GroupRenameTip", "Rename every step in this contiguous group. Undoable."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, TestRef, Index]()
            { RenameDev16Group(TestRef, Index); })));
        Menu.AddMenuEntry(LOCTEXT("Dev16GroupRecipe", "Save Group as Recipe..."),
            LOCTEXT("Dev16GroupRecipeTip", "Create a reusable recipe from these group steps. Does not change the Test."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, TestRef, Index]()
            { SaveDev16GroupRecipe(TestRef, Index); })));
        Menu.AddMenuSeparator();
        Menu.AddMenuEntry(LOCTEXT("Dev16GroupCopy", "Copy Group"), FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, TestRef, Index]() { CopyStepGroup(TestRef, Index); })));
        Menu.AddMenuEntry(LOCTEXT("Dev16GroupDuplicate", "Duplicate Group"), FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, TestRef, Index]() { DuplicateStepGroup(TestRef, Index); })));
        Menu.AddMenuEntry(LOCTEXT("Dev16GroupRun", "Run Group"),
            LOCTEXT("Dev16GroupRunTip", "Run this group alone. Previous setup steps are not executed."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, TestRef, Index]() { RunStepGroup(TestRef, Index); })));
        return Menu.MakeWidget();
    }

    TSharedRef<SWidget> BuildDev16StepActionsMenu(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        FMenuBuilder Menu(true, nullptr);
        Menu.AddMenuEntry(LOCTEXT("Dev16MoveUp", "Move Up"), FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, TestRef, Index]() { ReorderStep(TestRef, Index, -1); }),
                FCanExecuteAction::CreateLambda([Index]() { return Index > 0; })));
        Menu.AddMenuEntry(LOCTEXT("Dev16MoveDown", "Move Down"), FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, TestRef, Index]() { ReorderStep(TestRef, Index, +1); }),
                FCanExecuteAction::CreateLambda([this, Index]()
                { const UProbeTestAsset* Test = SelectedAsset(); return Test && Index + 1 < Test->Steps.Num(); })));
        Menu.AddMenuEntry(LOCTEXT("Dev16DuplicateStep", "Duplicate Step"), FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, TestRef, Index]() { DuplicateStep(TestRef, Index); })));
        Menu.AddMenuEntry(LOCTEXT("Dev16CopyStep", "Copy Step or Group"), FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, TestRef, Index]() { CopyStepGroup(TestRef, Index); })));
        Menu.AddMenuEntry(LOCTEXT("Dev16RunSingleStep", "Run Step or Group"),
            LOCTEXT("Dev16RunSingleTip", "Run only the step or its adjacent named group; setup steps are not executed."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, TestRef, Index]() { RunStepGroup(TestRef, Index); })));
        Menu.AddMenuSeparator();
        Menu.AddMenuEntry(LOCTEXT("Dev16DeleteStep", "Delete Step"), FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this, TestRef, Index]() { DeleteStep(TestRef, Index); })));
        return Menu.MakeWidget();
    }

    TSharedRef<SWidget> BuildDev16StepToolsMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        Menu.AddMenuEntry(LOCTEXT("Dev16ToolsPaste", "Paste Copied Steps"), FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this]() { PasteStepGroup(); })));
        Menu.AddMenuEntry(LOCTEXT("Dev16ToolsNotes", "Toggle Step Notes"), FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this]() { ToggleStepNotes(); })));
        Menu.AddMenuEntry(LOCTEXT("Dev16ToolsAll", "Select All Steps"), FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this]()
            { if (const UProbeTestAsset* Test = SelectedAsset())
              { SelectedBulkStepIndices.Reset(); for (int32 I = 0; I < Test->Steps.Num(); ++I) { SelectedBulkStepIndices.Add(I); }
                RebuildTestEditor(); } })));
        Menu.AddMenuEntry(LOCTEXT("Dev16ToolsClear", "Clear Step Selection"), FText::GetEmpty(), FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([this]()
            { SelectedBulkStepIndices.Reset(); RebuildTestEditor(); })));
        return Menu.MakeWidget();
    }

    void RebuildTestEditor()
    {
        if (!EditorRows.IsValid()) { return; }
        EditorRows->ClearChildren();
        StepRowWidgets.Reset();
        UProbeTestAsset* Asset = SelectedAsset();
        if (!Asset)
        {
            Dev16OverviewCache.Reset();
            ExpandedAssetPath.Reset();
            ExpandedStepIndices.Reset();
            EditorRows->AddSlot().AutoHeight().Padding(10)
            [ SNew(STextBlock).Text(LOCTEXT("NoTestSelected", "Create or select a Probe Test to edit its configuration.")) ];
            return;
        }
        Dev16OverviewCache = ProbeDev16::TestSummary(*Asset);
        const FString AssetPath = Asset->GetPathName();
        if (ExpandedAssetPath != AssetPath)
        {
            ExpandedAssetPath = AssetPath;
            ExpandedStepIndices.Reset();
            SelectedBulkStepIndices.Reset();
            Dev16CollapsedGroups.Reset();
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
        // Readiness is a test-lifecycle decision, not a miscellaneous checkbox.
        // New templates remain excluded from bulk runs until explicitly approved.
        Settings->AddSlot().AutoHeight().Padding(0, 7, 0, 5)
        [
            SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(FMargin(10.0f, 8.0f))
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                    [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Info")) ]
                    + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .AddMetaData(MakeShared<FDriverIdMetaData>(FName(TEXT("Probe.Workspace.TestReadiness"))))
                        .Font(FAppStyle::GetFontStyle("NormalFontBold"))
                        .Text_Lambda([TestRef]()
                        {
                            const UProbeTestAsset* Test = TestRef.Get();
                            if (!Test) { return FText::GetEmpty(); }
                            if (Test->bNeedsConfiguration) { return LOCTEXT("ReviewStatusTitle", "TEST READINESS  |  Needs Review"); }
                            if (!Probe::TestConfigurationIssue(*Test).IsEmpty()) { return LOCTEXT("InvalidStatusTitle", "TEST READINESS  |  Configuration Invalid"); }
                            return LOCTEXT("ReadyStatusTitle", "TEST READINESS  |  Ready");
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [
                        SNew(SButton)
                        .ToolTipText(LOCTEXT("ReadinessActionTip", "Ready tests participate in Run All. Mark Needs Review to hold a test out of automatic execution."))
                        .Text_Lambda([TestRef]()
                        {
                            const UProbeTestAsset* Test = TestRef.Get();
                            return Test && Test->bNeedsConfiguration
                                ? LOCTEXT("MarkReadyButton", "Mark Ready")
                                : LOCTEXT("MarkNeedsReviewButton", "Mark Needs Review");
                        })
                        .OnClicked_Lambda([this, TestRef]()
                        {
                            const UProbeTestAsset* Test = TestRef.Get();
                            if (!Test) { return FReply::Handled(); }
                            if (Test->bNeedsConfiguration)
                            {
                                const FString Issue = Probe::TestConfigurationIssue(*Test);
                                if (!Issue.IsEmpty())
                                {
                                    FeedbackMessage = FText::FromString(TEXT("Cannot mark Ready: ") + Issue);
                                    RebuildTestEditor();
                                    return FReply::Handled();
                                }
                            }
                            const bool bMarkNeedsReview = !Test->bNeedsConfiguration;
                            ChangeAsset(TestRef, LOCTEXT("ReviewTestTransaction", "Change Probe Test Readiness"),
                                [bMarkNeedsReview](UProbeTestAsset& Asset) { Asset.bNeedsConfiguration = bMarkNeedsReview; });
                            RebuildTestEditor();
                            return FReply::Handled();
                        })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(25, 5, 0, 0)
                [
                    SNew(STextBlock)
                    .AutoWrapText(true)
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                    .Text_Lambda([TestRef]()
                    {
                        const UProbeTestAsset* Test = TestRef.Get();
                        if (!Test) { return FText::GetEmpty(); }
                        if (Test->bNeedsConfiguration)
                        {
                            const FString Issue = Probe::TestConfigurationIssue(*Test);
                            return Issue.IsEmpty()
                                ? LOCTEXT("ReviewHint", "Review the step inputs and expected results, then mark this test Ready. It is excluded from Run All until then.")
                                : FText::FromString(TEXT("Complete before marking Ready: ") + Issue);
                        }
                        const FString Issue = Probe::TestConfigurationIssue(*Test);
                        if (!Issue.IsEmpty()) { return FText::FromString(TEXT("This test needs attention: ") + Issue); }
                        return LOCTEXT("ReadyHint", "This test can be included in automatic runs. Use Mark Needs Review to temporarily exclude it.");
                    })
                ]
            ]
        ];
        // Explicit authorization is separately identifiable from readiness.
        // Keep both settings serialized as before so existing assets do not migrate.
        Settings->AddSlot().AutoHeight().Padding(0, 2, 0, 5)
        [
            SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(FMargin(10.0f, 8.0f))
            .Visibility_Lambda([TestRef]()
            {
                const UProbeTestAsset* Test = TestRef.Get();
                if (!Test) { return EVisibility::Collapsed; }
                for (const FProbeTestStep& Step : Test->Steps)
                {
                    if (Step.AssertionId.IsNone() && Step.Type >= EProbeStepType::ActorFixtureSpawn
                        && Step.Type <= EProbeStepType::ActorFixtureScaleNotNearlyEquals)
                    { return EVisibility::Visible; }
                }
                return EVisibility::Collapsed;
            })
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                    [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Warning")) ]
                    + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock)
                        .Font(FAppStyle::GetFontStyle("NormalFontBold"))
                        .Text_Lambda([TestRef]()
                        {
                            const UProbeTestAsset* Test = TestRef.Get();
                            return Test && Test->bAllowTransientEditorActors
                                ? LOCTEXT("ActorsAuthorizedStatus", "WORLD ACTIONS  |  Authorized")
                                : LOCTEXT("ActorsBlockedStatus", "WORLD ACTIONS  |  Not Authorized");
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [
                        SNew(SButton)
                        .ToolTipText(LOCTEXT("ActorWorldPermissionTip", "Allows controlled test-owned actors to be created in the current Editor world. Probe attempts cleanup, but level dirtiness and empty folders can remain. Prefer a disposable test map."))
                        .Text_Lambda([TestRef]()
                        {
                            const UProbeTestAsset* Test = TestRef.Get();
                            return Test && Test->bAllowTransientEditorActors
                                ? LOCTEXT("RevokeActorPermission", "Revoke Authorization")
                                : LOCTEXT("GrantActorPermission", "Authorize Temporary Actors");
                        })
                        .OnClicked_Lambda([this, TestRef]()
                        {
                            const UProbeTestAsset* Test = TestRef.Get();
                            if (!Test) { return FReply::Handled(); }
                            const bool bGrant = !Test->bAllowTransientEditorActors;
                            ChangeAsset(TestRef, LOCTEXT("ActorPermissionTransaction", "Change Probe Editor World Permission"),
                                [bGrant](UProbeTestAsset& Asset) { Asset.bAllowTransientEditorActors = bGrant; });
                            RebuildTestEditor();
                            return FReply::Handled();
                        })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(25, 5, 0, 0)
                [
                    SNew(STextBlock)
                    .AutoWrapText(true)
                    .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                    .Text(LOCTEXT("WorldPermissionNotice", "These steps create or change test-owned actors in the active Editor level. Cleanup is attempted even on failure, but the level may become dirty. Use a disposable map. This authorization is independent of test readiness."))
                ]
            ]
        ];
        // Avoid suggesting that a read-only widget assertion needs simulated
        // input permission. This unobtrusive hint only appears when all native
        // UI steps in the asset are non-input observations or waits.
        Settings->AddSlot().AutoHeight().Padding(10, 3, 10, 5)
        [ SNew(STextBlock)
          .Visibility_Lambda([TestRef]()
          {
              const UProbeTestAsset* Test = TestRef.Get();
              if (!Test) { return EVisibility::Collapsed; }
              bool bReadOnlyUI = false;
              for (const FProbeTestStep& Step : Test->Steps)
              {
                  if (!Step.AssertionId.IsNone()) { continue; }
                  if (Step.bEnabled && ProbeNativeUIPolicy::IsInput(Step.Type))
                  { return EVisibility::Collapsed; }
                  bReadOnlyUI |= Step.bEnabled && ProbeNativeUIPolicy::IsObservation(Step.Type);
              }
              return bReadOnlyUI ? EVisibility::Visible : EVisibility::Collapsed;
          })
          .ColorAndOpacity(FSlateColor::UseSubduedForeground())
          .Text(LOCTEXT("UIReadOnlyPermissionHint", "Native widget observations and waits are read-only. Authorize UI Input is not required.")) ];
        // UI input permission is independent of world fixtures and is OFF on
        // legacy tests. Authors can opt in from the actual Probe test editor.
        Settings->AddSlot().AutoHeight().Padding(0, 2, 0, 5)
        [
            SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(FMargin(10.0f, 8.0f))
            .Visibility_Lambda([TestRef]()
            {
                const UProbeTestAsset* Test = TestRef.Get();
                if (!Test) { return EVisibility::Collapsed; }
                for (const FProbeTestStep& Step : Test->Steps)
                {
                    if (Step.AssertionId.IsNone() && Step.bEnabled &&
                        ProbeNativeUIPolicy::IsInput(Step.Type))
                    { return EVisibility::Visible; }
                }
                return EVisibility::Collapsed;
            })
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
                    [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Warning")) ]
                    + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                    [
                        SNew(STextBlock).Font(FAppStyle::GetFontStyle("NormalFontBold"))
                        .Text_Lambda([TestRef]()
                        {
                            const UProbeTestAsset* Test = TestRef.Get();
                            return Test && Test->bAllowEditorUIInput
                                ? LOCTEXT("NativeUIAuthorized", "LIVE EDITOR UI  |  Authorized")
                                : LOCTEXT("NativeUIBlocked", "LIVE EDITOR UI  |  Not Authorized");
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                    [
                        SNew(SButton)
                        .ToolTipText(LOCTEXT("NativeUIPermissionTip", "Allow real simulated mouse and keyboard input in Unreal Editor. Actions on arbitrary plugin controls can have persistent side effects. Use disposable content when testing destructive buttons."))
                        .Text_Lambda([TestRef]()
                        {
                            const UProbeTestAsset* Test = TestRef.Get();
                            return Test && Test->bAllowEditorUIInput
                                ? LOCTEXT("RevokeUIPermission", "Revoke Authorization")
                                : LOCTEXT("GrantUIPermission", "Authorize UI Input");
                        })
                        .OnClicked_Lambda([this, TestRef]()
                        {
                            const UProbeTestAsset* Test = TestRef.Get();
                            if (!Test) { return FReply::Handled(); }
                            const bool bGrant = !Test->bAllowEditorUIInput;
                            ChangeAsset(TestRef, LOCTEXT("UIPermissionTransaction", "Change Probe Live UI Permission"),
                                [bGrant](UProbeTestAsset& Asset) { Asset.bAllowEditorUIInput = bGrant; });
                            RebuildTestEditor();
                            return FReply::Handled();
                        })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight().Padding(25, 5, 0, 0)
                [ SNew(STextBlock).AutoWrapText(true)
                  .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                  .Text(LOCTEXT("NativeUIWarning", "Probe restores the native Details search text and keyboard focus, but cannot undo arbitrary button clicks or edits to other controls.")) ]
            ]
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
        // Dev 9 scenario authoring: settings are stored on the existing Test Asset,
        // with undo transactions. No external fixture or copied permission assets.
        TSharedRef<SVerticalBox> Scenario = SNew(SVerticalBox);
        Scenario->AddSlot().AutoHeight().Padding(0, 0, 0, 5)
        [ SNew(STextBlock).Text(LOCTEXT("ScenarioTitle", "WORKFLOW & VARIATIONS"))
          .Font(FAppStyle::GetFontStyle("NormalFontBold")) ];
        Scenario->AddSlot().AutoHeight().Padding(0, 0, 0, 5)
        [ SNew(STextBlock).AutoWrapText(true)
          .ColorAndOpacity(FSlateColor::UseSubduedForeground())
          .Text(LOCTEXT("ScenarioDescription", "Optional: name the first step group as Setup and the last as Cleanup. Cleanup runs after a failed full test. Add variations using ${Key} placeholders and one Key=Value binding per line.")) ];
        Scenario->AddSlot().AutoHeight().Padding(0, 2, 0, 5)
        [ SNew(SHorizontalBox)
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
          [ SNew(STextBlock).Text(LOCTEXT("SetupGroupName", "Setup Group")) ]
          + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 14, 0)
          [ SNew(SEditableTextBox)
            .HintText(LOCTEXT("SetupGroupHint", "First group name (optional)"))
            .Text_Lambda([TestRef]() { const UProbeTestAsset* A=TestRef.Get(); return A ? FText::FromString(A->SetupGroupName) : FText::GetEmpty(); })
            .OnTextCommitted_Lambda([this, TestRef](const FText& Value, ETextCommit::Type)
            { ChangeAsset(TestRef, LOCTEXT("SetSetupGroupTransaction", "Set Probe Setup Group"),
                [Name=Value.ToString().TrimStartAndEnd()](UProbeTestAsset& A) { A.SetupGroupName=Name.Left(96); }); }) ]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
          [ SNew(STextBlock).Text(LOCTEXT("CleanupGroupName", "Cleanup Group")) ]
          + SHorizontalBox::Slot().FillWidth(1)
          [ SNew(SEditableTextBox)
            .HintText(LOCTEXT("CleanupGroupHint", "Last group name (optional)"))
            .Text_Lambda([TestRef]() { const UProbeTestAsset* A=TestRef.Get(); return A ? FText::FromString(A->CleanupGroupName) : FText::GetEmpty(); })
            .OnTextCommitted_Lambda([this, TestRef](const FText& Value, ETextCommit::Type)
            { ChangeAsset(TestRef, LOCTEXT("SetCleanupGroupTransaction", "Set Probe Cleanup Group"),
                [Name=Value.ToString().TrimStartAndEnd()](UProbeTestAsset& A) { A.CleanupGroupName=Name.Left(96); }); }) ] ];
        Scenario->AddSlot().AutoHeight().Padding(0, 3, 0, 5)
        [ SNew(SHorizontalBox)
          + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
          [ SNew(STextBlock).Text(LOCTEXT("VariationsHeading", "Named parameter variations")) ]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
          [ SNew(SCheckBox)
            .IsChecked_Lambda([TestRef]() { const UProbeTestAsset* A=TestRef.Get(); return A && A->bRunVariationsInBulk ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
            .OnCheckStateChanged_Lambda([this, TestRef](ECheckBoxState State)
            { ChangeAsset(TestRef, LOCTEXT("BulkVariationTransaction", "Toggle Probe Bulk Variations"),
                [State](UProbeTestAsset& A) { A.bRunVariationsInBulk=State==ECheckBoxState::Checked; }); })
            [ SNew(STextBlock).Text(LOCTEXT("ExpandVariationsBulk", "Expand in Run All / Packs")) ] ]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
          [ SNew(SButton).IsEnabled_Lambda([this, TestRef]() { const UProbeTestAsset* A=TestRef.Get(); return !bRunInProgress && A && !A->Variations.IsEmpty(); })
            .OnClicked(this, &SProbeWorkspace::RunSelectedVariations)
            [ SNew(STextBlock).Text(LOCTEXT("RunSelectedVariations", "Run Variations")) ] ]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4, 0)
          [ SNew(SButton)
            .OnClicked_Lambda([this, TestRef]()
            {
                ChangeAsset(TestRef, LOCTEXT("AddVariationTransaction", "Add Probe Variation"),
                    [](UProbeTestAsset& A)
                    {
                        TSet<FString> Existing;
                        for (const FProbeTestVariation& V : A.Variations) { Existing.Add(V.Name); }
                        FString Name;
                        for (int32 N=1; N<1000; ++N)
                        { Name=FString::Printf(TEXT("Case %d"), N); if (!Existing.Contains(Name)) { break; } }
                        FProbeTestVariation V; V.Name=Name; A.Variations.Add(MoveTemp(V));
                    });
                RebuildTestEditor(); return FReply::Handled();
            })
            [ SNew(STextBlock).Text(LOCTEXT("AddVariation", "+ Variation")) ] ] ];
        for (int32 VariationIndex=0; VariationIndex<Asset->Variations.Num(); ++VariationIndex)
        {
            Scenario->AddSlot().AutoHeight().Padding(0, 2, 0, 2)
            [ SNew(SHorizontalBox)
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 5, 0)
              [ SNew(SCheckBox)
                .IsChecked_Lambda([TestRef, VariationIndex]()
                { const UProbeTestAsset* A=TestRef.Get(); return A && A->Variations.IsValidIndex(VariationIndex) && A->Variations[VariationIndex].bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this, TestRef, VariationIndex](ECheckBoxState State)
                { ChangeAsset(TestRef, LOCTEXT("EnableVariationTransaction", "Toggle Probe Variation"),
                    [VariationIndex, State](UProbeTestAsset& A) { if (A.Variations.IsValidIndex(VariationIndex)) { A.Variations[VariationIndex].bEnabled=State==ECheckBoxState::Checked; } }); }) ]
              + SHorizontalBox::Slot().FillWidth(0.26f).VAlign(VAlign_Center).Padding(0, 0, 5, 0)
              [ SNew(SEditableTextBox).HintText(LOCTEXT("VariationNameHint", "Variation name"))
                .Text_Lambda([TestRef, VariationIndex]()
                { const UProbeTestAsset* A=TestRef.Get(); return A && A->Variations.IsValidIndex(VariationIndex) ? FText::FromString(A->Variations[VariationIndex].Name) : FText::GetEmpty(); })
                .OnTextCommitted_Lambda([this, TestRef, VariationIndex](const FText& Value, ETextCommit::Type)
                { ChangeAsset(TestRef, LOCTEXT("RenameVariationTransaction", "Rename Probe Variation"),
                    [VariationIndex, Name=Value.ToString().TrimStartAndEnd()](UProbeTestAsset& A)
                    { if (A.Variations.IsValidIndex(VariationIndex)) { A.Variations[VariationIndex].Name=Name.Left(96); } }); }) ]
              + SHorizontalBox::Slot().FillWidth(0.74f).VAlign(VAlign_Center).Padding(0, 0, 5, 0)
              [ SNew(SEditableTextBox).HintText(LOCTEXT("VariationBindingsHint", "Key=Value (separate entries with semicolons)"))
                .ToolTipText(LOCTEXT("VariationBindingsTip", "Separate Key=Value bindings with semicolons; multiline is also accepted in asset data."))
                .Text_Lambda([TestRef, VariationIndex]()
                { const UProbeTestAsset* A=TestRef.Get(); return A && A->Variations.IsValidIndex(VariationIndex) ? FText::FromString(A->Variations[VariationIndex].Bindings) : FText::GetEmpty(); })
                .OnTextCommitted_Lambda([this, TestRef, VariationIndex](const FText& Value, ETextCommit::Type)
                { ChangeAsset(TestRef, LOCTEXT("SetBindingsTransaction", "Edit Probe Variation Bindings"),
                    [VariationIndex, Bindings=Value.ToString()](UProbeTestAsset& A)
                    { if (A.Variations.IsValidIndex(VariationIndex)) { A.Variations[VariationIndex].Bindings=Bindings; } }); }) ]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
              [ SNew(SButton)
                .OnClicked_Lambda([this, TestRef, VariationIndex]()
                {
                    ChangeAsset(TestRef, LOCTEXT("DeleteVariationTransaction", "Remove Probe Variation"),
                        [VariationIndex](UProbeTestAsset& A) { if (A.Variations.IsValidIndex(VariationIndex)) { A.Variations.RemoveAt(VariationIndex); } });
                    RebuildTestEditor(); return FReply::Handled();
                })
                [ SNew(STextBlock).Text(LOCTEXT("RemoveVariation", "Remove")) ] ] ];
        }
        Settings->AddSlot().AutoHeight().Padding(0, 8, 0, 3)
        [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
          .Padding(FMargin(10.0f, 8.0f)) [ Scenario ] ];
        EditorRows->AddSlot().AutoHeight().Padding(3, 3, 3, 7)
        [ SNew(SExpandableArea)
          .InitiallyCollapsed(!bDev16SettingsExpanded)
          .OnAreaExpansionChanged_Lambda([this](bool bExpanded)
          { bDev16SettingsExpanded = bExpanded;
            GConfig->SetBool(TEXT("ProbeWorkspace"), TEXT("Dev16SettingsExpanded"), bExpanded, GEditorPerProjectIni); })
          .HeaderContent()
          [ SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(2,2)
            [ SNew(STextBlock).Text(LOCTEXT("Dev16OverviewTitle", "Test Overview & Workflow Settings"))
              .Font(FAppStyle::GetFontStyle("NormalFontBold")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(2,0,2,4)
            [ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(Dev16OverviewCache); })
              .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ] ]
          .BodyContent()
          [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
            .Padding(FMargin(9,7))[ Settings ] ] ];
        EditorRows->AddSlot().AutoHeight().Padding(7, 4, 7, 4)
        [ SNew(SHorizontalBox)
          + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
          [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Test Steps (%d)"), Asset->Steps.Num())))
            .Font(FAppStyle::GetFontStyle("NormalFontBold")) ]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,7,0)
          [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
            .ToolTipText(LOCTEXT("Dev16PreflightTip", "Read-only readiness and permissions preflight; does not run this Test."))
            .OnClicked(this, &SProbeWorkspace::ShowDev14TestPreflight)
            [ SNew(STextBlock).Text(LOCTEXT("Dev16Preflight", "Preflight")) ] ]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,7,0)
          [ SNew(SComboButton)
            .ToolTipText(LOCTEXT("Dev16RecipesTip", "Built-in and project-saved reusable Test recipes, with creation and management in one menu."))
            .OnGetMenuContent(this, &SProbeWorkspace::BuildDev16RecipesMenu)
            .ButtonContent()[ SNew(STextBlock).Text(LOCTEXT("Dev16Recipes", "Recipes")) ] ]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
          [ SNew(SComboButton)
            .ToolTipText(LOCTEXT("Dev16StepToolsTip", "Paste, selection and step display actions"))
            .OnGetMenuContent(this, &SProbeWorkspace::BuildDev16StepToolsMenu)
            .ButtonContent()[ SNew(STextBlock).Text(LOCTEXT("Dev16StepTools", "Step Options")) ] ] ];
        EditorRows->AddSlot().AutoHeight().Padding(7, 0, 7, 5)
        [ SNew(SHorizontalBox)
          + SHorizontalBox::Slot().FillWidth(1.0f).Padding(0,0,6,0)
          [ SAssignNew(Dev15StepSearchBox, SSearchBox)
            .HintText(LOCTEXT("Dev15FindStepHint", "Find steps by assertion, target, group, description or expected value..."))
            .OnTextChanged_Lambda([this](const FText& Query) { Dev15StepQuery = Query.ToString(); }) ]
          + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
          [ SNew(SComboButton)
            .ToolTipText(LOCTEXT("Dev15OutlineTip", "Jump to a named group and expand its steps."))
            .OnGetMenuContent(this, &SProbeWorkspace::BuildDev15OutlineMenu)
            .ButtonContent()[ SNew(STextBlock).Text(LOCTEXT("Dev15Outline", "Groups")) ] ] ];
        EditorRows->AddSlot().AutoHeight().Padding(6, 0, 6, 6)
        [ SNew(SBox)
          .Visibility_Lambda([this]() { return SelectedBulkStepIndices.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
          [ MakeDev14BulkToolbar() ] ];
        // Restore search text when the editor rows are rebuilt (e.g. adding or moving steps).
        // The SSearchBox Slate argument list in UE 5.8 does not expose a .Text() setter.
        if (Dev15StepSearchBox.IsValid())
        {
            Dev15StepSearchBox->SetText(FText::FromString(Dev15StepQuery));
        }
        if (Asset->Steps.IsEmpty())
        {
            EditorRows->AddSlot().AutoHeight().Padding(10)
            [ SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground())
                .Text(LOCTEXT("NoSteps", "This test has no steps. Use Add Step above to create its first assertion.")) ];
        }
        for (int32 Index = 0; Index < Asset->Steps.Num(); ++Index)
        {
            if (ProbeDev16::IsGroupStart(Asset->Steps, Index))
            {
                const ProbeDev16::FGroup Group = ProbeDev16::GroupAt(Asset->Steps, Index);
                const FString Key = ProbeDev16::GroupKey(Group);
                EditorRows->AddSlot().AutoHeight().Padding(4,5,4,2)
                [ SNew(SBox)
                  .Visibility_Lambda([this, TestRef, Group]()
                  {
                      const UProbeTestAsset* Current = TestRef.Get();
                      if (!Current) { return EVisibility::Collapsed; }
                      if (Dev15StepQuery.IsEmpty()) { return EVisibility::Visible; }
                      for (int32 I = Group.First; I <= Group.Last; ++I)
                      { if (Current->Steps.IsValidIndex(I) && ProbeDev15::StepMatches(Current->Steps[I], Dev15StepQuery))
                        { return EVisibility::Visible; } }
                      return EVisibility::Collapsed;
                  })
                  [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder"))
                    .Padding(FMargin(7,3))
                    [ SNew(SHorizontalBox)
                      + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                      [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                        .ToolTipText(LOCTEXT("Dev16GroupCollapseTip", "Collapse or expand this contiguous group. Searching still shows matching steps."))
                        .OnClicked_Lambda([this, Key]()
                        { if (Dev16CollapsedGroups.Contains(Key)) { Dev16CollapsedGroups.Remove(Key); }
                          else { Dev16CollapsedGroups.Add(Key); }
                          RebuildTestEditor(); return FReply::Handled(); })
                        [ SNew(STextBlock).Text_Lambda([this, Group, Key]()
                          { return FText::FromString(FString::Printf(TEXT("%s  %s  (%d steps)"),
                            Dev16CollapsedGroups.Contains(Key) ? TEXT(">") : TEXT("v"), *Group.Name, Group.Num())); })
                          .Font(FAppStyle::GetFontStyle("NormalFontBold")) ] ]
                      + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                      [ SNew(SComboButton)
                        .ToolTipText(LOCTEXT("Dev16GroupActionsTip", "Group actions: rename, save as recipe, copy, duplicate or run"))
                        .OnGetMenuContent_Lambda([this, TestRef, Index]()
                        { return BuildDev16GroupActionsMenu(TestRef, Index); })
                        .ButtonContent()[ SNew(STextBlock).Text(LOCTEXT("Dev16GroupActions", "Group Actions")) ] ] ] ] ];
            }
            TSharedRef<SWidget> RowWidget = MakeStepCard(TestRef, Index);
            StepRowWidgets.Add(Index, RowWidget);
            // Compute the group identity once per editor rebuild, not every Slate visibility pass.
            const ProbeDev16::FGroup StepGroup = ProbeDev16::GroupAt(Asset->Steps, Index);
            const FString StepGroupKey = StepGroup.Num() ? ProbeDev16::GroupKey(StepGroup) : FString();
            EditorRows->AddSlot().AutoHeight().Padding(6, 0, 6, 2)
            [ SNew(SBox)
              .Visibility_Lambda([this, TestRef, Index, StepGroupKey]()
              {
                  const UProbeTestAsset* Current = TestRef.Get();
                  if (!Current || !Current->Steps.IsValidIndex(Index) ||
                      !ProbeDev15::StepMatches(Current->Steps[Index], Dev15StepQuery))
                  { return EVisibility::Collapsed; }
                  return (!StepGroupKey.IsEmpty() && Dev15StepQuery.IsEmpty() &&
                      Dev16CollapsedGroups.Contains(StepGroupKey))
                      ? EVisibility::Collapsed : EVisibility::Visible;
              })
              [ RowWidget ] ];
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
        if (!bSelectingFromCatalog && UnifiedCatalog.IsValid()) { UnifiedCatalog->ClearSelection(); }
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
        if (Asset)
        {
            Probe::PopulateBuiltinTemplate(Asset, Template);
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
        Probe::PopulateExtensionTemplate(Asset, *Template);
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
        const TArray<Probe::FTemplateInfo>& Templates = Probe::BuiltinTemplates();
        Menu.AddMenuSeparator();
        for (const FString& Category : Probe::AllCategories())
        {
            // Capture the descriptors by value; the submenu can be opened after this function returns.
            Menu.AddSubMenu(FText::FromString(Category), FText::FromString(Category + TEXT(" templates")),
                FNewMenuDelegate::CreateLambda([this, Category, Templates](FMenuBuilder& CategoryMenu)
                {
                    TArray<FString> Groups;
                    for (const Probe::FTemplateInfo& Info : Templates)
                    { if (Category == Info.Category) { Groups.AddUnique(Probe::MenuSubgroup(Category, Info.Name)); } }
                    for (const FName Id : Probe::GetExtensionTemplateIds())
                    {
                        if (const Probe::FExtensionTemplate* Info = Probe::FindExtensionTemplate(Id))
                        { if (Category == Info->Category) { Groups.AddUnique(Probe::MenuSubgroup(Category, Info->Name)); } }
                    }
                    Groups.Sort();
                    for (const FString& Group : Groups)
                    {
                        CategoryMenu.AddSubMenu(FText::FromString(Group), FText::FromString(Category + TEXT(" / ") + Group),
                            FNewMenuDelegate::CreateLambda([this, Category, Group, Templates](FMenuBuilder& GroupMenu)
                            {
                                for (const Probe::FTemplateInfo& Info : Templates)
                                {
                                    if (Category != Info.Category || Group != Probe::MenuSubgroup(Category, Info.Name)) { continue; }
                                    GroupMenu.AddMenuEntry(FText::FromString(Info.Name), FText::FromString(FString(Info.Description) + TEXT(". Review and mark Ready before Run All.")), FSlateIcon(),
                                        FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::CreateAsset, Info.Type)));
                                }
                                for (const FName Id : Probe::GetExtensionTemplateIds())
                                {
                                    const Probe::FExtensionTemplate* Info = Probe::FindExtensionTemplate(Id);
                                    if (!Info || Category != Info->Category || Group != Probe::MenuSubgroup(Category, Info->Name)) { continue; }
                                    GroupMenu.AddMenuEntry(FText::FromString(Info->Name), FText::FromString(Info->Description +
                                        (Info->bRequiresConfiguration ? TEXT(". Review inputs before automatic execution.") : TEXT(". Ready-to-run starter."))), FSlateIcon(),
                                        FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::CreateExtensionTemplate, Id)));
                                }
                            }));
                    }
                }));
        }
        return Menu.MakeWidget();
    }

    // Dev 5: one transaction creates a ready-to-run inspection step directly
    // from a discovered, unique target. Current text is a snapshot, not a bind.
    void AddWidgetTargetStep(EProbeStepType Type, const FString& Selector, const FString& /*PreviewText*/)
    {
        UProbeTestAsset* Asset = SelectedAsset();
        if (!Asset) { return; }
        // Never trust stale picker state: rescan just before applying.
        const ProbeWidgetDiscovery::FResolution Resolution =
            ProbeWidgetDiscovery::Resolve(ProbeWidgetDiscovery::Collect(), Selector);
        if (!Resolution.CanUse())
        {
            FeedbackMessage = FText::FromString(TEXT("Widget target changed: ") + Resolution.Description());
            return;
        }
        FProbeTestStep Step = Probe::MakeStep(Type, Asset->GetPathName());
        if (!ProbeWidgetTargetAuthoring::ConfigureQuickStep(Step, Type, Resolution.Candidate))
        {
            FeedbackMessage = LOCTEXT("QuickWidgetUnsupported", "That assertion cannot be created from this widget snapshot.");
            return;
        }
        const FScopedTransaction Transaction(LOCTEXT("AddWidgetTargetTransaction", "Add Probe Widget Assertion"));
        Asset->Modify();
        Asset->Steps.Add(MoveTemp(Step));
        Asset->MarkPackageDirty();
        ExpandedStepIndices.Add(Asset->Steps.Num() - 1);
        RebuildTestEditor();
        FeedbackMessage = FText::FromString(TEXT("Added widget assertion: ") + Selector);
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
            Menu.AddSubMenu(FText::FromString(Category), FText::FromString(Category + TEXT(" step types")),
                FNewMenuDelegate::CreateLambda([this, Category](FMenuBuilder& CategoryMenu)
                {
                    TArray<FString> Groups;
                    for (const Probe::FAssertionInfo& Info : Probe::AssertionCatalog())
                    { if (Category == Info.Category) { Groups.AddUnique(Probe::MenuSubgroup(Category, Info.Name)); } }
                    for (const FName Id : Probe::GetExtensionAssertionIds())
                    {
                        if (const Probe::FExtensionAssertion* Info = Probe::FindExtensionAssertion(Id))
                        { if (Category == Info->Category) { Groups.AddUnique(Probe::MenuSubgroup(Category, Info->Name)); } }
                    }
                    Groups.Sort();
                    for (const FString& Group : Groups)
                    {
                        CategoryMenu.AddSubMenu(FText::FromString(Group), FText::FromString(Category + TEXT(" / ") + Group),
                            FNewMenuDelegate::CreateLambda([this, Category, Group](FMenuBuilder& GroupMenu)
                            {
                                for (const Probe::FAssertionInfo& Info : Probe::AssertionCatalog())
                                {
                                    if (Category != Info.Category || Group != Probe::MenuSubgroup(Category, Info.Name)) { continue; }
                                    GroupMenu.AddMenuEntry(FText::FromString(Info.Name), FText::FromString(Info.Description), FSlateIcon(),
                                        FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::AddStep, Info.Type)));
                                }
                                for (const FName Id : Probe::GetExtensionAssertionIds())
                                {
                                    const Probe::FExtensionAssertion* Info = Probe::FindExtensionAssertion(Id);
                                    if (!Info || Category != Info->Category || Group != Probe::MenuSubgroup(Category, Info->Name)) { continue; }
                                    GroupMenu.AddMenuEntry(FText::FromString(Info->Name), FText::FromString(Info->Description), FSlateIcon(),
                                        FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::AddExtensionStep, Id)));
                                }
                            }));
                    }
                }));
        }
        return Menu.MakeWidget();
    }

    FReply CopyStepGroup(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        const UProbeTestAsset* Test = TestRef.Get();
        if (!Test) { return FReply::Handled(); }
        const ProbeStepGroups::FRange Range = ProbeStepGroups::FindContiguous(Test->Steps, Index);
        FString Json, Error;
        if (!ProbeStepGroups::Encode(Test->Steps, Range, Json, Error))
        { FeedbackMessage = FText::FromString(Error); return FReply::Handled(); }
        FPlatformApplicationMisc::ClipboardCopy(*Json);
        FeedbackMessage = FText::FromString(FString::Printf(TEXT("Copied %d Probe step(s)."), Range.Num()));
        return FReply::Handled();
    }

    FReply PasteStepGroup()
    {
        UProbeTestAsset* Test = SelectedAsset();
        if (!Test) { return FReply::Handled(); }
        FString Clipboard;
        FPlatformApplicationMisc::ClipboardPaste(Clipboard);
        TArray<FProbeTestStep> Pending;
        FString Error;
        if (!ProbeStepGroups::Decode(Clipboard, Pending, Error))
        { FeedbackMessage = FText::FromString(Error); return FReply::Handled(); }
        const int32 First = Test->Steps.Num();
        const int32 Count = Pending.Num();
        ChangeAsset(Test, LOCTEXT("PasteGroupTransaction", "Paste Probe Step Group"),
            [Steps = MoveTemp(Pending)](UProbeTestAsset& Asset)
            { Asset.Steps.Append(Steps); });
        for (int32 I = First; I < First + Count; ++I) { ExpandedStepIndices.Add(I); }
        RebuildTestEditor();
        FeedbackMessage = FText::FromString(FString::Printf(TEXT("Pasted %d steps. Review targets and permissions before running."), Count));
        return FReply::Handled();
    }

    FReply RunStepGroup(TWeakObjectPtr<UProbeTestAsset> TestRef, int32 Index)
    {
        UProbeTestAsset* Test = TestRef.Get();
        if (!Test || bRunInProgress) { return FReply::Handled(); }
        const ProbeStepGroups::FRange Range = ProbeStepGroups::FindContiguous(Test->Steps, Index);
        if (!Range.IsValid()) { return FReply::Handled(); }
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        Results.Reset();
        QueuedTestResultIndices.Reset();
        ScopedRunOptions.Reset();
        ExecuteAsset(Test);
        if (!QueuedTestResultIndices.IsEmpty())
        {
            Probe::FRunOptions Options;
            Options.FirstStepIndex = Range.First;
            Options.LastStepIndex = Range.Last;
            ScopedRunOptions.Add(QueuedTestResultIndices.Last(), Options);
        }
        RecordRunCompleted();
        ShowResults();
        return FReply::Handled();
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

    void ExecuteAsset(UProbeTestAsset* Asset, const Probe::FRunOptions& Options = Probe::FRunOptions())
    {
        bDev16ResultsCollapsed = false;
        if (!Asset) { return; }
        Probe::FTestResult Pending;
        Pending.Name = Asset->GetName();
        Pending.Id = Asset->TestId.ToString();
        Pending.AssetPath = Asset->GetPathName();
        Pending.Status = Probe::EResultStatus::Skipped;
        Pending.Detail = TEXT("Queued; not yet executed.");
        Pending.VariationIndex = Options.VariationIndex;
        if (Asset->Variations.IsValidIndex(Options.VariationIndex))
        {
            Pending.VariationName = Asset->Variations[Options.VariationIndex].Name;
            Pending.Name += TEXT(" [") + Pending.VariationName + TEXT("]");
        }
        const int32 ResultIndex = Results.Add(MoveTemp(Pending));
        QueuedTestResultIndices.Add(ResultIndex);
        if (Options.VariationIndex != INDEX_NONE || Options.FirstStepIndex != 0 || Options.LastStepIndex != MAX_int32)
        { ScopedRunOptions.Add(ResultIndex, Options); }
    }

    void QueueAssetForBulk(UProbeTestAsset* Asset)
    {
        if (!Asset) { return; }
        if (Asset->bRunVariationsInBulk && !Asset->Variations.IsEmpty())
        {
            const TArray<int32> Enabled = ProbeDev9Workflow::EnabledVariationIndices(*Asset);
            if (Enabled.IsEmpty())
            {
                Probe::FTestResult Skipped;
                Skipped.Name = Asset->GetName();
                Skipped.Id = Asset->TestId.ToString();
                Skipped.AssetPath = Asset->GetPathName();
                Skipped.Status = Probe::EResultStatus::Skipped;
                Skipped.Detail = TEXT("Bulk variation run enabled but all variations are disabled.");
                Results.Add(MoveTemp(Skipped));
            }
            for (int32 I : Enabled)
            { Probe::FRunOptions Options; Options.VariationIndex=I; ExecuteAsset(Asset, Options); }
        }
        else { ExecuteAsset(Asset); }
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
        case Probe::EResultStatus::Interrupted: return Style->GetBrush(TEXT("Probe.StatusSkipped"));
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
        case Probe::EResultStatus::Interrupted: return FLinearColor(0.88f, 0.70f, 0.30f);
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
        if (LastComparison.bHasPreviousRun)
        {
            Log += FString::Printf(TEXT("Compared with saved run baseline: %d regression(s), %d recovered, %d still failing, %d new identity(s).\n\n"),
                LastComparison.Regressions, LastComparison.Recovered, LastComparison.StillFailing, LastComparison.NewlyObserved);
        }
        for (const Probe::FTestResult& Result : Results)
        {
            if (!Log.IsEmpty()) { Log += TEXT("\n\n"); }
            const UProbeTestAsset* Source = FindObject<UProbeTestAsset>(nullptr, *Result.AssetPath);
            Log += FString::Printf(TEXT("[%s] %s  (%.2f ms, %d attempt(s), %s)\n%s"),
                Probe::StatusName(Result.Status), *Result.Name, Result.DurationMs, Result.Attempts,
                *ProbeDev10RunHistory::Category(Result, Source), *Result.Detail);
            for (const Probe::FStepResult& Step : Result.Steps)
            {
                const FString StepLabel = Step.Description.IsEmpty()
                    ? FString::Printf(TEXT("Step %d"), Step.Index)
                    : FString::Printf(TEXT("Step %d: %s"), Step.Index, *Step.Description);
                Log += FString::Printf(TEXT("\n  [%s] %s%s  (%.2f ms)\n    %s"),
                    Probe::StatusName(Step.Status), *StepLabel,
                    Step.GroupName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [Group: %s]"), *Step.GroupName),
                    Step.DurationMs, *Step.Detail);
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
        FString Summary = FString::Printf(TEXT("%d test(s) | %.2f ms | %s%s"),
            Results.Num(), LastRunDurationMs, *LastRunTime.ToString(TEXT("%H:%M:%S")),
            bViewingSavedHistory ? TEXT(" UTC (saved)") : TEXT(""));
        if (LastComparison.bHasPreviousRun)
        {
            Summary += FString::Printf(TEXT(" | Delta: %d regression(s), %d recovered"),
                LastComparison.Regressions, LastComparison.Recovered);
        }
        return FText::FromString(Summary);
    }

    TOptional<float> Dev14LiveProgressFraction() const
    {
        if (!bRunInProgress || Results.IsEmpty()) { return 0.0f; }
        const int32 Finished = Results.Num() - QueuedTestResultIndices.Num()
            - (ActiveExecutionResult != INDEX_NONE ? 1 : 0);
        const float StepFraction = ActiveExecutionStepCount > 0
            ? static_cast<float>(ActiveExecutionStep) / ActiveExecutionStepCount : 0.0f;
        return FMath::Clamp((Finished + StepFraction) / Results.Num(), 0.0f, 1.0f);
    }

    FText Dev14LiveProgressText() const
    {
        if (!bRunInProgress) { return LOCTEXT("Dev14MonitorIdle", "Execution monitor: idle"); }
        const int32 Finished = FMath::Max(0, Results.Num() - QueuedTestResultIndices.Num()
            - (ActiveExecutionResult != INDEX_NONE ? 1 : 0));
        int32 Problems = 0;
        for (int32 I = 0; I < Results.Num(); ++I)
        {
            if (I == ActiveExecutionResult || QueuedTestResultIndices.Contains(I)) { continue; }
            Problems += ProbeDev10PackPolicy::IsProblem(Results[I].Status);
        }
        const FString ActiveName = Results.IsValidIndex(ActiveExecutionResult)
            ? Results[ActiveExecutionResult].Name : TEXT("Queue");
        const double Elapsed = ActiveExecutionStartSeconds > 0.0
            ? FPlatformTime::Seconds() - ActiveExecutionStartSeconds : 0.0;
        return FText::FromString(FString::Printf(
            TEXT("Running %d/%d | %s | step %d/%d | %.1fs | %d problem(s)%s"),
            Finished, Results.Num(), *ActiveName, ActiveExecutionStep, ActiveExecutionStepCount,
            Elapsed, Problems, bStopAfterCurrentTest ? TEXT(" | stopping after test") : TEXT("")));
    }

    static int32 ResultSortPriority(Probe::EResultStatus Status)
    {
        switch (Status)
        {
        case Probe::EResultStatus::Error: return 0;
        case Probe::EResultStatus::Failed: return 1;
        case Probe::EResultStatus::Interrupted: return 2;
        case Probe::EResultStatus::Skipped: return 3;
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
        case EResultFilter::Interrupted: return Status == Probe::EResultStatus::Interrupted;
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
        int32 Passed = 0, Failed = 0, Skipped = 0, Errors = 0, Interrupted = 0;
        for (const Probe::FTestResult& Result : Results)
        {
            switch (Result.Status)
            {
            case Probe::EResultStatus::Passed: ++Passed; break;
            case Probe::EResultStatus::Failed: ++Failed; break;
            case Probe::EResultStatus::Skipped: ++Skipped; break;
            case Probe::EResultStatus::Interrupted: ++Interrupted; break;
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
        AddSummaryFilter(Interrupted, TEXT("Interrupted"), EResultFilter::Interrupted,
            ResultColor(Probe::EResultStatus::Interrupted));
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
        ResultTabs->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
        [
            SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
            .ToolTipText(LOCTEXT("CopyFailureSummaryTip", "Copy a concise list of failed/error tests and their failed steps, including asset paths and diagnostics."))
            .IsEnabled_Lambda([this]() { return HasProblemResults(); })
            .OnClicked(this, &SProbeWorkspace::CopyFailureSummary)
            [ SNew(STextBlock).Text(LOCTEXT("CopyFailureSummary", "Copy Failures")) ]
        ];
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
        if (Path.StartsWith(TEXT("probe-json://")))
        {
            FGlobalTabmanager::Get()->TryInvokeTab(Probe::PortableTabId);
            FeedbackMessage = LOCTEXT("Dev20PortableEdit", "Portable definitions are edited as Resources/Probe JSON. Use Probe Providers > Actions > Edit JSON; they are never modified through authored Test Assets.");
            return FReply::Handled();
        }
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

    FReply Dev14JumpToFirstFailure()
    {
        if (!Results.IsValidIndex(SelectedResultIndex)) { return FReply::Handled(); }
        for (const Probe::FStepResult& Step : Results[SelectedResultIndex].Steps)
        {
            if (ProbeDev10PackPolicy::IsProblem(Step.Status))
            { return EditSelectedResultStep(Step.Index); }
        }
        FeedbackMessage = LOCTEXT("Dev14NoFailedSteps", "The selected result contains no failed step to navigate to.");
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
            if (DefinitionScroll.IsValid())
            {
                if (const TWeakPtr<SWidget>* Row = StepRowWidgets.Find(StepNumber - 1))
                { if (TSharedPtr<SWidget> Found = Row->Pin())
                  { DefinitionScroll->ScrollDescendantIntoView(Found.ToSharedRef()); } }
            }
        }
        return Result;
    }

    FReply CopySelectedDetails()
    {
        FPlatformApplicationMisc::ClipboardCopy(*BuildSelectedResultString());
        FeedbackMessage = LOCTEXT("SelectedDetailsCopied", "Selected test diagnostics copied.");
        return FReply::Handled();
    }

    FReply CopyFailureSummary()
    {
        FPlatformApplicationMisc::ClipboardCopy(*ProbeDev8Workflow::BuildFailureSummary(Results));
        FeedbackMessage = LOCTEXT("CopiedFailures", "Failure summary copied. Use Export for complete JSON results.");
        return FReply::Handled();
    }

    bool CanRetrySelectedFailedGroup() const
    {
        if (bRunInProgress || !Results.IsValidIndex(SelectedResultIndex)) { return false; }
        const Probe::FTestResult& Result = Results[SelectedResultIndex];
        if (!ProbeDev8Workflow::IsProblem(Result.Status) || Result.AssetPath.IsEmpty()) { return false; }
        // Results come from an already-loaded asset. Do not synchronously load
        // packages during Slate's frequent IsEnabled attribute evaluations.
        const UProbeTestAsset* Asset = FindObject<UProbeTestAsset>(nullptr, *Result.AssetPath);
        return ProbeDev8Workflow::FirstFailedGroup(Result, Asset).bValid;
    }

    bool CanRerunSelectedResult() const
    {
        return !bRunInProgress && Results.IsValidIndex(SelectedResultIndex)
            && !Results[SelectedResultIndex].AssetPath.IsEmpty();
    }

    FReply RerunSelectedResult()
    {
        if (!CanRerunSelectedResult()) { return FReply::Handled(); }
        const FString AssetPath = Results[SelectedResultIndex].AssetPath;
        const int32 PreviousVariationIndex = Results[SelectedResultIndex].VariationIndex;
        UProbeTestAsset* Asset = FindObject<UProbeTestAsset>(nullptr, *AssetPath);
        if (!Asset) { Asset = LoadObject<UProbeTestAsset>(nullptr, *AssetPath); }
        if (!Asset)
        {
            FeedbackMessage = LOCTEXT("RerunResultMissing", "Cannot rerun: original Test Asset could not be loaded.");
            return FReply::Handled();
        }
        Results.Reset();
        QueuedTestResultIndices.Reset();
        ScopedRunOptions.Reset();
        SelectedResultIndex = INDEX_NONE;
        ActiveResultFilter = EResultFilter::All;
        Probe::FRunOptions Options;
        Options.VariationIndex = PreviousVariationIndex;
        ExecuteAsset(Asset, Options);
        RecordRunCompleted();
        ShowResults();
        return FReply::Handled();
    }

    FReply RetrySelectedFailedGroup()
    {
        if (bRunInProgress || !Results.IsValidIndex(SelectedResultIndex)) { return FReply::Handled(); }
        const Probe::FTestResult Previous = Results[SelectedResultIndex];
        UProbeTestAsset* Asset = FindObject<UProbeTestAsset>(nullptr, *Previous.AssetPath);
        if (!Asset) { Asset = LoadObject<UProbeTestAsset>(nullptr, *Previous.AssetPath); }
        const ProbeDev8Workflow::FFailureScope Retry = ProbeDev8Workflow::FirstFailedGroup(Previous, Asset);
        if (!Retry.bValid)
        {
            FeedbackMessage = FText::FromString(Retry.Explanation);
            return FReply::Handled();
        }
        Results.Reset();
        QueuedTestResultIndices.Reset();
        ScopedRunOptions.Reset();
        SelectedResultIndex = INDEX_NONE;
        ActiveResultFilter = EResultFilter::All;
        Probe::FRunOptions Options = Retry.Options;
        Options.VariationIndex = Previous.VariationIndex;
        ExecuteAsset(Asset, Options);
        RecordRunCompleted();
        ShowResults();
        FeedbackMessage = FText::FromString(Retry.Explanation);
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
            [ SNew(SComboButton)
              .ToolTipText(LOCTEXT("RetryResultTip", "Choose between rerunning the full Test Asset or only the first failed step's contiguous group."))
              .IsEnabled_Lambda([this]() { return CanRerunSelectedResult() || CanRetrySelectedFailedGroup(); })
              .OnGetMenuContent_Lambda([this]() -> TSharedRef<SWidget>
              {
                  FMenuBuilder Menu(true, nullptr);
                  Menu.AddMenuEntry(LOCTEXT("RerunResultFull", "Rerun Full Test"),
                      LOCTEXT("RerunResultFullTip", "Rerun this complete Test Asset including its setup steps; existing input authorization still applies."),
                      FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { RerunSelectedResult(); }),
                          FCanExecuteAction::CreateLambda([this]() { return CanRerunSelectedResult(); })));
                  Menu.AddMenuEntry(LOCTEXT("RetryFailedGroup", "Retry Failed Group"),
                      LOCTEXT("RetryFailedGroupTip", "Rerun ONLY the group containing the first failed step. Earlier setup steps do not run and current asset permissions still apply."),
                      FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this]() { RetrySelectedFailedGroup(); }),
                          FCanExecuteAction::CreateLambda([this]() { return CanRetrySelectedFailedGroup(); })));
                  return Menu.MakeWidget();
              })
              .ButtonContent()
              [ SNew(STextBlock).Text(LOCTEXT("RetryMenuLabel", "Retry")) ] ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
            [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
              .ToolTipText(LOCTEXT("ProblemStepsTip", "Toggle between all recorded steps and only Failed/Error steps."))
              .OnClicked_Lambda([this]()
              { bProblemStepsOnly = !bProblemStepsOnly; RebuildResultDetails(); return FReply::Handled(); })
              [ SNew(STextBlock).Text_Lambda([this]()
                { return bProblemStepsOnly ? LOCTEXT("AllStepsView", "All Steps") : LOCTEXT("ProblemsOnlyView", "Problems Only"); }) ] ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 4, 0)
            [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
              .ToolTipText(LOCTEXT("Dev14JumpFailureTip", "Open the first failed/errored step in Test Definition."))
              .OnClicked(this, &SProbeWorkspace::Dev14JumpToFirstFailure)
              [ SNew(STextBlock).Text(LOCTEXT("Dev14JumpFailure", "First Failure")) ] ]
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
        if (bProblemStepsOnly)
        {
            bool bAnyProblemStep = false;
            for (const Probe::FStepResult& Step : Result.Steps)
            { if (ProbeDev8Workflow::IsProblem(Step.Status)) { bAnyProblemStep = true; break; } }
            if (!bAnyProblemStep)
            {
                ResultDetails->AddSlot().AutoHeight().Padding(5)
                [ SNew(STextBlock).Text(LOCTEXT("NoProblemSteps", "No failed or errored steps recorded for this test."))
                  .ColorAndOpacity(FSlateColor::UseSubduedForeground()) ];
            }
        }
        for (const Probe::FStepResult& Step : Result.Steps)
        {
            if (bProblemStepsOnly && !ProbeDev8Workflow::IsProblem(Step.Status)) { continue; }
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
                            [ SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("Step %d: %s%s (%.1f ms)"), Step.Index,
                                *Step.Description, Step.GroupName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" [%s]"), *Step.GroupName), Step.DurationMs)))
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
        if (bRunInProgress)
        {
            FeedbackMessage = LOCTEXT("ProbeRunAlreadyActive", "A Probe run is already in progress. Wait for it to finish before starting another.");
            return;
        }
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
        if (bRunInProgress)
        {
            FeedbackMessage = LOCTEXT("ProbeRunAlreadyActive", "A Probe run is already in progress. Wait for it to finish before starting another.");
            return;
        }
        UProbeTestPack* Pack = LoadObject<UProbeTestPack>(nullptr, *PackPath);
        if (!Pack)
        {
            FeedbackMessage = LOCTEXT("PackMissing", "The selected Probe Pack could not be loaded.");
            return;
        }
        FString PolicyError;
        if (!ProbeDev10PackPolicy::Validate(*Pack, PolicyError))
        {
            FeedbackMessage = FText::FromString(TEXT("Pack cannot run: ") + PolicyError);
            return;
        }
        ActivePackForRun.Reset(Pack);
        PackCompletedAttempts.Reset();
        Results.Reset();
        QueuedTestResultIndices.Reset();
        ScopedRunOptions.Reset();
        for (const TSoftObjectPtr<UProbeTestAsset>& Reference : Pack->Tests)
        {
            const Probe::FReferenceHealth Health = Probe::InspectTestReference(Reference.ToSoftObjectPath());
            if (!Health.IsUsable())
            {
                Probe::FTestResult Missing;
                Missing.Name = Reference.ToString(); Missing.AssetPath = Reference.ToString();
                Missing.Status = Probe::EResultStatus::Error; Missing.Detail = Health.Explanation;
                Results.Add(MoveTemp(Missing));
                continue;
            }
            if (UProbeTestAsset* Test = Reference.LoadSynchronous()) { QueueAssetForBulk(Test); }
            else
            {
                Probe::FTestResult Missing;
                Missing.Name = Reference.ToString(); Missing.AssetPath = Reference.ToString();
                Missing.Status = Probe::EResultStatus::Error;
                Missing.Detail = TEXT("Probe Pack reference could not be loaded.");
                Results.Add(MoveTemp(Missing));
            }
        }
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        FeedbackMessage = FText::FromString(FString::Printf(TEXT("Ran pack '%s' (%d tests)"), *Pack->GetName(), Results.Num()));
        RecordRunCompleted();
        ShowResults();
    }

    void ViewSavedRun(FString Path)
    {
        if (bRunInProgress) { return; }
        ProbeDev11RunHistory::FSnapshot Saved;
        if (!ProbeDev11RunHistory::Read(Path, Saved))
        {
            FeedbackMessage = LOCTEXT("HistoryLoadFailed", "Saved Probe run is missing or invalid.");
            return;
        }
        Results = MoveTemp(Saved.Results);
        LastRunTime = Saved.TimestampUtc;
        LastRunDurationMs = 0.0;
        for (const Probe::FTestResult& Result : Results) { LastRunDurationMs += Result.DurationMs; }
        LastComparison = ProbeDev10RunHistory::FComparison();
        bViewingSavedHistory = true;
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        FeedbackMessage = LOCTEXT("HistoryViewed", "Viewing saved results. Running a test creates a new run; saved history is read-only.");
        ShowResults();
        return;
    }

    void CompareToSavedRun(FString Path)
    {
        if (bRunInProgress) { return; }
        ProbeDev11RunHistory::FSnapshot Saved;
        if (!ProbeDev11RunHistory::Read(Path, Saved))
        {
            FeedbackMessage = LOCTEXT("HistoryBaselineFailed", "Comparison baseline is missing or invalid.");
            return;
        }
        SelectedHistoryBaselinePath = MoveTemp(Path);
        LastComparison = ProbeDev10RunHistory::Compare(Saved.Results, Results);
        FeedbackMessage = LOCTEXT("HistoryBaselineSet", "Comparison baseline selected for this workspace and future runs.");
        ShowResults();
        return;
    }

    void UseLatestHistoryBaseline()
    {
        // Explicitly choosing the latest run also removes the persisted pin.
        ProbeDev12History::Unpin(ProbeDev11RunHistory::DefaultDirectory());
        SelectedHistoryBaselinePath.Reset();
        FeedbackMessage = LOCTEXT("HistoryBaselineAutomatic", "Future runs will compare against the latest completed run.");
    }

    void PinSavedRun(FString Path)
    {
        if (bRunInProgress) { return; }
        ProbeDev11RunHistory::FSnapshot Snapshot;
        if (!ProbeDev11RunHistory::Read(Path, Snapshot))
        { FeedbackMessage = LOCTEXT("PinHistoryInvalid", "Cannot pin an invalid or missing run."); return; }
        const FString Directory = ProbeDev11RunHistory::DefaultDirectory();
        if (!ProbeDev12History::Pin(Directory, Snapshot))
        { FeedbackMessage = LOCTEXT("PinHistoryWriteFailed", "Unable to save the pinned baseline."); return; }
        SelectedHistoryBaselinePath = ProbeDev12History::PinnedPath(Directory);
        LastComparison = ProbeDev10RunHistory::Compare(Snapshot.Results, Results);
        FeedbackMessage = LOCTEXT("PinHistorySuccess", "Baseline pinned across Editor restarts.");
        ShowResults();
    }

    void SetHistoryLimit(int32 NewLimit)
    {
        MaxSavedHistoryRuns = FMath::Clamp(NewLimit, 0, ProbeDev11RunHistory::MaximumSavedRuns);
        GConfig->SetInt(TEXT("ProbeWorkspace"), TEXT("MaxSavedHistoryRuns"), MaxSavedHistoryRuns, GEditorPerProjectIni);
        GConfig->Flush(false, GEditorPerProjectIni);
        // 0 disables recording and preserves old files until the user explicitly clears them.
        if (MaxSavedHistoryRuns > 0)
        { ProbeDev11RunHistory::Prune(ProbeDev11RunHistory::DefaultDirectory(), MaxSavedHistoryRuns); }
        FeedbackMessage = FText::FromString(MaxSavedHistoryRuns == 0
            ? TEXT("History recording disabled. Existing runs are preserved until cleared.")
            : FString::Printf(TEXT("History retention set to %d run(s), subject to a 64 MiB disk limit."), MaxSavedHistoryRuns));
    }

    void ClearSavedHistory()
    {
        if (bRunInProgress) { return; }
        const EAppReturnType::Type Decision = FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::No,
            LOCTEXT("ConfirmClearHistory", "Delete all Probe saved run history and the pinned baseline for this project? This cannot be undone. Test Assets, Packs, and exported reports will not be affected."));
        if (Decision != EAppReturnType::Yes) { return; }
        if (!ProbeDev12History::ClearHistory(ProbeDev11RunHistory::DefaultDirectory()))
        { FeedbackMessage = LOCTEXT("ClearHistoryFailed", "Some history files could not be deleted. Check file permissions."); return; }
        SelectedHistoryBaselinePath.Reset();
        LastCompletedResults.Reset();
        LastComparison = ProbeDev10RunHistory::FComparison();
        FeedbackMessage = LOCTEXT("ClearHistoryDone", "Saved Probe history and pinned baseline cleared. Current results remain visible.");
        ShowResults();
    }

    FString CurrentHistoryTrendReport() const
    {
        const TArray<ProbeDev12History::FSnapshot> Snapshots = ProbeDev12History::LoadChronological(
            ProbeDev11RunHistory::DefaultDirectory());
        TArray<ProbeDev12History::FSnapshot> Matching;
        bool bPackSearch = false;
        for (const ProbeDev12History::FSnapshot& Snapshot : Snapshots)
        {
            if (ProbeDev12History::Matches(Snapshot, HistorySearch, HistoryStatusFilter))
            {
                Matching.Add(Snapshot);
                bPackSearch |= !HistorySearch.IsEmpty()
                    && Snapshot.PackPath.Contains(HistorySearch, ESearchCase::IgnoreCase);
            }
        }
        return ProbeDev12History::TrendReport(ProbeDev12History::Analyze(
            Matching, bPackSearch ? FString() : HistorySearch), Matching.Num());
    }

    void ExportHistoryTrends()
    {
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Probe"), TEXT("Reports"));
        if (!IFileManager::Get().MakeDirectory(*Directory, true))
        { FeedbackMessage = LOCTEXT("TrendExportDirFailed", "Unable to create Probe report directory."); return; }
        const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("Probe-History-Trends-%s.txt"),
            *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));
        const FString Report = CurrentHistoryTrendReport();
        FeedbackMessage = FFileHelper::SaveStringToFile(Report, *Path,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            ? FText::FromString(FString::Printf(TEXT("History trend report exported: %s"), *Path))
            : LOCTEXT("HistoryTrendExportFailed", "Unable to write trend report.");
    }

    void CopyHistoryTrends()
    {
        FPlatformApplicationMisc::ClipboardCopy(*CurrentHistoryTrendReport());
        FeedbackMessage = LOCTEXT("HistoryTrendsCopied", "Historical trend report copied to clipboard.");
    }

    void ShowHistoryTrends()
    {
        const FString Text = CurrentHistoryTrendReport();
        FSlateApplication::Get().AddWindow(SNew(SWindow)
            .Title(LOCTEXT("HistoryTrendsTitle", "Probe History Trends"))
            .ClientSize(FVector2D(860, 520))
            [ SNew(SBorder)
                .Padding(10)
                [ SNew(SMultiLineEditableTextBox)
                    .Text(FText::FromString(Text))
                    .IsReadOnly(true) ] ]);
    }

    // Runs are browsed in a dedicated window. Keep the History dropdown short:
    // one entry point per concern, with no dynamically expanding raw run list.
    void OpenHistoryBrowser()
    {
        if (TSharedPtr<SWindow> Existing = HistoryBrowserWindow.Pin())
        {
            Existing->BringToFront(false);
            return;
        }
        TSharedRef<SWindow> Window = SNew(SWindow)
            .Title(LOCTEXT("ProbeHistoryBrowserWindow", "Probe - History Browser"))
            .ClientSize(FVector2D(1100, 700));
        Window->SetContent(
            SNew(ProbeDev13HistoryUI::SBrowser)
            .OnViewRun(ProbeDev13HistoryUI::FOnRunAction::CreateSP(this, &SProbeWorkspace::ViewSavedRun))
            .OnCompareRun(ProbeDev13HistoryUI::FOnRunAction::CreateSP(this, &SProbeWorkspace::CompareToSavedRun))
            .OnPinRun(ProbeDev13HistoryUI::FOnRunAction::CreateSP(this, &SProbeWorkspace::PinSavedRun))
            .OnDeleteRun(ProbeDev13HistoryUI::FOnRunAction::CreateSP(this, &SProbeWorkspace::DeleteSavedHistoryRun))
            .OnCopyReport(ProbeDev13HistoryUI::FOnHistoryTextAction::CreateSP(this, &SProbeWorkspace::CopyInvestigationReport))
            .OnExportCsv(ProbeDev13HistoryUI::FOnHistoryTextAction::CreateSP(this, &SProbeWorkspace::ExportHistoryCsv)));
        FSlateApplication::Get().AddWindow(Window);
        HistoryBrowserWindow = Window;
    }

    void DeleteSavedHistoryRun(FString Path)
    {
        if (bRunInProgress) { return; }
        ProbeDev11RunHistory::FSnapshot Snapshot;
        if (!ProbeDev11RunHistory::Read(Path, Snapshot))
        { FeedbackMessage = LOCTEXT("HistorySingleDeleteMissing", "That history run is missing or invalid."); return; }
        const FString Label = ProbeDev11RunHistory::Label(Snapshot);
        const EAppReturnType::Type Decision = FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::No,
            FText::FromString(FString::Printf(TEXT("Delete this saved Probe run?\n\n%s\n\nThis cannot be undone. The pinned baseline, Test Assets, Packs, and exported reports will be preserved."), *Label)));
        if (Decision != EAppReturnType::Yes) { return; }
        if (!ProbeDev13History::DeleteSavedRun(ProbeDev11RunHistory::DefaultDirectory(), Path))
        { FeedbackMessage = LOCTEXT("HistorySingleDeleteFailed", "Unable to delete the selected saved run."); return; }
        if (SelectedHistoryBaselinePath == Path) { SelectedHistoryBaselinePath.Reset(); }
        ProbeDev11RunHistory::FSnapshot Latest;
        LastCompletedResults.Reset();
        if (ProbeDev11RunHistory::ReadLatest(ProbeDev11RunHistory::DefaultDirectory(), Latest))
        { LastCompletedResults = MoveTemp(Latest.Results); }
        FeedbackMessage = LOCTEXT("HistorySingleDeleteSuccess", "Selected Probe run deleted. Other history and the pinned baseline were preserved.");
    }

    void CopyInvestigationReport(FString Content)
    {
        FPlatformApplicationMisc::ClipboardCopy(*Content);
        FeedbackMessage = LOCTEXT("InvestigationCopied", "Filtered history investigation report copied to clipboard.");
    }

    void ExportHistoryCsv(FString Content)
    {
        const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Probe"), TEXT("Reports"));
        if (!IFileManager::Get().MakeDirectory(*Directory, true))
        { FeedbackMessage = LOCTEXT("HistoryCsvFolderFailed", "Unable to create Probe reports directory."); return; }
        const FString Path = FPaths::Combine(Directory, FString::Printf(TEXT("Probe-History-Trends-%s.csv"),
            *FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));
        FeedbackMessage = FFileHelper::SaveStringToFile(Content, *Path,
            FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
            ? FText::FromString(FString::Printf(TEXT("Filtered trend CSV exported: %s"), *Path))
            : LOCTEXT("HistoryCsvWriteFailed", "Unable to export history trend CSV.");
    }

    TSharedRef<SWidget> BuildHistoryMenu()
    {
        FMenuBuilder Menu(true, nullptr);
        Menu.BeginSection(TEXT("ProbeHistoryBrowse"), LOCTEXT("HistoryBrowseHeading", "BROWSE"));
        Menu.AddMenuEntry(LOCTEXT("HistoryOpenBrowser", "Open History Browser..."),
            LOCTEXT("HistoryOpenBrowserTip", "Search saved runs, inspect test and step diagnostics, compare runs, pin a baseline, or delete one run."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::OpenHistoryBrowser)));
        Menu.EndSection();

        Menu.BeginSection(TEXT("ProbeHistoryAnalysis"), LOCTEXT("HistoryAnalyzeHeading", "ANALYSIS & REPORTS"));
        Menu.AddMenuEntry(LOCTEXT("HistoryViewTrends", "View Failure Trends"),
            LOCTEXT("HistoryViewTrendsTip", "View historical failure rates and pass/fail transitions."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::ShowHistoryTrends)));
        Menu.AddMenuEntry(LOCTEXT("HistoryCopyTrends", "Copy Trend Report"),
            LOCTEXT("HistoryCopyTrendsTip", "Copy the historical trend report."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::CopyHistoryTrends)));
        Menu.AddMenuEntry(LOCTEXT("HistoryExportTrends", "Export Trend Report"),
            LOCTEXT("HistoryExportTrendsTip", "Export text trends to Saved/Probe/Reports; the Browser also supports CSV."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::ExportHistoryTrends)));
        Menu.EndSection();

        Menu.BeginSection(TEXT("ProbeHistoryBaseline"), LOCTEXT("HistoryBaselineHeading", "COMPARISON BASELINE"));
        const FString Directory = ProbeDev11RunHistory::DefaultDirectory();
        ProbeDev12History::FSnapshot Pinned;
        const bool bPinned = ProbeDev12History::LoadPinned(Directory, Pinned);
        Menu.AddMenuEntry(FText::FromString(bPinned ? TEXT("Pinned baseline active") : TEXT("Using latest completed run")),
            bPinned ? FText::FromString(ProbeDev11RunHistory::Label(Pinned))
                    : LOCTEXT("HistoryBaselineAutoHint", "Future executions compare against the latest completed run."),
            FSlateIcon(), FUIAction());
        Menu.AddMenuEntry(LOCTEXT("HistoryAutoBaseline", "Use Latest Run as Baseline"),
            LOCTEXT("HistoryAutoBaselineTip", "Unpin the baseline and automatically compare against the previous run."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::UseLatestHistoryBaseline)));
        Menu.EndSection();

        Menu.BeginSection(TEXT("ProbeHistoryStorage"), LOCTEXT("HistoryStorageHeading", "STORAGE & MAINTENANCE"));
        Menu.AddSubMenu(FText::FromString(FString::Printf(TEXT("Maximum Saved Runs: %s"),
                MaxSavedHistoryRuns == 0 ? TEXT("Off") : *FString::FromInt(MaxSavedHistoryRuns))),
            LOCTEXT("HistoryLimitTip", "Project-local retention (default 10 runs, 64 MiB rolling storage limit)."),
            FNewMenuDelegate::CreateLambda([this](FMenuBuilder& SubMenu)
            {
                for (const int32 Limit : { 0, 5, 10, 20, 30, 50 })
                {
                    const FString Name = Limit == 0 ? TEXT("Off (keep existing)")
                        : FString::Printf(TEXT("%d runs"), Limit);
                    SubMenu.AddMenuEntry(FText::FromString(Name),
                        LOCTEXT("HistoryLimitChoiceTip", "Apply a per-project retention limit. Reducing the limit prunes old runs."),
                        FSlateIcon(), FUIAction(
                            FExecuteAction::CreateSP(this, &SProbeWorkspace::SetHistoryLimit, Limit),
                            FCanExecuteAction(), FIsActionChecked::CreateLambda([this, Limit]() { return MaxSavedHistoryRuns == Limit; })),
                        NAME_None, EUserInterfaceActionType::RadioButton);
                }
            }));
        Menu.AddMenuEntry(LOCTEXT("HistoryClear", "Clear All Saved History..."),
            LOCTEXT("HistoryClearTip", "Requires confirmation. Deletes saved snapshots and the pinned baseline, not Test Assets, Packs, or exports."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateSP(this, &SProbeWorkspace::ClearSavedHistory)));
        Menu.EndSection();
        return Menu.MakeWidget();
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

    FReply RunSelectedVariations()
    {
        if (bRunInProgress) { return FReply::Handled(); }
        UProbeTestAsset* Asset = SelectedAsset();
        if (!Asset) { FeedbackMessage = LOCTEXT("NoVariationTest", "Select a Test Asset first."); return FReply::Handled(); }
        const TArray<int32> Indices = ProbeDev9Workflow::EnabledVariationIndices(*Asset);
        if (Indices.IsEmpty()) { FeedbackMessage = LOCTEXT("NoEnabledVariations", "No enabled variations in this test."); return FReply::Handled(); }
        Results.Reset(); QueuedTestResultIndices.Reset(); ScopedRunOptions.Reset();
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        for (int32 VariationIndex : Indices)
        {
            Probe::FRunOptions Options;
            Options.VariationIndex = VariationIndex;
            ExecuteAsset(Asset, Options);
        }
        RecordRunCompleted(); ShowResults();
        return FReply::Handled();
    }

    void OpenCatalogEntry(const ProbeDev20::FEntry& Entry)
    {
        if (Entry.Source == ProbeDev20::ESource::AuthoredTest)
        {
            for (const TSharedPtr<FAssetData>& Item : Assets)
            {
                if (Item.IsValid() && Item->GetObjectPathString() == Entry.Locator)
                {
                    bSelectingFromCatalog = true;
                    if (TestSelector.IsValid()) { TestSelector->SetSelectedItem(Item); }
                    else { Selected = Item; RebuildTestEditor(); }
                    bSelectingFromCatalog = false;
                    break;
                }
            }
        }
        else if (Entry.Source == ProbeDev20::ESource::NativeAutomation)
        {
            FeedbackMessage = FText::FromString(TEXT("Native Automation selected: ") + Entry.Id
                + TEXT(". Run Selection tracks authoritative controller completion in Probe Results and History."));
        }
        else
        {
            FeedbackMessage = FText::FromString(Entry.Name + TEXT(" | ") + Entry.Id
                + (Entry.bRequiresAuthorization ? TEXT(" | Action authorization required on run") : TEXT(" | Read-only portable definition")));
        }
    }

    void CompletePortableResults(const FString& Id, bool bPack, bool bActions, TArray<Probe::FTestResult> Completed)
    {
        bPortableRun = false;
        bRunInProgress = false;
        Results = MoveTemp(Completed);
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        LastComparison = ProbeDev10RunHistory::Compare(LastCompletedResults, Results);
        LastCompletedResults = Results;
        LastRunTime = FDateTime::Now();
        LastRunDurationMs = 0.0;
        bViewingSavedHistory = false;
        for (const Probe::FTestResult& Item : Results) { LastRunDurationMs += Item.DurationMs; }
        // Portable and authored runs share the same Results Explorer and saved
        // history. Portable reports retain their existing richer JSON format.
        if (MaxSavedHistoryRuns > 0 && !Results.IsEmpty())
        {
            ProbeDev11RunHistory::FSnapshot Snapshot;
            Snapshot.TimestampUtc = FDateTime::UtcNow();
            Snapshot.EngineVersion = FEngineVersion::Current().ToString();
            Snapshot.PackPath = bPack ? TEXT("probe-json://") + Id : FString();
            Snapshot.Results = Results;
            if (!ProbeDev11RunHistory::Save(ProbeDev11RunHistory::DefaultDirectory(), Snapshot,
                nullptr, MaxSavedHistoryRuns))
            { UE_LOG(LogTemp, Warning, TEXT("Probe: failed to save portable run to unified history.")); }
        }
        FString ReportFile, Error;
        const bool bSaved = ProbeDev19::SaveRunReport(Id, bPack, bActions, Results, ReportFile, Error);
        FeedbackMessage = FText::FromString(TEXT("Portable ") + Id + TEXT(": ")
            + ProbeDev19::Summarize(Results).ToString()
            + (bSaved ? TEXT(" | Report: ") + ReportFile : TEXT(" | Report error: ") + Error));
        ShowResults();
    }

    void RunCatalogEntry(const ProbeDev20::FEntry& Entry)
    {
        if (bRunInProgress)
        {
            FeedbackMessage = LOCTEXT("Dev20Busy", "A Probe run is already active. Cancel or finish it before starting another.");
            return;
        }
        switch (Entry.Source)
        {
        case ProbeDev20::ESource::AuthoredTest:
            RunAssetPaths({Entry.Locator});
            return;
        case ProbeDev20::ESource::AuthoredPack:
            ExecutePackPath(Entry.Locator);
            return;
        case ProbeDev20::ESource::NativeAutomation:
        {
            FString DispatchError;
            if (!NativeExecution.Start(Entry.Locator, DispatchError))
            {
                FeedbackMessage = FText::FromString(TEXT("Native Automation dispatch unavailable: ") + DispatchError);
                return;
            }
            // Optional diagnostic listener; the controller's structured completion is authoritative.
            // If log observation is unavailable, API execution still proceeds.
            NativeBridge.Start(Entry.Locator);
            NativePath = Entry.Locator;
            bNativeRun = true;
            bRunInProgress = true;
            NativeStartSeconds = FPlatformTime::Seconds();
            NativeDeadline = NativeStartSeconds + 180.0;
            FeedbackMessage = FText::FromString(TEXT("Discovering and dispatching via AutomationController: ") + Entry.Locator);
            return;
        }
        case ProbeDev20::ESource::PortableTest:
        case ProbeDev20::ESource::PortablePack:
        {
            const bool bPack = Entry.Source == ProbeDev20::ESource::PortablePack;
            const bool bActions = ProbePortable::RequiresAuthorization(Entry.Id, bPack);
            if (bActions && FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::No,
                FText::FromString(FString::Printf(TEXT("Authorize portable %s '%s' to run provider actions?\n\nOnly approve trusted plugins. Cleanup is attempted, but arbitrary native code is not guaranteed reversible."),
                bPack ? TEXT("Pack") : TEXT("Test"), *Entry.Id))) != EAppReturnType::Yes)
            { return; }
            bPortableRun = true;
            bRunInProgress = true;
            FeedbackMessage = FText::FromString(TEXT("Running portable definition: ") + Entry.Id);
            const TWeakPtr<SProbeWorkspace> Weak = SharedThis(this);
            const FString Id = Entry.Id;
            const auto Done = [Weak, Id, bPack, bActions](TArray<Probe::FTestResult> Finished)
            {
                if (TSharedPtr<SProbeWorkspace> Self = Weak.Pin())
                { Self->CompletePortableResults(Id, bPack, bActions, MoveTemp(Finished)); }
            };
            FString Error;
            const bool bAccepted = bPack
                ? ProbePortable::RunPack(Id, Done, Error, bActions)
                : ProbePortable::RunTest(Id, Done, Error, bActions);
            if (!bAccepted)
            {
                bPortableRun = false;
                bRunInProgress = false;
                FeedbackMessage = FText::FromString(TEXT("Portable execution rejected: ") + Error);
            }
            return;
        }
        }
    }

    FReply StopOrCancel()
    {
        if (bNativeRun)
        {
            ProbeDev23::FObservedRun Observation;
            if (NativeExecution.Phase() == ProbeDev26::EPhase::Complete)
            {
                ProbeDev25::FStructuredResult Structured;
                if (ProbeDev25::TryReadCompletedReport(NativePath, Structured))
                {
                    Observation.bCompleted = true;
                    Observation.Outcome = Structured.State;
                    Observation.DurationMs = Structured.DurationMs;
                    FinishNativeRun(Observation, FString());
                    return FReply::Handled();
                }
            }
            // StopTests only targets a run that this Probe workspace dispatched.
            // It is a stop request, not proof that workers ceased immediately.
            const bool bOwnedRun = NativeExecution.OwnsRun();
            NativeExecution.Stop();
            bUserStoppedNativeObservation = true;
            FinishNativeRun(Observation, bOwnedRun
                ? TEXT("Probe requested AutomationController StopTests for this native run. Worker shutdown is not assumed; result recorded as Interrupted.")
                : TEXT("Probe cancelled native worker/test discovery before dispatch. No test success was inferred."));
            return FReply::Handled();
        }
        if (bPortableRun)
        {
            FeedbackMessage = ProbePortable::RequestCancel()
                ? LOCTEXT("Dev20CancelPending", "Cancellation requested. Provider cleanup remains active.")
                : LOCTEXT("Dev20CancelMissing", "No active portable run to cancel.");
            return FReply::Handled();
        }
        return StopAfterCurrentTest();
    }

    FReply RunSelected()
    {
        if (UnifiedCatalog.IsValid() && UnifiedCatalog->Selection().IsValid())
        {
            RunCatalogEntry(*UnifiedCatalog->Selection());
            return FReply::Handled();
        }
        if (bRunInProgress)
        {
            FeedbackMessage = LOCTEXT("ProbeRunAlreadyActive", "A Probe run is already in progress. Wait for it to finish before starting another.");
            return FReply::Handled();
        }
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
        if (bRunInProgress)
        {
            FeedbackMessage = LOCTEXT("ProbeRunAlreadyActive", "A Probe run is already in progress. Wait for it to finish before starting another.");
            return FReply::Handled();
        }
        RefreshAssets();
        ActiveResultFilter = EResultFilter::All;
        SelectedResultIndex = INDEX_NONE;
        Results.Reset();
        QueuedTestResultIndices.Reset();
        ScopedRunOptions.Reset();
        for (const TSharedPtr<FAssetData>& Entry : Assets)
        {
            if (Entry.IsValid()) { QueueAssetForBulk(Cast<UProbeTestAsset>(Entry->GetAsset())); }
        }
        FeedbackMessage = LOCTEXT("AllExecuted", "Test run complete. See Results below.");
        RecordRunCompleted();
        ShowResults();
        return FReply::Handled();
    }

    FReply RerunProblems()
    {
        for (const Probe::FTestResult& Result : Results)
        {
            if (Result.AssetPath.StartsWith(TEXT("probe-json://"))
                && (Result.Status == Probe::EResultStatus::Failed || Result.Status == Probe::EResultStatus::Error))
            {
                FeedbackMessage = LOCTEXT("Dev20PortableRerun", "Portable Tests require separate per-run authorization. Select the failed portable Test in Test Library and run it again; Probe will not silently convert or elevate it.");
                return FReply::Handled();
            }
        }
        if (bRunInProgress)
        {
            FeedbackMessage = LOCTEXT("ProbeRunAlreadyActive", "A Probe run is already in progress. Wait for it to finish before starting another.");
            return FReply::Handled();
        }
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

    FReply StopAfterCurrentTest()
    {
        if (bRunInProgress)
        {
            bStopAfterCurrentTest = true;
            bStopDueToPackFailure = false;
            FeedbackMessage = LOCTEXT("StopRequested", "Stop requested. Current test will finish, and remaining tests will be skipped.");
        }
        return FReply::Handled();
    }

    void ExecuteQueuedResult(int32 ResultIndex, UProbeTestAsset* Asset, const Probe::FRunOptions Options)
    {
        ActiveExecutionResult = ResultIndex;
        ActiveExecutionStep = 0;
        ActiveExecutionStepCount = Asset ? Asset->Steps.Num() : 0;
        ActiveExecutionStartSeconds = FPlatformTime::Seconds();
        TWeakPtr<SProbeWorkspace> WeakWorkspace = SharedThis(this);
        const TWeakObjectPtr<UProbeTestAsset> WeakAsset(Asset);
        Probe::RunTestAsync(Asset, [WeakWorkspace, WeakAsset, ResultIndex, Options](Probe::FTestResult Finished)
        {
            TSharedPtr<SProbeWorkspace> Workspace = WeakWorkspace.Pin();
            if (!Workspace.IsValid() || !Workspace->bRunInProgress || !Workspace->Results.IsValidIndex(ResultIndex)) { return; }
            const int32 CompletedAttempts = Workspace->PackCompletedAttempts.FindRef(ResultIndex) + 1;
            const UProbeTestPack* Pack = Workspace->ActivePackForRun.Get();
            const UProbeTestAsset* Test = WeakAsset.Get();
            if (Pack && Test && !Workspace->bStopAfterCurrentTest
                && ProbeDev10PackPolicy::ShouldRetry(*Pack, *Test, Finished.Status, CompletedAttempts))
            {
                Workspace->PackCompletedAttempts.Add(ResultIndex, CompletedAttempts);
                // Retry on a later tick; never recursively reenter the Automation Driver.
                FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
                    [WeakWorkspace, WeakAsset, ResultIndex, Options](float)
                    {
                        if (TSharedPtr<SProbeWorkspace> Owner = WeakWorkspace.Pin())
                        {
                            if (Owner->bRunInProgress && Owner->Results.IsValidIndex(ResultIndex))
                            {
                                if (UProbeTestAsset* RetryAsset = WeakAsset.Get())
                                { Owner->ExecuteQueuedResult(ResultIndex, RetryAsset, Options); }
                                else
                                {
                                    Owner->Results[ResultIndex].Status = Probe::EResultStatus::Error;
                                    Owner->Results[ResultIndex].Detail = TEXT("Read-only retry unavailable: Test Asset was unloaded.");
                                    Owner->StartNextQueuedTest();
                                }
                            }
                        }
                        return false;
                    }));
                return;
            }
            Finished.Attempts = CompletedAttempts;
            if (CompletedAttempts > 1) { Finished.Detail += TEXT(" | Retried once (read-only Pack policy)."); }
            const Probe::EResultStatus CompletedStatus = Finished.Status;
            Workspace->Results[ResultIndex] = MoveTemp(Finished);
            if (Pack && ProbeDev10PackPolicy::ShouldStop(*Pack, CompletedStatus))
            { Workspace->bStopAfterCurrentTest = true; Workspace->bStopDueToPackFailure = true; }
            Workspace->ShowResults();
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
                [WeakWorkspace](float)
                {
                    if (TSharedPtr<SProbeWorkspace> Owner = WeakWorkspace.Pin())
                    { Owner->StartNextQueuedTest(); }
                    return false;
                }));
        }, Options, [WeakWorkspace, ResultIndex](int32 StepNumber, int32 TotalSteps)
        {
            if (TSharedPtr<SProbeWorkspace> Owner = WeakWorkspace.Pin())
            {
                if (Owner->bRunInProgress && Owner->ActiveExecutionResult == ResultIndex)
                { Owner->ActiveExecutionStep = StepNumber; Owner->ActiveExecutionStepCount = TotalSteps; }
            }
        });
    }

    void StartNextQueuedTest()
    {
        ActiveExecutionResult = INDEX_NONE;
        ActiveExecutionStep = 0;
        ActiveExecutionStepCount = 0;
        ActiveExecutionStartSeconds = 0.0;
        if (bStopAfterCurrentTest)
        {
            ProbeQueueControl::SkipPending(QueuedTestResultIndices, Results,
                bStopDueToPackFailure ? TEXT("Skipped by Pack Stop on Failure policy after a failed/errored test.")
                    : TEXT("Stopped before execution at user request."));
            ScopedRunOptions.Reset();
        }
        if (QueuedTestResultIndices.IsEmpty())
        {
            bRunInProgress = false;
            bStopAfterCurrentTest = false;
            bStopDueToPackFailure = false;
            const FString FinishedPackPath = ActivePackForRun.Get()
                ? ActivePackForRun.Get()->GetPathName() : FString();
            ActivePackForRun.Reset();
            PackCompletedAttempts.Reset();
            ScopedRunOptions.Reset();
            // Read explicitly chosen history baseline; otherwise use the previous
            // completed run, including one recovered from a prior Editor session.
            const TArray<Probe::FTestResult>* Baseline = &LastCompletedResults;
            ProbeDev11RunHistory::FSnapshot ChosenBaseline;
            if (!SelectedHistoryBaselinePath.IsEmpty())
            {
                if (ProbeDev11RunHistory::Read(SelectedHistoryBaselinePath, ChosenBaseline))
                { Baseline = &ChosenBaseline.Results; }
                else
                { SelectedHistoryBaselinePath.Reset(); }
            }
            LastComparison = ProbeDev10RunHistory::Compare(*Baseline, Results);
            LastCompletedResults = Results;
            LastRunTime = FDateTime::Now();
            bViewingSavedHistory = false;
            // Persistence errors never turn a valid test run into a failed test.
            if (!Results.IsEmpty())
            {
                ProbeDev11RunHistory::FSnapshot Snapshot;
                Snapshot.TimestampUtc = FDateTime::UtcNow();
                Snapshot.EngineVersion = FEngineVersion::Current().ToString();
                Snapshot.PackPath = FinishedPackPath;
                Snapshot.Results = Results;
                if (MaxSavedHistoryRuns > 0 && !ProbeDev11RunHistory::Save(
                    ProbeDev11RunHistory::DefaultDirectory(), Snapshot, nullptr, MaxSavedHistoryRuns))
                { UE_LOG(LogTemp, Warning, TEXT("Probe could not persist run history under Saved/Probe/History.")); }
            }
            LastRunDurationMs = 0.0;
            for (const Probe::FTestResult& Result : Results) { LastRunDurationMs += Result.DurationMs; }
            FeedbackMessage = LOCTEXT("RunCompleted", "Run complete. Select a result for diagnostics.");
            ShowResults();
            return;
        }
        const int32 ResultIndex = QueuedTestResultIndices[0];
        if (const UProbeTestPack* Pack = ActivePackForRun.Get())
        {
            if (Pack->FailurePolicy == EProbePackFailurePolicy::StopOnFailure)
            {
                // Invalid/unloadable Pack references may already have an Error
                // result without a queued execution. Respect their Pack order.
                for (int32 Previous = 0; Previous < ResultIndex; ++Previous)
                {
                    if (ProbeDev10PackPolicy::IsProblem(Results[Previous].Status))
                    { bStopAfterCurrentTest = true; bStopDueToPackFailure = true; break; }
                }
                if (bStopAfterCurrentTest)
                {
                    ProbeQueueControl::SkipPending(QueuedTestResultIndices, Results,
                        TEXT("Skipped by Pack Stop on Failure policy after a failed/errored test."));
                    StartNextQueuedTest();
                    return;
                }
            }
        }
        QueuedTestResultIndices.RemoveAt(0);
        const FString AssetPath = Results[ResultIndex].AssetPath;
        if (const UProbeTestPack* Pack = ActivePackForRun.Get())
        {
            FString Reason;
            if (!ProbeDev10PackPolicy::DependenciesPassed(*Pack, AssetPath, Results, Reason))
            {
                Results[ResultIndex].Status = Probe::EResultStatus::Skipped;
                Results[ResultIndex].Detail = Reason;
                StartNextQueuedTest();
                return;
            }
        }
        UProbeTestAsset* Asset = FindObject<UProbeTestAsset>(nullptr, *AssetPath);
        if (!Asset) { Asset = LoadObject<UProbeTestAsset>(nullptr, *AssetPath); }
        if (!Asset)
        {
            Results[ResultIndex].Status = Probe::EResultStatus::Error;
            Results[ResultIndex].Detail = TEXT("Queued Probe Test could not be loaded.");
            if (const UProbeTestPack* Pack = ActivePackForRun.Get())
            { if (ProbeDev10PackPolicy::ShouldStop(*Pack, Probe::EResultStatus::Error))
              { bStopAfterCurrentTest = true; bStopDueToPackFailure = true; } }
            StartNextQueuedTest();
            return;
        }
        const Probe::FRunOptions Options = ScopedRunOptions.FindRef(ResultIndex);
        ScopedRunOptions.Remove(ResultIndex);
        ExecuteQueuedResult(ResultIndex, Asset, Options);
    }

    void RecordRunCompleted()
    {
        if (bRunInProgress) { return; }
        bRunInProgress = true;
        bStopAfterCurrentTest = false;
        bStopDueToPackFailure = false;
        bViewingSavedHistory = false;
        LastComparison = ProbeDev10RunHistory::FComparison();
        FeedbackMessage = LOCTEXT("RunInProgress", "Running Probe tests. Native UI steps execute asynchronously; results are shown as they finish.");
        StartNextQueuedTest();
    }

    FReply ExportSelectedPortableJson()
    {
        const UProbeTestAsset* Asset = SelectedAsset();
        if (!Asset)
        { FeedbackMessage = LOCTEXT("Dev19ExportNoTest", "Select a Probe Test Asset to export."); return FReply::Handled(); }
        // Authoring a *provider* requires an exact plugin name, not a friendly name.
        // Use the project name for an independent draft. The resulting folder is
        // never inserted into an installed plugin automatically.
        FString Destination, Error;
        const FString ProviderId = FApp::GetProjectName();
        if (ProbeDev19::ExportReadOnlyTest(*Asset, ProviderId, Destination, Error))
        { FeedbackMessage = FText::FromString(TEXT("Portable JSON exported: ") + Destination); }
        else { FeedbackMessage = FText::FromString(TEXT("Portable JSON export refused: ") + Error); }
        return FReply::Handled();
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
            TestJson->SetStringField(TEXT("variation"), Test.VariationName);
            TestJson->SetNumberField(TEXT("variation_index"), Test.VariationIndex);
            TestJson->SetStringField(TEXT("status"), Probe::StatusName(Test.Status));
            TestJson->SetStringField(TEXT("detail"), Test.Detail);
            TestJson->SetNumberField(TEXT("duration_ms"), Test.DurationMs);
            TestJson->SetNumberField(TEXT("attempts"), Test.Attempts);
            const UProbeTestAsset* SourceAsset = FindObject<UProbeTestAsset>(nullptr, *Test.AssetPath);
            TestJson->SetStringField(TEXT("failure_category"), ProbeDev10RunHistory::Category(Test, SourceAsset));
            TArray<TSharedPtr<FJsonValue>> Steps;
            for (const Probe::FStepResult& Step : Test.Steps)
            {
                TSharedRef<FJsonObject> StepJson = MakeShared<FJsonObject>();
                StepJson->SetNumberField(TEXT("index"), Step.Index);
                StepJson->SetStringField(TEXT("assertion_id"), Step.AssertionId);
                StepJson->SetStringField(TEXT("status"), Probe::StatusName(Step.Status));
                StepJson->SetStringField(TEXT("description"), Step.Description);
                StepJson->SetStringField(TEXT("group"), Step.GroupName);
                StepJson->SetStringField(TEXT("detail"), Step.Detail);
                StepJson->SetNumberField(TEXT("duration_ms"), Step.DurationMs);
                Steps.Add(MakeShared<FJsonValueObject>(StepJson));
            }
            TestJson->SetArrayField(TEXT("steps"), Steps);
            Tests.Add(MakeShared<FJsonValueObject>(TestJson));
        }
        Root->SetArrayField(TEXT("tests"), Tests);
        if (LastComparison.bHasPreviousRun)
        {
            TSharedRef<FJsonObject> Delta = MakeShared<FJsonObject>();
            Delta->SetNumberField(TEXT("regressions"), LastComparison.Regressions);
            Delta->SetNumberField(TEXT("recovered"), LastComparison.Recovered);
            Delta->SetNumberField(TEXT("still_failing"), LastComparison.StillFailing);
            Delta->SetNumberField(TEXT("newly_observed"), LastComparison.NewlyObserved);
            Root->SetObjectField(TEXT("previous_run_comparison"), Delta);
        }
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
              [ SNew(SButton)
                .ToolTipText(LOCTEXT("CreatePluginQAHelp", "Create a ready-to-run baseline QA Pack for an enabled Editor plugin. Automatically fills identifiers and expectations from the plugin descriptor."))
                .OnClicked(this, &SProbePackWorkspace::OpenPluginQAGenerator)
                [ SNew(STextBlock).Text(LOCTEXT("CreatePluginQA", "Plugin QA Pack...")) ] ]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,12,0)
              [ SNew(SButton).ToolTipText(LOCTEXT("SavePackTooltip", "Save changes to this Pack (Ctrl+S)."))
                .OnClicked(this, &SProbePackWorkspace::SavePack)
                .IsEnabled_Lambda([this]() { const UProbeTestPack* Pack = GetPack(); return Pack && Pack->GetOutermost()->IsDirty(); })
                [ SNew(SHorizontalBox)
                  + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
                  [ SNew(SImage).Image(FAppStyle::GetBrush("Icons.Save")) ]
                  + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                  [ SNew(STextBlock).Text(LOCTEXT("SaveCurrentPack", "Save")) ] ] ]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
              [ SNew(SCheckBox)
                .ToolTipText(LOCTEXT("PackStopPolicyHelp", "Stop a Pack after a failed/errored test. Pending tests are skipped; the current test always finishes."))
                .IsEnabled_Lambda([this]() { return GetPack() != nullptr; })
                .IsChecked_Lambda([this]() { const UProbeTestPack* Pack = GetPack(); return Pack && Pack->FailurePolicy == EProbePackFailurePolicy::StopOnFailure ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                {
                    if (UProbeTestPack* Pack = GetPack())
                    {
                        const FScopedTransaction Transaction(LOCTEXT("PackStopPolicyTransaction", "Set Probe Pack Failure Policy"));
                        Pack->Modify(); Pack->FailurePolicy = State == ECheckBoxState::Checked ? EProbePackFailurePolicy::StopOnFailure : EProbePackFailurePolicy::Continue;
                        Pack->MarkPackageDirty(); InvalidateAudit();
                    }
                })
                [ SNew(STextBlock).Text(LOCTEXT("PackStopAfterFailure", "Stop on Failure")) ] ]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,8,0)
              [ SNew(SCheckBox)
                .ToolTipText(LOCTEXT("PackRetryReadOnlyHelp", "Retry a failure once only for strictly read-only tests. Does not repeat UI input, fixture actions, selection changes, or provider assertions."))
                .IsEnabled_Lambda([this]() { return GetPack() != nullptr; })
                .IsChecked_Lambda([this]() { const UProbeTestPack* Pack = GetPack(); return Pack && Pack->bRetryReadOnlyFailuresOnce ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                {
                    if (UProbeTestPack* Pack = GetPack())
                    {
                        const FScopedTransaction Transaction(LOCTEXT("PackRetryReadOnlyTransaction", "Set Probe Pack Read-Only Retry"));
                        Pack->Modify(); Pack->bRetryReadOnlyFailuresOnce = State == ECheckBoxState::Checked;
                        Pack->MarkPackageDirty(); InvalidateAudit();
                    }
                })
                [ SNew(STextBlock).Text(LOCTEXT("PackRetryReadOnly", "Retry Read-Only Once")) ] ]
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
              [ SNew(SButton).OnClicked(this, &SProbePackWorkspace::ShowDev14PackReadiness)
                .ToolTipText(LOCTEXT("Dev14PackReadinessTip", "Preflight all Pack members and prerequisite rules, without executing tests."))
                .IsEnabled_Lambda([this]() { return GetPack() != nullptr; })
                [ SNew(STextBlock).Text(LOCTEXT("Dev14PackReadiness", "Readiness Overview")) ] ]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,4,0)
              [ SNew(SButton).OnClicked(this, &SProbePackWorkspace::ValidateCurrentReferences)
                .ToolTipText(LOCTEXT("ValidateCurrentPackTip", "Validate only the selected Pack's test references."))
                .IsEnabled_Lambda([this]() { return GetPack() != nullptr; })
                [ SNew(STextBlock).Text(LOCTEXT("ValidateThisPack", "Validate Pack")) ] ]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
              [ SNew(SButton).OnClicked(this, &SProbePackWorkspace::ValidateAllReferences)
                .ToolTipText(LOCTEXT("ValidateAllPackTip", "Check all Packs and duplicate stable Test IDs across the project."))
                [ SNew(STextBlock).Text(LOCTEXT("ValidateAllPacks", "Validate All")) ] ]
              + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4,0,6,0)
              [ SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton")
                .OnClicked_Lambda([]() { ProbeDev15::OpenBrowser(); return FReply::Handled(); })
                .ToolTipText(LOCTEXT("Dev15PackBrowserTip", "Open the project-wide QA Browser and readiness dashboard."))
                [ SNew(STextBlock).Text(LOCTEXT("Dev15PackBrowser", "QA Browser")) ] ]
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
        FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
        PackRemovedDelegate = RegistryModule.Get().OnAssetRemoved().AddSP(this, &SProbePackWorkspace::HandleAssetRemoved);
    }
    virtual ~SProbePackWorkspace() override
    {
        FEditorDelegates::PostUndoRedo.RemoveAll(this);
        if (FAssetRegistryModule* RegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>("AssetRegistry"))
        {
            RegistryModule->Get().OnAssetRemoved().Remove(PackRemovedDelegate);
        }
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
    FDelegateHandle PackRemovedDelegate;
    void HandleAssetRemoved(const FAssetData& Removed)
    {
        if (Removed.AssetClassPath != UProbeTestPack::StaticClass()->GetClassPathName()) { return; }
        const FString RemovedPath = Removed.GetObjectPathString();
        if (SelectedPack.IsValid() && SelectedPack->GetObjectPathString() == RemovedPath)
        {
            // Drop the stale FAssetData before any Slate text/enable bindings run.
            SelectedPack.Reset();
            SelectedMemberPaths.Reset();
            Message = TEXT("The selected Probe Pack was deleted. Choose another Pack or create a new one.");
            InvalidateAudit();
            if (PackSelector.IsValid()) { PackSelector->ClearSelection(); }
            if (MemberRows.IsValid()) { MemberRows->ClearChildren(); }
        }
        // Re-query on the next Slate tick, after the registry finishes dispatching deletion.
        RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateLambda(
            [this](double, float) -> EActiveTimerReturnType
            {
                RefreshPacks();
                return EActiveTimerReturnType::Stop;
            }));
    }
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
    FReply ShowDev14PackReadiness()
    {
        const UProbeTestPack* Pack = GetPack();
        if (!Pack) { return FReply::Handled(); }
        const ProbeDev14::FReport Report = ProbeDev14::PreflightPack(*Pack,
            [](const FString& Path) -> const UProbeTestAsset*
            { return Path.IsEmpty() ? nullptr : LoadObject<UProbeTestAsset>(nullptr, *Path); });
        FMessageDialog::Open(EAppMsgType::Ok, EAppReturnType::Ok, FText::FromString(Report.Text()));
        Message = FString::Printf(TEXT("Pack readiness: %d blocking, %d warning(s)."), Report.Blocking, Report.Warnings);
        return FReply::Handled();
    }

    FReply ValidateCurrentReferences()
    {
        const UProbeTestPack* Pack = GetPack();
        if (!Pack) { return FReply::Handled(); }
        const Probe::FReferenceAudit Report = Probe::AuditPackReferences(Pack);
        FString PolicyIssue;
        const bool bPolicyValid = ProbeDev10PackPolicy::Validate(*Pack, PolicyIssue);
        const FString Combined = Report.ToText() + (bPolicyValid ? TEXT("\nPack execution policy: Valid") : TEXT("\nPack execution policy: INVALID - ") + PolicyIssue);
        if (AuditOutput.IsValid()) { AuditOutput->SetText(FText::FromString(Combined)); }
        bHasAudit = true;
        Message = FString::Printf(TEXT("Validated Pack: %d reference(s), %d issue(s). %s"), Report.ReferencesChecked, Report.Problems,
            bPolicyValid ? TEXT("Execution policy valid.") : *PolicyIssue);
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

    FReply OpenPluginQAGenerator()
    {
        TArray<FProbeIdentifierChoice> Choices;
        for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetDiscoveredPlugins())
        {
            if (!Plugin->IsEnabled()) { continue; } // Generated enabled assertion must be meaningful.
            FProbeIdentifierChoice Choice;
            Choice.Id = Plugin->GetName();
            Choice.Display = Plugin->GetFriendlyName();
            if (Choice.Display.IsEmpty()) { Choice.Display = Choice.Id; }
            Choice.Owner = Plugin->GetDescriptor().Category;
            Choice.State = FString::Printf(TEXT("%d module(s)"), Plugin->GetDescriptor().Modules.Num());
            Choices.Add(MoveTemp(Choice));
        }
        Choices.Sort([](const FProbeIdentifierChoice& A, const FProbeIdentifierChoice& B)
        { return A.Display < B.Display; });
        TSharedRef<SWindow> Window = SNew(SWindow)
            .Title(LOCTEXT("SelectPluginForQAPack", "Generate Plugin QA Pack"))
            .ClientSize(FVector2D(485.0f, 405.0f))
            .SupportsMinimize(false).SupportsMaximize(false);
        Window->SetContent(
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(8,8,8,0)
            [ SNew(STextBlock).AutoWrapText(true)
              .Text(LOCTEXT("PluginQAGeneratorIntro", "Choose an enabled plugin. Probe will create four ready-to-run Test Assets plus a Pack, with identifiers taken from its live descriptor. No project levels or plugin settings are changed.")) ]
            + SVerticalBox::Slot().FillHeight(1).Padding(6)
            [ SNew(SProbeIdentifierPicker)
              .Candidates(Choices)
              .SearchHint(LOCTEXT("PluginQAFindPlugin", "Search enabled plugins by name or category..."))
              .OnPicked(FOnProbeIdentifierPicked::CreateLambda([this, WeakWindow = TWeakPtr<SWindow>(Window)](const FString& PluginName)
              {
                  if (const TSharedPtr<SWindow> WindowToClose = WeakWindow.Pin())
                  { WindowToClose->RequestDestroyWindow(); }
                  GeneratePluginQAPack(PluginName);
              })) ]);
        FSlateApplication::Get().AddModalWindow(Window, FSlateApplication::Get().GetActiveTopLevelWindow(), false);
        return FReply::Handled();
    }

    void GeneratePluginQAPack(const FString& PluginName)
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
        if (!Plugin.IsValid() || !Plugin->IsEnabled())
        { Message = TEXT("Choose an enabled, installed plugin."); return; }

        const FPluginDescriptor& Descriptor = Plugin->GetDescriptor();
        FString FolderName = Plugin->GetName();
        FolderName.ReplaceInline(TEXT("-"), TEXT("_"));
        FolderName.ReplaceInline(TEXT("."), TEXT("_"));
        FolderName.ReplaceInline(TEXT(" "), TEXT("_"));
        const FString Folder = TEXT("/Game/ProbeTests/PluginQA/") + FolderName;
        IAssetTools& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
        TArray<UProbeTestAsset*> CreatedTests;
        TArray<UPackage*> PackagesToSave;

        auto MakeStep = [](EProbeStepType Type, const FString& Target, const FString& Value = FString(), int32 Count = 0)
        {
            FProbeTestStep Step;
            Step.Type = Type;
            Step.Target = Target;
            Step.ExpectedValue = Value;
            Step.MinimumActorCount = Count;
            return Step;
        };
        auto CreateTest = [&](const TCHAR* Suffix, const FString& Description, TArray<FProbeTestStep> Steps)
        {
            FString PackageName, AssetName;
            Tools.CreateUniqueAssetName(Folder + TEXT("/QA_") + FolderName + TEXT("_") + Suffix,
                TEXT(""), PackageName, AssetName);
            UProbeTestAssetFactory* Factory = NewObject<UProbeTestAssetFactory>();
            UProbeTestAsset* Test = Cast<UProbeTestAsset>(Tools.CreateAsset(AssetName,
                FPackageName::GetLongPackagePath(PackageName), UProbeTestAsset::StaticClass(), Factory));
            if (Test)
            {
                Test->Modify();
                Test->TestDescription = Description;
                Test->bEnabled = true;
                Test->bNeedsConfiguration = false;
                Test->Steps = MoveTemp(Steps);
                Test->MarkPackageDirty();
                CreatedTests.Add(Test);
                PackagesToSave.AddUnique(Test->GetOutermost());
            }
        };

        {
            TArray<FProbeTestStep> Steps;
            Steps.Add(MakeStep(EProbeStepType::PluginInstalled, PluginName));
            Steps.Add(MakeStep(EProbeStepType::PluginEnabled, PluginName));
            CreateTest(TEXT("Availability"), TEXT("Verify plugin discovery and enabled state."), MoveTemp(Steps));
        }
        {
            TArray<FProbeTestStep> Steps;
            Steps.Add(MakeStep(EProbeStepType::PluginFriendlyNameEquals, PluginName, Plugin->GetFriendlyName()));
            if (!Descriptor.VersionName.IsEmpty())
            { Steps.Add(MakeStep(EProbeStepType::PluginVersionEquals, PluginName, Descriptor.VersionName)); }
            if (!Descriptor.Category.IsEmpty())
            { Steps.Add(MakeStep(EProbeStepType::PluginCategoryEquals, PluginName, Descriptor.Category)); }
            CreateTest(TEXT("Descriptor"), TEXT("Check plugin identity against the descriptor at Pack generation time. Update these expectations after intentional plugin version changes."), MoveTemp(Steps));
        }
        {
            TArray<FProbeTestStep> Steps;
            Steps.Add(MakeStep(EProbeStepType::PluginModuleCountExactly, PluginName, FString(), Descriptor.Modules.Num()));
            for (const FModuleDescriptor& Module : Descriptor.Modules)
            {
                const FString ModuleName = Module.Name.ToString();
                Steps.Add(MakeStep(EProbeStepType::PluginDeclaresModule, PluginName, ModuleName));
                // Lazy modules are not assumed to be loaded. Only baseline what is loaded now.
                if (FModuleManager::Get().IsModuleLoaded(Module.Name))
                { Steps.Add(MakeStep(EProbeStepType::ModuleLoaded, ModuleName)); }
            }
            CreateTest(TEXT("Modules"), TEXT("Validate descriptor module entries; checks loaded state only for modules currently loaded."), MoveTemp(Steps));
        }
        {
            TArray<FProbeTestStep> Steps;
            Steps.Add(MakeStep(EProbeStepType::EditorWorldExists, FString()));
            Steps.Add(MakeStep(EProbeStepType::PIEIsNotRunning, FString()));
            CreateTest(TEXT("EditorPreflight"), TEXT("Verify a loaded editor level and no active Play In Editor session."), MoveTemp(Steps));
        }

        if (CreatedTests.Num() != 4)
        {
            Message = FString::Printf(TEXT("Created only %d/4 QA tests. Check Output Log; partial assets were not deleted."), CreatedTests.Num());
            return;
        }
        FString PackageName, AssetName;
        Tools.CreateUniqueAssetName(Folder + TEXT("/QA_") + FolderName + TEXT("_Pack"),
            TEXT(""), PackageName, AssetName);
        UProbeTestPackFactory* PackFactory = NewObject<UProbeTestPackFactory>();
        UProbeTestPack* Pack = Cast<UProbeTestPack>(Tools.CreateAsset(AssetName,
            FPackageName::GetLongPackagePath(PackageName), UProbeTestPack::StaticClass(), PackFactory));
        if (!Pack)
        { Message = TEXT("QA tests created, but Pack creation failed. Check Output Log."); return; }
        Pack->Modify();
        Pack->Description = FString::Printf(TEXT("Generated baseline Editor plugin QA for %s. Validates descriptor, availability, declared modules, and Editor preflight. Does not yet test plugin-specific interactive behavior."), *PluginName);
        for (UProbeTestAsset* Test : CreatedTests)
        { Pack->Tests.Add(TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(Test->GetPathName()))); }
        Pack->MarkPackageDirty();
        PackagesToSave.AddUnique(Pack->GetOutermost());
        const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages(PackagesToSave, true);
        SelectPackPath(Pack->GetPathName());
        Message = FString::Printf(TEXT("Generated %d QA tests for '%s' and selected its Pack. %s Run Pack to establish your baseline."),
            CreatedTests.Num(), *PluginName,
            bSaved ? TEXT("Saved.") : TEXT("Save All to persist assets."));
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
        // Preserve dependency intent when a missing/moved Pack member is replaced.
        for (FProbePackPrerequisite& Rule : Pack->Prerequisites)
        {
            if (Rule.Test.ToString() == PreviousPath)
            { Rule.Test = TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(NewPath)); }
            if (Rule.Requires.ToString() == PreviousPath)
            { Rule.Requires = TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(NewPath)); }
        }
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
        Pack->Modify(); Pack->Tests.RemoveAt(Index);
        Pack->Prerequisites.RemoveAll([&Path](const FProbePackPrerequisite& Rule)
        { return Rule.Test.ToString() == Path || Rule.Requires.ToString() == Path; });
        Pack->MarkPackageDirty();
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
        if (Removed > 0)
        {
            Pack->Prerequisites.RemoveAll([this](const FProbePackPrerequisite& Rule)
            { return SelectedMemberPaths.Contains(Rule.Test.ToString()) || SelectedMemberPaths.Contains(Rule.Requires.ToString()); });
            Pack->MarkPackageDirty(); InvalidateAudit();
        }
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
    void TogglePrerequisite(const FString TestPath, const FString RequiredPath)
    {
        UProbeTestPack* Pack = GetPack();
        if (!Pack) { return; }
        const FScopedTransaction Transaction(LOCTEXT("PackDependencyTransaction", "Toggle Probe Pack Prerequisite"));
        Pack->Modify();
        const int32 Removed = Pack->Prerequisites.RemoveAll([&](const FProbePackPrerequisite& Dependency)
        { return Dependency.Test.ToString() == TestPath && Dependency.Requires.ToString() == RequiredPath; });
        if (!Removed)
        {
            FProbePackPrerequisite Dependency;
            Dependency.Test = TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(TestPath));
            Dependency.Requires = TSoftObjectPtr<UProbeTestAsset>(FSoftObjectPath(RequiredPath));
            Pack->Prerequisites.Add(MoveTemp(Dependency));
        }
        Pack->MarkPackageDirty();
        InvalidateAudit();
        RebuildMembers();
    }

    TSharedRef<SWidget> BuildPrerequisiteMenu(int32 DependentIndex)
    {
        FMenuBuilder Menu(true, nullptr);
        const UProbeTestPack* Pack = GetPack();
        if (!Pack || !Pack->Tests.IsValidIndex(DependentIndex)) { return Menu.MakeWidget(); }
        const FString DependentPath = Pack->Tests[DependentIndex].ToString();
        for (int32 Prior = 0; Prior < DependentIndex; ++Prior)
        {
            const FString RequiredPath = Pack->Tests[Prior].ToString();
            const bool bAlreadyRequired = Pack->Prerequisites.ContainsByPredicate([&](const FProbePackPrerequisite& Item)
            { return Item.Test.ToString() == DependentPath && Item.Requires.ToString() == RequiredPath; });
            const FString Label = FPackageName::GetShortName(RequiredPath) + (bAlreadyRequired ? TEXT(" [Required]") : FString());
            Menu.AddMenuEntry(FText::FromString(Label),
                FText::FromString(TEXT("Require this earlier Pack test to pass before running ") + DependentPath),
                FSlateIcon(), FUIAction(FExecuteAction::CreateSP(this,
                    &SProbePackWorkspace::TogglePrerequisite, DependentPath, RequiredPath)));
        }
        return Menu.MakeWidget();
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
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0,0,5,0)
                [ SNew(SComboButton)
                  .IsEnabled(Index > 0)
                  .ToolTipText(LOCTEXT("PackPrerequisitesTip", "Choose earlier Pack tests that must pass first. Checked entries show [Required]. Save the Pack to persist changes."))
                  .OnGetMenuContent_Lambda([this, Index]() { return BuildPrerequisiteMenu(Index); })
                  .ButtonContent()[ SNew(STextBlock).Text(LOCTEXT("PackPrerequisitesButton", "Requires")) ] ]
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

// Dev 17: transparent read-only discovery and opt-in execution of plugin-shipped JSON.
// Dev 19: one Provider Browser for discovery, diagnostics, previews, import,
// source editing, JSON export and explicit authorized execution.
class SProbePortableBrowser : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SProbePortableBrowser) {} SLATE_END_ARGS()

    void Construct(const FArguments&)
    {
        ChildSlot
        [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(10)
          [ SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
              [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                  [ SNew(STextBlock).Text(LOCTEXT("PortableProviderTitle", "Portable Plugin Providers"))
                    .Font(FAppStyle::GetFontStyle("HeadingExtraSmall")) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(3,0)
                  [ SNew(SButton).Text(LOCTEXT("Dev19Starter", "New Provider Starter..."))
                    .ToolTipText(LOCTEXT("Dev19StarterTip", "Generate validated, dependency-free JSON under Saved/Probe/ProviderStarters. Nothing is written to an installed plugin."))
                    .OnClicked_Lambda([this]() { OpenStarterDialog(); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(3,0)
                  [ SNew(SButton).Text(LOCTEXT("PortableProviderRefresh", "Validate & Refresh"))
                    .ToolTipText(LOCTEXT("PortableRefreshTip", "Validate Resources/Probe definitions from enabled plugins; never execute tests during discovery."))
                    .IsEnabled_Lambda([this]() { return !bRunning; })
                    .OnClicked_Lambda([this]() { ProbePortable::Refresh(); Rebuild(); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(3,0)
                  [ SNew(SButton).Text(LOCTEXT("PortableCancel", "Cancel Run"))
                    .ToolTipText(LOCTEXT("PortableCancelTip", "Request cancellation at the next safe step boundary. Async actions receive cancellation callbacks and cleanup still runs."))
                    .IsEnabled_Lambda([this]() { return bRunning; })
                    .OnClicked_Lambda([this]() {
                        if (ProbePortable::RequestCancel()) { Feedback = TEXT("Cancellation requested. Cleanup callbacks remain active."); }
                        return FReply::Handled(); }) ] ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,5)
              [ SNew(SSearchBox).HintText(LOCTEXT("Dev19SearchProviders", "Search providers, Tests, Packs, or IDs..."))
                .OnTextChanged_Lambda([this](const FText& Text) { SearchText = Text.ToString(); Rebuild(); }) ]
            + SVerticalBox::Slot().AutoHeight().Padding(0,0,0,6)
              [ SNew(STextBlock).AutoWrapText(true)
                .Text_Lambda([this]() { return FText::FromString(Feedback); }) ]
            + SVerticalBox::Slot().FillHeight(1.f)
              [ SNew(SSplitter).Orientation(Orient_Vertical)
                + SSplitter::Slot().Value(.67f)
                  [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(4)
                    [ SNew(SScrollBox) + SScrollBox::Slot() [ SAssignNew(Contents, SVerticalBox) ] ] ]
                + SSplitter::Slot().Value(.33f)
                  [ SNew(SBorder).BorderImage(FAppStyle::GetBrush("ToolPanel.GroupBorder")).Padding(8)
                    [ SNew(SScrollBox) + SScrollBox::Slot()
                      [ SAssignNew(Preview, STextBlock).AutoWrapText(true)
                        .Text(LOCTEXT("Dev19SelectPreview", "Select Preview on a Test or Pack to inspect steps, source and authorization requirements before running.")) ] ] ] ] ] ];
        Rebuild();
    }

private:
    TSharedPtr<SVerticalBox> Contents;
    TSharedPtr<STextBlock> Preview;
    FString SearchText;
    FString Feedback = TEXT("No execution during discovery. Portable action permissions are always explicit per run.");
    bool bRunning = false;

    bool Matches(const FString& Name, const FString& Id, const FString& ProviderName) const
    {
        return SearchText.IsEmpty() || Name.Contains(SearchText, ESearchCase::IgnoreCase)
            || Id.Contains(SearchText, ESearchCase::IgnoreCase)
            || ProviderName.Contains(SearchText, ESearchCase::IgnoreCase);
    }

    void Rebuild()
    {
        if (!Contents.IsValid()) { return; }
        Contents->ClearChildren();
        int32 Visible = 0;
        for (const ProbePortable::FPortableProvider& Provider : ProbePortable::Providers())
        {
            const bool bProviderMatches = SearchText.IsEmpty()
                || Provider.PluginName.Contains(SearchText, ESearchCase::IgnoreCase)
                || Provider.ProviderId.Contains(SearchText, ESearchCase::IgnoreCase);
            bool bAny = bProviderMatches;
            for (const ProbePortable::FPortableTest& Test : Provider.Tests)
            { bAny |= Matches(Test.Name, Test.Id, Provider.PluginName); }
            for (const ProbePortable::FPortablePack& Pack : Provider.Packs)
            { bAny |= Matches(Pack.Name, Pack.Id, Provider.PluginName); }
            if (!bAny) { continue; }
            ++Visible;
            const FString Header = FString::Printf(TEXT("%s  |  v%s  |  %d Tests  |  %d Packs  |  %d Actions"),
                *Provider.PluginName, *Provider.Version, Provider.Tests.Num(), Provider.Packs.Num(), Provider.Actions.Num());
            Contents->AddSlot().AutoHeight().Padding(2,7,2,3)
            [ SNew(SHorizontalBox)
              + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
                [ SNew(STextBlock).Text(FText::FromString(Header)).Font(FAppStyle::GetFontStyle("NormalFontBold")) ]
              + SHorizontalBox::Slot().AutoWidth().Padding(4,0)
                [ SNew(SButton).Text(LOCTEXT("Dev19ValidateProvider", "Validate"))
                  .ToolTipText(LOCTEXT("Dev19ValidateProviderTip", "Re-parse the plugin's Resources/Probe definition files without running them."))
                  .OnClicked_Lambda([this, Name=Provider.PluginName]() {
                      FString Summary, Error;
                      Feedback = ProbeDev19::ValidateInstalled(Name, Summary, Error) ? Summary : Error;
                      return FReply::Handled(); }) ]
              + SHorizontalBox::Slot().AutoWidth().Padding(4,0)
                [ SNew(SButton).Text(LOCTEXT("Dev19OpenProviderFolder", "Open JSON Folder"))
                  .ToolTipText(LOCTEXT("Dev19OpenProviderFolderTip", "Open this plugin's existing Resources/Probe folder for editing in your normal text editor."))
                  .OnClicked_Lambda([Dir=Provider.Directory]() {
                      FPlatformProcess::ExploreFolder(*Dir); return FReply::Handled(); }) ] ];
            for (const ProbePortable::FPortableTest& Test : Provider.Tests)
            {
                if (Matches(Test.Name, Test.Id, Provider.PluginName))
                { AddRunRow(Test.Name + TEXT("  [") + Test.Id + TEXT("]"), Test.Id, false,
                    Provider.PluginName, Test.Description); }
            }
            for (const ProbePortable::FPortablePack& Pack : Provider.Packs)
            {
                if (Matches(Pack.Name, Pack.Id, Provider.PluginName))
                { AddRunRow(Pack.Name + TEXT("  [") + Pack.Id + TEXT("]"), Pack.Id, true,
                    Provider.PluginName, FString::Printf(TEXT("%d Tests, %s"), Pack.TestIds.Num(),
                        Pack.bStopOnFailure ? TEXT("Stop on Failure") : TEXT("Continue on Failure"))); }
            }
        }
        for (const FString& Issue : ProbePortable::Diagnostics())
        {
            if (!SearchText.IsEmpty() && !Issue.Contains(SearchText, ESearchCase::IgnoreCase)) { continue; }
            ++Visible;
            Contents->AddSlot().AutoHeight().Padding(4)
            [ SNew(STextBlock).ColorAndOpacity(FLinearColor(1.f, .5f, .4f))
              .AutoWrapText(true).Text(FText::FromString(TEXT("Provider issue: ") + Issue)) ];
        }
        if (Visible == 0)
        {
            Contents->AddSlot().AutoHeight().Padding(4)
            [ SNew(STextBlock).AutoWrapText(true)
              .Text(LOCTEXT("Dev19NoProviders", "No matching providers or definitions. Enable a plugin containing Resources/Probe/manifest.json, or create a starter.")) ];
        }
        if (!bRunning)
        { Feedback = ProbePortable::DiscoverySummary() + TEXT(" Definitions are never executed during discovery."); }
    }

    void AddRunRow(const FString& Label, const FString& Id, bool bPack,
        const FString& ProviderName, const FString& Tip)
    {
        Contents->AddSlot().AutoHeight().Padding(3,2)
        [ SNew(SHorizontalBox)
          + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
            [ SNew(STextBlock).Text(FText::FromString(Label)).ToolTipText(FText::FromString(Tip)) ]
          + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
            [ SNew(SButton).Text(LOCTEXT("Dev19Preview", "Preview"))
              .OnClicked_Lambda([this, Id, bPack, ProviderName]() {
                  ShowPreview(ProviderName, Id, bPack); return FReply::Handled(); }) ]
          + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
            [ SNew(SComboButton)
              .ToolTipText(LOCTEXT("Dev19ProviderActionsTip", "Edit the original source JSON, export an unchanged copy, or import read-only Tests into editable Probe assets."))
              .OnGetMenuContent_Lambda([this, Id, bPack, ProviderName]() {
                  return BuildDefinitionMenu(ProviderName, Id, bPack); })
              .ButtonContent()
              [ SNew(STextBlock).Text(LOCTEXT("Dev19ProviderActions", "Actions")) ] ]
          + SHorizontalBox::Slot().AutoWidth().Padding(2,0)
            [ SNew(SButton).Text(FText::FromString(
                ProbePortable::RequiresAuthorization(Id, bPack) ? TEXT("Run Actions...") : TEXT("Run")))
              .IsEnabled_Lambda([this]() { return !bRunning; })
              .ToolTipText(LOCTEXT("RunPortableTip", "Explicitly execute this portable Test or Pack. Actions require separate confirmation; no .uasset is created."))
              .OnClicked_Lambda([this, Id, bPack]() { return Execute(Id, bPack); }) ] ];
    }

    TSharedRef<SWidget> BuildDefinitionMenu(const FString& ProviderName, const FString& Id, bool bPack)
    {
        FMenuBuilder Menu(true, nullptr);
        Menu.AddMenuEntry(LOCTEXT("Dev19EditJson", "Edit JSON"),
            LOCTEXT("Dev19EditJsonTip", "Open the original JSON in your associated text editor. Validate & Refresh after saving."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Id, bPack, ProviderName]()
            { OpenSource(ProviderName, Id, bPack); })));
        Menu.AddMenuEntry(LOCTEXT("Dev19ExportJson", "Export JSON"),
            LOCTEXT("Dev19ExportJsonTip", "Copy the exact validated source definition to Saved/Probe/DefinitionExports, without modifying the provider."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Id, bPack, ProviderName]()
            { ExportJson(ProviderName, Id, bPack); })));
        Menu.AddMenuSeparator();
        Menu.AddMenuEntry(LOCTEXT("Dev19ImportEditable", "Import as Editable Test"),
            LOCTEXT("Dev19ImportTip", "Create a disabled, Needs Review Test Asset. Unavailable for Packs or Tests with privileged actions."),
            FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([this, Id, ProviderName]()
            { ImportTest(ProviderName, Id); }),
                FCanExecuteAction::CreateLambda([Id, bPack]()
                { return !bPack && !ProbePortable::RequiresAuthorization(Id, false); })));
        return Menu.MakeWidget();
    }

    const ProbePortable::FPortableProvider* FindProvider(const FString& Name) const
    {
        return ProbePortable::Providers().FindByPredicate(
            [&Name](const ProbePortable::FPortableProvider& P) { return P.PluginName == Name; });
    }

    void ShowPreview(const FString& ProviderName, const FString& Id, bool bPack)
    {
        const ProbePortable::FPortableProvider* Provider = FindProvider(ProviderName);
        if (!Provider || !Preview.IsValid()) { return; }
        FString Detail;
        if (bPack)
        {
            const ProbePortable::FPortablePack* Pack = Provider->Packs.FindByPredicate(
                [&Id](const ProbePortable::FPortablePack& Item) { return Item.Id == Id; });
            if (Pack) { Detail = ProbeDev19::PreviewPack(*Provider, *Pack); }
        }
        else
        {
            const ProbePortable::FPortableTest* Test = Provider->Tests.FindByPredicate(
                [&Id](const ProbePortable::FPortableTest& Item) { return Item.Id == Id; });
            if (Test) { Detail = ProbeDev19::PreviewTest(*Provider, *Test); }
        }
        Preview->SetText(FText::FromString(Detail));
    }

    void OpenSource(const FString& ProviderName, const FString& Id, bool bPack)
    {
        const ProbePortable::FPortableProvider* Provider = FindProvider(ProviderName);
        if (!Provider) { return; }
        FString Relative;
        if (bPack)
        {
            if (const ProbePortable::FPortablePack* Pack = Provider->Packs.FindByPredicate(
                [&Id](const ProbePortable::FPortablePack& Item) { return Item.Id == Id; }))
            { Relative = Pack->SourcePath; }
        }
        else if (const ProbePortable::FPortableTest* Test = Provider->Tests.FindByPredicate(
            [&Id](const ProbePortable::FPortableTest& Item) { return Item.Id == Id; }))
        { Relative = Test->SourcePath; }
        FString Error;
        if (ProbePortable::ValidateRelativePath(Relative, Error))
        { FPlatformProcess::LaunchFileInDefaultExternalApplication(*(Provider->Directory / Relative)); }
        else { Feedback = Error; }
    }

    void ExportJson(const FString& ProviderName, const FString& Id, bool bPack)
    {
        const ProbePortable::FPortableProvider* Provider = FindProvider(ProviderName);
        if (!Provider) { return; }
        FString Path, Error;
        Feedback = ProbeDev19::ExportSourceDefinition(*Provider, Id, bPack, Path, Error)
            ? TEXT("Unmodified JSON exported: ") + Path : TEXT("Export failed: ") + Error;
    }

    void ImportTest(const FString& ProviderName, const FString& Id)
    {
        const ProbePortable::FPortableProvider* Provider = FindProvider(ProviderName);
        if (!Provider) { return; }
        const ProbePortable::FPortableTest* Test = Provider->Tests.FindByPredicate(
            [&Id](const ProbePortable::FPortableTest& Item) { return Item.Id == Id; });
        if (!Test) { return; }
        UProbeTestAsset* Asset = nullptr;
        FString Error;
        Feedback = ProbeDev19::ImportReadOnlyTest(*Test, Asset, Error)
            ? TEXT("Editable Test created (disabled, Needs Review): ") + Asset->GetPathName()
            : TEXT("Import refused: ") + Error;
    }

    void OpenStarterDialog()
    {
        const TSharedRef<SWindow> Dialog = SNew(SWindow)
            .Title(LOCTEXT("Dev19StarterTitle", "Create Probe Provider Starter"))
            .ClientSize(FVector2D(520,170)).SupportsMinimize(false).SupportsMaximize(false);
        TSharedPtr<SEditableTextBox> PluginInput;
        Dialog->SetContent(
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(12,12,12,7)
              [ SNew(STextBlock).AutoWrapText(true).Text(LOCTEXT("Dev19StarterInstructions",
                "Enter the EXACT .uplugin descriptor name. The starter is generated under Saved/Probe/ProviderStarters, never inside your installed plugin.")) ]
            + SVerticalBox::Slot().AutoHeight().Padding(12,0,12,8)
              [ SAssignNew(PluginInput, SEditableTextBox).HintText(LOCTEXT("Dev19StarterPlaceholder", "MyPlugin")) ]
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Right).Padding(12,0,12,10)
              [ SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().Padding(3,0)
                  [ SNew(SButton).Text(LOCTEXT("Dev19StarterCancel", "Cancel"))
                    .OnClicked_Lambda([Dialog]() { FSlateApplication::Get().RequestDestroyWindow(Dialog); return FReply::Handled(); }) ]
                + SHorizontalBox::Slot().AutoWidth().Padding(3,0)
                  [ SNew(SButton).Text(LOCTEXT("Dev19StarterCreate", "Create Starter"))
                    .OnClicked_Lambda([this, PluginInput, Dialog]() {
                        FString Directory, Error;
                        Feedback = ProbeDev19::CreateStarter(PluginInput->GetText().ToString(), Directory, Error)
                            ? TEXT("Created validated provider starter: ") + Directory : TEXT("Starter creation refused: ") + Error;
                        FSlateApplication::Get().RequestDestroyWindow(Dialog);
                        return FReply::Handled(); }) ] ]);
        FSlateApplication::Get().AddWindow(Dialog);
    }

    FReply Execute(const FString& Id, bool bPack)
    {
        if (bRunning) { return FReply::Handled(); }
        const bool bActions = ProbePortable::RequiresAuthorization(Id, bPack);
        if (bActions && FMessageDialog::Open(EAppMsgType::YesNo, EAppReturnType::No,
            FText::FromString(FString::Printf(
                TEXT("Allow portable Test/Pack '%s' to run provider actions?\n\nProvider functions may modify test-owned state. Cleanup functions are called, but arbitrary native code cannot be guaranteed reversible. Only authorize trusted providers."),
                *Id))) != EAppReturnType::Yes)
        { return FReply::Handled(); }
        bRunning = true;
        Feedback = TEXT("Running ") + Id;
        const TWeakPtr<SProbePortableBrowser> Weak = SharedThis(this);
        const auto Done = [Weak, Id, bPack, bActions](TArray<Probe::FTestResult> Results)
        {
            for (const Probe::FTestResult& Result : Results)
            { UE_LOG(LogTemp, Display, TEXT("Probe Portable %s: %s (%s)"),
                *Result.Id, *ProbeDev19::DisplayStatus(Result), *Result.Detail); }
            FString File, Error;
            const bool bSaved = ProbeDev19::SaveRunReport(Id, bPack, bActions, Results, File, Error);
            if (const TSharedPtr<SProbePortableBrowser> Self = Weak.Pin())
            {
                Self->bRunning = false;
                Self->Feedback = TEXT("Run complete: ") + ProbeDev19::Summarize(Results).ToString()
                    + (bSaved ? TEXT(". Report: ") + File : TEXT(". Report FAILED: ") + Error);
            }
        };
        FString Error;
        const bool bAccepted = bPack
            ? ProbePortable::RunPack(Id, Done, Error, bActions)
            : ProbePortable::RunTest(Id, Done, Error, bActions);
        if (!bAccepted) { bRunning = false; Feedback = Error; }
        return FReply::Handled();
    }
};

static void ProbePortableConsoleRun(const TArray<FString>& Args, bool bPack, bool bActions)
{
    if (Args.Num() != 1)
    {
        UE_LOG(LogTemp, Warning, TEXT("Usage: Probe.Portable.RunTest <Provider.TestId> / Probe.Portable.RunPack <Provider.PackId>"));
        return;
    }
    FString Error;
    const FString DefinitionId = Args[0];
    auto Done = [DefinitionId, bPack, bActions](TArray<Probe::FTestResult> Results)
    {
        for (const Probe::FTestResult& Result : Results)
        {
            UE_LOG(LogTemp, Display, TEXT("Probe Portable %s => %s: %s"),
                *Result.Id, *ProbeDev19::DisplayStatus(Result), *Result.Detail);
        }
        FString File, Error;
        if (ProbeDev19::SaveRunReport(DefinitionId, bPack, bActions, Results, File, Error))
        { UE_LOG(LogTemp, Display, TEXT("Probe Portable run: %s. JSON report: %s"),
            *ProbeDev19::Summarize(Results).ToString(), *File); }
        else { UE_LOG(LogTemp, Warning, TEXT("Probe Portable report export failed: %s"), *Error); }
    };
    const bool bAccepted = bPack
        ? ProbePortable::RunPack(Args[0], Done, Error, bActions)
        : ProbePortable::RunTest(Args[0], Done, Error, bActions);
    if (!bAccepted) { UE_LOG(LogTemp, Error, TEXT("Probe Portable: %s"), *Error); }
}

static void ProbePortableConsoleRunTest(const TArray<FString>& Args)
{ ProbePortableConsoleRun(Args, false, false); }
static void ProbePortableConsoleRunPack(const TArray<FString>& Args)
{ ProbePortableConsoleRun(Args, true, false); }
static void ProbePortableConsoleRunTestActions(const TArray<FString>& Args)
{ ProbePortableConsoleRun(Args, false, true); }
static void ProbePortableConsoleRunPackActions(const TArray<FString>& Args)
{ ProbePortableConsoleRun(Args, true, true); }
static void ProbePortableConsoleRefresh()
{
    ProbePortable::Refresh();
    UE_LOG(LogTemp, Display, TEXT("Probe Portable: %s"), *ProbePortable::DiscoverySummary());
    for (const FString& Issue : ProbePortable::Diagnostics())
    { UE_LOG(LogTemp, Warning, TEXT("Probe Portable discovery: %s"), *Issue); }
}
static void ProbePortableConsoleCancel()
{
    UE_LOG(LogTemp, Display, TEXT("Probe Portable: %s"),
        ProbePortable::RequestCancel() ? TEXT("Cancellation requested.") : TEXT("No portable runs active."));
}


// Dev 19: developer tools never execute tests and never write inside installed plugins.
static void ProbePortableConsoleValidate(const TArray<FString>& Args)
{
    if (Args.Num() != 1)
    { UE_LOG(LogTemp, Warning, TEXT("Usage: Probe.Portable.Validate <ExactPluginName>")); return; }
    FString Summary, Error;
    if (ProbeDev19::ValidateInstalled(Args[0], Summary, Error))
    { UE_LOG(LogTemp, Display, TEXT("Probe Portable: %s"), *Summary); }
    else { UE_LOG(LogTemp, Error, TEXT("Probe Portable validation: %s"), *Error); }
}
static void ProbePortableConsoleStarter(const TArray<FString>& Args)
{
    if (Args.Num() != 1)
    { UE_LOG(LogTemp, Warning, TEXT("Usage: Probe.Portable.CreateStarter <ExactPluginName>")); return; }
    FString Directory, Error;
    if (ProbeDev19::CreateStarter(Args[0], Directory, Error))
    { UE_LOG(LogTemp, Display, TEXT("Probe Portable: Starter generated at %s"), *Directory); }
    else { UE_LOG(LogTemp, Error, TEXT("Probe Portable starter: %s"), *Error); }
}
static void ProbePortableConsoleImport(const TArray<FString>& Args)
{
    if (Args.Num() != 1)
    { UE_LOG(LogTemp, Warning, TEXT("Usage: Probe.Portable.ImportTest <Provider.TestId>")); return; }
    for (const ProbePortable::FPortableProvider& Provider : ProbePortable::Providers())
    {
        for (const ProbePortable::FPortableTest& Test : Provider.Tests)
        {
            if (Test.Id != Args[0]) { continue; }
            UProbeTestAsset* Asset = nullptr;
            FString Error;
            if (ProbeDev19::ImportReadOnlyTest(Test, Asset, Error))
            { UE_LOG(LogTemp, Display, TEXT("Probe Portable imported disabled Test Asset: %s"), *Asset->GetPathName()); }
            else { UE_LOG(LogTemp, Error, TEXT("Probe Portable import refused: %s"), *Error); }
            return;
        }
    }
    UE_LOG(LogTemp, Error, TEXT("Unknown portable Test: %s"), *Args[0]);
}
static void ProbePortableConsoleExportAsset(const TArray<FString>& Args)
{
    if (Args.Num() != 2)
    { UE_LOG(LogTemp, Warning, TEXT("Usage: Probe.Portable.ExportAsset </Game/Path/Asset.Asset> <ExactProviderName>")); return; }
    UProbeTestAsset* Asset = LoadObject<UProbeTestAsset>(nullptr, *Args[0]);
    if (!Asset)
    { UE_LOG(LogTemp, Error, TEXT("No Probe Test Asset found at %s"), *Args[0]); return; }
    FString File, Error;
    if (ProbeDev19::ExportReadOnlyTest(*Asset, Args[1], File, Error))
    { UE_LOG(LogTemp, Display, TEXT("Probe Portable JSON exported: %s"), *File); }
    else { UE_LOG(LogTemp, Error, TEXT("Probe Portable JSON export refused: %s"), *Error); }
}

class FProbeEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        Probe::RegisterProbeAdvancedTemplates();
        Probe::RegisterProbeWorkflowTemplates();
        Probe::RegisterProbeActionTemplates();
        Probe::RegisterProbeSelectionTemplates();
        Probe::RegisterProbeActorFixtureTemplates();
        Probe::RegisterProbeSpatialTemplates();
        Probe::RegisterProbeBehaviorContractTemplates();
        Probe::RegisterProbeContractTemplates();
        // Temporary developer console command; not exposed in production Probe menus.
        IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("Probe.GenerateTemplateTests"),
            TEXT("Create one Probe Test asset per registered template under /Game/Probe/<Category>/<Group>; skip existing assets."),
            FConsoleCommandDelegate::CreateStatic(&Probe::GenerateTemplateTests),
            ECVF_Default);
        IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("Probe.SelfTest"),
            TEXT("Run deterministic Probe Self-Test Smoke cases, verify all outcomes independently, and write a JSON report under Saved/Probe/SelfTests."),
            FConsoleCommandDelegate::CreateStatic(&Probe::RunSelfTestConsoleCommand),
            ECVF_Default);
        // Probe manages registration. Other plugins only ship optional JSON files.
        ProbePortable::Refresh();
        IConsoleManager::Get().RegisterConsoleCommand(TEXT("Probe.Portable.Refresh"),
            TEXT("Refresh dependency-free Resources/Probe JSON provider discovery; never executes tests."),
            FConsoleCommandDelegate::CreateStatic(&ProbePortableConsoleRefresh), ECVF_Default);
        IConsoleManager::Get().RegisterConsoleCommand(TEXT("Probe.Portable.RunTest"),
            TEXT("Explicitly run a portable JSON Test by namespaced ID."),
            FConsoleCommandWithArgsDelegate::CreateStatic(&ProbePortableConsoleRunTest), ECVF_Default);
        IConsoleManager::Get().RegisterConsoleCommand(TEXT("Probe.Portable.RunPack"),
            TEXT("Explicitly run a read-only portable JSON Pack by namespaced ID."),
            FConsoleCommandWithArgsDelegate::CreateStatic(&ProbePortableConsoleRunPack), ECVF_Default);
        IConsoleManager::Get().RegisterConsoleCommand(TEXT("Probe.Portable.RunTestActions"),
            TEXT("Explicitly authorize provider actions and fixture operations for this Test."),
            FConsoleCommandWithArgsDelegate::CreateStatic(&ProbePortableConsoleRunTestActions), ECVF_Default);
        IConsoleManager::Get().RegisterConsoleCommand(TEXT("Probe.Portable.RunPackActions"),
            TEXT("Explicitly authorize provider actions and fixture operations for this Pack."),
            FConsoleCommandWithArgsDelegate::CreateStatic(&ProbePortableConsoleRunPackActions), ECVF_Default);
        IConsoleManager::Get().RegisterConsoleCommand(TEXT("Probe.Portable.Validate"),
            TEXT("Validate a named enabled plugin's Resources/Probe files without executing tests."),
            FConsoleCommandWithArgsDelegate::CreateStatic(&ProbePortableConsoleValidate), ECVF_Default);
        IConsoleManager::Get().RegisterConsoleCommand(TEXT("Probe.Portable.CreateStarter"),
            TEXT("Generate an isolated, validated provider starter under Saved/Probe/ProviderStarters."),
            FConsoleCommandWithArgsDelegate::CreateStatic(&ProbePortableConsoleStarter), ECVF_Default);
        IConsoleManager::Get().RegisterConsoleCommand(TEXT("Probe.Portable.ImportTest"),
            TEXT("Import a read-only portable Test into a disabled, Needs Review Test Asset."),
            FConsoleCommandWithArgsDelegate::CreateStatic(&ProbePortableConsoleImport), ECVF_Default);
        IConsoleManager::Get().RegisterConsoleCommand(TEXT("Probe.Portable.ExportAsset"),
            TEXT("Export a read-only Probe Test Asset as validated portable JSON under Saved/Probe."),
            FConsoleCommandWithArgsDelegate::CreateStatic(&ProbePortableConsoleExportAsset), ECVF_Default);
        IConsoleManager::Get().RegisterConsoleCommand(TEXT("Probe.Portable.Cancel"),
            TEXT("Request cancellation of active portable runs at safe boundaries."),
            FConsoleCommandDelegate::CreateStatic(&ProbePortableConsoleCancel), ECVF_Default);
        RegisterAssetAppearance();

        // Record only Probe assets affected by Undo/Redo. The post-transaction callback
        // restores package dirtiness after the engine has restored transaction package flags.
        FCoreUObjectDelegates::OnObjectTransacted.AddRaw(this, &FProbeEditorModule::OnObjectTransacted);
        FEditorDelegates::PostUndoRedo.AddRaw(this, &FProbeEditorModule::OnPostUndoRedo);
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(Probe::PortableTabId,
            FOnSpawnTab::CreateLambda([](const FSpawnTabArgs&)
            {
                return SNew(SDockTab).TabRole(ETabRole::NomadTab)
                    [ SNew(SProbePortableBrowser) ];
            }))
            .SetDisplayName(LOCTEXT("ProbePortableTabName", "Probe Providers"))
            .SetIcon(FSlateIcon(TEXT("ProbeAssetAppearance"), TEXT("Probe.TabIcon")))
            .SetMenuType(ETabSpawnerMenuType::Hidden);
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
        ProbePortable::Clear();
        IConsoleManager::Get().UnregisterConsoleObject(TEXT("Probe.Portable.Refresh"), false);
        IConsoleManager::Get().UnregisterConsoleObject(TEXT("Probe.Portable.RunTest"), false);
        IConsoleManager::Get().UnregisterConsoleObject(TEXT("Probe.Portable.RunPack"), false);
        IConsoleManager::Get().UnregisterConsoleObject(TEXT("Probe.Portable.RunTestActions"), false);
        IConsoleManager::Get().UnregisterConsoleObject(TEXT("Probe.Portable.RunPackActions"), false);
        IConsoleManager::Get().UnregisterConsoleObject(TEXT("Probe.Portable.Cancel"), false);
        IConsoleManager::Get().UnregisterConsoleObject(TEXT("Probe.Portable.Validate"), false);
        IConsoleManager::Get().UnregisterConsoleObject(TEXT("Probe.Portable.CreateStarter"), false);
        IConsoleManager::Get().UnregisterConsoleObject(TEXT("Probe.Portable.ImportTest"), false);
        IConsoleManager::Get().UnregisterConsoleObject(TEXT("Probe.Portable.ExportAsset"), false);

        IConsoleManager::Get().UnregisterConsoleObject(TEXT("Probe.SelfTest"), false);
        IConsoleManager::Get().UnregisterConsoleObject(TEXT("Probe.GenerateTemplateTests"), false);
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
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(Probe::PortableTabId);
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

        Section.AddMenuEntry("OpenProbeQABrowser", LOCTEXT("OpenProbeQABrowser", "Probe QA Browser"),
            LOCTEXT("OpenProbeQABrowserTip", "Search Test Assets and Packs and inspect authoring readiness without executing tests."),
            FSlateIcon(TEXT("ProbeAssetAppearance"), TEXT("Probe.TabIcon")),
            FUIAction(FExecuteAction::CreateLambda([]() { ProbeDev15::OpenBrowser(); })));

        Section.AddMenuEntry("OpenProbePortableProviders",
            LOCTEXT("OpenProbePortableProviders", "Probe Providers"),
            LOCTEXT("OpenProbePortableProvidersTip", "Discover and explicitly run portable JSON Tests/Packs shipped by enabled plugins in Resources/Probe, without a Probe dependency."),
            FSlateIcon(TEXT("ProbeAssetAppearance"), TEXT("Probe.TabIcon")),
            FUIAction(FExecuteAction::CreateLambda([]()
            { FGlobalTabmanager::Get()->TryInvokeTab(Probe::PortableTabId); })));

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
