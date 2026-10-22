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

    inline const TArray<FString>& AssertionCategories()
    {
        static const TArray<FString> Categories = {TEXT("Plugins & Modules"), TEXT("Editor UI"), TEXT("Editor State"), TEXT("Assets"), TEXT("Classes & Reflection"), TEXT("Actors & World"), TEXT("Selection"), TEXT("Properties & Values")};
        return Categories;
    }
}
