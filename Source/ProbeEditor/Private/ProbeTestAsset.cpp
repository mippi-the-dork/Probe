#include "ProbeTestAsset.h"

void UProbeTestAsset::PostInitProperties()
{
    Super::PostInitProperties();
    if (!HasAnyFlags(RF_ClassDefaultObject) && !TestId.IsValid())
    {
        TestId = FGuid::NewGuid();
    }
}
