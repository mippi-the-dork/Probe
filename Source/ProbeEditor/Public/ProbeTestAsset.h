#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ProbeTestAsset.generated.h"

UENUM(BlueprintType)
enum class EProbeStepType : uint8
{
    PluginEnabled UMETA(DisplayName="Plugin is enabled"),
    ClassExists UMETA(DisplayName="Class exists"),
    AssetExists UMETA(DisplayName="Asset exists"),
    EditorWorldExists UMETA(DisplayName="Editor world exists"),
    ActorCountAtLeast UMETA(DisplayName="Actor count at least"),
    ActorClassCountAtLeast UMETA(DisplayName="Actor class count at least"),
    ActorTagExists UMETA(DisplayName="Actor with tag exists"),
    AssetClassIs UMETA(DisplayName="Asset is class"),
    ObjectPropertyEquals UMETA(DisplayName="Object property equals"),
    // Append enum members to preserve serialized values of existing Probe Tests.
    ActorLabelExists UMETA(DisplayName="Actor with label exists"),
    SelectedActorCountAtLeast UMETA(DisplayName="Selected actor count at least"),
    // New types are appended only. Never reorder: serialized Probe Tests store enum values.
    PluginInstalled UMETA(DisplayName="Plugin is installed"),
    PluginDisabled UMETA(DisplayName="Plugin is disabled"),
    ModuleLoaded UMETA(DisplayName="Module is loaded"),
    ConsoleVariableExists UMETA(DisplayName="Console variable exists"),
    ConsoleVariableEquals UMETA(DisplayName="Console variable equals"),
    EditorTabOpen UMETA(DisplayName="Editor tab is open"),
    ToolMenuRegistered UMETA(DisplayName="Tool menu is registered"),
    AssetCountAtLeast UMETA(DisplayName="Asset count in folder at least"),
    AssetIsDirty UMETA(DisplayName="Asset has unsaved changes"),
    AssetIsClean UMETA(DisplayName="Asset has no unsaved changes"),
    AssetIsLoaded UMETA(DisplayName="Asset is loaded"),
    AssetRegistryTagEquals UMETA(DisplayName="Asset registry tag equals"),
    ClassIsChildOf UMETA(DisplayName="Class derives from class"),
    ClassImplementsInterface UMETA(DisplayName="Class implements interface"),
    ClassHasProperty UMETA(DisplayName="Class has property"),
    ClassHasFunction UMETA(DisplayName="Class has function"),
    ObjectLoaded UMETA(DisplayName="Object is loaded"),
    ObjectPropertyExists UMETA(DisplayName="Object property exists"),
    ObjectPropertyNotEquals UMETA(DisplayName="Object property does not equal"),
    ActorLabelNotExists UMETA(DisplayName="Actor with label does not exist"),
    ActorTagNotExists UMETA(DisplayName="Actor with tag does not exist"),
    ActorHasComponent UMETA(DisplayName="Actor has component"),
    ActorFolderEquals UMETA(DisplayName="Actor folder equals"),
    ActorCountExactly UMETA(DisplayName="Actor count exactly"),
    ActorClassCountExactly UMETA(DisplayName="Actor class count exactly"),
    ActorPropertyEquals UMETA(DisplayName="Actor property equals"),
    SelectedActorCountExactly UMETA(DisplayName="Selected actor count exactly"),
    SelectedActorLabelEquals UMETA(DisplayName="Selected actor label equals"),
    SelectedActorIsClass UMETA(DisplayName="Selected actor is class"),
    SelectedActorHasTag UMETA(DisplayName="Selected actor has tag"),
    // 0.6.0+: ALWAYS append. Existing enum ordinals are saved in .uasset files.
    PluginFriendlyNameEquals UMETA(DisplayName="Plugin Friendly Name Equals"),
    PluginVersionEquals UMETA(DisplayName="Plugin Version Equals"),
    PluginDeclaresModule UMETA(DisplayName="Plugin Declares Module"),
    PluginHasContent UMETA(DisplayName="Plugin Has Content"),
    ModuleNotLoaded UMETA(DisplayName="Module Is Not Loaded"),
    ModuleExists UMETA(DisplayName="Module Exists"),
    EditorTabNotOpen UMETA(DisplayName="Editor Tab Is Not Open"),
    EditorTabRegistered UMETA(DisplayName="Editor Tab Is Registered"),
    ToolMenuNotRegistered UMETA(DisplayName="Tool Menu Is Not Registered"),
    ConsoleVariableNotExists UMETA(DisplayName="Console Variable Does Not Exist"),
    ConsoleVariableNotEquals UMETA(DisplayName="Console Variable Does Not Equal"),
    PIEIsRunning UMETA(DisplayName="Play In Editor Is Running"),
    PIEIsNotRunning UMETA(DisplayName="Play In Editor Is Not Running"),
    AssetNotExists UMETA(DisplayName="Asset Does Not Exist"),
    AssetCountExactly UMETA(DisplayName="Asset Count In Folder Exactly"),
    AssetRegistryTagExists UMETA(DisplayName="Asset Registry Tag Exists"),
    ClassNotExists UMETA(DisplayName="Class Does Not Exist"),
    ClassHasNoProperty UMETA(DisplayName="Class Does Not Have Property"),
    ClassHasNoFunction UMETA(DisplayName="Class Does Not Have Function"),
    ClassIsAbstract UMETA(DisplayName="Class Is Abstract"),
    ClassFunctionBlueprintCallable UMETA(DisplayName="Class Function Is Blueprint Callable"),
    ObjectNotLoaded UMETA(DisplayName="Object Is Not Loaded"),
    ObjectPropertyGreaterThan UMETA(DisplayName="Object Property Greater Than"),
    ObjectPropertyLessThan UMETA(DisplayName="Object Property Less Than"),
    ActorCountAtMost UMETA(DisplayName="Actor Count At Most"),
    ActorClassCountAtMost UMETA(DisplayName="Actor Class Count At Most"),
    ActorLabelCountExactly UMETA(DisplayName="Actor Label Count Exactly"),
    ActorIsSelected UMETA(DisplayName="Actor Is Selected"),
    ActorIsNotSelected UMETA(DisplayName="Actor Is Not Selected"),
    SelectedActorCountAtMost UMETA(DisplayName="Selected Actor Count At Most"),
    SelectedActorHasComponent UMETA(DisplayName="Selected Actor Has Component"),
    // 0.7.0: append only, serialized legacy enum values must remain stable.
    PluginIsEnginePlugin UMETA(DisplayName="Plugin Is Engine Plugin"),
    PluginIsProjectPlugin UMETA(DisplayName="Plugin Is Project Plugin"),
    PluginModuleCountAtLeast UMETA(DisplayName="Plugin Module Count At Least"),
    PluginCategoryEquals UMETA(DisplayName="Plugin Category Equals"),
    LoadedModuleCountAtLeast UMETA(DisplayName="Loaded Module Count At Least"),
    ToolMenuSectionExists UMETA(DisplayName="Tool Menu Section Exists"),
    ToolMenuSectionNotExists UMETA(DisplayName="Tool Menu Section Does Not Exist"),
    SelectedActorsEmpty UMETA(DisplayName="Selected Actors Empty"),
    SelectedActorsNotEmpty UMETA(DisplayName="Selected Actors Not Empty"),
    AssetIsRedirector UMETA(DisplayName="Asset Is Redirector"),
    AssetIsNotRedirector UMETA(DisplayName="Asset Is Not Redirector"),
    AssetRegistryTagNotExists UMETA(DisplayName="Asset Registry Tag Does Not Exist"),
    ClassIsInterface UMETA(DisplayName="Class Is Interface"),
    ClassIsNotAbstract UMETA(DisplayName="Class Is Not Abstract"),
    ClassPropertyBlueprintVisible UMETA(DisplayName="Class Property Blueprint Visible"),
    ClassPropertyEditable UMETA(DisplayName="Class Property Is Editable"),
    ClassFunctionIsPure UMETA(DisplayName="Class Function Is Pure"),
    ClassFunctionIsStatic UMETA(DisplayName="Class Function Is Static"),
    ClassFunctionHasParameter UMETA(DisplayName="Class Function Has Parameter"),
    ActorLabelContains UMETA(DisplayName="Actor Label Contains"),
    ActorCountWithTagAtLeast UMETA(DisplayName="Actor Count With Tag At Least"),
    ActorHasTagByLabel UMETA(DisplayName="Actor Has Tag By Label"),
    ActorDoesNotHaveTagByLabel UMETA(DisplayName="Actor Does Not Have Tag By Label"),
    SelectedActorIsNotClass UMETA(DisplayName="Selected Actor Is Not Class"),
    SelectedActorHasNoTag UMETA(DisplayName="Selected Actor Does Not Have Tag"),
    SelectedActorLabelContains UMETA(DisplayName="Selected Actor Label Contains"),
    ObjectPropertyStringContains UMETA(DisplayName="Object Property String Contains"),
    ObjectPropertyStringStartsWith UMETA(DisplayName="Object Property String Starts With"),
    ObjectPropertyStringEndsWith UMETA(DisplayName="Object Property String Ends With"),
    ObjectPropertyIsTrue UMETA(DisplayName="Object Property Is True"),
    ObjectPropertyIsFalse UMETA(DisplayName="Object Property Is False"),
    // 0.8.0+: append only; serialized enum values from 0.1-0.7 must not change.
    PluginModuleCountExactly UMETA(DisplayName="Plugin Module Count Exactly"),
    PluginModuleCountAtMost UMETA(DisplayName="Plugin Module Count At Most"),
    PluginDescriptionContains UMETA(DisplayName="Plugin Description Contains"),
    PluginAuthorEquals UMETA(DisplayName="Plugin Author Equals"),
    LoadedModuleCountAtMost UMETA(DisplayName="Loaded Module Count At Most"),
    AssetCountAtMost UMETA(DisplayName="Asset Count In Folder At Most"),
    AssetNameContains UMETA(DisplayName="Asset Name Contains"),
    AssetNameStartsWith UMETA(DisplayName="Asset Name Starts With"),
    AssetNameEndsWith UMETA(DisplayName="Asset Name Ends With"),
    AssetRegistryTagValueContains UMETA(DisplayName="Asset Registry Tag Value Contains"),
    ClassPropertyIsBoolean UMETA(DisplayName="Class Property Is Boolean"),
    ClassPropertyIsNumeric UMETA(DisplayName="Class Property Is Numeric"),
    ClassFunctionHasReturnValue UMETA(DisplayName="Class Function Has Return Value"),
    ActorLabelStartsWith UMETA(DisplayName="Actor Label Starts With"),
    ActorLabelEndsWith UMETA(DisplayName="Actor Label Ends With"),
    ActorLabelCountAtLeast UMETA(DisplayName="Actor Label Count At Least"),
    ActorCountWithTagExactly UMETA(DisplayName="Actor Count With Tag Exactly"),
    ActorFolderCountAtLeast UMETA(DisplayName="Actor Folder Count At Least"),
    SelectedActorLabelStartsWith UMETA(DisplayName="Selected Actor Label Starts With"),
    SelectedActorLabelEndsWith UMETA(DisplayName="Selected Actor Label Ends With"),
    SelectedActorFolderEquals UMETA(DisplayName="Selected Actor Folder Equals"),
    ObjectPropertyStringNotContains UMETA(DisplayName="Object Property String Does Not Contain"),
    ObjectPropertyStringEqualsIgnoreCase UMETA(DisplayName="Object Property String Equals Ignore Case"),
    ObjectPropertyNumericInRange UMETA(DisplayName="Object Property Numeric In Range"),
    ObjectPropertyNumericNearlyEquals UMETA(DisplayName="Object Property Numeric Nearly Equals"),
    // 0.9.0: transient fixture actions. Never modify saved project objects or levels.
    FixtureCreateObject UMETA(DisplayName="Create Temporary Object"),
    FixtureSetProperty UMETA(DisplayName="Set Temporary Object Property"),
    FixturePropertyEquals UMETA(DisplayName="Temporary Object Property Equals"),
    FixturePropertyNotEquals UMETA(DisplayName="Temporary Object Property Does Not Equal"),
    FixtureObjectExists UMETA(DisplayName="Temporary Object Exists"),
    FixtureDestroyObject UMETA(DisplayName="Destroy Temporary Object"),
    // 0.10.0: append only to preserve every previously saved enum ordinal.
    PluginVersionContains UMETA(DisplayName="Plugin Version Contains"),
    PluginFriendlyNameContains UMETA(DisplayName="Plugin Friendly Name Contains"),
    PluginHasNoContent UMETA(DisplayName="Plugin Has No Content"),
    LoadedModuleCountExactly UMETA(DisplayName="Loaded Module Count Exactly"),
    AssetCountDirectAtLeast UMETA(DisplayName="Asset Count In Folder (Direct) At Least"),
    AssetCountDirectExactly UMETA(DisplayName="Asset Count In Folder (Direct) Exactly"),
    AssetPackagePathEquals UMETA(DisplayName="Asset Package Path Equals"),
    AssetClassNameEquals UMETA(DisplayName="Asset Class Name Equals"),
    ClassPropertyIsArray UMETA(DisplayName="Class Property Is Array"),
    ClassPropertyIsObjectReference UMETA(DisplayName="Class Property Is Object Reference"),
    ClassPropertyIsStruct UMETA(DisplayName="Class Property Is Struct"),
    ClassPropertyIsEnum UMETA(DisplayName="Class Property Is Enum"),
    ClassPropertyIsName UMETA(DisplayName="Class Property Is Name"),
    ClassPropertyIsString UMETA(DisplayName="Class Property Is String"),
    ClassPropertyIsText UMETA(DisplayName="Class Property Is Text"),
    ClassFunctionIsConst UMETA(DisplayName="Class Function Is Const"),
    ClassFunctionIsPublic UMETA(DisplayName="Class Function Is Public"),
    ClassFunctionIsDeprecated UMETA(DisplayName="Class Function Is Deprecated"),
    ClassHasMetadata UMETA(DisplayName="Class Has Metadata"),
    ActorLabelCountAtMost UMETA(DisplayName="Actor Label Count At Most"),
    ActorCountWithTagAtMost UMETA(DisplayName="Actor Count With Tag At Most"),
    ActorFolderCountExactly UMETA(DisplayName="Actor Folder Count Exactly"),
    ActorFolderCountAtMost UMETA(DisplayName="Actor Folder Count At Most"),
    SelectedActorIsTemporarilyHidden UMETA(DisplayName="Selected Actor Is Temporarily Hidden"),
    SelectedActorIsNotTemporarilyHidden UMETA(DisplayName="Selected Actor Is Not Temporarily Hidden"),
    // 0.11.0: selection-only Editor actions. Selection is restored after every run.
    SelectionClear UMETA(DisplayName="Clear Editor Actor Selection"),
    SelectionSelectFirstActor UMETA(DisplayName="Select First Available Actor"),
    SelectionSelectActorByLabel UMETA(DisplayName="Select Actor By Label"),
    SelectionSelectActorsWithTag UMETA(DisplayName="Select Actors With Tag"),
    SelectionRestore UMETA(DisplayName="Restore Original Actor Selection"),
    // 0.12.0: controlled transient Editor-world actor fixtures. Append only.
    ActorFixtureSpawn UMETA(DisplayName="Spawn Temporary Editor Actor"),
    ActorFixtureSetLabel UMETA(DisplayName="Set Temporary Actor Label"),
    ActorFixtureSetFolder UMETA(DisplayName="Set Temporary Actor Folder"),
    ActorFixtureAddTag UMETA(DisplayName="Add Temporary Actor Tag"),
    ActorFixtureRemoveTag UMETA(DisplayName="Remove Temporary Actor Tag"),
    ActorFixtureSelect UMETA(DisplayName="Select Temporary Actor"),
    ActorFixtureHide UMETA(DisplayName="Hide Temporary Actor In Editor"),
    ActorFixtureShow UMETA(DisplayName="Show Temporary Actor In Editor"),
    ActorFixtureExists UMETA(DisplayName="Temporary Actor Exists"),
    ActorFixtureLabelEquals UMETA(DisplayName="Temporary Actor Label Equals"),
    ActorFixtureHasTag UMETA(DisplayName="Temporary Actor Has Tag"),
    ActorFixtureFolderEquals UMETA(DisplayName="Temporary Actor Folder Equals"),
    ActorFixtureIsSelected UMETA(DisplayName="Temporary Actor Is Selected"),
    ActorFixtureIsHidden UMETA(DisplayName="Temporary Actor Is Hidden"),
    ActorFixtureDestroy UMETA(DisplayName="Destroy Temporary Editor Actor"),
    // 0.13.0: reversible test-owned transform and attachment workflows. Append only.
    ActorFixtureSetLocation UMETA(DisplayName="Set Temporary Actor Location"),
    ActorFixtureLocationNearlyEquals UMETA(DisplayName="Temporary Actor Location Nearly Equals"),
    ActorFixtureSetRotation UMETA(DisplayName="Set Temporary Actor Rotation"),
    ActorFixtureRotationNearlyEquals UMETA(DisplayName="Temporary Actor Rotation Nearly Equals"),
    ActorFixtureSetScale UMETA(DisplayName="Set Temporary Actor Scale"),
    ActorFixtureScaleNearlyEquals UMETA(DisplayName="Temporary Actor Scale Nearly Equals"),
    ActorFixtureAttachTo UMETA(DisplayName="Attach Temporary Actor To Temporary Parent"),
    ActorFixtureDetach UMETA(DisplayName="Detach Temporary Actor"),
    ActorFixtureHasParent UMETA(DisplayName="Temporary Actor Has Parent"),
    ActorFixtureHasNoParent UMETA(DisplayName="Temporary Actor Has No Parent"),
    ActorFixtureChildCountExactly UMETA(DisplayName="Temporary Actor Child Count Exactly"),
    ActorFixtureTagAbsent UMETA(DisplayName="Temporary Actor Does Not Have Tag"),
    ActorFixtureIsVisible UMETA(DisplayName="Temporary Actor Is Visible"),
    ActorFixtureIsNotSelected UMETA(DisplayName="Temporary Actor Is Not Selected"),
    ActorFixtureFolderNotEquals UMETA(DisplayName="Temporary Actor Folder Does Not Equal"),
    ActorFixtureDistanceAtMost UMETA(DisplayName="Temporary Actors Distance At Most"),
    ActorFixtureChildCountAtLeast UMETA(DisplayName="Temporary Actor Child Count At Least"),
    ActorFixtureChildCountAtMost UMETA(DisplayName="Temporary Actor Child Count At Most"),
    ActorFixtureDistanceAtLeast UMETA(DisplayName="Temporary Actors Distance At Least"),
    ActorFixtureLabelContains UMETA(DisplayName="Temporary Actor Label Contains"),
    ActorFixtureLabelStartsWith UMETA(DisplayName="Temporary Actor Label Starts With"),
    ActorFixtureLabelEndsWith UMETA(DisplayName="Temporary Actor Label Ends With"),
    ActorFixtureTagCountExactly UMETA(DisplayName="Temporary Actor Tag Count Exactly"),
    ActorFixtureHasRootComponent UMETA(DisplayName="Temporary Actor Has Root Component"),
    ActorFixtureLocationNotNearlyEquals UMETA(DisplayName="Temporary Actor Location Not Nearly Equals"),
    ActorFixtureRotationNotNearlyEquals UMETA(DisplayName="Temporary Actor Rotation Not Nearly Equals"),
    ActorFixtureScaleNotNearlyEquals UMETA(DisplayName="Temporary Actor Scale Not Nearly Equals")
};

