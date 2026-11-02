#pragma once
#include "ProbeTestAsset.h"

// Dev 6: centralized native-UI contracts, shared by the runner, authoring,
// quick-step policy, and target preflight. No changes to serialized enum IDs.
namespace ProbeNativeUIPolicy
{
    inline bool IsInput(EProbeStepType Type)
    {
        return Type == EProbeStepType::UIWidgetClick || Type == EProbeStepType::UIWidgetTypeText
            || Type == EProbeStepType::UIWidgetAppendText || Type == EProbeStepType::UIWidgetClearText;
    }
    inline bool IsObservation(EProbeStepType Type)
    {
        return (Type >= EProbeStepType::UIWidgetExists && Type <= EProbeStepType::UIWaitForWidgetTextEquals)
            || (Type >= EProbeStepType::UIWidgetNotVisible && Type <= EProbeStepType::UIWaitForWidgetTextNotEquals);
    }
    inline bool IsWait(EProbeStepType Type)
    {
        return Type == EProbeStepType::UIWaitForWidgetVisible || Type == EProbeStepType::UIWaitForWidgetTextEquals
            || (Type >= EProbeStepType::UIWaitForWidgetExists && Type <= EProbeStepType::UIWaitForWidgetTextNotEquals);
    }
    inline bool ReadsText(EProbeStepType Type)
    {
        return Type == EProbeStepType::UIWidgetTextEquals || Type == EProbeStepType::UIWidgetTextContains
            || Type == EProbeStepType::UIWidgetTextNotEquals || Type == EProbeStepType::UIWaitForWidgetTextEquals
            || Type == EProbeStepType::UIWaitForWidgetTextContains || Type == EProbeStepType::UIWaitForWidgetTextNotEquals;
    }
    inline bool AllowsEmptyValue(EProbeStepType Type)
    {
        return Type == EProbeStepType::UIWidgetTextEquals || Type == EProbeStepType::UIWidgetTextNotEquals
            || Type == EProbeStepType::UIWaitForWidgetTextEquals || Type == EProbeStepType::UIWaitForWidgetTextNotEquals
            || Type == EProbeStepType::UIWidgetTypeText || Type == EProbeStepType::UIWidgetAppendText
            || Type == EProbeStepType::UIDetailsSearchTextEquals || Type == EProbeStepType::UIDetailsSearchTextNotEquals;
    }
    inline bool HasTarget(EProbeStepType Type) { return IsInput(Type) || IsObservation(Type); }
}
