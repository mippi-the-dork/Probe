#include "ProbeTestAssetFactory.h"
#include "ProbeTestAsset.h"

UProbeTestAssetFactory::UProbeTestAssetFactory(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    DataAssetClass = UProbeTestAsset::StaticClass();
    SupportedClass = UProbeTestAsset::StaticClass();
    bCreateNew = true;
    bEditAfterNew = false;
}

bool UProbeTestAssetFactory::ConfigureProperties()
{
    // UDataAssetFactory::ConfigureProperties opens the generic class-picker.
    // Probe tests always use UProbeTestAsset, so no second dialog is needed.
    DataAssetClass = UProbeTestAsset::StaticClass();
    return true;
}
