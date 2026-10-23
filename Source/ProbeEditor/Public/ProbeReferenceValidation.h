#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"

class UProbeTestPack;

namespace Probe
{
    // Registry-only lookup: deliberately does not attempt to load missing test packages.
    enum class EReferenceHealth : uint8 { Valid, Missing, Redirector, WrongType, Empty };

    struct FReferenceHealth
    {
        EReferenceHealth Status = EReferenceHealth::Empty;
        FString Path;
        FString Explanation;
        bool IsUsable() const { return Status == EReferenceHealth::Valid; }
    };

    struct FReferenceAudit
    {
        int32 PacksChecked = 0;
        int32 ReferencesChecked = 0;
        int32 Problems = 0;
        int32 DuplicateIds = 0;
        TArray<FString> Findings;
        FString ToText() const;
    };

    PROBEEDITOR_API FReferenceHealth InspectTestReference(const FSoftObjectPath& Path);
    PROBEEDITOR_API FReferenceAudit AuditPackReferences(const UProbeTestPack* Pack);
    PROBEEDITOR_API FReferenceAudit AuditAllReferences();
}
