#pragma once
#include "ProbeAssertionCatalog.h"
#include "ProbeNativeUIPolicy.h"
#include "ProbeExtensions.h"

namespace ProbeDev14
{
    inline uint8 FieldsFor(const FProbeTestStep& Step)
    {
        if (!Step.AssertionId.IsNone())
        {
            if (const Probe::FExtensionAssertion* Info = Probe::FindExtensionAssertion(Step.AssertionId))
            { return Info->Fields; }
            return Probe::TargetField;
        }
        return Probe::GetAssertionInfo(Step.Type).Fields;
    }
    inline FString ConfigurationIssue(const FProbeTestStep& Step)
    {
        if (!Step.bEnabled) { return FString(); }
        if (!Step.AssertionId.IsNone() && !Probe::FindExtensionAssertion(Step.AssertionId))
        { return FString::Printf(TEXT("Required assertion provider '%s' is unavailable"), *Step.AssertionId.ToString()); }
        const uint8 Fields = FieldsFor(Step);
        if ((Fields & Probe::TargetField) && Step.Target.TrimStartAndEnd().IsEmpty()) { return TEXT("Choose a Target"); }
        if ((Fields & Probe::ClassField) && Step.ExpectedClass.TrimStartAndEnd().IsEmpty()) { return TEXT("Choose the expected class"); }
        if ((Fields & Probe::PropertyField) && Step.PropertyName.TrimStartAndEnd().IsEmpty()) { return TEXT("Enter a Property, Function, or Tag name"); }
        // Empty text is a legitimate expected value for equality/inequality
        // on text widgets, including an intentionally cleared Details search.
        const bool bEmptyExpectedIsValid = Step.AssertionId.IsNone()
            && ProbeNativeUIPolicy::AllowsEmptyValue(Step.Type);
        if ((Fields & Probe::ValueField) && Step.ExpectedValue.TrimStartAndEnd().IsEmpty() && !bEmptyExpectedIsValid)
        { return TEXT("Enter an expected value or search text"); }
        if (Step.AssertionId.IsNone() &&
            (Step.Type == EProbeStepType::ActorFixtureSetLocation
             || Step.Type == EProbeStepType::ActorFixtureLocationNearlyEquals
             || Step.Type == EProbeStepType::ActorFixtureLocationNotNearlyEquals
             || Step.Type == EProbeStepType::ActorFixtureSetRotation
             || Step.Type == EProbeStepType::ActorFixtureRotationNearlyEquals
             || Step.Type == EProbeStepType::ActorFixtureRotationNotNearlyEquals
             || Step.Type == EProbeStepType::ActorFixtureSetScale
             || Step.Type == EProbeStepType::ActorFixtureScaleNearlyEquals
             || Step.Type == EProbeStepType::ActorFixtureScaleNotNearlyEquals))
        {
            TArray<FString> Components;
            Step.ExpectedValue.ParseIntoArray(Components, TEXT(","), false);
            if (Components.Num() != 3) { return TEXT("Enter three comma-separated numbers, e.g. 100,0,50"); }
            for (const FString& Component : Components)
            {
                const FString Trimmed = Component.TrimStartAndEnd();
                double Number = 0.0;
                if (!LexTryParseString(Number, *Trimmed) || !FMath::IsFinite(Number))
                { return TEXT("Each vector or rotation component must be a finite number"); }
            }
        }
        if (Step.AssertionId.IsNone() &&
            ProbeNativeUIPolicy::HasTarget(Step.Type)
            && !Step.Target.Equals(TEXT("details:search"), ESearchCase::IgnoreCase)
            && !(Step.Target.StartsWith(TEXT("id:"), ESearchCase::IgnoreCase)
                && Step.Target.Len() > 3 && !Step.Target.RightChop(3).TrimStartAndEnd().IsEmpty()))
        { return TEXT("Use details:search or id:<Slate automation ID>, not screen coordinates"); }
        if (Step.AssertionId.IsNone() &&
            ProbeNativeUIPolicy::IsWait(Step.Type)
            && (!FMath::IsFinite(Step.UIWaitTimeoutSeconds)
                || Step.UIWaitTimeoutSeconds < 0.1 || Step.UIWaitTimeoutSeconds > 30.0))
        { return TEXT("Wait Timeout must be between 0.1 and 30 seconds"); }
        if (Step.Type == EProbeStepType::ObjectPropertyNumericInRange && Step.AssertionId.IsNone())
        {
            FString Lower, Upper;
            double Min = 0.0, Max = 0.0;
            if (!Step.ExpectedValue.Split(TEXT(","), &Lower, &Upper)
                || !LexTryParseString(Min, *Lower.TrimStartAndEnd())
                || !LexTryParseString(Max, *Upper.TrimStartAndEnd()) || Min > Max)
            { return TEXT("Expected range must be min,max with min <= max"); }
        }
        return FString();
    }
 }
