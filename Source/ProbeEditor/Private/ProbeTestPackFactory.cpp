#include "ProbeTestPackFactory.h"
#include "ProbeTestPack.h"

UProbeTestPackFactory::UProbeTestPackFactory(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    DataAssetClass = UProbeTestPack::StaticClass();
    SupportedClass = UProbeTestPack::StaticClass();
    bCreateNew = true;
    bEditAfterNew = false; // Probe Packs open in Probe's dedicated manager, not the generic Data Asset editor
}

bool UProbeTestPackFactory::ConfigureProperties()
{
    DataAssetClass = UProbeTestPack::StaticClass();
    return true;
}
