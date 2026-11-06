#include "ProbeExampleProviderChecks.h"

#include "Interfaces/IPluginManager.h"

bool UProbeExampleProviderChecks::CheckExpectedPlugin(FString Target, FString ExpectedValue, FString& OutDetail)
{
    const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(Target);
    const bool bInstalled = Plugin.IsValid();
    const bool bMatch = bInstalled && (ExpectedValue.IsEmpty() || Plugin->GetName() == ExpectedValue);
    OutDetail = bMatch
        ? FString::Printf(TEXT("Plugin '%s' is installed and matches the expected ID."), *Target)
        : FString::Printf(TEXT("Plugin '%s' is unavailable or does not match '%s'."), *Target, *ExpectedValue);
    return bMatch;
}
