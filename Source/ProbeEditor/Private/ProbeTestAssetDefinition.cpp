#include "ProbeTestAssetDefinition.h"
#include "ProbeTestAsset.h"
#include "ProbeEditorNavigation.h"
#include "AssetDefinition.h"

#define LOCTEXT_NAMESPACE "ProbeTestAssetDefinition"

FText UProbeTestAssetDefinition::GetAssetDisplayName() const
{
    return LOCTEXT("ProbeTestTypeName", "Probe Test");
}

FText UProbeTestAssetDefinition::GetAssetDisplayName(const FAssetData& AssetData) const
{
    // Avoid the generic Data Asset display name, "Data Asset (Probe Test Asset)".
    return GetAssetDisplayName();
}

TSoftClassPtr<UObject> UProbeTestAssetDefinition::GetAssetClass() const
{
    return UProbeTestAsset::StaticClass();
}

FLinearColor UProbeTestAssetDefinition::GetAssetColor() const
{
    return FLinearColor(0.22f, 0.61f, 0.88f);
}

EAssetCommandResult UProbeTestAssetDefinition::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
    TArray<FString> Paths;
    for (const FAssetData& Data : OpenArgs.Assets)
    {
        if (Data.AssetClassPath == UProbeTestAsset::StaticClass()->GetClassPathName())
        {
            Paths.Add(Data.GetObjectPathString());
        }
    }

    if (Paths.IsEmpty())
    {
        return EAssetCommandResult::Unhandled;
    }

    // Keep the selection consistent with the existing "Open in Probe" context action.
    // We deliberately don't invoke the generic Data Asset toolkit here.
    Probe::OpenTestAssetsInWorkspace(Paths);
    return EAssetCommandResult::Handled;
}

#undef LOCTEXT_NAMESPACE
