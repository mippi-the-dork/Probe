#pragma once

#include "CoreMinimal.h"
#include "AssetDefinitionDefault.h"
#include "ProbeTestAssetDefinition.generated.h"

/** Content Browser identity for Probe test Data Assets. */
UCLASS()
class PROBEEDITOR_API UProbeTestAssetDefinition : public UAssetDefinitionDefault
{
    GENERATED_BODY()

public:
    virtual FText GetAssetDisplayName() const override;
    virtual FText GetAssetDisplayName(const FAssetData& AssetData) const override;
    virtual TSoftClassPtr<UObject> GetAssetClass() const override;
    virtual FLinearColor GetAssetColor() const override;
    /** Double-click / Open always edits the test in Probe, not the generic Data Asset editor. */
    virtual EAssetCommandResult OpenAssets(const FAssetOpenArgs& OpenArgs) const override;
};
