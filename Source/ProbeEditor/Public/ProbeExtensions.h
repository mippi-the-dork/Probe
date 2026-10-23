#pragma once

#include "CoreMinimal.h"
#include "ProbeTestAsset.h"
#include "ProbeTestRunner.h"
#include "Templates/Function.h"

/**
 * Optional editor-only integration API for other plugins. Add a Probe dependency to the
 * companion testing plugin, then register in StartupModule and unregister in ShutdownModule.
 * IDs are permanent, namespaced, and independent from the built-in serialized enum.
 */
namespace Probe
{
    struct FExtensionAssertion
    {
        FName Id; // e.g. "Focus.ActorIsSoloed"
        FString Name;
        FString Category;
        FString Description;
        uint8 Fields = 0; // EProbeFieldFlags in ProbeAssertionCatalog.h
        TFunction<EResultStatus(const FProbeTestStep&, FString&)> Evaluate;
    };

    struct FExtensionTemplate
    {
        FName Id; // e.g. "Focus.SoloRegression"
        FString Name;
        FString Category;
        FString Description;
        TArray<FProbeTestStep> Steps;
        // True by default: third-party templates should never appear runnable until reviewed.
        bool bRequiresConfiguration = true;
    };

    /** All registry access is on the Editor/game thread. Registry owns the definitions. */
    PROBEEDITOR_API bool RegisterAssertion(FExtensionAssertion Definition);
    PROBEEDITOR_API bool RegisterTemplate(FExtensionTemplate Definition);
    PROBEEDITOR_API void UnregisterAssertion(FName Id);
    PROBEEDITOR_API void UnregisterTemplate(FName Id);
    PROBEEDITOR_API const FExtensionAssertion* FindExtensionAssertion(FName Id);
    PROBEEDITOR_API const FExtensionTemplate* FindExtensionTemplate(FName Id);
    PROBEEDITOR_API TArray<FName> GetExtensionAssertionIds();
    PROBEEDITOR_API TArray<FName> GetExtensionTemplateIds();
}
