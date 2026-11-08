#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

/** Dev 20: a single, read-only inventory of independently owned test definitions.
 * No portable JSON is imported as a UAsset and no native Automation test is run
 * while building or filtering this inventory.
 */
namespace ProbeDev20
{
    enum class ESource : uint8
    {
        AuthoredTest,
        AuthoredPack,
        PortableTest,
        PortablePack,
        NativeAutomation
    };

    struct FEntry
    {
        ESource Source = ESource::AuthoredTest;
        FString Id;
        FString Name;
        FString Owner;
        FString Detail;
        FString Locator; // Asset object path, portable ID, or exact native Automation name.
        bool bRequiresAuthorization = false;
    };

    const TCHAR* SourceName(ESource Source);
    bool Matches(const FEntry& Entry, const FString& Search, const FString& SourceFilter);
    /** Each source retains its original execution contract. No execution here. */
    TArray<FEntry> Discover();
    /** IDs are only unique within their typed namespace (not across all sources). */
    FString Identity(const FEntry& Entry);
}
