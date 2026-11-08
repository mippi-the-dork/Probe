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

namespace
{
    // Static provider-owned fixture tokens, never project assets or world objects.
    TMap<FString, int32> GExampleProbePolls;
}

bool UProbeExampleProviderChecks::BeginDelayedCheck(FString Target, FString ExpectedValue, FString& OutDetail)
{
    if (Target.IsEmpty() || GExampleProbePolls.Contains(Target))
    { OutDetail = TEXT("An empty or duplicate async token was refused."); return false; }
    GExampleProbePolls.Add(Target, 0);
    OutDetail = TEXT("Example async token started.");
    return true;
}

bool UProbeExampleProviderChecks::PollDelayedCheck(FString Target, FString ExpectedValue,
    bool& OutComplete, bool& OutPassed, FString& OutDetail)
{
    OutComplete = false; OutPassed = false;
    int32* Polls = GExampleProbePolls.Find(Target);
    if (!Polls) { OutDetail = TEXT("Async token is missing."); return false; }
    ++(*Polls);
    OutComplete = *Polls >= (ExpectedValue == TEXT("slow") ? 60 : 3);
    OutPassed = OutComplete && (ExpectedValue == TEXT("complete") || ExpectedValue == TEXT("slow"));
    OutDetail = OutComplete ? TEXT("Example action finished after three polls.")
        : TEXT("Example action is still pending.");
    return true;
}

bool UProbeExampleProviderChecks::CancelDelayedCheck(FString Target, FString ExpectedValue, FString& OutDetail)
{
    GExampleProbePolls.Remove(Target);
    OutDetail = TEXT("Example async token cancellation performed.");
    return true;
}

bool UProbeExampleProviderChecks::CleanupDelayedCheck(FString Target, FString ExpectedValue, FString& OutDetail)
{
    GExampleProbePolls.Remove(Target);
    OutDetail = TEXT("Example async token cleanup complete.");
    return true;
}
