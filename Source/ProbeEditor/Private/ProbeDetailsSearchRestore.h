#pragma once

// Probe 0.15.4 dev2: restore the native Details search through the
// PropertyEditor API that owns its filter, not just the Slate text box.
// IDetailsView::SetSearchText(Empty) is not equivalent to ClearSearch():
// the latter explicitly clears the active search terms. All calls must
// occur on the Editor game thread.
#include "IDetailsView.h"
#include "Widgets/Input/SSearchBox.h"

namespace ProbeDetailsSearchRestore
{
    inline void Restore(const TSharedPtr<IDetailsView>& DetailsView,
        const TSharedPtr<SSearchBox>& SearchBox, const FText& OriginalSearchText)
    {
        check(IsInGameThread());
        if (DetailsView.IsValid())
        {
            if (OriginalSearchText.IsEmpty())
            {
                DetailsView->ClearSearch();
            }
            else
            {
                DetailsView->SetSearchText(OriginalSearchText);
            }
        }
        // Keep the *visible* text synchronized with the native filter.
        // Do this after restoring the view's objects (if a fixture replaced
        // them), so a refresh cannot reactivate the temporary search.
        if (SearchBox.IsValid())
        {
            SearchBox->SetText(OriginalSearchText);
        }
    }
}
