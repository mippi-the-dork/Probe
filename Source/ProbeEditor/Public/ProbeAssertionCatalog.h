#pragma once
#include "CoreMinimal.h"
#include "ProbeTestAsset.h"

// The catalog is the single source for assertion menus, labels, parameter visibility, and help text.
// Existing enum values are preserved for serialized Probe Test assets.
namespace Probe
{
    enum EProbeFieldFlags : uint8 { TargetField = 1, CountField = 2, ClassField = 4, PropertyField = 8, ValueField = 16, ToleranceField = 32 };
    struct FAssertionInfo
    {
        EProbeStepType Type;
        const TCHAR* Category;
        const TCHAR* Name;
        const TCHAR* Description;
        uint8 Fields;
    };

    inline const TArray<FAssertionInfo>& AssertionCatalog()
    {
        static const TArray<FAssertionInfo> Catalog = {
            {EProbeStepType::PluginEnabled, TEXT("Plugins & Modules"), TEXT("Plugin Is Enabled"), TEXT("Confirm a named plugin is enabled"), TargetField},
            {EProbeStepType::ClassExists, TEXT("Classes & Reflection"), TEXT("Class Exists"), TEXT("Resolve a class by Unreal class path or name"), TargetField},
            {EProbeStepType::AssetExists, TEXT("Assets"), TEXT("Asset Exists"), TEXT("Find an asset by its object path"), TargetField},
            {EProbeStepType::EditorWorldExists, TEXT("Editor State"), TEXT("Editor World Exists"), TEXT("Check that the level Editor world is available"), 0},
            {EProbeStepType::ActorCountAtLeast, TEXT("Actors & World"), TEXT("Actor Count At Least"), TEXT("Count level actors (at least)"), CountField},
            {EProbeStepType::ActorClassCountAtLeast, TEXT("Actors & World"), TEXT("Actor Class Count At Least"), TEXT("Count actors derived from a class (at least)"), TargetField | CountField},
            {EProbeStepType::ActorTagExists, TEXT("Actors & World"), TEXT("Actor With Tag Exists"), TEXT("Find a level actor with a tag"), TargetField},
            {EProbeStepType::AssetClassIs, TEXT("Assets"), TEXT("Asset Is Class"), TEXT("Verify an asset is an instance of a class"), TargetField | ClassField},
            {EProbeStepType::ObjectPropertyEquals, TEXT("Properties & Values"), TEXT("Object Property Equals"), TEXT("Compare an object property with an expected value"), TargetField | PropertyField | ValueField | ToleranceField},
            {EProbeStepType::ActorLabelExists, TEXT("Actors & World"), TEXT("Actor With Label Exists"), TEXT("Find an actor by its exact Outliner label"), TargetField},
            {EProbeStepType::SelectedActorCountAtLeast, TEXT("Selection"), TEXT("Selected Actor Count At Least"), TEXT("Count selected level actors (at least)"), CountField},
            {EProbeStepType::PluginInstalled, TEXT("Plugins & Modules"), TEXT("Plugin Is Installed"), TEXT("Check that a plugin is discoverable, regardless of enabled state"), TargetField},
            {EProbeStepType::PluginDisabled, TEXT("Plugins & Modules"), TEXT("Plugin Is Disabled"), TEXT("Check that an installed plugin is explicitly disabled"), TargetField},
            {EProbeStepType::ModuleLoaded, TEXT("Plugins & Modules"), TEXT("Module Is Loaded"), TEXT("Check whether an Unreal module is loaded"), TargetField},
            {EProbeStepType::ConsoleVariableExists, TEXT("Editor State"), TEXT("Console Variable Exists"), TEXT("Check an engine console variable is registered"), TargetField},
            {EProbeStepType::ConsoleVariableEquals, TEXT("Editor State"), TEXT("Console Variable Equals"), TEXT("Compare a console variable with its current string value"), TargetField | ValueField},
            {EProbeStepType::EditorTabOpen, TEXT("Editor UI"), TEXT("Editor Tab Is Open"), TEXT("Check whether a tab with this internal tab ID is open"), TargetField},
            {EProbeStepType::ToolMenuRegistered, TEXT("Editor UI"), TEXT("Tool Menu Is Registered"), TEXT("Check whether a named ToolMenus menu is registered"), TargetField},
            {EProbeStepType::AssetCountAtLeast, TEXT("Assets"), TEXT("Asset Count In Folder At Least"), TEXT("Count assets under a /Game or plugin content folder, recursively"), TargetField | CountField},
            {EProbeStepType::AssetIsDirty, TEXT("Assets"), TEXT("Asset Has Unsaved Changes"), TEXT("Check an asset package is dirty (loads asset if necessary)"), TargetField},
            {EProbeStepType::AssetIsClean, TEXT("Assets"), TEXT("Asset Has No Unsaved Changes"), TEXT("Check an asset package is clean (loads asset if necessary)"), TargetField},
            {EProbeStepType::AssetIsLoaded, TEXT("Assets"), TEXT("Asset Is Loaded"), TEXT("Check whether an asset object is already in memory without loading it"), TargetField},
            {EProbeStepType::AssetRegistryTagEquals, TEXT("Assets"), TEXT("Asset Registry Tag Equals"), TEXT("Read a searchable asset registry tag without loading the asset"), TargetField | PropertyField | ValueField},
            {EProbeStepType::ClassIsChildOf, TEXT("Classes & Reflection"), TEXT("Class Derives From Class"), TEXT("Verify inheritance between two classes"), TargetField | ClassField},
            {EProbeStepType::ClassImplementsInterface, TEXT("Classes & Reflection"), TEXT("Class Implements Interface"), TEXT("Check a class implements a UInterface"), TargetField | ClassField},
            {EProbeStepType::ClassHasProperty, TEXT("Classes & Reflection"), TEXT("Class Has Property"), TEXT("Check a reflected property exists on a class"), TargetField | PropertyField},
            {EProbeStepType::ClassHasFunction, TEXT("Classes & Reflection"), TEXT("Class Has Function"), TEXT("Check a reflected UFUNCTION exists on a class"), TargetField | PropertyField},
            {EProbeStepType::ObjectLoaded, TEXT("Properties & Values"), TEXT("Object Is Loaded"), TEXT("Check whether an object is already loaded without loading it"), TargetField},
            {EProbeStepType::ObjectPropertyExists, TEXT("Properties & Values"), TEXT("Object Property Exists"), TEXT("Check a loaded or loadable object has a reflected property"), TargetField | PropertyField},
            {EProbeStepType::ObjectPropertyNotEquals, TEXT("Properties & Values"), TEXT("Object Property Does Not Equal"), TEXT("Compare a reflected property with a value and expect inequality"), TargetField | PropertyField | ValueField | ToleranceField},
            {EProbeStepType::ActorLabelNotExists, TEXT("Actors & World"), TEXT("Actor With Label Does Not Exist"), TEXT("Ensure there is no actor with this exact Outliner label"), TargetField},
            {EProbeStepType::ActorTagNotExists, TEXT("Actors & World"), TEXT("Actor With Tag Does Not Exist"), TEXT("Ensure no level actor has a particular tag"), TargetField},
            {EProbeStepType::ActorHasComponent, TEXT("Actors & World"), TEXT("Actor Has Component"), TEXT("Find a component class on an actor identified by Outliner label"), TargetField | ClassField},
            {EProbeStepType::ActorFolderEquals, TEXT("Actors & World"), TEXT("Actor Folder Equals"), TEXT("Check the World Outliner folder path of an actor"), TargetField | ValueField},
            {EProbeStepType::ActorCountExactly, TEXT("Actors & World"), TEXT("Actor Count Exactly"), TEXT("Require exactly this many level actors"), CountField},
            {EProbeStepType::ActorClassCountExactly, TEXT("Actors & World"), TEXT("Actor Class Count Exactly"), TEXT("Require exactly this many actors of a class"), TargetField | CountField},
            {EProbeStepType::ActorPropertyEquals, TEXT("Properties & Values"), TEXT("Actor Property Equals"), TEXT("Compare an actor property by Outliner label"), TargetField | PropertyField | ValueField | ToleranceField},
            {EProbeStepType::SelectedActorCountExactly, TEXT("Selection"), TEXT("Selected Actor Count Exactly"), TEXT("Require exactly this many selected level actors"), CountField},
            {EProbeStepType::SelectedActorLabelEquals, TEXT("Selection"), TEXT("Selected Actor Label Equals"), TEXT("Require one selected actor with an exact label"), TargetField},
            {EProbeStepType::SelectedActorIsClass, TEXT("Selection"), TEXT("Selected Actor Is Class"), TEXT("Require one selected actor derived from a class"), TargetField},
            {EProbeStepType::SelectedActorHasTag, TEXT("Selection"), TEXT("Selected Actor Has Tag"), TEXT("Require one selected actor with a tag"), TargetField},
            {EProbeStepType::PluginFriendlyNameEquals, TEXT("Plugins & Modules"), TEXT("Plugin Friendly Name Equals"), TEXT("Compare the friendly name declared in a plugin descriptor"), TargetField | ValueField},
            {EProbeStepType::PluginVersionEquals, TEXT("Plugins & Modules"), TEXT("Plugin Version Equals"), TEXT("Compare the VersionName string in a plugin descriptor"), TargetField | ValueField},
            {EProbeStepType::PluginDeclaresModule, TEXT("Plugins & Modules"), TEXT("Plugin Declares Module"), TEXT("Check a plugin descriptor lists a module; Target is plugin name"), TargetField | ValueField},
            {EProbeStepType::PluginHasContent, TEXT("Plugins & Modules"), TEXT("Plugin Has Content"), TEXT("Check that a plugin descriptor has CanContainContent enabled"), TargetField},
            {EProbeStepType::ModuleNotLoaded, TEXT("Plugins & Modules"), TEXT("Module Is Not Loaded"), TEXT("Check a module is currently unloaded; does not load it"), TargetField},
            {EProbeStepType::ModuleExists, TEXT("Plugins & Modules"), TEXT("Module Exists"), TEXT("Check that Unreal can locate a module without loading it"), TargetField},
            {EProbeStepType::EditorTabNotOpen, TEXT("Editor UI"), TEXT("Editor Tab Is Not Open"), TEXT("Check that a global Editor tab is not open"), TargetField},
            {EProbeStepType::EditorTabRegistered, TEXT("Editor UI"), TEXT("Editor Tab Is Registered"), TEXT("Check that a global tab spawner is registered, whether or not its tab is open"), TargetField},
            {EProbeStepType::ToolMenuNotRegistered, TEXT("Editor UI"), TEXT("Tool Menu Is Not Registered"), TEXT("Check that a ToolMenus menu ID is absent"), TargetField},
            {EProbeStepType::ConsoleVariableNotExists, TEXT("Editor State"), TEXT("Console Variable Does Not Exist"), TEXT("Check a console variable is not registered"), TargetField},
            {EProbeStepType::ConsoleVariableNotEquals, TEXT("Editor State"), TEXT("Console Variable Does Not Equal"), TEXT("Compare current console variable string to a disallowed value"), TargetField | ValueField},
            {EProbeStepType::PIEIsRunning, TEXT("Editor State"), TEXT("Play In Editor Is Running"), TEXT("Verify the Editor is currently running a PIE session"), 0},
            {EProbeStepType::PIEIsNotRunning, TEXT("Editor State"), TEXT("Play In Editor Is Not Running"), TEXT("Verify the Editor is not in a PIE session"), 0},
            {EProbeStepType::AssetNotExists, TEXT("Assets"), TEXT("Asset Does Not Exist"), TEXT("Verify the Asset Registry contains no asset at this object path"), TargetField},
            {EProbeStepType::AssetCountExactly, TEXT("Assets"), TEXT("Asset Count In Folder Exactly"), TEXT("Require exactly N assets recursively beneath a package folder"), TargetField | CountField},
            {EProbeStepType::AssetRegistryTagExists, TEXT("Assets"), TEXT("Asset Registry Tag Exists"), TEXT("Check an asset publishes a named searchable registry tag"), TargetField | PropertyField},
            {EProbeStepType::ClassNotExists, TEXT("Classes & Reflection"), TEXT("Class Does Not Exist"), TEXT("Verify a class cannot be resolved"), TargetField},
            {EProbeStepType::ClassHasNoProperty, TEXT("Classes & Reflection"), TEXT("Class Does Not Have Property"), TEXT("Check absence of a reflected property, including inherited members"), TargetField | PropertyField},
            {EProbeStepType::ClassHasNoFunction, TEXT("Classes & Reflection"), TEXT("Class Does Not Have Function"), TEXT("Check absence of a reflected UFUNCTION, including script/display aliases"), TargetField | PropertyField},
            {EProbeStepType::ClassIsAbstract, TEXT("Classes & Reflection"), TEXT("Class Is Abstract"), TEXT("Check a class carries Unreal's abstract class flag"), TargetField},
            {EProbeStepType::ClassFunctionBlueprintCallable, TEXT("Classes & Reflection"), TEXT("Class Function Is Blueprint Callable"), TEXT("Check whether a reflected function has the BlueprintCallable flag"), TargetField | PropertyField},
            {EProbeStepType::ObjectNotLoaded, TEXT("Properties & Values"), TEXT("Object Is Not Loaded"), TEXT("Check an object is absent from memory without loading it"), TargetField},
            {EProbeStepType::ObjectPropertyGreaterThan, TEXT("Properties & Values"), TEXT("Object Property Greater Than"), TEXT("Compare a numeric reflected property against a threshold"), TargetField | PropertyField | ValueField | ToleranceField},
            {EProbeStepType::ObjectPropertyLessThan, TEXT("Properties & Values"), TEXT("Object Property Less Than"), TEXT("Compare a numeric reflected property against a threshold"), TargetField | PropertyField | ValueField | ToleranceField},
            {EProbeStepType::ActorCountAtMost, TEXT("Actors & World"), TEXT("Actor Count At Most"), TEXT("Verify no more than N actors are in the Editor world"), CountField},
            {EProbeStepType::ActorClassCountAtMost, TEXT("Actors & World"), TEXT("Actor Class Count At Most"), TEXT("Verify no more than N actors belong to a class"), TargetField | CountField},
            {EProbeStepType::ActorLabelCountExactly, TEXT("Actors & World"), TEXT("Actor Label Count Exactly"), TEXT("Count actors with an exact World Outliner label"), TargetField | CountField},
            {EProbeStepType::ActorIsSelected, TEXT("Actors & World"), TEXT("Actor Is Selected"), TEXT("Check an actor found by its Outliner label is selected"), TargetField},
            {EProbeStepType::ActorIsNotSelected, TEXT("Actors & World"), TEXT("Actor Is Not Selected"), TEXT("Check an actor found by its Outliner label is unselected"), TargetField},
            {EProbeStepType::SelectedActorCountAtMost, TEXT("Selection"), TEXT("Selected Actor Count At Most"), TEXT("Verify at most N level actors are selected"), CountField},
            {EProbeStepType::SelectedActorHasComponent, TEXT("Selection"), TEXT("Selected Actor Has Component"), TEXT("Check the single selected actor has a component of a specified class"), ClassField},
            {EProbeStepType::PluginIsEnginePlugin, TEXT("Plugins & Modules"), TEXT("Plugin Is Engine Plugin"), TEXT("Verify plugin descriptor reports an engine-installed plugin"), TargetField},
            {EProbeStepType::PluginIsProjectPlugin, TEXT("Plugins & Modules"), TEXT("Plugin Is Project Plugin"), TEXT("Verify a plugin is installed in the project, not Engine"), TargetField},
            {EProbeStepType::PluginModuleCountAtLeast, TEXT("Plugins & Modules"), TEXT("Plugin Module Count At Least"), TEXT("Minimum modules declared by a plugin"), TargetField | CountField},
            {EProbeStepType::PluginCategoryEquals, TEXT("Plugins & Modules"), TEXT("Plugin Category Equals"), TEXT("Compare plugin descriptor category"), TargetField | ValueField},
            {EProbeStepType::LoadedModuleCountAtLeast, TEXT("Plugins & Modules"), TEXT("Loaded Module Count At Least"), TEXT("Count loaded modules in the Editor"), CountField},
            {EProbeStepType::ToolMenuSectionExists, TEXT("Editor UI"), TEXT("Tool Menu Section Exists"), TEXT("Check an Unreal ToolMenus section exists"), TargetField | ValueField},
            {EProbeStepType::ToolMenuSectionNotExists, TEXT("Editor UI"), TEXT("Tool Menu Section Does Not Exist"), TEXT("Check an Unreal ToolMenus section is absent"), TargetField | ValueField},
            {EProbeStepType::SelectedActorsEmpty, TEXT("Selection"), TEXT("Selected Actors Empty"), TEXT("Require zero selected actors"), 0},
            {EProbeStepType::SelectedActorsNotEmpty, TEXT("Selection"), TEXT("Selected Actors Not Empty"), TEXT("Require at least one selected actor"), 0},
            {EProbeStepType::AssetIsRedirector, TEXT("Assets"), TEXT("Asset Is Redirector"), TEXT("Asset class is UObjectRedirector"), TargetField},
            {EProbeStepType::AssetIsNotRedirector, TEXT("Assets"), TEXT("Asset Is Not Redirector"), TEXT("Asset class is not UObjectRedirector"), TargetField},
            {EProbeStepType::AssetRegistryTagNotExists, TEXT("Assets"), TEXT("Asset Registry Tag Does Not Exist"), TEXT("Verify a named registry tag is absent"), TargetField | PropertyField},
            {EProbeStepType::ClassIsInterface, TEXT("Classes & Reflection"), TEXT("Class Is Interface"), TEXT("Class has the Unreal interface flag"), TargetField},
            {EProbeStepType::ClassIsNotAbstract, TEXT("Classes & Reflection"), TEXT("Class Is Not Abstract"), TEXT("Class is concrete"), TargetField},
            {EProbeStepType::ClassPropertyBlueprintVisible, TEXT("Classes & Reflection"), TEXT("Class Property Blueprint Visible"), TEXT("Check reflected property has BlueprintVisible/ReadOnly flags"), TargetField | PropertyField},
            {EProbeStepType::ClassPropertyEditable, TEXT("Classes & Reflection"), TEXT("Class Property Is Editable"), TEXT("Check reflected property is EditAnywhere/EditDefaultsOnly/etc"), TargetField | PropertyField},
            {EProbeStepType::ClassFunctionIsPure, TEXT("Classes & Reflection"), TEXT("Class Function Is Pure"), TEXT("Check UFUNCTION is BlueprintPure"), TargetField | PropertyField},
            {EProbeStepType::ClassFunctionIsStatic, TEXT("Classes & Reflection"), TEXT("Class Function Is Static"), TEXT("Check UFUNCTION is static"), TargetField | PropertyField},
            {EProbeStepType::ClassFunctionHasParameter, TEXT("Classes & Reflection"), TEXT("Class Function Has Parameter"), TEXT("Check a reflected function contains a named parameter"), TargetField | PropertyField | ValueField},
            {EProbeStepType::ActorLabelContains, TEXT("Actors & World"), TEXT("Actor Label Contains"), TEXT("Find actor label containing text"), TargetField},
            {EProbeStepType::ActorCountWithTagAtLeast, TEXT("Actors & World"), TEXT("Actor Count With Tag At Least"), TEXT("Count level actors with a tag"), TargetField | CountField},
            {EProbeStepType::ActorHasTagByLabel, TEXT("Actors & World"), TEXT("Actor Has Tag By Label"), TEXT("Actor identified by label has a tag"), TargetField | ValueField},
            {EProbeStepType::ActorDoesNotHaveTagByLabel, TEXT("Actors & World"), TEXT("Actor Does Not Have Tag By Label"), TEXT("Actor identified by label lacks a tag"), TargetField | ValueField},
            {EProbeStepType::SelectedActorIsNotClass, TEXT("Selection"), TEXT("Selected Actor Is Not Class"), TEXT("Selected actor is not derived from specified class"), TargetField},
            {EProbeStepType::SelectedActorHasNoTag, TEXT("Selection"), TEXT("Selected Actor Does Not Have Tag"), TEXT("Selected actor does not have a tag"), TargetField},
            {EProbeStepType::SelectedActorLabelContains, TEXT("Selection"), TEXT("Selected Actor Label Contains"), TEXT("Selected actor label contains text"), TargetField},
            {EProbeStepType::ObjectPropertyStringContains, TEXT("Properties & Values"), TEXT("Object Property String Contains"), TEXT("String property contains a substring"), TargetField | PropertyField | ValueField},
            {EProbeStepType::ObjectPropertyStringStartsWith, TEXT("Properties & Values"), TEXT("Object Property String Starts With"), TEXT("String property starts with expected text"), TargetField | PropertyField | ValueField},
            {EProbeStepType::ObjectPropertyStringEndsWith, TEXT("Properties & Values"), TEXT("Object Property String Ends With"), TEXT("String property ends with expected text"), TargetField | PropertyField | ValueField},
            {EProbeStepType::ObjectPropertyIsTrue, TEXT("Properties & Values"), TEXT("Object Property Is True"), TEXT("Boolean property is true"), TargetField | PropertyField},
            {EProbeStepType::ObjectPropertyIsFalse, TEXT("Properties & Values"), TEXT("Object Property Is False"), TEXT("Boolean property is false"), TargetField | PropertyField},
            {EProbeStepType::PluginModuleCountExactly, TEXT("Plugins & Modules"), TEXT("Plugin Module Count Exactly"), TEXT("Require an exact number of declared plugin modules"), TargetField | CountField},
            {EProbeStepType::PluginModuleCountAtMost, TEXT("Plugins & Modules"), TEXT("Plugin Module Count At Most"), TEXT("Verify an upper bound on declared plugin modules"), TargetField | CountField},
            {EProbeStepType::PluginDescriptionContains, TEXT("Plugins & Modules"), TEXT("Plugin Description Contains"), TEXT("Check descriptive text published by a plugin"), TargetField | ValueField},
            {EProbeStepType::PluginAuthorEquals, TEXT("Plugins & Modules"), TEXT("Plugin Author Equals"), TEXT("Check the CreatedBy field in the plugin descriptor"), TargetField | ValueField},
            {EProbeStepType::LoadedModuleCountAtMost, TEXT("Plugins & Modules"), TEXT("Loaded Module Count At Most"), TEXT("Upper bound for active modules; environment dependent"), CountField},
            {EProbeStepType::AssetCountAtMost, TEXT("Assets"), TEXT("Asset Count In Folder At Most"), TEXT("Upper bound for assets in a Content folder recursively"), TargetField | CountField},
            {EProbeStepType::AssetNameContains, TEXT("Assets"), TEXT("Asset Name Contains"), TEXT("Asset Registry object name contains expected substring"), TargetField | ValueField},
            {EProbeStepType::AssetNameStartsWith, TEXT("Assets"), TEXT("Asset Name Starts With"), TEXT("Asset Registry object name begins with expected text"), TargetField | ValueField},
            {EProbeStepType::AssetNameEndsWith, TEXT("Assets"), TEXT("Asset Name Ends With"), TEXT("Asset Registry object name ends with expected text"), TargetField | ValueField},
            {EProbeStepType::AssetRegistryTagValueContains, TEXT("Assets"), TEXT("Asset Registry Tag Value Contains"), TEXT("Check whether a searchable Asset Registry tag contains expected text"), TargetField | PropertyField | ValueField},
            {EProbeStepType::ClassPropertyIsBoolean, TEXT("Classes & Reflection"), TEXT("Class Property Is Boolean"), TEXT("Check a reflected property uses FBoolProperty"), TargetField | PropertyField},
            {EProbeStepType::ClassPropertyIsNumeric, TEXT("Classes & Reflection"), TEXT("Class Property Is Numeric"), TEXT("Check a reflected property uses FNumericProperty"), TargetField | PropertyField},
            {EProbeStepType::ClassFunctionHasReturnValue, TEXT("Classes & Reflection"), TEXT("Class Function Has Return Value"), TEXT("Check a UFUNCTION has a reflected return parameter"), TargetField | PropertyField},
            {EProbeStepType::ActorLabelStartsWith, TEXT("Actors & World"), TEXT("Actor Label Starts With"), TEXT("Find an Outliner actor whose label starts with text"), TargetField},
            {EProbeStepType::ActorLabelEndsWith, TEXT("Actors & World"), TEXT("Actor Label Ends With"), TEXT("Find an Outliner actor whose label ends with text"), TargetField},
            {EProbeStepType::ActorLabelCountAtLeast, TEXT("Actors & World"), TEXT("Actor Label Count At Least"), TEXT("Count actors with an exact Outliner label"), TargetField | CountField},
            {EProbeStepType::ActorCountWithTagExactly, TEXT("Actors & World"), TEXT("Actor Count With Tag Exactly"), TEXT("Count actors with a tag and require an exact number"), TargetField | CountField},
            {EProbeStepType::ActorFolderCountAtLeast, TEXT("Actors & World"), TEXT("Actor Folder Count At Least"), TEXT("Minimum number of actors in the specified Outliner folder"), TargetField | CountField},
            {EProbeStepType::SelectedActorLabelStartsWith, TEXT("Selection"), TEXT("Selected Actor Label Starts With"), TEXT("Require exactly one selected actor whose label begins with text"), TargetField},
            {EProbeStepType::SelectedActorLabelEndsWith, TEXT("Selection"), TEXT("Selected Actor Label Ends With"), TEXT("Require exactly one selected actor whose label ends with text"), TargetField},
            {EProbeStepType::SelectedActorFolderEquals, TEXT("Selection"), TEXT("Selected Actor Folder Equals"), TEXT("Compare folder path for the one selected actor"), TargetField},
            {EProbeStepType::ObjectPropertyStringNotContains, TEXT("Properties & Values"), TEXT("Object Property String Does Not Contain"), TEXT("Verify an FString does not contain expected text"), TargetField | PropertyField | ValueField},
            {EProbeStepType::ObjectPropertyStringEqualsIgnoreCase, TEXT("Properties & Values"), TEXT("Object Property String Equals Ignore Case"), TEXT("Compare an FString case-insensitively"), TargetField | PropertyField | ValueField},
            {EProbeStepType::ObjectPropertyNumericInRange, TEXT("Properties & Values"), TEXT("Object Property Numeric In Range"), TEXT("Inclusive numerical range written min,max; validation checks both limits"), TargetField | PropertyField | ValueField},
            {EProbeStepType::ObjectPropertyNumericNearlyEquals, TEXT("Properties & Values"), TEXT("Object Property Numeric Nearly Equals"), TEXT("Compare numeric property to value within tolerance"), TargetField | PropertyField | ValueField | ToleranceField},
            {EProbeStepType::FixtureCreateObject, TEXT("Test Actions"), TEXT("Create Temporary Object"), TEXT("Create a transient UObject fixture. Target is its class path; New Fixture ID is the Expected Value. Never writes to the project."), TargetField | ValueField},
            {EProbeStepType::FixtureSetProperty, TEXT("Test Actions"), TEXT("Set Temporary Object Property"), TEXT("Change a primitive reflected property on a fixture created earlier in the same test. Target is Fixture ID."), TargetField | PropertyField | ValueField},
            {EProbeStepType::FixturePropertyEquals, TEXT("Test Actions"), TEXT("Temporary Object Property Equals"), TEXT("Check a primitive property on the named temporary object. No project assets are loaded or modified."), TargetField | PropertyField | ValueField | ToleranceField},
            {EProbeStepType::FixturePropertyNotEquals, TEXT("Test Actions"), TEXT("Temporary Object Property Does Not Equal"), TEXT("Check a temporary object's property differs from the supplied value."), TargetField | PropertyField | ValueField | ToleranceField},
            {EProbeStepType::FixtureObjectExists, TEXT("Test Actions"), TEXT("Temporary Object Exists"), TEXT("Verify a previously created fixture ID is still present."), TargetField},
            {EProbeStepType::FixtureDestroyObject, TEXT("Test Actions"), TEXT("Destroy Temporary Object"), TEXT("Release a temporary fixture; remaining fixtures are always cleaned up at the end of the run."), TargetField},
            {EProbeStepType::PluginVersionContains, TEXT("Plugins & Modules"), TEXT("Plugin Version Contains"), TEXT("Check that the plugin descriptor VersionName includes a stable substring, useful across patch releases"), TargetField | ValueField},
            {EProbeStepType::PluginFriendlyNameContains, TEXT("Plugins & Modules"), TEXT("Plugin Friendly Name Contains"), TEXT("Check a substring in the human-readable FriendlyName from the plugin descriptor"), TargetField | ValueField},
            {EProbeStepType::PluginHasNoContent, TEXT("Plugins & Modules"), TEXT("Plugin Has No Content"), TEXT("Verify CanContainContent is disabled in the plugin descriptor"), TargetField},
            {EProbeStepType::LoadedModuleCountExactly, TEXT("Plugins & Modules"), TEXT("Loaded Module Count Exactly"), TEXT("Require an exact number of loaded modules. Environment dependent; configure deliberately"), CountField},
            {EProbeStepType::AssetCountDirectAtLeast, TEXT("Assets"), TEXT("Asset Count In Folder (Direct) At Least"), TEXT("Count only assets directly in a folder, excluding descendant folders, and require a minimum"), TargetField | CountField},
            {EProbeStepType::AssetCountDirectExactly, TEXT("Assets"), TEXT("Asset Count In Folder (Direct) Exactly"), TEXT("Count only assets directly in a folder, excluding descendant folders, and require an exact total"), TargetField | CountField},
            {EProbeStepType::AssetPackagePathEquals, TEXT("Assets"), TEXT("Asset Package Path Equals"), TEXT("Compare the Asset Registry package folder of an existing asset to an expected /Game path"), TargetField | ValueField},
            {EProbeStepType::AssetClassNameEquals, TEXT("Assets"), TEXT("Asset Class Name Equals"), TEXT("Compare the Asset Registry class name of an existing asset without loading the asset"), TargetField | ValueField},
            {EProbeStepType::ClassPropertyIsArray, TEXT("Classes & Reflection"), TEXT("Class Property Is Array"), TEXT("Verify a reflected class property is an Unreal array (FArrayProperty)"), TargetField | PropertyField},
            {EProbeStepType::ClassPropertyIsObjectReference, TEXT("Classes & Reflection"), TEXT("Class Property Is Object Reference"), TEXT("Verify a reflected property references a UObject (FObjectPropertyBase)"), TargetField | PropertyField},
            {EProbeStepType::ClassPropertyIsStruct, TEXT("Classes & Reflection"), TEXT("Class Property Is Struct"), TEXT("Verify a reflected property stores an Unreal struct (FStructProperty)"), TargetField | PropertyField},
            {EProbeStepType::ClassPropertyIsEnum, TEXT("Classes & Reflection"), TEXT("Class Property Is Enum"), TEXT("Verify a reflected property is an enum or enum-backed byte property"), TargetField | PropertyField},
            {EProbeStepType::ClassPropertyIsName, TEXT("Classes & Reflection"), TEXT("Class Property Is Name"), TEXT("Verify a reflected property has FName type"), TargetField | PropertyField},
            {EProbeStepType::ClassPropertyIsString, TEXT("Classes & Reflection"), TEXT("Class Property Is String"), TEXT("Verify a reflected property is an FString"), TargetField | PropertyField},
            {EProbeStepType::ClassPropertyIsText, TEXT("Classes & Reflection"), TEXT("Class Property Is Text"), TEXT("Verify a reflected property is localized FText"), TargetField | PropertyField},
            {EProbeStepType::ClassFunctionIsConst, TEXT("Classes & Reflection"), TEXT("Class Function Is Const"), TEXT("Verify a reflected UFUNCTION has the Const flag"), TargetField | PropertyField},
            {EProbeStepType::ClassFunctionIsPublic, TEXT("Classes & Reflection"), TEXT("Class Function Is Public"), TEXT("Verify a reflected UFUNCTION has the Public flag, including K2 aliases"), TargetField | PropertyField},
            {EProbeStepType::ClassFunctionIsDeprecated, TEXT("Classes & Reflection"), TEXT("Class Function Is Deprecated"), TEXT("Verify a reflected UFUNCTION is marked Deprecated"), TargetField | PropertyField},
            {EProbeStepType::ClassHasMetadata, TEXT("Classes & Reflection"), TEXT("Class Has Metadata"), TEXT("Check that a loaded reflected class exposes a named metadata key"), TargetField | PropertyField},
            {EProbeStepType::ActorLabelCountAtMost, TEXT("Actors & World"), TEXT("Actor Label Count At Most"), TEXT("Count actors with the exact Outliner label and enforce an upper bound"), TargetField | CountField},
            {EProbeStepType::ActorCountWithTagAtMost, TEXT("Actors & World"), TEXT("Actor Count With Tag At Most"), TEXT("Count actors with a matching Actor Tag and enforce an upper bound"), TargetField | CountField},
            {EProbeStepType::ActorFolderCountExactly, TEXT("Actors & World"), TEXT("Actor Folder Count Exactly"), TEXT("Count actors whose Outliner folder path matches exactly, requiring an exact total"), TargetField | CountField},
            {EProbeStepType::ActorFolderCountAtMost, TEXT("Actors & World"), TEXT("Actor Folder Count At Most"), TEXT("Count actors whose Outliner folder path matches exactly, requiring an upper bound"), TargetField | CountField},
            {EProbeStepType::SelectedActorIsTemporarilyHidden, TEXT("Selection"), TEXT("Selected Actor Is Temporarily Hidden"), TEXT("Require one selected actor and verify its temporary Editor visibility is hidden"), 0},
            {EProbeStepType::SelectedActorIsNotTemporarilyHidden, TEXT("Selection"), TEXT("Selected Actor Is Not Temporarily Hidden"), TEXT("Require one selected actor and verify it is not temporarily hidden in the Editor"), 0},
            {EProbeStepType::SelectionClear, TEXT("Test Actions"), TEXT("Clear Editor Actor Selection"), TEXT("Clear actor selection temporarily. Probe restores your prior selection after the test, even on failure."), 0},
            {EProbeStepType::SelectionSelectFirstActor, TEXT("Test Actions"), TEXT("Select First Available Actor"), TEXT("Select a deterministic first non-WorldSettings actor. Existing actor state and assets are not modified; selection is restored automatically."), 0},
            {EProbeStepType::SelectionSelectActorByLabel, TEXT("Test Actions"), TEXT("Select Actor By Label"), TEXT("Select exactly one level actor matching its case-sensitive Outliner label; errors when missing or ambiguous. Restored after this test."), TargetField},
            {EProbeStepType::SelectionSelectActorsWithTag, TEXT("Test Actions"), TEXT("Select Actors With Tag"), TEXT("Select all level actors carrying the specified tag. Errors if none match; prior selection is restored."), TargetField},
            {EProbeStepType::SelectionRestore, TEXT("Test Actions"), TEXT("Restore Original Actor Selection"), TEXT("Restore the Editor selection captured before this test's first selection action. Automatic cleanup also restores it."), 0},
            {EProbeStepType::ActorFixtureSpawn, TEXT("Test Actions"), TEXT("Spawn Temporary Editor Actor"), TEXT("Spawn a transient actor in the active Editor world. Target is a concrete actor class; New Actor ID is Expected Value. Requires explicit per-test permission. May dirty the active level."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureSetLabel, TEXT("Test Actions"), TEXT("Set Temporary Actor Label"), TEXT("Set the Outliner label on an actor created by this test. Target is Actor ID; Value is label."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureSetFolder, TEXT("Test Actions"), TEXT("Set Temporary Actor Folder"), TEXT("Move a temporary actor into an Outliner folder. Target is Actor ID; Value is folder path."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureAddTag, TEXT("Test Actions"), TEXT("Add Temporary Actor Tag"), TEXT("Add an Actor Tag to a temporary actor. Target is Actor ID; Value is tag."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureRemoveTag, TEXT("Test Actions"), TEXT("Remove Temporary Actor Tag"), TEXT("Remove an Actor Tag from a temporary actor. Target is Actor ID; Value is tag."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureSelect, TEXT("Test Actions"), TEXT("Select Temporary Actor"), TEXT("Select only this test's temporary actor, automatically restoring prior Editor selection after the run."), TargetField},
            {EProbeStepType::ActorFixtureHide, TEXT("Test Actions"), TEXT("Hide Temporary Actor In Editor"), TEXT("Temporarily hide a test-owned actor in the Editor, without changing production actors."), TargetField},
            {EProbeStepType::ActorFixtureShow, TEXT("Test Actions"), TEXT("Show Temporary Actor In Editor"), TEXT("Remove temporary Editor hiding on a test-owned actor."), TargetField},
            {EProbeStepType::ActorFixtureExists, TEXT("Test Actions"), TEXT("Temporary Actor Exists"), TEXT("Check whether a temporary actor with this Actor ID exists in this test."), TargetField},
            {EProbeStepType::ActorFixtureLabelEquals, TEXT("Test Actions"), TEXT("Temporary Actor Label Equals"), TEXT("Verify the exact Outliner label of a test-owned temporary actor."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureHasTag, TEXT("Test Actions"), TEXT("Temporary Actor Has Tag"), TEXT("Verify a tag on a test-owned temporary actor."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureFolderEquals, TEXT("Test Actions"), TEXT("Temporary Actor Folder Equals"), TEXT("Verify the Outliner folder path of a test-owned temporary actor."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureIsSelected, TEXT("Test Actions"), TEXT("Temporary Actor Is Selected"), TEXT("Verify the test-owned temporary actor is currently selected."), TargetField},
            {EProbeStepType::ActorFixtureIsHidden, TEXT("Test Actions"), TEXT("Temporary Actor Is Hidden"), TEXT("Verify the test-owned temporary actor is temporarily hidden in the Editor."), TargetField},
            {EProbeStepType::ActorFixtureDestroy, TEXT("Test Actions"), TEXT("Destroy Temporary Editor Actor"), TEXT("Destroy a test-owned temporary actor. Undestroyed fixtures are cleaned up after the test."), TargetField},
            // Probe 0.13.0: transform and attachment workflows never target production actors.
            {EProbeStepType::ActorFixtureSetLocation, TEXT("Test Actions"), TEXT("Set Temporary Actor Location"), TEXT("Move a test-owned actor. Expected Value: X,Y,Z (centimeters). Requires an actor class with a root component."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureLocationNearlyEquals, TEXT("Test Actions"), TEXT("Temporary Actor Location Nearly Equals"), TEXT("Compare actor world location with X,Y,Z, using Numeric Tolerance (centimeters)."), TargetField | ValueField | ToleranceField},
            {EProbeStepType::ActorFixtureSetRotation, TEXT("Test Actions"), TEXT("Set Temporary Actor Rotation"), TEXT("Rotate a test-owned actor. Expected Value: Pitch,Yaw,Roll (degrees)."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureRotationNearlyEquals, TEXT("Test Actions"), TEXT("Temporary Actor Rotation Nearly Equals"), TEXT("Compare world rotation Pitch,Yaw,Roll within Numeric Tolerance (degrees)."), TargetField | ValueField | ToleranceField},
            {EProbeStepType::ActorFixtureSetScale, TEXT("Test Actions"), TEXT("Set Temporary Actor Scale"), TEXT("Set world scale using X,Y,Z on a test-owned actor."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureScaleNearlyEquals, TEXT("Test Actions"), TEXT("Temporary Actor Scale Nearly Equals"), TEXT("Compare world scale X,Y,Z using Numeric Tolerance."), TargetField | ValueField | ToleranceField},
            {EProbeStepType::ActorFixtureAttachTo, TEXT("Test Actions"), TEXT("Attach Temporary Actor To Temporary Parent"), TEXT("Target is the child Actor ID; Expected Value is the parent Actor ID. Both must be test-owned."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureDetach, TEXT("Test Actions"), TEXT("Detach Temporary Actor"), TEXT("Detach test-owned child from its temporary parent while keeping world transform."), TargetField},
            {EProbeStepType::ActorFixtureHasParent, TEXT("Test Actions"), TEXT("Temporary Actor Has Parent"), TEXT("Verify that the test-owned child is attached to the test-owned Parent Actor ID in Expected Value."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureHasNoParent, TEXT("Test Actions"), TEXT("Temporary Actor Has No Parent"), TEXT("Verify a test-owned actor has no attach parent."), TargetField},
            {EProbeStepType::ActorFixtureChildCountExactly, TEXT("Test Actions"), TEXT("Temporary Actor Child Count Exactly"), TEXT("Count directly attached test-owned child actors; compare to Exact Count."), TargetField | CountField},
            {EProbeStepType::ActorFixtureTagAbsent, TEXT("Test Actions"), TEXT("Temporary Actor Does Not Have Tag"), TEXT("Verify a tag was removed from a test-owned actor."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureIsVisible, TEXT("Test Actions"), TEXT("Temporary Actor Is Visible"), TEXT("Assert this test-owned actor is not temporarily hidden in the Editor."), TargetField},
            {EProbeStepType::ActorFixtureIsNotSelected, TEXT("Test Actions"), TEXT("Temporary Actor Is Not Selected"), TEXT("Verify this test-owned actor is not currently selected."), TargetField},
            {EProbeStepType::ActorFixtureFolderNotEquals, TEXT("Test Actions"), TEXT("Temporary Actor Folder Does Not Equal"), TEXT("Assert a test-owned actor is not in a specified World Outliner folder."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureDistanceAtMost, TEXT("Test Actions"), TEXT("Temporary Actors Distance At Most"), TEXT("Compare distance to the second temporary Actor ID in Expected Value with Maximum Distance (cm) in Count."), TargetField | ValueField | CountField},
            {EProbeStepType::ActorFixtureChildCountAtLeast, TEXT("Test Actions"), TEXT("Temporary Actor Child Count At Least"), TEXT("Minimum direct test-owned child actors attached to this fixture."), TargetField | CountField},
            {EProbeStepType::ActorFixtureChildCountAtMost, TEXT("Test Actions"), TEXT("Temporary Actor Child Count At Most"), TEXT("Maximum direct test-owned child actors attached to this fixture."), TargetField | CountField},
            {EProbeStepType::ActorFixtureDistanceAtLeast, TEXT("Test Actions"), TEXT("Temporary Actors Distance At Least"), TEXT("Minimum distance between two run-owned actors, in centimeters."), TargetField | ValueField | CountField},
            {EProbeStepType::ActorFixtureLabelContains, TEXT("Test Actions"), TEXT("Temporary Actor Label Contains"), TEXT("Check that the temporary actor's Outliner label contains this substring."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureLabelStartsWith, TEXT("Test Actions"), TEXT("Temporary Actor Label Starts With"), TEXT("Check that a temporary actor's Outliner label begins with this text."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureLabelEndsWith, TEXT("Test Actions"), TEXT("Temporary Actor Label Ends With"), TEXT("Check that a temporary actor's Outliner label ends with this text."), TargetField | ValueField},
            {EProbeStepType::ActorFixtureTagCountExactly, TEXT("Test Actions"), TEXT("Temporary Actor Tag Count Exactly"), TEXT("Count the tags on a test-owned actor."), TargetField | CountField},
            {EProbeStepType::ActorFixtureHasRootComponent, TEXT("Test Actions"), TEXT("Temporary Actor Has Root Component"), TEXT("Verify that the run-owned actor has a root SceneComponent."), TargetField},
            {EProbeStepType::ActorFixtureLocationNotNearlyEquals, TEXT("Test Actions"), TEXT("Temporary Actor Location Not Nearly Equals"), TEXT("Check world location is outside numeric tolerance."), TargetField | ValueField | ToleranceField},
            {EProbeStepType::ActorFixtureRotationNotNearlyEquals, TEXT("Test Actions"), TEXT("Temporary Actor Rotation Not Nearly Equals"), TEXT("Check Euler rotation is outside numeric tolerance."), TargetField | ValueField | ToleranceField},
            {EProbeStepType::ActorFixtureScaleNotNearlyEquals, TEXT("Test Actions"), TEXT("Temporary Actor Scale Not Nearly Equals"), TEXT("Check actor scale is outside numeric tolerance."), TargetField | ValueField | ToleranceField},
        };
        return Catalog;
    }

    inline const FAssertionInfo& GetAssertionInfo(EProbeStepType Type)
    {
        for (const FAssertionInfo& Item : AssertionCatalog())
        {
            if (Item.Type == Type) { return Item; }
        }
        return AssertionCatalog()[0];
    }

    // Shared classification for built-in and extension entries in Add Step,
    // Change Assertion, and Templates. No serialized data depends on these labels.
    inline FString MenuSubgroup(const FString& Category, const FString& Name)
    {
        if (Category == TEXT("Test Actions"))
        {
            if (Name.Contains(TEXT("Object"))) { return TEXT("Temporary Objects"); }
            if (Name.Contains(TEXT("Selection")) || (Name.Contains(TEXT("Select")) && !Name.Contains(TEXT("Temporary Actor")))) { return TEXT("Editor Selection"); }
            if (Name.Contains(TEXT("Location")) || Name.Contains(TEXT("Rotation")) || Name.Contains(TEXT("Scale")) || Name.Contains(TEXT("Distance")) || Name.Contains(TEXT("Transform")) || Name.Contains(TEXT("Spatial"))) { return TEXT("Actor Transforms & Distance"); }
            if (Name.Contains(TEXT("Parent")) || Name.Contains(TEXT("Child")) || Name.Contains(TEXT("Attach")) || Name.Contains(TEXT("Detach")) || Name.Contains(TEXT("Hierarchy")) || Name.Contains(TEXT("Reparent"))) { return TEXT("Actor Hierarchy"); }
            if (Name.Contains(TEXT("Spawn")) || Name.Contains(TEXT("Destroy")) || Name.Contains(TEXT("Lifecycle")) || Name.Contains(TEXT("Exists")) || Name.Contains(TEXT("Cleanup"))) { return TEXT("Actor Lifecycle"); }
            return TEXT("Actor State & Properties");
        }
        if (Category == TEXT("Plugins & Modules")) { return Name.Contains(TEXT("Module")) ? TEXT("Modules") : TEXT("Plugins"); }
        if (Category == TEXT("Editor UI")) { return Name.Contains(TEXT("Tab")) ? TEXT("Tabs") : TEXT("Menus & Commands"); }
        if (Category == TEXT("Editor State")) { return Name.Contains(TEXT("Console")) ? TEXT("Console Variables") : TEXT("Editor Lifecycle"); }
        if (Category == TEXT("Assets"))
        {
            if (Name.Contains(TEXT("Registry")) || Name.Contains(TEXT("Tag"))) { return TEXT("Registry & Tags"); }
            if (Name.Contains(TEXT("Count")) || Name.Contains(TEXT("Assets In"))) { return TEXT("Asset Counts"); }
            if (Name.Contains(TEXT("Name")) || Name.Contains(TEXT("Folder")) || Name.Contains(TEXT("Path"))) { return TEXT("Naming & Folders"); }
            return TEXT("Existence, Classes & State");
        }
        if (Category == TEXT("Classes & Reflection"))
        {
            if (Name.Contains(TEXT("Function"))) { return TEXT("Functions"); }
            if (Name.Contains(TEXT("Property"))) { return TEXT("Properties"); }
            return TEXT("Class Contracts");
        }
        if (Category == TEXT("Actors & World"))
        {
            if (Name.Contains(TEXT("Label"))) { return TEXT("Outliner Labels"); }
            if (Name.Contains(TEXT("Folder"))) { return TEXT("Outliner Folders"); }
            if (Name.Contains(TEXT("Tag"))) { return TEXT("Actor Tags"); }
            if (Name.Contains(TEXT("Count"))) { return TEXT("Actor Counts"); }
            return TEXT("Actor State & Components");
        }
        if (Category == TEXT("Selection"))
        {
            if (Name.Contains(TEXT("Count")) || Name.Contains(TEXT("Empty"))) { return TEXT("Selection Counts"); }
            if (Name.Contains(TEXT("Label")) || Name.Contains(TEXT("Tag")) || Name.Contains(TEXT("Folder"))) { return TEXT("Selected Actor Details"); }
            return TEXT("Selection State & Types");
        }
        if (Category == TEXT("Properties & Values"))
        {
            if (Name.Contains(TEXT("Numeric")) || Name.Contains(TEXT("Threshold")) || Name.Contains(TEXT("Range"))) { return TEXT("Numeric Comparisons"); }
            if (Name.Contains(TEXT("String"))) { return TEXT("String Comparisons"); }
            if (Name.Contains(TEXT("True")) || Name.Contains(TEXT("False")) || Name.Contains(TEXT("Boolean"))) { return TEXT("Boolean Checks"); }
            return TEXT("Value Comparisons");
        }
        return TEXT("General");
    }

    inline const TArray<FString>& AssertionCategories()
    {
        static const TArray<FString> Categories = {TEXT("Plugins & Modules"), TEXT("Editor UI"), TEXT("Editor State"), TEXT("Assets"), TEXT("Classes & Reflection"), TEXT("Actors & World"), TEXT("Selection"), TEXT("Properties & Values"), TEXT("Test Actions")};
        return Categories;
    }
}
