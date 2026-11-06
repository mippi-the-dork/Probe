#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProbeExampleProviderChecks.generated.h"

/** This class belongs to the provider; Probe does not define or compile it. */
UCLASS()
class PROBEEXAMPLEPROVIDEREDITOR_API UProbeExampleProviderChecks : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Probe checks this metadata and the exact reflected signature before calling it.
    UFUNCTION(BlueprintCallable, Category="Example Provider", meta=(ProbeReadOnly="true"))
    static bool CheckExpectedPlugin(FString Target, FString ExpectedValue, FString& OutDetail);
};
