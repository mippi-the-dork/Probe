#pragma once

// Dev 5: read-only enumeration, human-readable snapshots and explicit selector diagnostics.
// Dev 4: read-only enumeration of explicit Automation Driver IDs in live Slate.
// Deliberately no synthesized type/path selectors: they are not durable test targets.
#include "Framework/Application/SlateApplication.h"
#include "Framework/MetaData/DriverIdMetaData.h"
#include "Widgets/SWindow.h"
#include "Layout/Children.h"
#include "Widgets/SWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SEditableTextBox.h"

namespace ProbeWidgetDiscovery
{
    struct FCandidate
    {
        FString Id;
        FString WidgetType;
        FString WindowTitle;
        FString ReadableText; // Only known text widgets; never guesses from children.
        bool bHasReadableText = false;
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
                // Never try to infer a button's state from descendant labels. The
                // observed text belongs to the explicitly ID-tagged widget only.
                if (Item.WidgetType == TEXT("STextBlock"))
                {
                    Item.ReadableText = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
                    Item.bHasReadableText = true;
                }
                else if (Item.WidgetType == TEXT("SEditableTextBox"))
                {
                    Item.ReadableText = StaticCastSharedRef<SEditableTextBox>(Widget)->GetText().ToString();
                    Item.bHasReadableText = true;
                }
            }
        }
        if (FChildren* Children = Widget->GetChildren())
        {
            for (int32 Index = 0; Index < Children->Num() && Remaining > 0; ++Index)
            { Visit(Children->GetChildAt(Index), WindowTitle, Out, Visited, Remaining, Depth + 1, bVisible, bEnabled); }
        }
    }

    // A pure resolver: use exactly the same result for target diagnostics and
    // picker UI. A missing widget is NOT an invalid saved test, since the plugin
    // under test may not have its window open at authoring time.
    enum class EResolution : uint8 { Empty, Unsupported, NativeDetails, Missing, Unique, Ambiguous };
    struct FResolution
    {
        EResolution State = EResolution::Empty;
        int32 Matches = 0;
        FCandidate Candidate;

        bool CanUse() const { return State == EResolution::Unique; }
        FString Description() const
        {
            switch (State)
            {
            case EResolution::Empty: return TEXT("Enter or discover a widget selector.");
            case EResolution::Unsupported: return TEXT("Unsupported selector. Use id:Widget.Id or details:search.");
            case EResolution::NativeDetails: return TEXT("Native Details search is resolved by Probe at execution time, not enumerated by this browser.");
            case EResolution::Missing: return TEXT("Not found in visible Editor windows. Open the owning panel and refresh.");
            case EResolution::Ambiguous: return FString::Printf(TEXT("Ambiguous Automation Driver ID: %d live widgets match. Resolve duplicate IDs before running."), Matches);
            case EResolution::Unique:
                return FString::Printf(TEXT("Unique: %s in %s. %s / %s."),
                    *Candidate.WidgetType, *Candidate.WindowTitle,
                    Candidate.bVisible ? TEXT("Visible") : TEXT("Hidden"),
                    Candidate.bEnabled ? TEXT("Enabled") : TEXT("Disabled"));
            }
            return TEXT("Unknown target state.");
        }
    };

    inline FResolution Resolve(const TArray<FCandidate>& Candidates, FString Selector)
    {
        FResolution Result;
        Selector.TrimStartAndEndInline();
        if (Selector.IsEmpty()) { return Result; }
        // Match the authored runner, which accepts the native Details token
        // without regard to case.
        if (Selector.Equals(TEXT("details:search"), ESearchCase::IgnoreCase))
        {
            Result.State = EResolution::NativeDetails;
            return Result;
        }
        // Slate Automation Driver IDs are FName metadata. FName equality and
        // By::Id matching are case-insensitive; this resolver must agree.
        if (!Selector.StartsWith(TEXT("id:"), ESearchCase::IgnoreCase) || Selector.Len() <= 3)
        {
            Result.State = EResolution::Unsupported;
            return Result;
        }
        const FString Id = Selector.Mid(3);
        for (const FCandidate& Item : Candidates)
        {
            if (Item.Id.Equals(Id, ESearchCase::IgnoreCase))
            {
                ++Result.Matches;
                Result.Candidate = Item;
            }
        }
        Result.State = Result.Matches == 0 ? EResolution::Missing
            : Result.Matches == 1 ? EResolution::Unique : EResolution::Ambiguous;
        return Result;
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
        // Count colliding IDs with the same identity rule as the engine's
        // FDriverIdMetaData (FName), even if differently capitalized.
        TMap<FName, int32> Counts;
        for (const FCandidate& Item : Out) { ++Counts.FindOrAdd(FName(*Item.Id)); }
        for (FCandidate& Item : Out) { Item.Matches = Counts.FindChecked(FName(*Item.Id)); }
        Out.Sort([](const FCandidate& A, const FCandidate& B)
        {
            if (A.WindowTitle != B.WindowTitle) { return A.WindowTitle < B.WindowTitle; }
            if (A.Id != B.Id) { return A.Id < B.Id; }
            return A.WidgetType < B.WidgetType;
        });
        return Out;
    }
}
