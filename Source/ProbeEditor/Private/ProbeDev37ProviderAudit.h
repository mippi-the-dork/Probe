#pragma once

#include "CoreMinimal.h"
#include "ProbePortableProviders.h"

// Dev 37: read-only, deterministic review of the loaded, asset-free provider API.
// This is a declaration/registration audit, not a substitute for executing tests.
namespace ProbeDev37
{
    struct FProviderSummary
    {
        FString Name;
        FString Version;
        int32 Assertions = 0;
        int32 Actions = 0;
        int32 Tests = 0;
        int32 Packs = 0;
        int32 Templates = 0;
    };

    struct FReport
    {
        int32 Providers = 0;
        int32 Tests = 0;
        int32 Packs = 0;
        int32 Assertions = 0;
        int32 Actions = 0;
        int32 Templates = 0;
        int32 Blockers = 0;
        int32 Deferred = 0;
        TArray<FString> Findings;
        TArray<FProviderSummary> Details;
        FString ToText() const;
    };

    // Pure policy function. Does not refresh registries, load classes, run tests or mutate assets.
    FReport Audit(const TArray<ProbePortable::FPortableProvider>& Providers,
        const TArray<FString>& DiscoveryDiagnostics);
}
