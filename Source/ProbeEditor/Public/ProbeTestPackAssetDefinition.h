#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "ProbeTestPackAssetDefinition.generated.h"

/** Native Content Browser identity for a reusable Probe Pack. */
UCLASS()
class PROBEEDITOR_API UProbeTestPackAssetDefinition : public UAssetDefinitionDefault
{
    GENERATED_BODY()
public:
    virtual FText GetAssetDisplayName() const override;
    virtual FText GetAssetDisplayName(const FAssetData& AssetData) const override;
    virtual TSoftClassPtr<UObject> GetAssetClass() const override;
    virtual FLinearColor GetAssetColor() const override;
    virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};
