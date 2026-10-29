#pragma once

// Dev 4: read-only enumeration of explicit Automation Driver IDs in live Slate.
// Deliberately no synthesized type/path selectors: they are not durable test targets.
#include "Framework/Application/SlateApplication.h"
#include "Framework/MetaData/DriverIdMetaData.h"
#include "Widgets/SWindow.h"
#include "Layout/Children.h"
#include "Widgets/SWidget.h"

namespace ProbeWidgetDiscovery
{
    struct FCandidate
    {
        FString Id;
        FString WidgetType;
        FString WindowTitle;
        bool bVisible = false;
        bool bEnabled = false;
        int32 Matches = 1;

        FString Selector() const { return TEXT("id:") + Id; }
        bool IsUnique() const { return Matches == 1; }
    };

    // The scanner runs only on the game/UI thread, on an explicit refresh.
    // Hard limits protect the editor from unexpectedly large or cyclic trees.
    inline void Visit(const TSharedRef<SWidget>& Widget, const FString& WindowTitle,
        TArray<FCandidate>& Out, TSet<const SWidget*>& Visited, int32& Remaining, int32 Depth,
        bool bParentVisible = true, bool bParentEnabled = true)
    {
        if (Remaining-- <= 0 || Depth > 100 || Visited.Contains(&Widget.Get())) { return; }
        Visited.Add(&Widget.Get());
        const bool bVisible = bParentVisible && Widget->GetVisibility().IsVisible();
        const bool bEnabled = bParentEnabled && Widget->IsEnabled();
        if (TSharedPtr<FDriverIdMetaData> DriverId = Widget->GetMetaData<FDriverIdMetaData>())
        {
            if (!DriverId->Id.IsNone())
            {
                FCandidate& Item = Out.AddDefaulted_GetRef();
                Item.Id = DriverId->Id.ToString();
                Item.WidgetType = Widget->GetTypeAsString();
                Item.WindowTitle = WindowTitle;
                Item.bVisible = bVisible;
                Item.bEnabled = bEnabled;
            }
        }
        if (FChildren* Children = Widget->GetChildren())
        {
            for (int32 Index = 0; Index < Children->Num() && Remaining > 0; ++Index)
            { Visit(Children->GetChildAt(Index), WindowTitle, Out, Visited, Remaining, Depth + 1, bVisible, bEnabled); }
        }
    }

    inline TArray<FCandidate> Collect()
    {
        TArray<FCandidate> Out;
        if (!FSlateApplication::IsInitialized()) { return Out; }
        TSet<const SWidget*> Visited;
        int32 Remaining = 100000;
        TArray<TSharedRef<SWindow>> Windows;
        FSlateApplication::Get().GetAllVisibleWindowsOrdered(Windows);
        for (const TSharedRef<SWindow>& Window : Windows)
        {
            if (Remaining <= 0) { break; }
            Visit(StaticCastSharedRef<SWidget>(Window), Window->GetTitle().ToString(),
                Out, Visited, Remaining, 0);
        }
        TMap<FString, int32> Counts;
        for (const FCandidate& Item : Out) { ++Counts.FindOrAdd(Item.Id); }
        for (FCandidate& Item : Out) { Item.Matches = Counts.FindChecked(Item.Id); }
        Out.Sort([](const FCandidate& A, const FCandidate& B)
        {
            if (A.WindowTitle != B.WindowTitle) { return A.WindowTitle < B.WindowTitle; }
            if (A.Id != B.Id) { return A.Id < B.Id; }
            return A.WidgetType < B.WidgetType;
        });
        return Out;
    }
}
