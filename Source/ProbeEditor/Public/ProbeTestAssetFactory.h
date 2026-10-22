#pragma once

#include "CoreMinimal.h"
#include "Factories/DataAssetFactory.h"
#include "ProbeTestAssetFactory.generated.h"

/** Creates Probe tests without asking users to select a Data Asset class. */
UCLASS()
class PROBEEDITOR_API UProbeTestAssetFactory : public UDataAssetFactory
{
    GENERATED_BODY()

public:
    UProbeTestAssetFactory(const FObjectInitializer& ObjectInitializer);

    virtual bool ConfigureProperties() override;
};
