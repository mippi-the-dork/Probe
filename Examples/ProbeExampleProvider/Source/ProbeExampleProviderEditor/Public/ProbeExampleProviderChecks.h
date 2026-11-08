#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ProbeExampleProviderChecks.generated.h"

/** Transient-only example fixture. Owned by this plugin, not by Probe. */
UCLASS()
class PROBEEXAMPLEPROVIDEREDITOR_API UProbeExampleProviderFixture : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Probe Example")
    bool bEnabled = true;
};

/** This class belongs to the provider; Probe does not define or compile it. */
UCLASS()
class PROBEEXAMPLEPROVIDEREDITOR_API UProbeExampleProviderChecks : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Probe checks this metadata and the exact reflected signature before calling it.
    UFUNCTION(BlueprintCallable, Category="Example Provider", meta=(ProbeReadOnly="true"))
    static bool CheckExpectedPlugin(FString Target, FString ExpectedValue, FString& OutDetail);

    // Provider-owned, temporary in-memory state. These functions do NOT include
    // Probe headers or link to Probe; metadata declares each narrowly-scoped hook.
    UFUNCTION(BlueprintCallable, Category="Example Provider", meta=(ProbeActionStart="true"))
    static bool BeginDelayedCheck(FString Target, FString ExpectedValue, FString& OutDetail);
    UFUNCTION(BlueprintCallable, Category="Example Provider", meta=(ProbeActionPoll="true"))
    static bool PollDelayedCheck(FString Target, FString ExpectedValue, bool& OutComplete, bool& OutPassed, FString& OutDetail);
    UFUNCTION(BlueprintCallable, Category="Example Provider", meta=(ProbeActionCancel="true"))
    static bool CancelDelayedCheck(FString Target, FString ExpectedValue, FString& OutDetail);
    UFUNCTION(BlueprintCallable, Category="Example Provider", meta=(ProbeActionCleanup="true"))
    static bool CleanupDelayedCheck(FString Target, FString ExpectedValue, FString& OutDetail);
};
