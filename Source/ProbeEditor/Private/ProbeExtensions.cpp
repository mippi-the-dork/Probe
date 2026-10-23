#include "ProbeExtensions.h"

namespace Probe
{
    namespace
    {
        TMap<FName, FExtensionAssertion>& Assertions()
        {
            static TMap<FName, FExtensionAssertion> Items;
            return Items;
        }
        TMap<FName, FExtensionTemplate>& Templates()
        {
            static TMap<FName, FExtensionTemplate> Items;
            return Items;
        }
        bool ValidId(FName Id)
        {
            const FString Value = Id.ToString();
            int32 DotIndex = INDEX_NONE;
            return !Id.IsNone() && Value.FindChar(TEXT('.'), DotIndex)
                && DotIndex > 0 && DotIndex < Value.Len() - 1;
        }
    }
    bool RegisterAssertion(FExtensionAssertion Definition)
    {
        if (!IsInGameThread() || !ValidId(Definition.Id) || Definition.Name.IsEmpty()
            || Definition.Category.IsEmpty() || !Definition.Evaluate || Assertions().Contains(Definition.Id))
        {
            return false;
        }
        Assertions().Add(Definition.Id, MoveTemp(Definition));
        return true;
    }
    bool RegisterTemplate(FExtensionTemplate Definition)
    {
        if (!IsInGameThread() || !ValidId(Definition.Id) || Definition.Name.IsEmpty()
            || Definition.Category.IsEmpty() || Definition.Steps.IsEmpty() || Templates().Contains(Definition.Id))
        {
            return false;
        }
        Templates().Add(Definition.Id, MoveTemp(Definition));
        return true;
    }
    void UnregisterAssertion(FName Id) { if (IsInGameThread()) { Assertions().Remove(Id); } }
    void UnregisterTemplate(FName Id) { if (IsInGameThread()) { Templates().Remove(Id); } }
    const FExtensionAssertion* FindExtensionAssertion(FName Id) { return Assertions().Find(Id); }
    const FExtensionTemplate* FindExtensionTemplate(FName Id) { return Templates().Find(Id); }
    TArray<FName> GetExtensionAssertionIds()
    {
        TArray<FName> Names;
        Assertions().GetKeys(Names);
        Names.Sort([](FName A, FName B) { return A.ToString() < B.ToString(); });
        return Names;
    }
    TArray<FName> GetExtensionTemplateIds()
    {
        TArray<FName> Names;
        Templates().GetKeys(Names);
        Names.Sort([](FName A, FName B) { return A.ToString() < B.ToString(); });
        return Names;
    }
}
