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
    SelectedActorHasTag UMETA(DisplayName="Selected actor has tag")
};

USTRUCT(BlueprintType)
struct FProbeTestStep
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Step")
    EProbeStepType Type = EProbeStepType::EditorWorldExists;

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

    UPROPERTY(EditAnywhere, Category="Probe")
    TArray<FProbeTestStep> Steps;

    virtual void PostInitProperties() override;
};