USTRUCT(BlueprintType)
struct FProbeTestStep
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step")
    EProbeStepType Type = EProbeStepType::EditorWorldExists;

    // Optional registered extension assertion (e.g. Focus.ActorSoloed).
    // NAME_None means execute the historical built-in enum Type.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step")
    FName AssertionId = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step", meta=(ToolTip="Plugin name, class path/name, or asset object path depending on Type."))
    FString Target;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step", meta=(ClampMin="0"))
    int32 MinimumActorCount = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step", meta=(ToolTip="Comparison class or component/interface class path, e.g. /Script/Engine.Actor"))
    FString ExpectedClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step", meta=(ToolTip="Name of a reflected property, function, or Asset Registry tag depending on Type"))
    FString PropertyName;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step", meta=(ToolTip="Expected value for equality/inequality or object state assertions"))
    FString ExpectedValue;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step", meta=(ClampMin="0", ToolTip="Absolute numeric tolerance for reflected property comparisons"))
    double NumericTolerance = 0.0001;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step")
    FString Description;
};

UCLASS(BlueprintType)
class PROBEEDITOR_API UProbeTestAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(VisibleAnywhere, Category="Probe", meta=(ToolTip="Stable identifier used in reports, generated once when the asset is created."))
    FGuid TestId;

    UPROPERTY(EditAnywhere, Category="Probe")
    FString TestDescription;

    UPROPERTY(EditAnywhere, Category="Probe")
    bool bEnabled = true;

    // Legacy tests remain immediately runnable. Fresh templates requiring configuration
    // are held back from Run All until the author marks them ready in Probe.
    UPROPERTY(EditAnywhere, Category="Probe", meta=(ToolTip="Templates require review before inclusion in automatic runs."))
    bool bNeedsConfiguration = false;

    // Default is opt-out for old and new assets. Controlled actor actions briefly
    // affect the loaded Editor world and may mark its level package dirty.
    UPROPERTY(EditAnywhere, Category="Probe", meta=(ToolTip="Allow this test to spawn transient actors in the active Editor level. Probe only edits its own fixtures and always attempts cleanup. The level can still become dirty."))
    bool bAllowTransientEditorActors = false;

    UPROPERTY(EditAnywhere, Category="Probe")
    TArray<FProbeTestStep> Steps;

    virtual void PostInitProperties() override;
};
