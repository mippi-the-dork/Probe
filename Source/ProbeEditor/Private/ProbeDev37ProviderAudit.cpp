#include "ProbeDev37ProviderAudit.h"

namespace ProbeDev37
{
FReport Audit(const TArray<ProbePortable::FPortableProvider>& Providers,
    const TArray<FString>& DiscoveryDiagnostics)
{
    FReport Result;
    Result.Providers = Providers.Num();
    TSet<FString> SeenProviders;
    TSet<FString> SeenIds;
    for (const ProbePortable::FPortableProvider& Provider : Providers)
    {
        FProviderSummary Summary;
        Summary.Name = Provider.PluginName;
        Summary.Version = Provider.Version;
        Summary.Assertions = Provider.Assertions.Num();
        Summary.Actions = Provider.Actions.Num();
        Summary.Tests = Provider.Tests.Num();
        Summary.Packs = Provider.Packs.Num();
        Summary.Templates = Provider.Templates.Num();
        Result.Assertions += Summary.Assertions;
        Result.Actions += Summary.Actions;
        Result.Tests += Summary.Tests;
        Result.Packs += Summary.Packs;
        Result.Templates += Summary.Templates;
        Result.Details.Add(MoveTemp(Summary));
        if (Provider.PluginName.IsEmpty() || Provider.ProviderId != Provider.PluginName)
        {
            ++Result.Blockers;
            Result.Findings.Add(TEXT("[BLOCKER] PROVIDER_ID: Provider does not match its owning plugin."));
        }
        if (SeenProviders.Contains(Provider.PluginName))
        {
            ++Result.Blockers;
            Result.Findings.Add(TEXT("[BLOCKER] DUPLICATE_PROVIDER: ") + Provider.PluginName);
        }
        SeenProviders.Add(Provider.PluginName);
        auto CheckId = [&](const FString& Id)
        {
            if (Id.IsEmpty() || !Id.StartsWith(Provider.PluginName + TEXT(".")))
            {
                ++Result.Blockers;
                Result.Findings.Add(TEXT("[BLOCKER] ID_OWNERSHIP: ") + Id);
            }
            if (SeenIds.Contains(Id))
            {
                ++Result.Blockers;
                Result.Findings.Add(TEXT("[BLOCKER] DUPLICATE_DEFINITION: ") + Id);
            }
            SeenIds.Add(Id);
        };
        for (const auto& Definition : Provider.Assertions) { CheckId(Definition.Id); }
        for (const auto& Definition : Provider.Actions) { CheckId(Definition.Id); }
        for (const auto& Definition : Provider.Tests) { CheckId(Definition.Id); }
        for (const auto& Definition : Provider.Templates) { CheckId(Definition.Id); }
        for (const auto& Definition : Provider.Packs) { CheckId(Definition.Id); }
        for (const ProbePortable::FPortableTest& Definition : Provider.Tests)
        {
            if (Definition.bRequiresActionAuthorization)
            {
                ++Result.Deferred;
                Result.Findings.Add(TEXT("[DEFERRED] AUTHORIZED_EXECUTION: ") + Definition.Id
                    + TEXT(" requires explicit action permission and runtime cleanup verification."));
            }
            else
            {
                ++Result.Deferred;
                Result.Findings.Add(TEXT("[DEFERRED] EXECUTION: ") + Definition.Id
                    + TEXT(" has not been executed by this declaration audit."));
            }
        }
    }
    // Source discovery diagnostics are blockers even if a failed provider was never registered.
    for (const FString& Issue : DiscoveryDiagnostics)
    {
        ++Result.Blockers;
        Result.Findings.Add(TEXT("[BLOCKER] DISCOVERY: ") + Issue);
    }
    Result.Details.Sort([](const FProviderSummary& A, const FProviderSummary& B)
    { return A.Name < B.Name; });
    Result.Findings.Sort();
    return Result;
}

FString FReport::ToText() const
{
    FString Output = TEXT("PROBE PORTABLE API RELEASE AUDIT\n");
    Output += FString::Printf(TEXT("Providers: %d | Tests: %d | Packs: %d | Assertions: %d | Actions: %d | Templates: %d\n"),
        Providers, Tests, Packs, Assertions, Actions, Templates);
    Output += FString::Printf(TEXT("Definition blockers: %d | Execution checks deferred: %d\n"), Blockers, Deferred);
    Output += Blockers == 0 ? TEXT("Definition gate: PASS\n") : TEXT("Definition gate: BLOCKED\n");
    for (const FProviderSummary& Provider : Details)
    {
        Output += FString::Printf(TEXT("\n%s v%s | %d test(s), %d pack(s), %d assertion(s), %d action(s), %d template(s)\n"),
            *Provider.Name, *Provider.Version, Provider.Tests, Provider.Packs, Provider.Assertions,
            Provider.Actions, Provider.Templates);
    }
    for (const FString& Finding : Findings) { Output += Finding + TEXT("\n"); }
    Output += TEXT("\nScope: enabled, discovered JSON providers only. No assets created. No execution, permissions or module lifecycle guarantees inferred.\n");
    return Output;
}
}
