#pragma once
#include "CoreMinimal.h"
#include "Factories/DataAssetFactory.h"
#include "ProbeTestPackFactory.generated.h"

UCLASS()
class PROBEEDITOR_API UProbeTestPackFactory : public UDataAssetFactory
{
    GENERATED_BODY()
public:
    UProbeTestPackFactory(const FObjectInitializer& ObjectInitializer);
    virtual bool ConfigureProperties() override;
};
