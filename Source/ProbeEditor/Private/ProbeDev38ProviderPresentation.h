#pragma once

#include "CoreMinimal.h"
#include "ProbePortableProviders.h"

// Pure, side-effect-free policy used by the native provider browser.
// Deliberately does not dispatch, resolve actions, or reload providers.
namespace ProbeDev38
{
    struct FCounts
    {
        int32 Providers = 0;
        int32 Tests = 0;
        int32 Packs = 0;
        int32 Diagnostics = 0;
        int32 VisibleProviders = 0;
        int32 VisibleDefinitions = 0;
    };

    inline bool Matches(const FString& Query, const FString& Name,
        const FString& Id, const FString& Owner)
    {
        return Query.IsEmpty() || Name.Contains(Query, ESearchCase::IgnoreCase)
            || Id.Contains(Query, ESearchCase::IgnoreCase)
            || Owner.Contains(Query, ESearchCase::IgnoreCase);
    }

    inline FCounts Count(const TArray<ProbePortable::FPortableProvider>& Providers,
        const TArray<FString>& Diagnostics, const FString& Query)
    {
        FCounts Result;
        Result.Providers = Providers.Num();
        Result.Diagnostics = Diagnostics.Num();
        for (const ProbePortable::FPortableProvider& Provider : Providers)
        {
            Result.Tests += Provider.Tests.Num();
            Result.Packs += Provider.Packs.Num();
            const bool bOwnerMatch = Matches(Query, Provider.PluginName, Provider.ProviderId, Provider.PluginName);
            int32 MatchingDefinitions = 0;
            for (const ProbePortable::FPortableTest& Test : Provider.Tests)
            {
                if (bOwnerMatch || Matches(Query, Test.Name, Test.Id, Provider.PluginName))
                { ++MatchingDefinitions; }
            }
            for (const ProbePortable::FPortablePack& Pack : Provider.Packs)
            {
                if (bOwnerMatch || Matches(Query, Pack.Name, Pack.Id, Provider.PluginName))
                { ++MatchingDefinitions; }
            }
            if (bOwnerMatch || MatchingDefinitions > 0)
            {
                ++Result.VisibleProviders;
                Result.VisibleDefinitions += MatchingDefinitions;
            }
        }
        return Result;
    }

    inline FString Summary(const FCounts& Counts, bool bFiltered)
    {
        const FString Total = FString::Printf(TEXT("%d providers  |  %d tests  |  %d packs  |  %d discovery issues"),
            Counts.Providers, Counts.Tests, Counts.Packs, Counts.Diagnostics);
        return bFiltered
            ? FString::Printf(TEXT("%s  |  %d matching providers, %d definitions"),
                *Total, Counts.VisibleProviders, Counts.VisibleDefinitions)
            : Total;
    }
}
