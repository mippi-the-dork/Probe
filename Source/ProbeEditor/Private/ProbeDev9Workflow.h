#pragma once

#include "ProbeTestRunner.h"
#include "ProbeStepGroups.h"

// All scenario configuration is opt-in. The runner consumes an expanded copy;
// the Test Asset, its permission flags and its serialized enum ordinals stay untouched.
namespace ProbeDev9Workflow
{
    struct FLifecycle
    {
        int32 CleanupFirst = INDEX_NONE;
        int32 CleanupLast = INDEX_NONE;
        bool HasCleanup() const { return CleanupFirst != INDEX_NONE; }
    };

    inline bool FindEdgeGroup(const TArray<FProbeTestStep>& Steps, const FString& RawName,
        bool bFirst, int32& OutFirst, int32& OutLast, FString& Error)
    {
        const FString Name = RawName.TrimStartAndEnd();
        if (Name.IsEmpty()) { return true; }
        if (Steps.IsEmpty()) { Error = TEXT("Workflow has no steps."); return false; }
        const int32 Edge = bFirst ? 0 : Steps.Num() - 1;
        if (Steps[Edge].GroupName.TrimStartAndEnd() != Name)
        {
            Error = FString::Printf(TEXT("%s group '%s' must be at the %s of the test."),
                bFirst ? TEXT("Setup") : TEXT("Cleanup"), *Name, bFirst ? TEXT("start") : TEXT("end"));
            return false;
        }
        const ProbeStepGroups::FRange Range = ProbeStepGroups::FindContiguous(Steps, Edge);
        for (int32 I = 0; I < Steps.Num(); ++I)
        {
            if ((I < Range.First || I > Range.Last) && Steps[I].GroupName.TrimStartAndEnd() == Name)
            {
                Error = FString::Printf(TEXT("Group '%s' occurs in multiple separate blocks. Lifecycle groups must be contiguous."), *Name);
                return false;
            }
        }
        OutFirst = Range.First;
        OutLast = Range.Last;
        return true;
    }

    inline bool ValidateLifecycle(const UProbeTestAsset& Asset, const Probe::FRunOptions& Options,
        FLifecycle& Out, FString& Error)
    {
        Out = FLifecycle(); Error.Reset();
        // Scoped group retries do not silently run setup or cleanup.
        if (Options.FirstStepIndex != 0 || Options.LastStepIndex < Asset.Steps.Num() - 1)
        { return true; }
        int32 SetupFirst = INDEX_NONE, SetupLast = INDEX_NONE;
        if (!FindEdgeGroup(Asset.Steps, Asset.SetupGroupName, true, SetupFirst, SetupLast, Error)
            || !FindEdgeGroup(Asset.Steps, Asset.CleanupGroupName, false, Out.CleanupFirst, Out.CleanupLast, Error))
        { return false; }
        if (SetupFirst != INDEX_NONE && Out.HasCleanup() && SetupLast >= Out.CleanupFirst)
        {
            Error = TEXT("Setup and Cleanup groups must be distinct, nonoverlapping blocks.");
            return false;
        }
        if (Out.HasCleanup() && Out.CleanupFirst == 0)
        {
            Error = TEXT("Cleanup must follow at least one setup or verification step.");
            return false;
        }
        return true;
    }

