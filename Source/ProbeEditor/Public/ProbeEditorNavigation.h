#pragma once

#include "CoreMinimal.h"

/** Routes Probe Test asset activation to the existing dockable Probe workspace. */
namespace Probe
{
    PROBEEDITOR_API void OpenTestAssetsInWorkspace(const TArray<FString>& AssetObjectPaths);
}
