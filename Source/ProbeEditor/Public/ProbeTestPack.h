#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "ProbeTestPack.generated.h"

class UProbeTestAsset;

/** An optional group of Probe Test assets; no copy of assertion data is stored. */
UCLASS(BlueprintType)
class PROBEEDITOR_API UProbeTestPack : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Probe Pack")
    FString Description;

    UPROPERTY(EditAnywhere, Category="Probe Pack")
    TArray<TSoftObjectPtr<UProbeTestAsset>> Tests;
};