    inline bool IsValidKey(const FString& Key)
    {
        if (Key.IsEmpty() || Key.Len() > 48) { return false; }
        for (int32 I = 0; I < Key.Len(); ++I)
        {
            const TCHAR C = Key[I];
            const bool bAlpha = (C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z'));
            if (!bAlpha && C != TEXT('_') && !(I > 0 && C >= TEXT('0') && C <= TEXT('9')))
            { return false; }
        }
        return true;
    }

    inline bool ParseBindings(const FString& Source, TMap<FString, FString>& Values, FString& Error)
    {
        Values.Reset(); Error.Reset();
        if (Source.Len() > 8192) { Error = TEXT("Variation bindings exceed 8192 characters."); return false; }
        TArray<FString> Lines;
        Source.Replace(TEXT(";"), TEXT("\n")).ParseIntoArrayLines(Lines, false);
        for (FString Line : Lines)
        {
            Line = Line.TrimStartAndEnd();
            if (Line.IsEmpty()) { continue; }
            int32 Separator = INDEX_NONE;
            if (!Line.FindChar(TEXT('='), Separator))
            { Error = FString::Printf(TEXT("Expected Key=Value: '%s'."), *Line); return false; }
            const FString Key = Line.Left(Separator).TrimStartAndEnd();
            const FString Value = Line.Mid(Separator + 1).TrimStartAndEnd();
            if (!IsValidKey(Key))
            { Error = FString::Printf(TEXT("Invalid parameter key '%s'. Use letters, digits and underscores, starting with a letter or underscore."), *Key); return false; }
            for (const TPair<FString, FString>& Existing : Values)
            {
                if (Existing.Key.Equals(Key, ESearchCase::IgnoreCase))
                { Error = FString::Printf(TEXT("Duplicate parameter key '%s'."), *Key); return false; }
            }
            if (Values.Num() >= 64) { Error = TEXT("At most 64 parameters are allowed."); return false; }
            Values.Add(Key, Value);
        }
        return true;
    }

    inline bool ExpandField(FString& Field, const TMap<FString, FString>& Values, FString& Error)
    {
        FString Resolved;
        for (int32 I = 0; I < Field.Len();)
        {
            if (Field[I] == TEXT('$') && I + 1 < Field.Len() && Field[I + 1] == TEXT('{'))
            {
                const int32 NameStart = I + 2;
                int32 End = INDEX_NONE;
                for (int32 J = NameStart; J < Field.Len(); ++J)
                { if (Field[J] == TEXT('}')) { End = J; break; } }
                if (End == INDEX_NONE) { Error = TEXT("Unclosed ${Parameter} placeholder."); return false; }
                const FString Key = Field.Mid(NameStart, End - NameStart);
                const FString* Value = Values.Find(Key);
                if (!Value) { Error = FString::Printf(TEXT("Missing variation parameter '%s'."), *Key); return false; }
                Resolved += *Value;
                I = End + 1;
            }
            else { Resolved.AppendChar(Field[I]); ++I; }
        }
        Field = MoveTemp(Resolved);
        return true;
    }

    inline bool Resolve(const UProbeTestAsset& Asset, int32 VariationIndex,
        TArray<FProbeTestStep>& OutSteps, FString& OutName, FString& Error)
    {
        OutSteps = Asset.Steps; OutName.Reset(); Error.Reset();
        if (VariationIndex == INDEX_NONE) { return true; }
        if (!Asset.Variations.IsValidIndex(VariationIndex))
        { Error = TEXT("Variation index no longer exists."); return false; }
        const FProbeTestVariation& Variation = Asset.Variations[VariationIndex];
        OutName = Variation.Name.TrimStartAndEnd();
        if (!Variation.bEnabled || OutName.IsEmpty())
        { Error = TEXT("Variation is disabled or unnamed."); return false; }
        for (int32 Index = 0; Index < Asset.Variations.Num(); ++Index)
        {
            if (Index != VariationIndex && Asset.Variations[Index].Name.TrimStartAndEnd().Equals(OutName, ESearchCase::IgnoreCase))
            { Error = TEXT("Variation names must be unique."); return false; }
        }
        TMap<FString, FString> Values;
        if (!ParseBindings(Variation.Bindings, Values, Error)) { return false; }
        for (int32 I = 0; I < OutSteps.Num(); ++I)
        {
            FProbeTestStep& Step = OutSteps[I];
            if (!ExpandField(Step.Target, Values, Error)
                || !ExpandField(Step.ExpectedClass, Values, Error)
                || !ExpandField(Step.PropertyName, Values, Error)
                || !ExpandField(Step.ExpectedValue, Values, Error)
                || !ExpandField(Step.Description, Values, Error))
            {
                Error = FString::Printf(TEXT("Step %d: %s"), I + 1, *Error);
                return false;
            }
        }
        return true;
    }

    inline TArray<int32> EnabledVariationIndices(const UProbeTestAsset& Asset)
    {
        TArray<int32> Indices;
        for (int32 I = 0; I < Asset.Variations.Num(); ++I)
        {
            if (Asset.Variations[I].bEnabled) { Indices.Add(I); }
        }
        return Indices;
    }
}
