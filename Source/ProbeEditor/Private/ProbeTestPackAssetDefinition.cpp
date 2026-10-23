#include "ProbeTestPackAssetDefinition.h"
#include "ProbeTestPack.h"
#include "ProbeEditorNavigation.h"

#define LOCTEXT_NAMESPACE "ProbeTestPackAssetDefinition"
FText UProbeTestPackAssetDefinition::GetAssetDisplayName() const
{
    return LOCTEXT("ProbeTestPackType", "Probe Pack");
}
FText UProbeTestPackAssetDefinition::GetAssetDisplayName(const FAssetData& AssetData) const
{
    return GetAssetDisplayName();
}
TSoftClassPtr<UObject> UProbeTestPackAssetDefinition::GetAssetClass() const
{
    return UProbeTestPack::StaticClass();
}
FLinearColor UProbeTestPackAssetDefinition::GetAssetColor() const
{
    return FLinearColor(0.22f, 0.61f, 0.88f);
}
EAssetCommandResult UProbeTestPackAssetDefinition::OpenAssets(const FAssetOpenArgs& OpenArgs) const
{
    TArray<FString> Paths;
    for (const FAssetData& Data : OpenArgs.Assets)
    {
        if (Data.AssetClassPath == UProbeTestPack::StaticClass()->GetClassPathName())
        {
            Paths.Add(Data.GetObjectPathString());
        }
    }
    if (Paths.IsEmpty()) { return EAssetCommandResult::Unhandled; }
    Probe::OpenPackAssetsInWorkspace(Paths);
    return EAssetCommandResult::Handled;
}
#undef LOCTEXT_NAMESPACE
