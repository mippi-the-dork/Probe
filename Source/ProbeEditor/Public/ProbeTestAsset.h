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
    ActorCountAtLeast UMETA(DisplayName="Actor count at least")
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
