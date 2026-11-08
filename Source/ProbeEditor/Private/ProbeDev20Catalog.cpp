#include "ProbeDev20Catalog.h"
#include "ProbePortableProviders.h"
#include "ProbeTestAsset.h"
#include "ProbeTestPack.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/AutomationTest.h"

namespace ProbeDev20
{
    const TCHAR* SourceName(ESource Source)
    {
        switch (Source)
        {
        case ESource::AuthoredTest: return TEXT("Authored Test");
        case ESource::AuthoredPack: return TEXT("Authored Pack");
        case ESource::PortableTest: return TEXT("Portable Test");
        case ESource::PortablePack: return TEXT("Portable Pack");
        case ESource::NativeAutomation: return TEXT("Unreal Automation");
        }
        return TEXT("Unknown");
    }

    FString Identity(const FEntry& Entry)
    {
        return FString::FromInt(static_cast<int32>(Entry.Source)) + TEXT(":") + Entry.Id;
    }

    bool Matches(const FEntry& Entry, const FString& Search, const FString& SourceFilter)
    {
        const FString Kind = SourceName(Entry.Source);
        if (!SourceFilter.IsEmpty() && SourceFilter != TEXT("All")
            && !Kind.Equals(SourceFilter, ESearchCase::IgnoreCase))
        { return false; }
        const FString Query = Search.TrimStartAndEnd();
        return Query.IsEmpty()
            || Entry.Name.Contains(Query, ESearchCase::IgnoreCase)
            || Entry.Id.Contains(Query, ESearchCase::IgnoreCase)
            || Entry.Owner.Contains(Query, ESearchCase::IgnoreCase)
            || Entry.Locator.Contains(Query, ESearchCase::IgnoreCase)
            || Entry.Detail.Contains(Query, ESearchCase::IgnoreCase)
            || Kind.Contains(Query, ESearchCase::IgnoreCase);
    }

    TArray<FEntry> Discover()
    {
        check(IsInGameThread());
        TArray<FEntry> Entries;
        FAssetRegistryModule& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        auto AddAssets = [&Entries, &Registry](const UClass* AssetClass, ESource Source)
        {
            FARFilter Filter;
            Filter.ClassPaths.Add(AssetClass->GetClassPathName());
            Filter.bRecursiveClasses = true;
            TArray<FAssetData> Assets;
            Registry.Get().GetAssets(Filter, Assets);
            for (const FAssetData& Asset : Assets)
            {
                FEntry Item;
                Item.Source = Source;
                Item.Id = Asset.GetObjectPathString();
                Item.Locator = Item.Id;
                Item.Name = Asset.AssetName.ToString();
                Item.Owner = TEXT("Project");
                Item.Detail = Asset.PackagePath.ToString();
                Entries.Add(MoveTemp(Item));
            }
        };
        AddAssets(UProbeTestAsset::StaticClass(), ESource::AuthoredTest);
        AddAssets(UProbeTestPack::StaticClass(), ESource::AuthoredPack);
        for (const ProbePortable::FPortableProvider& Provider : ProbePortable::Providers())
        {
            for (const ProbePortable::FPortableTest& Test : Provider.Tests)
            {
                FEntry Entry;
                Entry.Source = ESource::PortableTest;
                Entry.Id = Test.Id;
                Entry.Locator = Test.Id;
                Entry.Name = Test.Name;
                Entry.Owner = Provider.PluginName;
                Entry.Detail = Test.Description.IsEmpty() ? Test.SourcePath : Test.Description;
                Entry.bRequiresAuthorization = Test.bRequiresActionAuthorization;
                Entries.Add(MoveTemp(Entry));
            }
            for (const ProbePortable::FPortablePack& Pack : Provider.Packs)
            {
                FEntry Entry;
                Entry.Source = ESource::PortablePack;
                Entry.Id = Pack.Id;
                Entry.Locator = Pack.Id;
                Entry.Name = Pack.Name;
                Entry.Owner = Provider.PluginName;
                Entry.Detail = FString::Printf(TEXT("%d portable Tests | %s"), Pack.TestIds.Num(), *Pack.SourcePath);
                Entry.bRequiresAuthorization = ProbePortable::RequiresAuthorization(Pack.Id, true);
                Entries.Add(MoveTemp(Entry));
            }
        }
        TArray<FAutomationTestInfo> NativeTests;
        FAutomationTestFramework::Get().GetValidTestNames(NativeTests);
        for (FAutomationTestInfo& Native : NativeTests)
        {
            FEntry Entry;
            Entry.Source = ESource::NativeAutomation;
            Entry.Id = Native.GetTestName();
            Entry.Locator = Entry.Id;
            Entry.Name = Native.GetDisplayName();
            if (Entry.Name.IsEmpty()) { Entry.Name = Native.GetFullTestPath(); }
            Entry.Owner = TEXT("Unreal Automation Framework");
            Entry.Detail = Native.GetSourceFile();
            Entries.Add(MoveTemp(Entry));
        }
        Entries.Sort([](const FEntry& A, const FEntry& B)
        {
            if (A.Source != B.Source) { return static_cast<int32>(A.Source) < static_cast<int32>(B.Source); }
            return A.Name.Compare(B.Name, ESearchCase::IgnoreCase) < 0;
        });
        return Entries;
    }
}
